/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * condition.c
 *
 */

/**
 * DOC: Idio conditions
 *
 * A thin shim around structs presenting an interpretation of Scheme's
 * SRFI 35/36
 */

#include "idio.h"

/*
 * We use these *_condition_type_mci in idio_vm_init_thread() to
 * bootstrap the base trap handlers
 */
IDIO idio_condition_condition_type_mci;

/* SRFI-36 */
IDIO idio_condition_condition_type;
IDIO idio_condition_message_type;
IDIO idio_condition_error_type;
IDIO idio_condition_io_error_type;
IDIO idio_condition_io_handle_error_type;
IDIO idio_condition_io_read_error_type;
IDIO idio_condition_io_write_error_type;
IDIO idio_condition_io_closed_error_type;
IDIO idio_condition_io_filename_error_type;
IDIO idio_condition_io_malformed_filename_error_type;
IDIO idio_condition_io_file_protection_error_type;
IDIO idio_condition_io_file_is_read_only_error_type;
IDIO idio_condition_io_file_already_exists_error_type;
IDIO idio_condition_io_no_such_file_error_type;
IDIO idio_condition_read_error_type;
IDIO idio_condition_evaluation_error_type;

/* Idio */
IDIO idio_condition_idio_error_type;
IDIO idio_condition_system_error_type;

IDIO idio_condition_static_error_type;
IDIO idio_condition_st_variable_error_type;
IDIO idio_condition_st_variable_type_error_type;
IDIO idio_condition_st_function_error_type;
IDIO idio_condition_st_function_arity_error_type;

IDIO idio_condition_runtime_error_type;
IDIO idio_condition_rt_parameter_type_error_type;
IDIO idio_condition_rt_const_parameter_error_type;
IDIO idio_condition_rt_parameter_nil_error_type;
IDIO idio_condition_rt_variable_error_type;
IDIO idio_condition_rt_variable_unbound_error_type;
IDIO idio_condition_rt_dynamic_variable_error_type;
IDIO idio_condition_rt_dynamic_variable_unbound_error_type;
IDIO idio_condition_rt_environ_variable_error_type;
IDIO idio_condition_rt_environ_variable_unbound_error_type;
IDIO idio_condition_rt_computed_variable_error_type;
IDIO idio_condition_rt_computed_variable_no_accessor_error_type;
IDIO idio_condition_rt_function_error_type;
IDIO idio_condition_rt_function_arity_error_type;
IDIO idio_condition_rt_module_error_type;
IDIO idio_condition_rt_module_unbound_error_type;
IDIO idio_condition_rt_module_symbol_unbound_error_type;
IDIO idio_condition_rt_glob_error_type;
IDIO idio_condition_rt_array_bounds_error_type;
IDIO idio_condition_rt_hash_key_not_found_error_type;
IDIO idio_condition_rt_bignum_conversion_error_type;
IDIO idio_condition_rt_fixnum_conversion_error_type;
IDIO idio_condition_rt_divide_by_zero_error_type;

IDIO idio_condition_rt_command_argv_type_error_type;
IDIO idio_condition_rt_command_forked_error_type;
IDIO idio_condition_rt_command_env_type_error_type;
IDIO idio_condition_rt_command_exec_error_type;
IDIO idio_condition_rt_command_status_error_type;

IDIO idio_condition_rt_signal_type;

IDIO idio_condition_reset_condition_handler;
IDIO idio_condition_restart_condition_handler;
IDIO idio_condition_default_condition_handler;
IDIO idio_condition_handler_rt_command_status;
IDIO idio_condition_SIGHUP_signal_handler;
IDIO idio_condition_SIGCHLD_signal_handler;

IDIO idio_condition_default_handler;

IDIO_DEFINE_PRIMITIVE2V_DS ("make-condition-type", make_condition_type, (IDIO name, IDIO parent, IDIO fields), "name parent fields", "\
make a new condition type			\n\
						\n\
:param name: condition type name		\n\
:param parent: parent condition type		\n\
:param fields: condition type fields		\n\
						\n\
:return: new condition type			\n\
						\n\
make a new condition type based on existing condition `parent` with fields `fields`\n\
")
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    if (idio_S_nil != parent) {
	IDIO_VERIFY_PARAM_TYPE (condition_type, parent);
    }
    IDIO_VERIFY_PARAM_TYPE (list, fields);

    return idio_struct_type (name, parent, fields);
}

