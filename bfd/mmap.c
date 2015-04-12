/* BFD library -- mmap of file descriptors.

   Copyright (C) 2015 Free Software Foundation, Inc.

   Hacked by Steve Chamberlain of Cygnus Support (steve@cygnus.com).

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "sysdep.h"

#ifdef HAVE_MMAP
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"
#include "bfd_stdint.h"

#include <sys/mman.h>

static file_ptr
mmap_btell (struct bfd *abfd)
{
  return abfd->where;
}

/* Copied and modified from gold_fallocate in gold.  */

static int
mmap_fallocate (int fd, file_ptr offset, file_ptr len)
{
#ifdef HAVE_FALLOCATE
  if (fallocate (fd, 0, offset, len) == 0)
    return 0;
#endif
#ifdef HAVE_POSIX_FALLOCATE
    return posix_fallocate (fd, offset, len);
#endif
  if (ftruncate (fd, offset + len) < 0)
    return errno;
  return 0;
}

static int
mmap_resize (bfd *abfd, file_ptr size)
{
  if (mmap_fallocate (abfd->u.mmap_fd, 0, size) != 0)
    {
syscall_error:
      bfd_set_error (bfd_error_system_call);
      abfd->io.mmap_size = 0;
      return -1;
    }
  if (abfd->io.mmap_size != 0)
    {
#ifdef HAVE_MREMAP
      abfd->iostream = mremap (abfd->iostream, abfd->io.mmap_size,
			       size, MREMAP_MAYMOVE);
      if (abfd->iostream == MAP_FAILED)
	goto syscall_error;
      else
	goto success;
#else
      if (munmap (abfd->iostream, abfd->io.mmap_size) != 0)
	goto syscall_error;
#endif
    }
  abfd->iostream = mmap (NULL, size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, abfd->u.mmap_fd, 0);
  if (abfd->iostream == MAP_FAILED)
    goto syscall_error;

success:
  abfd->io.mmap_size = size;
  return 0;
}

static int
mmap_bseek (bfd *abfd, file_ptr position, int direction)
{
  file_ptr nwhere;

  if (direction == SEEK_SET)
    nwhere = position;
  else
    nwhere = abfd->where + position;

  if (nwhere < 0)
    {
      abfd->where = 0;
      errno = EINVAL;
      return -1;
    }
  else if (nwhere >= abfd->io.mmap_size && mmap_resize (abfd, nwhere) != 0)
    return -1;

  return 0;
}

static file_ptr
mmap_bread (struct bfd *abfd, void *buf, file_ptr size)
{
  memcpy (buf, abfd->iostream + abfd->where, size);
  return size;
}

static file_ptr
mmap_bwrite (bfd *abfd, const void *ptr, file_ptr size)
{
  file_ptr filesize = abfd->where + size;

  if (filesize > abfd->io.mmap_size && mmap_resize (abfd, filesize) != 0)
    {
       bfd_set_error (bfd_error_system_call);
       return 0;
    }

  memcpy (abfd->iostream + abfd->where, ptr, size);
  return size;
}

static int
mmap_bclose (struct bfd *abfd)
{
  int status = munmap (abfd->iostream, abfd->io.mmap_size);
  if (status == 0)
    status = close (abfd->u.mmap_fd);
  if (status != 0)
    bfd_set_error (bfd_error_system_call);

  abfd->iostream = MAP_FAILED;
  abfd->io.mmap_size = 0;
  abfd->u.mmap_fd = -1;

  return status;
}

static int
mmap_bflush (struct bfd *abfd ATTRIBUTE_UNUSED)
{
#ifdef HAVE_MSYNC
  int status = msync (abfd->iostream, abfd->io.mmap_size, MS_SYNC);
  if (status != 0)
    bfd_set_error (bfd_error_system_call);
  return status;
#else
  return 0;
#endif
}

static int
mmap_bstat (struct bfd *abfd ATTRIBUTE_UNUSED, struct stat *sb)
{
  int status;
  status = fstat (abfd->u.mmap_fd, sb);
  if (status < 0)
    bfd_set_error (bfd_error_system_call);
  return status;
}

static void *
mmap_bmmap (struct bfd *abfd ATTRIBUTE_UNUSED,
	    void *addr ATTRIBUTE_UNUSED,
	    bfd_size_type len ATTRIBUTE_UNUSED,
	    int prot ATTRIBUTE_UNUSED,
	    int flags ATTRIBUTE_UNUSED,
	    file_ptr offset ATTRIBUTE_UNUSED,
	    void **map_addr ATTRIBUTE_UNUSED,
	    bfd_size_type *map_len ATTRIBUTE_UNUSED)
{
  /* Unsupported.  */
  abort ();
  return 0;
}

static const struct bfd_iovec mmap_iovec =
{
  &mmap_bread, &mmap_bwrite, &mmap_btell, &mmap_bseek,
  &mmap_bclose, &mmap_bflush, &mmap_bstat, &mmap_bmmap
};

static bfd_boolean
mmap_init (bfd *abfd, file_ptr size)
{
  if (abfd->u.mmap_fd != -1
      || abfd->io.mmap_size != 0
      || abfd->iostream == NULL)
    abort ();

  /* Only suport write before writing starts.  */
  if (abfd->direction != write_direction || abfd->output_has_begun)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  abfd->u.mmap_fd = dup (fileno (abfd->iostream));
  if (abfd->u.mmap_fd < 0)
    {
      bfd_set_error (bfd_error_system_call);
      return FALSE;
    }

  if (mmap_resize (abfd, size) != 0)
    {
      bfd_set_error (bfd_error_system_call);
      close (abfd->u.mmap_fd);
      abfd->u.mmap_fd = -1;
      return FALSE;
    }

  abfd->iovec = &mmap_iovec;
  bfd_cache_snip (abfd);
  return TRUE;
}
#endif

/*
INTERNAL_FUNCTION
	bfd_mmap_resize

SYNOPSIS
	bfd_boolean bfd_mmap_resize (bfd *abfd, file_ptr size);

DESCRIPTION
	Resize a BFD opened to write with mmap.
*/

bfd_boolean
bfd_mmap_resize (bfd *abfd ATTRIBUTE_UNUSED,
		 file_ptr size ATTRIBUTE_UNUSED)
{
#ifdef HAVE_MMAP
  if (abfd->u.mmap_fd == -1
      || abfd->io.mmap_size == 0
      || abfd->iostream == MAP_FAILED)
    return mmap_init (abfd, size);

  if (size > abfd->io.mmap_size && mmap_resize (abfd, size) != 0)
    {
      bfd_set_error (bfd_error_system_call);
      return FALSE;
    }
#endif

  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_mmape_close

SYNOPSIS
	bfd_boolean bfd_mmap_close (bfd *abfd);

DESCRIPTION
	Ummap the BFD @var{abfd} and close the attached file.

RETURNS
	<<FALSE>> is returned if closing the file fails, <<TRUE>> is
	returned if all is well.
*/

bfd_boolean
bfd_mmap_close (bfd *abfd ATTRIBUTE_UNUSED)
{
#ifdef HAVE_MMAP
  if (abfd->iovec != &mmap_iovec)
    return TRUE;

  if (abfd->iostream == MAP_FAILED)
    /* Previously closed.  */
    return TRUE;

   return mmap_bclose (abfd) == 0 ? TRUE : FALSE;
#else
   return TRUE;
#endif
}
