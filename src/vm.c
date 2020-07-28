/*
 * Copyright (c) 2015, 2017, 2018, 2020 Ian Fitchet <idf(at)idio-lang.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * vm.c
 *
 */

#include "idio.h"

/**
 * DOC: Some debugging aids.
 *
 * idio_vm_tracing reports the nominal function call and arguments and
 * return value.  You can enable/disable it in code with
 *
 * %%vm-trace {val}.
 *
 * Note that as you can start it deep inside some nested call
 * structure and have it continue to run until well outside that nest
 * then the indentation will be wrong.
 *
 *
 * idio_vm_dis reports the byte-instruction by byte-instruction flow.
 * You can enable/disable it in code with
 *
 * %%vm-dis {val}
 *
 * It is very^W verbose.
 *
 * You need the compile flag -DIDIO_DEBUG to use it.
 */
static int idio_vm_tracing = 0;
static char *idio_vm_tracing_in = ">>>>>>>>>>>>>>>>>>>>>>>>>";
static char *idio_vm_tracing_out = "<<<<<<<<<<<<<<<<<<<<<<<<<";
#ifdef IDIO_DEBUG
static int idio_vm_dis = 0;
#endif
FILE *idio_dasm_FILE;

/**
 * DOC:
 *
 * We don't know if some arbitrary code is going to set a global value
 * to be a closure.  If it does, we need to retain the code for the
 * closure.  Hence a global list of all known code.
 *
 * Prologue
 *
 * There is a prologue which defines some universal get-out behaviour
 * (from Queinnec).  idio_vm_FINISH_pc is the PC for the FINISH
 * instruction and idio_prologue_len how big the prologue is.
 *
 * In addition:
 *
 *   idio_vm_NCE_pc	NON-CONT-ERR
 *   idio_vm_CHR_pc	condition handler return
 *   idio_vm_IHR_pc	interrupt handler return
 *   idio_vm_AR_pc	apply return
 */
IDIO_IA_T idio_all_code;
idio_ai_t idio_vm_FINISH_pc;
idio_ai_t idio_vm_NCE_pc;
idio_ai_t idio_vm_CHR_pc;
idio_ai_t idio_vm_IHR_pc;
idio_ai_t idio_vm_AR_pc;
size_t idio_prologue_len;

int idio_vm_exit = 0;

/**
 * DOC: Some VM tables:
 *
 * constants - all known constants
 *
 *   we can't have numbers, strings, quoted values etc. embedded in
 *   compiled code -- as they are an unknown size which is harder work
 *   for a byte compiler -- but we can have a (known size) index into
 *   this table
 *
 *   symbols are also kept in this table.  symbols are a fixed size (a
 *   pointer) but they will have different values from compile to
 *   compile (and potentially from run to run).  So the symbol itself
 *   isn't an idempotent entity to be encoded for the VM and we must
 *   use a fixed index.
 *
 *   A symbol index in the following discussion is an index into the
 *   constants table.
 *
 * values - all known toplevel values
 *
 *   values is what the *VM* cares about and is the table of all
 *   toplevel values (lexical, dynamic, etc.) and the various
 *   -SET/-REF instructions indirect into this table by an index
 *   (usually a symbol index -- which requires further module-specific
 *   mapping to a value index).
 *
 *   The *evaluator* cares about symbols.  By and large it doesn't
 *   care about values although for primitives and macros it must set
 *   values as soon as it sees them because the macros will be run
 *   during evaluation and need primitives (and other macros) to be
 *   available.
 *
 *   Note: MODULES
 *
 *   Modules blur the easy view of the world as described above for
 *   two reasons.  Firstly, if two modules both define a variable
 *   using the same *symbol* then we must have two distinct *values*
 *   in the VM.  However, we don't know which one of the values we are
 *   meant to use until the time we try to reference it because we
 *   don't know which modules have been set as imports.  So the
 *   variable referencing instructions use a symbol index which,
 *   together with the current environment and imports, we can use to
 *   figure out which of the values we are refering to.
 *
 *   At least, having got a value index, we can memo-ize it for future
 *   use!
 *
 *   Secondly, modules suggest library code, ie. something we can
 *   write out and load in again later.  This causes a similar but
 *   different problem.  If I compile a module it will use the next
 *   available (global) symbol index of the ucrrently running Idio
 *   instance and encode that in the compiled output.  If I compile
 *   another module in a different Idio instance then, because it will
 *   use the same next available (global) symbol index, those symbol
 *   indexes are virtually certain to be in conflict.  Symbol index ci
 *   was used to mean one symbol in one module and a different symbol
 *   in the other.  When a VM instruction uses symbol index ci which
 *   *symbol* should we be using for lookup in any imported modules?
 *
 *   So we need a plan for those two circumstances:
 *
 *     constants/symbols
 *
 *     When a module is compiled it will (can only!) use the next
 *     available (global) constant/symbol index of the currently
 *     running Idio instance and encode that into the compiled output.
 *     This is not an issue until the module is saved.  A subsequent
 *     module, compiled in another Idio instance might also use the
 *     same next available (global) constant/symbol index.  That will
 *     then conflict with the first module when it is loaded in.
 *
 *     Therefore, all module constant/symbol indexes are regarded as
 *     module-specific and there exists module-specific tables
 *     (IDIO_MODULE_VCI()) to contain the module-specific index ->
 *     global index mappings.
 *
 *       A popular alternative is to rewrite the IDIO_A_* codes so
 *       that on the first pass the original index is read, the
 *       correct index is calculated then the VM instructions are
 *       rewritten to use the correct index directly.
 *
 *       Self-modifying code firstly requires that we ensure there is
 *       enough room in the space used by the original instructions to
 *       fit the new instructions (and any difference is handled) but
 *       also prevents us performing any kind of code-corruption (or
 *       malign influence) detection.
 *
 *     These two mappings are created when a module is loaded and the
 *     module-specifc constants/symbols are added to the VM:
 *     idio_vm_add_module_constants().
 *
 *       As a side-note: all Idio bootstrap code (and any interactivly
 *       input code) will not have been through
 *       idio_vm_add_module_constants() and therefore the mapping
 *       table will be empty.  As a consequence, for a given
 *       module-specific ci, if the mapping table is empty them we
 *       must assume that the ci is a global ci.
 *
 *     values
 *
 *     Values are a bit more tricky because the referencing
 *     instructions indicate which *symbol* we are trying to
 *     reference.  Once we've mapped the module-specific symbol index
 *     to a global one we then need to discover the instance of the
 *     *symbol* within our environment.  Modules are the problem here
 *     as we don't know whether a symbol has been defined in the
 *     current environment or a module it imports (or the Idio or
 *     *primitives* modules) until the time we attempt the reference.
 *
 *     Only then can we make a permanent link between the module's
 *     *symbol* reference and the VM's global *value* index.
 *
 *     Subsequent references get the quicker idio_module_get_vvi()
 *     result.
 *
 *       Again, a popular alternative is to rewrite the IDIO_A_*
 *       codes.  This would result in an even-quicker lookup -- a
 *       putative IDIO_A_GLOBAL_SYM_REF_DIRECT (global-value-index)
 *       which could be a simple array lookup -- much faster than a
 *       hash table lookup (as idio_module_get_vvi() is).
 *
 *     Naming Scheme
 *
 *     To try to maintain the sense of things name variables
 *     appropriately: mci/mvi and gci/gvi for the module-specific and
 *     global versions of constant/value indexes.
 *
 *     There'll be f... variants: fgci == idio_fixnum (gci).
 *
 * closure_name -
 *
 *   if we see GLOBAL-SYM-SET {name} {CLOS} we can maintain a map from
 *   {CLOS} back to the name the closure was once defined as.  This is
 *   handy during a trace when a {name} is redefined -- which happens
 *   a lot.  The trace always prints the closure's ID (memory
 *   location) so you can see if a recursive call is actually
 *   recursive or calling a previous definition (which may be
 *   recursive).
 *
 *   When a reflective evaluator is implemented this table should go
 *   (as the details will be indexes to constants and embedded in the
 *   code).
 *
 * continuations -
 *
 *   each call to idio_vm_run() adds a new {krun} to this stack which
 *   gives the restart/reset condition handlers something to go back
 *   to.
 */
IDIO idio_vm_constants;
static IDIO idio_vm_values;
IDIO idio_vm_krun;

static IDIO idio_vm_signal_handler_name;

static time_t idio_vm_t0;

static idio_ai_t idio_vm_get_or_create_vvi (idio_ai_t mci);

#ifdef IDIO_VM_PERF
static uint64_t idio_vm_ins_counters[IDIO_I_MAX];
static struct timespec idio_vm_ins_call_time[IDIO_I_MAX];
#endif

#define IDIO_THREAD_FETCH_NEXT()	(IDIO_IA_AE (idio_all_code, IDIO_THREAD_PC(thr)++))
#define IDIO_IA_GET_NEXT(xp)		(IDIO_IA_AE (idio_all_code, (*xp)++))
#define IDIO_THREAD_STACK_PUSH(v)	(idio_array_push (IDIO_THREAD_STACK(thr), v))
#define IDIO_THREAD_STACK_POP()		(idio_array_pop (IDIO_THREAD_STACK(thr)))

#define IDIO_VM_INVOKE_REGULAR_CALL	0
#define IDIO_VM_INVOKE_TAIL_CALL	1

void idio_vm_panic (IDIO thr, char *m)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * Not reached!
     *
     *
     * Ha!  Yeah, I wish! ... :(
     */

    fprintf (stderr, "\n\nPANIC: %s\n\n", m);
    idio_vm_debug (thr, "PANIC", 0);
    idio_final_vm();
    idio_exit_status = -1;
    idio_vm_restore_exit (idio_k_exit, idio_S_unspec);
    IDIO_C_ASSERT (0);
}

static void idio_vm_error_function_invoke (char *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);
    idio_display_C (": '", sh);
    idio_display (args, sh);
    idio_display_C ("'", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_function_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       location,
					       c_location));

    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_function_trace (IDIO_I ins, IDIO thr);
static void idio_vm_error_arity (IDIO_I ins, IDIO thr, size_t given, size_t arity, IDIO c_location)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, c_location);

    fprintf (stderr, "\n\nARITY != %zd\n", arity);
    idio_vm_function_trace (ins, thr);

    IDIO msh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "incorrect arity: %zd args for an arity-%zd function", given, arity);
    idio_display_C (em, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    IDIO func = IDIO_THREAD_FUNC (thr);

    IDIO val = IDIO_THREAD_VAL (thr);
    if (idio_isa_closure (func)) {
	IDIO name = idio_get_property (func, idio_KW_name, IDIO_LIST1 (idio_S_nil));
	IDIO sigstr = idio_get_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

	sprintf (em, "ARITY != %zd%s; closure (", arity, "");
	idio_display_C (em, dsh);
	idio_display (name, dsh);
	idio_display_C (" ", dsh);
	idio_display (sigstr, dsh);
	idio_display_C (") was called as (", dsh);
	idio_display (func, dsh);
	IDIO args = idio_frame_params_as_list (val);
	if (idio_S_nil != args) {
	    idio_display_C (" ", dsh);
	    char *s = idio_display_string (args);
	    idio_display_C_len (s + 1, strlen (s) - 2, dsh);
	    free (s);
	}
	idio_display_C (")", dsh);
    }

#ifdef IDIO_DEBUG
    idio_display_C (" ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh)));

    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_error_arity_varargs (IDIO_I ins, IDIO thr, size_t given, size_t arity, IDIO c_location)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, c_location);

    fprintf (stderr, "\n\nARITY < %zd\n", arity);
    idio_vm_function_trace (ins, thr);

    IDIO sh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "incorrect arity: %zd args for an arity-%zd+ function", given, arity);
    idio_display_C (em, sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       location,
					       c_location));

    idio_raise_condition (idio_S_true, c);
}

static void idio_error_runtime_unbound (IDIO fmci, IDIO fgci, IDIO sym, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no such binding: mci ", sh);
    idio_display (fmci, sh);
    idio_display_C (" -> gci ", sh);
    idio_display (fgci, sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_variable_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       sym));

    idio_raise_condition (idio_S_true, c);
}

static void idio_error_dynamic_unbound (idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no such dynamic binding: mci ", sh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, sh);
    idio_display_C (" -> gci ", sh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, sh);
    idio_display_C (" -> gvi ", sh);
    idio_display (idio_fixnum (gvi), sh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_dynamic_variable_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       sym));

    idio_raise_condition (idio_S_true, c);
}

static void idio_error_environ_unbound (idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no such environ binding: mci ", sh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, sh);
    idio_display_C (" -> gci ", sh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, sh);
    idio_display_C (" -> gvi ", sh);
    idio_display (idio_fixnum (gvi), sh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_environ_variable_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       sym));

    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_error_computed (char *msg, idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);
    idio_display_C (": mci ", sh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, sh);
    idio_display_C (" -> gci ", sh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, sh);
    idio_display_C (" -> gvi ", sh);
    idio_display (idio_fixnum (gvi), sh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_computed_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       sym));

    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_error_computed_no_accessor (char *msg, idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no ", sh);
    idio_display_C (msg, sh);
    idio_display_C (" accessor: mci ", sh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, sh);
    idio_display_C (" -> gci ", sh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, sh);
    idio_display_C (" -> gvi ", sh);
    idio_display (idio_fixnum (gvi), sh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_computed_variable_no_accessor_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       sym));

    idio_raise_condition (idio_S_true, c);
}

void idio_vm_debug (IDIO thr, char *prefix, idio_ai_t stack_start)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (prefix);
    IDIO_TYPE_ASSERT (thread, thr);

    fprintf (stderr, "idio-debug: %s THR %10p\n", prefix, thr);
    idio_debug ("  src=%s\n", idio_vm_source_location ());
    fprintf (stderr, "     pc=%6zd\n", IDIO_THREAD_PC (thr));
    idio_debug ("    val=%s\n", IDIO_THREAD_VAL (thr));
    idio_debug ("   reg1=%s\n", IDIO_THREAD_REG1 (thr));
    idio_debug ("   reg2=%s\n", IDIO_THREAD_REG2 (thr));

    IDIO fmci = IDIO_THREAD_EXPR (thr);
    if (idio_isa_fixnum (fmci)) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	IDIO src = idio_vm_constants_ref (gci);
	idio_debug ("   expr=%s\n", src);
    } else {
	idio_debug ("   expr=%s\n", fmci);
    }
    idio_debug ("   func=%s\n", IDIO_THREAD_FUNC (thr));
    idio_debug ("    env=%s\n", IDIO_THREAD_ENV (thr));
    idio_debug ("  frame=%s\n", IDIO_THREAD_FRAME (thr));
    idio_debug ("   t/sp=% 3s\n", IDIO_THREAD_TRAP_SP (thr));
    idio_debug ("   d/sp=% 3s\n", IDIO_THREAD_DYNAMIC_SP (thr));
    idio_debug ("   e/sp=% 3s\n", IDIO_THREAD_ENVIRON_SP (thr));
    idio_debug ("     in=%s\n", IDIO_THREAD_INPUT_HANDLE (thr));
    idio_debug ("    out=%s\n", IDIO_THREAD_OUTPUT_HANDLE (thr));
    idio_debug ("    err=%s\n", IDIO_THREAD_ERROR_HANDLE (thr));
    idio_debug ("    mod=%s\n", IDIO_THREAD_MODULE (thr));
    fprintf (stderr, "jmp_buf=%p\n", IDIO_THREAD_JMP_BUF (thr));
    fprintf (stderr, "\n");

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t stack_size = idio_array_size (stack);

    if (stack_start < 0) {
	stack_start += stack_size;
    }

    IDIO_C_ASSERT (stack_start < stack_size);

    /*
    idio_ai_t i;
    fprintf (stderr, "%s STK %zd:%zd\n", prefix, stack_start, stack_size - 1);
    if (stack_start) {
	fprintf (stderr, "  %3zd  ...\n", stack_start - 1);
    }
    for (i = stack_size - 1; i >= 0; i--) {
	fprintf (stderr, "  %3zd ", i);
	idio_debug ("%.100s\n", idio_array_get_index (stack, i));
    }
    */

    idio_vm_decode_thread (thr);
}

static void idio_vm_invoke (IDIO thr, IDIO func, int tailp);

static uint64_t idio_vm_fetch_varuint (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    int i = IDIO_THREAD_FETCH_NEXT ();
    if (i <= 240) {
	return i;
    } else if (i <= 248) {
	int j = IDIO_THREAD_FETCH_NEXT ();

	return (240 + 256 * (i - 241) + j);
    } else if (249 == i) {
	int j = IDIO_THREAD_FETCH_NEXT ();
	int k = IDIO_THREAD_FETCH_NEXT ();

	return (2288 + 256 * j + k);
    } else {
	int n = (i - 250) + 3;

	uint64_t r = 0;
	for (i = 0; i < n; i++) {
	    r <<= 8;
	    r |= IDIO_THREAD_FETCH_NEXT ();
	}

	return r;
    }
}

static uint64_t idio_vm_read_fixuint (int n, size_t offset)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    /* fprintf (stderr, "ivrf: %d %zd\n", n, offset); */

    int i;
    uint64_t r = 0;
    for (i = 0; i < n; i++) {
	r <<= 8;
	r |= IDIO_IA_AE (idio_all_code, offset + i);
    }

    return r;
}

static void idio_vm_write_fixuint (int n, size_t offset, idio_ai_t gvi)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    /* fprintf (stderr, "ivwf: %d %zd %td\n", n, offset, gvi); */

    for (n--; n >= 0; n--) {
	IDIO_IA_AE (idio_all_code, offset + n) = gvi & 0xff;
	gvi >>= 8;
    }

    /*
     * Check there's nothing left!
     */
    IDIO_C_ASSERT (0 == gvi);
}

static uint64_t idio_vm_fetch_fixuint (int n, IDIO thr)
{
    IDIO_C_ASSERT (n < 9 && n > 0);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    uint64_t r = idio_vm_read_fixuint (n, IDIO_THREAD_PC (thr));
    IDIO_THREAD_PC (thr) += n;

    return r;
}

static uint64_t idio_vm_fetch_8uint (IDIO thr)
{
    return idio_vm_fetch_fixuint (1, thr);
}

static uint64_t idio_vm_fetch_16uint (IDIO thr)
{
    return idio_vm_fetch_fixuint (2, thr);
}

static uint64_t idio_vm_fetch_32uint (IDIO thr)
{
    return idio_vm_fetch_fixuint (4, thr);
}

static uint64_t idio_vm_fetch_64uint (IDIO thr)
{
    return idio_vm_fetch_fixuint (8, thr);
}

static uint64_t idio_vm_get_varuint (idio_ai_t *pcp)
{
    int i = IDIO_IA_GET_NEXT (pcp);
    if (i <= 240) {
	return i;
    } else if (i <= 248) {
	int j = IDIO_IA_GET_NEXT (pcp);

	return (240 + 256 * (i - 241) + j);
    } else if (249 == i) {
	int j = IDIO_IA_GET_NEXT (pcp);
	int k = IDIO_IA_GET_NEXT (pcp);

	return (2288 + 256 * j + k);
    } else {
	int n = (i - 250) + 3;

	uint64_t r = 0;
	for (i = 0; i < n; i++) {
	    r <<= 8;
	    r |= IDIO_IA_GET_NEXT (pcp);
	}

	return r;
    }
}

static uint64_t idio_vm_get_fixuint (int n, idio_ai_t *pcp)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    uint64_t r = idio_vm_read_fixuint (n, *pcp);
    *pcp += n;

    return r;
}

static uint64_t idio_vm_get_8uint (idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (1, pcp);
}

static uint64_t idio_vm_get_16uint (idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (2, pcp);
}

static uint64_t idio_vm_get_32uint (idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (4, pcp);
}

static uint64_t idio_vm_get_64uint (idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (8, pcp);
}

/*
 * Check this aligns with IDIO_IA_PUSH_REF in codegen.c
 */
#define IDIO_VM_FETCH_REF(t)	(idio_vm_fetch_16uint (t))
#define IDIO_VM_GET_REF(pcp)	(idio_vm_get_16uint (pcp))

/*
 * For a function with varargs, ie.
 *
 * (define (func x & rest) ...)
 *
 * we need to rewrite the call such that the non-mandatory args are
 * bundled up as a list:
 *
 * (func a b c d) => (func a (b c d))
 */
static void idio_vm_listify (IDIO frame, size_t arity)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    size_t index = IDIO_FRAME_NARGS (frame) - 1;
    IDIO result = idio_S_nil;

    for (;;) {
	if (arity == index) {
	    IDIO_FRAME_ARGS (frame, arity) = result;
	    return;
	} else {
	    result = idio_pair (IDIO_FRAME_ARGS (frame, index - 1),
				result);
	    index--;
	}
    }
}

static void idio_vm_preserve_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_ENVIRON_SP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_DYNAMIC_SP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_TRAP_SP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FRAME (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_ENV (thr));
    IDIO_THREAD_STACK_PUSH (idio_SM_preserve_state);
}