int idio_isa_condition_type (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_struct_type (o) &&
	idio_struct_type_isa (o, idio_condition_condition_type)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("condition-type?", condition_typep, (IDIO o), "o", "\
test if `o` is a condition type			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a condition type #f otherwise\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_condition_type (o)) {
	r = idio_S_true;
    }

    return r;
}

/* message-condition-type? */
IDIO_DEFINE_PRIMITIVE1_DS ("message-condition?", message_conditionp, (IDIO o), "o", "\
test if `o` is a message condition type		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a message condition type #f otherwise\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_struct_instance (o) &&
	idio_struct_instance_isa (o, idio_condition_message_type)) {
	r = idio_S_true;
    }

    return r;
}

/* error-condition-type? */
IDIO_DEFINE_PRIMITIVE1_DS ("error?", errorp, (IDIO o), "o", "\
test if `o` is an error condition type		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an error condition type #f otherwise\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_struct_instance (o) &&
	idio_struct_instance_isa (o, idio_condition_error_type)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("allocate-condition", allocate_condition, (IDIO ct), "ct", "\
allocate a condition of condition type `ct`	\n\
						\n\
:param ct: condition type to allocate		\n\
						\n\
:return: allocated condition			\n\
						\n\
The allocated condition will have fields set to #n\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    return idio_allocate_struct_instance (ct, 1);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("make-condition", make_condition, (IDIO ct, IDIO values), "ct values", "\
initialize a condition of condition type `ct` with values `values`\n\
						\n\
:param ct: condition type to allocate		\n\
:param values: initial values for condition fields\n\
						\n\
:return: allocated condition			\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (values);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);
    IDIO_VERIFY_PARAM_TYPE (list, values);

    return idio_struct_instance (ct, values);
}

