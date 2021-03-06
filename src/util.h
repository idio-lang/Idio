/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * util.h
 *
 */

#ifndef UTIL_H
#define UTIL_H

#define IDIO_EQUAL_EQP		1
#define IDIO_EQUAL_EQVP		2
#define IDIO_EQUAL_EQUALP	3

#define IDIO_COPY_DEEP		1
#define IDIO_COPY_SHALLOW	2

extern IDIO idio_util_value_as_string;
extern IDIO idio_print_conversion_format_sym;
extern IDIO idio_print_conversion_precision_sym;

int idio_type (IDIO o);
const char *idio_type2string (IDIO o);
const char *idio_type_enum2string (idio_type_e type);
int idio_isa_fixnum (IDIO o);
int idio_isa_boolean (IDIO o);
int idio_eqp (void *o1, void *o2);
int idio_eqvp (void *o1, void *o2);
int idio_equalp (void *o1, void *o2);
int idio_equal (IDIO o1, IDIO o2, int eqp);
IDIO idio_value (IDIO o);
char *idio_as_string (IDIO o, size_t *sizep, int depth, IDIO seen, int first);
char *idio_as_string_safe (IDIO o, size_t *sizep, int depth, int first);
char *idio_display_string (IDIO o, size_t *sizep);
const char *idio_vm_bytecode2string (int code);
void idio_as_flat_string (IDIO o, char **argv, int *i);
IDIO idio_copy (IDIO o, int depth);
void idio_dump (IDIO o, int detail);
void idio_debug_FILE (FILE *file, const char *fmt, IDIO o);
void idio_debug (const char *fmt, IDIO o);
IDIO idio_add_feature (IDIO f);
IDIO idio_add_feature_ps (char *p, char *s);
IDIO idio_add_feature_pi (char *p, size_t size);

#if ! defined (strnlen)
size_t strnlen (const char *s, size_t maxlen);
#endif

void idio_init_util ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