static void idio_vm_preserve_all_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_vm_preserve_state (thr);
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_REG1 (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_REG2 (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_EXPR (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FUNC (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
    IDIO_THREAD_STACK_PUSH (idio_SM_preserve_all_state);
}

static void idio_vm_restore_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_vm_debug (thr, "ivrs", -5); */

    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_preserve_state != marker) {
	idio_debug ("iv_restore_state: marker: expected idio_SM_preserve_state not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv_restore_state: unexpected stack marker");
    }
    ss--;

    IDIO_THREAD_ENV (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_S_nil != IDIO_THREAD_ENV (thr)) {
	if (! idio_isa_module (IDIO_THREAD_ENV (thr))) {
	    idio_debug ("\n\n****\nvm-restore-state: env = %s ?? -- not a module\n", IDIO_THREAD_ENV (thr));
	    idio_vm_decode_thread (thr);
	    idio_vm_debug (thr, "vm-restore-state", 0);
	    idio_vm_reset_thread (thr, 1);
	    return;
	}
	IDIO_TYPE_ASSERT (module, IDIO_THREAD_ENV (thr));
    }
    ss--;

    IDIO_THREAD_FRAME (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_S_nil != IDIO_THREAD_FRAME (thr)) {
	IDIO_TYPE_ASSERT (frame, IDIO_THREAD_FRAME (thr));
    }
    ss--;

    IDIO_THREAD_TRAP_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));

    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
    if (tsp < 2) {
	/*
	 * As we've just ascertained we don't have a condition handler
	 * this will end in even more tears...
	 */
	idio_error_C ("bad TRAP SP: < 2", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (tsp >= ss) {
	idio_error_C ("bad TRAP SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    ss--;

    IDIO_THREAD_DYNAMIC_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_DYNAMIC_SP (thr));
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    if (dsp >= ss) {
	idio_error_C ("bad DYNAMIC SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    ss--;

    IDIO_THREAD_ENVIRON_SP (thr) = IDIO_THREAD_STACK_POP ();

    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_ENVIRON_SP (thr));
    if (esp >= ss) {
	idio_error_C ("bad ENVIRON SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
}

static void idio_vm_restore_all_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("iv-restore-all-state: THR %s\n", thr); */
    /* idio_debug ("iv-restore-all-state: STK %s\n", IDIO_THREAD_STACK (thr)); */
    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_preserve_all_state != marker) {
	idio_debug ("iv-restore-all-state: marker: expected idio_SM_preserve_all_state not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv-restore-all-state: unexpected stack marker");
    }
    IDIO_THREAD_VAL (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_FUNC (thr) = IDIO_THREAD_STACK_POP ();

    /*
     * This verification of _FUNC() needs to be in sync with what
     * idio_vm_invoke() allows
     */
    if (! (idio_isa_procedure (IDIO_THREAD_FUNC (thr)) ||
	   idio_isa_symbol (IDIO_THREAD_FUNC (thr)) ||
	   idio_isa_continuation (IDIO_THREAD_FUNC (thr)))) {
	idio_debug ("iv-ras: func is not invokable: %s\n", IDIO_THREAD_FUNC (thr));
	IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FUNC (thr));
	IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_thread_state ();
	idio_error_param_type ("not an invokable type", IDIO_THREAD_FUNC (thr), IDIO_C_FUNC_LOCATION ());
    }

    IDIO_THREAD_EXPR (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_EXPR (thr));
    IDIO_THREAD_REG2 (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_REG1 (thr) = IDIO_THREAD_STACK_POP ();
    idio_vm_restore_state (thr);
}

#ifdef IDIO_VM_PERF
static struct timespec idio_vm_clos_t0;
static IDIO idio_vm_clos = NULL;

void idio_vm_func_start (IDIO func, struct timespec *tsp)
{
    IDIO_ASSERT (func);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    IDIO_C_ASSERT (0);
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	    idio_vm_clos = func;
	    IDIO_CLOSURE_CALLED (idio_vm_clos)++;
	    if (0 != clock_gettime (CLOCK_MONOTONIC, &idio_vm_clos_t0)) {
		perror ("clock_gettime (CLOCK_MONOTONIC, idio_vm_clos_t0)");
	    }
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO_C_ASSERT (tsp);
	    IDIO_PRIMITIVE_CALLED (func)++;
	    if (0 != clock_gettime (CLOCK_MONOTONIC, tsp)) {
		perror ("clock_gettime (CLOCK_MONOTONIC, tsp)");
	    }
	}
	break;
    default:
	{
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST1 (func),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

void idio_vm_func_stop (IDIO func, struct timespec *tsp)
{
    IDIO_ASSERT (func);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    IDIO_C_ASSERT (0);
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO_C_ASSERT (tsp);
	    IDIO_PRIMITIVE_CALLED (func)++;
	    if (0 != clock_gettime (CLOCK_MONOTONIC, tsp)) {
		perror ("clock_gettime (CLOCK_MONOTONIC, tsp)");
	    }
	}
	break;
    default:
	{
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST1 (func),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

static void idio_vm_clos_time (IDIO thr, const char *context)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (NULL == idio_vm_clos) {
	return;
    }

    if (0 == idio_vm_clos->type) {
	/*
	 * closure stashed in idio_vm_clos has been recycled before we
	 * got round to updating its timings
	 */
	return;
    }

    if (! idio_isa_closure (idio_vm_clos)) {
	/*
	 * closure stashed in idio_vm_clos has been recycled before we
	 * got round to updating its timings
	 */
	return;
    }

    struct timespec clos_te;
    if (0 != clock_gettime (CLOCK_MONOTONIC, &clos_te)) {
	perror ("clock_gettime (CLOCK_MONOTONIC, clos_te)");
    }

    struct timespec clos_td;
    clos_td.tv_sec = clos_te.tv_sec - idio_vm_clos_t0.tv_sec;
    clos_td.tv_nsec = clos_te.tv_nsec - idio_vm_clos_t0.tv_nsec;
    if (clos_td.tv_nsec < 0) {
	clos_td.tv_nsec += IDIO_VM_NS;
	clos_td.tv_sec -= 1;
    }

    IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_sec += clos_td.tv_sec;
    IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec += clos_td.tv_nsec;
    if (IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec > IDIO_VM_NS) {
	IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec -= IDIO_VM_NS;
	IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_sec += 1;
    }

    idio_vm_clos = NULL;
}

void idio_vm_prim_time (IDIO func, struct timespec *ts0p, struct timespec *tsep)
{
    IDIO_ASSERT (func);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    IDIO_C_ASSERT (0);
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO_C_ASSERT (ts0p);
	    IDIO_C_ASSERT (tsep);
	    struct timespec prim_td;
	    prim_td.tv_sec = tsep->tv_sec - ts0p->tv_sec;
	    prim_td.tv_nsec = tsep->tv_nsec - ts0p->tv_nsec;
	    if (prim_td.tv_nsec < 0) {
		prim_td.tv_nsec += IDIO_VM_NS;
		prim_td.tv_sec -= 1;
	    }

	    IDIO_PRIMITIVE_CALL_TIME (func).tv_sec += prim_td.tv_sec;
	    IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec += prim_td.tv_nsec;
	    if (IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec > IDIO_VM_NS) {
		IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec -= IDIO_VM_NS;
		IDIO_PRIMITIVE_CALL_TIME (func).tv_sec += 1;
	    }
	}
	break;
    default:
	{
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST1 (func),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

#endif

static void idio_vm_primitive_call_trace (char *name, IDIO thr, int nargs);
static void idio_vm_primitive_result_trace (IDIO thr);

static void idio_vm_invoke (IDIO thr, IDIO func, int tailp)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (func);
    IDIO_TYPE_ASSERT (thread, thr);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    /* IDIO_C_ASSERT (0); */
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	    if (0 == tailp) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
		IDIO_THREAD_STACK_PUSH (idio_SM_return);
	    }

	    IDIO_THREAD_FRAME (thr) = IDIO_CLOSURE_FRAME (func);
	    IDIO_THREAD_ENV (thr) = IDIO_CLOSURE_ENV (func);
	    IDIO_THREAD_PC (thr) = IDIO_CLOSURE_CODE_PC (func);

	    if (idio_vm_tracing &&
		0 == tailp) {
		idio_vm_tracing++;
	    }
#ifdef IDIO_VM_PERF
	    idio_vm_func_start (func, NULL);
#endif
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    /*
	     * PC shenanigans for primitives.
	     *
	     * If we are not in tail position then we should push the
	     * current PC onto the stack so that when the invoked code
	     * calls RETURN it will return to whomever called us -- as
	     * the CLOSURE code does above.
	     *
	     * By and large, though, primitives do not change the PC
	     * as they are entirely within the realm of C.  So we
	     * don't really care if they were called in tail position
	     * or not they just do whatever and set VAL.
	     *
	     * However, (apply proc . args) will prepare some
	     * procedure which may well be a closure which *will*
	     * alter the PC.  (As an aside, apply always invokes proc
	     * in tail position -- as proc *is* in tail position from
	     * apply's point of view).  The closure will, of course,
	     * RETURN to whatever is on top of the stack.
	     *
	     * But we haven't put anything there because this is a
	     * primitive and primitives don't change the PC...
	     *
	     * So, if, after invoking the primitive, the PC has
	     * changed (ie. apply prepared a closure which has set the
	     * PC ready to run the closure when we return from here)
	     * *and* we are not in tail position then we push the
	     * saved pc0 onto the stack.
	     *
	     * NB. If you push PC before calling the primitive (with a
	     * view to popping it off if the PC didn't change) and the
	     * primitive calls idio_raise_condition then there is an
	     * extraneous PC on the stack.
	     */
	    size_t pc0 = IDIO_THREAD_PC (thr);
	    IDIO val = IDIO_THREAD_VAL (thr);

	    IDIO last = IDIO_FRAME_ARGS (val, IDIO_FRAME_NARGS (val) - 1);
	    IDIO_FRAME_NARGS (val) -= 1;

	    if (idio_S_nil != last) {
		idio_error_C ("primitive: varargs?", last, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    /*
	     * Unlike the other invocations of a primitive (see
	     * PRIMCALL*, below) we haven't preset _VAL, _REG1 with
	     * our arguments so idio_vm_primitive_call_trace() can't
	     * do the right thing.
	     *
	     * idio_vm_start_func() bumbles through well enough.
	     */
#ifdef IDIO_VM_PERF
	    struct timespec prim_t0;
	    idio_vm_func_start (func, &prim_t0);
#endif

	    switch (IDIO_PRIMITIVE_ARITY (func)) {
	    case 0:
		{
		    IDIO args = idio_frame_args_as_list_from (val, 0);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (args);
		}
		break;
	    case 1:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO args = idio_frame_args_as_list_from (val, 1);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, args);
		}
		break;
	    case 2:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO args = idio_frame_args_as_list_from (val, 2);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, args);
		}
		break;
	    case 3:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO arg3 = IDIO_FRAME_ARGS (val, 2);
		    IDIO args = idio_frame_args_as_list_from (val, 3);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, args);
		}
		break;
	    case 4:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO arg3 = IDIO_FRAME_ARGS (val, 2);
		    IDIO arg4 = IDIO_FRAME_ARGS (val, 3);
		    IDIO args = idio_frame_args_as_list_from (val, 4);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, arg4, args);
		}
		break;
	    case 5:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO arg3 = IDIO_FRAME_ARGS (val, 2);
		    IDIO arg4 = IDIO_FRAME_ARGS (val, 3);
		    IDIO arg5 = IDIO_FRAME_ARGS (val, 4);
		    IDIO args = idio_frame_args_as_list_from (val, 5);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, arg4, arg5, args);
		}
		break;
	    default:
		idio_vm_error_function_invoke ("arity unexpected", IDIO_LIST2 (func, val), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
		break;
	    }

#ifdef IDIO_VM_PERF
	    struct timespec prim_te;
	    idio_vm_func_stop (func, &prim_te);
	    idio_vm_prim_time (func, &prim_t0, &prim_te);
#endif
	    size_t pc = IDIO_THREAD_PC (thr);

	    if (0 == tailp &&
		pc != pc0) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (pc0));
		IDIO_THREAD_STACK_PUSH (idio_SM_return);
	    }

	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }

	    return;
	}
	break;
    case IDIO_TYPE_CONTINUATION:
	{
	    IDIO val = IDIO_THREAD_VAL (thr);

	    IDIO last = IDIO_FRAME_ARGS (val, IDIO_FRAME_NARGS (val) - 1);
	    IDIO_FRAME_NARGS (val) -= 1;

	    if (idio_S_nil != last) {
		idio_error_C ("continuation: varargs?", last, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    if (IDIO_FRAME_NARGS (val) != 1) {
		idio_vm_error_function_invoke ("unary continuation", IDIO_LIST2 (func, val), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    idio_vm_restore_continuation (func, IDIO_FRAME_ARGS (val, 0));
	}
	break;
    case IDIO_TYPE_SYMBOL:
	{
	    char *pathname = idio_command_find_exe (func);
	    if (NULL != pathname) {
		IDIO_THREAD_VAL (thr) = idio_command_invoke (func, thr, pathname);
		free (pathname);
	    } else {
		IDIO val = IDIO_THREAD_VAL (thr);
		/*
		 * IDIO_FRAME_FA() includes a varargs element so
		 * should always be one or more
		 */
		IDIO args = idio_S_nil;
		if (IDIO_FRAME_NARGS (val) > 1) {
		    args = idio_frame_params_as_list (val);
		} else {
		    /*
		     * A single varargs element but if it is #n then
		     * nothing
		     */
		    if (idio_S_nil != IDIO_FRAME_ARGS (val, 0)) {
			args = IDIO_FRAME_ARGS (val, 0);
		    }
		}

		IDIO invocation = IDIO_LIST1 (func);
		if (idio_S_nil != args) {
		    invocation = idio_list_append2 (invocation, args);
		}
		idio_vm_error_function_invoke ("external command not found",
					       invocation,
					       IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
	break;
    default:
	{
	    idio_debug ("iv-i: func=%s\n", func);
	    idio_dump (func, 1);
	    idio_vm_thread_state ();
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST2 (func, IDIO_THREAD_VAL (thr)),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

/*
 * Given a command as a list, (foo bar baz), run the code
 *
 * WARNING: in the calling environment idio_gc_protect() any IDIO
 * objects you want to use after calling this function (as it may call
 * idio_gc_collect())
 */
IDIO idio_vm_invoke_C (IDIO thr, IDIO command)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (command);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
    IDIO_THREAD_STACK_PUSH (idio_SM_return);
    idio_vm_preserve_all_state (thr);

    switch (command->type) {
    case IDIO_TYPE_PAIR:
	{
	    /*
	     * (length command) will give us the +1 frame allocation we need
	     * because it will allocate a slot for the command name even
	     * though it won't go there.
	     */
	    IDIO vs = idio_frame_allocate (idio_list_length (command));
	    idio_ai_t fai;
	    IDIO args = IDIO_PAIR_T (command);
	    for (fai = 0; idio_S_nil != args; fai++) {
		idio_frame_update (vs, 0, fai, IDIO_PAIR_H (args));
		args = IDIO_PAIR_T (args);
	    }
	    IDIO_THREAD_VAL (thr) = vs;
	    /* IDIO func = idio_module_current_symbol_value (IDIO_PAIR_H (command)); */
	    idio_vm_invoke (thr, IDIO_PAIR_H (command), IDIO_VM_INVOKE_TAIL_CALL);

	    /*
	     * XXX
	     *
	     * If the command was a primitive then we called
	     * idio_vm_run() we'd be continuing our parent's loop.
	     *
	     * Need to figure out the whole invoke-from-C thing
	     * properly (or at least consistently).
	     */
	    if (! idio_isa_primitive (IDIO_PAIR_H (command))) {
		IDIO dosh = idio_open_output_string_handle_C ();
		idio_display_C ("vm-invoke-C PAIR: ", dosh);
		idio_display (command, dosh);

		idio_vm_run (thr, idio_get_output_string (dosh));
	    }
	}
	break;
    case IDIO_TYPE_CLOSURE:
	{
	    /*
	     * Must be a thunk
	     */
	    IDIO dosh = idio_open_output_string_handle_C ();
	    idio_display_C ("vm-invoke-C CLOS: ", dosh);

	    IDIO name = idio_get_property (command, idio_KW_name, IDIO_LIST1 (idio_S_nil));
	    IDIO sigstr = idio_get_property (command, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

	    if (idio_S_unspec != name) {
		idio_display (name, dosh);
	    }
	    if (idio_S_nil != sigstr) {
		idio_display_C (" ", dosh);
		idio_display (sigstr, dosh);
	    }
	    idio_display_C (" {CLOS}", dosh);

	    IDIO vs = idio_frame_allocate (1);
	    IDIO_THREAD_VAL (thr) = vs;
	    idio_vm_invoke (thr, command, IDIO_VM_INVOKE_TAIL_CALL);
	    idio_vm_run (thr, idio_get_output_string (dosh));
	}
    }

    IDIO r = IDIO_THREAD_VAL (thr);

    idio_vm_restore_all_state (thr);
    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_return != marker) {
	idio_debug ("iv-invoke-C: marker: expected idio_SM_return not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv-invoke-C: unexpected stack marker");
    }
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());

    return r;
}

static void idio_vm_push_dynamic (idio_ai_t gvi, IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_DYNAMIC_SP (thr));
    idio_array_push (stack, val);
    IDIO_THREAD_DYNAMIC_SP (thr) = idio_fixnum (idio_array_size (stack));
    idio_array_push (stack, idio_fixnum (gvi));
    idio_array_push (stack, idio_SM_dynamic);
}

static void idio_vm_pop_dynamic (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_dynamic != marker) {
	idio_debug ("ivpd: marker: expected idio_SM_dynamic not %s\n", marker);
	idio_vm_panic (thr, "ivpd: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_DYNAMIC_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_DYNAMIC_SP (thr));
}

IDIO idio_vm_dynamic_ref (idio_ai_t mci, idio_ai_t gvi, IDIO thr, IDIO args)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (list, args);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));

    IDIO v = idio_S_undef;

    for (;;) {
	if (sp >= 0) {
	    IDIO sv = idio_array_get_index (stack, sp);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		v = idio_array_get_index (stack, sp - 1);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    v = idio_vm_values_ref (gvi);
	    break;
	}
    }

    if (idio_S_undef == v) {
	if (idio_S_nil == args) {
	    idio_error_dynamic_unbound (mci, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return IDIO_PAIR_H (args);
	}
    }

    return v;
}

void idio_vm_dynamic_set (idio_ai_t mci, idio_ai_t gvi, IDIO v, IDIO thr)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));

    for (;;) {
	if (sp >= 0) {
	    IDIO sv = idio_array_get_index (stack, sp);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		idio_array_insert_index (stack, v, sp - 1);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    idio_array_insert_index (idio_vm_values, v, gvi);
	    break;
	}
    }
}

static void idio_vm_push_environ (idio_ai_t mci, idio_ai_t gvi, IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_ENVIRON_SP (thr));
    idio_array_push (stack, val);
    IDIO_THREAD_ENVIRON_SP (thr) = idio_fixnum (idio_array_size (stack));
    idio_array_push (stack, idio_fixnum (gvi));
    idio_array_push (stack, idio_SM_environ);
}

static void idio_vm_pop_environ (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_environ != marker) {
	idio_debug ("ivpe: marker: expected idio_SM_environ not %s\n", marker);
	idio_vm_panic (thr, "ivpe: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_ENVIRON_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_ENVIRON_SP (thr));
}

IDIO idio_vm_environ_ref (idio_ai_t mci, idio_ai_t gvi, IDIO thr, IDIO args)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (list, args);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));

    IDIO v = idio_S_undef;

    for (;;) {
	if (sp >= 0) {
	    IDIO sv = idio_array_get_index (stack, sp);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		v = idio_array_get_index (stack, sp - 1);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    v = idio_vm_values_ref (gvi);
	    break;
	}
    }

    if (idio_S_undef == v) {
	if (idio_S_nil == args) {
	    idio_error_environ_unbound (mci, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return IDIO_PAIR_H (args);
	}
    }

    return v;
}

void idio_vm_environ_set (idio_ai_t mci, idio_ai_t gvi, IDIO v, IDIO thr)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));

    for (;;) {
	if (sp >= 0) {
	    IDIO sv = idio_array_get_index (stack, sp);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		idio_array_insert_index (stack, v, sp - 1);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    idio_array_insert_index (idio_vm_values, v, gvi);
	    break;
	}
    }
}

