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
 * module.h
 *
 */

#ifndef MODULE_H
#define MODULE_H

extern IDIO idio_primitives_module;
extern IDIO idio_Idio_module;

void idio_error_module_unbound_name (IDIO symbol, IDIO module);
IDIO idio_module (IDIO name);
int idio_isa_module (IDIO o);
void idio_free_module (IDIO fo);
IDIO idio_module_get_vci (IDIO module, IDIO mci);
IDIO idio_module_set_vci (IDIO module, IDIO mci, IDIO gci);
IDIO idio_module_get_or_set_vci (IDIO module, IDIO mci);
IDIO idio_module_get_vvi (IDIO module, IDIO msi);
IDIO idio_module_set_vvi (IDIO module, IDIO msi, IDIO gvi);
IDIO idio_module_find_module (IDIO name);
IDIO idio_module_find_or_create_module (IDIO name);
IDIO idio_Idio_module_instance ();
IDIO idio_primitives_module_instance ();
IDIO idio_thread_current_module ();
IDIO idio_thread_current_env ();
IDIO idio_module_symbols (IDIO module);
IDIO idio_module_visible_symbols (IDIO module, IDIO type);
IDIO idio_module_direct_reference (IDIO name);
IDIO idio_module_find_symbol_recurse (IDIO symbol, IDIO module, int recurse);
IDIO idio_module_env_symbol_recurse (IDIO symbol);
IDIO idio_module_find_symbol (IDIO symbol, IDIO module);
IDIO idio_module_symbol_value (IDIO symbol, IDIO module, IDIO args);
IDIO idio_module_env_symbol_value (IDIO symbol, IDIO args);
IDIO idio_module_symbol_value_recurse (IDIO symbol, IDIO module, IDIO args);
IDIO idio_module_current_symbol_value_recurse (IDIO symbol, IDIO args);
IDIO idio_module_env_symbol_value_recurse (IDIO symbol, IDIO args);
IDIO idio_module_set_symbol (IDIO symbol, IDIO value, IDIO module);
IDIO idio_module_set_symbol_value (IDIO symbol, IDIO value, IDIO module);
IDIO idio_module_export_symbol_value (IDIO symbol, IDIO value, IDIO module);
IDIO idio_module_primitive_set_symbol_value (IDIO symbol, IDIO value);
IDIO idio_module_current_set_symbol_value (IDIO symbol, IDIO value);
IDIO idio_module_env_set_symbol_value (IDIO symbol, IDIO value);
IDIO idio_module_toplevel_set_symbol_value (IDIO symbol, IDIO value);
IDIO idio_module_add_computed_symbol (IDIO symbol, IDIO get, IDIO set, IDIO module);
IDIO idio_module_export_computed_symbol (IDIO symbol, IDIO get, IDIO set, IDIO module);

void idio_init_module (void);

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
