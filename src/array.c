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
 * array.c
 * 
 */

#include "idio.h"

void idio_assign_array (IDIO f, IDIO a, size_t asize)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);
    IDIO_C_ASSERT (asize);

    IDIO_TYPE_ASSERT (array, a);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_assign_array: %10p = [%d]\n", a, asize);

    IDIO_ALLOC (f, a->u.array, sizeof (idio_array_t));
    IDIO_ALLOC (f, a->u.array->ae, asize * sizeof (idio_t));
    
    IDIO_ARRAY_GREY (a) = NULL;
    IDIO_ARRAY_ASIZE (a) = asize;
    IDIO_ARRAY_USIZE (a) = 0;
    idio_index_t i;
    for (i = 0; i < asize; i++) {
	IDIO_ARRAY_AE (a, i) = idio_S_nil;
    }
}

IDIO idio_array (IDIO f, size_t size)
{
    IDIO_ASSERT (f);

    if (0 == size) {
	size = 1;
    }
    
    IDIO a = idio_get (f, IDIO_TYPE_ARRAY);
    idio_assign_array (f, a, size);
    return a;
}

int idio_isa_array (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    return idio_isa (f, a, IDIO_TYPE_ARRAY);
}

void idio_free_array (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    idio_gc_t *gc = IDIO_GC (f);

    gc->stats.nbytes -= sizeof (idio_array_t) + IDIO_ARRAY_ASIZE (a) * sizeof (idio_t);

    free (a->u.array->ae);
    free (a->u.array);
}

void idio_array_resize (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    idio_gc_t *gc = IDIO_GC (f);

    idio_array_t *oarray = a->u.array;
    size_t oasize = IDIO_ARRAY_ASIZE (a);
    size_t ousize = IDIO_ARRAY_USIZE (a);
    size_t nsize = oasize << 1;

    IDIO_FRAME_FPRINTF (f, stderr, "idio_array_resize: %10p = {%d} -> {%d}\n", a, oasize, nsize);
    idio_assign_array (f, a, nsize);

    size_t i;
    for (i = 0 ; i < oarray->usize; i++) {
	idio_array_insert_index (f, a, oarray->ae[i], i);
    }

    /* pad with idio_S_nil */
    for (;i < IDIO_ARRAY_ASIZE (a); i++) {
	idio_array_insert_index (f, a, idio_S_nil, i);
    }

    /* all the inserts above will have mucked usize up */
    IDIO_ARRAY_USIZE (a) = ousize;
    
    gc->stats.nbytes -= sizeof (idio_array_t) + oasize * sizeof (idio_t);
    gc->stats.tbytes -= sizeof (idio_array_t) + oasize * sizeof (idio_t);

    free (oarray->ae);
    free (oarray);
}

idio_index_t idio_array_size (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    return IDIO_ARRAY_USIZE (a);
}

void idio_array_insert_index (IDIO f, IDIO a, IDIO o, idio_index_t index)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	/*
	  negative indexes cannot be larger than the size of the
	  existing array
	*/
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    fprintf (stderr, "idio_array_insert_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	    IDIO_C_EXIT (1);
	}
    } else if (index >= IDIO_ARRAY_ASIZE (a)) {
	/*
	  positive indexes can't be too large...
	*/
	if (index < (IDIO_ARRAY_ASIZE (a) * 2)) {
	    idio_array_resize (f, a);
	} else {	
	    fprintf (stderr, "idio_array_insert_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_ASIZE (a));
	    IDIO_C_EXIT (1);
	}
    }

    IDIO_ARRAY_AE (a, index) = o;

    /* index is 0+, usize is 1+ */
    index++;
    if (index > IDIO_ARRAY_USIZE (a)) {
	IDIO_ARRAY_USIZE (a) = index;
    }

    IDIO_C_ASSERT (IDIO_ARRAY_USIZE (a) <= IDIO_ARRAY_ASIZE (a));
}

void idio_array_push (IDIO f, IDIO a, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (array, a);

    idio_array_insert_index (f, a, o, IDIO_ARRAY_USIZE (a));
}

IDIO idio_array_pop (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }

    IDIO e = idio_array_get_index (f, a, IDIO_ARRAY_USIZE (a) - 1);
    idio_array_delete_index (f, a, IDIO_ARRAY_USIZE (a) - 1);
    IDIO_ARRAY_USIZE (a)--;

    IDIO_ASSERT (e);
    IDIO_ASSERT_NOT_FREED (e);
    
    return e;
}