IDIO idio_vm_computed_ref (idio_ai_t mci, idio_ai_t gvi, IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO gns = idio_array_get_index (idio_vm_values, gvi);

    if (idio_isa_pair (gns)) {
	IDIO get = IDIO_PAIR_H (gns);
	if (idio_isa_primitive (get) ||
	    idio_isa_closure (get)) {
	    return idio_vm_invoke_C (thr, IDIO_LIST1 (get));
	} else {
	    idio_vm_error_computed_no_accessor ("get", mci, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	idio_vm_error_computed ("no get/set accessors", mci, gvi, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO idio_vm_computed_set (idio_ai_t mci, idio_ai_t gvi, IDIO v, IDIO thr)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO gns = idio_array_get_index (idio_vm_values, gvi);

    if (idio_isa_pair (gns)) {
	IDIO set = IDIO_PAIR_T (gns);
	if (idio_isa_primitive (set) ||
	    idio_isa_closure (set)) {
	    return idio_vm_invoke_C (thr, IDIO_LIST2 (set, v));
	} else {
	    idio_vm_error_computed_no_accessor ("set", mci, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	idio_vm_error_computed ("no accessors", mci, gvi, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

void idio_vm_computed_define (idio_ai_t mci, idio_ai_t gvi, IDIO v, IDIO thr)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (pair, v);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_array_insert_index (idio_vm_values, v, gvi);
}

void idio_vm_push_trap (IDIO thr, IDIO handler, IDIO fmci)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (handler);
    IDIO_ASSERT (fmci);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fmci);

    if (! (idio_isa_closure (handler) ||
	   idio_isa_primitive (handler))) {
	idio_error_param_type ("closure|primitive", handler, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_TRAP_SP (thr));
    idio_array_push (stack, fmci);
    IDIO_THREAD_TRAP_SP (thr) = idio_fixnum (idio_array_size (stack));

    idio_array_push (stack, handler);
    idio_array_push (stack, idio_SM_push_trap);
}

static void idio_vm_pop_trap (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_push_trap != marker) {
	idio_debug ("ivpt: marker: expected idio_SM_push_trap not %s\n", marker);
	idio_vm_panic (thr, "ivpt: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();	/* handler */
    IDIO_THREAD_STACK_POP ();	/* fmci */

    IDIO_THREAD_TRAP_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
}

static void idio_vm_restore_trap (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_TRAP_SP (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_isa_fixnum (IDIO_THREAD_TRAP_SP (thr)) == 0) {
	IDIO_THREAD_STACK_PUSH (IDIO_THREAD_TRAP_SP (thr));
	idio_vm_panic (thr, "eek!");
    }
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
}

void idio_vm_raise_condition (IDIO continuablep, IDIO condition, int IHR)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (condition);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    /* idio_debug ("\n\nraise-condition: %s\n", condition); */

    IDIO thr = idio_thread_current_thread ();

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_ai_t trap_sp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));

    if (trap_sp >= idio_array_size (stack)) {
	idio_vm_thread_state ();
	idio_vm_panic (thr, "trap SP >= sizeof (stack)");
    }
    if (trap_sp < 2) {
	idio_vm_panic (thr, "trap SP < 2");
    }

    /*
     * This feels mildy expensive: The trap call will say:
     *
     * trap COND-TYPE-NAME handler body
     *
     * So what we have in our hands is an index, mci, into the
     * constants table which we can lookup, idio_vm_constants_ref, to
     * get a symbol, COND-TYPE-NAME.  We now need to look that up,
     * idio_module_symbol_value_recurse, to get a value, trap_ct.
     *
     * We now need to determine if the actual condition isa trap_ct.
     */
    IDIO trap_ct_mci;
    IDIO handler;
    while (1) {
	handler = idio_array_get_index (stack, trap_sp);
	trap_ct_mci = idio_array_get_index (stack, trap_sp - 1);
	IDIO ftrap_sp_next = idio_array_get_index (stack, trap_sp - 2);

	IDIO trap_ct_sym = idio_vm_constants_ref ((idio_ai_t) IDIO_FIXNUM_VAL (trap_ct_mci));
	IDIO trap_ct = idio_module_symbol_value_recurse (trap_ct_sym, IDIO_THREAD_ENV (thr), idio_S_nil);

	if (idio_S_undef == trap_ct) {
	    idio_vm_debug (thr, "not good", 0);
	    idio_vm_panic (thr, "trap condition type is undef");
	}

	idio_ai_t trap_sp_next = IDIO_FIXNUM_VAL (ftrap_sp_next);

	if (idio_struct_instance_isa (condition, trap_ct)) {
	    break;
	}

	if (trap_sp == trap_sp_next) {
	    idio_debug ("ivrc: Yikes!  Failed to match TRAP on %s\n", condition);
	    idio_vm_panic (thr, "ivrc: no more TRAP handlers\n");
	}
	trap_sp = trap_sp_next;
    }

    int isa_closure = idio_isa_closure (handler);

    /*
     * We should, normally, make some distinction between a call to a
     * primitive and a call to a closure as the primitive doesn't need
     * some of the wrapping around it.  In fact a call to a primitive
     * will unhelpfully trip over the wrapping because it didn't
     * RETURN into the unwrapping code (CHR - condition handler
     * return).
     *
     * On top of which, if this was a regular condition, called in
     * tail position then it effectively replaces the extant function
     * call at the time the condition was raised (by a C function,
     * probably).
     *
     * That's not so clever for an interrupt handler where we
     * effectively want to stop the clock, run something else and then
     * puts the clock back where it was.  Here we *don't* want to
     * either run in tail position (although that might matter less)
     * or replace the extant function call.
     *
     * This is particularly obvious for SIGCHLD where we are blocked
     * in waitpid, probably, and the SIGCHLD arrives, and we replace
     * the call to waitpid with result of do-job-notification
     * (eventually, via some C functions) which returns a value that
     * no-one is expecting.
     */

    if (IHR ||
	isa_closure) {
	idio_array_push (stack, idio_fixnum (IDIO_THREAD_PC (thr)));
	idio_array_push (stack, idio_SM_return);
    }

    int tailp = IDIO_VM_INVOKE_TAIL_CALL;
    if (IHR) {
	tailp = IDIO_VM_INVOKE_REGULAR_CALL;
	idio_vm_preserve_all_state (thr);
	IDIO_THREAD_PC (thr) = idio_vm_IHR_pc;  /* => RESTORE-ALL-STATE, RETURN */
    }

    if (isa_closure) {
	if (0 == IHR) {
	    idio_vm_preserve_state (thr);
	    idio_array_push (stack, IDIO_THREAD_TRAP_SP (thr)); /* for RESTORE-TRAP */
	}
    }

    IDIO vs = idio_frame (idio_S_nil, IDIO_LIST1 (condition));
    IDIO_THREAD_VAL (thr) = vs;

    /*
     * We need to run this code in the care of the next handler on the
     * stack (not the current handler).  Unless the next handler is
     * the base handler in which case it gets reused (ad infinitum).
     *
     * We can do that easily by just changing IDIO_THREAD_TRAP_SP
     * but if that handler RETURNs then we must restore the current
     * handler.
     */
    IDIO_THREAD_TRAP_SP (thr) = idio_array_get_index (stack, trap_sp - 2);
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));

    /*
     * Whether we are continuable or not determines where in the
     * prologue we set the PC for the RETURNee.
     */
    if (isa_closure) {
	if (0 == IHR) {
	    if (idio_S_true == continuablep) {
		idio_array_push (stack, idio_fixnum (idio_vm_CHR_pc)); /* => RESTORE-TRAP, RESTORE-STATE, RETURN */
		idio_array_push (stack, idio_SM_return);
	    } else {
		idio_array_push (stack, idio_fixnum (idio_vm_NCE_pc)); /* => NON-CONT-ERR */
		idio_array_push (stack, idio_SM_return);
	    }
	}
    }

    /* God speed! */
    idio_vm_invoke (thr, handler, tailp);

    /*
     * Actually, for a user-defined error handler, which will be a
     * closure, idio_vm_invoke did nothing much, the error
     * handling closure will only be run when we continue looping
     * around idio_vm_run1.
     *
     * Wait, though, consider how we've got to here.  Some
     * (user-level) code called a C primitive which stumbled over an
     * erroneous condition.  That C code called an idio_*error*
     * routine which eventually called us, idio_vm_raise_condition.
     *
     * Whilst we are ready to process the OOB exception handler in
     * Idio-land, the C language part of us still thinks we've in a
     * regular function call tree from the point of origin of the
     * error, ie. we still have a trail of C language frames back
     * through the caller (of idio_vm_raise_condition) -- and, in
     * turn, back up through to idio_vm_run1 where the C primitive was
     * first called from.  We need to make that trail disappear
     * otherwise either we'll accidentally call C's 'return' back down
     * that C stack (erroneously) or, after enough
     * idio_vm_raise_condition()s, we'll blow up the C stack.
     *
     * That means siglongjmp(3).
     *
     * XXX siglongjmp means that we won't be free()ing any memory
     * allocated during the life of that C stack.  Unless we think of
     * something clever...well?...er, still waiting...
     */
    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	siglongjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_SIGLONGJMP_CONDITION);
    } else {
	fprintf (stderr, "WARNING: raise-condition: unable to use jmp_buf==NULL\n");
	idio_vm_debug (thr, "raise-condition unable to use jmp_buf==NULL", 0);
	idio_vm_panic (thr, "raise-condition unable to use jmp_buf==NULL");
	return;
    }

    /* not reached */
    IDIO_C_ASSERT (0);
}

void idio_raise_condition (IDIO continuablep, IDIO condition)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (condition);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    idio_vm_raise_condition (continuablep, condition, 0);
}

IDIO_DEFINE_PRIMITIVE1 ("raise", raise, (IDIO c))
{
    IDIO_ASSERT (c);
    IDIO_VERIFY_PARAM_TYPE (condition, c);

    idio_raise_condition (idio_S_true, c);

    return idio_S_notreached;
}

IDIO idio_apply (IDIO fn, IDIO args)
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    /* idio_debug ("apply: %s", fn);    */
    /* idio_debug (" %s\n", args);    */

    size_t nargs = idio_list_length (args);
    size_t size = nargs;

    /*
     * (apply + 1 2 '(3 4 5))
     *
     * fn == +
     * args == (1 2 (3 4 5))
     *
     * nargs == 3
     *
     * size => (nargs - 1) + len (args[nargs-1])
     */

    IDIO larg = args;
    while (idio_S_nil != larg &&
	   idio_S_nil != IDIO_PAIR_T (larg)) {
	larg = IDIO_PAIR_T (larg);
    }
    if (idio_S_nil != larg) {
	larg = IDIO_PAIR_H (larg);
	size = (nargs - 1) + idio_list_length (larg);
    }

    IDIO vs = idio_frame_allocate (size + 1);

    if (nargs) {
	idio_ai_t vsi;
	for (vsi = 0; vsi < nargs - 1; vsi++) {
	    IDIO_FRAME_ARGS (vs, vsi) = IDIO_PAIR_H (args);
	    args = IDIO_PAIR_T (args);
	}
	args = larg;
	for (; idio_S_nil != args; vsi++) {
	    IDIO_FRAME_ARGS (vs, vsi) = IDIO_PAIR_H (args);
	    args = IDIO_PAIR_T (args);
	}
    }

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_VAL (thr) = vs;

    idio_vm_invoke (thr, fn, IDIO_VM_INVOKE_TAIL_CALL);

    return IDIO_THREAD_VAL (thr);
}

IDIO_DEFINE_PRIMITIVE1V ("apply", apply, (IDIO fn, IDIO args))
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    return idio_apply (fn, args);
}

IDIO_DEFINE_PRIMITIVE0 ("%%make-continuation", make_continuation, ())
{
    IDIO thr = idio_thread_current_thread ();

    IDIO k = idio_continuation (thr);

    return k;
}

void idio_vm_restore_continuation_data (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    IDIO thr = idio_thread_current_thread ();

    /*
     * WARNING:
     *
     * Make sure you *copy* the continuation's stack -- in case this
     * continuation is used again.
     */

    IDIO_THREAD_STACK (thr) = idio_array_copy (IDIO_CONTINUATION_STACK (k), IDIO_COPY_SHALLOW, 0);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_preserve_continuation != marker) {
	idio_debug ("iv_rest_cont_data: marker: expected idio_SM_preserve_continuation not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv_rest_cont_data: unexpected stack marker");

	/* notreached */
    }

    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());
    IDIO_C_ASSERT (IDIO_THREAD_PC (thr) < IDIO_IA_USIZE (idio_all_code));

    idio_vm_restore_state (thr);

    IDIO_THREAD_VAL (thr) = val;
    IDIO_THREAD_JMP_BUF (thr) = IDIO_CONTINUATION_JMP_BUF (k);
}

void idio_vm_restore_continuation (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    idio_vm_restore_continuation_data (k, val);
    IDIO thr = idio_thread_current_thread ();

    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	siglongjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_SIGLONGJMP_CONTINUATION);
    } else {
	fprintf (stderr, "WARNING: restore-continuation: unable to use jmp_buf==NULL\n");
	idio_vm_debug (thr, "iv_rest_cont unable to use jmp_buf==NULL", 0);
	idio_vm_panic (thr, "iv_rest_cont unable to use jmp_buf==NULL");
	return;
    }
}

void idio_vm_restore_exit (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    idio_vm_restore_continuation_data (k, val);

    IDIO thr = idio_thread_current_thread ();

    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	siglongjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_SIGLONGJMP_EXIT);
    } else {
	fprintf (stderr, "WARNING: restore-exit: unable to use jmp_buf==NULL\n");
	idio_vm_debug (thr, "iv_rest_exit unable to use jmp_buf==NULL", 0);
	idio_vm_panic (thr, "iv_rest_exit unable to use jmp_buf==NULL");
	return;
    }
}

IDIO_DEFINE_PRIMITIVE2 ("%%restore-continuation", restore_continuation, (IDIO k, IDIO val))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    idio_vm_restore_continuation (k, val);

    /* not reached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%%call/cc", call_cc, (IDIO proc), "proc", "\
call ``proc`` with the current continuation			\n\
								\n\
:param proc:							\n\
:type proc: a procedure of 1 argument				\n\
								\n\
This is the ``call/cc`` primitive.				\n\
")
{
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (closure, proc);

    IDIO thr = idio_thread_current_thread ();

    IDIO k = idio_continuation (thr);

    /* idio_debug ("%%%%call/cc: %s\n", k); */

    IDIO_THREAD_VAL (thr) = idio_frame (IDIO_THREAD_VAL (thr), IDIO_LIST1 (k));

    idio_vm_invoke (thr, proc, IDIO_VM_INVOKE_REGULAR_CALL);

    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	siglongjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_SIGLONGJMP_CALLCC);
    } else {
	fprintf (stderr, "WARNING: %%call/cc: unable to use jmp_buf==NULL\n");
	idio_vm_debug (thr, "%%call/cc unable to use jmp_buf==NULL", 0);
	idio_vm_panic (thr, "%%call/cc unable to use jmp_buf==NULL");
	return idio_S_unspec;
    }

    /* not reached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE0_DS ("%%vm-continuations", vm_continuations, (), "", "\
return the current VM continuations			\n\
							\n\
the format is undefined and subject to arbitrary change	\n\
")
{
    return idio_vm_krun;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%%vm-apply-continuation", vm_apply_continuation, (IDIO n, IDIO val), "n v", "\
invoke the ``n``th VM continuation with value ``v``		\n\
								\n\
:param n: the continuation to invoke				\n\
:type n: (non-negative) integer					\n\
:param v: the value to pass to the continuation			\n\
								\n\
``n`` is subject to a range check on the array of stored	\n\
continuations in the VM.					\n\
								\n\
The function does not return.					\n\
")
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (val);

    idio_ai_t n_C = 0;

    if (idio_isa_fixnum (n)) {
	n_C = IDIO_FIXNUM_VAL (n);
    } else if (idio_isa_bignum (n)) {
	if (IDIO_BIGNUM_INTEGER_P (n)) {
	    n_C = idio_bignum_ptrdiff_value (n);
	} else {
	    IDIO n_i = idio_bignum_real_to_integer (n);
	    if (idio_S_nil == n_i) {
		idio_error_param_type ("integer", n, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		n_C = idio_bignum_ptrdiff_value (n_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (n_C < 0) {
	idio_error_param_type ("positive integer", n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_ai_t krun_p = idio_array_size (idio_vm_krun);

    if (n_C >= krun_p) {
	idio_error_param_type ("out of range", n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO krun = idio_S_nil;

    while (krun_p > n_C) {
	krun = idio_array_pop (idio_vm_krun);
	krun_p--;
    }

    if (idio_isa_pair (krun)) {
	fprintf (stderr, "%%vm-apply-continuation: restoring krun #%td: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), val);

	return idio_S_notreached;
    }

    idio_error_C ("failed to invoke contunation", IDIO_LIST2 (n, val), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1 ("%%vm-trace", vm_trace, (IDIO trace))
{
    IDIO_ASSERT (trace);
    IDIO_VERIFY_PARAM_TYPE (fixnum, trace);

    idio_vm_tracing = IDIO_FIXNUM_VAL (trace);

    return idio_S_unspec;
}

#ifdef IDIO_DEBUG
IDIO_DEFINE_PRIMITIVE1 ("%%vm-dis", vm_dis, (IDIO dis))
{
    IDIO_ASSERT (dis);
    IDIO_VERIFY_PARAM_TYPE (fixnum, dis);

    idio_vm_dis = IDIO_FIXNUM_VAL (dis);

    return idio_S_unspec;
}

#define IDIO_VM_RUN_DIS(...)	if (idio_vm_dis) { fprintf (stderr, __VA_ARGS__); }
#else
#define IDIO_VM_RUN_DIS(...)	((void) 0)
#endif
#define IDIO_VM_DASM(...)	{ fprintf (idio_dasm_FILE, __VA_ARGS__); }

IDIO idio_vm_closure_name (IDIO c)
{
    IDIO_ASSERT (c);
    IDIO_TYPE_ASSERT (closure, c);

    return idio_get_property (c, idio_KW_name, IDIO_LIST1 (idio_S_nil));
}

static void idio_vm_function_trace (IDIO_I ins, IDIO thr)
{
    IDIO func = IDIO_THREAD_FUNC (thr);
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args = idio_frame_params_as_list (val);
    IDIO expr = idio_list_append2 (IDIO_LIST1 (func), args);

    struct timespec ts;
    if (0 != clock_gettime (CLOCK_MONOTONIC, &ts)) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ts)");
    }

    /*
     * %9d	- clock ns
     * SPACE
     * %7zd	- PC of ins
     * SPACE
     * %40s	- lexical information
     * %*s	- trace-depth indent
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %s	- expression
     */

    fprintf (stderr, "%09ld ", ts.tv_nsec);
    fprintf (stderr, "%7zd ", IDIO_THREAD_PC (thr) - 1);

    IDIO fmci = IDIO_THREAD_EXPR (thr);
    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

    IDIO src = idio_vm_constants_ref (gci);

    IDIO lo_sh = idio_open_output_string_handle_C ();

    if (idio_isa_pair (src)) {
	IDIO lo = idio_hash_get (idio_src_properties, src);
	if (idio_S_unspec == lo){
	    idio_display (lo, lo_sh);
	} else {
	    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME), lo_sh);
	    idio_display_C (":line ", lo_sh);
	    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE), lo_sh);
	}
    } else {
	idio_display (src, lo_sh);
	idio_display_C (" !pair", lo_sh);
    }
    idio_debug ("%-40s", idio_get_output_string (lo_sh));

    fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_in);

    int isa_closure = idio_isa_closure (func);

    if (isa_closure) {
	IDIO name = idio_get_property (func, idio_KW_name, IDIO_LIST1 (idio_S_nil));
	IDIO sigstr = idio_get_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

	if (idio_S_nil != name) {
	    char *s = idio_display_string (name);
	    fprintf (stderr, "(%s", s);
	    free (s);
	} else {
	    fprintf (stderr, "(-anon-");
	}
	if (idio_S_nil != sigstr) {
	    char *s = idio_display_string (sigstr);
	    fprintf (stderr, " %s", s);
	    free (s);
	}
	fprintf (stderr, ") was ");
    } else if (idio_isa_primitive (func)) {
    }

    if (isa_closure) {
	switch (ins) {
	case IDIO_A_FUNCTION_GOTO:
	    fprintf (stderr, "tail-called as\n");
	    break;
	case IDIO_A_FUNCTION_INVOKE:
	    fprintf (stderr, "called as\n");
	    break;
	}

	fprintf (stderr, "%9s ", "");
	fprintf (stderr, "%7s ", "");
	fprintf (stderr, "%40s", "");

	fprintf (stderr, "%*s  ", idio_vm_tracing, "");
    }

    idio_debug ("%s", expr);
    fprintf (stderr, "\n");
}

static void idio_vm_primitive_call_trace (char *name, IDIO thr, int nargs)
{
    /*
     * %7zd	- PC of ins
     * SPACE
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    fprintf (stderr, "%9s ", "");
    fprintf (stderr, "%7zd ", IDIO_THREAD_PC (thr) - 1);
    fprintf (stderr, "%40s", "");

    /* fprintf (stderr, "        __primcall__    "); */

    fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_in);
    fprintf (stderr, "(%s", name);
    if (nargs > 1) {
	idio_debug (" %s", IDIO_THREAD_REG1 (thr));
    }
    if (nargs > 0) {
	idio_debug (" %s", IDIO_THREAD_VAL (thr));
    }
    fprintf (stderr, ")\n");
}

static void idio_vm_primitive_result_trace (IDIO thr)
{
    IDIO val = IDIO_THREAD_VAL (thr);

    /*
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    /* fprintf (stderr, "                                "); */

    fprintf (stderr, "%9s ", "");
    fprintf (stderr, "%7zd ", IDIO_THREAD_PC (thr));
    fprintf (stderr, "%40s", "");
    fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_out);
    idio_debug ("%s\n", val);
}

/*
 * When a module is written out it's constants will have arbitrary
 * module-specific indexes and so the representation must be
 * key:value, ie. a hash
 */
void idio_vm_add_module_constants (IDIO module, IDIO constants)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (constants);
    IDIO_TYPE_ASSERT (module, module);

    if (idio_S_nil == constants) {
	return;
    }

    IDIO_TYPE_ASSERT (array, constants);

    idio_ai_t i;
    idio_ai_t al = idio_array_size (constants);

    for (i = 0; i < al; i++) {
	IDIO c = idio_array_get_index (constants, i);
	if (idio_S_nil != c) {
	    idio_ai_t gci = idio_vm_constants_lookup_or_extend (c);
	    idio_module_set_vci (module, idio_fixnum (i), idio_fixnum (gci));
	}
    }
}

