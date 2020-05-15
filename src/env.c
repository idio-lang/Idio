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
 * env.c
 *
 */

#include "idio.h"

/* MacOS & Solaris doesn't have environ in unistd.h */
extern char **environ;

char *idio_env_PATH_default = "/bin:/usr/bin";
char *idio_env_IDIOLIB_default = NULL;
IDIO idio_env_IDIOLIB_sym;
IDIO idio_env_PATH_sym;
IDIO idio_env_PWD_sym;

static IDIO idio_env_default_lo;

static int idio_env_set_default (IDIO name, char *val)
{
    IDIO_ASSERT (name);
    IDIO_C_ASSERT (val);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO ENV = idio_module_env_symbol_value (name, IDIO_LIST1 (idio_S_false));
    if (idio_S_false == ENV) {
	idio_environ_extend (idio_env_default_lo, name, idio_string_C (val), idio_vm_constants);
	return 1;
    }

    return 0;
}

static void idio_env_add_environ ()
{
    char **env;
    for (env = environ; *env ; env++) {
	char *e = strchr (*env, '=');

	IDIO var;
	IDIO val = idio_S_undef;

	if (NULL != e) {
	    char *name = idio_alloc (e - *env + 1);
	    strncpy (name, *env, e - *env);
	    name[e - *env] = '\0';
	    var = idio_symbols_C_intern (name);
	    free (name);

	    val = idio_string_C (e + 1);
	} else {
	    var = idio_string_C (*env);
	}

	idio_environ_extend (idio_env_default_lo, var, val, idio_vm_constants);
    }

    /*
     * Hmm.  What if we have a "difficult" environment?  A particular
     * example is if we don't have a PATH.  We use the PATH internally
     * at the very least and not having one is an issue.
     *
     * So we'll test for some environment variables we need and set a
     * default if there isn't one.
     */

    idio_env_set_default (idio_env_PATH_sym, idio_env_PATH_default);

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());
    }

    if (idio_env_set_default (idio_env_PWD_sym, cwd) == 0) {
	/*
	 * On Mac OS X (Mavericks):
	 *
	 * (lldb) process launch -t -w X -- ...
	 *
	 * may well change the working directory to X but it doesn't
	 * change the environment variable PWD.  There must be any
	 * number of other situations where a process changes the
	 * working directory but doesn't change the environment
	 * variable -- no reason why it should, of course.
	 *
	 * For testing we can use:
	 *
	 * env PWD=/ .../idio
	 *
	 * So, if we didn't create a new variable in
	 * idio_env_set_default() then set the value regardless now.
	 */
	idio_module_env_set_symbol_value (idio_env_PWD_sym, idio_string_C (cwd));
    }

    free (cwd);
}

/*
 * We want to generate a nominal IDIOLIB based on the path to the
 * running executable.  If argv0 is simply "idio" then we need to
 * discover where on the PATH it was found, otherwise we can normalize
 * argv0 with realpath(3).
 *
 * NB. We are called after idio_env_add_environ() as main() has to
 * pass us argv0.  main() could have passed argv0 to idio_init() then
 * to idio_env_add_primitives() then to idio_env_add_environ() and
 * then here to us.  Or it could just call us separately.  Which it
 * does.
 */
void idio_env_init_idiolib (char *argv0)
{
    IDIO_C_ASSERT (argv0);

    char *dir = rindex (argv0, '/');

    if (NULL == dir) {
	argv0 = idio_command_find_exe_C (argv0);
    }

    char resolved_path[PATH_MAX];
    char *path = realpath (argv0, resolved_path);

    if (NULL == path) {
	idio_error_system_errno ("realpath(3) => NULL", idio_S_nil, IDIO_C_FUNC_LOCATION ());
    }

    dir = rindex (path, '/');

    if (dir != path) {
	char *pdir = dir - 1;
	while (pdir > path &&
	       '/' != *pdir) {
	    pdir--;
	}

	if (pdir >= path) {
	    if (strncmp (pdir, "/bin", 4) == 0) {
		/* ... + /lib */
		size_t ieId_len = pdir - path + 4;
		idio_env_IDIOLIB_default = idio_alloc (ieId_len + 1);
		strncpy (idio_env_IDIOLIB_default, path, pdir - path);
		idio_env_IDIOLIB_default[pdir - path] = '\0';
		strcat (idio_env_IDIOLIB_default, "/lib");

		if (! idio_env_set_default (idio_env_IDIOLIB_sym, idio_env_IDIOLIB_default)) {
		    IDIO idiolib = idio_module_env_symbol_value (idio_env_IDIOLIB_sym, IDIO_LIST1 (idio_S_false));
		    char *index = strstr (IDIO_STRING_S (idiolib), idio_env_IDIOLIB_default);
		    int append = 0;
		    if (index) {
			if (! ('\0' == index[ieId_len + 1] ||
			       ':' == index[ieId_len + 1])) {
			    append = 1;
			}
		    } else {
			append = 1;
		    }

		    if (append) {
			size_t idiolib_len = strlen (IDIO_STRING_S (idiolib));
			size_t ni_len = idiolib_len + 1 + ieId_len + 1;
			if (0 == idiolib_len) {
			    ni_len = ieId_len + 1;
			}
			char *ni = idio_alloc (ni_len + 1);
			ni[0] = '\0';
			if (idiolib_len) {
			    strcpy (ni, IDIO_STRING_S (idiolib));
			    strcat (ni, ":");
			}
			strcat (ni, idio_env_IDIOLIB_default);
			ni[ni_len] = '\0';
			idio_module_env_set_symbol_value (idio_env_IDIOLIB_sym, idio_string_C (ni));
			free (ni);
		    }
		}
	    }
	}
    }
}

void idio_init_env ()
{
    idio_env_default_lo = idio_struct_instance (idio_lexobj_type,
						idio_pair (idio_string_C ("*env default*"),
						idio_pair (idio_integer (0),
						idio_pair (idio_integer (0),
						idio_pair (idio_S_unspec,
						idio_S_nil)))));
    idio_gc_protect (idio_env_default_lo);

    idio_env_IDIOLIB_sym = idio_symbols_C_intern ("IDIOLIB");
    idio_env_PATH_sym = idio_symbols_C_intern ("PATH");
    idio_env_PWD_sym = idio_symbols_C_intern ("PWD");
}

void idio_env_add_primitives ()
{
    idio_env_add_environ ();
}

void idio_final_env ()
{
    idio_gc_expose (idio_env_default_lo);
    free (idio_env_IDIOLIB_default);
}
