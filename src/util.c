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
 * util.c
 * 
 */

#include "idio.h"

int idio_type (IDIO o)
{
    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	return IDIO_TYPE_FIXNUM;
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		return IDIO_TYPE_CONSTANT_IDIO;
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		return IDIO_TYPE_CONSTANT_TOKEN;
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		return IDIO_TYPE_CONSTANT_I_CODE;
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		return IDIO_TYPE_CONSTANT_CHARACTER;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_LOCATION ("idio_type/CONSTANT"), "type: unexpected object type %#x", o);

		/* notreached */
		return IDIO_TYPE_NONE;
	    }
	}
    case IDIO_TYPE_PLACEHOLDER_MARK:
	return IDIO_TYPE_PLACEHOLDER;
    case IDIO_TYPE_POINTER_MARK:
	return o->type;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_LOCATION ("idio_type"), "type: unexpected object type %#x", o);

	/* notreached */
	return IDIO_TYPE_NONE;
    }
}

const char *idio_type_enum2string (idio_type_e type)
{
    switch (type) {
    case IDIO_TYPE_NONE: return "NONE";
    case IDIO_TYPE_FIXNUM: return "FIXNUM";
    case IDIO_TYPE_CONSTANT_IDIO: return "CONSTANT_IDIO";
    case IDIO_TYPE_CONSTANT_TOKEN: return "CONSTANT_TOKEN";
    case IDIO_TYPE_CONSTANT_I_CODE: return "CONSTANT_I_CODE";
    case IDIO_TYPE_CONSTANT_CHARACTER: return "CONSTANT_CHARACTER";
    case IDIO_TYPE_PLACEHOLDER: return "PLACEHOLDER";
    case IDIO_TYPE_STRING: return "STRING";
    case IDIO_TYPE_SUBSTRING: return "SUBSTRING";
    case IDIO_TYPE_SYMBOL: return "SYMBOL";
    case IDIO_TYPE_KEYWORD: return "KEYWORD";
    case IDIO_TYPE_PAIR: return "PAIR";
    case IDIO_TYPE_ARRAY: return "ARRAY";
    case IDIO_TYPE_HASH: return "HASH";
    case IDIO_TYPE_CLOSURE: return "CLOSURE";
    case IDIO_TYPE_PRIMITIVE: return "PRIMITIVE";
    case IDIO_TYPE_BIGNUM: return "BIGNUM";
    case IDIO_TYPE_MODULE: return "MODULE";
    case IDIO_TYPE_FRAME: return "FRAME";
    case IDIO_TYPE_HANDLE: return "HANDLE";
    case IDIO_TYPE_STRUCT_TYPE: return "STRUCT_TYPE";
    case IDIO_TYPE_STRUCT_INSTANCE: return "STRUCT_INSTANCE";
    case IDIO_TYPE_THREAD: return "THREAD";
    case IDIO_TYPE_CONTINUATION: return "CONTINUATION";
	
    case IDIO_TYPE_C_INT: return "C INT";
    case IDIO_TYPE_C_UINT: return "C UINT";
    case IDIO_TYPE_C_FLOAT: return "C FLOAT";
    case IDIO_TYPE_C_DOUBLE: return "C DOUBLE";
    case IDIO_TYPE_C_POINTER: return "C POINTER";
    case IDIO_TYPE_C_VOID: return "C VOID";
	
    case IDIO_TYPE_C_TYPEDEF: return "TAG";
    case IDIO_TYPE_C_STRUCT: return "C_STRUCT";
    case IDIO_TYPE_C_INSTANCE: return "C_INSTANCE";
    case IDIO_TYPE_C_FFI: return "C_FFI";
    case IDIO_TYPE_OPAQUE: return "OPAQUE";
    default:
	IDIO_FPRINTF (stderr, "IDIO_TYPE_ENUM2STRING: unexpected type %d\n", type);
	return "NOT KNOWN";
    }
}

const char *idio_type2string (IDIO o)
{
    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	return "FIXNUM";
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK: return "CONSTANT_IDIO";
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK: return "CONSTANT_TOKEN";
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK: return "CONSTANT_I_CODE";
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK: return "CONSTANT_CHARACTER";
	    default:
		idio_error_C ("idio_type2string: unexpected type", o, IDIO_C_LOCATION ("idio_type2string/CONSTANT"));

		/* notreached */
		return "NOT KNOWN";
	    }
	}
    case IDIO_TYPE_PLACEHOLDER_MARK:
	return "PLACEHOLDER";
    case IDIO_TYPE_POINTER_MARK:
	return idio_type_enum2string (o->type);
    default:
	idio_error_C ("idio_type2string: unexpected type", o, IDIO_C_LOCATION ("idio_type2string"));

	/* notreached */
	return "NOT KNOWN";
    }
}

IDIO_DEFINE_PRIMITIVE1 ("zero?", zerop, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if ((idio_isa_fixnum (o) &&
	 0 == IDIO_FIXNUM_VAL (o)) ||
	(idio_isa_bignum (o) &&
	 idio_bignum_zero_p (o))) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_nil (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_S_nil == o);
}

IDIO_DEFINE_PRIMITIVE1 ("null?", nullp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_nil == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("unset?", unsetp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_unset == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("undef?", undefp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_undef == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("unspec?", unspecp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_unspec == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("void?", voidp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_void == o) {
	r = idio_S_true;
    }

    return r;
}

/*
 * Unrelated to undef?, %undefined? tests whether a non-local symbol
 * is defined in this environment.
 */
IDIO_DEFINE_PRIMITIVE1 ("%defined?", definedp, (IDIO s))
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    IDIO r = idio_S_false;

    IDIO sk = idio_module_env_symbol_recurse (s);
    
    if (idio_S_unspec != sk) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_boolean (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_S_true == o ||
	    idio_S_false == o);
}

IDIO_DEFINE_PRIMITIVE1 ("boolean?", booleanp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_boolean (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("not", not, (IDIO e))
{
    IDIO_ASSERT (e);

    IDIO r = idio_S_false;

    if (idio_S_false == e) {
	r = idio_S_true;
    }

    return r;
}

#define IDIO_EQUAL_EQP		1
#define IDIO_EQUAL_EQVP		2
#define IDIO_EQUAL_EQUALP	3

int idio_eqp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQP);
}

IDIO_DEFINE_PRIMITIVE2 ("eq?", eqp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("eqv?", eqvp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqvp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * s9.scm redefines equal? from eq? and eqv? and recurses on itself --
 * or it will if we do not define a primitive equal? which would be
 * used in its definition

IDIO_DEFINE_PRIMITIVE2 ("equal?", equalp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_equalp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}
*/

int idio_eqvp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQVP);
}

int idio_equalp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQUALP);
}