static idio_ai_t idio_vm_get_or_create_vvi (idio_ai_t mci)
{
    IDIO fmci = idio_fixnum (mci);

    IDIO ce = idio_thread_current_env ();

    IDIO fgvi = idio_module_get_vvi (ce, fmci);
    /* idio_debug ("fgvi=%s\n", fgvi); */
    /*
     * NB 0 is the placeholder value index (see idio_init_vm_values())
     */
    idio_ai_t gvi = 0;

    if (idio_S_unspec == fgvi ||
	0 == IDIO_FIXNUM_VAL (fgvi)) {
	/*
	 * This is the first time we have looked for mci in this module
	 * and we have failed to find a vvi, ie. fast-lookup, mapping
	 * to a value index.  So we need to find the symbol in the
	 * current environment (or its imports) and set the mapping up
	 * for future calls.
	 *
	 * Remember this could be a ref of something that has never
	 * been set.  In a pure programming language that would be an
	 * error.  However, in Idio that could be the name of a
	 * program we want to execute: "ls -l" should have both "ls"
	 * and "-l" as un-bound symbols for which we need to return a
	 * gvi of 0 so that IDIO_A[_CHECKED]_GLOBAL_FUNCTION_SYM_REF
	 * (and IDIO_A[_CHECKED]_GLOBAL_SYM_REF) can return the
	 * symbols for idio_vm_invoke() to exec.
	 */

	/*
	 * 1. map mci to a global constant index
	 */
	IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);
	IDIO sym = idio_vm_constants_ref (gci);
	IDIO_TYPE_ASSERT (symbol, sym);

	/*
	 * 2. Look in the current environment
	 */
	IDIO sk_ce = idio_module_find_symbol (sym, ce);

	if (idio_S_unspec == sk_ce) {
	    /*
	     * 2. Look in the imports of the current environment.
	     */
	    IDIO sk_im = idio_module_find_symbol_recurse (sym, ce, 2);

	    if (idio_S_unspec == sk_im) {
		IDIO dr = idio_module_direct_reference (sym);
		if (idio_S_unspec != dr) {
		    IDIO sk_dr = IDIO_PAIR_HTT (dr);
		    idio_module_set_symbol (sym, sk_dr, ce);
		    fgvi = IDIO_PAIR_HTT (sk_dr);
		    idio_module_set_vvi (ce, fmci, fgvi);
		    return IDIO_FIXNUM_VAL (fgvi);
		} else {
		    idio_debug ("ivgoc-vvi: %-20s return 0\n", sym);
		    return 0;
		}

		/*
		 * Not found so forge this mci to have a value of
		 * itself, the symbol sym.
		 */
		gvi = idio_vm_extend_values ();
		fgvi = idio_fixnum (gvi);
		sk_im = IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, ce, idio_string_C ("idio_vm_get_or_create_vvi"));
		idio_module_set_symbol (sym, sk_im, ce);
		idio_module_set_symbol_value (sym, sym, ce);

		return gvi;
	    }

	    fgvi = IDIO_PAIR_HTT (sk_im);
	    gvi = IDIO_FIXNUM_VAL (fgvi);
	    IDIO_C_ASSERT (gvi >= 0);

	    /*
	     * There was no existing entry for:
	     *
	     *   a. this mci in the idio_module_vvi table -- so set it
	     *
	     *   b. this sym in the current environment -- so copy the
	     *   imported sk
	     */
	    idio_module_set_symbol (sym, sk_im, ce);
	    idio_module_set_vvi (ce, fmci, fgvi);
	} else {
	    fgvi = IDIO_PAIR_HTT (sk_ce);
	    gvi = IDIO_FIXNUM_VAL (fgvi);
	    IDIO_C_ASSERT (gvi >= 0);

	    if (0 == gvi &&
		idio_eqp (ce, idio_Idio_module_instance ())) {
		IDIO sk_im = idio_module_find_symbol_recurse (sym, ce, 2);

		if (idio_S_unspec != sk_im) {
		    fgvi = IDIO_PAIR_HTT (sk_im);
		    gvi = IDIO_FIXNUM_VAL (fgvi);
		    IDIO_C_ASSERT (gvi >= 0);

		}

		IDIO_PAIR_HTT (sk_ce) = fgvi;
	    }

	    /*
	     * There was no existing entry for:
	     *
	     *   a. this mci in the idio_module_vvi table -- so set it
	     */
	    idio_module_set_vvi (ce, fmci, fgvi);
	}
    } else {
	gvi = IDIO_FIXNUM_VAL (fgvi);
	IDIO_C_ASSERT (gvi >= 0);
    }

    return gvi;
}

int idio_vm_run1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (IDIO_THREAD_PC(thr) < 0) {
	fprintf (stderr, "\n\nidio_vm_run1: PC %" PRIdPTR " < 0\n", IDIO_THREAD_PC (thr));
	idio_vm_panic (thr, "idio_vm_run1: bad PC!");
    } else if (IDIO_THREAD_PC(thr) > IDIO_IA_USIZE (idio_all_code)) {
	fprintf (stderr, "\n\nidio_vm_run1: PC %" PRIdPTR " > max code PC %" PRIdPTR "\n", IDIO_THREAD_PC (thr), IDIO_IA_USIZE (idio_all_code));
	idio_vm_panic (thr, "idio_vm_run1: bad PC!");
    }
    IDIO_I ins = IDIO_THREAD_FETCH_NEXT ();

#ifdef IDIO_VM_PERF
    idio_vm_ins_counters[ins]++;
    struct timespec ins_t0;
    if (0 != clock_gettime (CLOCK_MONOTONIC, &ins_t0)) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ins_t0)");
    }