IDIO idio_condition_idio_error (IDIO message, IDIO location, IDIO detail)
{
    IDIO_ASSERT (message);
    IDIO_ASSERT (location);
    IDIO_ASSERT (detail);
    IDIO_TYPE_ASSERT (string, message);

    if (! (idio_isa_string (location) ||
	   idio_isa_symbol (location))) {
	idio_error_param_type ("string|symbol", location, IDIO_C_FUNC_LOCATION ());
    }

    return idio_struct_instance (idio_condition_idio_error_type, IDIO_LIST3 (message, location, detail));
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%idio-error-condition", idio_error_condition, (IDIO message, IDIO args), "message args", "\
create an ^idio-error condition values `message` and any `args`\n\
						\n\
:param message: ^idio-error message		\n\
:param args: ^idio-error localtion and details	\n\
						\n\
:return: allocated condition			\n\
")
{
    IDIO_ASSERT (message);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (string, message);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_struct_instance (idio_condition_idio_error_type, idio_list_append2 (IDIO_LIST1 (message), args));
}

int idio_isa_condition (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_struct_instance (o) &&
	idio_struct_instance_isa (o, idio_condition_condition_type)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("condition?", conditionp, (IDIO o), "o", "\
test if `o` is a condition			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a condition #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_condition (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_condition_isap (IDIO c, IDIO ct)
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (ct);
    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    if (idio_struct_instance_isa (c, ct)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE2 ("condition-isa?", condition_isap, (IDIO c, IDIO ct))
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (ct);
    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    IDIO r = idio_S_false;

    if (idio_condition_isap (c, ct)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("condition-ref", condition_ref, (IDIO c, IDIO field), "c field", "\
return field `field` of condition `c`		\n\
						\n\
:param c: condition				\n\
:param field: field to return			\n\
						\n\
:return: field `field` of `c`			\n\
")
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (field);
    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    return idio_struct_instance_ref (c, field);
}

/* condition-ref <condition-message> message */
IDIO_DEFINE_PRIMITIVE1_DS ("condition-message", condition_message, (IDIO c), "c", "\
return field `message` of condition `c`		\n\
						\n\
:param c: condition				\n\
						\n\
:return: field `message` of `c`			\n\
						\n\
`c` must be a condition-message type		\n\
")
{
    IDIO_ASSERT (c);
    IDIO_VERIFY_PARAM_TYPE (condition, c);

    if (! idio_struct_instance_isa (c, idio_condition_message_type)) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "not a message condition", c);
	return idio_S_unspec;
    }

    return idio_struct_instance_ref_direct (c, 0);
}

IDIO_DEFINE_PRIMITIVE3_DS ("condition-set!", condition_set, (IDIO c, IDIO field, IDIO value), "c field value", "\
set field `field` of condition `c` to value `value`\n\
						\n\
:param c: condition				\n\
:param field: field to set			\n\
:param value: value to set			\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (field);
    IDIO_ASSERT (value);
    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    return idio_struct_instance_set (c, field, value);
}

void idio_condition_set_default_handler (IDIO ct, IDIO handler)
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (handler);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    idio_hash_put (idio_condition_default_handler, ct, handler);
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-default-handler!", set_default_handler, (IDIO ct, IDIO handler), "ct handler", "\
set the default handler for condition type ``ct`` to	\n\
``handler``						\n\
							\n\
If a condition of type ``ct`` is not otherwise handled	\n\
then ``handler`` will be invoked with a continuable flag\n\
and the continuation.					\n\
							\n\
:param ct: condition type				\n\
:type ct: condition type				\n\
:param handler: handler for the condition type		\n\
:type handler: procedure				\n\
							\n\
:return: #<unspec>					\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (handler);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    idio_condition_set_default_handler (ct, handler);

    return idio_S_unspec;
}

void idio_condition_clear_default_handler (IDIO ct)
{
    IDIO_ASSERT (ct);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    idio_hash_delete (idio_condition_default_handler, ct);
}

IDIO_DEFINE_PRIMITIVE1_DS ("clear-default-handler!", clear_default_handler, (IDIO ct), "ct", "\
unset the default handler for condition type ``ct``	\n\
							\n\
The default behaviour for conditions of type ``ct`` will\n\
resume.							\n\
							\n\
:param ct: condition type				\n\
:type ct: condition type				\n\
							\n\
:return: #<unspec>					\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    idio_condition_clear_default_handler (ct);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("default-condition-handler", default_condition_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);

    /*
     * XXX IDIO_TYPE_ASSERT() will raise a condition if it fails!
     */
    IDIO_TYPE_ASSERT (boolean, cont);
    IDIO_TYPE_ASSERT (condition, cond);

    if (idio_S_false == cont) {
	/*
	 * This should go to the restart-condition-handler (which will
	 * go to the reset-condition-handler) which will reset the VM
	 */
	idio_debug ("default-condition-handler: non-cont-err %s\n", cond);

	idio_raise_condition (cont, cond);

	return idio_S_notreached;
    }

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
    IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

    if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
	IDIO signum_I = idio_array_get_index (sif, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
	int signum_C = IDIO_C_TYPE_INT (signum_I);

	switch (signum_C) {
	case SIGCHLD:
	    fprintf (stderr, "default-c-h: SIGCHLD -> idio_command_SIGCHLD_signal_handler\n");
	    idio_command_SIGCHLD_signal_handler ();
	    return idio_S_unspec;
	case SIGHUP:
	    fprintf (stderr, "default-c-h: SIGHUP -> idio_command_SIGHUP_signal_handler\n");
	    idio_command_SIGHUP_signal_handler ();
	    return idio_S_unspec;
	default:
	    break;
	}
    } else if (idio_struct_type_isa (sit, idio_condition_rt_command_status_error_type)) {
	fprintf (stderr, "default-c-h: SIG?? -> idio_command_SIG??_signal_handler\n");
	return idio_S_unspec;
    }

    IDIO handler = idio_hash_get (idio_condition_default_handler, sit);

    if (idio_S_unspec != handler) {
	return idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST3 (handler, cont, cond));
    }

    if (idio_command_interactive) {
	IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
	IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

	if (idio_struct_type_isa (sit, idio_condition_system_error_type)) {
	    IDIO eh = idio_thread_current_error_handle ();
	    int printed = 0;

	    idio_display_C ("\ndefault-condition-handler: ", eh);

	    IDIO l = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_LOCATION);
	    if (idio_S_nil != l) {
		idio_display (l, eh);
		printed = 1;

		if (idio_struct_type_isa (sit, idio_condition_read_error_type)) {
		    idio_display_C (":", eh);
		    idio_display (idio_array_get_index (sif, IDIO_SI_READ_ERROR_TYPE_LINE), eh);
		    idio_display_C (":", eh);
		    idio_display (idio_array_get_index (sif, IDIO_SI_READ_ERROR_TYPE_POSITION), eh);
		} else if (idio_struct_type_isa (sit, idio_condition_evaluation_error_type)) {
		    idio_display_C (":", eh);
		    idio_display (idio_array_get_index (sif, IDIO_SI_EVALUATION_ERROR_TYPE_EXPR), eh);
		}
	    }

	    if (printed) {
		idio_display_C (": ", eh);
	    }
	    idio_display (IDIO_STRUCT_TYPE_NAME (sit), eh);
	    idio_display_C (": ", eh);

	    IDIO m = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_MESSAGE);
	    if (idio_S_nil != m) {
		if (printed) {
		    idio_display_C (": ", eh);
		}
		idio_display (m, eh);
		printed = 1;
	    }
	    IDIO e = idio_array_get_index (sif, IDIO_SI_SYSTEM_ERROR_TYPE_ERRNO);
	    if (idio_S_nil != e) {
		idio_display_C (" => ", eh);
		int er = IDIO_C_TYPE_INT (e);
		idio_display_C (idio_libc_errno_name (er), eh);
		printed = 1;
	    }
	    IDIO d = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_DETAIL);
	    if (idio_S_nil != d) {
		if (printed) {
		    idio_display_C (": ", eh);
		}
		idio_display (d, eh);
		printed = 1;
	    }
	    idio_display_C ("\n", eh);
	} else if (idio_struct_type_isa (sit, idio_condition_idio_error_type)) {
	    IDIO eh = idio_thread_current_error_handle ();
	    int printed = 0;

	    idio_display_C ("\ndefault-condition-handler: ", eh);

	    IDIO l = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_LOCATION);
	    if (idio_S_nil != l) {
		idio_display (l, eh);
		printed = 1;

		if (idio_struct_type_isa (sit, idio_condition_read_error_type)) {
		    idio_display_C (":", eh);
		    idio_display (idio_array_get_index (sif, IDIO_SI_READ_ERROR_TYPE_LINE), eh);
		    idio_display_C (":", eh);
		    idio_display (idio_array_get_index (sif, IDIO_SI_READ_ERROR_TYPE_POSITION), eh);
		} else if (idio_struct_type_isa (sit, idio_condition_evaluation_error_type)) {
		    idio_display_C (":", eh);
		    idio_display (idio_array_get_index (sif, IDIO_SI_EVALUATION_ERROR_TYPE_EXPR), eh);
		}
	    }

	    if (printed) {
		idio_display_C (": ", eh);
	    }
	    idio_display (IDIO_STRUCT_TYPE_NAME (sit), eh);
	    idio_display_C (": ", eh);

	    if (idio_struct_type_isa (sit, idio_condition_rt_variable_error_type)) {
		IDIO name = idio_array_get_index (sif, IDIO_SI_RT_VARIABLE_ERROR_TYPE_NAME);
		if (idio_S_nil != name) {
		    if (printed) {
			idio_display_C (": ", eh);
		    }
		    idio_display (name, eh);
		    idio_display_C (": ", eh);
		    printed = 1;
		}
	    }

	    IDIO m = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_MESSAGE);
	    if (idio_S_nil != m) {
		idio_display (m, eh);
		printed = 1;
	    }
	    IDIO d = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_DETAIL);
	    if (idio_S_nil != d) {
		if (printed) {
		    idio_display_C (": ", eh);
		}
		idio_display (d, eh);
		printed = 1;
	    }
	    idio_display_C ("\n", eh);
	} else if (idio_struct_type_isa (sit, idio_condition_error_type)) {
	    IDIO eh = idio_thread_current_error_handle ();

	    idio_display_C ("\ndefault-condition-handler: ", eh);
	    idio_display (IDIO_STRUCT_TYPE_NAME (sit), eh);
	    idio_display_C ("\n", eh);
	} else {
	    idio_debug ("default-condition-handler: no clause for %s\n", cond);
	}

	/*
	 * Save a continuation in case things get ropey and we have to
	 * bail out.
	 */
	IDIO pdosh = idio_open_output_string_handle_C ();
	idio_display_C ("debugger: ", pdosh);

	IDIO thr = idio_thread_current_thread ();

	IDIO debug_k = idio_continuation (thr);
	idio_array_push (idio_vm_krun, IDIO_LIST2 (debug_k, idio_get_output_string (pdosh)));

	IDIO cmd = IDIO_LIST2 (idio_module_symbol_value (idio_symbols_C_intern ("debug"),
							 idio_Idio_module,
							 idio_S_nil),
			       debug_k);

	return idio_vm_invoke_C (thr, cmd);
    }