IDIO idio_array_shift (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }

    IDIO e = idio_array_get_index (f, a, 0);
    size_t i;
    for (i = 0 ; i < IDIO_ARRAY_USIZE (a) - 1; i++) {
	IDIO e = idio_array_get_index (f, a, i + 1);
	IDIO_ASSERT (e);
	
	idio_array_insert_index (f, a, e, i);
    }

    idio_array_pop (f, a);

    return e;
}

void idio_array_unshift (IDIO f, IDIO a, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (array, a);

    size_t i;
    if (IDIO_ARRAY_USIZE (a) > 0) {
	for (i = IDIO_ARRAY_USIZE (a); i > 0; i--) {
	    IDIO e = idio_array_get_index (f, a, i - 1);
	    IDIO_ASSERT (e);
	    
	    idio_array_insert_index (f, a, e, i);
	}
    }
    idio_array_insert_index (f, a, o, 0);

    IDIO_C_ASSERT (IDIO_ARRAY_USIZE (a) <= IDIO_ARRAY_ASIZE (a));
}

IDIO idio_array_head (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }
    
    return idio_array_get_index (f, a, 0);
}

IDIO idio_array_top (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }
    
    return idio_array_get_index (f, a, IDIO_ARRAY_USIZE (a) - 1);
}

IDIO idio_array_get_index (IDIO f, IDIO a, idio_index_t index)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    fprintf (stderr, "idio_array_get_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	    IDIO_C_EXIT (1);
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_get_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    return IDIO_ARRAY_AE (a, index);
}

idio_index_t idio_array_find_free_index (IDIO f, IDIO a, idio_index_t index)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);
    
    if (index < 0) {
	fprintf (stderr, "idio_array_find_free_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_find_free_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    for (; index < IDIO_ARRAY_USIZE (a); index++) {
	if (idio_S_nil == IDIO_ARRAY_AE (a, index)) {
	    return index;
	}
    }

    return -1;
}

idio_index_t idio_array_find_eqp (IDIO f, IDIO a, IDIO e, idio_index_t index)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);
    
    if (index < 0) {
	fprintf (stderr, "idio_array_find_eqp: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_find_eqp: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    for (; index < IDIO_ARRAY_USIZE (a); index++) {
	if (idio_eqp (f, IDIO_ARRAY_AE (a, index), e)) {
	    return index;
	}
    }

    return -1;
}

void idio_array_bind (IDIO f, IDIO a, size_t nargs, ...)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < nargs) {
	char *os = idio_display_string (f, a);
	fprintf (stderr, "idio_array_bind: o=%s\n", os);
	free (os);
	idio_expr_dump (f, a);
	IDIO_C_ASSERT (0);
    }

    va_list ap;
    va_start (ap, nargs);

    size_t i;
    for (i = 0; i < nargs; i++) {
	IDIO *arg = va_arg (ap, IDIO *);
	*arg = idio_array_get_index (f, a, i);
	IDIO_ASSERT (*arg);
    }
    
    va_end (ap);
}

IDIO idio_array_copy (IDIO f, IDIO a, size_t extra)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    size_t osz = IDIO_ARRAY_USIZE (a);
    size_t nsz = osz + extra;
    IDIO na = idio_array (f, nsz);

    size_t i;
    for (i = 0; i < osz; i++) {
	idio_array_insert_index (f,
				 na,
				 idio_array_get_index (f, a, i),
				 i);
    }

    return na;
}

IDIO idio_array_to_list (IDIO f, IDIO a)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    size_t al = IDIO_ARRAY_USIZE (a);
    size_t ai;
    
    IDIO r = idio_S_nil;

    for (ai = al -1; ai >= 0; ai--) {
	r = idio_pair (f,
		       idio_array_get_index (f, a, ai),
		       r);
    }

    return r;
}

int idio_array_delete_index (IDIO f, IDIO a, idio_index_t index)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    fprintf (stderr, "idio_array_delete_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	    IDIO_C_EXIT (1);
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_delete_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    IDIO_ARRAY_AE (a, index) = idio_S_nil;

    return 1;
}