#endif

    IDIO_VM_RUN_DIS ("idio_vm_run1: %10p %5zd %3d: ", thr, IDIO_THREAD_PC (thr) - 1, ins);

    switch (ins) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 0");
	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 0);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 1");
	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 1);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 2");
	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 2);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 3");
	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 3);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:
	{
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), j);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), i, j);
	}
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 0");
	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 0) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 1");
	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 1) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 2");
	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 2) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 3");
	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 3) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), i) = IDIO_THREAD_VAL (thr);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    idio_frame_update (IDIO_THREAD_FRAME (thr), i, j, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_GLOBAL_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    idio_debug ("\nGLOBAL-SYM-REF: %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		    IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		IDIO ce = idio_thread_current_env ();
		IDIO sk_ce = idio_module_find_symbol_recurse (sym, ce, 1);

		if (idio_S_unspec == sk_ce) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else {
		    IDIO sk_fgvi = IDIO_PAIR_HTT (sk_ce);
		    idio_ai_t sk_gvi = IDIO_FIXNUM_VAL (sk_fgvi);
		    if (0 == sk_gvi) {
			idio_debug ("G-R ce=%s\n", ce);
			idio_debug ("G-R sk_ce=%s\n", sk_ce);
			idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"));

			/* notreached */
		    } else {
			/*
			 * Setup the fast-lookup -- NB we don't update
			 * the sk_ce value with sk_fgvi -- should we?
			 */
			idio_module_set_vvi (ce, fmci, sk_fgvi);
			IDIO val = idio_vm_values_ref (sk_gvi);

			if (idio_S_undef == val) {
			    IDIO_THREAD_VAL (thr) = sym;
			} else if (idio_S_unspec == val) {
			    idio_debug ("\nGLOBAL-SYM-REF: %s", sym);
			    fprintf (stderr, " #%" PRId64, mci);
			    idio_dump (thr, 2);
			    idio_debug ("c-m: %s\n", idio_thread_current_module ());
			    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

			    /* notreached */
			    return 0;
			} else {
			    IDIO_THREAD_VAL (thr) = val;
			}
		    }
		}
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

    	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    idio_debug ("\nCHECKED-GLOBAL-SYM-REF: %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    IDIO_C_ASSERT (0);
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CHECKED-GLOBAL-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		    IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		idio_debug ("CHECKED-GLOBAL-SYM-REF: %s gvi==0 => sym\n", sym);
		idio_debug (" ce=%s\n", idio_thread_current_env ());
		fprintf (stderr, " mci #%" PRId64 "\n", mci);
		idio_vm_panic (thr, "CHECKED-GLOBAL-SYM-REF: gvi==0");
		IDIO_THREAD_VAL (thr) = sym;
	    }
	}
	break;
    case IDIO_A_GLOBAL_FUNCTION_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-FUNCTION-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    idio_debug ("\nGLOBAL-FUNCTION-SYM-REF:) %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-FUNCTION-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		  IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		IDIO ce = idio_thread_current_env ();
		IDIO sk_ce = idio_module_find_symbol_recurse (sym, ce, 1);

		if (idio_S_unspec == sk_ce) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else {
		    IDIO sk_fgvi = IDIO_PAIR_HTT (sk_ce);
		    idio_ai_t sk_gvi = IDIO_FIXNUM_VAL (sk_fgvi);
		    if (0 == sk_gvi) {
			idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"));

			/* notreached */
		    } else {
			/*
			 * Setup the fast-lookup -- NB we don't update
			 * the sk_ce value with sk_fgvi -- should we?
			 */
			idio_module_set_vvi (ce, fmci, sk_fgvi);
			IDIO val = idio_vm_values_ref (sk_gvi);

			if (idio_S_undef == val) {
			    IDIO_THREAD_VAL (thr) = sym;
			} else if (idio_S_unspec == val) {
			    idio_debug ("\nGLOBAL-FUNCTION-SYM-REF: %s", sym);
			    fprintf (stderr, " #%" PRId64, mci);
			    idio_dump (thr, 2);
			    idio_debug ("c-m: %s\n", idio_thread_current_module ());
			    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-FUNCTION-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

			    /* notreached */
			    return 0;
			} else {
			    IDIO_THREAD_VAL (thr) = val;
			}
		    }
		}
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-FUNCTION-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    idio_debug ("\nCHECKED-GLOBAL-FUNCTION-SYM-REF:) %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CHECKED-GLOBAL-FUNCTION-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		    IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		idio_debug ("CHECKED-GLOBAL-FUNCTION-SYM-REF: %s gvi==0 => sym\n", sym);
		idio_vm_panic (thr, "CHECKED-GLOBAL-FUNCTION-SYM-REF: gvi==0");
		IDIO_THREAD_VAL (thr) = sym;
	    }
	}
	break;
    case IDIO_A_CONSTANT_SYM_REF:
    	{
    	    idio_ai_t mci = idio_vm_fetch_varuint (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	    IDIO c = idio_vm_constants_ref (gci);

    	    IDIO_VM_RUN_DIS ("CONSTANT %td", gci);
#ifdef IDIO_DEBUG
	    if (idio_vm_dis) {
		idio_debug (" %s", c);
	    }
#endif
	    switch ((intptr_t) c & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
		IDIO_THREAD_VAL (thr) = c;
		break;
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		idio_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-SYM-REF"));

		/* notreached */
		break;
	    case IDIO_TYPE_POINTER_MARK:
		{
		    switch (c->type) {
		    case IDIO_TYPE_STRING:
		    case IDIO_TYPE_SYMBOL:
		    case IDIO_TYPE_KEYWORD:
		    case IDIO_TYPE_PAIR:
		    case IDIO_TYPE_ARRAY:
		    case IDIO_TYPE_HASH:
		    case IDIO_TYPE_BIGNUM:
		    case IDIO_TYPE_BITSET:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_STRUCT_INSTANCE:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_PRIMITIVE:
		    case IDIO_TYPE_CLOSURE:
			idio_debug ("idio_vm_run1/CONSTANT-SYM-REF: you should NOT be reifying %s", c);
			IDIO name = idio_get_property (c, idio_KW_name, idio_S_unspec);
			if (idio_S_unspec != name) {
			    idio_debug (" %s", name);
			}
			fprintf (stderr, "\n");
			IDIO_THREAD_VAL (thr) = c;
			break;
		    default:
			idio_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-SYM-REF"));
			break;
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT-SYM-REF"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", c, (intptr_t) c & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

		return 0;
		break;
	    }
    	}
    	break;
    case IDIO_A_COMPUTED_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("COMPUTED-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_computed_ref (mci, gvi, thr);
	    } else {
		idio_vm_panic (thr, "COMPUTED-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_GLOBAL_SYM_DEF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    uint64_t mkci = idio_vm_fetch_varuint (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO ce = idio_thread_current_env ();
	    IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
	    IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci));
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO kind = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgkci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-SYM-DEF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    IDIO sk_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_unspec == sk_ce) {
		idio_ai_t gvi = idio_vm_extend_values ();
		IDIO fgvi = idio_fixnum (gvi);
		idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);
		sk_ce = IDIO_LIST5 (kind, fmci, fgvi, ce, idio_string_C ("IDIO_A_GLOBAL_SYM_DEF"));
		idio_module_set_symbol (sym, sk_ce, ce);
	    } else {
		IDIO fgvi = IDIO_PAIR_HTT (sk_ce);
		if (0 == IDIO_FIXNUM_VAL (fgvi)) {
		    idio_ai_t gvi = idio_vm_extend_values ();
		    fgvi = idio_fixnum (gvi);
		    idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);
		    sk_ce = IDIO_LIST5 (kind, fmci, fgvi, ce, idio_string_C ("IDIO_A_GLOBAL_SYM_DEF/gvi=0"));
		    idio_module_set_symbol (sym, sk_ce, ce);
		}
	    }
	}
	break;
    case IDIO_A_GLOBAL_SYM_SET:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-SYM-SET %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		idio_vm_values_set (gvi, val);

		if (idio_isa_closure (val)) {
		    idio_set_property (val, idio_KW_name, sym);
		    idio_set_property (val, idio_KW_source, idio_string_C ("GLOBAL-SYM-SET"));
		    IDIO str = idio_get_property (val, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));
		    if (idio_S_nil != str) {
			idio_set_property (val, idio_KW_sigstr, str);
		    }
		    str = idio_get_property (val, idio_KW_docstr, IDIO_LIST1 (idio_S_nil));
		    if (idio_S_nil != str) {
			idio_set_property (val, idio_KW_docstr, str);
		    }
		}
	    } else {
		IDIO ce = idio_thread_current_env ();
		IDIO sk_ce = idio_module_find_symbol (sym, ce);
		idio_debug ("GLOBAL-SYM-SET: UNBOUND sym=%s", sym);
		idio_debug (" fmci=%s", fmci);
		idio_debug (" fgci=%s\n", fgci);
		idio_debug (" ce=%s\n", IDIO_MODULE_NAME (ce));
		idio_debug (" sk_ce=%s\n", sk_ce);
		idio_debug (" MI=%s\n", IDIO_MODULE_IMPORTS (ce));

		IDIO sk = idio_module_find_symbol_recurse (sym, ce, 1);
		idio_debug (" sk=%s\n", sk);

		idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-SET"));
		idio_vm_panic (thr, "GLOBAL-SYM-SET: no gvi!");

		/* notreached */
	    }
	}
	break;
    case IDIO_A_COMPUTED_SYM_SET:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("COMPUTED-SYM-SET %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		/*
		 * For other values, *val* remains the value "set".  For a
		 * computed value, setting it runs an arbitrary piece of
		 * code which returns a value.
		 */
		IDIO_THREAD_VAL (thr) = idio_vm_computed_set (mci, gvi, val, thr);
	    } else {
		idio_vm_panic (thr, "COMPUTED-SYM-SET: no gvi!");
	    }
	}
	break;
    case IDIO_A_COMPUTED_SYM_DEFINE:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO ce = idio_thread_current_env ();
	    IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("COMPUTED-SYM-DEFINE %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);

	    IDIO sk_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_unspec == sk_ce) {
		sk_ce = IDIO_LIST5 (idio_S_toplevel, idio_fixnum (mci), fgvi, ce, idio_string_C ("IDIO_A_COMPUTED_SYM_DEFINE"));
		idio_module_set_symbol (sym, sk_ce, ce);
	    } else {
		IDIO_PAIR_HTT (sk_ce) = fgvi;
	    }

	    IDIO val = IDIO_THREAD_VAL (thr);

	    idio_vm_computed_define (mci, gvi, val, thr);
	}
	break;
    case IDIO_A_GLOBAL_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("GLOBAL-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "GLOBAL-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "CHECKED-GLOBAL-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_GLOBAL_FUNCTION_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("GLOBAL-FUNCTION-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "GLOBAL-FUNCTION-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-FUNCTION-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "CHECKED-GLOBAL-FUNCTION-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_CONSTANT_VAL_REF:
	{
	    idio_ai_t gvi = idio_vm_fetch_varuint (thr);
	    IDIO fgvi = idio_fixnum (gvi);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fgvi);
	    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	    IDIO c = idio_vm_constants_ref (gci);

	    IDIO_VM_RUN_DIS ("CONSTANT %td", gci);
#ifdef IDIO_DEBUG
	    if (idio_vm_dis) {
		idio_debug (" %s", c);
	    }
#endif
	    switch ((intptr_t) c & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
		IDIO_THREAD_VAL (thr) = c;
		break;
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		idio_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-VAL-REF"));

		/* notreached */
		break;
	    case IDIO_TYPE_POINTER_MARK:
		{
		    switch (c->type) {
		    case IDIO_TYPE_STRING:
		    case IDIO_TYPE_SYMBOL:
		    case IDIO_TYPE_KEYWORD:
		    case IDIO_TYPE_PAIR:
		    case IDIO_TYPE_ARRAY:
		    case IDIO_TYPE_HASH:
		    case IDIO_TYPE_BIGNUM:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_STRUCT_INSTANCE:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_PRIMITIVE:
		    case IDIO_TYPE_CLOSURE:
			idio_debug ("idio_vm_run1/CONSTANT-VAL-REF: you should NOT be reifying %s", c);
			IDIO name = idio_get_property (c, idio_KW_name, idio_S_unspec);
			if (idio_S_unspec != name) {
			    idio_debug (" %s", name);
			}
			fprintf (stderr, "\n");
			IDIO_THREAD_VAL (thr) = c;
			break;
		    default:
			idio_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-VAL-REF"));
			break;
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT-VAL-REF"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", c, (intptr_t) c & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

		return 0;
		break;
	    }
	}
	break;
    case IDIO_A_COMPUTED_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("COMPUTED-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_computed_ref (gvi, gvi, thr);
	    } else {
		idio_vm_panic (thr, "COMPUTED-VAL-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_GLOBAL_VAL_DEF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("GLOBAL-VAL-DEF %" PRId64, gvi);

	    /* no symbol to define! */
	    IDIO_C_ASSERT (0);
	}
	break;
    case IDIO_A_GLOBAL_VAL_SET:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("GLOBAL-VAL-SET %" PRId64, gvi);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		idio_vm_values_set (gvi, val);
	    } else {
		idio_vm_panic (thr, "GLOBAL-VAL-SET: no gvi!");
	    }
	}
	break;
    case IDIO_A_COMPUTED_VAL_SET:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("COMPUTED-VAL-SET %" PRId64, gvi);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		/*
		 * For other values, *val* remains the value "set".  For a
		 * computed value, setting it runs an arbitrary piece of
		 * code which returns a value.
		 */
		IDIO_THREAD_VAL (thr) = idio_vm_computed_set (gvi, gvi, val, thr);
	    } else {
		idio_vm_panic (thr, "COMPUTED-VAL-SET: no gvi!");
	    }
	}
	break;
    case IDIO_A_COMPUTED_VAL_DEFINE:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("COMPUTED-VAL-DEFINE %" PRId64, gvi);

	    /* no symbol to define! */
	    IDIO_C_ASSERT (0);
	}
	break;
    case IDIO_A_PREDEFINED0:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 0 #t");
	    IDIO_THREAD_VAL (thr) = idio_S_true;
	}
	break;
    case IDIO_A_PREDEFINED1:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 1 #f");
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	}
	break;
    case IDIO_A_PREDEFINED2:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 2 #nil");
	    IDIO_THREAD_VAL (thr) = idio_S_nil;
	}
	break;
    case IDIO_A_PREDEFINED:
	{
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO pd = idio_vm_values_ref (vi);
	    IDIO_VM_RUN_DIS ("PREDEFINED %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (pd));
	    IDIO_THREAD_VAL (thr) = pd;
	}
	break;
    case IDIO_A_LONG_GOTO:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-GOTO +%" PRId64 "", i);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_LONG_JUMP_FALSE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-JUMP-FALSE +%" PRId64 "", i);
	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_LONG_JUMP_TRUE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-JUMP-TRUE +%" PRId64 "", i);
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_GOTO:
	{
	    int i = IDIO_THREAD_FETCH_NEXT ();
	    IDIO_VM_RUN_DIS ("SHORT-GOTO +%d", i);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_SHORT_JUMP_FALSE:
	{
	    int i = IDIO_THREAD_FETCH_NEXT ();
	    IDIO_VM_RUN_DIS ("SHORT-JUMP-FALSE +%d", i);
	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_JUMP_TRUE:
	{
	    int i = IDIO_THREAD_FETCH_NEXT ();
	    IDIO_VM_RUN_DIS ("SHORT-JUMP-TRUE +%d", i);
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_PUSH_VALUE:
	{
	    IDIO_VM_RUN_DIS ("PUSH-VALUE");
	    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_VALUE:
	{
	    IDIO_VM_RUN_DIS ("POP-VALUE");
	    IDIO_THREAD_VAL (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_REG1:
	{
	    IDIO_VM_RUN_DIS ("POP-REG1");
	    IDIO_THREAD_REG1 (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_REG2:
	{
	    IDIO_VM_RUN_DIS ("POP-REG2");
	    IDIO_THREAD_REG2 (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_EXPR:
	{
    	    idio_ai_t mci = idio_vm_fetch_varuint (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO_VM_RUN_DIS ("POP-EXPR %td", mci);
	    IDIO_THREAD_EXPR (thr) = fmci;
	    break;
	}
	break;
    case IDIO_A_POP_FUNCTION:
	{
	    IDIO_VM_RUN_DIS ("POP-FUNCTION");
	    IDIO_THREAD_FUNC (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_PRESERVE_STATE:
	{
	    IDIO_VM_RUN_DIS ("PRESERVE-STATE");
	    idio_vm_preserve_state (thr);
	}
	break;
    case IDIO_A_RESTORE_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-STATE");
	    idio_vm_restore_state (thr);
	}
	break;
    case IDIO_A_RESTORE_ALL_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-ALL-STATE");
	    idio_vm_restore_all_state (thr);
	}
	break;
    case IDIO_A_CREATE_CLOSURE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CREATE-CLOSURE @ +%" PRId64 "", i);
	    uint64_t code_len = idio_vm_fetch_varuint (thr);
	    uint64_t ssci = idio_vm_fetch_varuint (thr);
	    uint64_t dsci = idio_vm_fetch_varuint (thr);

	    IDIO ce = idio_thread_current_env ();
	    IDIO fci = idio_fixnum (ssci);
	    IDIO fgci = idio_module_get_or_set_vci (ce, fci);
	    IDIO sigstr = idio_S_nil;
	    if (idio_S_unspec != fgci) {
		sigstr = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    } else {
		fprintf (stderr, "vm cc sig: failed to find %" PRId64 " (%" PRId64 ")\n", (int64_t) IDIO_FIXNUM_VAL (fci), ssci);
	    }
	    fci = idio_fixnum (dsci);
	    fgci = idio_module_get_or_set_vci (ce, fci);
	    IDIO docstr = idio_S_nil;
	    if (idio_S_unspec != fgci) {
		docstr = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    } else {
		fprintf (stderr, "vm cc doc: failed to find %" PRId64 " (%" PRId64 ")\n", (int64_t) IDIO_FIXNUM_VAL (fci), ssci);
	    }

	    /*
	     * NB create the closure in the environment of the current
	     * module -- irrespective of whatever IDIO_THREAD_ENV(thr)
	     * we are currently working in
	     */
	    IDIO_THREAD_VAL (thr) = idio_closure (IDIO_THREAD_PC (thr) + i, code_len, IDIO_THREAD_FRAME (thr), IDIO_THREAD_ENV (thr), sigstr, docstr);
	}
	break;
    case IDIO_A_FUNCTION_INVOKE:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-INVOKE ...");
	    if (! idio_isa_primitive (IDIO_THREAD_FUNC (thr))) {
		IDIO_VM_RUN_DIS ("\n");
	    }
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
#ifdef IDIO_VM_PERF
	    idio_vm_clos_time (thr, "FUNCTION-INVOKE");
#endif

	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_REGULAR_CALL);
	}
	break;
    case IDIO_A_FUNCTION_GOTO:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-GOTO ...");
	    if (! idio_isa_primitive (IDIO_THREAD_FUNC (thr))) {
		IDIO_VM_RUN_DIS ("\n");
	    }
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
#ifdef IDIO_VM_PERF
	    idio_vm_clos_time (thr, "FUNCTION-GOTO");
#endif

	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_TAIL_CALL);
	}
	break;
    case IDIO_A_RETURN:
	{
	    IDIO marker = IDIO_THREAD_STACK_POP ();
	    if (idio_SM_return != marker) {
		idio_debug ("RETURN: marker: expected idio_SM_return not %s\n", marker);
		IDIO_THREAD_STACK_PUSH (marker);
		idio_vm_decode_thread (thr);
		idio_vm_panic (thr, "RETURN: unexpected stack marker");
	    }
	    IDIO ipc = IDIO_THREAD_STACK_POP ();
	    if (! IDIO_TYPE_FIXNUMP (ipc)) {
		idio_debug ("\n\nRETURN {fixnum}: not %s\n", ipc);
		idio_vm_debug (thr, "IDIO_A_RETURN", 0);
		IDIO_THREAD_STACK_PUSH (ipc);
		idio_vm_decode_thread (thr);
		idio_error_C ("RETURN: not a number", ipc, IDIO_C_FUNC_LOCATION_S ("RETURN"));

		/* notreached */
		return 0;
	    }
	    idio_ai_t pc = IDIO_FIXNUM_VAL (ipc);
	    if (pc > IDIO_IA_USIZE (idio_all_code) ||
		pc < 0) {
		fprintf (stderr, "\n\nPC= %td?\n", pc);
		idio_dump (thr, 1);
		idio_dump (IDIO_THREAD_STACK (thr), 1);
		idio_vm_decode_thread (thr);
		idio_vm_panic (thr, "RETURN: impossible PC on stack top");
	    }
	    IDIO_VM_RUN_DIS ("RETURN to %" PRIdPTR, pc);
	    IDIO_THREAD_PC (thr) = pc;
	    if (idio_vm_tracing) {
		fprintf (stderr, "%9s ", "");
		fprintf (stderr, "%7zd ", IDIO_THREAD_PC (thr));
		fprintf (stderr, "%40s", "");
		fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_out);
		idio_debug ("%s\n", IDIO_THREAD_VAL (thr));
		if (idio_vm_tracing <= 1) {
		    /* fprintf (stderr, "XXX RETURN to %td: tracing depth <= 1!\n", pc); */
		} else {
		    idio_vm_tracing--;
		}
	    }
#ifdef IDIO_VM_PERF
	    idio_vm_clos_time (thr, "RETURN");
#endif
	}
	break;
    case IDIO_A_FINISH:
	{
	    /* invoke exit handler... */
	    IDIO_VM_RUN_DIS ("FINISH\n");
	    return 0;
	}
	break;
    case IDIO_A_ABORT:
	{
	    uint64_t o = idio_vm_fetch_varuint (thr);

	    IDIO_VM_RUN_DIS ("ABORT to PC +%" PRIu64 "\n", o);

	    /*
	     * A continuation right now would just lead us back into
	     * this errant code.  We want to massage the
	     * continuation's PC to be offset by {o}.
	     */
	    IDIO k = idio_continuation (thr);
	    IDIO k_stk = IDIO_CONTINUATION_STACK (k);

	    /* continuation PC is penultimate arg */
	    idio_ai_t al = idio_array_size (k_stk);
	    IDIO I_PC = idio_array_get_index (k_stk, al - 2);
	    I_PC = idio_fixnum (IDIO_FIXNUM_VAL (I_PC) + o);
	    idio_array_insert_index (k_stk, I_PC, al - 2);

	    IDIO dosh = idio_open_output_string_handle_C ();
	    idio_display_C ("ABORT to toplevel (PC ", dosh);
	    idio_display (I_PC, dosh);
	    idio_display_C (")", dosh);

	    /* ABORT to main should be in slot #0 */
	    idio_array_insert_index (idio_vm_krun, IDIO_LIST2 (k, idio_get_output_string (dosh)), 1);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME1:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 1");
	    /* no args, no need to pull an empty list ref */
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (1);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME2:
	{
	    uint64_t aci = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 2, args %" PRIu64, aci);
	    IDIO frame = idio_frame_allocate (2);
	    IDIO_FRAME_NAMES (frame) = idio_fixnum (aci);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME3:
	{
	    uint64_t aci = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 3, args %" PRIu64, aci);
	    IDIO frame = idio_frame_allocate (3);
	    IDIO_FRAME_NAMES (frame) = idio_fixnum (aci);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME4:
	{
	    uint64_t aci = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 4, args %" PRIu64, aci);
	    IDIO frame = idio_frame_allocate (4);
	    IDIO_FRAME_NAMES (frame) = idio_fixnum (aci);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME5:
	{
	    uint64_t aci = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 5, args %" PRIu64, aci);
	    IDIO frame = idio_frame_allocate (5);
	    IDIO_FRAME_NAMES (frame) = idio_fixnum (aci);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t aci = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME %" PRId64 ", args %" PRIu64, i, aci);
	    IDIO frame = idio_frame_allocate (i);
	    IDIO_FRAME_NAMES (frame) = idio_fixnum (aci);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    uint64_t aci = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-DOTTED-FRAME %" PRId64 ", args %" PRIu64, arity, aci);
	    IDIO vs = idio_frame_allocate (arity);
	    IDIO_FRAME_NAMES (vs) = idio_fixnum (aci);
	    idio_frame_update (vs, 0, arity - 1, idio_S_nil);
	    IDIO_THREAD_VAL (thr) = vs;
	}
	break;
    case IDIO_A_REUSE_FRAME:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("REUSE-FRAME %" PRId64 "", i);
	    fprintf (stderr, "\n");
	    IDIO fr = IDIO_THREAD_FRAME (thr);
	    while (idio_S_nil != fr) {
		idio_dump (fr, 16);
		fr = IDIO_FRAME_NEXT (fr);
	    }
	    idio_ai_t ai = IDIO_FRAME_NARGS (IDIO_THREAD_FRAME (thr));
	    if (i > ai) {
	    } else {
		for (; ai > i; ai--) {
		    /* idio_array_pop (IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr))); */
		}
	    }
	    for (ai = 0; ai < i; ai++) {
		/* idio_array_insert_index (IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr)), idio_S_undef, ai); */
	    }
	    /* idio_array_insert_index (IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr)), idio_S_nil, ai); */
	    IDIO_FRAME_NARGS (IDIO_THREAD_FRAME (thr)) = i;
	    IDIO_THREAD_VAL (thr) = IDIO_THREAD_FRAME (thr);
	    fprintf (stderr, "->\n");
	    idio_dump (IDIO_THREAD_FRAME (thr), 16);
	}
	break;
    case IDIO_A_POP_FRAME0:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 0");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 0, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME1:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 1");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 1, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME2:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 2");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 2, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME3:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 3");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 3, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME:
	{
	    uint64_t rank = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("POP-FRAME %" PRId64 "", rank);
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, rank, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_EXTEND_FRAME:
	{
	    IDIO_VM_RUN_DIS ("EXTEND-FRAME");
	    IDIO_THREAD_FRAME (thr) = idio_frame_extend (IDIO_THREAD_FRAME (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_UNLINK_FRAME:
	{
	    IDIO_VM_RUN_DIS ("UNLINK-FRAME");
	    IDIO_THREAD_FRAME (thr) = IDIO_FRAME_NEXT (IDIO_THREAD_FRAME (thr));
	}
	break;
    case IDIO_A_PACK_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("PACK-FRAME %" PRId64 "", arity);
	    idio_vm_listify (IDIO_THREAD_VAL (thr), arity);
	}
	break;
    case IDIO_A_POP_CONS_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);

	    IDIO_VM_RUN_DIS ("POP-CONS-FRAME %" PRId64 "", arity);
	    idio_frame_update (IDIO_THREAD_VAL (thr),
			       0,
			       arity,
			       idio_pair (IDIO_THREAD_STACK_POP (),
					  idio_frame_fetch (IDIO_THREAD_VAL (thr), 0, arity)));
	}
	break;
    case IDIO_A_ARITY1P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=1?");
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NARGS (val);
	    }
	    if (1 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 0, IDIO_C_FUNC_LOCATION_S ("ARITY1P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITY2P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=2?");
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NARGS (val);
	    }
	    if (2 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 1, IDIO_C_FUNC_LOCATION_S ("ARITY2P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITY3P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=3?");
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NARGS (val);
	    }
	    if (3 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 2, IDIO_C_FUNC_LOCATION_S ("ARITY3P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITY4P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=4?");
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NARGS (val);
	    }
	    if (4 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 3, IDIO_C_FUNC_LOCATION_S ("ARITY4P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYEQP:
	{
	    uint64_t arityp1 = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ARITY=? %" PRId64 "", arityp1);
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NARGS (val);
	    }
	    if (arityp1 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, arityp1 - 1, IDIO_C_FUNC_LOCATION_S ("ARITYEQP"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYGEP:
	{
	    uint64_t arityp1 = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ARITY>=? %" PRId64 "", arityp1);
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NARGS (val);
	    }
	    if (nargs < arityp1) {
		idio_vm_error_arity_varargs (ins, thr, nargs - 1, arityp1 - 1, IDIO_C_FUNC_LOCATION_S ("ARITYGEP"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_CONSTANT_0:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 0");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (0);
	}
	break;
    case IDIO_A_CONSTANT_1:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 1");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (1);
	}
	break;
    case IDIO_A_CONSTANT_2:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 2");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (2);
	}
	break;
    case IDIO_A_CONSTANT_3:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 3");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (3);
	}
	break;
    case IDIO_A_CONSTANT_4:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 4");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (4);
	}
	break;
    case IDIO_A_FIXNUM:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("FIXNUM"), "FIXNUM OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_FIXNUM:
	{
	    int64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-FIXNUM"), "FIXNUM OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_CHARACTER:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CHARACTER %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CHARACTER"), "CHARACTER OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CHARACTER ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_CHARACTER:
	{
	    int64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-CHARACTER %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-CHARACTER"), "CHARACTER OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CHARACTER ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CONSTANT %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT"), "CONSTANT OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT_IDIO ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-CONSTANT %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-CONSTANT"), "CONSTANT OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT_IDIO ((intptr_t) v);
	}
	break;
    case IDIO_A_UNICODE:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("UNICODE %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("UNICODE"), "UNICODE OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_UNICODE ((intptr_t) v);
	}
	break;
    case IDIO_A_NOP:
	{
	    IDIO_VM_RUN_DIS ("NOP");
	}
	break;
    case IDIO_A_PRIMCALL0:
	{
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_values_ref (vi);
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 0);
	    }

	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) ();
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL0_NEWLINE:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 newline");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("newline", thr, 0);
	    }
	    IDIO_THREAD_VAL (thr) = idio_unicode_lookup ("newline");
	}
	break;
    case IDIO_A_PRIMCALL0_READ:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 read");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("read", thr, 0);
	    }
	    IDIO_THREAD_VAL (thr) = idio_read (IDIO_THREAD_INPUT_HANDLE (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1:
	{
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_values_ref (vi);
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 1);
	    }

	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) (IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_HEAD:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 head");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("head", thr, 1);
	    }
	    IDIO_THREAD_VAL (thr) = idio_list_head (IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_TAIL:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 tail");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("tail", thr, 1);
	    }
	    IDIO_THREAD_VAL (thr) = idio_list_tail (IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_PAIRP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 pair?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("pair?", thr, 1);
	    }
	    if (idio_isa_pair (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_SYMBOLP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 symbol?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("symbol?", thr, 1);
	    }
	    if (idio_isa_symbol (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_DISPLAY:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 display");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("display", thr, 1);
	    }
	    IDIO h = IDIO_THREAD_OUTPUT_HANDLE (thr);
	    char *vs = idio_display_string (IDIO_THREAD_VAL (thr));
	    IDIO_HANDLE_M_PUTS (h) (h, vs, strlen (vs));
	    free (vs);
	}
	break;
    case IDIO_A_PRIMCALL1_PRIMITIVEP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 primitive?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("primitive?", thr, 1);
	    }
	    if (idio_isa_primitive (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_NULLP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 null?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("null?", thr, 1);
	    }
	    if (idio_S_nil == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_CONTINUATIONP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 continuation?");
	    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONTINUATION"), "continuation?");

	    /* notreached */
	    return 0;
	}
	break;
    case IDIO_A_PRIMCALL1_EOFP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 eof?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("eof?", thr, 1);
	    }
	    if (idio_eofp_handle (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_SET_CUR_MOD:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 %%set-current-module!");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("%set-current-module!", thr, 1);
	    }
	    idio_thread_set_current_module (IDIO_THREAD_VAL (thr));
	    IDIO_THREAD_VAL (thr) = idio_S_unspec;
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2:
	{
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_values_ref (vi);
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 2);
	    }

	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_PAIR:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 pair");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("pair", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_pair (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_EQP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 eq?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("eq?", thr, 2);
	    }
	    if (idio_eqp (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_SET_HEAD:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 set-head!");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("set-head!", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_pair_set_head (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_SET_TAIL:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 set-tail!");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("set-tail!", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_pair_set_tail (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_ADD:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 add");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("add", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_add (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								       IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_SUBTRACT:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 subtract");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("subtract", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_subtract (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									    IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_EQ:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 =");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("=", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_eq (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_LT:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 <");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("<", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_lt (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_GT:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 >");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (">", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_gt (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_MULTIPLY:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 *");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("*", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_multiply (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									    IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_LE:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 <=");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("<=", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_le (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_GE:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 >=");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (">=", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_ge (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_REMAINDER:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 remainder");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("remainder", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_remainder (IDIO_THREAD_REG1 (thr),
								 IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_EXPANDER:
	{
	    /*
	     * A slight dance, here.
	     *
	     * During *compilation* the expander's mci was mapped to a
	     * gci in idio_expander_module.
	     *
	     * However, during runtime it should be available as an
	     * mci in the current envronment.
	     */
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO_VM_RUN_DIS ("EXPANDER %" PRId64 "", mci);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO ce = idio_thread_current_env ();
	    IDIO fgci = idio_module_get_vci (ce, fmci);
	    IDIO sym = idio_S_unspec;
	    if (idio_S_unspec != fgci) {
		sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    }

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);

	    IDIO sk_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_unspec == sk_ce) {
		sk_ce = IDIO_LIST5 (idio_S_toplevel, idio_fixnum (mci), fgvi, ce, idio_string_C ("IDIO_A_EXPANDER"));
		idio_module_set_symbol (sym, sk_ce, ce);
	    } else {
		IDIO_PAIR_HTT (sk_ce) = fgvi;
	    }

	    idio_install_expander (sym, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_INFIX_OPERATOR:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    uint64_t pri = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("INFIX-OPERATOR %" PRId64 "", mci);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_vci (idio_operator_module, fmci);
	    IDIO sym = idio_S_unspec;
	    if (idio_S_unspec != fgci) {
		sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    }

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (idio_operator_module, idio_fixnum (mci), fgvi);

	    IDIO sk_ce = idio_module_find_symbol (sym, idio_operator_module);

	    if (idio_S_unspec == sk_ce) {
		sk_ce = IDIO_LIST5 (idio_S_toplevel, idio_fixnum (mci), fgvi, idio_operator_module, idio_string_C ("IDIO_A_INFIX_OPERATOR"));
		idio_module_set_symbol (sym, sk_ce, idio_operator_module);
	    } else {
		IDIO_PAIR_HTT (sk_ce) = fgvi;
	    }

	    /*
	     * XXX overrides any existing name
	     */
	    idio_set_property (IDIO_THREAD_VAL (thr), idio_KW_name, sym);
	    idio_install_infix_operator (sym, IDIO_THREAD_VAL (thr), pri);
	}
	break;
    case IDIO_A_POSTFIX_OPERATOR:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    uint64_t pri = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("POSTFIX-OPERATOR %" PRId64 "", mci);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO ce = idio_thread_current_env ();
	    IDIO fgci = idio_module_get_vci (idio_operator_module, fmci);
	    IDIO sym = idio_S_unspec;
	    if (idio_S_unspec != fgci) {
		sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    }

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (idio_operator_module, idio_fixnum (mci), fgvi);

	    IDIO sk_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_unspec == sk_ce) {
		sk_ce = IDIO_LIST5 (idio_S_toplevel, idio_fixnum (mci), fgvi, idio_operator_module, idio_string_C ("IDIO_A_POSTFIX_OPERATOR"));
		idio_module_set_symbol (sym, sk_ce, idio_operator_module);
	    } else {
		IDIO_PAIR_HTT (sk_ce) = fgvi;
	    }

	    /*
	     * XXX overrides any existing name
	     */
	    idio_set_property (IDIO_THREAD_VAL (thr), idio_KW_name, sym);
	    idio_install_postfix_operator (sym, IDIO_THREAD_VAL (thr), pri);
	}
	break;
    case IDIO_A_PUSH_DYNAMIC:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO_VM_RUN_DIS ("PUSH-DYNAMIC %" PRId64, mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		idio_vm_push_dynamic (gvi, thr, IDIO_THREAD_VAL (thr));
	    } else {
		idio_vm_panic (thr, "PUSH-DYNAMIC: no gvi!");
	    }
	}
	break;
    case IDIO_A_POP_DYNAMIC:
	{
	    IDIO_VM_RUN_DIS ("POP-DYNAMIC");
	    idio_vm_pop_dynamic (thr);
	}
	break;
    case IDIO_A_DYNAMIC_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO_VM_RUN_DIS ("DYNAMIC-SYM-REF %" PRId64 "", mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (mci, gvi, thr, idio_S_nil);
	    } else {
		idio_vm_panic (thr, "DYNAMIC-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO_VM_RUN_DIS ("DYNAMIC-FUNCTION-SYM-REF %" PRId64 "", mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (mci, gvi, thr, idio_S_nil);
	    } else {
		idio_vm_panic (thr, "DYNAMIC-FUNCTION-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_PUSH_ENVIRON:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO_VM_RUN_DIS ("PUSH-ENVIRON %" PRId64, mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		idio_vm_push_environ (mci, gvi, thr, IDIO_THREAD_VAL (thr));
	    } else {
		idio_vm_panic (thr, "PUSH-ENVIRON: no gvi!");
	    }
	}
	break;
    case IDIO_A_POP_ENVIRON:
	{
	    IDIO_VM_RUN_DIS ("POP-ENVIRON");
	    idio_vm_pop_environ (thr);
	}
	break;
    case IDIO_A_ENVIRON_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);
	    IDIO_VM_RUN_DIS ("ENVIRON-SYM-REF %" PRId64 "", mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_environ_ref (mci, gvi, thr, idio_S_nil);
	    } else {
		idio_vm_panic (thr, "ENVIRON-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_NON_CONT_ERR:
	{
	    IDIO_VM_RUN_DIS ("NON-CONT-ERROR\n");
	    idio_raise_condition (idio_S_false, idio_struct_instance (idio_condition_idio_error_type,
								      IDIO_LIST3 (idio_string_C ("non-cont-error"),
										  IDIO_C_FUNC_LOCATION_S ("NON_CONT_ERR"),
										  IDIO_THREAD_VAL (thr))));

	    /* notreached */
	    return 0;
	}
	break;
    case IDIO_A_PUSH_TRAP:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr);

	    IDIO_VM_RUN_DIS ("PUSH-TRAP %" PRId64 "", mci);
	    idio_vm_push_trap (thr, IDIO_THREAD_VAL (thr), idio_fixnum (mci));
	}
	break;
    case IDIO_A_POP_TRAP:
	{
	    IDIO_VM_RUN_DIS ("POP-TRAP");
	    idio_vm_pop_trap (thr);
	}
	break;
    case IDIO_A_RESTORE_TRAP:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-TRAP");
	    idio_vm_restore_trap (thr);
	}
	break;
    default:
	{
	    idio_ai_t pc = IDIO_THREAD_PC (thr);
	    idio_ai_t pcm = pc + 10;
	    pc = pc - 10;
	    if (pc < 0) {
		pc = 0;
	    }
	    if (pc % 10) {
		idio_ai_t pc1 = pc - (pc % 10);
		fprintf (stderr, "\n  %5td ", pc1);
		for (; pc1 < pc; pc1++) {
		    fprintf (stderr, "    ");
		}
	    }
	    for (; pc < pcm; pc++) {
		if (0 == (pc % 10)) {
		    fprintf (stderr, "\n  %5td ", pc);
		}
		fprintf (stderr, "%3d ", IDIO_IA_AE (idio_all_code, pc));
	    }
	    fprintf (stderr, "\n");
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d @%" PRId64 "\n", ins, IDIO_THREAD_PC (thr) - 1);

	    /* notreached */
	    return 0;
	}
	break;
    }

#ifdef IDIO_VM_PERF
    struct timespec ins_te;
    if (0 != clock_gettime (CLOCK_MONOTONIC, &ins_te)) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ins_te)");
    }

    struct timespec ins_td;
    ins_td.tv_sec = ins_te.tv_sec - ins_t0.tv_sec;
    ins_td.tv_nsec = ins_te.tv_nsec - ins_t0.tv_nsec;
    if (ins_td.tv_nsec < 0) {
	ins_td.tv_nsec += IDIO_VM_NS;
	ins_td.tv_sec -= 1;
    }

    idio_vm_ins_call_time[ins].tv_sec += ins_td.tv_sec;
    idio_vm_ins_call_time[ins].tv_nsec += ins_td.tv_nsec;
    if (idio_vm_ins_call_time[ins].tv_nsec > IDIO_VM_NS) {
	idio_vm_ins_call_time[ins].tv_nsec -= IDIO_VM_NS;
	idio_vm_ins_call_time[ins].tv_sec += 1;
    }
#endif

    IDIO_VM_RUN_DIS ("\n");
    return 1;
}

void idio_vm_dasm (IDIO thr, idio_ai_t pc0, idio_ai_t pce)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (pc0 < 0) {
	pc0 = IDIO_THREAD_PC (thr);
    }

    if (0 == pce) {
	pce = IDIO_IA_USIZE (idio_all_code);
    }

    if (pc0 > pce) {
	fprintf (stderr, "\n\nPC %" PRIdPTR " > max code PC %" PRIdPTR"\n", pc0, pce);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %.1000s\n", IDIO_THREAD_STACK (thr));
	idio_vm_panic (thr, "vm-dasm: bad PC!");
    }

    IDIO_VM_DASM ("idio_vm_dasm: thr %p pc0 %6zd pce %6zd\n", thr, pc0, pce);

    IDIO hints = IDIO_HASH_EQP (256);

    idio_ai_t pc = pc0;
    idio_ai_t *pcp = &pc;

    for (; pc < pce;) {
	IDIO hint = idio_hash_get (hints, idio_fixnum (pc));
	if (idio_S_unspec != hint) {
	    char *hint_C = idio_as_string (hint, 40);
	    IDIO_VM_DASM ("%-20s ", hint_C);
	    free (hint_C);
	} else {
	    IDIO_VM_DASM ("%20s ", "");
	}

	IDIO_VM_DASM ("%6zd ", pc);

	IDIO_I ins = IDIO_IA_GET_NEXT (pcp);

	IDIO_VM_DASM ("%3d: ", ins);

	switch (ins) {
	case IDIO_A_SHALLOW_ARGUMENT_REF0:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 0");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF1:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 1");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF2:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 2");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF3:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 3");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF:
	    {
		uint64_t j = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_REF:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		uint64_t j = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
	    }
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET0:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 0");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET1:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 1");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET2:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 2");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET3:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 3");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		uint64_t j = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    }
	    break;
	case IDIO_A_GLOBAL_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("GLOBAL-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_GLOBAL_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("GLOBAL-FUNCTION-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-FUNCTION-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_CONSTANT_SYM_REF:
	    {
		idio_ai_t mci = idio_vm_get_varuint (pcp);

		IDIO fmci = idio_fixnum (mci);
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_constants_ref (gci);

		IDIO_VM_DASM ("CONSTANT %td", mci);
		idio_debug_FILE (idio_dasm_FILE, " %s", c);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_GLOBAL_SYM_DEF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);
		uint64_t mkci = idio_vm_get_varuint (pcp);

		IDIO ce = idio_thread_current_env ();
		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		/* IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci)); */

		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
		/* IDIO kind = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgkci)); */

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("GLOBAL-SYM-DEF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("GLOBAL-SYM-DEF %" PRId64 " %s", mci, idio_type2string (sym));
		    idio_debug_FILE (idio_dasm_FILE, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_GLOBAL_SYM_SET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("GLOBAL-SYM-SET %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("GLOBAL-SYM-SET %" PRId64 " %s", mci, idio_type2string (sym));
		    idio_debug_FILE (idio_dasm_FILE, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_SET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-SET %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_DEFINE:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-DEFINE %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_GLOBAL_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("GLOBAL-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_GLOBAL_FUNCTION_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("GLOBAL-FUNCTION-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-FUNCTION-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_CONSTANT_VAL_REF:
	    {
		idio_ai_t gvi = idio_vm_get_varuint (pcp);

		IDIO fgvi = idio_fixnum (gvi);
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fgvi);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_constants_ref (gci);

		IDIO_VM_DASM ("CONSTANT %td", gvi);
		idio_debug_FILE (idio_dasm_FILE, " %s", c);
	    }
	    break;
	case IDIO_A_COMPUTED_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("COMPUTED-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_GLOBAL_VAL_DEF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);
		uint64_t mkci = idio_vm_get_varuint (pcp);

		IDIO ce = idio_thread_current_env ();
		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (gvi));
		/* IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci)); */

		IDIO val = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
		/* IDIO kind = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgkci)); */

		if (idio_isa_symbol (val)) {
		    IDIO_VM_DASM ("GLOBAL-VAL-DEF %" PRId64 " %s", gvi, IDIO_SYMBOL_S (val));
		} else {
		    IDIO_VM_DASM ("GLOBAL-VAL-DEF %" PRId64 " %s", gvi, idio_type2string (val));
		    idio_debug_FILE (idio_dasm_FILE, " %s", val);
		}
	    }
	    break;
	case IDIO_A_GLOBAL_VAL_SET:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("GLOBAL-VAL-SET %" PRId64, gvi);
	    }
	    break;
	case IDIO_A_COMPUTED_VAL_SET:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("COMPUTED-VAL-SET %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_COMPUTED_VAL_DEFINE:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("COMPUTED-VAL-DEFINE %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_PREDEFINED0:
	    {
		IDIO_VM_DASM ("PREDEFINED 0 #t");
	    }
	    break;
	case IDIO_A_PREDEFINED1:
	    {
		IDIO_VM_DASM ("PREDEFINED 1 #f");
	    }
	    break;
	case IDIO_A_PREDEFINED2:
	    {
		IDIO_VM_DASM ("PREDEFINED 2 #nil");
	    }
	    break;
	case IDIO_A_PREDEFINED:
	    {
		uint64_t vi = idio_vm_get_varuint (pcp);
		IDIO pd = idio_vm_values_ref (vi);
		if (idio_isa_primitive (pd)) {
		    IDIO_VM_DASM ("PREDEFINED %" PRId64 " PRIM %s", vi, IDIO_PRIMITIVE_NAME (pd));
		} else {
		    IDIO_VM_DASM ("PREDEFINED %" PRId64 " %s", vi, idio_type2string (pd));
		}
	    }
	    break;
	case IDIO_A_LONG_GOTO:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		char h[BUFSIZ];
		sprintf (h, "LG@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("LONG-GOTO +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_LONG_JUMP_FALSE:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		char h[BUFSIZ];
		sprintf (h, "LJF@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("LONG-JUMP-FALSE +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_LONG_JUMP_TRUE:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		char h[BUFSIZ];
		sprintf (h, "LJT@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("LONG-JUMP-TRUE +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_GOTO:
	    {
		int i = IDIO_IA_GET_NEXT (pcp);
		char h[BUFSIZ];
		sprintf (h, "SG@%td", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("SHORT-GOTO +%d %td", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_FALSE:
	    {
		int i = IDIO_IA_GET_NEXT (pcp);
		char h[BUFSIZ];
		sprintf (h, "SJF@%td", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("SHORT-JUMP-FALSE +%d %td", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_TRUE:
	    {
		int i = IDIO_IA_GET_NEXT (pcp);
		char h[BUFSIZ];
		sprintf (h, "SJT@%td", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("SHORT-JUMP-TRUE %d %td", i, pc + i);
	    }
	    break;
	case IDIO_A_PUSH_VALUE:
	    {
		IDIO_VM_DASM ("PUSH-VALUE");
	    }
	    break;
	case IDIO_A_POP_VALUE:
	    {
		IDIO_VM_DASM ("POP-VALUE");
	    }
	    break;
	case IDIO_A_POP_REG1:
	    {
		IDIO_VM_DASM ("POP-REG1");
	    }
	    break;
	case IDIO_A_POP_REG2:
	    {
		IDIO_VM_DASM ("POP-REG2");
	    }
	    break;
	case IDIO_A_POP_EXPR:
	    {
		idio_ai_t mci = idio_vm_get_varuint (pcp);

		IDIO fmci = idio_fixnum (mci);
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_constants_ref (gci);

		IDIO_VM_DASM ("POP-EXPR %td", mci);
		idio_debug_FILE (idio_dasm_FILE, " %s", c);
	    }
	    break;
	case IDIO_A_POP_FUNCTION:
	    {
		IDIO_VM_DASM ("POP-FUNCTION");
	    }
	    break;
	case IDIO_A_PRESERVE_STATE:
	    {
		IDIO_VM_DASM ("PRESERVE-STATE");
	    }
	    break;
	case IDIO_A_RESTORE_STATE:
	    {
		IDIO_VM_DASM ("RESTORE-STATE");
	    }
	    break;
	case IDIO_A_RESTORE_ALL_STATE:
	    {
		IDIO_VM_DASM ("RESTORE-ALL-STATE");
	    }
	    break;
	case IDIO_A_CREATE_CLOSURE:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		uint64_t code_len = idio_vm_get_varuint (pcp);
		uint64_t ssci = idio_vm_get_varuint (pcp);
		uint64_t dsci = idio_vm_get_varuint (pcp);

		char h[BUFSIZ];
		sprintf (h, "C@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("CREATE-CLOSURE @ +%" PRId64 " %" PRId64 "", i, pc + i);

		IDIO ce = idio_thread_current_env ();
		IDIO fssci = idio_fixnum (ssci);
		IDIO fgssci = idio_module_get_or_set_vci (ce, fssci);
		IDIO ss = idio_S_nil;
		if (idio_S_unspec != fgssci) {
		    ss = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgssci));
		} else {
		    fprintf (stderr, "vm cc sig: failed to find %" PRIu64 "\n", ssci);
		}
		char *ids = idio_display_string (ss);
		IDIO_VM_DASM (" (%s)", ids);
		free (ids);

		IDIO fdsci = idio_fixnum (dsci);
		IDIO fgdsci = idio_module_get_or_set_vci (ce, fdsci);
		IDIO ds = idio_S_nil;
		if (idio_S_unspec != fgdsci) {
		    ds = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgdsci));
		} else {
		    fprintf (stderr, "vm cc doc: failed to find %" PRIu64 "\n", dsci);
		}
		if (idio_S_nil != ds) {
		    ids = idio_display_string (ds);
		    IDIO_VM_DASM ("\n%s", ids);
		    free (ids);
		}
	    }
	    break;
	case IDIO_A_FUNCTION_INVOKE:
	    {
		IDIO_VM_DASM ("FUNCTION-INVOKE ... ");
	    }
	    break;
	case IDIO_A_FUNCTION_GOTO:
	    {
		IDIO_VM_DASM ("FUNCTION-GOTO ...");
	    }
	    break;
	case IDIO_A_RETURN:
	    {
		IDIO_VM_DASM ("RETURN\n");
	    }
	    break;
	case IDIO_A_ABORT:
	    {
		uint64_t o = idio_vm_get_varuint (pcp);
		char h[BUFSIZ];
		sprintf (h, "A@%" PRId64 "", pc + o);
		idio_hash_put (hints, idio_fixnum (pc + o), idio_symbols_C_intern (h));
		IDIO_VM_DASM ("ABORT to PC +%" PRIu64 " %td", o, pc + o);
	    }
	    break;
	case IDIO_A_FINISH:
	    {
		IDIO_VM_DASM ("FINISH");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME1:
	    {
		/* no args, no need to pull an empty list ref */
		IDIO_VM_DASM ("ALLOCATE-FRAME 1");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME2:
	    {
		uint64_t aci = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME 2, args %" PRIu64, aci);
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME3:
	    {
		uint64_t aci = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME 3, args %" PRIu64, aci);
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME4:
	    {
		uint64_t aci = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME 4, args %" PRIu64, aci);
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME5:
	    {
		uint64_t aci = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME 5, args %" PRIu64, aci);
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME:
	    {
		uint64_t i = idio_vm_get_varuint (pcp);
		uint64_t aci = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME %" PRId64 ", args %" PRIu64, i, aci);
	    }
	    break;
	case IDIO_A_ALLOCATE_DOTTED_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (pcp);
		uint64_t aci = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ALLOCATE-DOTTED-FRAME %" PRId64 ", args %" PRIu64, arity, aci);
	    }
	    break;
	case IDIO_A_POP_FRAME0:
	    {
		IDIO_VM_DASM ("POP-FRAME 0");
	    }
	    break;
	case IDIO_A_POP_FRAME1:
	    {
		IDIO_VM_DASM ("POP-FRAME 1");
	    }
	    break;
	case IDIO_A_POP_FRAME2:
	    {
		IDIO_VM_DASM ("POP-FRAME 2");
	    }
	    break;
	case IDIO_A_POP_FRAME3:
	    {
		IDIO_VM_DASM ("POP-FRAME 3");
	    }
	    break;
	case IDIO_A_POP_FRAME:
	    {
		uint64_t rank = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("POP-FRAME %" PRId64 "", rank);
	    }
	    break;
	case IDIO_A_EXTEND_FRAME:
	    {
		IDIO_VM_DASM ("EXTEND-FRAME");
	    }
	    break;
	case IDIO_A_UNLINK_FRAME:
	    {
		IDIO_VM_DASM ("UNLINK-FRAME");
	    }
	    break;
	case IDIO_A_PACK_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("PACK-FRAME %" PRId64 "", arity);
	    }
	    break;
	case IDIO_A_POP_CONS_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (pcp);

		IDIO_VM_DASM ("POP-CONS-FRAME %" PRId64 "", arity);
	    }
	    break;
	case IDIO_A_ARITY1P:
	    {
		IDIO_VM_DASM ("ARITY=1?");
	    }
	    break;
	case IDIO_A_ARITY2P:
	    {
		IDIO_VM_DASM ("ARITY=2?");
	    }
	    break;
	case IDIO_A_ARITY3P:
	    {
		IDIO_VM_DASM ("ARITY=3?");
	    }
	    break;
	case IDIO_A_ARITY4P:
	    {
		IDIO_VM_DASM ("ARITY=4?");
	    }
	    break;
	case IDIO_A_ARITYEQP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ARITY=? %" PRId64 "", arityp1);
	    }
	    break;
	case IDIO_A_ARITYGEP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("ARITY>=? %" PRId64 "", arityp1);
	    }
	    break;
	case IDIO_A_CONSTANT_0:
	    {
		IDIO_VM_DASM ("CONSTANT 0");
	    }
	    break;
	case IDIO_A_CONSTANT_1:
	    {
		IDIO_VM_DASM ("CONSTANT 1");
	    }
	    break;
	case IDIO_A_CONSTANT_2:
	    {
		IDIO_VM_DASM ("CONSTANT 2");
	    }
	    break;
	case IDIO_A_CONSTANT_3:
	    {
		IDIO_VM_DASM ("CONSTANT 3");
	    }
	    break;
	case IDIO_A_CONSTANT_4:
	    {
		IDIO_VM_DASM ("CONSTANT 4");
	    }
	    break;
	case IDIO_A_FIXNUM:
	    {
		uint64_t v = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("FIXNUM %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_NEG_FIXNUM:
	    {
		int64_t v = idio_vm_get_varuint (pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-FIXNUM %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_CHARACTER:
	    {
		uint64_t v = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("CHARACTER %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_NEG_CHARACTER:
	    {
		int64_t v = idio_vm_get_varuint (pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-CHARACTER %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_CONSTANT:
	    {
		uint64_t v = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("CONSTANT %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_NEG_CONSTANT:
	    {
		uint64_t v = idio_vm_get_varuint (pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-CONSTANT %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_UNICODE:
	    {
		uint64_t v = idio_vm_get_varuint (pcp);
		IDIO_VM_DASM ("UNICODE %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_NOP:
	    {
		IDIO_VM_DASM ("NOP");
	    }
	    break;
	case IDIO_A_PRIMCALL0:
	    {
		uint64_t vi = idio_vm_get_varuint (pcp);
		IDIO primdata = idio_vm_values_ref (vi);
		IDIO_VM_DASM ("PRIMITIVE0 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    }
	    break;
	case IDIO_A_PRIMCALL0_NEWLINE:
	    {
		IDIO_VM_DASM ("PRIMITIVE0 newline");
	    }
	    break;
	case IDIO_A_PRIMCALL0_READ:
	    {
		IDIO_VM_DASM ("PRIMITIVE0 read");
	    }
	    break;
	case IDIO_A_PRIMCALL1:
	    {
		uint64_t vi = idio_vm_get_varuint (pcp);
		IDIO primdata = idio_vm_values_ref (vi);
		IDIO_VM_DASM ("PRIMITIVE1 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    }
	    break;
	case IDIO_A_PRIMCALL1_HEAD:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 head");
	    }
	    break;
	case IDIO_A_PRIMCALL1_TAIL:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 tail");
	    }
	    break;
	case IDIO_A_PRIMCALL1_PAIRP:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 pair?");
	    }
	    break;
	case IDIO_A_PRIMCALL1_SYMBOLP:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 symbol?");
	    }
	    break;
	case IDIO_A_PRIMCALL1_DISPLAY:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 display");
	    }
	    break;
	case IDIO_A_PRIMCALL1_PRIMITIVEP:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 primitive?");
	    }
	    break;
	case IDIO_A_PRIMCALL1_NULLP:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 null?");
	    }
	    break;
	case IDIO_A_PRIMCALL1_CONTINUATIONP:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 continuation?");
	    }
	    break;
	case IDIO_A_PRIMCALL1_EOFP:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 eof?");
	    }
	    break;
	case IDIO_A_PRIMCALL1_SET_CUR_MOD:
	    {
		IDIO_VM_DASM ("PRIMITIVE1 %%set-current-module!");
	    }
	    break;
	case IDIO_A_PRIMCALL2:
	    {
		uint64_t vi = idio_vm_get_varuint (pcp);
		IDIO primdata = idio_vm_values_ref (vi);
		IDIO_VM_DASM ("PRIMITIVE2 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    }
	    break;
	case IDIO_A_PRIMCALL2_PAIR:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 pair");
	    }
	    break;
	case IDIO_A_PRIMCALL2_EQP:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 eq?");
	    }
	    break;
	case IDIO_A_PRIMCALL2_SET_HEAD:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 set-head!");
	    }
	    break;
	case IDIO_A_PRIMCALL2_SET_TAIL:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 set-tail!");
	    }
	    break;
	case IDIO_A_PRIMCALL2_ADD:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 add");
	    }
	    break;
	case IDIO_A_PRIMCALL2_SUBTRACT:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 subtract");
	    }
	    break;
	case IDIO_A_PRIMCALL2_EQ:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 =");
	    }
	    break;
	case IDIO_A_PRIMCALL2_LT:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 <");
	    }
	    break;
	case IDIO_A_PRIMCALL2_GT:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 >");
	    }
	    break;
	case IDIO_A_PRIMCALL2_MULTIPLY:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 *");
	    }
	    break;
	case IDIO_A_PRIMCALL2_LE:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 <=");
	    }
	    break;
	case IDIO_A_PRIMCALL2_GE:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 >=");
	    }
	    break;
	case IDIO_A_PRIMCALL2_REMAINDER:
	    {
		IDIO_VM_DASM ("PRIMITIVE2 remainder");
	    }
	    break;
	case IDIO_A_EXPANDER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("EXPANDER %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_INFIX_OPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);
		uint64_t pri = idio_vm_get_varuint (pcp);

		IDIO_VM_DASM ("INFIX-OPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_POSTFIX_OPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);
		uint64_t pri = idio_vm_get_varuint (pcp);

		IDIO_VM_DASM ("POSTFIX-OPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_PUSH_DYNAMIC:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("PUSH-DYNAMIC %" PRId64, mci);
	    }
	    break;
	case IDIO_A_POP_DYNAMIC:
	    {
		IDIO_VM_DASM ("POP-DYNAMIC");
	    }
	    break;
	case IDIO_A_DYNAMIC_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("DYNAMIC-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("DYNAMIC-FUNCTION-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_PUSH_ENVIRON:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("PUSH-ENVIRON %" PRId64, mci);
	    }
	    break;
	case IDIO_A_POP_ENVIRON:
	    {
		IDIO_VM_DASM ("POP-ENVIRON");
	    }
	    break;
	case IDIO_A_ENVIRON_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("ENVIRON-SYM-REF %" PRIu64, mci);
	    }
	    break;
	case IDIO_A_NON_CONT_ERR:
	    {
		IDIO_VM_DASM ("NON-CONT-ERROR");
	    }
	    break;
	case IDIO_A_PUSH_TRAP:
	    {
		uint64_t mci = IDIO_VM_GET_REF (pcp);

		IDIO_VM_DASM ("PUSH-TRAP %" PRIu64, mci);
	    }
	    break;
	case IDIO_A_POP_TRAP:
	    {
		IDIO_VM_DASM ("POP-TRAP");
	    }
	    break;
	case IDIO_A_RESTORE_TRAP:
	    {
		IDIO_VM_DASM ("RESTORE-TRAP");
	    }
	    break;
	default:
	    {
		idio_ai_t pci = pc - 1;
		idio_ai_t pcm = pc + 10;
		pc = pc - 10;
		if (pc < 0) {
		    pc = 0;
		}
		fprintf (stderr, "idio-vm-dasm: unexpected ins %3d @%td\n", ins, pci);
		fprintf (stderr, "dumping from %td to %td\n", pc, pcm - 1);
		if (pc % 10) {
		    idio_ai_t pc1 = pc - (pc % 10);
		    fprintf (stderr, "\n  %5td ", pc1);
		    for (; pc1 < pc; pc1++) {
			fprintf (stderr, "    ");
		    }
		}
		for (; pc < pcm; pc++) {
		    if (0 == (pc % 10)) {
			fprintf (stderr, "\n  %5td ", pc);
		    }
		    fprintf (stderr, "%3d ", IDIO_IA_AE (idio_all_code, pc));
		}
		fprintf (stderr, "\n");
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d @%" PRId64 "\n", ins, pci);

		/* notreached */
		return;
	    }
	    break;
	}

	IDIO_VM_DASM ("\n");
    }
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%%idio-dasm", dasm, (IDIO args), "[c]", "\
generate the disassembler code for closure ``c`` or everything	\n\
								\n\
:param c: (optional) the closure to disassemble			\n\
:type c: closure						\n\
								\n\
The output goes to the file \"vm-dasm\" in the current directory.	\n\
It will get overwritten when Idio stops.			\n\
")
{
    IDIO_ASSERT (args);

    idio_ai_t pc0 = 0;
    idio_ai_t pce = 0;

    if (idio_isa_pair (args)) {
	IDIO c = IDIO_PAIR_H (args);
	if (idio_isa_closure (c)) {
	    pc0 = IDIO_CLOSURE_CODE_PC (c);
	    pce = pc0 + IDIO_CLOSURE_CODE_LEN (c);
	} else {
	    idio_error_param_type ("closure", c, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    idio_vm_dasm (idio_thread_current_thread (), pc0, pce);

    return idio_S_unspec;
}

void idio_vm_thread_init (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
    IDIO_C_ASSERT (tsp <= sp);

    if (0 == tsp) {
	/*
	 * Special case.  We can't call the generic idio_vm_push_trap
	 * as that assumes a sensible TRAP_SP to be pushed on the
	 * stack first.  We don't have that yet.
	 *
	 * In the meanwhile, the manual result of the stack will be
	 *
	 * #[ ... (sp)NEXT-SP CONDITION-TYPE HANDLER ]
	 *
	 * where, as this is the fallback handler, NEXT-SP points at
	 * HANDLER, ie sp+2.
	 *
	 * The CONDITION-TYPE for the fallback handler is ^condition
	 * (the base type for all other conditions).
	 *
	 * Not forgetting to set the actual TRAP_SP to sp+2 as well!
	 */
	IDIO_THREAD_STACK_PUSH (idio_fixnum (sp + 2));
	IDIO_THREAD_STACK_PUSH (idio_condition_condition_type_mci);
	IDIO_THREAD_STACK_PUSH (idio_condition_reset_condition_handler);
	IDIO_THREAD_STACK_PUSH (idio_SM_push_trap);
	IDIO_THREAD_TRAP_SP (thr) = idio_fixnum (sp + 2);
	IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
    }

    idio_vm_push_trap (thr, idio_condition_restart_condition_handler, idio_condition_condition_type_mci);
    idio_vm_push_trap (thr, idio_condition_default_condition_handler, idio_condition_condition_type_mci);
}

void idio_vm_default_pc (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * The problem for an external user, eg. idio_meaning_expander, is
     * that if the expander is a primitive then idio_vm_run() pushes
     * idio_vm_FINISH_pc on the stack expecting the code to run through
     * to the NOP/RETURN it added on the end.  However, for a
     * primitive idio_vm_invoke() will simply do it's thing without
     * changing the PC.  Which will not be about to walk into
     * NOP/RETURN.
     *
     * So we need to preset the PC to be ready to walk into
     * NOP/RETURN.
     *
     * If we put on real code the idio_vm_invoke will set PC after
     * this.
     */
    IDIO_THREAD_PC (thr) = IDIO_IA_USIZE (idio_all_code);
}

static uintptr_t idio_vm_run_loops = 0;

IDIO idio_vm_run (IDIO thr, IDIO desc)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (desc);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, desc);

    /*
     * Save a continuation in case things get ropey and we have to
     * bail out.
     */
    idio_ai_t krun_p0 = idio_array_size (idio_vm_krun);
    if (0 == krun_p0 &&
	thr != idio_expander_thread) {
	fprintf (stderr, "How is krun 0?\n");
	idio_vm_thread_state ();
    }
    /* idio_array_push (idio_vm_krun, IDIO_LIST2 (idio_continuation (thr), desc)); */

    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));

    /*
     * make sure this segment returns to idio_vm_FINISH_pc
     *
     * XXX should this be in idio_codegen_compile?
     */
    IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
    IDIO_THREAD_STACK_PUSH (idio_SM_return);
    /* idio_ia_push (idio_all_code, IDIO_A_NOP); */
    idio_ia_push (idio_all_code, IDIO_A_NOP);
    idio_ia_push (idio_all_code, IDIO_A_RETURN);

    struct timeval t0;
    gettimeofday (&t0, NULL);

    uintptr_t loops0 = idio_vm_run_loops;

    int gc_pause = idio_gc_get_pause ("idio_vm_run");

    sigjmp_buf sjb;
    sigjmp_buf *osjb = IDIO_THREAD_JMP_BUF (thr);
    IDIO_THREAD_JMP_BUF (thr) = &sjb;

    /*
     * Ready ourselves for idio_raise_condition/continuations to
     * clear the decks.
     *
     * NB Keep counters/timers above this sigsetjmp (otherwise they
     * get reset -- duh)
     */
    int sjv = sigsetjmp (*(IDIO_THREAD_JMP_BUF (thr)), 1);

    /*
     * Hmm, we really should consider caring whether we got here from
     * a siglongjmp...shouldn't we?
     *
     * I'm not sure we do care.
     */
    switch (sjv) {
    case 0:
	break;
    case IDIO_VM_SIGLONGJMP_CONDITION:
	idio_gc_reset ("idio_vm_run/condition", gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_CONTINUATION:
	idio_gc_reset ("idio_vm_run/continuation", gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_CALLCC:
	idio_gc_reset ("idio_vm_run/callcc", gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_EVENT:
	idio_gc_reset ("idio_vm_run/event", gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_EXIT:
	idio_gc_reset ("idio_vm_run/exit", gc_pause);
	idio_final ();
	exit (idio_exit_status);
	break;
    default:
	fprintf (stderr, "setjmp: unexpected value: %d\n", sjv);
	break;
    }

    /*
     * Finally, run the VM code with idio_vm_run1(), one instruction
     * at a time.
     *
     * Every so often we'll poke the GC to tidy up.  It has its own
     * view on whether it is time and/or safe to garbage collect but
     * we need to poke it to find out.
     *
     * It's also a good time to react to any asynchronous events,
     * signals. etc..
     *
     *
     * NB. Perhaps counter-inuitively, not running the GC slows things
     * down dramatically as the Unix process struggles to jump around
     * in masses of virtual memory.  What you really want to be doing
     * is constantly trimming the deadwood -- but only when you've
     * done enough work to generate some deadwood.
     *
     * That said, at the opposite extreme, calling the GC every time
     * round the loop is equally slow as you waste CPU cycles running
     * over the (same) allocated memory to little effect.
     *
     * Often enough, then, but not too often.
     *
     * You can experiment by changing the mask, Oxff, from smallest
     * (0x0) to large (0xffffffff).  s9-test.idio has enough happening
     * to show the effect of using a large value -- virtual memory
     * usage should reach 1+GB.
     *
     * Setting it very small is a good way to find out if you have
     * successfully protected all your values in C-land.  Random SEGVs
     * occur if you haven't.
     */
    for (;;) {
	if (idio_vm_run1 (thr)) {

	    /*
	     * Has anything interesting happened of late while we were
	     * busy doing other things?
	     */
	    int signum;
	    for (signum = IDIO_LIBC_FSIG; signum <= IDIO_LIBC_NSIG; signum++) {
		if (idio_command_signal_record[signum]) {
		    idio_command_signal_record[signum] = 0;

		    IDIO signal_condition = idio_array_get_index (idio_vm_signal_handler_conditions, (idio_ai_t) signum);
		    if (idio_S_nil != signal_condition) {
			idio_vm_raise_condition (idio_S_true, signal_condition, 1);
		    } else {
			fprintf (stderr, "ivm_r signal %d has no condition?\n", signum);
			idio_error_C ("signal without a condition to raise", idio_fixnum (signum), IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }

		    IDIO signal_handler_name = idio_array_ref (idio_vm_signal_handler_name, idio_fixnum (signum));
		    if (idio_S_nil == signal_handler_name) {
			fprintf (stderr, "iv_r raising signal %d: no handler name\n", signum);
			idio_debug ("iv_r ivshn %s\n", idio_vm_signal_handler_name);
			IDIO_C_ASSERT (0);
		    }
		    IDIO signal_handler_exists = idio_module_find_symbol_recurse (signal_handler_name, idio_Idio_module, 1);
		    IDIO idio_vm_signal_handler = idio_S_nil;
		    if (idio_S_unspec != signal_handler_exists) {
			idio_vm_signal_handler = idio_module_symbol_value_recurse (signal_handler_name, idio_Idio_module, idio_S_nil);
		    }

		    if (idio_S_nil != idio_vm_signal_handler) {
			/*
			 * We're about to call an event handler which
			 * could be either a primitive or a closure.
			 *
			 * Either way, we are in the middle of some
			 * sequence of instructions -- we are *not* at a
			 * safe point, eg. in between two lines of source
			 * code (assuming such a point would itself be
			 * "safe").  So we need to preserve all state on
			 * the stack.
			 *
			 * Including the current PC.  We'll replace it
			 * with idio_vm_IHR_pc which knows how to unpick
			 * what we've just pushed onto the stack and
			 * RETURN to the current PC.
			 *
			 * In case the handler is a closure, we need to
			 * create an empty argument frame in
			 * IDIO_THREAD_VAL(thr).
			 *
			 * If the handler is a primitive then it'll run
			 * through to completion and we'll immediately
			 * start running idio_vm_IHR_pc to get us back to
			 * where we interrupted.  All good.
			 *
			 * If it is a closure then we need to invoke it
			 * such that it will return back to the current
			 * PC, idio_vm_IHR_pc.  It we must invoke it as a
			 * regular call, ie. not in tail position.
			 */

			IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
			IDIO_THREAD_STACK_PUSH (idio_SM_return);
			idio_vm_preserve_all_state (thr);
			/* IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_IHR_pc));  */
			IDIO_THREAD_PC (thr) = idio_vm_IHR_pc;

			/* one arg, signum */
			IDIO vs = idio_frame_allocate (2);
			idio_frame_update (vs, 0, 0, idio_fixnum (signum));

			IDIO_THREAD_VAL (thr) = vs;
			idio_vm_invoke (thr, idio_vm_signal_handler, IDIO_VM_INVOKE_REGULAR_CALL);

			if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
			    siglongjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_SIGLONGJMP_EVENT);
			} else {
			    fprintf (stderr, "iv_r WARNING: SIGCHLD: unable to use jmp_buf==NULL in thr %10p\n", thr);
			    idio_vm_debug (thr, "SIGCHLD unable to use jmp_buf==NULL", 0);
			    idio_vm_panic (thr, "SIGCHLD unable to use jmp_buf==NULL");
			}
		    } else {
			idio_debug ("iv_r signal_handler_name=%s\n", signal_handler_name);
			idio_debug ("iv_r idio_vm_signal_handler_name=%s\n", idio_vm_signal_handler_name);
			idio_debug ("iv_r idio_vm_signal_handler_name[17]=%s\n", idio_array_ref (idio_vm_signal_handler_name, idio_fixnum (SIGCHLD)));
			fprintf (stderr, "iv_r no sighandler for signal #%d\n", signum);
		    }
		} else {
		    /* fprintf (stderr, "VM: no sighandler for signal #%d\n", signum); */
		}

		if ((idio_vm_run_loops++ & 0xff) == 0) {
		    idio_gc_possibly_collect ();
		}
	    }
	} else {
	    break;
	}
    }

    IDIO_THREAD_JMP_BUF (thr) = osjb;

    struct timeval tr;
    gettimeofday (&tr, NULL);

    time_t s = tr.tv_sec - t0.tv_sec;
    suseconds_t us = tr.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

    /*
     * If we've taken long enough and done enough then record OPs/ms
     *
     * test.idio	=> ~1750/ms (~40/ms under valgrind)
     * counter.idio	=> ~6000/ms (~10-15k/ms in a lean build)
     */
    uintptr_t loops = (idio_vm_run_loops - loops0);
    if (loops > 500000 &&
	(s ||
	 us > 100000)) {
	uintptr_t ipms = loops / (s * 1000 + us / 1000);
	FILE *fh = stderr;
#ifdef IDIO_VM_PERF
	fh = idio_vm_perf_FILE;
#endif
	fprintf (fh, "vm_run: %" PRIdPTR " ins in time %ld.%03ld => %" PRIdPTR " i/ms\n", loops, s, (long) us / 1000, ipms);
    }

    IDIO r = IDIO_THREAD_VAL (thr);

    if (idio_vm_exit) {
	fprintf (stderr, "vm-run/exit (%d)\n", idio_exit_status);
	idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

	return idio_S_notreached;
    }

    /*
     * Check we are where we think we should be...wherever that is!
     *
     * There's an element of having fallen down the rabbit hole here
     * so we do what we can.  We shouldn't be anywhere other than one
     * beyond idio_vm_FINISH_pc having successfully run through the code
     * we were passed and we shouldn't have left the stack in a mess.
     *
     * XXX except if a handler went off from a signal handler...
     */
    int bail = 0;
    if (IDIO_THREAD_PC (thr) != (idio_vm_FINISH_pc + 1)) {
	fprintf (stderr, "vm-run: THREAD failed to run FINISH: PC %zu != %td\n", IDIO_THREAD_PC (thr), (idio_vm_FINISH_pc + 1));
	bail = 1;
    }

    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    if (ss != ss0) {
	fprintf (stderr, "vm-run: THREAD failed to consume stack: SP0 %td -> %td\n", ss0 - 1, ss - 1);
	idio_vm_decode_thread (thr);
	if (ss < ss0) {
	    fprintf (stderr, "\n\nNOTICE: current stack smaller than when we started\n");
	}
	bail = 1;
    }

    /*
     * ABORT and others will have added to idio_vm_krun with some
     * abandon but are in no position to repair the krun stack
     */
    idio_ai_t krun_p = idio_array_size (idio_vm_krun);
    idio_ai_t krun_pd = krun_p - krun_p0;
    IDIO krun = idio_S_nil;
    if (krun_pd > 1) {
	fprintf (stderr, "vm-run: krun: popping %td to #%td\n", krun_pd, krun_p0);
    }
    while (krun_p > krun_p0) {
	krun = idio_array_pop (idio_vm_krun);
	krun_p--;
    }
    if (krun_pd > 1) {
	idio_gc_collect ("vm-run: pop krun");
    }

    if (bail) {
	if (idio_isa_pair (krun)) {
	    fprintf (stderr, "vm-run/bail: restoring krun #%td: ", krun_p - 1);
	    idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	    idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

	    return idio_S_notreached;
	} else {
	    fprintf (stderr, "vm-run/bail: nothing to restore => exit (1)\n");
	    idio_exit_status = 1;
	    idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

	    return idio_S_notreached;
	}
    }

    return r;
}

idio_ai_t idio_vm_extend_constants (IDIO v)
{
    IDIO_ASSERT (v);

    idio_ai_t gci = idio_array_size (idio_vm_constants);
    idio_array_push (idio_vm_constants, v);
    return gci;
}

IDIO idio_vm_constants_ref (idio_ai_t gci)
{
    if (gci > idio_array_size (idio_vm_constants)) {
	IDIO_C_ASSERT (0);
    }
    return idio_array_get_index (idio_vm_constants, gci);
}

idio_ai_t idio_vm_constants_lookup (IDIO name)
{
    IDIO_ASSERT (name);

    idio_ai_t al = idio_array_size (idio_vm_constants);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	if (idio_eqp (name, idio_array_get_index (idio_vm_constants, i))) {
	    return i;
	}
    }

    return -1;
}

idio_ai_t idio_vm_constants_lookup_or_extend (IDIO name)
{
    IDIO_ASSERT (name);

    idio_ai_t gci = idio_vm_constants_lookup (name);
    if (-1 == gci) {
	gci = idio_vm_extend_constants (name);
    }

    return gci;
}

void idio_vm_dump_constants ()
{
    FILE *fp = fopen ("vm-constants", "w");
    if (NULL == fp) {
	perror ("fopen (vm-constants, w)");
	return;
    }

    idio_ai_t al = idio_array_size (idio_vm_constants);
    fprintf (fp, "idio_vm_constants: %td\n", al);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	IDIO c = idio_array_get_index (idio_vm_constants, i);
	fprintf (fp, "%6td: ", i);
	char *cs = idio_as_string (c, 40);
	fprintf (fp, "%-20s %s\n", idio_type2string (c), cs);
	free (cs);
    }

    fclose (fp);
}

idio_ai_t idio_vm_extend_values ()
{
    idio_ai_t i = idio_array_size (idio_vm_values);
    idio_array_push (idio_vm_values, idio_S_undef);
    return i;
}

IDIO idio_vm_values_ref (idio_ai_t gvi)
{
    if (gvi) {
	IDIO v = idio_array_get_index (idio_vm_values, gvi);

	if (idio_isa_struct_instance (v)) {
	    if (idio_struct_type_isa (IDIO_STRUCT_INSTANCE_TYPE (v), idio_path_type)) {
		v = idio_path_expand (v);
	    }
	}

	return v;
    } else {
	fprintf (stderr, "ivvr: gvi == 0\n");
	return idio_S_unspec;
    }
}

void idio_vm_values_set (idio_ai_t gvi, IDIO v)
{
    idio_array_insert_index (idio_vm_values, v, gvi);
}

void idio_vm_dump_values ()
{
    FILE *fp = fopen ("vm-values", "w");
    if (NULL == fp) {
	perror ("fopen (vm-values, w)");
	return;
    }

    idio_ai_t al = idio_array_size (idio_vm_values);
    fprintf (fp, "idio_vm_values: %td\n", al);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	IDIO v = idio_array_get_index (idio_vm_values, i);
	fprintf (fp, "%6td: ", i);
	char *vs = NULL;
	if (idio_src_properties == v) {
	    /*
	     * This is tens of thousands of
	     *
	     * e -> struct {file, line, e}
	     *
	     * entries.  It takes millions of calls to implement and
	     * seconds to print!
	     */
	    vs = idio_as_string (v, 0);
	} else {
	    vs = idio_as_string (v, 40);
	}
	fprintf (fp, "%-20s %s\n", idio_type2string (v), vs);
	free (vs);
    }

    fclose (fp);
}

void idio_vm_thread_state ()
{
    IDIO thr = idio_thread_current_thread ();
    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_vm_debug (thr, "vm-thread-state", 0);
    fprintf (stderr, "\n");

    IDIO frame = IDIO_THREAD_FRAME (thr);
    while (idio_S_nil != frame) {
	fprintf (stderr, "vm-thread-state: frame: %p (%p)", frame, IDIO_FRAME_NEXT (frame));
	idio_debug ("%s\n", idio_frame_args_as_list (frame));
	frame = IDIO_FRAME_NEXT (frame);
    }

    fprintf (stderr, "\n");

    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
    idio_ai_t ss = idio_array_size (stack);
    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));

    if (tsp > ss) {
	fprintf (stderr, "TRAP SP %td > size (stack) %td\n", tsp, ss);
    } else {
	while (1) {
	    fprintf (stderr, "vm-thread-state: trap: SP %3td: ", tsp);
	    IDIO handler = idio_array_get_index (stack, tsp);

	    if (idio_isa_closure (handler)) {
		IDIO name = idio_get_property (handler, idio_KW_name, IDIO_LIST1 (idio_S_nil));
		if (idio_S_nil != name) {
		    idio_debug (" %-45s", name);
		} else {
		    idio_debug (" %-45s", handler);
		}
	    } else {
		idio_debug (" %-45s", handler);
	    }

	    IDIO ct_mci = idio_array_get_index (stack, tsp - 1);

	    IDIO ct_sym = idio_vm_constants_ref ((idio_ai_t) IDIO_FIXNUM_VAL (ct_mci));
	    IDIO ct = idio_module_symbol_value_recurse (ct_sym, IDIO_THREAD_ENV (thr), idio_S_nil);

	    if (idio_isa_struct_type (ct)) {
		idio_debug (" %s\n", IDIO_STRUCT_TYPE_NAME (ct));
	    } else {
		idio_debug (" %s\n", ct);
	    }

	    idio_ai_t ntsp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, tsp - 2));
	    if (ntsp == tsp) {
		break;
	    }
	    tsp = ntsp;
	}
    }

    int header = 1;
    IDIO dhs = idio_hash_keys_to_list (idio_condition_default_handler);

    while (idio_S_nil != dhs) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	IDIO ct = IDIO_PAIR_H (dhs);
	idio_debug ("vm-thread-state: dft handlers: %-45s ", idio_hash_get (idio_condition_default_handler, ct));
	idio_debug (" %s\n", IDIO_STRUCT_TYPE_NAME (ct));

	dhs = IDIO_PAIR_T (dhs);
    }

    header = 1;
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    while (dsp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: dynamic: SP %3td ", dsp);
	idio_debug ("= %s\n", idio_array_get_index (stack, dsp - 1));
	dsp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, dsp - 2));
    }

    header = 1;
    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
    while (esp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: environ: SP %3td ", esp);
	idio_debug ("= %s\n", idio_array_get_index (stack, esp - 1));
	esp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, esp - 2));
    }

    header = 1;
    idio_ai_t krun_p = idio_array_size (idio_vm_krun) - 1;
    while (krun_p >= 0) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	IDIO krun = idio_array_get_index (idio_vm_krun, krun_p);
	fprintf (stderr, "vm-thread-state: krun: % 3td", krun_p);
	idio_debug (" %s\n", IDIO_PAIR_HT (krun));
	krun_p--;
    }

    if (NULL == idio_k_exit) {
	fprintf (stderr, "vm-thread-state: idio_k_exit NULL\n");
    } else {
	idio_debug ("vm-thread-state: idio_k_exit %s\n", idio_k_exit);
    }
}