#ifdef IDIO_DEBUG
    idio_debug ("default-condition-handler: no handler re-raising %s\n", cond);
#endif
    idio_raise_condition (cont, cond);

    idio_debug ("default-condition-handler: returning %s\n", idio_S_void);

    /*
     * For a continuable continuation, if it gets here, we'll
     * return void because...
     */
    return idio_S_void;
}

IDIO_DEFINE_PRIMITIVE2 ("restart-condition-handler", restart_condition_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);

    if (idio_S_true == cont) {
	if (idio_isa_condition (cond)) {
	    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
	    /* IDIO stf = IDIO_STRUCT_TYPE_FIELDS (sit); */
	    IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

	    /*
	     * Hmm, a timing issue with SIGCHLD?  Should have been caught
	     * in default-condition-handler.
	     */
	    if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
		IDIO signum_I = idio_array_get_index (sif, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
		int signum_C = IDIO_C_TYPE_INT (signum_I);

		switch (signum_C) {
		case SIGCHLD:
		    fprintf (stderr, "restart-c-h: SIGCHLD -> idio_command_SIGCHLD_signal_handler\n");
		    idio_command_SIGCHLD_signal_handler (signum_I);
		    return idio_S_unspec;
		case SIGHUP:
		    fprintf (stderr, "restart-c-h: SIGHUP -> idio_command_SIGHUP_signal_handler\n");
		    idio_command_SIGHUP_signal_handler (signum_I);
		    return idio_S_unspec;
		default:
		    break;
		}
	    } else if (idio_struct_type_isa (sit, idio_condition_idio_error_type)) {
		IDIO eh = idio_thread_current_error_handle ();
		int printed = 0;

		idio_display_C ("\nrestart-condition-handler: ", eh);

		IDIO l = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_LOCATION);
		if (idio_S_nil != l) {
		    if (printed) {
			idio_display_C (": ", eh);
		    }
		    idio_display (l, eh);
		    printed = 1;

		    if (idio_struct_type_isa (sit, idio_condition_read_error_type)) {
			idio_display_C (":", eh);
			idio_display (idio_array_get_index (sif, IDIO_SI_READ_ERROR_TYPE_LINE), eh);
			idio_display_C (":", eh);
			idio_display (idio_array_get_index (sif, IDIO_SI_READ_ERROR_TYPE_POSITION), eh);
		    } else if (idio_struct_type_isa (sit, idio_condition_evaluation_error_type)) {
			idio_display_C (":", eh);
			idio_display (idio_array_get_index (sif, IDIO_SI_EVALUATION_ERROR_TYPE_EXPR), eh);
		    }
		}

		if (printed) {
		    idio_display_C (": ", eh);
		}
		idio_display (IDIO_STRUCT_TYPE_NAME (sit), eh);
		idio_display_C (": ", eh);

		IDIO m = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_MESSAGE);
		if (idio_S_nil != m) {
		    idio_display (m, eh);
		    printed = 1;
		}
		IDIO d = idio_array_get_index (sif, IDIO_SI_IDIO_ERROR_TYPE_DETAIL);
		if (idio_S_nil != d) {
		    if (printed) {
			idio_display_C (": ", eh);
		    }
		    idio_display (d, eh);
		}
		idio_display_C ("\n", eh);
	    } else {
		idio_debug ("restart-condition-handler: %s\n", cond);
	    }
	} else {
	    /*
	     * Something has gone badly wrong if we've been given a
	     * non-condition as an argument.
	     *
	     * So throw up a message and revert to
	     * reset-condition-handler.
	     */
	    fprintf (stderr, "restart-condition-handler: expected a condition, not a %s\n", idio_type2string (cond));
	    idio_debug ("%s\n", cond);

	    IDIO sh = idio_open_output_string_handle_C ();
	    idio_display_C ("condition-handler-rt-command-status: expected a condition not a '", sh);
	    idio_display (cond, sh);
	    idio_display_C ("'", sh);
	    IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
					   IDIO_LIST3 (idio_get_output_string (sh),
						       IDIO_C_FUNC_LOCATION (),
						       idio_S_nil));

	    idio_raise_condition (idio_S_true, c);
	}
    }

    /*
     * The topmost krun on the VM's stack is the one that just blew up.
     *
     * As the restart-condition-handler we'll go back to the next most
     * recent krun on the VM's stack.
     */
    idio_ai_t krun_p = idio_array_size (idio_vm_krun);
    IDIO krun = idio_S_nil;
    while (krun_p > 0) {
	krun = idio_array_pop (idio_vm_krun);
	idio_debug ("restart-condition-handler: krun: popping %s\n", IDIO_PAIR_HT (krun));
	krun_p--;
    }

    if (idio_isa_pair (krun)) {
	fprintf (stderr, "restart-condition-handler: restoring krun #%td: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

	return idio_S_notreached;
    }

    fprintf (stderr, "restart-condition-handler: nothing to restore\n");
    idio_raise_condition (cont, cond);

    /* notreached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2 ("reset-condition-handler", reset_condition_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);

    IDIO eh = idio_thread_current_error_handle ();
    idio_display_C ("\nreset-condition-handler: ", eh);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
    idio_display (IDIO_STRUCT_TYPE_NAME (sit), eh);
    idio_display_C (": ", eh);
    idio_display (cont, eh);
    idio_display_C (" ", eh);
    idio_display (cond, eh);
    idio_display_C ("\n", eh);

    /*
     * As the reset-condition-handler we'll go back to the first krun
     * on the VM's stack.
     */
    idio_ai_t krun_p = idio_array_size (idio_vm_krun);
    IDIO krun = idio_S_nil;
    while (krun_p > 0) {
	krun = idio_array_pop (idio_vm_krun);
	krun_p--;
    }

    if (idio_isa_pair (krun)) {
	fprintf (stderr, "reset-condition-handler: restoring krun #%td: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

	return idio_S_notreached;
    }

    fprintf (stderr, "reset-condition-handler: nothing to restore\n");
    IDIO thr = idio_thread_current_thread ();

    idio_vm_reset_thread (thr, 1);

    /*
     * For a continuable continuation, if it gets here, we'll
     * return void because...
     */
    return idio_S_void;
}

void idio_init_condition ()
{

#define IDIO_CONDITION_CONDITION_TYPE_NAME "^condition"

    /* SRFI-35-ish */
    IDIO_DEFINE_CONDITION0 (idio_condition_condition_type, IDIO_CONDITION_CONDITION_TYPE_NAME, idio_S_nil);
    IDIO_DEFINE_CONDITION1 (idio_condition_message_type, "^message", idio_condition_condition_type, "message");
    IDIO_DEFINE_CONDITION0 (idio_condition_error_type, "^error", idio_condition_condition_type);

    /*
     * We want the fmci of ^condition for the *-condition-handler(s)
     * which means we have to repeat a couple of the actions of the
     * IDIO_DEFINE_CONDITION0 macro.
     */
    IDIO sym = idio_symbols_C_intern (IDIO_CONDITION_CONDITION_TYPE_NAME);
    idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);
    idio_condition_condition_type_mci = idio_fixnum (gci);

    /* Idio */
    IDIO_DEFINE_CONDITION3 (idio_condition_idio_error_type, "^idio-error", idio_condition_error_type, "message", "location", "detail");

    /* SRFI-36-ish */
    IDIO_DEFINE_CONDITION0 (idio_condition_io_error_type, "^i/o-error", idio_condition_idio_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_io_handle_error_type, "^i/o-handle-error", idio_condition_io_error_type, "handle");
    IDIO_DEFINE_CONDITION0 (idio_condition_io_read_error_type, "^i/o-read-error", idio_condition_io_handle_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_write_error_type, "^i/o-write-error", idio_condition_io_handle_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_closed_error_type, "^i/o-closed-error", idio_condition_io_handle_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_io_filename_error_type, "^i/o-filename-error", idio_condition_io_error_type, "filename");
    IDIO_DEFINE_CONDITION0 (idio_condition_io_malformed_filename_error_type, "^i/o-malformed-filename-error", idio_condition_io_filename_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_protection_error_type, "^i/o-file-protection-error", idio_condition_io_filename_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_is_read_only_error_type, "^i/o-file-is-read-only-error", idio_condition_io_file_protection_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_already_exists_error_type, "^i/o-file-already-exists-error", idio_condition_io_filename_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_no_such_file_error_type, "^i/o-no-such-file-error", idio_condition_io_filename_error_type);

    /* NB. no column or span! */
    IDIO_DEFINE_CONDITION2 (idio_condition_read_error_type, "^read-error", idio_condition_idio_error_type, "line", "position");
    IDIO_DEFINE_CONDITION1 (idio_condition_evaluation_error_type, "^evaluation-error", idio_condition_idio_error_type, "expr");

    /* Idio */
    IDIO_DEFINE_CONDITION1 (idio_condition_system_error_type, "^system-error", idio_condition_idio_error_type, "errno");

    IDIO_DEFINE_CONDITION0 (idio_condition_static_error_type, "^static-error", idio_condition_idio_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_st_variable_error_type, "^st-variable-error", idio_condition_static_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_st_variable_type_error_type, "^st-variable-type-error", idio_condition_st_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_st_function_error_type, "^st-function-error", idio_condition_static_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_st_function_arity_error_type, "^st-function-arity-error", idio_condition_st_function_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_runtime_error_type, "^runtime-error", idio_condition_idio_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_parameter_type_error_type, "^rt-parameter-type-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_const_parameter_error_type, "^rt-const-parameter-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_parameter_nil_error_type, "^rt-parameter-nil-error", idio_condition_runtime_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_variable_error_type, "^rt-variable-error", idio_condition_runtime_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_variable_unbound_error_type, "^rt-variable-unbound-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_dynamic_variable_error_type, "^rt-dynamic-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_dynamic_variable_unbound_error_type, "^rt-dynamic-variable-unbound-error", idio_condition_rt_dynamic_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_environ_variable_error_type, "^rt-environ-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_environ_variable_unbound_error_type, "^rt-environ-variable-unbound-error", idio_condition_rt_environ_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_computed_variable_error_type, "^rt-computed-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_computed_variable_no_accessor_error_type, "^rt-computed-variable-no-accessor-error", idio_condition_rt_computed_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_function_error_type, "^rt-function-error", idio_condition_static_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_function_arity_error_type, "^rt-function-arity-error", idio_condition_rt_function_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_module_error_type, "^rt-module-error", idio_condition_runtime_error_type, "module");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_module_unbound_error_type, "^rt-module-unbound-error", idio_condition_rt_module_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_module_symbol_unbound_error_type, "^rt-module-symbol-unbound-error", idio_condition_rt_module_error_type, "symbol");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_glob_error_type, "^rt-glob-error", idio_condition_runtime_error_type, "pattern");

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_argv_type_error_type, "^rt-command-argv-type-error", idio_condition_runtime_error_type, "arg");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_command_forked_error_type, "^rt-command-forked-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_env_type_error_type, "^rt-command-env-type-error", idio_condition_rt_command_forked_error_type, "name");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_exec_error_type, "^rt-command-exec-error", idio_condition_rt_command_forked_error_type, "errno");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_status_error_type, "^rt-command-status-error", idio_condition_runtime_error_type, "status");

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_array_bounds_error_type, "^rt-array-bounds-error", idio_condition_runtime_error_type, "index");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_hash_key_not_found_error_type, "^rt-hash-key-not-found", idio_condition_runtime_error_type, "key");

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_bignum_conversion_error_type, "^rt-bignum-conversion-error", idio_condition_runtime_error_type, "bignum");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_fixnum_conversion_error_type, "^rt-fixnum-conversion-error", idio_condition_runtime_error_type, "fixnum");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_divide_by_zero_error_type, "^rt-divide-by-zero-error", idio_condition_runtime_error_type);

