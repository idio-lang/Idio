/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * string.h
 *
 */

#ifndef STRING_H
#define STRING_H

#include "idio.h"

#define IDIO_STRING_TOKEN_FLAG_NONE		0
#define IDIO_STRING_TOKEN_FLAG_EXACT		(1<<0)

#define IDIO_STRING_TOKEN_EXACT(f)		((f) & IDIO_STRING_TOKEN_FLAG_EXACT)
#define IDIO_STRING_TOKEN_INEXACT(f)		(IDIO_STRING_TOKEN_EXACT (f) == 0)

int idio_assign_string_C (IDIO so, const char *s_C);
IDIO idio_string_C (const char *s_C);
IDIO idio_string_C_len (const char *s_C, size_t blen);
IDIO idio_string_copy (IDIO s);
IDIO idio_string_C_array (size_t ns, char *a_C[]);
void idio_free_string (IDIO so);
int idio_isa_string (IDIO so);
IDIO idio_substring_offset (IDIO so, size_t offset, size_t blen);
int idio_isa_substring (IDIO so);
void idio_free_substring (IDIO so);
size_t idio_string_blen (IDIO so);
char *idio_string_s (IDIO so);
char *idio_string_as_C (IDIO so);
IDIO idio_string_ref (IDIO s, IDIO index);
IDIO idio_string_set (IDIO s, IDIO index, IDIO c);

void idio_init_string ();
void idio_string_add_primitives ();
void idio_final_string ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
