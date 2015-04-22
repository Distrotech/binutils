/* Branch trace support for GDB, the GNU debugger.

   Copyright (C) 2013-2015 Free Software Foundation, Inc.

   Contributed by Intel Corp. <markus.t.metzger@intel.com>

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

#include "defs.h"
#include "btrace.h"
#include "gdbthread.h"
#include "inferior.h"
#include "target.h"
#include "record.h"
#include "symtab.h"
#include "disasm.h"
#include "source.h"
#include "filenames.h"
#include "xml-support.h"
#include "regcache.h"

/* Print a record debug message.  Use do ... while (0) to avoid ambiguities
   when used in if statements.  */

#define DEBUG(msg, args...)						\
  do									\
    {									\
      if (record_debug != 0)						\
        fprintf_unfiltered (gdb_stdlog,					\
			    "[btrace] " msg "\n", ##args);		\
    }									\
  while (0)

#define DEBUG_FTRACE(msg, args...) DEBUG ("[ftrace] " msg, ##args)

/* Return the function name of a recorded function segment for printing.
   This function never returns NULL.  */

static const char *
ftrace_print_function_name (const struct btrace_function *bfun)
{
  struct minimal_symbol *msym;
  struct symbol *sym;

  msym = bfun->msym;
  sym = bfun->sym;

  if (sym != NULL)
    return SYMBOL_PRINT_NAME (sym);

  if (msym != NULL)
    return MSYMBOL_PRINT_NAME (msym);

  return "<unknown>";
}

/* Return the file name of a recorded function segment for printing.
   This function never returns NULL.  */

static const char *
ftrace_print_filename (const struct btrace_function *bfun)
{
  struct symbol *sym;
  const char *filename;

  sym = bfun->sym;

  if (sym != NULL)
    filename = symtab_to_filename_for_display (symbol_symtab (sym));
  else
    filename = "<unknown>";

  return filename;
}

/* Return a string representation of the address of an instruction.
   This function never returns NULL.  */

static const char *
ftrace_print_insn_addr (const struct btrace_insn *insn)
{
  if (insn == NULL)
    return "<nil>";

  return core_addr_to_string_nz (insn->pc);
}

/* Print an ftrace debug status message.  */

static void
ftrace_debug (const struct btrace_function *bfun, const char *prefix)
{
  const char *fun, *file;
  unsigned int ibegin, iend;
  int level;

  fun = ftrace_print_function_name (bfun);
  file = ftrace_print_filename (bfun);
  level = bfun->level;

  ibegin = bfun->insn_offset;
  iend = ibegin + VEC_length (btrace_insn_s, bfun->insn);

  DEBUG_FTRACE ("%s: fun = %s, file = %s, level = %d, insn = [%u; %u)",
		prefix, fun, file, level, ibegin, iend);
}

/* Return non-zero if BFUN does not match MFUN and FUN,
   return zero otherwise.  */

static int
ftrace_function_switched (const struct btrace_function *bfun,
			  const struct minimal_symbol *mfun,
			  const struct symbol *fun)
{
  struct minimal_symbol *msym;
  struct symbol *sym;

  msym = bfun->msym;
  sym = bfun->sym;

  /* If the minimal symbol changed, we certainly switched functions.  */
  if (mfun != NULL && msym != NULL
      && strcmp (MSYMBOL_LINKAGE_NAME (mfun), MSYMBOL_LINKAGE_NAME (msym)) != 0)
    return 1;

  /* If the symbol changed, we certainly switched functions.  */
  if (fun != NULL && sym != NULL)
    {
      const char *bfname, *fname;

      /* Check the function name.  */
      if (strcmp (SYMBOL_LINKAGE_NAME (fun), SYMBOL_LINKAGE_NAME (sym)) != 0)
	return 1;

      /* Check the location of those functions, as well.  */
      bfname = symtab_to_fullname (symbol_symtab (sym));
      fname = symtab_to_fullname (symbol_symtab (fun));
      if (filename_cmp (fname, bfname) != 0)
	return 1;
    }

  /* If we lost symbol information, we switched functions.  */
  if (!(msym == NULL && sym == NULL) && mfun == NULL && fun == NULL)
    return 1;

  /* If we gained symbol information, we switched functions.  */
  if (msym == NULL && sym == NULL && !(mfun == NULL && fun == NULL))
    return 1;

  return 0;
}

/* Allocate and initialize a new branch trace function segment.
   PREV is the chronologically preceding function segment.
   MFUN and FUN are the symbol information we have for this function.  */

static struct btrace_function *
ftrace_new_function (struct btrace_function *prev,
		     struct minimal_symbol *mfun,
		     struct symbol *fun)
{
  struct btrace_function *bfun;

  bfun = xzalloc (sizeof (*bfun));

  bfun->msym = mfun;
  bfun->sym = fun;
  bfun->flow.prev = prev;

  if (prev == NULL)
    {
      /* Start counting at one.  */
      bfun->number = 1;
      bfun->insn_offset = 1;
    }
  else
    {
      gdb_assert (prev->flow.next == NULL);
      prev->flow.next = bfun;

      bfun->number = prev->number + 1;
      bfun->insn_offset = (prev->insn_offset
			   + VEC_length (btrace_insn_s, prev->insn));
      bfun->level = prev->level;
    }

  return bfun;
}

/* Update the UP field of a function segment.  */

static void
ftrace_update_caller (struct btrace_function *bfun,
		      struct btrace_function *caller,
		      enum btrace_function_flag flags)
{
  if (bfun->up != NULL)
    ftrace_debug (bfun, "updating caller");

  bfun->up = caller;
  bfun->flags = flags;

  ftrace_debug (bfun, "set caller");
}

/* Fix up the caller for all segments of a function.  */

static void
ftrace_fixup_caller (struct btrace_function *bfun,
		     struct btrace_function *caller,
		     enum btrace_function_flag flags)
{
  struct btrace_function *prev, *next;

  ftrace_update_caller (bfun, caller, flags);

  /* Update all function segments belonging to the same function.  */
  for (prev = bfun->segment.prev; prev != NULL; prev = prev->segment.prev)
    ftrace_update_caller (prev, caller, flags);

  for (next = bfun->segment.next; next != NULL; next = next->segment.next)
    ftrace_update_caller (next, caller, flags);
}

/* Add a new function segment for a call.
   CALLER is the chronologically preceding function segment.
   MFUN and FUN are the symbol information we have for this function.  */

static struct btrace_function *
ftrace_new_call (struct btrace_function *caller,
		 struct minimal_symbol *mfun,
		 struct symbol *fun)
{
  struct btrace_function *bfun;

  bfun = ftrace_new_function (caller, mfun, fun);
  bfun->up = caller;
  bfun->level += 1;

  ftrace_debug (bfun, "new call");

  return bfun;
}

/* Add a new function segment for a tail call.
   CALLER is the chronologically preceding function segment.
   MFUN and FUN are the symbol information we have for this function.  */

static struct btrace_function *
ftrace_new_tailcall (struct btrace_function *caller,
		     struct minimal_symbol *mfun,
		     struct symbol *fun)
{
  struct btrace_function *bfun;

  bfun = ftrace_new_function (caller, mfun, fun);
  bfun->up = caller;
  bfun->level += 1;
  bfun->flags |= BFUN_UP_LINKS_TO_TAILCALL;

  ftrace_debug (bfun, "new tail call");

  return bfun;
}

/* Find the innermost caller in the back trace of BFUN with MFUN/FUN
   symbol information.  */

static struct btrace_function *
ftrace_find_caller (struct btrace_function *bfun,
		    struct minimal_symbol *mfun,
		    struct symbol *fun)
{
  for (; bfun != NULL; bfun = bfun->up)
    {
      /* Skip functions with incompatible symbol information.  */
      if (ftrace_function_switched (bfun, mfun, fun))
	continue;

      /* This is the function segment we're looking for.  */
      break;
    }

  return bfun;
}

/* Find the innermost caller in the back trace of BFUN, skipping all
   function segments that do not end with a call instruction (e.g.
   tail calls ending with a jump).  */

static struct btrace_function *
ftrace_find_call (struct btrace_function *bfun)
{
  for (; bfun != NULL; bfun = bfun->up)
    {
      struct btrace_insn *last;

      /* Skip gaps.  */
      if (bfun->errcode != 0)
	continue;

      last = VEC_last (btrace_insn_s, bfun->insn);

      if (last->iclass == BTRACE_INSN_CALL)
	break;
    }

  return bfun;
}

/* Add a continuation segment for a function into which we return.
   PREV is the chronologically preceding function segment.
   MFUN and FUN are the symbol information we have for this function.  */

static struct btrace_function *
ftrace_new_return (struct btrace_function *prev,
		   struct minimal_symbol *mfun,
		   struct symbol *fun)
{
  struct btrace_function *bfun, *caller;

  bfun = ftrace_new_function (prev, mfun, fun);

  /* It is important to start at PREV's caller.  Otherwise, we might find
     PREV itself, if PREV is a recursive function.  */
  caller = ftrace_find_caller (prev->up, mfun, fun);
  if (caller != NULL)
    {
      /* The caller of PREV is the preceding btrace function segment in this
	 function instance.  */
      gdb_assert (caller->segment.next == NULL);

      caller->segment.next = bfun;
      bfun->segment.prev = caller;

      /* Maintain the function level.  */
      bfun->level = caller->level;

      /* Maintain the call stack.  */
      bfun->up = caller->up;
      bfun->flags = caller->flags;

      ftrace_debug (bfun, "new return");
    }
  else
    {
      /* We did not find a caller.  This could mean that something went
	 wrong or that the call is simply not included in the trace.  */

      /* Let's search for some actual call.  */
      caller = ftrace_find_call (prev->up);
      if (caller == NULL)
	{
	  /* There is no call in PREV's back trace.  We assume that the
	     branch trace did not include it.  */

	  /* Let's find the topmost call function - this skips tail calls.  */
	  while (prev->up != NULL)
	    prev = prev->up;

	  /* We maintain levels for a series of returns for which we have
	     not seen the calls.
	     We start at the preceding function's level in case this has
	     already been a return for which we have not seen the call.
	     We start at level 0 otherwise, to handle tail calls correctly.  */
	  bfun->level = min (0, prev->level) - 1;

	  /* Fix up the call stack for PREV.  */
	  ftrace_fixup_caller (prev, bfun, BFUN_UP_LINKS_TO_RET);

	  ftrace_debug (bfun, "new return - no caller");
	}
      else
	{
	  /* There is a call in PREV's back trace to which we should have
	     returned.  Let's remain at this level.  */
	  bfun->level = prev->level;

	  ftrace_debug (bfun, "new return - unknown caller");
	}
    }

  return bfun;
}

/* Add a new function segment for a function switch.
   PREV is the chronologically preceding function segment.
   MFUN and FUN are the symbol information we have for this function.  */

static struct btrace_function *
ftrace_new_switch (struct btrace_function *prev,
		   struct minimal_symbol *mfun,
		   struct symbol *fun)
{
  struct btrace_function *bfun;

  /* This is an unexplained function switch.  The call stack will likely
     be wrong at this point.  */
  bfun = ftrace_new_function (prev, mfun, fun);

  ftrace_debug (bfun, "new switch");

  return bfun;
}

/* Add a new function segment for a gap in the trace due to a decode error.
   PREV is the chronologically preceding function segment.
   ERRCODE is the format-specific error code.  */

static struct btrace_function *
ftrace_new_gap (struct btrace_function *prev, int errcode)
{
  struct btrace_function *bfun;

  /* We hijack prev if it was empty.  */
  if (prev != NULL && prev->errcode == 0
      && VEC_empty (btrace_insn_s, prev->insn))
    bfun = prev;
  else
    bfun = ftrace_new_function (prev, NULL, NULL);

  bfun->errcode = errcode;

  ftrace_debug (bfun, "new gap");

  return bfun;
}

/* Update BFUN with respect to the instruction at PC.  This may create new
   function segments.
   Return the chronologically latest function segment, never NULL.  */

static struct btrace_function *
ftrace_update_function (struct btrace_function *bfun, CORE_ADDR pc)
{
  struct bound_minimal_symbol bmfun;
  struct minimal_symbol *mfun;
  struct symbol *fun;
  struct btrace_insn *last;

  /* Try to determine the function we're in.  We use both types of symbols
     to avoid surprises when we sometimes get a full symbol and sometimes
     only a minimal symbol.  */
  fun = find_pc_function (pc);
  bmfun = lookup_minimal_symbol_by_pc (pc);
  mfun = bmfun.minsym;

  if (fun == NULL && mfun == NULL)
    DEBUG_FTRACE ("no symbol at %s", core_addr_to_string_nz (pc));

  /* If we didn't have a function or if we had a gap before, we create one.  */
  if (bfun == NULL || bfun->errcode != 0)
    return ftrace_new_function (bfun, mfun, fun);

  /* Check the last instruction, if we have one.
     We do this check first, since it allows us to fill in the call stack
     links in addition to the normal flow links.  */
  last = NULL;
  if (!VEC_empty (btrace_insn_s, bfun->insn))
    last = VEC_last (btrace_insn_s, bfun->insn);

  if (last != NULL)
    {
      switch (last->iclass)
	{
	case BTRACE_INSN_RETURN:
	  {
	    const char *fname;

	    /* On some systems, _dl_runtime_resolve returns to the resolved
	       function instead of jumping to it.  From our perspective,
	       however, this is a tailcall.
	       If we treated it as return, we wouldn't be able to find the
	       resolved function in our stack back trace.  Hence, we would
	       lose the current stack back trace and start anew with an empty
	       back trace.  When the resolved function returns, we would then
	       create a stack back trace with the same function names but
	       different frame id's.  This will confuse stepping.  */
	    fname = ftrace_print_function_name (bfun);
	    if (strcmp (fname, "_dl_runtime_resolve") == 0)
	      return ftrace_new_tailcall (bfun, mfun, fun);

	    return ftrace_new_return (bfun, mfun, fun);
	  }

	case BTRACE_INSN_CALL:
	  /* Ignore calls to the next instruction.  They are used for PIC.  */
	  if (last->pc + last->size == pc)
	    break;

	  return ftrace_new_call (bfun, mfun, fun);

	case BTRACE_INSN_JUMP:
	  {
	    CORE_ADDR start;

	    start = get_pc_function_start (pc);

	    /* If we can't determine the function for PC, we treat a jump at
	       the end of the block as tail call.  */
	    if (start == 0 || start == pc)
	      return ftrace_new_tailcall (bfun, mfun, fun);
	  }
	}
    }

  /* Check if we're switching functions for some other reason.  */
  if (ftrace_function_switched (bfun, mfun, fun))
    {
      DEBUG_FTRACE ("switching from %s in %s at %s",
		    ftrace_print_insn_addr (last),
		    ftrace_print_function_name (bfun),
		    ftrace_print_filename (bfun));

      return ftrace_new_switch (bfun, mfun, fun);
    }

  return bfun;
}

/* Add the instruction at PC to BFUN's instructions.  */

static void
ftrace_update_insns (struct btrace_function *bfun,
		     const struct btrace_insn *insn)
{
  VEC_safe_push (btrace_insn_s, bfun->insn, insn);

  if (record_debug > 1)
    ftrace_debug (bfun, "update insn");
}

/* Classify the instruction at PC.  */

static enum btrace_insn_class
ftrace_classify_insn (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  enum btrace_insn_class iclass;

  iclass = BTRACE_INSN_OTHER;
  TRY
    {
      if (gdbarch_insn_is_call (gdbarch, pc))
	iclass = BTRACE_INSN_CALL;
      else if (gdbarch_insn_is_ret (gdbarch, pc))
	iclass = BTRACE_INSN_RETURN;
      else if (gdbarch_insn_is_jump (gdbarch, pc))
	iclass = BTRACE_INSN_JUMP;
    }
  CATCH (error, RETURN_MASK_ERROR)
    {
    }
  END_CATCH

  return iclass;
}

/* Compute the function branch trace from BTS trace.  */

static void
btrace_compute_ftrace_bts (struct thread_info *tp,
			   const struct btrace_data_bts *btrace)
{
  struct btrace_thread_info *btinfo;
  struct btrace_function *begin, *end;
  struct gdbarch *gdbarch;
  unsigned int blk, ngaps;
  int level;

  gdbarch = target_gdbarch ();
  btinfo = &tp->btrace;
  begin = btinfo->begin;
  end = btinfo->end;
  ngaps = btinfo->ngaps;
  level = begin != NULL ? -btinfo->level : INT_MAX;
  blk = VEC_length (btrace_block_s, btrace->blocks);

  while (blk != 0)
    {
      btrace_block_s *block;
      CORE_ADDR pc;

      blk -= 1;

      block = VEC_index (btrace_block_s, btrace->blocks, blk);
      pc = block->begin;

      for (;;)
	{
	  struct btrace_insn insn;
	  int size;

	  /* We should hit the end of the block.  Warn if we went too far.  */
	  if (block->end < pc)
	    {
	      /* Indicate the gap in the trace - unless we're at the
		 beginning.  */
	      if (begin != NULL)
		{
		  warning (_("Recorded trace may be corrupted around %s."),
			   core_addr_to_string_nz (pc));

		  end = ftrace_new_gap (end, BDE_BTS_OVERFLOW);
		  ngaps += 1;
		}
	      break;
	    }

	  end = ftrace_update_function (end, pc);
	  if (begin == NULL)
	    begin = end;

	  /* Maintain the function level offset.
	     For all but the last block, we do it here.  */
	  if (blk != 0)
	    level = min (level, end->level);

	  size = 0;
	  TRY
	    {
	      size = gdb_insn_length (gdbarch, pc);
	    }
	  CATCH (error, RETURN_MASK_ERROR)
	    {
	    }
	  END_CATCH

	  insn.pc = pc;
	  insn.size = size;
	  insn.iclass = ftrace_classify_insn (gdbarch, pc);

	  ftrace_update_insns (end, &insn);

	  /* We're done once we pushed the instruction at the end.  */
	  if (block->end == pc)
	    break;

	  /* We can't continue if we fail to compute the size.  */
	  if (size <= 0)
	    {
	      warning (_("Recorded trace may be incomplete around %s."),
		       core_addr_to_string_nz (pc));

	      /* Indicate the gap in the trace.  We just added INSN so we're
		 not at the beginning.  */
	      end = ftrace_new_gap (end, BDE_BTS_INSN_SIZE);
	      ngaps += 1;

	      break;
	    }

	  pc += size;

	  /* Maintain the function level offset.
	     For the last block, we do it here to not consider the last
	     instruction.
	     Since the last instruction corresponds to the current instruction
	     and is not really part of the execution history, it shouldn't
	     affect the level.  */
	  if (blk == 0)
	    level = min (level, end->level);
	}
    }

  btinfo->begin = begin;
  btinfo->end = end;
  btinfo->ngaps = ngaps;

  /* LEVEL is the minimal function level of all btrace function segments.
     Define the global level offset to -LEVEL so all function levels are
     normalized to start at zero.  */
  btinfo->level = -level;
}

/* Compute the function branch trace from a block branch trace BTRACE for
   a thread given by BTINFO.  */

static void
btrace_compute_ftrace (struct thread_info *tp, struct btrace_data *btrace)
{
  DEBUG ("compute ftrace");

  switch (btrace->format)
    {
    case BTRACE_FORMAT_NONE:
      return;

    case BTRACE_FORMAT_BTS:
      btrace_compute_ftrace_bts (tp, &btrace->variant.bts);
      return;
    }

  internal_error (__FILE__, __LINE__, _("Unkown branch trace format."));
}

/* Add an entry for the current PC.  */

static void
btrace_add_pc (struct thread_info *tp)
{
  struct btrace_data btrace;
  struct btrace_block *block;
  struct regcache *regcache;
  struct cleanup *cleanup;
  CORE_ADDR pc;

  regcache = get_thread_regcache (tp->ptid);
  pc = regcache_read_pc (regcache);

  btrace_data_init (&btrace);
  btrace.format = BTRACE_FORMAT_BTS;
  btrace.variant.bts.blocks = NULL;

  cleanup = make_cleanup_btrace_data (&btrace);

  block = VEC_safe_push (btrace_block_s, btrace.variant.bts.blocks, NULL);
  block->begin = pc;
  block->end = pc;

  btrace_compute_ftrace (tp, &btrace);

  do_cleanups (cleanup);
}

/* See btrace.h.  */

void
btrace_enable (struct thread_info *tp, const struct btrace_config *conf)
{
  if (tp->btrace.target != NULL)
    return;

  if (!target_supports_btrace (conf->format))
    error (_("Target does not support branch tracing."));

  DEBUG ("enable thread %d (%s)", tp->num, target_pid_to_str (tp->ptid));

  tp->btrace.target = target_enable_btrace (tp->ptid, conf);

  /* Add an entry for the current PC so we start tracing from where we
     enabled it.  */
  if (tp->btrace.target != NULL)
    btrace_add_pc (tp);
}

/* See btrace.h.  */

const struct btrace_config *
btrace_conf (const struct btrace_thread_info *btinfo)
{
  if (btinfo->target == NULL)
    return NULL;

  return target_btrace_conf (btinfo->target);
}

/* See btrace.h.  */

void
btrace_disable (struct thread_info *tp)
{
  struct btrace_thread_info *btp = &tp->btrace;
  int errcode = 0;

  if (btp->target == NULL)
    return;

  DEBUG ("disable thread %d (%s)", tp->num, target_pid_to_str (tp->ptid));

  target_disable_btrace (btp->target);
  btp->target = NULL;

  btrace_clear (tp);
}

/* See btrace.h.  */

void
btrace_teardown (struct thread_info *tp)
{
  struct btrace_thread_info *btp = &tp->btrace;
  int errcode = 0;

  if (btp->target == NULL)
    return;

  DEBUG ("teardown thread %d (%s)", tp->num, target_pid_to_str (tp->ptid));

  target_teardown_btrace (btp->target);
  btp->target = NULL;

  btrace_clear (tp);
}

/* Stitch branch trace in BTS format.  */

static int
btrace_stitch_bts (struct btrace_data_bts *btrace, struct thread_info *tp)
{
  struct btrace_thread_info *btinfo;
  struct btrace_function *last_bfun;
  struct btrace_insn *last_insn;
  btrace_block_s *first_new_block;

  btinfo = &tp->btrace;
  last_bfun = btinfo->end;
  gdb_assert (last_bfun != NULL);
  gdb_assert (!VEC_empty (btrace_block_s, btrace->blocks));

  /* If the existing trace ends with a gap, we just glue the traces
     together.  We need to drop the last (i.e. chronologically first) block
     of the new trace,  though, since we can't fill in the start address.*/
  if (VEC_empty (btrace_insn_s, last_bfun->insn))
    {
      VEC_pop (btrace_block_s, btrace->blocks);
      return 0;
    }

  /* Beware that block trace starts with the most recent block, so the
     chronologically first block in the new trace is the last block in
     the new trace's block vector.  */
  first_new_block = VEC_last (btrace_block_s, btrace->blocks);
  last_insn = VEC_last (btrace_insn_s, last_bfun->insn);

  /* If the current PC at the end of the block is the same as in our current
     trace, there are two explanations:
       1. we executed the instruction and some branch brought us back.
       2. we have not made any progress.
     In the first case, the delta trace vector should contain at least two
     entries.
     In the second case, the delta trace vector should contain exactly one
     entry for the partial block containing the current PC.  Remove it.  */
  if (first_new_block->end == last_insn->pc
      && VEC_length (btrace_block_s, btrace->blocks) == 1)
    {
      VEC_pop (btrace_block_s, btrace->blocks);
      return 0;
    }

  DEBUG ("stitching %s to %s", ftrace_print_insn_addr (last_insn),
	 core_addr_to_string_nz (first_new_block->end));

  /* Do a simple sanity check to make sure we don't accidentally end up
     with a bad block.  This should not occur in practice.  */
  if (first_new_block->end < last_insn->pc)
    {
      warning (_("Error while trying to read delta trace.  Falling back to "
		 "a full read."));
      return -1;
    }

  /* We adjust the last block to start at the end of our current trace.  */
  gdb_assert (first_new_block->begin == 0);
  first_new_block->begin = last_insn->pc;

  /* We simply pop the last insn so we can insert it again as part of
     the normal branch trace computation.
     Since instruction iterators are based on indices in the instructions
     vector, we don't leave any pointers dangling.  */
  DEBUG ("pruning insn at %s for stitching",
	 ftrace_print_insn_addr (last_insn));

  VEC_pop (btrace_insn_s, last_bfun->insn);

  /* The instructions vector may become empty temporarily if this has
     been the only instruction in this function segment.
     This violates the invariant but will be remedied shortly by
     btrace_compute_ftrace when we add the new trace.  */

  /* The only case where this would hurt is if the entire trace consisted
     of just that one instruction.  If we remove it, we might turn the now
     empty btrace function segment into a gap.  But we don't want gaps at
     the beginning.  To avoid this, we remove the entire old trace.  */
  if (last_bfun == btinfo->begin && VEC_empty (btrace_insn_s, last_bfun->insn))
    btrace_clear (tp);

  return 0;
}

/* Adjust the block trace in order to stitch old and new trace together.
   BTRACE is the new delta trace between the last and the current stop.
   TP is the traced thread.
   May modifx BTRACE as well as the existing trace in TP.
   Return 0 on success, -1 otherwise.  */

static int
btrace_stitch_trace (struct btrace_data *btrace, struct thread_info *tp)
{
  /* If we don't have trace, there's nothing to do.  */
  if (btrace_data_empty (btrace))
    return 0;

  switch (btrace->format)
    {
    case BTRACE_FORMAT_NONE:
      return 0;

    case BTRACE_FORMAT_BTS:
      return btrace_stitch_bts (&btrace->variant.bts, tp);
    }

  internal_error (__FILE__, __LINE__, _("Unkown branch trace format."));
}

/* Clear the branch trace histories in BTINFO.  */

static void
btrace_clear_history (struct btrace_thread_info *btinfo)
{
  xfree (btinfo->insn_history);
  xfree (btinfo->call_history);
  xfree (btinfo->replay);

  btinfo->insn_history = NULL;
  btinfo->call_history = NULL;
  btinfo->replay = NULL;
}

/* See btrace.h.  */

void
btrace_fetch (struct thread_info *tp)
{
  struct btrace_thread_info *btinfo;
  struct btrace_target_info *tinfo;
  struct btrace_data btrace;
  struct cleanup *cleanup;
  int errcode;

  DEBUG ("fetch thread %d (%s)", tp->num, target_pid_to_str (tp->ptid));

  btinfo = &tp->btrace;
  tinfo = btinfo->target;
  if (tinfo == NULL)
    return;

  /* There's no way we could get new trace while replaying.
     On the other hand, delta trace would return a partial record with the
     current PC, which is the replay PC, not the last PC, as expected.  */
  if (btinfo->replay != NULL)
    return;

  btrace_data_init (&btrace);
  cleanup = make_cleanup_btrace_data (&btrace);

  /* Let's first try to extend the trace we already have.  */
  if (btinfo->end != NULL)
    {
      errcode = target_read_btrace (&btrace, tinfo, BTRACE_READ_DELTA);
      if (errcode == 0)
	{
	  /* Success.  Let's try to stitch the traces together.  */
	  errcode = btrace_stitch_trace (&btrace, tp);
	}
      else
	{
	  /* We failed to read delta trace.  Let's try to read new trace.  */
	  errcode = target_read_btrace (&btrace, tinfo, BTRACE_READ_NEW);

	  /* If we got any new trace, discard what we have.  */
	  if (errcode == 0 && !btrace_data_empty (&btrace))
	    btrace_clear (tp);
	}

      /* If we were not able to read the trace, we start over.  */
      if (errcode != 0)
	{
	  btrace_clear (tp);
	  errcode = target_read_btrace (&btrace, tinfo, BTRACE_READ_ALL);
	}
    }
  else
    errcode = target_read_btrace (&btrace, tinfo, BTRACE_READ_ALL);

  /* If we were not able to read the branch trace, signal an error.  */
  if (errcode != 0)
    error (_("Failed to read branch trace."));

  /* Compute the trace, provided we have any.  */
  if (!btrace_data_empty (&btrace))
    {
      btrace_clear_history (btinfo);
      btrace_compute_ftrace (tp, &btrace);
    }

  do_cleanups (cleanup);
}

/* See btrace.h.  */

void
btrace_clear (struct thread_info *tp)
{
  struct btrace_thread_info *btinfo;
  struct btrace_function *it, *trash;

  DEBUG ("clear thread %d (%s)", tp->num, target_pid_to_str (tp->ptid));

  /* Make sure btrace frames that may hold a pointer into the branch
     trace data are destroyed.  */
  reinit_frame_cache ();

  btinfo = &tp->btrace;

  it = btinfo->begin;
  while (it != NULL)
    {
      trash = it;
      it = it->flow.next;

      xfree (trash);
    }

  btinfo->begin = NULL;
  btinfo->end = NULL;
  btinfo->ngaps = 0;

  btrace_clear_history (btinfo);
}

/* See btrace.h.  */

void
btrace_free_objfile (struct objfile *objfile)
{
  struct thread_info *tp;

  DEBUG ("free objfile");

  ALL_NON_EXITED_THREADS (tp)
    btrace_clear (tp);
}

#if defined (HAVE_LIBEXPAT)

/* Check the btrace document version.  */

static void
check_xml_btrace_version (struct gdb_xml_parser *parser,
			  const struct gdb_xml_element *element,
			  void *user_data, VEC (gdb_xml_value_s) *attributes)
{
  const char *version = xml_find_attribute (attributes, "version")->value;

  if (strcmp (version, "1.0") != 0)
    gdb_xml_error (parser, _("Unsupported btrace version: \"%s\""), version);
}

/* Parse a btrace "block" xml record.  */

static void
parse_xml_btrace_block (struct gdb_xml_parser *parser,
			const struct gdb_xml_element *element,
			void *user_data, VEC (gdb_xml_value_s) *attributes)
{
  struct btrace_data *btrace;
  struct btrace_block *block;
  ULONGEST *begin, *end;

  btrace = user_data;

  switch (btrace->format)
    {
    case BTRACE_FORMAT_BTS:
      break;

    case BTRACE_FORMAT_NONE:
      btrace->format = BTRACE_FORMAT_BTS;
      btrace->variant.bts.blocks = NULL;
      break;

    default:
      gdb_xml_error (parser, _("Btrace format error."));
    }

  begin = xml_find_attribute (attributes, "begin")->value;
  end = xml_find_attribute (attributes, "end")->value;

  block = VEC_safe_push (btrace_block_s, btrace->variant.bts.blocks, NULL);
  block->begin = *begin;
  block->end = *end;
}

static const struct gdb_xml_attribute block_attributes[] = {
  { "begin", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { "end", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute btrace_attributes[] = {
  { "version", GDB_XML_AF_NONE, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element btrace_children[] = {
  { "block", block_attributes, NULL,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL, parse_xml_btrace_block, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static const struct gdb_xml_element btrace_elements[] = {
  { "btrace", btrace_attributes, btrace_children, GDB_XML_EF_NONE,
    check_xml_btrace_version, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

#endif /* defined (HAVE_LIBEXPAT) */

/* See btrace.h.  */

void
parse_xml_btrace (struct btrace_data *btrace, const char *buffer)
{
  struct cleanup *cleanup;
  int errcode;

#if defined (HAVE_LIBEXPAT)

  btrace->format = BTRACE_FORMAT_NONE;

  cleanup = make_cleanup_btrace_data (btrace);
  errcode = gdb_xml_parse_quick (_("btrace"), "btrace.dtd", btrace_elements,
				 buffer, btrace);
  if (errcode != 0)
    error (_("Error parsing branch trace."));

  /* Keep parse results.  */
  discard_cleanups (cleanup);

#else  /* !defined (HAVE_LIBEXPAT) */

  error (_("Cannot process branch trace.  XML parsing is not supported."));

#endif  /* !defined (HAVE_LIBEXPAT) */
}

#if defined (HAVE_LIBEXPAT)

/* Parse a btrace-conf "bts" xml record.  */

static void
parse_xml_btrace_conf_bts (struct gdb_xml_parser *parser,
			  const struct gdb_xml_element *element,
			  void *user_data, VEC (gdb_xml_value_s) *attributes)
{
  struct btrace_config *conf;
  struct gdb_xml_value *size;

  conf = user_data;
  conf->format = BTRACE_FORMAT_BTS;
  conf->bts.size = 0;

  size = xml_find_attribute (attributes, "size");
  if (size != NULL)
    conf->bts.size = (unsigned int) * (ULONGEST *) size->value;
}

static const struct gdb_xml_attribute btrace_conf_bts_attributes[] = {
  { "size", GDB_XML_AF_OPTIONAL, gdb_xml_parse_attr_ulongest, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element btrace_conf_children[] = {
  { "bts", btrace_conf_bts_attributes, NULL, GDB_XML_EF_OPTIONAL,
    parse_xml_btrace_conf_bts, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute btrace_conf_attributes[] = {
  { "version", GDB_XML_AF_NONE, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element btrace_conf_elements[] = {
  { "btrace-conf", btrace_conf_attributes, btrace_conf_children,
    GDB_XML_EF_NONE, NULL, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

#endif /* defined (HAVE_LIBEXPAT) */

/* See btrace.h.  */

void
parse_xml_btrace_conf (struct btrace_config *conf, const char *xml)
{
  int errcode;

#if defined (HAVE_LIBEXPAT)

  errcode = gdb_xml_parse_quick (_("btrace-conf"), "btrace-conf.dtd",
				 btrace_conf_elements, xml, conf);
  if (errcode != 0)
    error (_("Error parsing branch trace configuration."));

#else  /* !defined (HAVE_LIBEXPAT) */

  error (_("XML parsing is not supported."));

#endif  /* !defined (HAVE_LIBEXPAT) */
}

/* See btrace.h.  */

const struct btrace_insn *
btrace_insn_get (const struct btrace_insn_iterator *it)
{
  const struct btrace_function *bfun;
  unsigned int index, end;

  index = it->index;
  bfun = it->function;

  /* Check if the iterator points to a gap in the trace.  */
  if (bfun->errcode != 0)
    return NULL;

  /* The index is within the bounds of this function's instruction vector.  */
  end = VEC_length (btrace_insn_s, bfun->insn);
  gdb_assert (0 < end);
  gdb_assert (index < end);

  return VEC_index (btrace_insn_s, bfun->insn, index);
}

/* See btrace.h.  */

unsigned int
btrace_insn_number (const struct btrace_insn_iterator *it)
{
  const struct btrace_function *bfun;

  bfun = it->function;

  /* Return zero if the iterator points to a gap in the trace.  */
  if (bfun->errcode != 0)
    return 0;

  return bfun->insn_offset + it->index;
}

/* See btrace.h.  */

void
btrace_insn_begin (struct btrace_insn_iterator *it,
		   const struct btrace_thread_info *btinfo)
{
  const struct btrace_function *bfun;

  bfun = btinfo->begin;
  if (bfun == NULL)
    error (_("No trace."));

  it->function = bfun;
  it->index = 0;
}

/* See btrace.h.  */

void
btrace_insn_end (struct btrace_insn_iterator *it,
		 const struct btrace_thread_info *btinfo)
{
  const struct btrace_function *bfun;
  unsigned int length;

  bfun = btinfo->end;
  if (bfun == NULL)
    error (_("No trace."));

  length = VEC_length (btrace_insn_s, bfun->insn);

  /* The last function may either be a gap or it contains the current
     instruction, which is one past the end of the execution trace; ignore
     it.  */
  if (length > 0)
    length -= 1;

  it->function = bfun;
  it->index = length;
}

/* See btrace.h.  */

unsigned int
btrace_insn_next (struct btrace_insn_iterator *it, unsigned int stride)
{
  const struct btrace_function *bfun;
  unsigned int index, steps;

  bfun = it->function;
  steps = 0;
  index = it->index;

  while (stride != 0)
    {
      unsigned int end, space, adv;

      end = VEC_length (btrace_insn_s, bfun->insn);

      /* An empty function segment represents a gap in the trace.  We count
	 it as one instruction.  */
      if (end == 0)
	{
	  const struct btrace_function *next;

	  next = bfun->flow.next;
	  if (next == NULL)
	    break;

	  stride -= 1;
	  steps += 1;

	  bfun = next;
	  index = 0;

	  continue;
	}

      gdb_assert (0 < end);
      gdb_assert (index < end);

      /* Compute the number of instructions remaining in this segment.  */
      space = end - index;

      /* Advance the iterator as far as possible within this segment.  */
      adv = min (space, stride);
      stride -= adv;
      index += adv;
      steps += adv;

      /* Move to the next function if we're at the end of this one.  */
      if (index == end)
	{
	  const struct btrace_function *next;

	  next = bfun->flow.next;
	  if (next == NULL)
	    {
	      /* We stepped past the last function.

		 Let's adjust the index to point to the last instruction in
		 the previous function.  */
	      index -= 1;
	      steps -= 1;
	      break;
	    }

	  /* We now point to the first instruction in the new function.  */
	  bfun = next;
	  index = 0;
	}

      /* We did make progress.  */
      gdb_assert (adv > 0);
    }

  /* Update the iterator.  */
  it->function = bfun;
  it->index = index;

  return steps;
}

/* See btrace.h.  */

unsigned int
btrace_insn_prev (struct btrace_insn_iterator *it, unsigned int stride)
{
  const struct btrace_function *bfun;
  unsigned int index, steps;

  bfun = it->function;
  steps = 0;
  index = it->index;

  while (stride != 0)
    {
      unsigned int adv;

      /* Move to the previous function if we're at the start of this one.  */
      if (index == 0)
	{
	  const struct btrace_function *prev;

	  prev = bfun->flow.prev;
	  if (prev == NULL)
	    break;

	  /* We point to one after the last instruction in the new function.  */
	  bfun = prev;
	  index = VEC_length (btrace_insn_s, bfun->insn);

	  /* An empty function segment represents a gap in the trace.  We count
	     it as one instruction.  */
	  if (index == 0)
	    {
	      stride -= 1;
	      steps += 1;

	      continue;
	    }
	}

      /* Advance the iterator as far as possible within this segment.  */
      adv = min (index, stride);

      stride -= adv;
      index -= adv;
      steps += adv;

      /* We did make progress.  */
      gdb_assert (adv > 0);
    }

  /* Update the iterator.  */
  it->function = bfun;
  it->index = index;

  return steps;
}

/* See btrace.h.  */

int
btrace_insn_cmp (const struct btrace_insn_iterator *lhs,
		 const struct btrace_insn_iterator *rhs)
{
  unsigned int lnum, rnum;

  lnum = btrace_insn_number (lhs);
  rnum = btrace_insn_number (rhs);

  /* A gap has an instruction number of zero.  Things are getting more
     complicated if gaps are involved.

     We take the instruction number offset from the iterator's function.
     This is the number of the first instruction after the gap.

     This is OK as long as both lhs and rhs point to gaps.  If only one of
     them does, we need to adjust the number based on the other's regular
     instruction number.  Otherwise, a gap might compare equal to an
     instruction.  */

  if (lnum == 0 && rnum == 0)
    {
      lnum = lhs->function->insn_offset;
      rnum = rhs->function->insn_offset;
    }
  else if (lnum == 0)
    {
      lnum = lhs->function->insn_offset;

      if (lnum == rnum)
	lnum -= 1;
    }
  else if (rnum == 0)
    {
      rnum = rhs->function->insn_offset;

      if (rnum == lnum)
	rnum -= 1;
    }

  return (int) (lnum - rnum);
}

/* See btrace.h.  */

int
btrace_find_insn_by_number (struct btrace_insn_iterator *it,
			    const struct btrace_thread_info *btinfo,
			    unsigned int number)
{
  const struct btrace_function *bfun;
  unsigned int end, length;

  for (bfun = btinfo->end; bfun != NULL; bfun = bfun->flow.prev)
    {
      /* Skip gaps. */
      if (bfun->errcode != 0)
	continue;

      if (bfun->insn_offset <= number)
	break;
    }

  if (bfun == NULL)
    return 0;

  length = VEC_length (btrace_insn_s, bfun->insn);
  gdb_assert (length > 0);

  end = bfun->insn_offset + length;
  if (end <= number)
    return 0;

  it->function = bfun;
  it->index = number - bfun->insn_offset;

  return 1;
}

/* See btrace.h.  */

const struct btrace_function *
btrace_call_get (const struct btrace_call_iterator *it)
{
  return it->function;
}

/* See btrace.h.  */

unsigned int
btrace_call_number (const struct btrace_call_iterator *it)
{
  const struct btrace_thread_info *btinfo;
  const struct btrace_function *bfun;
  unsigned int insns;

  btinfo = it->btinfo;
  bfun = it->function;
  if (bfun != NULL)
    return bfun->number;

  /* For the end iterator, i.e. bfun == NULL, we return one more than the
     number of the last function.  */
  bfun = btinfo->end;
  insns = VEC_length (btrace_insn_s, bfun->insn);

  /* If the function contains only a single instruction (i.e. the current
     instruction), it will be skipped and its number is already the number
     we seek.  */
  if (insns == 1)
    return bfun->number;

  /* Otherwise, return one more than the number of the last function.  */
  return bfun->number + 1;
}

/* See btrace.h.  */

void
btrace_call_begin (struct btrace_call_iterator *it,
		   const struct btrace_thread_info *btinfo)
{
  const struct btrace_function *bfun;

  bfun = btinfo->begin;
  if (bfun == NULL)
    error (_("No trace."));

  it->btinfo = btinfo;
  it->function = bfun;
}

/* See btrace.h.  */

void
btrace_call_end (struct btrace_call_iterator *it,
		 const struct btrace_thread_info *btinfo)
{
  const struct btrace_function *bfun;

  bfun = btinfo->end;
  if (bfun == NULL)
    error (_("No trace."));

  it->btinfo = btinfo;
  it->function = NULL;
}

/* See btrace.h.  */

unsigned int
btrace_call_next (struct btrace_call_iterator *it, unsigned int stride)
{
  const struct btrace_function *bfun;
  unsigned int steps;

  bfun = it->function;
  steps = 0;
  while (bfun != NULL)
    {
      const struct btrace_function *next;
      unsigned int insns;

      next = bfun->flow.next;
      if (next == NULL)
	{
	  /* Ignore the last function if it only contains a single
	     (i.e. the current) instruction.  */
	  insns = VEC_length (btrace_insn_s, bfun->insn);
	  if (insns == 1)
	    steps -= 1;
	}

      if (stride == steps)
	break;

      bfun = next;
      steps += 1;
    }

  it->function = bfun;
  return steps;
}

/* See btrace.h.  */

unsigned int
btrace_call_prev (struct btrace_call_iterator *it, unsigned int stride)
{
  const struct btrace_thread_info *btinfo;
  const struct btrace_function *bfun;
  unsigned int steps;

  bfun = it->function;
  steps = 0;

  if (bfun == NULL)
    {
      unsigned int insns;

      btinfo = it->btinfo;
      bfun = btinfo->end;
      if (bfun == NULL)
	return 0;

      /* Ignore the last function if it only contains a single
	 (i.e. the current) instruction.  */
      insns = VEC_length (btrace_insn_s, bfun->insn);
      if (insns == 1)
	bfun = bfun->flow.prev;

      if (bfun == NULL)
	return 0;

      steps += 1;
    }

  while (steps < stride)
    {
      const struct btrace_function *prev;

      prev = bfun->flow.prev;
      if (prev == NULL)
	break;

      bfun = prev;
      steps += 1;
    }

  it->function = bfun;
  return steps;
}

/* See btrace.h.  */

int
btrace_call_cmp (const struct btrace_call_iterator *lhs,
		 const struct btrace_call_iterator *rhs)
{
  unsigned int lnum, rnum;

  lnum = btrace_call_number (lhs);
  rnum = btrace_call_number (rhs);

  return (int) (lnum - rnum);
}

/* See btrace.h.  */

int
btrace_find_call_by_number (struct btrace_call_iterator *it,
			    const struct btrace_thread_info *btinfo,
			    unsigned int number)
{
  const struct btrace_function *bfun;

  for (bfun = btinfo->end; bfun != NULL; bfun = bfun->flow.prev)
    {
      unsigned int bnum;

      bnum = bfun->number;
      if (number == bnum)
	{
	  it->btinfo = btinfo;
	  it->function = bfun;
	  return 1;
	}

      /* Functions are ordered and numbered consecutively.  We could bail out
	 earlier.  On the other hand, it is very unlikely that we search for
	 a nonexistent function.  */
  }

  return 0;
}

/* See btrace.h.  */

void
btrace_set_insn_history (struct btrace_thread_info *btinfo,
			 const struct btrace_insn_iterator *begin,
			 const struct btrace_insn_iterator *end)
{
  if (btinfo->insn_history == NULL)
    btinfo->insn_history = xzalloc (sizeof (*btinfo->insn_history));

  btinfo->insn_history->begin = *begin;
  btinfo->insn_history->end = *end;
}

/* See btrace.h.  */

void
btrace_set_call_history (struct btrace_thread_info *btinfo,
			 const struct btrace_call_iterator *begin,
			 const struct btrace_call_iterator *end)
{
  gdb_assert (begin->btinfo == end->btinfo);

  if (btinfo->call_history == NULL)
    btinfo->call_history = xzalloc (sizeof (*btinfo->call_history));

  btinfo->call_history->begin = *begin;
  btinfo->call_history->end = *end;
}

/* See btrace.h.  */

int
btrace_is_replaying (struct thread_info *tp)
{
  return tp->btrace.replay != NULL;
}

/* See btrace.h.  */

int
btrace_is_empty (struct thread_info *tp)
{
  struct btrace_insn_iterator begin, end;
  struct btrace_thread_info *btinfo;

  btinfo = &tp->btrace;

  if (btinfo->begin == NULL)
    return 1;

  btrace_insn_begin (&begin, btinfo);
  btrace_insn_end (&end, btinfo);

  return btrace_insn_cmp (&begin, &end) == 0;
}

/* Forward the cleanup request.  */

static void
do_btrace_data_cleanup (void *arg)
{
  btrace_data_fini (arg);
}

/* See btrace.h.  */

struct cleanup *
make_cleanup_btrace_data (struct btrace_data *data)
{
  return make_cleanup (do_btrace_data_cleanup, data);
}
