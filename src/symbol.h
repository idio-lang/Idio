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
 * symbol.h
 * 
 */

#ifndef SYMBOL_H
#define SYMBOL_H

void idio_init_symbol (void);
void idio_final_symbol (void);
IDIO idio_symbol_C (const char *s_C);
IDIO idio_tag_C (const char *s_C);
void idio_free_symbol (IDIO s);
int idio_isa_symbol (IDIO s);
IDIO idio_symbols_C_intern (char *s);
IDIO idio_symbols_string_intern (IDIO str);


#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