int idio_equal (IDIO o1, IDIO o2, int eqp)
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    if (o1 == o2) {
	return 1;
    }
    
    int m1 = (intptr_t) o1 & IDIO_TYPE_MASK;
    
    switch (m1) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	/*
	  We already tested for equality above!
	 */
	return 0;
    case IDIO_TYPE_POINTER_MARK:
	{
	    int m2 = (intptr_t) o2 & IDIO_TYPE_MASK;
    
	    switch (m2) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		/* we would have matched at the top */
		return 0;
	    default:
		break;
	    }
	    
	    if (o1->type != o2->type) {
		return 0;
	    }
    
	    if (IDIO_FLAG_FREE_SET (o1) ||
		IDIO_FLAG_FREE_SET (o2)) {
		return 0;
	    }

	    size_t i;

	    switch (o1->type) {
	    case IDIO_TYPE_STRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		    /* return (o1->u.string == o2->u.string); */
		}
		
		if (IDIO_STRING_BLEN (o1) != IDIO_STRING_BLEN (o2)) {
		    return 0;
		}
		    
		return (strncmp (IDIO_STRING_S (o1), IDIO_STRING_S (o2), IDIO_STRING_BLEN (o1)) == 0);
	    case IDIO_TYPE_SUBSTRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		    /* return (o1->u.substring == o2->u.substring); */
		}
		    
		if (IDIO_SUBSTRING_BLEN (o1) != IDIO_SUBSTRING_BLEN (o2)) {
		    return 0;
		}
		    
		return (strncmp (IDIO_SUBSTRING_S (o1), IDIO_SUBSTRING_S (o2), IDIO_SUBSTRING_BLEN (o1)) == 0);
	    case IDIO_TYPE_SYMBOL:
		return (o1 == o2);
		
		break;
	    case IDIO_TYPE_KEYWORD:
		return (o1 == o2);
		
		break;
	    case IDIO_TYPE_PAIR:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		    /* return (o1->u.pair == o2->u.pair); */
		}
		
		return (idio_equalp (IDIO_PAIR_H (o1), IDIO_PAIR_H (o2)) &&
			idio_equalp (IDIO_PAIR_T (o1), IDIO_PAIR_T (o2)));
	    case IDIO_TYPE_ARRAY:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.array == o2->u.array);
		}
		
		if (IDIO_ARRAY_USIZE (o1) != IDIO_ARRAY_USIZE (o2)) {
		    return 0;
		}

		for (i = 0; i < IDIO_ARRAY_ASIZE (o1); i++) {
		    if (! idio_equalp (IDIO_ARRAY_AE (o1, i), IDIO_ARRAY_AE (o2, i))) {
			return 0;
		    }
		}
		return 1;
	    case IDIO_TYPE_HASH:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.hash == o2->u.hash);
		}
		
		if (IDIO_HASH_SIZE (o1) != IDIO_HASH_SIZE (o2)) {
		    return 0;
		}
		
		for (i = 0; i < IDIO_HASH_SIZE (o1); i++) {
		    if (! idio_equalp (IDIO_HASH_HE_KEY (o1, i), IDIO_HASH_HE_KEY (o2, i)) ||
			! idio_equalp (IDIO_HASH_HE_VALUE (o1, i), IDIO_HASH_HE_VALUE (o2, i))) {
			return 0;
		    }
		}
		return 1;
	    case IDIO_TYPE_CLOSURE:
		return (o1 == o2);
		/* return (o1->u.closure == o2->u.closure); */
	    case IDIO_TYPE_PRIMITIVE:
		return (o1 == o2);
		/* return (o1->u.primitive == o2->u.primitive); */
	    case IDIO_TYPE_BIGNUM:
		if (IDIO_EQUAL_EQP == eqp) {
		    /*
		     * u.bignum is part of the idio_s union so check
		     * the malloc'd sig
		     */
		    return (IDIO_BIGNUM_SIG (o1) == IDIO_BIGNUM_SIG (o2));
		}
		
		return idio_bignum_real_equal_p (o1, o2);
	    case IDIO_TYPE_MODULE:
		return (o1 == o2);
	    case IDIO_TYPE_FRAME:
		return (o1 == o2);
	    case IDIO_TYPE_HANDLE:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.handle == o2->u.handle);
		}
		
		if (! idio_equalp (IDIO_HANDLE_NAME (o1), IDIO_HANDLE_NAME (o2))) {
		    return 0;
		}
		break;
	    case IDIO_TYPE_C_INT:
		return (IDIO_C_TYPE_INT (o1) == IDIO_C_TYPE_INT (o2));
	    case IDIO_TYPE_C_UINT:
		return (IDIO_C_TYPE_UINT (o1) == IDIO_C_TYPE_UINT (o2));
	    case IDIO_TYPE_C_FLOAT:
		return (IDIO_C_TYPE_FLOAT (o1) == IDIO_C_TYPE_FLOAT (o2));
	    case IDIO_TYPE_C_DOUBLE:
		return (IDIO_C_TYPE_DOUBLE (o1) == IDIO_C_TYPE_DOUBLE (o2));
	    case IDIO_TYPE_C_POINTER:
		return (IDIO_C_TYPE_POINTER_P (o1) == IDIO_C_TYPE_POINTER_P (o2));
	    case IDIO_TYPE_STRUCT_TYPE:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.struct_type == o2->u.struct_type);
		}
		
		if (! idio_equalp (IDIO_STRUCT_TYPE_NAME (o1), IDIO_STRUCT_TYPE_NAME (o2)) ||
		    ! idio_equalp (IDIO_STRUCT_TYPE_PARENT (o1), IDIO_STRUCT_TYPE_PARENT (o2)) ||
		    ! idio_equalp (IDIO_STRUCT_TYPE_FIELDS (o1), IDIO_STRUCT_TYPE_FIELDS (o2))) {
		    return 0;
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.struct_instance == o2->u.struct_instance);
		}
		
		if (! idio_equalp (IDIO_STRUCT_INSTANCE_TYPE (o1), IDIO_STRUCT_INSTANCE_TYPE (o2)) ||
		    ! idio_equalp (IDIO_STRUCT_INSTANCE_FIELDS (o1), IDIO_STRUCT_INSTANCE_FIELDS (o2))) {
		    return 0;
		}
		break;
	    case IDIO_TYPE_THREAD:
		return (o1->u.thread == o2->u.thread);
	    case IDIO_TYPE_CONTINUATION:
		return (o1->u.continuation == o2->u.continuation);
	    case IDIO_TYPE_C_TYPEDEF:
		return (o1->u.C_typedef == o2->u.C_typedef);
	    case IDIO_TYPE_C_STRUCT:
		return (o1->u.C_struct == o2->u.C_struct);
	    case IDIO_TYPE_C_INSTANCE:
		return o1->u.C_instance == o2->u.C_instance;
	    case IDIO_TYPE_C_FFI:
		return (o1->u.C_FFI == o2->u.C_FFI);
	    case IDIO_TYPE_OPAQUE:
		return (o1->u.opaque == o2->u.opaque);
	    default:
		idio_error_C ("IDIO_TYPE_POINTER_MARK: o1->type unexpected", IDIO_LIST1 (o1), IDIO_C_LOCATION ("idio_equal"));

		/* notreached */
		return 0;
	    }
	}
    default:
	idio_error_C ("o1->type unexpected", o1, IDIO_C_LOCATION ("idio_equal"));

	/* notreached */
	return 0;
    }

    return 1;
}

/*
 * reconstruct C escapes in s
 */
char *idio_escape_string (size_t blen, char *s)
{
    size_t i;
    size_t n = 0;
    for (i = 0; i < blen; i++) {
	n++;
	switch (s[i]) {
	case '\a': n++; break;
	case '\b': n++; break;
	case '\f': n++; break;
	case '\n': n++; break;
	case '\r': n++; break;
	case '\t': n++; break;
	case '\v': n++; break;
	case '"': n++; break;
	}
    }

    /* 2 for "s and 1 for \0 */
    char *r = idio_alloc (1 + n + 1 + 1);

    n = 0;
    r[n++] = '"';
    for (i = 0; i < blen; i++) {
	char c = 0;
	switch (s[i]) {
	case '\a': c = 'a'; break;
	case '\b': c = 'b'; break;
	case '\f': c = 'f'; break;
	case '\n': c = 'n'; break;
	case '\r': c = 'r'; break;
	case '\t': c = 't'; break;
	case '\v': c = 'v'; break;
	case '"': c = '"'; break;
	}

	if (c) {
	    r[n++] = '\\';
	    r[n++] = c;
	} else {
	    r[n++] = s[i];
	}
    }

    r[n++] = '"';
    r[n] = '\0';
    
    return r;
}

/*
  Scheme-ish write -- internal representation (where appropriate)
  suitable for (read).  Primarily:

  CHARACTER #\a:	#\a
  STRING "foo":		"foo"
 */