#define IDIO_CONDITION_RT_SIGNAL_TYPE_NAME "^rt-signal"

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_signal_type, IDIO_CONDITION_RT_SIGNAL_TYPE_NAME, idio_condition_error_type, "signum");
}

void idio_condition_add_primitives ()
{
    idio_condition_default_handler = IDIO_HASH_EQP (8);
    idio_gc_protect (idio_condition_default_handler);

    IDIO_ADD_PRIMITIVE (make_condition_type);
    IDIO_ADD_PRIMITIVE (condition_typep);
    IDIO_ADD_PRIMITIVE (message_conditionp);
    IDIO_ADD_PRIMITIVE (errorp);

    IDIO_ADD_PRIMITIVE (allocate_condition);
    IDIO_ADD_PRIMITIVE (make_condition);
    IDIO_ADD_PRIMITIVE (idio_error_condition);
    IDIO_ADD_PRIMITIVE (conditionp);
    IDIO_ADD_PRIMITIVE (condition_isap);
    IDIO_ADD_PRIMITIVE (condition_ref);
    IDIO_ADD_PRIMITIVE (condition_message);
    IDIO_ADD_PRIMITIVE (condition_set);

    IDIO_ADD_PRIMITIVE (set_default_handler);
    IDIO_ADD_PRIMITIVE (clear_default_handler);

    IDIO fvi;
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, reset_condition_handler);
    idio_condition_reset_condition_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, restart_condition_handler);
    idio_condition_restart_condition_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, default_condition_handler);
    idio_condition_default_condition_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
}

