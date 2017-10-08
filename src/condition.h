/*
 * Copyright (c) 2015, 2017 Ian Fitchet <idf(at)idio-lang.org>
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
 * condition.h
 * 
 */

#ifndef CONDITION_H
#define CONDITION_H

/*
 * Indexes into structures for direct references
 */
/* ^idio-error */
#define IDIO_SI_IDIO_ERROR_TYPE_MESSAGE		0
#define IDIO_SI_IDIO_ERROR_TYPE_LOCATION	1
#define IDIO_SI_IDIO_ERROR_TYPE_DETAIL		2
	
/* ^read-error = ^idio_error plus */
#define IDIO_SI_READ_ERROR_TYPE_LINE		3
#define IDIO_SI_READ_ERROR_TYPE_POSITION	4
	
/* ^rt-signal */
#define IDIO_SI_RT_SIGNAL_TYPE_SIGNUM		0

#define IDIO_DEFINE_CONDITION0(v,n,p) {											\
	IDIO sym = idio_symbols_C_intern (n);										\
	v = idio_struct_type (sym, p, idio_S_nil);									\
	idio_gc_protect (v);												\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);							\
	idio_ai_t gvi = idio_vm_extend_values ();									\
	idio_module_toplevel_set_symbol (sym, IDIO_LIST3 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi)));	\
	idio_module_toplevel_set_symbol_value (sym, v);									\
    }

#define IDIO_DEFINE_CONDITION0_DYNAMIC(v,n,p) {										\
	IDIO sym = idio_symbols_C_intern (n);										\
	v = idio_struct_type (sym, p, idio_S_nil);									\
	idio_gc_protect_auto (v);											\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);							\
	idio_ai_t gvi = idio_vm_extend_values ();									\
	idio_module_toplevel_set_symbol (sym, IDIO_LIST3 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi)));	\
	idio_module_toplevel_set_symbol_value (sym, v);									\
    }

#define IDIO_DEFINE_CONDITION1(v,n,p,f1) {										\
	IDIO sym = idio_symbols_C_intern (n);										\
	v = idio_struct_type (sym, p, IDIO_LIST1 (idio_symbols_C_intern (f1)));						\
	idio_gc_protect (v);												\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);							\
	idio_ai_t gvi = idio_vm_extend_values ();									\
	idio_module_toplevel_set_symbol (sym, IDIO_LIST3 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi)));	\
	idio_module_toplevel_set_symbol_value (sym, v);									\
    }

#define IDIO_DEFINE_CONDITION2(v,n,p,f1,f2) {										\
	IDIO sym = idio_symbols_C_intern (n);										\
	v = idio_struct_type (sym, p, IDIO_LIST2 (idio_symbols_C_intern (f1), idio_symbols_C_intern (f2)));		\
	idio_gc_protect (v);												\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);							\
	idio_ai_t gvi = idio_vm_extend_values ();									\
	idio_module_toplevel_set_symbol (sym, IDIO_LIST3 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi)));	\
	idio_module_toplevel_set_symbol_value (sym, v);									\
    }

#define IDIO_DEFINE_CONDITION3(v,n,p,f1,f2,f3) {										       \
	IDIO sym = idio_symbols_C_intern (n);											       \
	v = idio_struct_type (sym, p, IDIO_LIST3 (idio_symbols_C_intern (f1), idio_symbols_C_intern (f2), idio_symbols_C_intern (f3)));\
	idio_gc_protect (v);													       \
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);								       \
	idio_ai_t gvi = idio_vm_extend_values ();										       \
	idio_module_toplevel_set_symbol (sym, IDIO_LIST3 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi)));		       \
	idio_module_toplevel_set_symbol_value (sym, v);										       \
    }

extern IDIO idio_condition_condition_type;
extern IDIO idio_condition_message_type;
extern IDIO idio_condition_error_type;
extern IDIO idio_condition_io_error_type;
extern IDIO idio_condition_io_handle_error_type;
extern IDIO idio_condition_io_read_error_type;
extern IDIO idio_condition_io_write_error_type;
extern IDIO idio_condition_io_closed_error_type;
extern IDIO idio_condition_io_filename_error_type;
extern IDIO idio_condition_io_malformed_filename_error_type;
extern IDIO idio_condition_io_file_protection_error_type;
extern IDIO idio_condition_io_file_is_read_only_error_type;
extern IDIO idio_condition_io_file_already_exists_error_type;
extern IDIO idio_condition_io_no_such_file_error_type;
extern IDIO idio_condition_read_error_type;

extern IDIO idio_condition_idio_error_type;
extern IDIO idio_condition_system_error_type;

extern IDIO idio_condition_static_error_type;
extern IDIO idio_condition_st_variable_error_type;
extern IDIO idio_condition_st_variable_type_error_type;
extern IDIO idio_condition_st_function_error_type;
extern IDIO idio_condition_st_function_arity_error_type;

extern IDIO idio_condition_runtime_error_type;
extern IDIO idio_condition_rt_parameter_type_error_type;
extern IDIO idio_condition_rt_parameter_nil_error_type;
extern IDIO idio_condition_rt_variable_error_type;
extern IDIO idio_condition_rt_variable_unbound_error_type;
extern IDIO idio_condition_rt_dynamic_variable_error_type;
extern IDIO idio_condition_rt_dynamic_variable_unbound_error_type;
extern IDIO idio_condition_rt_environ_variable_error_type;
extern IDIO idio_condition_rt_environ_variable_unbound_error_type;
extern IDIO idio_condition_rt_computed_variable_error_type;
extern IDIO idio_condition_rt_computed_variable_no_accessor_error_type;
extern IDIO idio_condition_rt_function_error_type;
extern IDIO idio_condition_rt_function_arity_error_type;
extern IDIO idio_condition_rt_module_error_type;
extern IDIO idio_condition_rt_module_unbound_error_type;
extern IDIO idio_condition_rt_module_symbol_unbound_error_type;
extern IDIO idio_condition_rt_glob_error_type;
extern IDIO idio_condition_rt_command_argv_type_error_type;
extern IDIO idio_condition_rt_command_forked_error_type;
extern IDIO idio_condition_rt_command_env_type_error_type;
extern IDIO idio_condition_rt_command_exec_error_type;
extern IDIO idio_condition_rt_command_status_error_type;
extern IDIO idio_condition_rt_array_bounds_error_type;
extern IDIO idio_condition_rt_hash_key_not_found_error_type;
extern IDIO idio_condition_rt_bignum_conversion_error_type;
extern IDIO idio_condition_rt_fixnum_conversion_error_type;

extern IDIO idio_condition_rt_signal_type;

extern IDIO idio_condition_handler_default;
extern IDIO idio_condition_handler_rt_command_status;
extern IDIO idio_condition_signal_handler_SIGHUP;
extern IDIO idio_condition_signal_handler_SIGCHLD;

int idio_isa_condition_type (IDIO o);
int idio_isa_condition (IDIO o);

IDIO idio_condition_idio_error (IDIO message, IDIO location, IDIO detail);
int idio_condition_isap (IDIO c, IDIO ct);

void idio_init_condition ();
void idio_condition_add_primitives ();
void idio_final_condition ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
