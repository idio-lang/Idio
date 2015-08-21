/*
 * Copyright (c) 2015 Ian Fitchet <idf(at)idio-lang.org>
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
 * error.c
 * 
 */

#include "idio.h"

static IDIO idio_S_internal;
static IDIO idio_S_user;
static IDIO idio_S_user_code;

void idio_error_vfprintf (char *format, va_list argp)
{
    vfprintf (stderr, format, argp);
}

IDIO idio_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == vasprintf (&s, format, argp)) {
	idio_error_alloc ("asprintf");
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);
    free (s);

    return idio_get_output_string (sh);
}

void idio_error_printf (IDIO loc, char *format, ...)
{
    IDIO_ASSERT (loc);
    IDIO_C_ASSERT (format);
    IDIO_TYPE_ASSERT (string, loc);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg = idio_error_string (format, fmt_args);
    va_end (fmt_args);

    IDIO c = idio_condition_idio_error (msg, loc, idio_S_nil);
    idio_raise_condition (idio_S_false, c);
}

void idio_warning_message (char *format, ...)
{
    fprintf (stderr, "WARNING: ");

    va_list fmt_args;
    va_start (fmt_args, format);
    idio_error_vfprintf (format, fmt_args);
    va_end (fmt_args);

    switch (format[strlen(format)-1]) {
    case '\n':
	break;
    default:
	fprintf (stderr, "\n");
    }
}

void idio_strerror (char *msg, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_error_printf (loc, "%s: %s", msg, strerror (errno));
}

void idio_error_alloc (char *m)
{
    IDIO_C_ASSERT (m);

    /*
     * This wants to be a lean'n'mean error "handler" as we've
     * (probably) run out of memory.  The chances are {m} is a static
     * string and has been pushed onto the stack so no allocation
     * there.
     *
     * perror(3) ought to be able to work in this situation and in the
     * end we assert(0) and therefore abort(3).
     */
    perror (m);
    IDIO_C_ASSERT (0);
}

void idio_error_param_nil (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (name, sh);
    idio_display_C (" is nil", sh);

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_nil_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil));
    idio_raise_condition (idio_S_true, c);
    idio_error_printf (loc, "%s is nil", name);
}

void idio_error_param_type (char *etype, IDIO who, IDIO loc)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("bad parameter type: a ", sh);
    idio_display_C ((char *) idio_type2string (who), sh);
    idio_display_C (" is not a ", sh);
    idio_display_C (etype, sh);

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       loc,
					       who));
    idio_raise_condition (idio_S_true, c);
}

/*
 * Used by IDIO_TYPE_ASSERT
 */
void idio_error_param_type_C (char *etype, IDIO who, char *file, const char *func, int line)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);

    char loc[BUFSIZ];
    sprintf (loc, "%s:%s:%d", func, file, line);

    idio_error_param_type (etype, who, idio_string_C (loc));
}

void idio_error (IDIO who, IDIO msg, IDIO args, IDIO loc)
{
    IDIO_ASSERT (who);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args); 
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (list, args);

    if (! (idio_isa_string (loc) ||
	   idio_isa_symbol (loc))) {
	idio_error_param_type ("string|symbol", loc, IDIO_C_LOCATION ("idio_error"));
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (msg, sh);
    idio_display_C (" ", sh);
    idio_display (args, sh);

    IDIO c = idio_condition_idio_error (idio_get_output_string (sh),
					loc,
					who);
    idio_raise_condition (idio_S_false, c);
}

void idio_error_C (char *msg, IDIO args, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args); 
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, loc);

    idio_error (idio_S_internal, idio_string_C (msg), args, loc);
}

IDIO_DEFINE_PRIMITIVE1V ("error", error, (IDIO msg, IDIO args))
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args); 
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO who = idio_S_user;
    
    if (idio_isa_symbol (msg)) {
	who = msg;
	msg = idio_list_head (args);
	args = idio_list_tail (args);
    }

    idio_error (idio_S_user_code, msg, args, who);

    /* not reached */
    return idio_S_unspec;
}

void idio_error_system (char *msg, IDIO args, int err, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);
    if (idio_S_nil != args) {
	idio_display_C (": ", msh);
	idio_display (args, msh);
    }

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (err), dsh);

    IDIO c = idio_struct_instance (idio_condition_system_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       idio_fixnum ((intptr_t) err)));
    idio_raise_condition (idio_S_true, c);
}

void idio_error_system_errno (char *msg, IDIO args, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (list, args);    
    IDIO_TYPE_ASSERT (string, loc);

    idio_error_system (msg, args, errno, loc);
}

void idio_init_error ()
{
    idio_S_internal = idio_symbols_C_intern ("internal");
    idio_gc_protect (idio_S_internal);
    idio_S_user = idio_symbols_C_intern ("user");
    idio_gc_protect (idio_S_user);
    idio_S_user_code = idio_string_C ("user code");
    idio_gc_protect (idio_S_user_code);
}

void idio_error_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (error);
}

void idio_final_error ()
{
    idio_gc_expose (idio_S_internal);
    idio_gc_expose (idio_S_user);
    idio_gc_expose (idio_S_user_code);
}