IDIO_DEFINE_PRIMITIVE0 ("idio-thread-state", idio_thread_state, ())
{
    idio_vm_thread_state ();

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0 ("idio-find-frame", idio_find_frame, ())
{
    IDIO thr = idio_thread_current_thread ();
    IDIO frame = IDIO_THREAD_FRAME (thr);

    fprintf (stderr, "iff: %p\n", frame);
    idio_gc_find_frame_capture (frame);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("idio-find-object", idio_find_object, (IDIO o))
{
    IDIO_ASSERT (o);

    fprintf (stderr, "ifo: %p\n", o);
    idio_gc_find_frame_capture (o);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("exit", exit, (IDIO istatus))
{
    IDIO_ASSERT (istatus);

    int status = -1;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else if (idio_isa_C_int (istatus)) {
	status = IDIO_C_TYPE_INT (istatus);
    } else {
	idio_error_param_type ("fixnum|C_int istatus", istatus, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * We've been asked to exit.  Try to flush the usual buffers.
     */
    IDIO oh = idio_thread_current_output_handle ();
    idio_flush_handle (oh);
    if (idio_isa_file_handle (oh)) {
	fflush (IDIO_FILE_HANDLE_FILEP (oh));
    }

    IDIO eh = idio_thread_current_error_handle ();
    idio_flush_handle (eh);
    if (idio_isa_file_handle (eh)) {
	fflush (IDIO_FILE_HANDLE_FILEP (eh));
    }

    idio_exit_status = status;

    idio_vm_restore_exit (idio_k_exit, istatus);

    return idio_S_notreached;
}

time_t idio_vm_elapsed (void)
{
    return (time ((time_t *) NULL) - idio_vm_t0);
}

IDIO_DEFINE_PRIMITIVE0 ("SECONDS/get", SECONDS_get, (void))
{
    return idio_integer (idio_vm_elapsed ());
}

IDIO_DEFINE_PRIMITIVE2_DS ("run-in-thread", run_in_thread, (IDIO thr, IDIO func, IDIO args), "thr func [args]", "\
Run ``func [args]`` in thread ``thr``.				\n\
								\n\
:param thr: the thread						\n\
:type thr: thread						\n\
:param func: a function						\n\
:type func: function						\n\
:param args: (optional) arguments to ``func``			\n\
:type args: list						\n\
")
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (func);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (procedure, func);

    IDIO cthr = idio_thread_current_thread ();

    idio_thread_set_current_thread (thr);

    idio_ai_t pc0 = IDIO_THREAD_PC (thr);
    idio_vm_default_pc (thr);

    IDIO dosh = idio_open_output_string_handle_C ();
    idio_display_C ("run-in-thread: ", dosh);
    idio_display (func, dosh);

    idio_apply (func, args);
    IDIO r = idio_vm_run (thr, idio_get_output_string (dosh));

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }
    idio_thread_set_current_thread (cthr);

    return r;
}

IDIO idio_vm_frame_tree (IDIO args)
{
    IDIO_ASSERT (args);

    IDIO thr = idio_thread_current_thread ();

    IDIO frame = IDIO_THREAD_FRAME (thr);

    int depth = 0;

    while (idio_S_nil != frame) {
	IDIO faci = IDIO_FRAME_NAMES (frame);
	IDIO names = idio_S_nil;
	names = idio_vm_constants_ref (IDIO_FIXNUM_VAL (faci));

	idio_ai_t al = IDIO_FRAME_NARGS (frame);
	idio_ai_t i;
	for (i = 0; i < al - 1; i++) {
	    fprintf (stderr, "  %2d %td: ", depth, i);
	    if (idio_S_nil != names) {
		idio_debug ("%15s = ", IDIO_PAIR_H (names));
		names = IDIO_PAIR_T (names);
	    } else {
		fprintf (stderr, "%15s = ", "?");
	    }
	    idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));
	}
	if (idio_S_nil != IDIO_FRAME_ARGS (frame, i)) {
	    fprintf (stderr, "  %2d *: ", depth);
	    idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));
	}
	fprintf (stderr, "\n");

	depth++;
	frame = IDIO_FRAME_NEXT (frame);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%vm-frame-tree", vm_frame_tree, (IDIO args), "[args]", "\
Show the current frame tree.					\n\
								\n\
:param args: (optional)						\n\
:type args: list						\n\
")
{
    IDIO_ASSERT (args);

    return idio_vm_frame_tree (args);
}

/*
 * NB There is no point in exposing idio_vm_source_location() as a
 * primitive as wherever you call it it returns that place -- in other
 * words it is of no use in any kind of handler as it merely tells you
 * you are in the handler.
 */
IDIO idio_vm_source_location ()
{
    IDIO lsh = idio_open_output_string_handle_C ();
    IDIO cthr = idio_thread_current_thread ();
    IDIO fmci = IDIO_THREAD_EXPR (cthr);
    if (idio_isa_fixnum (fmci)) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	IDIO expr = idio_vm_constants_ref (gci);

	IDIO lo = idio_S_nil;
	if (idio_S_nil != expr) {
	    lo = idio_hash_get (idio_src_properties, expr);
	    if (idio_S_unspec == lo) {
		lo = idio_S_nil;
	    }
	}

	if (idio_S_nil == lo) {
	    idio_display_C ("<no lexobj for ", lsh);
	    idio_display (expr, lsh);
	    idio_display_C (">", lsh);
	} else {
	    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME), lsh);
	    idio_display_C (":line ", lsh);
	    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE), lsh);
	}
    } else {
	idio_display (fmci, lsh);
    }

    return idio_get_output_string (lsh);
}