void idio_final_condition ()
{
    idio_gc_expose (idio_condition_condition_type);
    idio_gc_expose (idio_condition_message_type);
    idio_gc_expose (idio_condition_error_type);
    idio_gc_expose (idio_condition_idio_error_type);
    idio_gc_expose (idio_condition_io_error_type);
    idio_gc_expose (idio_condition_io_handle_error_type);
    idio_gc_expose (idio_condition_io_read_error_type);
    idio_gc_expose (idio_condition_io_write_error_type);
    idio_gc_expose (idio_condition_io_closed_error_type);
    idio_gc_expose (idio_condition_io_filename_error_type);
    idio_gc_expose (idio_condition_io_malformed_filename_error_type);
    idio_gc_expose (idio_condition_io_file_protection_error_type);
    idio_gc_expose (idio_condition_io_file_is_read_only_error_type);
    idio_gc_expose (idio_condition_io_file_already_exists_error_type);
    idio_gc_expose (idio_condition_io_no_such_file_error_type);
    idio_gc_expose (idio_condition_read_error_type);
    idio_gc_expose (idio_condition_evaluation_error_type);

    idio_gc_expose (idio_condition_system_error_type);

    idio_gc_expose (idio_condition_static_error_type);
    idio_gc_expose (idio_condition_st_variable_error_type);
    idio_gc_expose (idio_condition_st_variable_type_error_type);
    idio_gc_expose (idio_condition_st_function_error_type);
    idio_gc_expose (idio_condition_st_function_arity_error_type);

    idio_gc_expose (idio_condition_runtime_error_type);
    idio_gc_expose (idio_condition_rt_parameter_type_error_type);
    idio_gc_expose (idio_condition_rt_const_parameter_error_type);
    idio_gc_expose (idio_condition_rt_parameter_nil_error_type);
    idio_gc_expose (idio_condition_rt_variable_error_type);
    idio_gc_expose (idio_condition_rt_variable_unbound_error_type);
    idio_gc_expose (idio_condition_rt_dynamic_variable_error_type);
    idio_gc_expose (idio_condition_rt_dynamic_variable_unbound_error_type);
    idio_gc_expose (idio_condition_rt_environ_variable_error_type);
    idio_gc_expose (idio_condition_rt_environ_variable_unbound_error_type);
    idio_gc_expose (idio_condition_rt_computed_variable_error_type);
    idio_gc_expose (idio_condition_rt_computed_variable_no_accessor_error_type);
    idio_gc_expose (idio_condition_rt_function_error_type);
    idio_gc_expose (idio_condition_rt_function_arity_error_type);
    idio_gc_expose (idio_condition_rt_module_error_type);
    idio_gc_expose (idio_condition_rt_module_unbound_error_type);
    idio_gc_expose (idio_condition_rt_module_symbol_unbound_error_type);
    idio_gc_expose (idio_condition_rt_glob_error_type);
    idio_gc_expose (idio_condition_rt_command_argv_type_error_type);
    idio_gc_expose (idio_condition_rt_command_forked_error_type);
    idio_gc_expose (idio_condition_rt_command_env_type_error_type);
    idio_gc_expose (idio_condition_rt_command_exec_error_type);
    idio_gc_expose (idio_condition_rt_command_status_error_type);
    idio_gc_expose (idio_condition_rt_array_bounds_error_type);
    idio_gc_expose (idio_condition_rt_hash_key_not_found_error_type);
    idio_gc_expose (idio_condition_rt_bignum_conversion_error_type);
    idio_gc_expose (idio_condition_rt_fixnum_conversion_error_type);
    idio_gc_expose (idio_condition_rt_divide_by_zero_error_type);
    idio_gc_expose (idio_condition_rt_signal_type);

    idio_gc_expose (idio_condition_default_handler);
}

