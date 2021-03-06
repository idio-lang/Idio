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
 * file-handle.c
 *
 * This code sits a-top C's standard IO functions which isn't the
 * smartest move.
 *
 * In practice, it means that we, Idio, maintain buffers and state in
 * front of C's standard IO buffers and state.
 *
 * This really needs to be reworked to use read(2)/write(2) direct.
 */

#include "idio.h"

static IDIO idio_file_handles = idio_S_nil;
static IDIO idio_stdin = idio_S_nil;
static IDIO idio_stdout = idio_S_nil;
static IDIO idio_stderr = idio_S_nil;

static idio_handle_methods_t idio_file_handle_file_methods = {
    idio_free_file_handle,
    idio_readyp_file_handle,
    idio_getb_file_handle,
    idio_eofp_file_handle,
    idio_close_file_handle,
    idio_putb_file_handle,
    idio_putc_file_handle,
    idio_puts_file_handle,
    idio_flush_file_handle,
    idio_seek_file_handle,
    idio_print_file_handle
};

static idio_handle_methods_t idio_file_handle_pipe_methods = {
    idio_free_file_handle,
    idio_readyp_file_handle,
    idio_getb_file_handle,
    idio_eofp_file_handle,
    idio_close_file_handle,
    idio_putb_file_handle,
    idio_putc_file_handle,
    idio_puts_file_handle,
    idio_flush_file_handle,
    NULL,
    idio_print_file_handle
};

