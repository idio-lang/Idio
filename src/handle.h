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
 * handle.h
 * 
 */

#ifndef HANDLE_H
#define HANDLE_H

void idio_init_handle ();
void idio_final_handle ();
IDIO idio_handle ();
int idio_isa_handle (IDIO d);
void idio_free_handle (IDIO d);
void idio_handle_finalizer (IDIO handle);

IDIO idio_close_input_handle (IDIO h);
IDIO idio_close_output_handle (IDIO h);
IDIO idio_current_input_handle ();
IDIO idio_current_output_handle ();

IDIO idio_open_handle (IDIO pathname, char *mode);
IDIO idio_read (IDIO h);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
