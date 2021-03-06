/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * vars.c
 *
 */

#include "idio.h"

char *idio_vars_IFS_default = " \t\n";
IDIO idio_vars_IFS_sym;

static int idio_vars_set_default (IDIO name, char *val)
{
    IDIO_ASSERT (name);
    IDIO_C_ASSERT (val);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO VARS = idio_module_current_symbol_value_recurse (name, IDIO_LIST1 (idio_S_false));
    if (idio_S_false == VARS) {
	idio_dynamic_extend (name, name, idio_string_C (val), idio_vm_constants);
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE0_DS ("SECONDS/get", SECONDS_get, (void), "", "\
Return the VM's elapsed running time in seconds	\n\
						\n\
Normally accessed as the variable SECONDS	\n\
						\n\
:return: elapsed VM running time		\n\
:rtype: integer					\n\
")
{
    return idio_integer (idio_vm_elapsed ());
}

void idio_vars_add_primitives ()
{
    IDIO geti;
    geti = IDIO_ADD_PRIMITIVE (SECONDS_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("SECONDS"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_Idio_module);

    idio_vars_set_default (idio_vars_IFS_sym, idio_vars_IFS_default);
}

void idio_final_vars ()
{
}

void idio_init_vars ()
{
    idio_module_table_register (idio_vars_add_primitives, idio_final_vars);

    idio_vars_IFS_sym = idio_symbols_C_intern ("IFS");
}

