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
 * c-ffi.h
 *
 */

#ifndef C_FFI_H
#define C_FFI_H

IDIO idio_C_FFI (IDIO symbol, IDIO arg_types, IDIO result_type);
void idio_free_C_FFI (IDIO o);


#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