void idio_vm_decode_thread (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp0 = idio_array_size (stack) - 1;
    idio_ai_t sp = sp0;
    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));

    fprintf (stderr, "vm-decode-thread: thr=%8p esp=%4td dsp=%4td tsp=%4td sp=%4td pc=%6zd\n", thr, esp, dsp, tsp, sp, IDIO_THREAD_PC (thr));

    idio_vm_decode_stack (stack);
}

void idio_vm_decode_stack (IDIO stack)
{
    IDIO_ASSERT (stack);
    IDIO_TYPE_ASSERT (array, stack);

    idio_ai_t sp0 = idio_array_size (stack) - 1;
    idio_ai_t sp = sp0;

    fprintf (stderr, "vm-decode-stack: stk=%p sp=%4td\n", stack, sp);

    for (;sp >= 0; ) {

	fprintf (stderr, "%4td\t", sp);

	IDIO sv0 = sp >= 0 ? idio_array_get_index (stack, sp - 0) : idio_S_nil;
	IDIO sv1 = sp >= 1 ? idio_array_get_index (stack, sp - 1) : idio_S_nil;
	IDIO sv2 = sp >= 2 ? idio_array_get_index (stack, sp - 2) : idio_S_nil;
	IDIO sv3 = sp >= 3 ? idio_array_get_index (stack, sp - 3) : idio_S_nil;
	IDIO sv4 = sp >= 4 ? idio_array_get_index (stack, sp - 4) : idio_S_nil;
	IDIO sv5 = sp >= 5 ? idio_array_get_index (stack, sp - 5) : idio_S_nil;

	/*
	 * Make some educated guess about what was pushed onto the
	 * stack
	 */
	if (idio_SM_push_trap == sv0 &&
		   sp >= 3 &&
		   idio_isa_procedure (sv1) &&
		   idio_isa_fixnum (sv2) &&
		   idio_isa_fixnum (sv3)) {
	    fprintf (stderr, "%-20s ", "TRAP");
	    idio_debug ("%-35s ", sv1);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), sv2);
	    idio_debug ("%-20s ", idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci)));
	    idio_ai_t tsp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next t/h @%td", tsp);
	    sp -= 4;
	} else if (idio_SM_preserve_all_state == sv0 &&
		   sp >= 5) {
	    fprintf (stderr, "%-20s ", "ALL-STATE");
	    idio_debug ("reg1 %s ", sv5);
	    idio_debug ("reg2 %s ", sv4);
	    idio_debug ("expr %s ", sv3);
	    idio_debug ("func %s ", sv2);
	    idio_debug ("val  %s ", sv1);
	    sp -= 6;
	} else if (idio_SM_preserve_state == sv0 &&
		   sp >= 5 &&
		   idio_isa_module (sv1) &&
		   (idio_S_nil == sv2 ||
		    idio_isa_frame (sv2)) &&
		   idio_isa_fixnum (sv3) &&
		   idio_isa_fixnum (sv4) &&
		   idio_isa_fixnum (sv5)) {
	    fprintf (stderr, "%-20s ", "STATE");
	    idio_debug ("esp %s ", sv5);
	    idio_debug ("dsp %s ", sv4);
	    idio_debug ("tsp %s ", sv3);
	    idio_debug ("%s ", sv2);
	    idio_debug ("%s ", sv1);
	    sp -= 6;
	} else if (idio_SM_return == sv0 &&
		   sp >= 1 &&
		   idio_isa_fixnum (sv1)) {
	    fprintf (stderr, "%-20s ", "RETURN");
	    idio_debug ("%s ", sv1);
	    int pc = IDIO_FIXNUM_VAL (sv1);
	    if (idio_vm_NCE_pc == pc) {
		fprintf (stderr, "-- NON-CONT-ERROR");
	    } else if  (idio_vm_FINISH_pc == pc) {
		fprintf (stderr, "-- FINISH");
	    } else if (idio_vm_CHR_pc == pc) {
		fprintf (stderr, "-- condition handler return (TRAP SP then STATE following?)");
	    } else if (idio_vm_AR_pc ==  pc) {
		fprintf (stderr, "-- apply return");
	    } else if (idio_vm_IHR_pc == pc) {
		fprintf (stderr, "-- interrupt handler return");
	    }
	    sp -= 2;
	} else if (idio_SM_preserve_continuation == sv0 &&
		   sp >= 1 &&
		   idio_isa_fixnum (sv1)) {
	    fprintf (stderr, "%-20s ", "CONTINUATION PC");
	    idio_debug ("%s ", sv1);
	    sp -= 2;
	} else {
	    fprintf (stderr, "a %-18s ", idio_type2string (sv0));
	    idio_debug ("%.100s", sv0);
	    sp -= 1;
	}

	fprintf (stderr, "\n");
    }
}