char *idio_as_string (IDIO o, int depth)
{
    char *r = NULL;
    size_t i;
    
    IDIO_C_ASSERT (depth >= -10000);

    if (depth < 0) {
	return NULL;
    }
    
    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    if (asprintf (&r, "%" PRIdPTR, IDIO_FIXNUM_VAL (o)) == -1) {
		idio_error_alloc ("asprintf");
	    }
	    break;
	}
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    /*
	     * character will set r directly but constants will point
	     * t to a fixed string (which gets copied)
	     */
	    char *t = NULL;
	    
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_TOKEN_VAL (o);
	    
		    switch (v) {
		    case IDIO_CONSTANT_NIL:                        t = "#n";                          break;
		    case IDIO_CONSTANT_UNDEF:                      t = "#<undef>";                    break;
		    case IDIO_CONSTANT_UNSPEC:                     t = "#<unspec>";                   break;
		    case IDIO_CONSTANT_EOF:                        t = "#<eof>";                      break;
		    case IDIO_CONSTANT_TRUE:                       t = "#t";                          break;
		    case IDIO_CONSTANT_FALSE:                      t = "#f";                          break;
		    case IDIO_CONSTANT_VOID:                       t = "#<void>";                     break;
		    case IDIO_CONSTANT_NAN:                        t = "#<NaN>";                      break;

			/*
			 * We shouldn't really see any of the
			 * following constants but they leak out
			 * especially when the code errors.
			 *
			 * It's then easier to debug if we can read
			 * "PREDEFINED" rather than "C=2001"
			 */
		    case IDIO_CONSTANT_TOPLEVEL:                   t = "toplevel/c";                  break;
		    case IDIO_CONSTANT_PREDEF:                     t = "predef/c";                    break;
		    case IDIO_CONSTANT_LOCAL:                      t = "local/c";                     break;
		    case IDIO_CONSTANT_ENVIRON:                    t = "environ/c";                   break;
		    case IDIO_CONSTANT_COMPUTED:                   t = "computed/c";                  break;
			
		    default:
			if (asprintf (&r, "#<type/constant/idio?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");
			}
			break;
		    }

		    if (NULL == t) {
			if (asprintf (&r, "#<C=%" PRIdPTR ">", v) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    } else {
			if (asprintf (&r, "%s", t) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    }
		}
		break;
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_IDIO_VAL (o);
	    
		    switch (v) {
			
		    case IDIO_TOKEN_DOT:                           t = "T/.";                         break;
		    case IDIO_TOKEN_LPAREN:                        t = "T/(";                         break;
		    case IDIO_TOKEN_RPAREN:                        t = "T/)";                         break;
		    case IDIO_TOKEN_LBRACE:                        t = "T/{";                         break;
		    case IDIO_TOKEN_RBRACE:                        t = "T/}";                         break;
		    case IDIO_TOKEN_LBRACKET:                      t = "T/[";                         break;
		    case IDIO_TOKEN_RBRACKET:                      t = "T/]";                         break;
		    case IDIO_TOKEN_LANGLE:                        t = "T/<";                         break;
		    case IDIO_TOKEN_RANGLE:                        t = "T/>";                         break;
		    case IDIO_TOKEN_EOL:                           t = "T/EOL";                       break;
		    case IDIO_TOKEN_PAIR_SEPARATOR:                t = "T/p-s";                       break;
			
		    default:
			if (asprintf (&r, "#<type/constant/token?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");
			}
			break;
		    }

		    if (NULL == t) {
			if (asprintf (&r, "#<C=%" PRIdPTR ">", v) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    } else {
			if (asprintf (&r, "%s", t) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    }
		}
		break;

	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_I_CODE_VAL (o);
	    
		    switch (v) {
			
		    case IDIO_I_CODE_SHALLOW_ARGUMENT_REF:        t = "SHALLOW-ARGUMENT-REF";        break;
		    case IDIO_I_CODE_PREDEFINED:                  t = "PREDEFINED";                  break;
		    case IDIO_I_CODE_DEEP_ARGUMENT_REF:           t = "DEEP-ARGUMENT-REF";           break;
		    case IDIO_I_CODE_SHALLOW_ARGUMENT_SET:        t = "SHALLOW-ARGUMENT-SET";        break;
		    case IDIO_I_CODE_DEEP_ARGUMENT_SET:           t = "DEEP-ARGUMENT-SET";           break;
		    case IDIO_I_CODE_GLOBAL_REF:                  t = "GLOBAL-REF";                  break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_REF:          t = "CHECKED-GLOBAL-REF";          break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_REF: t = "CHECKED-GLOBAL-FUNCTION-REF"; break;
		    case IDIO_I_CODE_GLOBAL_SET:                  t = "GLOBAL-SET";                  break;
		    case IDIO_I_CODE_CONSTANT:                    t = "CONSTANT";                    break;
		    case IDIO_I_CODE_ALTERNATIVE:                 t = "ALTERNATIVE";                 break;
		    case IDIO_I_CODE_SEQUENCE:                    t = "SEQUENCE";                    break;
		    case IDIO_I_CODE_TR_FIX_LET:                  t = "TR-FIX-LET";                  break;
		    case IDIO_I_CODE_FIX_LET:                     t = "FIX-LET";                     break;
		    case IDIO_I_CODE_PRIMCALL0:                   t = "PRIMCALL0";                   break;
		    case IDIO_I_CODE_PRIMCALL1:                   t = "PRIMCALL1";                   break;
		    case IDIO_I_CODE_PRIMCALL2:                   t = "PRIMCALL2";                   break;
		    case IDIO_I_CODE_PRIMCALL3:                   t = "PRIMCALL3";                   break;
		    case IDIO_I_CODE_FIX_CLOSURE:                 t = "FIX-CLOSURE";                 break;
		    case IDIO_I_CODE_NARY_CLOSURE:                t = "NARY-CLOSURE";                break;
		    case IDIO_I_CODE_TR_REGULAR_CALL:             t = "TR-REGULAR-CALL";             break;
		    case IDIO_I_CODE_REGULAR_CALL:                t = "REGULAR-CALL";                break;
		    case IDIO_I_CODE_STORE_ARGUMENT:              t = "STORE-ARGUMENT";              break;
		    case IDIO_I_CODE_CONS_ARGUMENT:               t = "CONS-ARGUMENT";               break;
		    case IDIO_I_CODE_ALLOCATE_FRAME:              t = "ALLOCATE-FRAME";              break;
		    case IDIO_I_CODE_ALLOCATE_DOTTED_FRAME:       t = "ALLOCATE-DOTTED-FRAME";       break;
		    case IDIO_I_CODE_FINISH:                      t = "FINISH";                      break;
		    case IDIO_I_CODE_PUSH_DYNAMIC:                t = "PUSH-DYNAMIC";                break;
		    case IDIO_I_CODE_POP_DYNAMIC:                 t = "POP-DYNAMIC";                 break;
		    case IDIO_I_CODE_DYNAMIC_REF:                 t = "DYNAMIC-REF";                 break;
		    case IDIO_I_CODE_DYNAMIC_FUNCTION_REF:        t = "DYNAMIC-FUNCTION-REF";        break;
		    case IDIO_I_CODE_ENVIRON_REF:                 t = "ENVIRON-REF";                 break;
		    case IDIO_I_CODE_PUSH_HANDLER:                t = "PUSH-HANDLER";                break;
		    case IDIO_I_CODE_POP_HANDLER:                 t = "POP-HANDLER";                 break;
		    case IDIO_I_CODE_PUSH_TRAP:                   t = "PUSH-TRAP";                break;
		    case IDIO_I_CODE_POP_TRAP:                    t = "POP-TRAP";                 break;
		    case IDIO_I_CODE_AND:                         t = "AND";                         break;
		    case IDIO_I_CODE_OR:                          t = "OR";                          break;
		    case IDIO_I_CODE_BEGIN:                       t = "BEGIN";                       break;
		    case IDIO_I_CODE_EXPANDER:                    t = "EXPANDER";                    break;
		    case IDIO_I_CODE_INFIX_OPERATOR:              t = "INFIX-OPERATOR";              break;
		    case IDIO_I_CODE_POSTFIX_OPERATOR:            t = "POSTFIX-OPERATOR";            break;
		    case IDIO_I_CODE_NOP:                         t = "NOP";                         break;

		    default:
			if (asprintf (&r, "#<type/constant/vm_code?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");
			}
			break;
		    }

		    if (NULL == t) {
			if (asprintf (&r, "#<C=%" PRIdPTR ">", v) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    } else {
			if (asprintf (&r, "%s", t) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    }
		}
		break;

	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		{
		    intptr_t c = IDIO_CHARACTER_VAL (o);
		    switch (c) {
		    case ' ':
			if (asprintf (&r, "#\\space") == -1) {
			    idio_error_alloc ("asprintf");
			}
			break;
		    case '\n':
			if (asprintf (&r, "#\\newline") == -1) {
			    idio_error_alloc ("asprintf");
			}
			break;
		    default:
			if (isprint (c)) {
			    if (asprintf (&r, "#\\%c", (char) c) == -1) {
				idio_error_alloc ("asprintf");
			    }
			} else {
			    if (asprintf (&r, "#\\%#" PRIxPTR, c) == -1) {
				idio_error_alloc ("asprintf");
			    }
			}
			break;
		    }
		    break;
		}
	    default:
		if (asprintf (&r, "#<type/constant?? %10p>", o) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    }
	}
	break;
    case IDIO_TYPE_PLACEHOLDER_MARK:
	if (asprintf (&r, "#<type/placecholder?? %10p>", o) == -1) {
	    idio_error_alloc ("asprintf");
	}
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    idio_type_e type = idio_type (o);
	    
	    switch (type) {
	    case IDIO_TYPE_STRING:
		r = idio_escape_string (IDIO_STRING_BLEN (o), IDIO_STRING_S (o));
		break;
	    case IDIO_TYPE_SUBSTRING:
		r = idio_escape_string (IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_S (o));
		break;
	    case IDIO_TYPE_SYMBOL:
		if (asprintf (&r, "%s", IDIO_SYMBOL_S (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_KEYWORD:
		if (asprintf (&r, ":%s", IDIO_KEYWORD_S (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_PAIR:
		/*
		  Technically a list (of pairs) should look like:

		  "(a . (b . (c . (d . nil))))"

		  but tradition dictates that we should flatten
		  the list to:

		  "(a b c d)"

		  hence the while loop which continues if the tail is
		  itself a pair
		*/
		{
		    if (idio_isa_symbol (IDIO_PAIR_H (o))) {
			int special = 0;
		
			if (idio_S_quote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "'") == -1) {
				idio_error_alloc ("asprintf");
			    }
			} else if (idio_S_unquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, ",") == -1) {
				idio_error_alloc ("asprintf");
			    }
			} else if (idio_S_unquotesplicing == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, ",@") == -1) {
				idio_error_alloc ("asprintf");
			    }
			} else if (idio_S_quasiquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "`") == -1) {
				idio_error_alloc ("asprintf");
			    }
			}

			if (special) {
			    if (idio_isa_pair (IDIO_PAIR_T (o))) {
				IDIO_STRCAT_FREE (r, idio_as_string (idio_list_head (IDIO_PAIR_T (o)), depth - 1));
			    } else {
				IDIO_STRCAT_FREE (r, idio_as_string (IDIO_PAIR_T (o), depth - 1));
			    }
			    break;
			}
		    }
	    
		    if (asprintf (&r, "(") == -1) {
			idio_error_alloc ("asprintf");
		    }
		    while (1) {
			IDIO_STRCAT_FREE (r, idio_as_string (IDIO_PAIR_H (o), depth - 1));

			o = IDIO_PAIR_T (o);
			if (idio_type (o) != IDIO_TYPE_PAIR) {
			    if (idio_S_nil != o) {
				char *t = idio_as_string (o, depth - 1);
				char *ps;
				if (asprintf (&ps, " %c %s", IDIO_PAIR_SEPARATOR, t) == -1) {
				    free (t);
				    free (r);
				    idio_error_alloc ("asprintf");
				}
				free (t);
				IDIO_STRCAT_FREE (r, ps);
			    }
			    break;
			} else {
			    IDIO_STRCAT (r, " ");
			}
		    }
		    IDIO_STRCAT (r, ")");
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (asprintf (&r, "#[ ") == -1) {
		    idio_error_alloc ("asprintf");
		}
		if (depth > 0) {
		    if (IDIO_ARRAY_USIZE (o) < 40) {
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), depth - 1);
			    char *aes;
			    if (asprintf (&aes, "%s ", t) == -1) {
				free (t);
				free (r);
				idio_error_alloc ("asprintf");
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, aes);
			}
		    } else {
			for (i = 0; i < 20; i++) {
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), depth - 1);
			    char *aes;
			    if (asprintf (&aes, "%s ", t) == -1) {
				free (t);
				free (r);
				idio_error_alloc ("asprintf");
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, aes);
			}
			char *aei;
			if (asprintf (&aei, "...[%zd] ", IDIO_ARRAY_USIZE (o) - 20) == -1) {
			    free (r);
			    idio_error_alloc ("asprintf");
			}
			IDIO_STRCAT_FREE (r, aei);
			for (i = IDIO_ARRAY_USIZE (o) - 20; i < IDIO_ARRAY_USIZE (o); i++) {
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), depth - 1);
			    char *aes;
			    if (asprintf (&aes, "%s ", t) == -1) {
				free (t);
				free (r);
				idio_error_alloc ("asprintf");
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, aes);
			}
		    }
		} else {
		    IDIO_STRCAT (r, "... ");
		}
		IDIO_STRCAT (r, "]");
		break;
	    case IDIO_TYPE_HASH:
		if (asprintf (&r, "#{ ") == -1) {
		    idio_error_alloc ("asprintf");
		}
		if (depth > 0) {
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
			    char *t;
			    if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
				t = (char *) IDIO_HASH_HE_KEY (o, i);
			    } else {
				t = idio_as_string (IDIO_HASH_HE_KEY (o, i), depth - 1);
			    }
			    char *hes;
			    if (asprintf (&hes, "(%s %c ", t, IDIO_PAIR_SEPARATOR) == -1) {
				if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				    free (t);
				}
				free (r);
				idio_error_alloc ("asprintf");
			    }
			    if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				free (t);
			    }
			    IDIO_STRCAT_FREE (r, hes);
			    if (IDIO_HASH_HE_VALUE (o, i)) {
				t = idio_as_string (IDIO_HASH_HE_VALUE (o, i), depth - 1);
			    } else {
				if (asprintf (&t, "-") == -1) {
				    free (r);
				    idio_error_alloc ("asprintf");
				}
			    }
			    if (asprintf (&hes, "%s) ", t) == -1) {
				free (t);
				free (r);
				idio_error_alloc ("asprintf");
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, hes);
			}
		    }
		} else {
		    IDIO_STRCAT (r, "...");
		}
		IDIO_STRCAT (r, "}");
		break;
	    case IDIO_TYPE_CLOSURE:
		{
		    if (asprintf (&r, "#<CLOS @%zd/%p/", IDIO_CLOSURE_CODE (o), IDIO_CLOSURE_FRAME (o)) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_CLOSURE_ENV (o), depth - 1));
		    IDIO_STRCAT (r, ">");
		    break;
		}
	    case IDIO_TYPE_PRIMITIVE:
		if (asprintf (&r, "#<PRIM %s>", IDIO_PRIMITIVE_NAME (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_BIGNUM:
		{
		    r = idio_bignum_as_string (o);
		    break;
		}
	    case IDIO_TYPE_MODULE:
		{
		    /* if (asprintf (&r, "#<module %p", o) == -1) { */
		    /* 	idio_error_alloc ("asprintf"); */
		    /* } */
		    if (asprintf (&r, "#<module ") == -1) {
			idio_error_alloc ("asprintf");
		    }
		    /* IDIO_STRCAT (r, " name="); */
		    if (idio_S_nil == IDIO_MODULE_NAME (o)) {
			IDIO_STRCAT (r, "(nil)");
		    } else {
			IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_NAME (o), depth - 1));
		    }
		    if (0 && depth > 0) {
			IDIO_STRCAT (r, " exports=");
			if (idio_S_nil == IDIO_MODULE_EXPORTS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_EXPORTS (o), depth - 1));
			}
			IDIO_STRCAT (r, " imports=");
			if (idio_S_nil == IDIO_MODULE_IMPORTS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_IMPORTS (o), 0));
			}
			IDIO_STRCAT (r, " symbols=");
			if (idio_S_nil == IDIO_MODULE_SYMBOLS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_SYMBOLS (o), depth - 1));
			}
		    }
		    IDIO_STRCAT (r, ">");
		    break;
		}
	    case IDIO_TYPE_FRAME:
		{
		    if (asprintf (&r, "#<FRAME %p ", o) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_FRAME_ARGS (o), 1));
		    IDIO_STRCAT (r, ">");
		    break;
		}
	    case IDIO_TYPE_HANDLE:
		{
		    if (asprintf (&r, "#<H ") == -1) {
			idio_error_alloc ("asprintf");
		    }
		    if (idio_isa_file_handle (o)) {
			char *fds;
			if (asprintf (&fds, "%d:", idio_file_handle_fd (o)) == -1) {
			    free (r);
			    idio_error_alloc ("asprintf");
			}
			IDIO_STRCAT_FREE (r, fds);
		    } else {
			IDIO_STRCAT (r, "-");
		    }

		    FLAGS_T h_flags = IDIO_HANDLE_FLAGS (o);
		    if (h_flags & IDIO_HANDLE_FLAG_STRING) {
			IDIO_STRCAT (r, "s");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_FILE) {
			IDIO_STRCAT (r, "f");

			FLAGS_T s_flags = IDIO_FILE_HANDLE_FLAGS (o);
			if (s_flags & IDIO_FILE_HANDLE_FLAG_CLOEXEC) {
			    IDIO_STRCAT (r, "E");
			}
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_CLOSED) {
			IDIO_STRCAT (r, "C");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_READ) {
			IDIO_STRCAT (r, "r");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_WRITE) {
			IDIO_STRCAT (r, "w");
		    }

		    char *info;
		    if (asprintf (&info, ":\"%s\":%lld:%lld>", IDIO_HANDLE_NAME (o), (unsigned long long) IDIO_HANDLE_LINE (o), (unsigned long long) IDIO_HANDLE_POS (o)) == -1) {
			free (r);
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, info);
		}
		break;
	    case IDIO_TYPE_C_INT:
		if (asprintf (&r, "%jd", IDIO_C_TYPE_INT (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_C_UINT:
		if (asprintf (&r, "%ju", IDIO_C_TYPE_UINT (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_C_FLOAT:
		if (asprintf (&r, "%g", IDIO_C_TYPE_FLOAT (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_C_DOUBLE:
		if (asprintf (&r, "%g", IDIO_C_TYPE_DOUBLE (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_C_POINTER:
		if (asprintf (&r, "#<C/* %p%s>", IDIO_C_TYPE_POINTER_P (o), IDIO_C_TYPE_POINTER_FREEP (o) ? " free" : "") == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		{
		    if (asprintf (&r, "#<ST %p ", o) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_STRUCT_TYPE_NAME (o), 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_STRUCT_TYPE_PARENT (o), 1));
		    
		    IDIO stf = IDIO_STRUCT_TYPE_FIELDS (o);

		    idio_ai_t al = idio_array_size (stf);
		    idio_ai_t ai;
		    for (ai = 0; ai < al; ai++) {
			IDIO_STRCAT (r, " ");
			IDIO_STRCAT_FREE (r, idio_as_string (idio_array_get_index (stf, ai), 1));
		    }

		    IDIO_STRCAT (r, ">");
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		{
		    if (asprintf (&r, "#<SI %p ", o) == -1) {
			idio_error_alloc ("asprintf");
		    }

		    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (o);
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_STRUCT_TYPE_NAME (sit), 1));
		    
		    IDIO stf = IDIO_STRUCT_TYPE_FIELDS (sit);
		    IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (o);

		    idio_ai_t al = idio_array_size (stf);
		    idio_ai_t ai;
		    for (ai = 0; ai < al; ai++) {
			IDIO_STRCAT (r, " ");
			IDIO_STRCAT_FREE (r, idio_as_string (idio_array_get_index (stf, ai), 1));
			IDIO_STRCAT (r, ":");
			IDIO_STRCAT_FREE (r, idio_as_string (idio_array_get_index (sif, ai), 1));
		    }

		    IDIO_STRCAT (r, ">");
		}
		break;
	    case IDIO_TYPE_THREAD:
		{
		    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (o));
		    if (asprintf (&r, "#<THREAD %p pc=%6zd sp/top=%2zd/",
				  o,
				  IDIO_THREAD_PC (o),
				  sp - 1) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (idio_array_top (IDIO_THREAD_STACK (o)), 1));
		    IDIO_STRCAT (r, " val=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_VAL (o), 2));
		    IDIO_STRCAT (r, " func=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_FUNC (o), 1));
		    if (1 == depth) {
			IDIO frame = IDIO_THREAD_FRAME (o);

			if (idio_S_nil == frame) {
			    IDIO_STRCAT (r, " fr=nil");
			} else {
			    char *es;
			    if (asprintf (&es, " fr=%p ", frame) == -1) {
				idio_error_alloc ("asprintf");
			    }
			    IDIO_STRCAT_FREE (r, es);
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_FRAME_ARGS (frame), 1));
			}
		    }
		    IDIO_STRCAT (r, " env=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_ENV (o), 1));
		    IDIO_STRCAT (r, " t/sp=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_TRAP_SP (o), 1));
		    IDIO_STRCAT (r, " h/sp=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_HANDLER_SP (o), 1));
		    IDIO_STRCAT (r, " d/sp=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_DYNAMIC_SP (o), 1));
		    IDIO_STRCAT (r, " e/sp=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_ENVIRON_SP (o), 1));
		    if (depth > 1) {
			IDIO_STRCAT (r, " fr=");
			IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_FRAME (o), 1));
			if (depth > 2) {
			    IDIO_STRCAT (r, " reg1=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_REG1 (o), 1));
			    IDIO_STRCAT (r, " reg2=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_REG2 (o), 1));
			    IDIO_STRCAT (r, " input_handle=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_INPUT_HANDLE (o), 1));
			    IDIO_STRCAT (r, " output_handle=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_OUTPUT_HANDLE (o), 1));
			    IDIO_STRCAT (r, " error_handle=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_ERROR_HANDLE (o), 1));
			    IDIO_STRCAT (r, " module=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_MODULE (o), 1));
			}
		    }
		    IDIO_STRCAT (r, ">");
		}
		break;
	    case IDIO_TYPE_CONTINUATION:
		{
		    idio_ai_t sp = idio_array_size (IDIO_CONTINUATION_STACK (o));
		    if (asprintf (&r, "#<K %p sp/top=%2zd/", o, sp) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (idio_array_top (IDIO_CONTINUATION_STACK (o)), 1));
		    IDIO_STRCAT (r, ">");
		}
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		{
		    if (asprintf (&r, "#<C/typedef %10p>", IDIO_C_TYPEDEF_SYM (o)) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    break;
		}
	    case IDIO_TYPE_C_STRUCT:
		{
		    if (asprintf (&r, "#<C/struct %10p ", o) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT (r, "\n\tfields: ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_STRUCT_FIELDS (o), depth - 1));

		    IDIO mh = IDIO_C_STRUCT_METHODS (o);
	    
		    IDIO_STRCAT (r, "\n\tmethods: ");
		    if (idio_S_nil != mh) {
			for (i = 0; i < IDIO_HASH_SIZE (mh); i++) {
			    if (idio_S_nil != IDIO_HASH_HE_KEY (mh, i)) {
				char *t = idio_as_string (IDIO_HASH_HE_KEY (mh, i), depth - 1);
				char *hes;
				if (asprintf (&hes, "\n\t%10s:", t) == -1) {
				    free (t);
				    free (r);
				    idio_error_alloc ("asprintf");
				}
				free (t);
				IDIO_STRCAT_FREE (r, hes);
				if (IDIO_HASH_HE_VALUE (mh, i)) {
				    t = idio_as_string (IDIO_HASH_HE_VALUE (mh, i), depth - 1);
				} else {
				    if (asprintf (&t, "-") == -1) {
					free (r);
					idio_error_alloc ("asprintf");
				    }
				}
				if (asprintf (&hes, "%s ", t) == -1) {
				    free (t);
				    free (r);
				    idio_error_alloc ("asprintf");
				}
				free (t);
				IDIO_STRCAT_FREE (r, hes);
			    }
			}
		    }
		    IDIO_STRCAT (r, "\n\tframe: ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_STRUCT_FRAME (o), depth - 1));
		    IDIO_STRCAT (r, "\n>");
		    break;
		}
	    case IDIO_TYPE_C_INSTANCE:
		{
		    if (asprintf (&r, "#<C/instance %10p C/*=%10p C/struct=%10p>", o, IDIO_C_INSTANCE_P (o), IDIO_C_INSTANCE_C_STRUCT (o)) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    break;
		}
	    case IDIO_TYPE_C_FFI:
		{
		    char *t = idio_as_string (IDIO_C_FFI_NAME (o), depth - 1);
		    if (asprintf (&r, "#<CFFI %s ", t) == -1) {
			free (t);
			idio_error_alloc ("asprintf");
		    }
		    free (t);
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_SYMBOL (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_RESULT (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_NAME (o), depth - 1));
		    IDIO_STRCAT (r, " >");
		    break;
		}
	    case IDIO_TYPE_OPAQUE:
		{
		    if (asprintf (&r, "#<O %10p ", IDIO_OPAQUE_P (o)) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_OPAQUE_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, ">");
		    break;
		}
	    default:
		{
		    /*
		     * Oh dear, bad data.  Can we dump any clues?
		     *
		     * If it's a short enough string then its length
		     * will be less than 40 chars, otherwise we can
		     * only dump out a C pointer.
		     *
		     * Of course the string can still be gobbledegook
		     * but it's something to go on if we've trampled
		     * on a hash's string key.
		     */
		    size_t n = strnlen ((char *) o, 40);
		    if (40 == n) {
			if (asprintf (&r, "#<void?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    } else {
			if (asprintf (&r, "#<string?? \"%s\">", (char *) o) == -1) {
			    idio_error_alloc ("asprintf");
			}
		    }
		    IDIO_C_ASSERT (0);
		}
		break;
	    }
	}
	break;
    default:
	if (asprintf (&r, "#<type?? %10p>", o) == -1) {
	    idio_error_alloc ("asprintf");
	}
	break;
    }

    return r;
}

/*
  Scheme-ish display -- no internal representation (where
  appropriate).  Unsuitable for (read).  Primarily:

  CHARACTER #\a:	a
  STRING "foo":		foo

  Most non-data types will still come out as some internal
  representation.  (Still unsuitable for (read) as it doesn't know
  about them.)
 */
char *idio_display_string (IDIO o)
{
    char *r;
    
    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	r = idio_as_string (o, 4);
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		r = idio_as_string (o, 4);
		break;
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		{
		    intptr_t c = IDIO_CHARACTER_VAL (o);
		    if (asprintf (&r, "%c", (char) c) == -1) {
			idio_error_alloc ("asprintf");
		    }
		    /*
		     * If we check for isprint(c) then (newline) fails as
		     * it'll be printed as \\0xa
		     */
		    /* if (isprint (c)) { */
		    /* } else { */
		    /* 	if (asprintf (&r, "x%" PRIxPTR, IDIO_CHARACTER_VAL (o)) == -1) { */
		    /* 	    idio_error_alloc ("asprintf"); */
		    /* 	} */
		    /* } */
		}
	    }
	}
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_STRING:
		if (asprintf (&r, "%.*s", (int) IDIO_STRING_BLEN (o), IDIO_STRING_S (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (asprintf (&r, "%.*s", (int) IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_S (o)) == -1) {
		    idio_error_alloc ("asprintf");
		}
		break;
	    default:
		r = idio_as_string (o, 4);
		break;
	    }
	}
	break;
    default:
	if (asprintf (&r, "type %d n/k", o->type) == -1) {
	    idio_error_alloc ("asprintf");
	}
	break;
    }
    
    return r;
}

const char *idio_vm_bytecode2string (int code)
{
    char *r;

    switch (code) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0: r = "SHALLOW_ARGUMENT_REF0"; break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1: r = "SHALLOW_ARGUMENT_REF1"; break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2: r = "SHALLOW_ARGUMENT_REF2"; break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3: r = "SHALLOW_ARGUMENT_REF3"; break;
    case IDIO_A_SHALLOW_ARGUMENT_REF: r = "SHALLOW_ARGUMENT_REF"; break;
    case IDIO_A_DEEP_ARGUMENT_REF: r = "DEEP_ARGUMENT_REF"; break;
    case IDIO_A_GLOBAL_REF: r = "GLOBAL_REF"; break;
    case IDIO_A_CHECKED_GLOBAL_REF: r = "CHECKED_GLOBAL_REF"; break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_REF: r = "CHECKED_GLOBAL_FUNCTION_REF"; break;
    case IDIO_A_CONSTANT_REF: r = "CONSTANT_REF"; break;
    case IDIO_A_PREDEFINED0: r = "PREDEFINED0"; break;

    case IDIO_A_COMPUTED_REF: r = "COMPUTED_REF"; break;
    case IDIO_A_PREDEFINED1: r = "PREDEFINED1"; break;
    case IDIO_A_PREDEFINED2: r = "PREDEFINED2"; break;
    case IDIO_A_PREDEFINED3: r = "PREDEFINED3"; break;
    case IDIO_A_PREDEFINED4: r = "PREDEFINED4"; break;
    case IDIO_A_PREDEFINED5: r = "PREDEFINED5"; break;
    case IDIO_A_PREDEFINED6: r = "PREDEFINED6"; break;
    case IDIO_A_PREDEFINED7: r = "PREDEFINED7"; break;
    case IDIO_A_PREDEFINED8: r = "PREDEFINED8"; break;
    case IDIO_A_PREDEFINED: r = "PREDEFINED"; break;

    case IDIO_A_SHALLOW_ARGUMENT_SET0: r = "SHALLOW_ARGUMENT_SET0"; break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1: r = "SHALLOW_ARGUMENT_SET1"; break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2: r = "SHALLOW_ARGUMENT_SET2"; break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3: r = "SHALLOW_ARGUMENT_SET3"; break;
    case IDIO_A_SHALLOW_ARGUMENT_SET: r = "SHALLOW_ARGUMENT_SET"; break;
    case IDIO_A_DEEP_ARGUMENT_SET: r = "DEEP_ARGUMENT_SET"; break;
    case IDIO_A_GLOBAL_DEF: r = "GLOBAL_DEF"; break;
    case IDIO_A_GLOBAL_SET: r = "GLOBAL_SET"; break;
    case IDIO_A_COMPUTED_SET: r = "COMPUTED_SET"; break;
    case IDIO_A_COMPUTED_DEFINE: r = "COMPUTED_DEFINE"; break;

    case IDIO_A_LONG_GOTO: r = "LONG_GOTO"; break;
    case IDIO_A_LONG_JUMP_FALSE: r = "LONG_JUMP_FALSE"; break;
    case IDIO_A_SHORT_GOTO: r = "SHORT_GOTO"; break;
    case IDIO_A_SHORT_JUMP_FALSE: r = "SHORT_JUMP_FALSE"; break;

    case IDIO_A_PUSH_VALUE: r = "PUSH_VALUE"; break;
    case IDIO_A_POP_VALUE: r = "POP_VALUE"; break;
    case IDIO_A_POP_REG1: r = "POP_REG1"; break;
    case IDIO_A_POP_REG2: r = "POP_REG2"; break;
    case IDIO_A_POP_FUNCTION: r = "POP_FUNCTION"; break;
    case IDIO_A_PRESERVE_STATE: r = "PRESERVE_STATE"; break;
    case IDIO_A_RESTORE_STATE: r = "RESTORE_STATE"; break;
    case IDIO_A_CREATE_CLOSURE: r = "CREATE_CLOSURE"; break;

    case IDIO_A_RESTORE_ALL_STATE: r = "RESTORE_ALL_STATE"; break;
    case IDIO_A_FUNCTION_INVOKE: r = "FUNCTION_INVOKE"; break;
    case IDIO_A_FUNCTION_GOTO: r = "FUNCTION_GOTO"; break;
    case IDIO_A_RETURN: r = "RETURN"; break;
    case IDIO_A_FINISH: r = "FINISH"; break;

    case IDIO_A_ALLOCATE_FRAME1: r = "ALLOCATE_FRAME1"; break;
    case IDIO_A_ALLOCATE_FRAME2: r = "ALLOCATE_FRAME2"; break;
    case IDIO_A_ALLOCATE_FRAME3: r = "ALLOCATE_FRAME3"; break;
    case IDIO_A_ALLOCATE_FRAME4: r = "ALLOCATE_FRAME4"; break;
    case IDIO_A_ALLOCATE_FRAME5: r = "ALLOCATE_FRAME5"; break;
    case IDIO_A_ALLOCATE_FRAME: r = "ALLOCATE_FRAME"; break;
    case IDIO_A_POP_FRAME0: r = "POP_FRAME0"; break;

    case IDIO_A_ALLOCATE_DOTTED_FRAME: r = "ALLOCATE_DOTTED_FRAME"; break;
    case IDIO_A_POP_FRAME1: r = "POP_FRAME1"; break;
    case IDIO_A_POP_FRAME2: r = "POP_FRAME2"; break;
    case IDIO_A_POP_FRAME3: r = "POP_FRAME3"; break;
    case IDIO_A_EXTEND_FRAME: r = "EXTEND_FRAME"; break;

    case IDIO_A_POP_FRAME: r = "POP_FRAME"; break;
    case IDIO_A_UNLINK_FRAME: r = "UNLINK_FRAME"; break;
    case IDIO_A_PACK_FRAME: r = "PACK_FRAME"; break;
    case IDIO_A_POP_CONS_FRAME: r = "POP_CONS_FRAME"; break;

    case IDIO_A_ARITY1P: r = "ARITY1P"; break;
    case IDIO_A_ARITY2P: r = "ARITY2P"; break;
    case IDIO_A_ARITY3P: r = "ARITY3P"; break;
    case IDIO_A_ARITY4P: r = "ARITY4P"; break;
    case IDIO_A_ARITYEQP: r = "ARITYEQP"; break;
    case IDIO_A_ARITYGEP: r = "ARITYGEP"; break;

    case IDIO_A_SHORT_NUMBER: r = "SHORT_NUMBER"; break;
    case IDIO_A_SHORT_NEG_NUMBER: r = "SHORT_NEG_NUMBER"; break;
    case IDIO_A_CONSTANT_0: r = "CONSTANT_0"; break;
    case IDIO_A_CONSTANT_1: r = "CONSTANT_1"; break;
    case IDIO_A_CONSTANT_2: r = "CONSTANT_2"; break;
    case IDIO_A_CONSTANT_3: r = "CONSTANT_3"; break;
    case IDIO_A_CONSTANT_4: r = "CONSTANT_4"; break;

    case IDIO_A_PRIMCALL0_NEWLINE: r = "PRIMCALL0_NEWLINE"; break;
    case IDIO_A_PRIMCALL0_READ: r = "PRIMCALL0_READ"; break;
    case IDIO_A_PRIMCALL1_HEAD: r = "PRIMCALL1_HEAD"; break;
    case IDIO_A_PRIMCALL1_TAIL: r = "PRIMCALL1_TAIL"; break;
    case IDIO_A_PRIMCALL1_PAIRP: r = "PRIMCALL1_PAIRP"; break;
    case IDIO_A_PRIMCALL1_SYMBOLP: r = "PRIMCALL1_SYMBOLP"; break;
    case IDIO_A_PRIMCALL1_DISPLAY: r = "PRIMCALL1_DISPLAY"; break;
    case IDIO_A_PRIMCALL1_PRIMITIVEP: r = "PRIMCALL1_PRIMITIVEP"; break;
    case IDIO_A_PRIMCALL1_NULLP: r = "PRIMCALL1_NULLP"; break;
    case IDIO_A_PRIMCALL1_CONTINUATIONP: r = "PRIMCALL1_CONTINUATIONP"; break;
    case IDIO_A_PRIMCALL1_EOFP: r = "PRIMCALL1_EOFP"; break;
    case IDIO_A_PRIMCALL1_SET_CUR_MOD: r = "PRIMCALL1_SET_CUR_MOD"; break;
    case IDIO_A_PRIMCALL2_PAIR: r = "PRIMCALL2_PAIR"; break;
    case IDIO_A_PRIMCALL2_EQP: r = "PRIMCALL2_EQP"; break;
    case IDIO_A_PRIMCALL2_SET_HEAD: r = "PRIMCALL2_SET_HEAD"; break;
    case IDIO_A_PRIMCALL2_SET_TAIL: r = "PRIMCALL2_SET_TAIL"; break;
    case IDIO_A_PRIMCALL2_ADD: r = "PRIMCALL2_ADD"; break;
    case IDIO_A_PRIMCALL2_SUBTRACT: r = "PRIMCALL2_SUBTRACT"; break;
    case IDIO_A_PRIMCALL2_EQ: r = "PRIMCALL2_EQ"; break;
    case IDIO_A_PRIMCALL2_LT: r = "PRIMCALL2_LT"; break;
    case IDIO_A_PRIMCALL2_GT: r = "PRIMCALL2_GT"; break;
    case IDIO_A_PRIMCALL2_MULTIPLY: r = "PRIMCALL2_MULTIPLY"; break;
    case IDIO_A_PRIMCALL2_LE: r = "PRIMCALL2_LE"; break;
    case IDIO_A_PRIMCALL2_GE: r = "PRIMCALL2_GE"; break;
    case IDIO_A_PRIMCALL2_REMAINDER: r = "PRIMCALL2_REMAINDER"; break;

    case IDIO_A_NOP: r = "NOP"; break;
    case IDIO_A_PRIMCALL0: r = "PRIMCALL0"; break;
    case IDIO_A_PRIMCALL1: r = "PRIMCALL1"; break;
    case IDIO_A_PRIMCALL2: r = "PRIMCALL2"; break;
    case IDIO_A_PRIMCALL3: r = "PRIMCALL3"; break;
    case IDIO_A_PRIMCALL: r = "PRIMCALL"; break;

    case IDIO_A_LONG_JUMP_TRUE: r = "LONG_JUMP_TRUE"; break;
    case IDIO_A_SHORT_JUMP_TRUE: r = "SHORT_JUMP_TRUE"; break;
    case IDIO_A_FIXNUM: r = "FIXNUM"; break;
    case IDIO_A_NEG_FIXNUM: r = "NEG_FIXNUM"; break;
    case IDIO_A_CHARACTER: r = "CHARACTER"; break;
    case IDIO_A_NEG_CHARACTER: r = "NEG_CHARACTER"; break;
    case IDIO_A_CONSTANT: r = "CONSTANT"; break;
    case IDIO_A_NEG_CONSTANT: r = "NEG_CONSTANT"; break;

    case IDIO_A_EXPANDER: r = "EXPANDER"; break;
    case IDIO_A_INFIX_OPERATOR: r = "INFIX_OPERATOR"; break;
    case IDIO_A_POSTFIX_OPERATOR: r = "POSTFIX_OPERATOR"; break;

    case IDIO_A_DYNAMIC_REF: r = "DYNAMIC_REF"; break;
    case IDIO_A_DYNAMIC_FUNCTION_REF: r = "DYNAMIC_FUNCTION_REF"; break;
    case IDIO_A_POP_DYNAMIC: r = "POP_DYNAMIC"; break;
    case IDIO_A_PUSH_DYNAMIC: r = "PUSH_DYNAMIC"; break;

    case IDIO_A_ENVIRON_REF: r = "ENVIRON_REF"; break;
    case IDIO_A_POP_ENVIRON: r = "POP_ENVIRON"; break;
    case IDIO_A_PUSH_ENVIRON: r = "PUSH_ENVIRON"; break;

    case IDIO_A_NON_CONT_ERR: r = "NON_CONT_ERR"; break;
    case IDIO_A_PUSH_HANDLER: r = "PUSH_HANDLER"; break;
    case IDIO_A_POP_HANDLER: r = "POP_HANDLER"; break;
    case IDIO_A_RESTORE_HANDLER: r = "RESTORE_HANDLER"; break;
    case IDIO_A_PUSH_TRAP: r = "PUSH_TRAP"; break;
    case IDIO_A_POP_TRAP: r = "POP_TRAP"; break;
    case IDIO_A_RESTORE_TRAP: r = "RESTORE_TRAP"; break;

    case IDIO_A_POP_ESCAPER: r = "POP_ESCAPER"; break;
    case IDIO_A_PUSH_ESCAPER: r = "PUSH_ESCAPER"; break;

    default:
	fprintf (stderr, "idio_vm_bytecode2string: unexpected bytecode %d\n", code);
	r = "Unknown bytecode";
	break;
    }

    return r;
}

/* 
 * Basic map -- only one list and the function must be a primitive so
 * we can call it directly.  We can't apply a closure as apply only
 * changes the PC, it doesn't actually do anything!
 *
 * The real map is defined early on in bootstrap.
 */
IDIO_DEFINE_PRIMITIVE2 ("%map1", map1, (IDIO fn, IDIO list))
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (list);

    IDIO_VERIFY_PARAM_TYPE (primitive, fn);
    IDIO_VERIFY_PARAM_TYPE (list, list);

    IDIO r = idio_S_nil;

    /* idio_debug ("map: in: %s\n", list); */

    while (idio_S_nil != list) {
	r = idio_pair (IDIO_PRIMITIVE_F (fn) (IDIO_PAIR_H (list)), r);
	list = IDIO_PAIR_T (list);
    }

    r = idio_list_reverse (r);
    /* idio_debug ("map => %s\n", r); */
    return r;
}

IDIO idio_list_map_ph (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_H (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_map_pt (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_T (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_memq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (idio_eqp (k, IDIO_PAIR_H (l))) {
	    return l;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("memq", memq, (IDIO k, IDIO l))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_memq (k, l);
}

IDIO idio_list_assq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    /* fprintf (stderr, "assq: k=%s in l=%s\n", idio_as_string (k, 1), idio_as_string (idio_list_map_ph (l), 4)); */
    
    while (idio_S_nil != l) {
	IDIO p = IDIO_PAIR_H (l);

	if (idio_S_nil == p) {
	    return idio_S_false;
	}

	if (! idio_isa_pair (p)) {
	    idio_error_C ("not a pair in list", IDIO_LIST2 (p, l), IDIO_C_LOCATION ("idio_list_assq"));
	}

	if (idio_eqp (k, IDIO_PAIR_H (p))) {
	    return p;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("assq", assq, (IDIO k, IDIO l))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_assq (k, l);
}

IDIO idio_list_set_difference (IDIO set1, IDIO set2)
{
    if (idio_isa_pair (set1)) {
	if (idio_S_false != idio_list_memq (IDIO_PAIR_H (set1), set2)) {
	    return idio_list_set_difference (IDIO_PAIR_T (set1), set2);
	} else {
	    return idio_pair (IDIO_PAIR_H (set1),
			      idio_list_set_difference (IDIO_PAIR_T (set1), set2));
	}
    } else {
	if (idio_S_nil != set1) {
	    idio_error_C ("set1", set1, IDIO_C_LOCATION ("idio_list_set_difference"));

	    /* notreached */
	    return idio_S_unspec;
	}
	return idio_S_nil;
    }
}

IDIO_DEFINE_PRIMITIVE2 ("value-index", value_index, (IDIO o, IDIO i))
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (i);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_SUBSTRING:
	    case IDIO_TYPE_STRING:
		return idio_string_ref (o, i);
	    case IDIO_TYPE_ARRAY:
		return idio_array_ref (o, i);
	    case IDIO_TYPE_HASH:
		return idio_hash_ref (o, i, idio_S_nil);
	    case IDIO_TYPE_STRUCT_INSTANCE:
		return idio_struct_instance_ref (o, i);
	    default:
		break;
	    }
	}
    default:
	break;
    }

    idio_error_C ("non-indexable object", IDIO_LIST2 (o, i), IDIO_C_LOCATION ("value-index"));

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3 ("set-value-index!", set_value_index, (IDIO o, IDIO i, IDIO v))
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_SUBSTRING:
	    case IDIO_TYPE_STRING:
		return idio_string_set (o, i, v);
	    case IDIO_TYPE_ARRAY:
		return idio_array_set (o, i, v);
	    case IDIO_TYPE_HASH:
		return idio_hash_set (o, i, v);
	    case IDIO_TYPE_STRUCT_INSTANCE:
		return idio_struct_instance_set (o, i, v);
	    default:
		break;
	    }
	}
    default:
	break;
    }

    idio_error_C ("non-indexable object", IDIO_LIST2 (o, i), IDIO_C_LOCATION ("set-value-index!"));

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("identity", identity, (IDIO o))
{
    IDIO_ASSERT (o);

    return o;
}

void idio_dump (IDIO o, int detail)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    if (detail > 0) {
		IDIO_FPRINTF (stderr, "%10p ", o);
		if (detail > 1) {
		    IDIO_FPRINTF (stderr, "-> %10p ", o->next);
		}
		IDIO_FPRINTF (stderr, "t=%2d/%4.4s f=%2x ", o->type, idio_type2string (o), o->flags);
	    }
    
	    switch (o->type) {
	    case IDIO_TYPE_STRING:
		if (detail) {
		    IDIO_FPRINTF (stderr, "blen=%d s=", IDIO_STRING_BLEN (o));
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (detail) {
		    IDIO_FPRINTF (stderr, "blen=%d parent=%10p subs=", IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_PARENT (o));
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		IDIO_FPRINTF (stderr, "sym=");
		break;
	    case IDIO_TYPE_KEYWORD:
		IDIO_FPRINTF (stderr, "key=");
		break;
	    case IDIO_TYPE_PAIR:
		if (detail > 1) {
		    IDIO_FPRINTF (stderr, "head=%10p tail=%10p p=", IDIO_PAIR_H (o), IDIO_PAIR_T (o));
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (detail) {
		    IDIO_FPRINTF (stderr, "size=%d/%d \n", IDIO_ARRAY_USIZE (o), IDIO_ARRAY_ASIZE (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    if (idio_S_nil != IDIO_ARRAY_AE (o, i) ||
				detail > 3) {
				char *s = idio_as_string (IDIO_ARRAY_AE (o, i), 4);
				IDIO_FPRINTF (stderr, "\t%3d: %10p %10s\n", i, IDIO_ARRAY_AE (o, i), s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_HASH:
		if (detail) {
		    IDIO_FPRINTF (stderr, "hsize=%d hmask=%x\n", IDIO_HASH_SIZE (o), IDIO_HASH_MASK (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			    if (idio_S_nil == IDIO_HASH_HE_KEY (o, i)) {
				continue;
			    } else {
				char *s;
				if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
				    s = (char *) IDIO_HASH_HE_KEY (o, i);
				} else {
				    s = idio_as_string (IDIO_HASH_HE_KEY (o, i), 4);
				}
				if (detail & 0x4) {
				    IDIO_FPRINTF (stderr, "\t%30s : ", s);
				} else {
				    IDIO_FPRINTF (stderr, "\t%3d: k=%10p v=%10p n=%3d %10s : ",
							i,
							IDIO_HASH_HE_KEY (o, i),
							IDIO_HASH_HE_VALUE (o, i),
							IDIO_HASH_HE_NEXT (o, i),
							s);
				}
				if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				    free (s);
				}
				if (IDIO_HASH_HE_VALUE (o, i)) {
				    s = idio_as_string (IDIO_HASH_HE_VALUE (o, i), 4);
				} else {
				    if (asprintf (&s, "-") == -1) {
					idio_error_alloc ("asprintf");
				    }
				}
				IDIO_FPRINTF (stderr, "%-10s\n", s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_CLOSURE:
	    case IDIO_TYPE_PRIMITIVE:
	    case IDIO_TYPE_BIGNUM:
	    case IDIO_TYPE_MODULE:
	    case IDIO_TYPE_FRAME:
	    case IDIO_TYPE_HANDLE:
	    case IDIO_TYPE_STRUCT_TYPE:
	    case IDIO_TYPE_STRUCT_INSTANCE:
	    case IDIO_TYPE_THREAD:
	    case IDIO_TYPE_CONTINUATION:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_POINTER:
	    case IDIO_TYPE_C_TYPEDEF:
	    case IDIO_TYPE_C_STRUCT:
	    case IDIO_TYPE_C_INSTANCE:
	    case IDIO_TYPE_C_FFI:
	    case IDIO_TYPE_OPAQUE:
		break;
	    default:
		idio_error_C ("unimplemented type", o, IDIO_C_LOCATION ("idio_dump"));
		break;
	    }
	}
	break;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_LOCATION ("idio_dump"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", o, (intptr_t) o & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

	break;
    }	

    if (detail &&
	!(detail & 0x4)) {
	idio_debug ("%s", o);
    }

    fprintf (stderr, "\n");
}

void idio_debug_FILE (FILE *file, const char *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    char *os = idio_as_string (o, 40);
    fprintf (file, fmt, os);
    free (os);
}

void idio_debug (const char *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    idio_debug_FILE (stderr, fmt, o);
}

IDIO_DEFINE_PRIMITIVE2 ("idio-debug", idio_debug, (IDIO fmt, IDIO o))
{
    IDIO_ASSERT (fmt);
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (string, fmt);

    idio_debug (IDIO_STRING_S (fmt), o);

    return idio_S_unspec;
}

#if ! defined (strnlen)
/*
 * Mac OS X - 10.5.8
 */
size_t strnlen (const char *s, size_t maxlen)
{
    size_t n = 0;
    while (*s &&
	   n < maxlen) {
	n++;
    }

    return n;
}
#endif

void idio_init_util ()
{
}

void idio_util_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (nullp);
    IDIO_ADD_PRIMITIVE (unsetp);
    IDIO_ADD_PRIMITIVE (undefp);
    IDIO_ADD_PRIMITIVE (unspecp);
    IDIO_ADD_PRIMITIVE (voidp);
    IDIO_ADD_PRIMITIVE (definedp);
    IDIO_ADD_PRIMITIVE (booleanp);
    IDIO_ADD_PRIMITIVE (not);
    IDIO_ADD_PRIMITIVE (eqp);
    IDIO_ADD_PRIMITIVE (eqvp);
    /* IDIO_ADD_PRIMITIVE (equalp); */
    IDIO_ADD_PRIMITIVE (zerop);
    IDIO_ADD_PRIMITIVE (map1);
    IDIO_ADD_PRIMITIVE (memq);
    IDIO_ADD_PRIMITIVE (assq);
    IDIO_ADD_PRIMITIVE (value_index);
    IDIO_ADD_PRIMITIVE (set_value_index);
    IDIO_ADD_PRIMITIVE (identity);
    IDIO_ADD_PRIMITIVE (idio_debug);
}

void idio_final_util ()
{
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: undecided-unix */
/* End: */
