/* GDB/Scheme charset interface.

   Copyright (C) 2014 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* See README file in this directory for implementation notes, coding
   conventions, et.al.  */

#include "defs.h"
#include <stdarg.h>
#include "charset.h"
#include "guile-internal.h"

/* Convert a C (latin1) string to an SCM string.
   "latin1" is chosen because Guile won't throw an exception.  */

SCM
gdbscm_scm_from_c_string (const char *string)
{
  return scm_from_latin1_string (string);
}

/* Convert an SCM string to a C (latin1) string.
   "latin1" is chosen because Guile won't throw an exception.
   Space for the result is allocated with malloc, caller must free.
   It is an error to call this if STRING is not a string.  */

char *
gdbscm_scm_to_c_string (SCM string)
{
  return scm_to_latin1_string (string);
}

/* Use printf to construct a Scheme string.  */

SCM
gdbscm_scm_from_printf (const char *format, ...)
{
  va_list args;
  char *string;
  SCM result;

  va_start (args, format);
  string = xstrvprintf (format, args);
  va_end (args);
  result = scm_from_latin1_string (string);
  xfree (string);

  return result;
}

/* Struct to pass data from gdbscm_scm_to_string to
   gdbscm_call_scm_to_stringn.  */

struct scm_to_stringn_data
{
  SCM string;
  size_t *lenp;
  const char *charset;
  int conversion_kind;
  char *result;
};

/* Helper for gdbscm_scm_to_string to call scm_to_stringn
   from within scm_c_catch.  */

static SCM
gdbscm_call_scm_to_stringn (void *datap)
{
  struct scm_to_stringn_data *data = datap;

  data->result = scm_to_stringn (data->string, data->lenp, data->charset,
				 data->conversion_kind);
  return SCM_BOOL_F;
}

/* Convert an SCM string to a string in charset CHARSET.
   This function is guaranteed to not throw an exception.
   If STRICT is non-zero, and there's a conversion error, then a
   <gdb:exception> object is stored in *EXCEPT_SCMP, and NULL is returned.
   If STRICT is zero, then escape sequences are used for characters that
   can't be converted, and EXCEPT_SCMP may be passed as NULL.
   Space for the result is allocated with malloc, caller must free.
   It is an error to call this if STRING is not a string.  */

char *
gdbscm_scm_to_string (SCM string, size_t *lenp,
		      const char *charset, int strict, SCM *except_scmp)
{
  struct scm_to_stringn_data data;
  SCM scm_result;

  data.string = string;
  data.lenp = lenp;
  data.charset = charset;
  data.conversion_kind = (strict
			  ? SCM_FAILED_CONVERSION_ERROR
			  : SCM_FAILED_CONVERSION_ESCAPE_SEQUENCE);
  data.result = NULL;

  scm_result = gdbscm_call_guile (gdbscm_call_scm_to_stringn, &data, NULL);

  if (gdbscm_is_false (scm_result))
    {
      gdb_assert (data.result != NULL);
      return data.result;
    }
  gdb_assert (gdbscm_is_exception (scm_result));
  *except_scmp = scm_result;
  return NULL;
}

/* Struct to pass data from gdbscm_scm_from_string to
   gdbscm_call_scm_from_stringn.  */

struct scm_from_stringn_data
{
  const char *string;
  size_t len;
  const char *charset;
  int conversion_kind;
  SCM result;
};

/* Helper for gdbscm_scm_from_string to call scm_from_stringn
   from within scm_c_catch.  */

static SCM
gdbscm_call_scm_from_stringn (void *datap)
{
  struct scm_from_stringn_data *data = datap;

  data->result = scm_from_stringn (data->string, data->len, data->charset,
				   data->conversion_kind);
  return SCM_BOOL_F;
}

/* Convert STRING to a Scheme string in charset CHARSET.
   This function is guaranteed to not throw an exception.
   If STRICT is non-zero, and there's a conversion error, then a
   <gdb:exception> object is returned.
   If STRICT is zero, then question marks are used for characters that
   can't be converted (limitation of underlying Guile conversion support).  */

SCM
gdbscm_scm_from_string (const char *string, size_t len,
			const char *charset, int strict)
{
  struct scm_from_stringn_data data;
  SCM scm_result;

  data.string = string;
  data.len = len;
  data.charset = charset;
  /* The use of SCM_FAILED_CONVERSION_QUESTION_MARK is specified by Guile.  */
  data.conversion_kind = (strict
			  ? SCM_FAILED_CONVERSION_ERROR
			  : SCM_FAILED_CONVERSION_QUESTION_MARK);
  data.result = SCM_UNDEFINED;

  scm_result = gdbscm_call_guile (gdbscm_call_scm_from_stringn, &data, NULL);

  if (gdbscm_is_false (scm_result))
    {
      gdb_assert (!SCM_UNBNDP (data.result));
      return data.result;
    }
  gdb_assert (gdbscm_is_exception (scm_result));
  return scm_result;
}

/* Convert an SCM string to a target string.
   This function will thrown a conversion error if there's a problem.
   Space for the result is allocated with malloc, caller must free.
   It is an error to call this if STRING is not a string.  */

char *
gdbscm_scm_to_target_string_unsafe (SCM string, size_t *lenp,
				    struct gdbarch *gdbarch)
{
  return scm_to_stringn (string, lenp, target_charset (gdbarch),
			 SCM_FAILED_CONVERSION_ERROR);
}

/* (string->argv string) -> list
   Return list of strings split up according to GDB's argv parsing rules.
   This is useful when writing GDB commands in Scheme.  */

static SCM
gdbscm_string_to_argv (SCM string_scm)
{
  char *string;
  char **c_argv;
  int i;
  SCM result = SCM_EOL;

  gdbscm_parse_function_args (FUNC_NAME, SCM_ARG1, NULL, "s",
			      string_scm, &string);

  if (string == NULL || *string == '\0')
    {
      xfree (string);
      return SCM_EOL;
    }

  c_argv = gdb_buildargv (string);
  for (i = 0; c_argv[i] != NULL; ++i)
    result = scm_cons (gdbscm_scm_from_c_string (c_argv[i]), result);

  freeargv (c_argv);
  xfree (string);

  return scm_reverse_x (result, SCM_EOL);
}

/* Initialize the Scheme charset interface to GDB.  */

static const scheme_function string_functions[] =
{
  { "string->argv", 1, 0, 0, gdbscm_string_to_argv,
  "\
Convert a string to a list of strings split up according to\n\
gdb's argv parsing rules." },

  END_FUNCTIONS
};

void
gdbscm_initialize_strings (void)
{
  gdbscm_define_functions (string_functions, 1);
}