static void idio_file_handle_filename_system_error (char *circumstance, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (": ", msh);
    idio_display_C (strerror (errno), msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_filename_delete_error (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("remove: ", msh);
    idio_display_C (strerror (errno), msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

/*
 * Most of the calls to this function are from our own pre-checks that
 * would otherwise have generated an ENAMETOOLONG eror (somewhere) had
 * the code continued.
 */
static void idio_file_handle_malformed_filename_error (char *msg, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("malformed filename: ", msh);
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_malformed_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_file_protection_error (char *circumstance, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (": ", msh);
    idio_display_C (strerror (errno), msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_file_protection_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_filename_already_exists_error (char *circumstance, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (": ", msh);
    idio_display_C (strerror (errno), msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_file_already_exists_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_filename_not_found_error (char *circumstance, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (": ", msh);
    idio_display_C (strerror (errno), msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_no_such_file_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_file_handle_format_error (char *circumstance, char *kind, char *msg, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_C_ASSERT (kind);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (" ", msh);
    idio_display_C (kind, msh);
    idio_display_C (" ", msh);
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_no_such_file_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_file_handle_filename_format_error (char *circumstance, char *msg, IDIO filename, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (" filename ", msh);
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_no_such_file_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_file_handle_mode_format_error (char *circumstance, char *msg, IDIO mode, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (mode);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, mode);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (" mode ", msh);
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_no_such_file_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       mode));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

char *idio_file_handle_string_C (IDIO val, char *op_C, char *kind, int *free_me_p, IDIO c_location)
{
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (op_C);
    IDIO_C_ASSERT (kind);
    IDIO_C_ASSERT (free_me_p);

    *free_me_p = 0;

    if (idio_isa_symbol (val)) {
	return IDIO_SYMBOL_S (val);
    } else if (idio_isa_string (val)) {
	size_t size = 0;
	char *val_C = idio_string_as_C (val, &size);
	size_t C_size = strlen (val_C);
	if (C_size != size) {
	    IDIO_GC_FREE (val_C);

	    idio_file_handle_format_error (op_C, kind, "contains an ASCII NUL", val, c_location);

	    /* notreached */
	    return NULL;
	}
	*free_me_p = 1;

	return val_C;
    } else {
	/*
	 * Code coverage: coding error
	 */
	idio_error_param_type ("symbol|string", val, c_location);

	/* notreached */
	return NULL;
    }
}

char *idio_file_handle_filename_string_C (IDIO val, char *op_C, int *free_me_p, IDIO c_location)
{
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (op_C);
    IDIO_C_ASSERT (free_me_p);

    return idio_file_handle_string_C (val, op_C, "filename", free_me_p, c_location);
}

char *idio_file_handle_mode_string_C (IDIO val, char *op_C, int *free_me_p, IDIO c_location)
{
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (op_C);
    IDIO_C_ASSERT (free_me_p);

    return idio_file_handle_string_C (val, op_C, "mode", free_me_p, c_location);
}

static IDIO idio_open_file_handle (IDIO filename, char *pathname, int fd, int h_type, int h_flags, int s_flags)
{
    IDIO_ASSERT (filename);
    IDIO_C_ASSERT (pathname);

    IDIO_TYPE_ASSERT (string, filename);

    idio_file_handle_stream_t *fhsp = idio_alloc (sizeof (idio_file_handle_stream_t));
    int bufsiz = BUFSIZ;

    if (isatty (fd)) {
	s_flags |= IDIO_FILE_HANDLE_FLAG_INTERACTIVE;
    }

    IDIO_FILE_HANDLE_STREAM_FD (fhsp) = fd;
    IDIO_FILE_HANDLE_STREAM_FLAGS (fhsp) = s_flags;
    IDIO_FILE_HANDLE_STREAM_BUF (fhsp) = idio_alloc (bufsiz);
    IDIO_FILE_HANDLE_STREAM_BUFSIZ (fhsp) = bufsiz;
    IDIO_FILE_HANDLE_STREAM_PTR (fhsp) = IDIO_FILE_HANDLE_STREAM_BUF (fhsp);
    IDIO_FILE_HANDLE_STREAM_COUNT (fhsp) = 0;

    IDIO fh = idio_handle ();

    IDIO_HANDLE_FLAGS (fh) |= h_type | h_flags;
    IDIO_HANDLE_FILENAME (fh) = filename;
    IDIO_HANDLE_PATHNAME (fh) = idio_string_C (pathname);
    IDIO_HANDLE_STREAM (fh) = fhsp;
    switch (h_type) {
    case IDIO_HANDLE_FLAG_FILE:
	IDIO_HANDLE_METHODS (fh) = &idio_file_handle_file_methods;
	break;
    case IDIO_HANDLE_FLAG_PIPE:
	IDIO_HANDLE_METHODS (fh) = &idio_file_handle_pipe_methods;
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    char em[BUFSIZ];
	    sprintf (em, "unexpected handle type %#x", h_type);
	    idio_error_C (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    }

    if ((s_flags & IDIO_FILE_HANDLE_FLAG_STDIO) == 0) {
	idio_gc_register_finalizer (fh, idio_file_handle_finalizer);
    }

    return fh;
}

/*
 * Must be:
 *
 * 1. a string of at least one character
 *
 * 2. start with r, w or a
 *
 * 3. be of a limited set of subsequent mode characters
 *
 * Can we be fooled by an extremely long mode string?  The glibc
 * fopen(3) implementation deliberately limits the mode string to 5
 * (or 6 or 7) characters.
 */
static int idio_file_handle_validate_mode_flags (char *mode_str, int *sflagsp, int *flagsp)
{
    if (strnlen (mode_str, 1) < 1) {
	return -1;
    }

    switch (mode_str[0]) {
    case 'r':
	*flagsp = O_RDONLY;
	break;
    case 'w':
	*flagsp = O_WRONLY | O_CREAT | O_TRUNC;
	break;
    case 'a':
	*flagsp = O_WRONLY | O_CREAT | O_APPEND;
	break;
    default:
	return -1;
    }

    for (size_t i = 1; i < strlen (mode_str); i++) {
	switch (mode_str[i]) {
	case 'b':
	    /*
	     * ISO C89 compatibility - ignored for POSIX conforming systems
	     */
	    break;
	case '+':
	    /*
	     * Careful, other flags could have been applied by now:
	     * "re+"
	     */
	    *flagsp = (*flagsp & ~ O_ACCMODE) | O_RDWR;
	    break;
	case 'e':
	    *flagsp |= O_CLOEXEC;
	    *sflagsp |= IDIO_FILE_HANDLE_FLAG_CLOEXEC;
	    break;
	case 'x':
	    *flagsp |= O_EXCL;
	    break;
	default:
	    return -1;
	}
    }

    return 0;
}

static IDIO idio_file_handle_open_from_fd (IDIO ifd, IDIO args, int h_type, char *func, char *def_mode_str, int def_mode, int plus_mode)
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);

    /*
     * Test Cases:
     *
     *   file-handle-errors/open-file-from-fd-bad-fd-type.idio
     *   file-handle-errors/open-input-file-from-fd-bad-fd-type.idio
     *   file-handle-errors/open-output-file-from-fd-bad-fd-type.idio
     *
     * open-file-from-fd #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, ifd);
    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    int fd = IDIO_C_TYPE_int (ifd);

    char fd_name[PATH_MAX];
    sprintf (fd_name, "/dev/fd/%d", fd);

    if (idio_S_nil != args) {
	IDIO name = IDIO_PAIR_H (args);
	if (idio_isa_string (name)) {
	    int free_name_C = 0;

	    /*
	     * Test Cases:
	     *
	     *   file-handle-errors/open-file-from-fd-filename-format.idio
	     *   file-handle-errors/open-input-file-from-fd-filename-format.idio
	     *   file-handle-errors/open-output-file-from-fd-filename-format.idio
	     *
	     * open-file-from-fd (stdin-fileno) (join-string (make-string 1 #U+0) '("hello" "world"))
	     */
	    char *name_C = idio_file_handle_filename_string_C (name, func, &free_name_C, IDIO_C_FUNC_LOCATION ());

	    if (strlen (name_C) >= PATH_MAX) {
		/*
		 * Test Cases:
		 *
		 *   file-handle-errors/open-file-from-fd-filename-PATH_MAX.idio
		 *   file-handle-errors/open-input-file-from-fd-filename-PATH_MAX.idio
		 *   file-handle-errors/open-output-file-from-fd-filename-PATH_MAX.idio
		 *
		 * open-file-from-fd (stdin-fileno) (make-string (C/->integer PATH_MAX) #\A)
		 */
		if (free_name_C) {
		    IDIO_GC_FREE (name_C);
		}

		idio_file_handle_malformed_filename_error ("name too long", name, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    sprintf (fd_name, "%s", name_C);

	    if (free_name_C) {
		IDIO_GC_FREE (name_C);
	    }

	    args = IDIO_PAIR_T (args);
	} else {
	    /*
	     * Test Cases:
	     *
	     *   file-handle-errors/open-file-from-fd-filename-type.idio
	     *   file-handle-errors/open-input-file-from-fd-filename-type.idio
	     *   file-handle-errors/open-output-file-from-fd-filename-type.idio
	     *
	     * open-file-from-fd (stdin-fileno) #t
	     */
	    idio_error_param_type ("string", name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    int free_mode_C = 0;
    char *mode_C = def_mode_str;

    if (idio_S_nil != args) {
	IDIO mode = IDIO_PAIR_H (args);
	if (idio_isa_string (mode)) {
	    /*
	     * Test Cases:
	     *
	     *   file-handle-errors/open-file-from-fd-mode-format.idio
	     *   file-handle-errors/open-input-file-from-fd-mode-format.idio
	     *   file-handle-errors/open-output-file-from-fd-mode-format.idio
	     *
	     * open-file-from-fd (stdin-fileno) "bob" (join-string (make-string 1 #U+0) '("r" "w"))
	     */
	    mode_C = idio_file_handle_mode_string_C (mode, func, &free_mode_C, IDIO_C_FUNC_LOCATION ());
	    args = IDIO_PAIR_T (args);
	} else {
	    /*
	     * Test Cases:
	     *
	     *   file-handle-errors/open-file-from-fd-mode-type.idio
	     *   file-handle-errors/open-input-file-from-fd-mode-type.idio
	     *   file-handle-errors/open-output-file-from-fd-mode-type.idio
	     *
	     * open-file-from-fd (stdin-fileno) "bob" #t
	     */
	    idio_error_param_type ("string", mode, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    /*
     * This is the moral equivalent of fdopen(3) and we've been passed
     * some mode flags in a string.
     *
     * That said, there's not much we can do if someone specifies a
     * mode that is not true of the fd itself as the fd is already
     * open.
     *
     * We can validate they've passed recognised mode letters and we
     * will call fcntl(2) with the resultant flags but all we're
     * really interested in is setting the handle's read/write flags.
     *
     * We can query the current access mode and validate:
     *
     *   open-file-from-fd (stdin-fileno) "bob" "w"
     *
     * for consistency.  fcntl() will ignore use if we try to change
     * access modes anyway.
     *
     * The only useful file status flag that can be changed is
     * O_APPEND (as the mode string doesn't support the other mutable
     * flags) and the only file descriptor flag we can change is
     * O_CLOEXEC.
     */
    int s_flags = IDIO_FILE_HANDLE_FLAG_NONE;
    int req_flags = 0;
    if (-1 == idio_file_handle_validate_mode_flags (mode_C, &s_flags, &req_flags)) {
	/*
	 * Test Cases:
	 *
	 *   file-handle-errors/open-file-from-fd-mode-letter-invalid-[12].idio
	 *   file-handle-errors/open-input-file-from-fd-mode-letter-invalid-[12].idio
	 *   file-handle-errors/open-output-file-from-fd-mode-letter-invalid-[12].idio
	 *
	 * open-file-from-fd (stdin-fileno) "bob" "rr"
	 * open-file-from-fd (stdin-fileno) "bob" "rq"
	 *
	 * NB r, w and a can only appear as the first letter and q
	 * isn't a valid mode character.
	 */
	IDIO imode = idio_string_C (mode_C);
	if (free_mode_C) {
	    IDIO_GC_FREE (mode_C);
	}

	idio_file_handle_mode_format_error (func, "invalid", imode, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * File status flags include file access mode.
     */
    int fs_flags = fcntl (fd, F_GETFL);

    if (-1 == fs_flags) {
	/*
	 * Test Case: ??
	 */
	fprintf (stderr, "[%d]fcntl %d (%s) => %d\n", getpid (), fd, idio_type2string (ifd), errno);
	idio_error_system_errno_msg ("fcntl", "F_GETFL", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * What is mode inconsistency anyway?
     */
    int inconsistent = 0;
    int fs_flags_mode = fs_flags & O_ACCMODE;
    int req_flags_mode = req_flags & O_ACCMODE;
    switch (fs_flags_mode) {
    case O_RDONLY:
	switch (req_flags_mode) {
	case O_WRONLY:
	    inconsistent = 1;
	    break;
	}
	break;
    case O_WRONLY:
	switch (req_flags_mode) {
	case O_RDONLY:
	    inconsistent = 1;
	    break;
	}
	break;
    }

    if (inconsistent) {
	/*
	 * Test Cases:
	 *
	 *   file-handle-errors/open-file-from-fd-mode-letter-inconsistent.idio
	 *   file-handle-errors/open-input-file-from-fd-mode-letter-inconsistent.idio
	 *   file-handle-errors/open-output-file-from-fd-mode-letter-inconsistent.idio
	 *
	 * rm testfile
	 * touch testfile
	 * fh := open-input-file testfile
	 * fd = libc/dup (file-handle-fd fh)
	 * open-input-file-from-fd fd "bob" "w"
	 *
	 * XXX be careful of the mode the file is in initially,
	 * without the rm and touch I was getting mode O_RDWR back
	 * from fctnl (F_GETFL) -- even though the file was opened as
	 * "r".
	 *
	 * Also note that {fd} remains open so it is up to you to
	 * ensure it is closed.
	 */
	IDIO imode = idio_string_C (mode_C);
	if (free_mode_C) {
	    IDIO_GC_FREE (mode_C);
	}

	idio_file_handle_mode_format_error (func, "inconsistent", imode, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * fcntl (F_SETFL) is only usefully going to affect O_APPEND as
     * the other fcntl() mutable file status flags are not mode string
     * options.
     *
     * In particular, file access and creation modes are ignored.
     */
    int r = fcntl (fd, F_SETFL, req_flags);

    if (-1 == r) {
	/*
	 * Test Case: ??
	 */
	idio_error_system_errno_msg ("fcntl", "F_SETFL", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * We're wrappering a file handle around an existing file
     * descriptor.
     *
     * File descriptor flags fcntl(F_GETFD) include (are only?)
     * O_CLOEXEC.
     *
     * Most people won't bother with it and assume the file will be
     * closed on exec.  However, if they pass "r", rather than "re",
     * then a na�ve implementation might remove O_CLOEXEC from a file
     * descriptor that was opened with it.
     *
     * When users are opening files, it should be their call but what
     * do we do with an existing fd?
     *
     * In the short term...do what they ask and don't try to second
     * guess their motives.
     *
     * TBD
     */
#ifdef IDIO_FILE_DESCRIPTOR_FLAGS
    int fd_flags = fcntl (fd, F_GETFD);

    if (-1 == fd_flags) {
	/*
	 * Test Case: ??
	 */
	idio_error_system_errno_msg ("fcntl", "F_GETFD", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if ((fd_flags & O_CLOEXEC) != (req_flags & O_CLOEXEC)) {
	fprintf (stderr, "fcntl (%d, F_GETFD) => %#x: wants %s\n", fd, fd_flags, mode_C);
	/*
	 * Test Cases: ??
	 */
	IDIO imode = idio_string_C (mode_C);
	if (free_mode_C) {
	    IDIO_GC_FREE (mode_C);
	}

	idio_file_handle_mode_format_error (func, "flags inconsistent", imode, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
#endif

    /*
     * fcntl (F_SETFD) is only going to affect O_CLOEXEC (and any
     * other file descriptor flags).
     */
    r = fcntl (fd, F_SETFD, req_flags);

    if (-1 == r) {
	/*
	 * Test Case: ??
	 */

	if (EINVAL == errno &&
	    idio_vm_virtualisation_WSL) {
	    perror ("fcntl F_SETFD");
	} else {
	    idio_error_system_errno_msg ("fcntl", "F_SETFD", ifd, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    int hflags = def_mode;
    if (strchr (mode_C, '+') != NULL) {
	hflags |= plus_mode;
    }

    if (free_mode_C) {
	IDIO_GC_FREE (mode_C);
    }

    return idio_open_file_handle (idio_string_C (fd_name), fd_name, fd, h_type, hflags, IDIO_FILE_HANDLE_FLAG_NONE);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("open-file-from-fd", open_file_handle_from_fd, (IDIO ifd, IDIO args), "fd [name [mode]]", "\
construct an input file handle from `fd` using the optional	\n\
`name` instead of the default `/dev/fd/{fd}` and	\n\
the optional mode `mode` instead of ``re``		\n\
							\n\
:param fd: file descriptor				\n\
:type fd: C/int						\n\
:param name: (optional) file name for display		\n\
:type fd: string					\n\
:param name: (optional) file mode for opening		\n\
:type fd: string					\n\
							\n\
:return: file handle					\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);

    return idio_file_handle_open_from_fd (ifd, args, IDIO_HANDLE_FLAG_FILE, "open-file-from-fd", "re", IDIO_HANDLE_FLAG_READ, IDIO_HANDLE_FLAG_WRITE);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("open-input-file-from-fd", open_input_file_handle_from_fd, (IDIO ifd, IDIO args), "fd [name [mode]]", "\
construct an input file handle from `fd` using the optional	\n\
`name` instead of the default `/dev/fd/{fd}` and	\n\
the optional mode `mode` instead of ``re``		\n\
							\n\
:param fd: file descriptor				\n\
:type fd: C/int						\n\
:param name: (optional) file name for display		\n\
:type fd: string					\n\
:param name: (optional) file mode for opening		\n\
:type fd: string					\n\
							\n\
:return: file handle					\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);

    return idio_file_handle_open_from_fd (ifd, args, IDIO_HANDLE_FLAG_FILE, "open-input-file-from-fd", "re", IDIO_HANDLE_FLAG_READ, IDIO_HANDLE_FLAG_WRITE);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("open-output-file-from-fd", open_output_file_handle_from_fd, (IDIO ifd, IDIO args), "fd [name [mode]]", "\
construct an output file handle from `fd` using the optional	\n\
`name` instead of the default `/dev/fd/{fd}` and	\n\
the optional mode `mode` instead of ``we``		\n\
							\n\
:param fd: file descriptor				\n\
:type fd: C/int						\n\
:param name: (optional) file name for display		\n\
:type fd: string					\n\
:param name: (optional) file mode for opening		\n\
:type fd: string					\n\
							\n\
:return: file handle					\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);

    return idio_file_handle_open_from_fd (ifd, args, IDIO_HANDLE_FLAG_FILE, "open-output-file-from-fd", "we", IDIO_HANDLE_FLAG_WRITE, IDIO_HANDLE_FLAG_READ);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("open-input-pipe", open_input_pipe_handle, (IDIO ifd, IDIO args), "fd [name]", "\
construct an input pipe handle from `fd` using the optional	\n\
`name` instead of the default `/dev/fd/{fd}` and	\n\
the optional mode `mode` instead of ``re``		\n\
							\n\
The key difference from a regular *-from-fd is that a	\n\
pipe file handle is not seekable.			\n\
							\n\
:param fd: file descriptor				\n\
:type fd: C/int						\n\
:param name: (optional) file name for display		\n\
:type fd: string					\n\
:param name: (optional) file mode for opening		\n\
:type fd: string					\n\
							\n\
:return: pipe file handle				\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);

    IDIO ph = idio_file_handle_open_from_fd (ifd, args, IDIO_HANDLE_FLAG_PIPE, "open-input-pipe", "re", IDIO_HANDLE_FLAG_READ, IDIO_HANDLE_FLAG_NONE);

    return ph;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("open-output-pipe", open_output_pipe_handle, (IDIO ifd, IDIO args), "fd [name [mode]]", "\
construct an output pipe handle from `fd` using the optional	\n\
`name` instead of the default `/dev/fd/{fd}` and	\n\
the optional mode `mode` instead of ``we``		\n\
							\n\
The key difference from a regular *-from-fd is that a	\n\
pipe file handle is not seekable.			\n\
							\n\
:param fd: file descriptor				\n\
:type fd: C/int						\n\
:param name: (optional) file name for display		\n\
:type fd: string					\n\
:param name: (optional) file mode for opening		\n\
:type fd: string					\n\
							\n\
:return: pipe file handle				\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);

    IDIO ph = idio_file_handle_open_from_fd (ifd, args, IDIO_HANDLE_FLAG_PIPE, "open-output-pipe", "we", IDIO_HANDLE_FLAG_WRITE, IDIO_HANDLE_FLAG_NONE);

    return ph;
}

IDIO idio_open_file_handle_C (char *func, IDIO filename, char *pathname, int free_pathname, char *mode_str, int free_mode_str)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (filename);	/* the user supplied name */
    IDIO_C_ASSERT (pathname);
    IDIO_C_ASSERT (mode_str);

    IDIO_TYPE_ASSERT (string, filename);

    int h_flags = 0;

    switch (mode_str[0]) {
    case 'r':
	h_flags = IDIO_HANDLE_FLAG_READ;
	if (strchr (mode_str, '+') != NULL) {
	    h_flags |= IDIO_HANDLE_FLAG_WRITE;
	}
	break;
    case 'a':
    case 'w':
	h_flags = IDIO_HANDLE_FLAG_WRITE;
	if (strchr (mode_str, '+') != NULL) {
	    h_flags |= IDIO_HANDLE_FLAG_READ;
	}
	break;
    default:
	/*
	 * Test Cases:
	 *
	 *   file-handle-errors/open-file-mode-invalid.idio
	 *   file-handle-errors/open-input-file-mode-invalid.idio
	 *   file-handle-errors/open-output-file-mode-invalid.idio
	 *
	 * open-file "bob" "q"
	 */
	if (free_pathname) {
	    IDIO_GC_FREE (pathname);
	}

	IDIO imode = idio_string_C (mode_str);
	if (free_mode_str) {
	    IDIO_GC_FREE (mode_str);
	}

	idio_file_handle_mode_format_error (func, "invalid", imode, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	break;
    }

    int s_flags = IDIO_FILE_HANDLE_FLAG_NONE;
    int flags = 0;
    if (-1 == idio_file_handle_validate_mode_flags (mode_str, &s_flags, &flags)) {
	/*
	 * Test Cases:
	 *
	 *   file-handle-errors/open-file-mode-letter-invalid-1.idio
	 *   file-handle-errors/open-file-mode-letter-invalid-2.idio
	 *
	 * open-file "bob" "rr"
	 * open-file "bob" "rq"
	 */
	if (free_pathname) {
	    IDIO_GC_FREE (pathname);
	}

	IDIO imode = idio_string_C (mode_str);
	if (free_mode_str) {
	    IDIO_GC_FREE (mode_str);
	}

	idio_file_handle_mode_format_error (func, "invalid", imode, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

    int fd;
    int tries;
    for (tries = 2; tries > 0 ; tries--) {
	fd = open (pathname, flags, mode);
	if (-1 == fd) {
	    switch (errno) {
	    case EMFILE:	/* process max */
	    case ENFILE:	/* system max */
		idio_gc_collect_all ("idio_open_file_handle_C");
		break;
	    case EACCES:
		{
		    /*
		     * Test Cases:
		     *
		     *   file-handle-errors/open-file-protection.idio
		     *   file-handle-errors/open-input-file-protection.idio
		     *   file-handle-errors/open-output-file-protection.idio
		     *
		     * tmpfile := (make-tmp-file)
		     * chmod \= tmpfile
		     * open-file tmpfile "re"
		     */
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode_str) {
			IDIO_GC_FREE (mode_str);
		    }

		    idio_file_handle_file_protection_error (func, pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case EEXIST:
		{
		    /*
		     * Test Case: file-handle-errors/open-file-exists.idio
		     *
		     * tmpfile := (make-tmp-file)
		     * open-file tmpfile "wx"
		     *
		     * XXX requires the non-POSIX "x" == O_EXCL
		     */
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode_str) {
			IDIO_GC_FREE (mode_str);
		    }

		    idio_file_handle_filename_already_exists_error (func, pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case ENAMETOOLONG:
		{
		    /*
		     * Test Cases:
		     *
		     *   file-handle-errors/open-file-filename-PATH_MAX.idio
		     *   file-handle-errors/open-input-file-filename-PATH_MAX.idio
		     *   file-handle-errors/open-output-file-filename-PATH_MAX.idio
		     *
		     * open-file (make-string (C/->integer PATH_MAX) #\A) "re"
		     */
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode_str) {
			IDIO_GC_FREE (mode_str);
		    }

		    idio_file_handle_filename_system_error (func, pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case ENOENT:
		{
		    /*
		     * Test Cases:
		     *
		     *   file-handle-errors/open-file-filename-missing.idio
		     *   file-handle-errors/open-input-file-filename-missing.idio
		     *
		     * tmpfile := (make-tmp-file)
		     * delete-file tmpfile
		     * open-file tmpfile "re"
		     *
		     * XXX "w" and "a" mode flags imply O_CREAT
		     */
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode_str) {
			IDIO_GC_FREE (mode_str);
		    }

		    idio_file_handle_filename_not_found_error (func, pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case ENOTDIR:
		{
		    /*
		     * Test Cases:
		     *
		     *   file-handle-errors/open-file-dirname-missing.idio
		     *   file-handle-errors/open-input-file-dirname-missing.idio
		     *   file-handle-errors/open-output-file-dirname-missing.idio
		     *
		     * tmpfile := (make-tmp-file)
		     * delete-file tmpfile
		     * open-file (append-string tmpfile "/foo") "re"
		     */
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode_str) {
			IDIO_GC_FREE (mode_str);
		    }

		    idio_file_handle_filename_system_error (func, pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    default:
		{
		    /*
		     * Test Case: ??
		     *
		     * What can we reasonably generate?  ELOOP, maybe?
		     */
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode_str) {
			IDIO_GC_FREE (mode_str);
		    }

		    idio_error_system_errno ("fopen", pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }
	} else {
	    break;
	}
    }

    if (-1 == fd) {
	/*
	 * Test Case: file-handle-errors/EMFILE.idio
	 *
	 * loop creating files but not closing them
	 */
	IDIO pn = idio_string_C (pathname);
	IDIO m = idio_string_C (mode_str);

	if (free_pathname) {
	    IDIO_GC_FREE (pathname);
	}

	if (free_mode_str) {
	    /*
	     * Code coverage:
	     *
	     * Use open-file with a mode string (rather than
	     * open-input-file with an fixed mode string) to get here.
	     */
	    IDIO_GC_FREE (mode_str);
	}

	idio_error_system_errno ("open (final)", IDIO_LIST2 (pn, m), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_open_file_handle (filename, pathname, fd, IDIO_HANDLE_FLAG_FILE, h_flags, s_flags);
}

/*
 * imode, if not #n, is used in preferance.
 */
static IDIO idio_file_handle_open_file (char *func, IDIO name, IDIO mode, char *def_mode)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (name);
    IDIO_ASSERT (mode);

    int free_name_C = 0;
    char *name_C;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	/*
	 * Test Cases:
	 *
	 *   file-handle-errors/open-file-filename-format.idio
	 *   file-handle-errors/open-input-file-filename-format.idio
	 *   file-handle-errors/open-output-file-filename-format.idio
	 *
	 * open-file (join-string (make-string 1 #U+0) '("hello" "world")) "re"
	 */
	name_C = idio_file_handle_filename_string_C (name, func, &free_name_C, IDIO_C_FUNC_LOCATION ());
	break;
    default:
	/*
	 * Test Cases:
	 *
	 *   file-handle-errors/open-file-filename-type.idio
	 *   file-handle-errors/open-input-file-filename-type.idio
	 *   file-handle-errors/open-output-file-filename-type.idio
	 *
	 * open-file #t "re"
	 */
	idio_error_param_type ("string", name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	break;
    }

    int free_mode_C = 0;
    char *mode_C = def_mode;

    switch (idio_type (mode)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	/*
	 * Test Case: file-handle-errors/open-file-mode-format.idio
	 *
	 * open-file "bob" (join-string (make-string 1 #U+0) '("r" "w"))
	 *
	 */
	mode_C = idio_file_handle_mode_string_C (mode, func, &free_mode_C, IDIO_C_FUNC_LOCATION ());
	break;
    default:
	if (idio_S_nil != mode ||
	    NULL == def_mode) {
	    /*
	     * Test Case: file-handle-errors/open-file-mode-type.idio
	     *
	     * open-file "bob" #t
	     */
	    IDIO_GC_FREE (name_C);

	    idio_error_param_type ("string", mode, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    }

    return idio_open_file_handle_C (func, name, name_C, 1, mode_C, free_mode_C);
}

IDIO_DEFINE_PRIMITIVE2_DS ("open-file", open_file_handle, (IDIO name, IDIO mode), "name mode", "\
open input file `name` using mode `mode`		\n\
							\n\
:param name: file name					\n\
:type name: string					\n\
:param mode: open mode					\n\
:type mode: string					\n\
							\n\
:return: file handle					\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (mode);

    return idio_file_handle_open_file ("open-file", name, mode, NULL);
}

IDIO_DEFINE_PRIMITIVE1_DS ("open-input-file", open_input_file_handle, (IDIO name), "name", "\
open input file `name`					\n\
							\n\
:param name: file name					\n\
:type name: string					\n\
							\n\
:return: file handle					\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (name);

    return idio_file_handle_open_file ("open-input-file", name, idio_S_nil, "re");
}

IDIO_DEFINE_PRIMITIVE1_DS ("open-output-file", open_output_file_handle, (IDIO name), "name", "\
open output file `name`					\n\
							\n\
:param name: file name					\n\
:type name: string					\n\
							\n\
:return: file handle					\n\
:rtype: handle						\n\
")
{
    IDIO_ASSERT (name);

    return idio_file_handle_open_file ("open-output-file", name, idio_S_nil, "we");
}

static IDIO idio_open_std_file_handle (FILE *filep)
{
    IDIO_C_ASSERT (filep);

    int hflags = IDIO_HANDLE_FLAG_NONE;
    char *name = NULL;

    if (filep == stdin) {
	hflags = IDIO_HANDLE_FLAG_READ;
	name = "*stdin*";
    } else if (filep == stdout) {
	hflags = IDIO_HANDLE_FLAG_WRITE;
	name = "*stdout*";
    } else if (filep == stderr) {
	hflags = IDIO_HANDLE_FLAG_WRITE;
	name = "*stderr*";
    } else {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected standard IO stream");

	return idio_S_notreached;
    }

    return idio_open_file_handle (idio_string_C (name), name, fileno (filep), IDIO_HANDLE_FLAG_FILE, hflags, IDIO_FILE_HANDLE_FLAG_STDIO);
}

IDIO idio_stdin_file_handle ()
{
    return idio_stdin;
}

IDIO idio_stdout_file_handle ()
{
    return idio_stdout;
}

IDIO idio_stderr_file_handle ()
{
    return idio_stderr;
}

int idio_isa_file_handle (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_handle (o) &&
	IDIO_HANDLE_FLAGS (o) & IDIO_HANDLE_FLAG_FILE) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("file-handle?", file_handlep, (IDIO o), "o", "\
test if `o` is a file handle			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a file handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_file_handle (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_input_file_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_file_handle (o) &&
	    IDIO_INPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("input-file-handle?", input_file_handlep, (IDIO o), "o", "\
test if `o` is an input file handle		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an input file handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_input_file_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_output_file_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_file_handle (o) &&
	    IDIO_OUTPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("output-file-handle?", output_file_handlep, (IDIO o), "o", "\
test if `o` is an output file handle		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an output file handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_output_file_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("file-handle-fd", file_handle_fd, (IDIO fh), "fh", "\
return the file descriptor associated with	\n\
file handle `fh`				\n\
						\n\
:param fh: file handle to query			\n\
:type fh: file handle				\n\
						\n\
:return: file descriptor			\n\
:rtype: C/int					\n\
")
{
    IDIO_ASSERT (fh);

    /*
     * Test Case: file-handle-errors/file-handle-fd-bad-type.idio
     *
     * file-handle-fd #t
     */
    IDIO_USER_TYPE_ASSERT (file_handle, fh);

    return idio_C_int (IDIO_FILE_HANDLE_FD (fh));
}

/*
 * Grr!  Having split the difference between a file and a pipe handle
 * (important for some things) by using separate type flags we really
 * could do with making them indistinct for other things.
 *
 * So a testable type of "fd" handle -- a handle wrappering a C file
 * descriptor
 */
int idio_isa_fd_handle (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_handle (o) &&
	IDIO_HANDLE_FLAGS (o) & (IDIO_HANDLE_FLAG_FILE |
				 IDIO_HANDLE_FLAG_PIPE)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("fd-handle?", fd_handlep, (IDIO o), "o", "\
test if `o` is a fd handle			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a fd handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_fd_handle (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_input_fd_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_fd_handle (o) &&
	    IDIO_INPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("input-fd-handle?", input_fd_handlep, (IDIO o), "o", "\
test if `o` is an input fd handle		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an input fd handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_input_fd_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_output_fd_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_fd_handle (o) &&
	    IDIO_OUTPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("output-fd-handle?", output_fd_handlep, (IDIO o), "o", "\
test if `o` is an output fd handle		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an output fd handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_output_fd_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("fd-handle-fd", fd_handle_fd, (IDIO fh), "fh", "\
return the file descriptor associated with	\n\
fd handle `fh`					\n\
						\n\
:param fh: fd handle to query			\n\
:type fh: fd handle				\n\
						\n\
:return: file descriptor			\n\
:rtype: C/int					\n\
")
{
    IDIO_ASSERT (fh);

    /*
     * Test Case: file-handle-errors/fd-handle-fd-bad-type.idio
     *
     * fd-handle-fd #t
     */
    IDIO_USER_TYPE_ASSERT (fd_handle, fh);

    return idio_C_int (IDIO_FILE_HANDLE_FD (fh));
}

int idio_isa_pipe_handle (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_handle (o) &&
	IDIO_HANDLE_FLAGS (o) & IDIO_HANDLE_FLAG_PIPE) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pipe-handle?", pipe_handlep, (IDIO o), "o", "\
test if `o` is a pipe handle			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a pipe handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_pipe_handle (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_input_pipe_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_pipe_handle (o) &&
	    IDIO_INPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("input-pipe-handle?", input_pipe_handlep, (IDIO o), "o", "\
test if `o` is an input pipe handle		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an input pipe handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_input_pipe_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_output_pipe_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_pipe_handle (o) &&
	    IDIO_OUTPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("output-pipe-handle?", output_pipe_handlep, (IDIO o), "o", "\
test if `o` is an output pipe handle		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an output pipe handle, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_output_pipe_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pipe-handle-fd", pipe_handle_fd, (IDIO fh), "fh", "\
return the file descriptor associated with	\n\
pipe handle `fh`				\n\
						\n\
:param fh: pipe handle to query			\n\
:type fh: pipe handle				\n\
						\n\
:return: file descriptor			\n\
:rtype: C/int					\n\
")
{
    IDIO_ASSERT (fh);

    /*
     * Test Case: pipe-handle-errors/pipe-handle-fd-bad-type.idio
     *
     * pipe-handle-fd #t
     */
    IDIO_USER_TYPE_ASSERT (pipe_handle, fh);

    return idio_C_int (IDIO_FILE_HANDLE_FD (fh));
}

void idio_file_handle_finalizer (IDIO fh)
{
    IDIO_ASSERT (fh);
    IDIO_TYPE_ASSERT (handle, fh);

    if (! (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED)) {
	IDIO_HANDLE_M_CLOSE (fh) (fh);
    }
}

/*
 * Code coverage:
 *
 * XXX I need to remove the final references to this.
 */
void idio_remember_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    idio_hash_put (idio_file_handles, fh, idio_S_nil);
}

/*
 * Code coverage:
 *
 * XXX I need to remove the final references to this.
 */
void idio_forget_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    idio_hash_delete (idio_file_handles, fh);
}

void idio_free_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_GC_FREE (IDIO_FILE_HANDLE_BUF (fh));
    IDIO_GC_FREE (IDIO_HANDLE_STREAM (fh));
}

/*
 * ready? (char-ready? in Scheme) is true if
 *
 * 1. there is input available (without the need to block, ie. already
 *    buffered from a previous read) or
 *
 * 2. is at end of file.
 *
 * The general commentary is [Guile]: If char-ready? were to return #f
 * at end of file, a port at end of file would be indistinguishable
 * from an interactive port that has no ready characters.
 *
 * I've augmented this as ready? can't be true for a closed handle.
 * Surely?
 */
int idio_readyp_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (IDIO_CLOSEDP_HANDLE (fh)) {
	/*
	 * Test Case: file-handle-errors/ready-closed-handle.idio
	 *
	 * fh := open-file ...
	 * close-handle fh
	 * ready? fh
	 */
	idio_handle_closed_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    if (! idio_input_fd_handlep (fh)) {
	/*
	 * Test Case: ?? not file-handle-errors/ready-bad-handle.idio
	 *
	 * ready? (current-output-handle)
	 *
	 * The ready? function has called idio_handle_or_current()
	 * which has done the IDIO_HANDLE_INPUTP() check for us
	 */
	idio_handle_read_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    if (IDIO_FILE_HANDLE_COUNT (fh) > 0) {
	/*
	 * Code coverage:
	 *
	 * To get here we need to have read something from the file
	 * handle but obviously(?) not everything.
	 */
	return 1;
    }

    return (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_EOF);
}

void idio_file_handle_read_more (IDIO fh)
{
    IDIO_ASSERT (fh);

    ssize_t nread = read (IDIO_FILE_HANDLE_FD (fh), IDIO_FILE_HANDLE_BUF (fh), IDIO_FILE_HANDLE_BUFSIZ (fh));
    if (-1 == nread) {
	/*
	 * Test Case: ??
	 *
	 * How to get a (previously good) file descriptor to fail for
	 * read(2)?
	 */
	idio_error_system_errno ("read", fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
    } else if (0 == nread) {
	IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_EOF;
    } else {
	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = nread;
    }
}

int idio_getb_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (! idio_input_fd_handlep (fh)) {
	/*
	 * Test Case: ??
	 *
	 * We don't expose the getb method otherwise it would be
	 * something like:
	 *
	 * getb-handle (current-output-handle)
	 */
	idio_handle_read_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) >= 1) {
	    IDIO_FILE_HANDLE_COUNT (fh) -= 1;
	    int c = (int) *(IDIO_FILE_HANDLE_PTR (fh));
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    return c;
	} else {
	    idio_file_handle_read_more (fh);
	    if (idio_eofp_file_handle (fh)) {
		return EOF;
	    }
	    if (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		/*
		 * Code coverage:
		 *
		 * Clearly needs to be interactive!
		 */
		IDIO_FILE_HANDLE_COUNT (fh) -= 1;
		int c = (int) *(IDIO_FILE_HANDLE_PTR (fh));
		IDIO_FILE_HANDLE_PTR (fh) += 1;
		return c;
	    }
	}
    }
}

int idio_eofp_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (fd_handle, fh);

    return (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_EOF);
}

int idio_close_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (fd_handle, fh);

    if (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED) {
	/*
	 * Test Case: file-handle-errors/close-closed-handle.idio
	 *
	 * fh := open-file ...
	 * close-handle fh
	 * close-handle fh
	 */
	idio_handle_closed_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    } else {
	if (EOF == idio_flush_file_handle (fh)) {
	    return EOF;
	}

	IDIO_HANDLE_FLAGS (fh) |= IDIO_HANDLE_FLAG_CLOSED;
	idio_gc_deregister_finalizer (fh);

	/*
	 * XXX we don't error if close(2) fails.  This function is
	 * called from idio_file_handle_finalizer() and so needs to
	 * disregard failures.
	 */
	return close (IDIO_FILE_HANDLE_FD (fh));
    }
}

/*
 * Code coverage:
 *
 * idio_putb_handle() is only called by idio_command_invoke() when
 * recovering stdout/stderr handles and will NOT have happened if
 * stdout/stderr was a file handle (see stdout-fileno/stderr-fileno in
 * llibc.idio).
 *
 * So...we never get here?
 */
int idio_putb_file_handle (IDIO fh, uint8_t c)
{
    IDIO_ASSERT (fh);

    if (! idio_output_fd_handlep (fh)) {
	/*
	 * Test Case: ??
	 *
	 * We don't expose the putb method otherwise it would be
	 * something like:
	 *
	 * handle-putb (current-input-handle)
	 */
	idio_handle_write_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) < IDIO_FILE_HANDLE_BUFSIZ (fh)) {
	    *(IDIO_FILE_HANDLE_PTR (fh)) = (char) c;
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    IDIO_FILE_HANDLE_COUNT (fh) += 1;

	    if ('\n' == c &&
		IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		if (EOF == idio_flush_file_handle (fh)) {
		    return EOF;
		}
	    }
	    break;
	} else {
	    if (EOF == idio_flush_file_handle (fh)) {
		return EOF;
	    }
	}
    }

    return 1;
}

int idio_putc_file_handle (IDIO fh, idio_unicode_t c)
{
    IDIO_ASSERT (fh);

    if (! idio_output_fd_handlep (fh)) {
	/*
	 * Test Case: ?? not file-handle-errors/write-char-bad-handle.idio
	 *
	 * write-char #\a (current-input-handle)
	 *
	 * XXX This doesn't get you here as write-char calls
	 * idio_handle_or_current() which does the
	 * IDIO_HANDLE_OUTPUTP() test before we get here.
	 */
	idio_handle_write_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    char buf[4];
    int size;
    idio_utf8_code_point (c, buf, &size);

    for (int n = 0;n < size;n++) {
	if (IDIO_FILE_HANDLE_COUNT (fh) < IDIO_FILE_HANDLE_BUFSIZ (fh)) {
	    *(IDIO_FILE_HANDLE_PTR (fh)) = buf[n];
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    IDIO_FILE_HANDLE_COUNT (fh) += 1;

	    if ('\n' == c &&
		IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		/*
		 * Code coverage:
		 *
		 * Requires, uh, interaction...
		 */
		if (EOF == idio_flush_file_handle (fh)) {
		    return EOF;
		}
	    }
	    break;
	} else {
	    /*
	     * Code coverage:
	     *
	     * We need to have filled the buffer (BUFSIZ) and then one
	     * more.
	     */
	    if (EOF == idio_flush_file_handle (fh)) {
		/*
		 * Code coverage:
		 *
		 * The fwrite(3) in idio_flush_file_handle() needs to
		 * fail...
		 */
		return EOF;
	    }
	}
    }

    return size;
}

ptrdiff_t idio_puts_file_handle (IDIO fh, char *s, size_t slen)
{
    IDIO_ASSERT (fh);

    if (! idio_output_fd_handlep (fh)) {
	/*
	 * Test Case: ?? not file-handle-errors/write-bad-handle.idio
	 *
	 * write "a" (current-input-handle)
	 *
	 * XXX This doesn't get you here as write-char calls
	 * idio_handle_or_current() which does the
	 * IDIO_HANDLE_OUTPUTP() test before we get here.
	 */
	idio_handle_write_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    ssize_t r = EOF;

    /*
     * If the string is going to cause a flush then flush and write
     * the string directly out
     */
    if (slen > IDIO_FILE_HANDLE_BUFSIZ (fh) ||
	slen > (IDIO_FILE_HANDLE_BUFSIZ (fh) - IDIO_FILE_HANDLE_COUNT (fh))) {
	if (EOF == idio_flush_file_handle (fh)) {
	    /*
	     * Code coverage:
	     *
	     * The fwrite(3) in idio_flush_file_handle() needs to
	     * fail...
	     */
	    return EOF;
	}

	struct sigaction nsa;
	struct sigaction osa;

	if (! idio_isa_file_handle (fh)) {
	     nsa.sa_handler = SIG_IGN;
	     sigemptyset (&nsa.sa_mask);
	     nsa.sa_flags = 0;

	     if (sigaction (SIGPIPE, &nsa, &osa) == -1) {
		  /*
		   * Test Case: ??
		   *
		   * Hmm, not sure...
		   */
		  idio_error_system_errno ("sigaction", fh, IDIO_C_FUNC_LOCATION ());

		  /* notreached */
		  return r;
	     }
	}

	r = write (IDIO_FILE_HANDLE_FD (fh), s, slen);

	if (! idio_isa_file_handle (fh)) {
	     if (sigaction (SIGPIPE, &osa, NULL) == -1) {
		  /*
		   * Test Case: ??
		   *
		   * Hmm, not sure...
		   */
		  idio_error_system_errno ("sigaction", fh, IDIO_C_FUNC_LOCATION ());

		  /* notreached */
		  return r;
	     }
	}

	if (-1 == r) {
	     if (! idio_isa_file_handle (fh) &&
		 EPIPE == errno) {
#ifdef IDIO_DEBUG
		  fprintf (stderr, "Ceci n'est pas une pipe.\n");
#endif
	     } else {
		  /*
		   * Test Case: ??
		   *
		   * Hmm, not sure...
		   */
		  idio_error_system_errno ("write", fh, IDIO_C_FUNC_LOCATION ());

		  /* notreached */
		  return r;
	     }
	} else {
	     if (r != slen) {
#ifdef IDIO_DEBUG
		 fprintf (stderr, "write: %4td / %4zu\n", r, slen);
#endif
	     }
	}

	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = 0;
    } else {
	memcpy (IDIO_FILE_HANDLE_PTR (fh), s, slen);
	IDIO_FILE_HANDLE_PTR (fh) += slen;
	IDIO_FILE_HANDLE_COUNT (fh) += slen;
	r = slen;
    }

    if (EOF == idio_flush_file_handle (fh)) {
	/*
	 * Code coverage:
	 *
	 * The fwrite(3) in idio_flush_file_handle() needs to fail...
	 */
	return EOF;
    }

    size_t nl = 0;
    size_t i;
    for (i = 0; i < slen; i++) {
	if ('\n' == s[i]) {
	    nl++;
	}
    }
    IDIO_HANDLE_LINE (fh) += nl;

    return r;
}

/*
 * We need to return 0 on successful "flush" or EOF.
 */
int idio_flush_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (fd_handle, fh);

    if (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED) {
	/*
	 * Test Case: ??
	 */
	idio_debug ("flushing a closed handle: %s\n", fh);
	idio_handle_closed_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    /*
     * What does it mean to flush a file open for reading?  fflush(3)
     * "discards any buffered data that has been fetched from the
     * underlying file, but has not been consumed by the application."
     *
     * ??
     *
     * Anyway, all we do here is write(2) the contents of *our* buffer
     * to the underlying file descriptor.
     */
    if (IDIO_INPUTP_HANDLE (fh) &&
	! IDIO_OUTPUTP_HANDLE (fh)) {
	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = 0;

	return 0;
    }

    int r = EOF;

    struct sigaction nsa;
    struct sigaction osa;

    if (! idio_isa_file_handle (fh)) {
	 nsa.sa_handler = SIG_IGN;
	 sigemptyset (&nsa.sa_mask);
	 nsa.sa_flags = 0;

	 if (sigaction (SIGPIPE, &nsa, &osa) == -1) {
	      /*
	       * Test Case: ??
	       *
	       * Hmm, not sure...
	       */
	      idio_error_system_errno ("sigaction", fh, IDIO_C_FUNC_LOCATION ());

	      /* notreached */
	      return r;
	 }
    }

    ssize_t n = write (IDIO_FILE_HANDLE_FD (fh), IDIO_FILE_HANDLE_BUF (fh), IDIO_FILE_HANDLE_COUNT (fh));

    if (! idio_isa_file_handle (fh)) {
	 if (sigaction (SIGPIPE, &osa, NULL) == -1) {
	      /*
	       * Test Case: ??
	       *
	       * Hmm, not sure...
	       */
	      idio_error_system_errno ("sigaction", fh, IDIO_C_FUNC_LOCATION ());

	      /* notreached */
	      return r;
	 }
    }

    if (-1 == n) {
	 if (! idio_isa_file_handle (fh) &&
	     EPIPE == errno) {
#ifdef IDIO_DEBUG
	      fprintf (stderr, "Ceci n'est pas une pipe.\n");
#endif
	 } else {
	      /*
	       * Test Case: ??
	       *
	       * Hmm, not sure...
	       */

	      fprintf (stderr, "flush %d bytes failed for fd=%3d\n", IDIO_FILE_HANDLE_COUNT (fh), IDIO_FILE_HANDLE_FD (fh));
	      fprintf (stderr, "hflags=%#x\n", IDIO_HANDLE_FLAGS (fh));
	      idio_debug ("fh=%s\n", fh);
	      idio_error_system_errno ("write", fh, IDIO_C_FUNC_LOCATION ());

	      /* notreached */
	      return r;
	 }
    } else {
	 if (n == IDIO_FILE_HANDLE_COUNT (fh)) {
	      r = 0;
	 } else {
#ifdef IDIO_DEBUG
	      fprintf (stderr, "write: %4td / %4d\n", n, IDIO_FILE_HANDLE_COUNT (fh));
#endif
	 }
    }

    IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
    IDIO_FILE_HANDLE_COUNT (fh) = 0;

    return r;
}

off_t idio_seek_file_handle (IDIO fh, off_t offset, int whence)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    off_t lseek_r = lseek (IDIO_FILE_HANDLE_FD (fh), offset, whence);
    if (-1 == lseek_r) {
	/*
	 * Test Case: file-handle-errors/lseek-negative-offset.idio
	 *
	 * ifh := open-input-file ...
	 * seek-handle ifh -1
	 */
	idio_error_system_errno ("lseek", fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    /*
     * fseek(3): A successful call to the fseek() function clears the
     * end-of-file indicator for the stream and undoes any effects of
     * the ungetc(3) function on the same stream.
     *
     * If we use lseek(2) we should implement the same.
     *
     * Our caller will clear the lookahead character leaving us to
     * handle EOF.
     */
    IDIO_FILE_HANDLE_FLAGS (fh) &= ~IDIO_FILE_HANDLE_FLAG_EOF;

    if (IDIO_FILE_HANDLE_COUNT (fh)) {
	/*
	 * We need to be careful about what we have buffered as the
	 * seek may well have invalidated it.
	 *
	 * But it might not have invalidated it saving us a
	 * re-buffering!
	 *
	 * The buffer PTR range is: BUF <= PTR < BUF+BUFSIZ
	 *
	 * We can invalidate the buffer by setting COUNT, the number
	 * of bytes remaining in the current buffer, to 0.
	 *
	 * If we haven't invalidated the buffer we need to adjust PTR
	 * and COUNT.
	 *
	 * There's potential for off-by-one errors:
	 *
	 * 1. If the lookahead char is not EOF then PTR is one more
	 *    than POS thinks it should be.
	 */

	int invalid = 0;
	switch (whence) {
	case SEEK_SET:
	    {
		if (IDIO_HANDLE_LC (fh) != EOF) {
		    IDIO_FILE_HANDLE_PTR (fh)--;
		}
		ptrdiff_t seek_pos = offset - IDIO_HANDLE_POS (fh);
		if (seek_pos < 0) {
		    if (IDIO_FILE_HANDLE_PTR (fh) - IDIO_FILE_HANDLE_BUF (fh) + seek_pos < 0) {
			invalid = 1;
		    } else {
			IDIO_FILE_HANDLE_PTR (fh) += seek_pos;
			IDIO_FILE_HANDLE_COUNT (fh) -= seek_pos;
		    }
		} else {
		    if (seek_pos >= (IDIO_FILE_HANDLE_COUNT (fh))) {
			invalid = 1;
		    } else {
			IDIO_FILE_HANDLE_PTR (fh) += seek_pos;
			IDIO_FILE_HANDLE_COUNT (fh) -= seek_pos;
		    }
		}
	    }
	    break;
	case SEEK_END:
	    /*
	     * XXX need to do the maths in case BUF overlaps the end
	     * of the file!
	     */
	    invalid = 1;
	    break;
	case SEEK_CUR:
	    /*
	     * Code coverage:
	     *
	     * Should not occur as SEEK_CUR was transformed into a
	     * SEEK_SET.
	     */
	    if (offset) {
		invalid = 1;
	    }
	    break;
	default:
	    {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		char em[BUFSIZ];
		sprintf (em, "'%#x' is invalid", whence);
		idio_error_param_value ("whence", em, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return -1;
	    }
	}

	if (invalid) {
	    IDIO_FILE_HANDLE_COUNT (fh) = 0;
	}
    }

    return lseek_r;
}

/*
 * Code coverage:
 *
 * We don't expose the print method (idio_print_handle() /
 * IDIO_HANDLE_M_PRINT()).
 */
void idio_print_file_handle (IDIO fh, IDIO o)
{
    IDIO_ASSERT (fh);

    if (! idio_output_fd_handlep (fh)) {
	/*
	 * Test Case: ??
	 *
	 * We don't expose the print method otherwise it would be
	 * something like:
	 *
	 * print-handle (current-input-handle)
	 */
	idio_handle_write_error (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
    }

    size_t size = 0;
    char *os = idio_display_string (o, &size);
    IDIO_HANDLE_M_PUTS (fh) (fh, os, size);
    IDIO_HANDLE_M_PUTS (fh) (fh, "\n", 1);
    IDIO_GC_FREE (os);
}

IDIO_DEFINE_PRIMITIVE1_DS ("close-fd-handle-on-exec", close_fd_handle_on_exec, (IDIO fh), "fh", "\
call fcntl(3) on the C file descriptor associated with		\n\
fd handle `fh` with `F_SETFD` and `FD_CLOEXEC` arguments	\n\
						\n\
:param fh: fd handle to cloexec			\n\
:type fh: fd handle				\n\
						\n\
:return: 0 or raises ^system-error		\n\
:rtype: C/int					\n\
")
{
    IDIO_ASSERT (fh);

    /*
     * Test Case: file-handle-errors/close-file-handle-on-exec-bad-type.idio
     *
     * close-file-handle-on-exec #t
     */
    IDIO_USER_TYPE_ASSERT (fd_handle, fh);

    int fd = IDIO_FILE_HANDLE_FD (fh);

    int r = fcntl (fd, F_SETFD, FD_CLOEXEC);

    if (-1 == r) {
	/*
	 * Test Case: ??
	 *
	 * Not sure how to provoke this...
	 */
	idio_error_system_errno_msg ("fcntl", "F_SETFD FD_CLOEXEC", fh, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_CLOEXEC;

    return idio_C_int (r);
}

typedef struct idio_file_extension_s {
    char *ext;
    IDIO (*reader) (IDIO h);
    IDIO (*evaluator) (IDIO e, IDIO cs);
    IDIO (*modulep) (void);
} idio_file_extension_t;

static idio_file_extension_t idio_file_extensions[] = {
    { NULL, idio_read, idio_evaluate, idio_Idio_module_instance },
    { ".idio", idio_read, idio_evaluate, idio_Idio_module_instance },
    /* { ".scm", idio_scm_read, idio_scm_evaluate, idio_main_scm_module_instance }, */
    { NULL, NULL, NULL }
};

char *idio_libfile_find_C (char *file)
{
    IDIO_C_ASSERT (file);

    size_t filelen = strlen (file);

    IDIO IDIOLIB = idio_module_current_symbol_value_recurse (idio_env_IDIOLIB_sym, idio_S_nil);

    /*
     * idiolib is the start of the current IDIOLIB pathname element --
     * colon will mark the end
     *
     * idiolibe is the end of the whole IDIOLIB string, used to
     * calculate when we've tried all parts
     */
    int free_idiolib_copy_C = 0;
    char *idiolib_copy_C = NULL;
    char *idiolib;
    char *idiolibe;
    if (idio_S_undef == IDIOLIB ||
	! idio_isa_string (IDIOLIB)) {
	/*
	 * Code coverage:
	 *
	 * IDIOLIB = #t
	 * find-lib "foo"
	 */
	idio_error_warning_message ("IDIOLIB is not a string");
	idiolib = idio_env_IDIOLIB_default;
	idiolibe = idiolib + strlen (idiolib);
    } else {
	size_t size = 0;
	idiolib_copy_C = idio_string_as_C (IDIOLIB, &size);
	size_t C_size = strlen (idiolib_copy_C);
	if (C_size != size) {
	    /*
	     * Test Case: file-handle-errors/find-lib-IDIOLIB-format.idio
	     *
	     * IDIOLIB :* join-string (make-string 1 #U+0) '("hello" "world")
	     * find-lib "foo"
	     */
	    IDIO_GC_FREE (idiolib_copy_C);

	    idio_env_format_error ("libfile-find", "contains an ASCII NUL", idio_env_IDIOLIB_sym, IDIOLIB, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	idiolib = idiolib_copy_C;
	idiolibe = idiolib + idio_string_len (IDIOLIB);
    }

    /*
     * find the longest filename extension -- so we don't overrun
     * PATH_MAX
     */
    size_t max_ext_len = 0;
    idio_file_extension_t *fe = idio_file_extensions;

    for (;NULL != fe->reader;fe++) {
	if (NULL != fe->ext) {
	    size_t el = strlen (fe->ext);
	    if (el > max_ext_len) {
		max_ext_len = el;
	    }
	}
    }

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char libname[PATH_MAX];
    char cwd[PATH_MAX];
    if (getcwd (cwd, PATH_MAX) == NULL) {
	/*
	 * Test Case: file-handle-errors/find-lib-getcwd-rmdir.idio
	 *
	 * tmpdir := (make-tmp-dir)
	 * chdir tmpdir
	 * rmdir tmpdir
	 * find-lib "foo"
	 */
	if (free_idiolib_copy_C) {
	    IDIO_GC_FREE (idiolib_copy_C);
	}

	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }
    size_t cwdlen = strlen (cwd);

    if ('/' == file[0]) {
	strcpy (libname, file);
    } else {
	int done = 0;
	while (! done) {
	    size_t idioliblen = idiolibe - idiolib;
	    char * colon = NULL;

	    if (0 == idioliblen) {
		if (free_idiolib_copy_C) {
		    IDIO_GC_FREE (idiolib_copy_C);
		}

		return NULL;
	    }

	    colon = memchr (idiolib, ':', idioliblen);

	    if (NULL == colon) {
		if ((idioliblen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
		    /*
		     * Test Case: file-handle-errors/find-lib-dir-lib-PATH_MAX-1.idio
		     *
		     * IDIOLIB = "foo"
		     * find-lib (make-string (C/->integer PATH_MAX) #\A)
		     */

		    if (free_idiolib_copy_C) {
			IDIO_GC_FREE (idiolib_copy_C);
		    }

		    idio_error_system ("IDIOLIB+file.idio libname length", NULL, IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		}

		memcpy (libname, idiolib, idioliblen);
		libname[idioliblen] = '\0';
	    } else {
		size_t dirlen = colon - idiolib;

		if (0 == dirlen) {
		    /*
		     * An empty value, eg. ::, in IDIOLIB means PWD.  This
		     * is a hangover behaviour from the shell's PATH.
		     *
		     * Is that a good thing?
		     */
		    if ((cwdlen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
			/*
			 * Test Case: file-handle-errors/find-lib-dir-cmd-PATH_MAX-2.idio
			 *
			 * IDIOLIB = ":"
			 * find-lib (make-string (C/->integer PATH_MAX) #\A)
			 */

			if (free_idiolib_copy_C) {
			    IDIO_GC_FREE (idiolib_copy_C);
			}

			idio_error_system ("cwd+file.idio libname length", NULL, IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    strcpy (libname, cwd);
		} else {
		    if ((dirlen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
			/*
			 * Test Case: file-handle-errors/find-lib-dir-cmd-PATH_MAX-2.idio
			 *
			 * IDIOLIB = "foo:"
			 * find-lib (make-string (C/->integer PATH_MAX) #\A)
			 */

			if (free_idiolib_copy_C) {
			    IDIO_GC_FREE (idiolib_copy_C);
			}

			idio_error_system ("dir+file.idio libname length", NULL, IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    memcpy (libname, idiolib, dirlen);
		    libname[dirlen] = '\0';
		}
	    }

	    strcat (libname, "/");
	    strcat (libname, file);

	    /*
	     * libname now contains "/path/to/file".
	     *
	     * We now try each extension in turn by maintaining lne which
	     * points at the end of the current value of libname.  That
	     * is, it points at the '\0' at the end of "/path/to/file".
	     */
	    size_t lnlen = strlen (libname);
	    char *lne = strrchr (libname, '\0');

	    fe = idio_file_extensions;

	    for (;NULL != fe->reader;fe++) {
		if (NULL != fe->ext) {

		    if ((lnlen + strlen (fe->ext)) >= PATH_MAX) {
			/*
			 * Test Case: ??
			 *
			 * Can we get here if we checked with max_ext_len
			 * in the above cases?
			 */
			if (free_idiolib_copy_C) {
			    IDIO_GC_FREE (idiolib_copy_C);
			}

			idio_file_handle_malformed_filename_error ("name too long", idio_string_C (libname), IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    strncpy (lne, fe->ext, PATH_MAX - lnlen);
		    lne[PATH_MAX - lnlen - 1] = '\0';
		}

		if (access (libname, R_OK) == 0) {
		    done = 1;
		    break;
		}

		/* reset libname without ext */
		*lne = '\0';
	    }

	    /*
	     * Sadly we can't do a double break without a GOTO -- which is
	     * considered harmful.  So we need to check and break again,
	     * here.
	     *
	     * No GOTOs in C, even though we've implemented continuations
	     * in Idio...
	     */
	    if (done) {
		break;
	    }

	    if (NULL == colon) {
		/*
		 * Nothing left to try in IDIOLIB so as a final throw of
		 * the dice, let's try idio_env_IDIOLIB_default.
		 *
		 * Unless that was the last thing we tried.
		 */
		size_t dl = strlen (idio_env_IDIOLIB_default);
		if (strncmp (libname, idio_env_IDIOLIB_default, dl) == 0 &&
		    '/' == libname[dl]) {
		    done = 1;
		    libname[0] = '\0';
		    break;
		} else {
		    idiolib = idio_env_IDIOLIB_default;
		    idiolibe = idiolib + strlen (idiolib);
		    colon = idiolib - 1;
		}
	    }

	    idiolib = colon + 1;
	}
    }

    char *idiolibname = NULL;

    if (0 != libname[0]) {
	idiolibname = idio_alloc (strlen (libname) + 1);
	strcpy (idiolibname, libname);
    }

    if (free_idiolib_copy_C) {
	IDIO_GC_FREE (idiolib_copy_C);
    }

    return idiolibname;
}

char *idio_libfile_find (IDIO file)
{
    IDIO_ASSERT (file);
    IDIO_TYPE_ASSERT (string, file);

    int free_file_C = 0;

    /*
     * Test Case: file-handle-errors/find-lib-format.idio
     *
     * find-lib (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    char *file_C = idio_file_handle_filename_string_C (file, "find-lib", &free_file_C, IDIO_C_FUNC_LOCATION ());

    char *r = idio_libfile_find_C (file_C);

    if (free_file_C) {
	IDIO_GC_FREE (file_C);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("find-lib", find_lib, (IDIO file), "filename", "\
search `IDIOLIB` for `filename` with a set of	\n\
possible file name extensions			\n\
						\n\
:param filename: file to search for		\n\
:type filename: string				\n\
						\n\
:return: the full pathname to `filename`	\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (file);

    /*
     * Test Case: file-handle-errors/find-lib-bad-type.idio
     *
     * find-lib #t
     */
    IDIO_USER_TYPE_ASSERT (string, file);

    char *r_C = idio_libfile_find (file);

    IDIO r = idio_S_nil;

    if (NULL != r_C) {
	r = idio_string_C (r_C);
	IDIO_GC_FREE (r_C);
    }

    return  r;
}

IDIO idio_load_file_name (IDIO filename, IDIO cs)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (cs);

    if (! idio_isa_string (filename)) {
	/*
	 * Test Case: ??
	 *
	 * The user-facing primitive asserts the argument is a string.
	 *
	 * That leaves a coding error.
	 */
	idio_error_param_type ("string", filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    IDIO_TYPE_ASSERT (array, cs);

    int free_filename_C = 0;

    /*
     * Test Case: file-handle-errors/load-format.idio
     *
     * load (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    char *filename_C = idio_file_handle_filename_string_C (filename, "load", &free_filename_C, IDIO_C_FUNC_LOCATION ());

    char lfn[PATH_MAX];

    char *libfile = idio_libfile_find_C (filename_C);

    if (NULL == libfile) {
	/*
	 * Test Case: file-handle-errors/load-not-found.idio
	 *
	 * tmpfile := (make-tmp-file)
	 * delete-file tmpfile
	 * load tmpfile
	 */
	if (free_filename_C) {
	    IDIO_GC_FREE (filename_C);
	}

	idio_file_handle_filename_not_found_error ("load", filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    strncpy (lfn, libfile, PATH_MAX);
    lfn[PATH_MAX - 1] = '\0';
    IDIO_GC_FREE (libfile);

    char *filename_slash = strrchr (filename_C, '/');
    if (NULL == filename_slash) {
	filename_slash = filename_C;
    }

    char *filename_dot = strrchr (filename_slash, '.');

    char *lfn_slash = strrchr (lfn, '/');

    char *lfn_dot = strrchr (lfn_slash, '.');

    IDIO (*reader) (IDIO h) = idio_read;
    IDIO (*evaluator) (IDIO e, IDIO cs) = idio_evaluate;

    idio_file_extension_t *fe = idio_file_extensions;
    IDIO filename_ext = filename;
    IDIO stack = IDIO_THREAD_STACK (idio_thread_current_thread ());

    if (NULL != lfn_dot) {
	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {
		if (strncmp (lfn_dot, fe->ext, strlen (fe->ext)) == 0) {
		    reader = fe->reader;
		    evaluator = fe->evaluator;

		    /*
		     * If it's not the same extension as the user gave
		     * us then tack it on the end
		     */
		    if (NULL == filename_dot ||
			strncmp (filename_dot, fe->ext, strlen (fe->ext))) {

			char *ss[] = { filename_C, fe->ext };

			filename_ext = idio_string_C_array (2, ss);

			idio_array_push (stack, filename_ext);
		    }
		    break;
		}
	    }
	}
    }

    if (access (lfn, R_OK) == 0) {
	IDIO fh = idio_open_file_handle_C ("load", filename_ext, lfn, 0, "r", 0);

	if (free_filename_C) {
	    IDIO_GC_FREE (filename_C);
	}

	if (filename_ext != filename) {
	    idio_array_pop (stack);
	}

	/* idio_thread_set_current_module ((*fe->modulep) ()); */
	return idio_load_handle_C (fh, reader, evaluator, cs);
    }

    /*
     * Test Case: ??
     *
     * It would require that a file we found via idio_libfile_find_C()
     * which uses access(2) with R_OK now fail the same access(2)
     * test.
     *
     * Race condition?
     */
    if (filename_ext != filename) {
	idio_array_pop (stack);
    }

    if (free_filename_C) {
	IDIO_GC_FREE (filename_C);
    }

    idio_file_handle_filename_not_found_error ("load", filename, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("load", load, (IDIO filename), "filename", "\
load ``filename`` expression by expression			\n\
								\n\
:param filename: the file to load				\n\
:type filename: string						\n\
								\n\
The system will use the environment variable ``IDIOLIB`` to	\n\
find ``filename``.						\n\
								\n\
This is the ``load`` primitive.					\n\
")
{
    IDIO_ASSERT (filename);

    /*
     * Test Case: ??
     *
     * load is overridden (more than once) but this would be:
     *
     * load #t
     */
    IDIO_USER_TYPE_ASSERT (string, filename);

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t pc0 = IDIO_THREAD_PC (thr);

    /*
     * Explicitly disable interactive for the duration of a load
     *
     * It will be reset just prior to the prompt.
     */
    idio_job_control_set_interactive (0);
    IDIO r = idio_load_file_name (filename, idio_vm_constants);

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("delete-file", delete_file, (IDIO filename), "filename", "\
does `remove (filename)` succeed?		\n\
						\n\
:param filename: file to delete			\n\
:type filename: string				\n\
						\n\
:return: #t or raises ^file-handle-error otherwise	\n\
")
{
    IDIO_ASSERT (filename);

    /*
     * Test Case: file-handle-errors/delete-file-bad-type.idio
     *
     * delete-file #t
     */
    IDIO_USER_TYPE_ASSERT (string, filename);

    int free_filename_C = 0;

    /*
     * Test Case: file-handle-errors/delete-file-format.idio
     *
     * delete-file (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    char *filename_C = idio_file_handle_filename_string_C (filename, "delete-file", &free_filename_C, IDIO_C_FUNC_LOCATION ());

    IDIO r = idio_S_false;

    if (remove (filename_C)) {
	/*
	 * Test Case: file-handle-errors/delete-file-EACCESS.idio
	 *
	 * tmpdir := (make-tmp-dir)
	 * tmpfile := append-string tmpdir "/foo"
	 * chmod \= tmpdir
	 * delete-file tmpfile
	 *
	 * NB Needs wrapping with unwind-protect to restore
	 * permissions and rmdir tmpdir
	 */
	if (free_filename_C) {
	    IDIO_GC_FREE (filename_C);
	}

	idio_file_handle_filename_delete_error (filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    } else {
	r = idio_S_true;
    }

    if (free_filename_C) {
	IDIO_GC_FREE (filename_C);
    }

    return r;
}

void idio_file_handle_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (open_file_handle_from_fd);
    IDIO_ADD_PRIMITIVE (open_input_file_handle_from_fd);
    IDIO_ADD_PRIMITIVE (open_output_file_handle_from_fd);
    IDIO_ADD_PRIMITIVE (open_input_pipe_handle);
    IDIO_ADD_PRIMITIVE (open_output_pipe_handle);
    IDIO_ADD_PRIMITIVE (open_file_handle);
    IDIO_ADD_PRIMITIVE (open_input_file_handle);
    IDIO_ADD_PRIMITIVE (open_output_file_handle);
    IDIO_ADD_PRIMITIVE (file_handlep);
    IDIO_ADD_PRIMITIVE (input_file_handlep);
    IDIO_ADD_PRIMITIVE (output_file_handlep);
    IDIO_ADD_PRIMITIVE (file_handle_fd);
    IDIO_ADD_PRIMITIVE (fd_handlep);
    IDIO_ADD_PRIMITIVE (input_fd_handlep);
    IDIO_ADD_PRIMITIVE (output_fd_handlep);
    IDIO_ADD_PRIMITIVE (fd_handle_fd);
    IDIO_ADD_PRIMITIVE (pipe_handlep);
    IDIO_ADD_PRIMITIVE (input_pipe_handlep);
    IDIO_ADD_PRIMITIVE (output_pipe_handlep);
    IDIO_ADD_PRIMITIVE (pipe_handle_fd);
    IDIO_ADD_PRIMITIVE (find_lib);
    IDIO_ADD_PRIMITIVE (load);
    IDIO_ADD_PRIMITIVE (delete_file);
    IDIO_ADD_PRIMITIVE (close_fd_handle_on_exec);
}

void idio_final_file_handle ()
{
    /*
     * Code coverage:
     *
     * Should be trying to deprecate these and using the stack
     * instead.
     */
    IDIO fhl = idio_hash_keys_to_list (idio_file_handles);

    while (idio_S_nil != fhl) {
	IDIO fh = IDIO_PAIR_H (fhl);

	if (0 == (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED)) {
	    IDIO_HANDLE_M_CLOSE (fh) (fh);
	}

	fhl = IDIO_PAIR_T (fhl);
    }
}

void idio_init_file_handle ()
{
    idio_module_table_register (idio_file_handle_add_primitives, idio_final_file_handle);

    idio_file_handles = IDIO_HASH_EQP (1<<3);
    idio_gc_protect_auto (idio_file_handles);

    idio_stdin = idio_open_std_file_handle (stdin);
    idio_gc_protect_auto (idio_stdin);
    idio_stdout = idio_open_std_file_handle (stdout);
    idio_gc_protect_auto (idio_stdout);
    idio_stderr = idio_open_std_file_handle (stderr);
    idio_gc_protect_auto (idio_stderr);
}

