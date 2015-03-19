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
 * idio.c
 * 
 */

#include "idio.h"

void idio_init ()
{
    /* GC first then symbol for the symbol table! */
    idio_init_gc ();
    idio_init_symbol ();
    
    idio_init_handle ();
    idio_init_file_handle ();
    idio_init_C_struct ();
    idio_init_frame ();
}

void idio_final ()
{
    /*
     * reverse order of idio_init () -- if there is an idio_final_X ()
     */
    idio_final_frame ();
    idio_final_C_struct ();
    idio_final_file_handle ();
    idio_final_handle ();
    idio_final_symbol ();
    idio_final_gc ();
}

int main (int argc, char **argv, char **envp)
{
    /*
      FILE *fout = stdout;
    */

    idio_init ();

    IDIO fh = idio_open_file_handle_C ("bootstrap.idio", "r");

    idio_load_file (fh);

    /*
      IDIO inp = idio_array_get_index (IDIO_FRAME_GC (idio_G_frame)->input_port, 0);
    */
    
    idio_final ();

    return 0;
}