void idio_vm_reset_thread (IDIO thr, int verbose)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (0 && verbose) {
	fprintf (stderr, "\nvm-reset-thread\n");

	/* IDIO stack = IDIO_THREAD_STACK (thr); */
	IDIO frame = IDIO_THREAD_FRAME (thr);

	idio_vm_thread_state ();

	size_t i = 0;
	while (idio_S_nil != frame) {
	    fprintf (stderr, "call frame %4zd: ", i++);
	    idio_debug ("%s\n", frame);
	    frame = IDIO_FRAME_NEXT (frame);
	}

	idio_debug ("env: %s\n", IDIO_THREAD_ENV (thr));

	idio_debug ("MODULE:\t%s\n", IDIO_MODULE_NAME (IDIO_THREAD_MODULE (thr)));
	idio_debug ("INPUT:\t%s\n", IDIO_THREAD_INPUT_HANDLE (thr));
	idio_debug ("OUTPUT:\t%s\n", IDIO_THREAD_OUTPUT_HANDLE (thr));
    }

    /*
     * There was code to clear the stack here as where better to clear
     * down the stack than in the reset code?  But the question was,
     * clear down to what to ensure the engine kept running?  Whatever
     * value was chosen always seemed to end in tears.
     *
     * However, idio_vm_run() knows the SP for when it was started and
     * given that we're about to tell it to FINISH the current run
     * then it would make sense for it to clear down to the last known
     * good value, the value it had when it started.
     */
    IDIO_THREAD_PC (thr) = idio_vm_FINISH_pc;
}

void idio_init_vm_values ()
{
    idio_vm_constants = idio_array (8000);
    idio_gc_protect (idio_vm_constants);

    idio_vm_values = idio_array (8000);
    idio_gc_protect (idio_vm_values);

    idio_vm_krun = idio_array (4);
    idio_gc_protect (idio_vm_krun);

    /*
     * Push a dummy value onto idio_vm_values so that slot 0 is
     * unavailable.  We can then use 0 as a marker to say the value
     * needs to be dynamically referenced and the the 0 backfilled
     * with the true value.
     *
     * idio_S_undef is the value whereon the *_REF VM instructions do
     * a double-take
     */
    idio_array_push (idio_vm_values, idio_S_undef);
}

void idio_init_vm ()
{
    idio_vm_t0 = time ((time_t *) NULL);

    idio_all_code = idio_ia (500000);

    idio_codegen_code_prologue (idio_all_code);
    idio_prologue_len = IDIO_IA_USIZE (idio_all_code);

    idio_vm_signal_handler_name = idio_array (IDIO_LIBC_NSIG + 1);
    idio_gc_protect (idio_vm_signal_handler_name);
    /*
     * idio_vm_run1() will be indexing anywhere into this array when
     * it gets a signal so make sure that the "used" size is up there
     * by putting a value in index NSIG.
     */
    idio_array_insert_index (idio_vm_signal_handler_name, idio_S_nil, (idio_ai_t) IDIO_LIBC_NSIG);

    IDIO geti;
    geti = IDIO_ADD_PRIMITIVE (SECONDS_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("SECONDS"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_Idio_module_instance ());

#ifdef IDIO_VM_PERF
    for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
	idio_vm_ins_call_time[i].tv_sec = 0;
	idio_vm_ins_call_time[i].tv_nsec = 0;
    }
#endif
    idio_dasm_FILE = stderr;
}

void idio_vm_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (raise);
    IDIO_ADD_PRIMITIVE (apply);
    IDIO_ADD_PRIMITIVE (make_continuation);
    IDIO_ADD_PRIMITIVE (restore_continuation);
    IDIO_ADD_PRIMITIVE (call_cc);
    IDIO_ADD_PRIMITIVE (vm_continuations);
    IDIO_ADD_PRIMITIVE (vm_apply_continuation);
    IDIO_ADD_PRIMITIVE (vm_trace);
#ifdef IDIO_DEBUG
    IDIO_ADD_PRIMITIVE (vm_dis);
#endif
    IDIO_ADD_PRIMITIVE (dasm);
    IDIO_ADD_PRIMITIVE (idio_thread_state);
    IDIO_ADD_PRIMITIVE (idio_find_frame);
    IDIO_ADD_PRIMITIVE (idio_find_object);
    IDIO_ADD_PRIMITIVE (exit);
    IDIO_ADD_PRIMITIVE (run_in_thread);
    IDIO_ADD_PRIMITIVE (vm_frame_tree);
}

void idio_final_vm ()
{
    /*
     * Run a GC in case someone is hogging all the file descriptors,
     * say, as we want to use one, at least.
     */
    idio_gc_collect ("idio_final_vm");
    IDIO thr = idio_thread_current_thread ();

    if (getpid () == idio_pid) {
	fprintf (stderr, "final-vm:\n");

	idio_dasm_FILE = fopen ("vm-dasm", "w");
	if (idio_dasm_FILE) {
	    idio_vm_dasm (thr, 0, 0);
	    fclose (idio_dasm_FILE);
	}
	idio_vm_dump_constants ();
	idio_vm_dump_values ();

#ifdef IDIO_DEBUG
	IDIO stack = IDIO_THREAD_STACK (thr);
	idio_ai_t ss = idio_array_size (stack);
	if (ss > 12) {
	    fprintf (stderr, "VM didn't finish cleanly\n");
	    idio_vm_thread_state ();
	}
#endif

#ifdef IDIO_VM_PERF
	fprintf (idio_vm_perf_FILE, "final-vm: created %zu instruction bytes\n", IDIO_IA_USIZE (idio_all_code));
	fprintf (idio_vm_perf_FILE, "final-vm: created %td constants\n", idio_array_size (idio_vm_constants));
	fprintf (idio_vm_perf_FILE, "final-vm: created %td values\n", idio_array_size (idio_vm_values));
#endif

#ifdef IDIO_VM_PERF
	uint64_t c = 0;
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = 0;

	for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
	    c += idio_vm_ins_counters[i];
	    t.tv_sec += idio_vm_ins_call_time[i].tv_sec;
	    t.tv_nsec += idio_vm_ins_call_time[i].tv_nsec;
	    if (t.tv_nsec > IDIO_VM_NS) {
		t.tv_nsec -= IDIO_VM_NS;
		t.tv_sec += 1;
	    }
	}

	float c_pct = 0;
	float t_pct = 0;

	fprintf (idio_vm_perf_FILE, "vm-ins:  %4.4s %-40.40s %8.8s %5.5s %15.15s %5.5s %6.6s\n", "code", "instruction", "count", "cnt%", "time (sec.nsec)", "time%", "ns/call");
	for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
	    if (1 || idio_vm_ins_counters[i]) {
		const char *bc_name = idio_vm_bytecode2string (i);
		if (strcmp (bc_name, "Unknown bytecode") ||
		    idio_vm_ins_counters[i]) {
		    float count_pct = 100.0 * idio_vm_ins_counters[i] / c;
		    c_pct += count_pct;

		    /*
		     * convert to 100ths of a second
		     */
		    float t_time = t.tv_sec * 100 + t.tv_nsec / 10000000;
		    float i_time = idio_vm_ins_call_time[i].tv_sec * 100 + idio_vm_ins_call_time[i].tv_nsec / 10000000;
		    float time_pct = i_time * 100 / t_time;
		    t_pct += time_pct;

		    fprintf (idio_vm_perf_FILE, "vm-ins:  %4" PRIu8 " %-40s %8" PRIu64 " %5.1f %5ld.%09ld %5.1f",
			     i,
			     bc_name,
			     idio_vm_ins_counters[i],
			     count_pct,
			     idio_vm_ins_call_time[i].tv_sec,
			     idio_vm_ins_call_time[i].tv_nsec,
			time_pct);
		    double call_time = 0;
		    if (idio_vm_ins_counters[i]) {
			call_time = (idio_vm_ins_call_time[i].tv_sec * IDIO_VM_NS + idio_vm_ins_call_time[i].tv_nsec) / idio_vm_ins_counters[i];
		    }
		    fprintf (idio_vm_perf_FILE, " %6.f", call_time);
		    fprintf (idio_vm_perf_FILE, "\n");
		}
	    }
	}
	fprintf (idio_vm_perf_FILE, "vm-ins:  %4s %-40s %8" PRIu64 " %5.1f %5ld.%09ld %5.1f\n", "", "total", c, c_pct, t.tv_sec, t.tv_nsec, t_pct);
#endif
    }

    idio_ia_free (idio_all_code);
    idio_gc_expose (idio_vm_constants);
    idio_gc_expose (idio_vm_values);
    idio_gc_expose (idio_vm_krun);
    idio_gc_expose (idio_vm_signal_handler_name);
}
