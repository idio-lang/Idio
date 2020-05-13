/*
 * Copyright (c) 2015, 2017, 2018, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * libc-wrap.c
 *
 */

#include "idio.h"

IDIO idio_libc_wrap_module = idio_S_nil;
IDIO idio_vm_signal_handler_conditions;
char **idio_libc_signal_names = NULL;
IDIO idio_vm_errno_conditions;
char **idio_libc_errno_names = NULL;
char **idio_libc_rlimit_names = NULL;
static IDIO idio_libc_struct_sigaction = NULL;
static IDIO idio_libc_struct_utsname = NULL;
static IDIO idio_libc_struct_rlimit = NULL;
IDIO idio_libc_struct_stat = NULL;

/*
 * Indexes into structures for direct references
 */
#define IDIO_STRUCT_SIGACTION_SA_HANDLER	0
#define IDIO_STRUCT_SIGACTION_SA_SIGACTION	1
#define IDIO_STRUCT_SIGACTION_SA_MASK		2
#define IDIO_STRUCT_SIGACTION_SA_FLAGS		3

#define IDIO_STRUCT_UTSNAME_SYSNAME		0
#define IDIO_STRUCT_UTSNAME_NODENAME		1
#define IDIO_STRUCT_UTSNAME_RELEASE		2
#define IDIO_STRUCT_UTSNAME_VERSION		3
#define IDIO_STRUCT_UTSNAME_MACHINE		4

#define IDIO_STRUCT_RLIMIT_RLIM_CUR		0
#define IDIO_STRUCT_RLIMIT_RLIM_MAX		1

IDIO idio_libc_export_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_export_symbol_value (symbol, value, idio_libc_wrap_module);
}

IDIO_DEFINE_PRIMITIVE0V ("system-error", libc_system_error, (IDIO args))
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    char *name = "n/k";

    if (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	if (idio_isa_string (h)) {
	    name = idio_string_as_C (h);
	    args = IDIO_PAIR_T (args);
	} else if (idio_isa_symbol (h)) {
	    name = IDIO_SYMBOL_S (h);
	    args = IDIO_PAIR_T (args);
	}
    }

    idio_error_system_errno (name, args, IDIO_C_LOCATION ("system-error"));

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2 ("access", libc_access, (IDIO ipath, IDIO imode))
{
    IDIO_ASSERT (ipath);
    IDIO_ASSERT (imode);
    IDIO_VERIFY_PARAM_TYPE (string, ipath);
    IDIO_VERIFY_PARAM_TYPE (C_int, imode);

    char *path = idio_string_as_C (ipath);
    int mode = IDIO_C_TYPE_INT (imode);

    IDIO r = idio_S_false;

    if (0 == access (path, mode)) {
	r = idio_S_true;
    }

    free (path);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("close", libc_close, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = close (fd);

    if (-1 == r) {
	idio_error_system_errno ("close", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("close"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("dup", libc_dup, (IDIO ioldfd))
{
    IDIO_ASSERT (ioldfd);

    int oldfd = -1;
    if (idio_isa_fixnum (ioldfd)) {
	oldfd = IDIO_FIXNUM_VAL (ioldfd);
    } else if (idio_isa_C_int (ioldfd)) {
	oldfd = IDIO_C_TYPE_INT (ioldfd);
    } else {
	idio_error_param_type ("fixnum|C_int ioldfd", ioldfd, IDIO_C_LOCATION ("dup"));
    }

    int r = dup (oldfd);

    if (-1 == r) {
	idio_error_system_errno ("dup", IDIO_LIST1 (ioldfd), IDIO_C_LOCATION ("dup"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("dup2", libc_dup2, (IDIO ioldfd, IDIO inewfd))
{
    IDIO_ASSERT (ioldfd);
    IDIO_ASSERT (inewfd);

    int oldfd = -1;
    if (idio_isa_fixnum (ioldfd)) {
	oldfd = IDIO_FIXNUM_VAL (ioldfd);
    } else if (idio_isa_C_int (ioldfd)) {
	oldfd = IDIO_C_TYPE_INT (ioldfd);
    } else {
	idio_error_param_type ("fixnum|C_int ioldfd", ioldfd, IDIO_C_LOCATION ("dup"));
    }

    int newfd = -1;
    if (idio_isa_fixnum (inewfd)) {
	newfd = IDIO_FIXNUM_VAL (inewfd);
    } else if (idio_isa_C_int (inewfd)) {
	newfd = IDIO_C_TYPE_INT (inewfd);
    } else {
	idio_error_param_type ("fixnum|C_int inewfd", inewfd, IDIO_C_LOCATION ("dup"));
    }


    int r = dup2 (oldfd, newfd);

    if (-1 == r) {
	idio_error_system_errno ("dup2", IDIO_LIST2 (ioldfd, inewfd), IDIO_C_LOCATION ("dup2"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("exit", libc_exit, (IDIO istatus))
{
    IDIO_ASSERT (istatus);

    int status = 0;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else {
	idio_error_param_type ("fixnum", istatus, IDIO_C_LOCATION ("exit"));
    }

    exit (status);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2V ("fcntl", libc_fcntl, (IDIO ifd, IDIO icmd, IDIO args))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (icmd);
    IDIO_ASSERT (args);

    int fd = 0;
    if (idio_isa_fixnum (ifd)) {
	fd = IDIO_FIXNUM_VAL (ifd);
    } else if (idio_isa_C_int (ifd)) {
	fd = IDIO_C_TYPE_INT (ifd);
    } else {
	idio_error_param_type ("fixnum|C_int ifd", ifd, IDIO_C_LOCATION ("fcntl"));
    }

    int cmd = 0;
    if (idio_isa_C_int (icmd)) {
	cmd = IDIO_C_TYPE_INT (icmd);
    } else {
	idio_error_param_type ("C_int icmd", icmd, IDIO_C_LOCATION ("fcntl"));
    }

    IDIO iarg = idio_list_head (args);

    int r;

    switch (cmd) {
    case F_DUPFD:
	{
	    /*
	     * CentOS 6 i386 fcntl(2) says it wants long but accepts
	     * int
	     */
	    int arg;
	    if (idio_isa_fixnum (iarg)) {
		arg = IDIO_FIXNUM_VAL (iarg);
	    } else if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_LOCATION ("fcntl"));

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
#if defined (F_DUPFD_CLOEXEC)
    case F_DUPFD_CLOEXEC:
	{
	    /*
	     * CentOS 6 i386 fcntl(2) says it wants long but accepts
	     * int
	     */
	    int arg;
	    if (idio_isa_fixnum (iarg)) {
		arg = IDIO_FIXNUM_VAL (iarg);
	    } else if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_LOCATION ("fcntl"));

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
#endif
    case F_GETFD:
	{
	    r = fcntl (fd, cmd);
	}
	break;
    case F_SETFD:
	{
	    int arg;
	    if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		idio_error_param_type ("C_int", iarg, IDIO_C_LOCATION ("fcntl"));

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
    default:
	idio_error_C ("unexpected cmd", IDIO_LIST2 (ifd, icmd), IDIO_C_LOCATION ("fcntl"));

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("fileno", libc_fileno, (IDIO ifilep))
{
    IDIO_ASSERT (ifilep);

    int fd = 0;
    if (idio_isa_file_handle (ifilep)) {
	fd = idio_file_handle_fd (ifilep);
    } else {
	idio_error_param_type ("file-handle", ifilep, IDIO_C_LOCATION ("fileno"));

	return idio_S_notreached;
    }

    return idio_C_int (fd);
}

IDIO_DEFINE_PRIMITIVE0 ("fork", libc_fork, ())
{
    pid_t pid = fork ();

    if (-1 == pid) {
	idio_error_system_errno ("fork", idio_S_nil, IDIO_C_LOCATION ("fork"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("getcwd", libc_getcwd, ())
{
    /*
     * getcwd(3) and its arguments
     *
     * A sensible {size}?
     *
     * PATH_MAX varies: POSIX is 256, CentOS 7 is 4096
     *
     * The Linux man page for realpath(3) suggests that calling
     * pathconf(3) for _PC_PATH_MAX doesn't improve matters a whole
     * bunch as it can return a value that is infeasible to allocate
     * in memory.
     *
     * Some systems (OS X, FreeBSD) suggest getcwd(3) should accept
     * MAXPATHLEN (which is #define'd as PATH_MAX in <sys/param.h>).
     *
     * A NULL {buf}?
     *
     * Some systems (older OS X, FreeBSD) do not support a zero {size}
     * parameter.  If passed a NULL {buf}, those systems seem to
     * allocate as much memory as is required to contain the result,
     * regardless of {size}.
     *
     * On systems that do support a zero {size} parameter then they
     * will limit themselves to allocating a maximum of {size} bytes
     * if passed a NULL {buf} and a non-zero {size}.
     *
     * Given that we can't set {size} to zero on some systems then
     * always set {size} to PATH_MAX which should be be enough.
     *
     * If getcwd(3) returns a value that consumes all of PATH_MAX (or
     * more) then we're doomed to hit other problems in the near
     * future anyway as other parts of the system try to use the
     * result.
     */
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_LOCATION ("getcwd"));
    }

    IDIO r = idio_string_C (cwd);
    free (cwd);

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("getpgrp", libc_getpgrp, ())
{
    pid_t pid = getpgrp ();

    if (-1 == pid) {
	idio_error_system_errno ("getpgrp", idio_S_nil, IDIO_C_LOCATION ("getpgrp"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("getpid", libc_getpid, ())
{
    pid_t pid = getpid ();

    if (-1 == pid) {
	idio_error_system_errno ("getpid", idio_S_nil, IDIO_C_LOCATION ("getpid"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE1 ("isatty", libc_isatty, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = isatty (fd);

    if (0 == r) {
	idio_error_system_errno ("isatty", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("isatty"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("kill", libc_kill, (IDIO ipid, IDIO isig))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (isig);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int sig = IDIO_C_TYPE_INT (isig);

    int r = kill (pid, sig);

    if (-1 == r) {
	idio_error_system_errno ("kill", IDIO_LIST2 (ipid, isig), IDIO_C_LOCATION ("kill"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("mkdtemp", libc_mkdtemp, (IDIO idirname))
{
    IDIO_ASSERT (idirname);
    IDIO_VERIFY_PARAM_TYPE (string, idirname);

    /*
     * XXX mkdtemp() requires a NUL-terminated C string and it will
     * modify the template part.
     *
     * If we are passed a SUBSTRING then we must substitute a
     * NUL-terminated C string and copy the result back.
     */
    char *dirname = idio_string_s (idirname);

    int isa_substring = 0;
    if (idio_isa_substring (idirname)) {
	isa_substring = 1;
	dirname = idio_string_as_C (idirname);
    }

    char *d = mkdtemp (dirname);

    if (NULL == d) {
	idio_error_system_errno ("mkdtemp", IDIO_LIST1 (idirname), IDIO_C_LOCATION ("mkdtemp"));
    }

    if (isa_substring) {
	memcpy (idio_string_s (idirname), dirname, idio_string_blen (idirname));
	free (dirname);
    }

    return idio_string_C (d);
}

IDIO_DEFINE_PRIMITIVE1 ("mkstemp", libc_mkstemp, (IDIO ifilename))
{
    IDIO_ASSERT (ifilename);
    IDIO_VERIFY_PARAM_TYPE (string, ifilename);

    /*
     * XXX mkstemp() requires a NUL-terminated C string and it will
     * modify the template part.
     *
     * If we are passed a SUBSTRING then we must substitute a
     * NUL-terminated C string and copy the result back.
     */
    char *filename = idio_string_s (ifilename);

    int isa_substring = 0;
    if (idio_isa_substring (ifilename)) {
	isa_substring = 1;
	filename = idio_string_as_C (ifilename);
    }

    int r = mkstemp (filename);

    if (-1 == r) {
	idio_error_system_errno ("mkstemp", IDIO_LIST1 (ifilename), IDIO_C_LOCATION ("mkstemp"));
    }

    if (isa_substring) {
	memcpy (idio_string_s (ifilename), filename, idio_string_blen (ifilename));
	free (filename);
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0 ("pipe", libc_pipe, ())
{
    int *pipefd = idio_alloc (2 * sizeof (int));

    int r = pipe (pipefd);

    if (-1 == r) {
	idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_LOCATION ("pipe"));
    }

    return idio_C_pointer_free_me (pipefd);
}

IDIO_DEFINE_PRIMITIVE1 ("pipe-reader", libc_pipe_reader, (IDIO ipipefd))
{
    IDIO_ASSERT (ipipefd);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[0]);
}

IDIO_DEFINE_PRIMITIVE1 ("pipe-writer", libc_pipe_writer, (IDIO ipipefd))
{
    IDIO_ASSERT (ipipefd);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[1]);
}

IDIO_DEFINE_PRIMITIVE1V ("read", libc_read, (IDIO ifd, IDIO icount))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t count = BUFSIZ;

    if (idio_S_nil != icount) {
	icount = IDIO_PAIR_H (icount);

	if (idio_isa_fixnum (icount)) {
	    count = IDIO_FIXNUM_VAL (icount);
	} else if (idio_isa_C_int (icount)) {
	    count = IDIO_C_TYPE_INT (icount);
	} else {
	    idio_error_param_type ("fixnum|C_int", icount, IDIO_C_LOCATION ("read"));
	}
    }

    char buf[count];

    ssize_t n = read (fd, buf, count);

    IDIO r;

    if (n) {
	r = idio_string_C_len (buf, n);
    } else {
	r = idio_S_eof;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("setpgid", libc_setpgid, (IDIO ipid, IDIO ipgid))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (ipgid);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipgid);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    pid_t pgid = IDIO_C_TYPE_INT (ipgid);

    int r = setpgid (pid, pgid);

    if (-1 == r) {
	if (EACCES == errno) {
	    /*
	     * The child has already successfully executed exec() =>
	     * EACCES for us.
	     *
	     * Since the child also ran setpgid() on itself before
	     * calling exec() we should be good.
	     */
	    r = 0;
	} else {
	    idio_error_system_errno ("setpgid", IDIO_LIST2 (ipid, ipgid), IDIO_C_LOCATION ("setpgid"));
	}
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("signal", libc_signal, (IDIO isig, IDIO ifunc))
{
    IDIO_ASSERT (isig);
    IDIO_ASSERT (ifunc);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ifunc);

    int sig = IDIO_C_TYPE_INT (isig);
    void (*func) (int) = IDIO_C_TYPE_POINTER_P (ifunc);

    void (*r) (int) = signal (sig, func);

    if (SIG_ERR == r) {
	idio_error_system_errno ("signal", IDIO_LIST2 (isig, ifunc), IDIO_C_LOCATION ("signal"));
    }

    return idio_C_pointer (r);
}

/*
 * signal-handler isn't a real libc function.  It has been added in
 * to aid spotting if a parent process has kindly sigignored()d
 * SIGPIPE for us:
 *
 * == (c/signal-handler SIGPIPE) SIG_IGN
 *
 * Hopefully we'll find other uses for it.
 */
IDIO_DEFINE_PRIMITIVE1 ("signal-handler", libc_signal_handler, (IDIO isig))
{
    IDIO_ASSERT (isig);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);

    int sig = IDIO_C_TYPE_INT (isig);

    struct sigaction osa;

    if (sigaction (sig, NULL, &osa) < 0) {
	idio_error_system_errno ("sigaction", idio_S_nil, IDIO_C_LOCATION ("signal-handler"));
    }

    /*
     * Our result be be either of:

     void     (*sa_handler)(int);
     void     (*sa_sigaction)(int, siginfo_t *, void *);

     * so, uh, prototype with no args!
     */
    void (*r) ();

    if (osa.sa_flags & SA_SIGINFO) {
	r = osa.sa_sigaction;
    } else {
	r = osa.sa_handler;
    }

    return idio_C_pointer (r);
}

IDIO_DEFINE_PRIMITIVE1 ("sleep", libc_sleep, (IDIO iseconds))
{
    IDIO_ASSERT (iseconds);

    unsigned int seconds = 0;
    if (idio_isa_fixnum (iseconds) &&
	IDIO_FIXNUM_VAL (iseconds) >= 0) {
	seconds = IDIO_FIXNUM_VAL (iseconds);
    } else if (idio_isa_C_uint (iseconds)) {
	seconds = IDIO_C_TYPE_UINT (iseconds);
    } else {
	idio_error_param_type ("unsigned fixnum|C_uint", iseconds, IDIO_C_LOCATION ("sleep"));
    }

    unsigned int r = sleep (seconds);

    return idio_C_uint (r);
}

IDIO idio_libc_stat (IDIO p)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (string, p);

    char *p_C = idio_string_as_C (p);

    struct stat sb;

    if (stat (p_C, &sb) == -1) {
	idio_error_system_errno ("stat", IDIO_LIST1 (p), IDIO_C_LOCATION ("idio_libc_stat"));
    }

    /*
     * XXX idio_C_uint for everything?  We should know more.
     */
    IDIO r = idio_struct_instance (idio_libc_struct_stat,
				   idio_pair (idio_C_uint (sb.st_dev),
				   idio_pair (idio_C_uint (sb.st_ino),
				   idio_pair (idio_C_uint (sb.st_mode),
				   idio_pair (idio_C_uint (sb.st_nlink),
				   idio_pair (idio_C_uint (sb.st_uid),
				   idio_pair (idio_C_uint (sb.st_gid),
				   idio_pair (idio_C_uint (sb.st_rdev),
				   idio_pair (idio_C_uint (sb.st_size),
				   idio_pair (idio_C_uint (sb.st_blksize),
				   idio_pair (idio_C_uint (sb.st_blocks),
				   idio_pair (idio_C_uint (sb.st_atime),
				   idio_pair (idio_C_uint (sb.st_mtime),
				   idio_pair (idio_C_uint (sb.st_ctime),
				   idio_S_nil))))))))))))));
    free (p_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("stat", libc_stat, (IDIO p))
{
    IDIO_ASSERT (p);
    IDIO_VERIFY_PARAM_TYPE (string, p);

    return idio_libc_stat (p);
}

IDIO_DEFINE_PRIMITIVE1 ("strerror", libc_strerror, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);

    int errnum = 0;
    if (idio_isa_fixnum (ierrnum)) {
	errnum = IDIO_FIXNUM_VAL (ierrnum);
    } else if (idio_isa_C_int (ierrnum)) {
	errnum = IDIO_C_TYPE_INT (ierrnum);
    } else {
	idio_error_param_type ("unsigned fixnum|C_int", ierrnum, IDIO_C_LOCATION ("strerror"));
    }

    char *r = strerror (errnum);

    return idio_string_C (r);
}

IDIO_DEFINE_PRIMITIVE1 ("strsignal", libc_strsignal, (IDIO isignum))
{
    IDIO_ASSERT (isignum);

    int signum = 0;
    if (idio_isa_fixnum (isignum)) {
	signum = IDIO_FIXNUM_VAL (isignum);
    } else if (idio_isa_C_int (isignum)) {
	signum = IDIO_C_TYPE_INT (isignum);
    } else {
	idio_error_param_type ("unsigned fixnum|C_int", isignum, IDIO_C_LOCATION ("strsignal"));
    }

    char *r = strsignal (signum);

    return idio_string_C (r);
}

IDIO_DEFINE_PRIMITIVE1 ("tcgetattr", libc_tcgetattr, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    struct termios *tcattrs = idio_alloc (sizeof (struct termios));
    int r = tcgetattr (fd, tcattrs);

    if (-1 == r) {
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("tcgetattr"));
    }

    return idio_C_pointer_free_me (tcattrs);
}

IDIO_DEFINE_PRIMITIVE1 ("tcgetpgrp", libc_tcgetpgrp, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    pid_t pid = tcgetpgrp (fd);

    if (-1 == pid) {
	idio_error_system_errno ("tcgetpgrp", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("tcgetpgrp"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE3 ("tcsetattr", libc_tcsetattr, (IDIO ifd, IDIO ioptions, IDIO itcattrs))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ioptions);
    IDIO_ASSERT (itcattrs);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ioptions);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, itcattrs);

    int fd = IDIO_C_TYPE_INT (ifd);
    int options = IDIO_C_TYPE_INT (ioptions);
    struct termios *tcattrs = IDIO_C_TYPE_POINTER_P (itcattrs);

    int r = tcsetattr (fd, options, tcattrs);

    if (-1 == r) {
	idio_error_system_errno ("tcsetattr", IDIO_LIST3 (ifd, ioptions, itcattrs), IDIO_C_LOCATION ("tcsetattr"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("tcsetpgrp", libc_tcsetpgrp, (IDIO ifd, IDIO ipgrp))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ipgrp);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipgrp);

    int fd = IDIO_C_TYPE_INT (ifd);
    pid_t pgrp = IDIO_C_TYPE_INT (ipgrp);


    int r = tcsetpgrp (fd, pgrp);

    if (-1 == r) {
	idio_error_system_errno ("tcsetpgrp", IDIO_LIST2 (ifd, ipgrp), IDIO_C_LOCATION ("tcsetpgrp"));
    }

    return idio_C_int (r);
}

IDIO idio_libc_uname ()
{
    struct utsname u;

    if (uname (&u) == -1) {
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_LOCATION ("idio_libc_uname"));
    }

    return idio_struct_instance (idio_libc_struct_utsname, IDIO_LIST5 (idio_string_C (u.sysname),
								       idio_string_C (u.nodename),
								       idio_string_C (u.release),
								       idio_string_C (u.version),
								       idio_string_C (u.machine)));
}

IDIO_DEFINE_PRIMITIVE0 ("uname", libc_uname, ())
{
    struct utsname *up;
    up = idio_alloc (sizeof (struct utsname));

    if (uname (up) == -1) {
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_LOCATION ("uname"));
    }

    return idio_C_pointer_free_me (up);
}

IDIO_DEFINE_PRIMITIVE1 ("unlink", libc_unlink, (IDIO ipath))
{
    IDIO_ASSERT (ipath);
    IDIO_VERIFY_PARAM_TYPE (string, ipath);

    char *path = idio_string_as_C (ipath);

    int r = unlink (path);

    free (path);

    if (-1 == r) {
	idio_error_system_errno ("unlink", IDIO_LIST1 (ipath), IDIO_C_LOCATION ("unlink"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("waitpid", libc_waitpid, (IDIO ipid, IDIO ioptions))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (ioptions);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, ioptions);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int options = IDIO_C_TYPE_INT (ioptions);

    int *statusp = idio_alloc (sizeof (int));

    pid_t r = waitpid (pid, statusp, options);

    if (-1 == r) {
	if (ECHILD == errno) {
	    return IDIO_LIST2 (idio_C_int (0), idio_S_nil);
	}
	idio_error_system_errno ("waitpid", IDIO_LIST2 (ipid, ioptions), IDIO_C_LOCATION ("waitpid"));
    }

    IDIO istatus = idio_C_pointer_free_me (statusp);

    return IDIO_LIST2 (idio_C_int (r), istatus);
}

IDIO_DEFINE_PRIMITIVE1 ("WEXITSTATUS", libc_WEXITSTATUS, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WEXITSTATUS (*statusp));
}

IDIO_DEFINE_PRIMITIVE1 ("WIFEXITED", libc_WIFEXITED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFEXITED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("WIFSIGNALED", libc_WIFSIGNALED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSIGNALED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("WIFSTOPPED", libc_WIFSTOPPED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSTOPPED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("WTERMSIG", libc_WTERMSIG, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WTERMSIG (*statusp));
}

IDIO_DEFINE_PRIMITIVE2_DS ("write", libc_write, (IDIO ifd, IDIO istr), "fd str", "\
write (fd, str)							\n\
a wrapper to libc write (2)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:param str: string						\n\
:type str: string						\n\
:return: number of bytes written				\n\
:rtype: integer							\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (istr);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (string, istr);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t blen = idio_string_blen (istr);

    /*
     * A rare occasion we can use idio_string_s() as we are also using
     * blen.
     */
    ssize_t n = write (fd, idio_string_s (istr), blen);

    if (-1 == n) {
	idio_error_system_errno ("write", IDIO_LIST2 (ifd, istr), IDIO_C_LOCATION ("write"));
    }

    return idio_integer (n);
}

/*
 * idio_libc_set_signal_names
 *
 * Surprisingly, despite using the macro value, say, SIGINT in code
 * there is no way to get the descriptive string "SIGINT" back out of
 * the system.  strsignal(3) provides the helpful string "Interrupt".
 *
 * Bash's support/signames.c leads the way
 */

/*
 * How many chars in SIGRTMIN+n ?
 *
 * strlen ("SIGRTMIN+") == 9
 * +1 for NUL == 10 chars
 *
 * IDIO_LIBC_SIGNAMELEN - 10 => max n of 9999
 */
#define IDIO_LIBC_SIGNAMELEN 14

static void idio_libc_set_signal_names ()
{
    idio_libc_signal_names = idio_alloc ((IDIO_LIBC_NSIG + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG; i++) {
	idio_libc_signal_names[i] = idio_alloc (IDIO_LIBC_SIGNAMELEN);
	*(idio_libc_signal_names[i]) = '\0';
    }
    idio_libc_signal_names[i] = NULL;

    /*
     * Linux and SIGRTMIN are a slippery pair.  To be fair, the header
     * file, asm-generic/signal.h, says "These should not be
     * considered constants from userland." [ie. userland, us, should
     * not consider these values as constants] but immediately defines
     * SIGRTMIN as 32.
     *
     * In practice, the code sees SIGRTMIN as 34 and a quick grep has
     * bits/signum.h #define'ing SIGRTMIN as (__libc_current_sigrtmin
     * ()).
     *
     * Which is neither clear nor portable (assuming bits/signum.h
     * speaks the truth).
     */
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    int rtmin = SIGRTMIN;
    int rtmax = SIGRTMAX;
    if (rtmax > rtmin &&
	(rtmax - rtmin) > 7) {
        sprintf (idio_libc_signal_names[SIGRTMIN], "SIGRTMIN");
	sprintf (idio_libc_signal_names[SIGRTMAX], "SIGRTMAX");

	int rtmid = (rtmax - rtmin) / 2;
	int rtdiff = (rtmax - rtmin) - (rtmid * 2);
	if (rtdiff) {
	    rtmid++;
	}

	for (i = 1; i < rtmid ; i++) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	    sprintf (idio_libc_signal_names[rtmax - i], "SIGRTMAX-%d", i);
	}

	/*
	 * Can have an extra SIGRTMIN+n if there's an odd number --
	 * don't forget it is SIGRTMIN -> SIGRTMAX *inclusive* so
	 * there is an off-by-one error tempting us here...
	 */
	if (0 == rtdiff) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	}
    }
#endif

#if defined(SIGHUP)
    IDIO_LIBC_SIGNAL (SIGHUP)
#endif

#if defined(SIGINT)
    IDIO_LIBC_SIGNAL (SIGINT)
#endif

#if defined(SIGQUIT)
    IDIO_LIBC_SIGNAL (SIGQUIT)
#endif

#if defined(SIGILL)
    IDIO_LIBC_SIGNAL (SIGILL)
#endif

#if defined(SIGTRAP)
    IDIO_LIBC_SIGNAL (SIGTRAP)
#endif

#if defined(SIGIOT)
    IDIO_LIBC_SIGNAL (SIGIOT)
#endif

#if defined(SIGEMT)
    IDIO_LIBC_SIGNAL (SIGEMT)
#endif

#if defined(SIGFPE)
    IDIO_LIBC_SIGNAL (SIGFPE)
#endif

#if defined(SIGKILL)
    IDIO_LIBC_SIGNAL (SIGKILL)
#endif

#if defined(SIGBUS)
    IDIO_LIBC_SIGNAL (SIGBUS)
#endif

#if defined(SIGSEGV)
    IDIO_LIBC_SIGNAL (SIGSEGV)
#endif

#if defined(SIGSYS)
    IDIO_LIBC_SIGNAL (SIGSYS)
#endif

#if defined(SIGPIPE)
    IDIO_LIBC_SIGNAL (SIGPIPE)
#endif

#if defined(SIGALRM)
    IDIO_LIBC_SIGNAL (SIGALRM)
#endif

#if defined(SIGTERM)
    IDIO_LIBC_SIGNAL (SIGTERM)
#endif

#if defined(SIGUSR1)
    IDIO_LIBC_SIGNAL (SIGUSR1)
#endif

#if defined(SIGUSR2)
    IDIO_LIBC_SIGNAL (SIGUSR2)
#endif

#if defined(SIGCHLD)
    IDIO_LIBC_SIGNAL (SIGCHLD)
#endif

#if defined(SIGPWR)
    IDIO_LIBC_SIGNAL (SIGPWR)
#endif

#if defined(SIGWINCH)
    IDIO_LIBC_SIGNAL (SIGWINCH)
#endif

#if defined(SIGURG)
    IDIO_LIBC_SIGNAL (SIGURG)
#endif

#if defined(SIGPOLL)
    IDIO_LIBC_SIGNAL (SIGPOLL)
#endif

#if defined(SIGSTOP)
    IDIO_LIBC_SIGNAL (SIGSTOP)
#endif

#if defined(SIGTSTP)
    IDIO_LIBC_SIGNAL (SIGTSTP)
#endif

#if defined(SIGCONT)
    IDIO_LIBC_SIGNAL (SIGCONT)
#endif

#if defined(SIGTTIN)
    IDIO_LIBC_SIGNAL (SIGTTIN)
#endif

#if defined(SIGTTOU)
    IDIO_LIBC_SIGNAL (SIGTTOU)
#endif

#if defined(SIGVTALRM)
    IDIO_LIBC_SIGNAL (SIGVTALRM)
#endif

#if defined(SIGPROF)
    IDIO_LIBC_SIGNAL (SIGPROF)
#endif

#if defined(SIGXCPU)
    IDIO_LIBC_SIGNAL (SIGXCPU)
#endif

#if defined(SIGXFSZ)
    IDIO_LIBC_SIGNAL (SIGXFSZ)
#endif

    /* SunOS */
#if defined(SIGWAITING)
    IDIO_LIBC_SIGNAL (SIGWAITING)
#endif

    /* SunOS */
#if defined(SIGLWP)
    IDIO_LIBC_SIGNAL (SIGLWP)
#endif

    /* SunOS */
#if defined(SIGFREEZE)
    IDIO_LIBC_SIGNAL (SIGFREEZE)
#endif

    /* SunOS */
#if defined(SIGTHAW)
    IDIO_LIBC_SIGNAL (SIGTHAW)
#endif

    /* SunOS */
#if defined(SIGCANCEL)
    IDIO_LIBC_SIGNAL (SIGCANCEL)
#endif

    /* SunOS */
#if defined(SIGLOST)
    IDIO_LIBC_SIGNAL (SIGLOST)
#endif

    /* SunOS */
#if defined(SIGXRES)
    IDIO_LIBC_SIGNAL (SIGXRES)
#endif

    /* SunOS */
#if defined(SIGJVM1)
    IDIO_LIBC_SIGNAL (SIGJVM1)
#endif

    /* SunOS */
#if defined(SIGJVM2)
    IDIO_LIBC_SIGNAL (SIGJVM2)
#endif

    /* OpenIndiana */
#if defined(SIGINFO)
    IDIO_LIBC_SIGNAL (SIGINFO)
#endif

    /* Linux */
#if defined(SIGSTKFLT)
    IDIO_LIBC_SIGNAL (SIGSTKFLT)
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FSIG ; i < IDIO_LIBC_NSIG ; i++) {
	if ('\0' == *(idio_libc_signal_names[i])) {
	    char sig_name[IDIO_LIBC_SIGNAMELEN + 3];
	    sprintf (sig_name, "SIGJUNK%d", i);
	    IDIO_LIBC_SIGNAL_NAME_ONLY (sig_name, i)
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped signal numbers:\n");
		fprintf (stderr, " %3.3s %-*.*s %s\n", "id", IDIO_LIBC_SIGNAMELEN, IDIO_LIBC_SIGNAMELEN, "Idio name", "strsignal()");
	    }
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_SIGNAMELEN, sig_name, strsignal (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

/*
 * (According to Bash's .../builtins/common.c)
 *
 * POSIX.2 says the signal names are displayed without the `SIG'
 * prefix.
 *
 * We'll support both.  Functions like ~signal~ says the full signal
 * name and ~sig~ says the POSIX.2 format (arguably it should be ~nal~
 * as we've stripped off the "sig" part...can't be witty in the API,
 * though!)
 */
char *idio_libc_sig_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	idio_error_param_type ("int < NSIG (or SIGRTMAX)", idio_C_int (signum), IDIO_C_LOCATION ("idio_libc_sig_name"));
    }

    char *signame = idio_libc_signal_names[signum];

    if (strncmp (signame, "SIG", 3) == 0) {
	return (signame + 3);
    } else {
	return signame;
    }
}

char *idio_libc_signal_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	idio_error_param_type ("int < NSIG (or SIGRTMAX)", idio_C_int (signum), IDIO_C_LOCATION ("idio_libc_signal_name"));
    }

    return idio_libc_signal_names[signum];
}

IDIO_DEFINE_PRIMITIVE1 ("sig-name", libc_sig_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_sig_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0 ("sig-names", libc_sig_names, ())
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_sig_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE1 ("signal-name", libc_signal_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_signal_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0 ("signal-names", libc_signal_names, ())
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_signal_name (i))), r);
    }

    return idio_list_reverse (r);
}

/*
 * idio_libc_set_errno_names
 *
 * Surprisingly, despite using the macro value, say, ECHILD in code
 * there is no way to get the descriptive string "ECHILD" back out of
 * the system.  strerror(3) provides the helpful string "No child
 * processes".
 *
 * We must follow the path blazed above for signals
 */

/*
 * How many errnos are there?
 *
 * FreeBSD and OS X define ELAST but many others do not.
 *
 * Linux's errno(3) suggests we might be referring to the set
 * including *at least* Linux's own definitions, those of POSIX.1-2001
 * and those of C99.  (Open)Solaris' Intro(2) mentions the Single Unix
 * Specification as the source of all errnos and has gaps in its own
 * definitions for some BSD Networking (100-119) and XENIX (135-142).
 *
 * And there's a stragglers found in anything else that we stumble
 * across.
 *
 * CentOS 6&7	EHWPOISON	133
 * OI 151a8	ESTALE		151
 * OS X 9.8.0	ENOPOLICY	103	aka ELAST
 * OS X 14.4.0	EQFULL		106	aka ELAST
 * FreeBSD 10	EOWNERDEAD	96	aka ELAST
 * Ubuntu 14	EHWPOISON	133
 * Debian 8	EHWPOISON	133
 *
 */

#define IDIO_LIBC_FERRNO 1

#if defined (BSD)
#define IDIO_LIBC_NERRNO (ELAST + 1)
#elif defined (__linux__)
#define IDIO_LIBC_NERRNO (EHWPOISON + 1)
#elif defined (__APPLE__) && defined (__MACH__)
#define IDIO_LIBC_NERRNO (ELAST + 1)
#elif defined (__sun) && defined (__SVR4)
#define IDIO_LIBC_NERRNO (ESTALE + 1)
#else
#error Cannot define IDIO_LIBC_NERRNO
#endif

/*
 * How many chars in E-somename- ?
 *
 * Empirical study suggests ENOTRECOVERABLE, EPROTONOSUPPORT and
 * ESOCKTNOSUPPORT are the longest E-names at 15 chars each.
 */
#define IDIO_LIBC_ERRNAMELEN 20

static void idio_libc_set_errno_names ()
{
    idio_libc_errno_names = idio_alloc ((IDIO_LIBC_NERRNO + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
	idio_libc_errno_names[i] = idio_alloc (IDIO_LIBC_ERRNAMELEN);
	*(idio_libc_errno_names[i]) = '\0';
    }
    idio_libc_errno_names[i] = NULL;

    idio_libc_errno_names[0] = "E0";

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (E2BIG)
    IDIO_LIBC_ERRNO (E2BIG);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EACCES)
    IDIO_LIBC_ERRNO (EACCES);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRINUSE)
    IDIO_LIBC_ERRNO (EADDRINUSE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRNOTAVAIL)
    IDIO_LIBC_ERRNO (EADDRNOTAVAIL);
#endif

    /* Linux, Solaris */
#if defined (EADV)
    IDIO_LIBC_ERRNO (EADV);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAFNOSUPPORT)
    IDIO_LIBC_ERRNO (EAFNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAGAIN)
    IDIO_LIBC_ERRNO (EAGAIN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EALREADY)
    IDIO_LIBC_ERRNO (EALREADY);
#endif

    /* FreeBSD, OSX */
#if defined (EAUTH)
    IDIO_LIBC_ERRNO (EAUTH);
#endif

    /* OSX */
#if defined (EBADARCH)
    IDIO_LIBC_ERRNO (EBADARCH);
#endif

    /* Linux, Solaris */
#if defined (EBADE)
    IDIO_LIBC_ERRNO (EBADE);
#endif

    /* OSX */
#if defined (EBADEXEC)
    IDIO_LIBC_ERRNO (EBADEXEC);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADF)
    IDIO_LIBC_ERRNO (EBADF);
#endif

    /* Linux, Solaris */
#if defined (EBADFD)
    IDIO_LIBC_ERRNO (EBADFD);
#endif

    /* OSX */
#if defined (EBADMACHO)
    IDIO_LIBC_ERRNO (EBADMACHO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADMSG)
    IDIO_LIBC_ERRNO (EBADMSG);
#endif

    /* Linux, Solaris */
#if defined (EBADR)
    IDIO_LIBC_ERRNO (EBADR);
#endif

    /* FreeBSD, OSX */
#if defined (EBADRPC)
    IDIO_LIBC_ERRNO (EBADRPC);
#endif

    /* Linux, Solaris */
#if defined (EBADRQC)
    IDIO_LIBC_ERRNO (EBADRQC);
#endif

    /* Linux, Solaris */
#if defined (EBADSLT)
    IDIO_LIBC_ERRNO (EBADSLT);
#endif

    /* Linux, Solaris */
#if defined (EBFONT)
    IDIO_LIBC_ERRNO (EBFONT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBUSY)
    IDIO_LIBC_ERRNO (EBUSY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECANCELED)
    IDIO_LIBC_ERRNO (ECANCELED);
#endif

    /* FreeBSD */
#if defined (ECAPMODE)
    IDIO_LIBC_ERRNO (ECAPMODE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECHILD)
    IDIO_LIBC_ERRNO (ECHILD);
#endif

    /* Linux, Solaris */
#if defined (ECHRNG)
    IDIO_LIBC_ERRNO (ECHRNG);
#endif

    /* Linux, Solaris */
#if defined (ECOMM)
    IDIO_LIBC_ERRNO (ECOMM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNABORTED)
    IDIO_LIBC_ERRNO (ECONNABORTED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNREFUSED)
    IDIO_LIBC_ERRNO (ECONNREFUSED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNRESET)
    IDIO_LIBC_ERRNO (ECONNRESET);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDEADLK)
    IDIO_LIBC_ERRNO (EDEADLK);
#endif

    /* Linux, Solaris */
#if defined (EDEADLOCK)
    IDIO_LIBC_ERRNO (EDEADLOCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDESTADDRREQ)
    IDIO_LIBC_ERRNO (EDESTADDRREQ);
#endif

    /* OSX */
#if defined (EDEVERR)
    IDIO_LIBC_ERRNO (EDEVERR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDOM)
    IDIO_LIBC_ERRNO (EDOM);
#endif

    /* FreeBSD */
#if defined (EDOOFUS)
    IDIO_LIBC_ERRNO (EDOOFUS);
#endif

    /* Linux */
#if defined (EDOTDOT)
    IDIO_LIBC_ERRNO (EDOTDOT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDQUOT)
    IDIO_LIBC_ERRNO (EDQUOT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EEXIST)
    IDIO_LIBC_ERRNO (EEXIST);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFAULT)
    IDIO_LIBC_ERRNO (EFAULT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFBIG)
    IDIO_LIBC_ERRNO (EFBIG);
#endif

    /* FreeBSD, OSX */
#if defined (EFTYPE)
    IDIO_LIBC_ERRNO (EFTYPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTDOWN)
    IDIO_LIBC_ERRNO (EHOSTDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTUNREACH)
    IDIO_LIBC_ERRNO (EHOSTUNREACH);
#endif

    /* Linux */
#if defined (EHWPOISON)
    IDIO_LIBC_ERRNO (EHWPOISON);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIDRM)
    IDIO_LIBC_ERRNO (EIDRM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EILSEQ)
    IDIO_LIBC_ERRNO (EILSEQ);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINPROGRESS)
    IDIO_LIBC_ERRNO (EINPROGRESS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINTR)
    IDIO_LIBC_ERRNO (EINTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINVAL)
    IDIO_LIBC_ERRNO (EINVAL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIO)
    IDIO_LIBC_ERRNO (EIO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISCONN)
    IDIO_LIBC_ERRNO (EISCONN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISDIR)
    IDIO_LIBC_ERRNO (EISDIR);
#endif

    /* Linux */
#if defined (EISNAM)
    IDIO_LIBC_ERRNO (EISNAM);
#endif

    /* Linux */
#if defined (EKEYEXPIRED)
    IDIO_LIBC_ERRNO (EKEYEXPIRED);
#endif

    /* Linux */
#if defined (EKEYREJECTED)
    IDIO_LIBC_ERRNO (EKEYREJECTED);
#endif

    /* Linux */
#if defined (EKEYREVOKED)
    IDIO_LIBC_ERRNO (EKEYREVOKED);
#endif

    /* Linux, Solaris */
#if defined (EL2HLT)
    IDIO_LIBC_ERRNO (EL2HLT);
#endif

    /* Linux, Solaris */
#if defined (EL2NSYNC)
    IDIO_LIBC_ERRNO (EL2NSYNC);
#endif

    /* Linux, Solaris */
#if defined (EL3HLT)
    IDIO_LIBC_ERRNO (EL3HLT);
#endif

    /* Linux, Solaris */
#if defined (EL3RST)
    IDIO_LIBC_ERRNO (EL3RST);
#endif

    /* Linux, Solaris */
#if defined (ELIBACC)
    IDIO_LIBC_ERRNO (ELIBACC);
#endif

    /* Linux, Solaris */
#if defined (ELIBBAD)
    IDIO_LIBC_ERRNO (ELIBBAD);
#endif

    /* Linux, Solaris */
#if defined (ELIBEXEC)
    IDIO_LIBC_ERRNO (ELIBEXEC);
#endif

    /* Linux, Solaris */
#if defined (ELIBMAX)
    IDIO_LIBC_ERRNO (ELIBMAX);
#endif

    /* Linux, Solaris */
#if defined (ELIBSCN)
    IDIO_LIBC_ERRNO (ELIBSCN);
#endif

    /* Linux, Solaris */
#if defined (ELNRNG)
    IDIO_LIBC_ERRNO (ELNRNG);
#endif

    /* Solaris */
#if defined (ELOCKUNMAPPED)
    IDIO_LIBC_ERRNO (ELOCKUNMAPPED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ELOOP)
    IDIO_LIBC_ERRNO (ELOOP);
#endif

    /* Linux */
#if defined (EMEDIUMTYPE)
    IDIO_LIBC_ERRNO (EMEDIUMTYPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMFILE)
    IDIO_LIBC_ERRNO (EMFILE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMLINK)
    IDIO_LIBC_ERRNO (EMLINK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMSGSIZE)
    IDIO_LIBC_ERRNO (EMSGSIZE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMULTIHOP)
    IDIO_LIBC_ERRNO (EMULTIHOP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENAMETOOLONG)
    IDIO_LIBC_ERRNO (ENAMETOOLONG);
#endif

    /* Linux */
#if defined (ENAVAIL)
    IDIO_LIBC_ERRNO (ENAVAIL);
#endif

    /* FreeBSD, OSX */
#if defined (ENEEDAUTH)
    IDIO_LIBC_ERRNO (ENEEDAUTH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETDOWN)
    IDIO_LIBC_ERRNO (ENETDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETRESET)
    IDIO_LIBC_ERRNO (ENETRESET);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETUNREACH)
    IDIO_LIBC_ERRNO (ENETUNREACH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENFILE)
    IDIO_LIBC_ERRNO (ENFILE);
#endif

    /* Linux, Solaris */
#if defined (ENOANO)
    IDIO_LIBC_ERRNO (ENOANO);
#endif

    /* FreeBSD, OSX */
#if defined (ENOATTR)
    IDIO_LIBC_ERRNO (ENOATTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOBUFS)
    IDIO_LIBC_ERRNO (ENOBUFS);
#endif

    /* Linux, Solaris */
#if defined (ENOCSI)
    IDIO_LIBC_ERRNO (ENOCSI);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENODATA)
    IDIO_LIBC_ERRNO (ENODATA);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENODEV)
    IDIO_LIBC_ERRNO (ENODEV);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOENT)
    IDIO_LIBC_ERRNO (ENOENT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOEXEC)
    IDIO_LIBC_ERRNO (ENOEXEC);
#endif

    /* Linux */
#if defined (ENOKEY)
    IDIO_LIBC_ERRNO (ENOKEY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLCK)
    IDIO_LIBC_ERRNO (ENOLCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLINK)
    IDIO_LIBC_ERRNO (ENOLINK);
#endif

    /* Linux */
#if defined (ENOMEDIUM)
    IDIO_LIBC_ERRNO (ENOMEDIUM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMEM)
    IDIO_LIBC_ERRNO (ENOMEM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMSG)
    IDIO_LIBC_ERRNO (ENOMSG);
#endif

    /* Linux, Solaris */
#if defined (ENONET)
    IDIO_LIBC_ERRNO (ENONET);
#endif

    /* Linux, Solaris */
#if defined (ENOPKG)
    IDIO_LIBC_ERRNO (ENOPKG);
#endif

    /* OSX */
#if defined (ENOPOLICY)
    IDIO_LIBC_ERRNO (ENOPOLICY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOPROTOOPT)
    IDIO_LIBC_ERRNO (ENOPROTOOPT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSPC)
    IDIO_LIBC_ERRNO (ENOSPC);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSR)
    IDIO_LIBC_ERRNO (ENOSR);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSTR)
    IDIO_LIBC_ERRNO (ENOSTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSYS)
    IDIO_LIBC_ERRNO (ENOSYS);
#endif

    /* Solaris */
#if defined (ENOTACTIVE)
    IDIO_LIBC_ERRNO (ENOTACTIVE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTBLK)
    IDIO_LIBC_ERRNO (ENOTBLK);
#endif

    /* FreeBSD */
#if defined (ENOTCAPABLE)
    IDIO_LIBC_ERRNO (ENOTCAPABLE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTCONN)
    IDIO_LIBC_ERRNO (ENOTCONN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTDIR)
    IDIO_LIBC_ERRNO (ENOTDIR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTEMPTY)
    IDIO_LIBC_ERRNO (ENOTEMPTY);
#endif

    /* Linux */
#if defined (ENOTNAM)
    IDIO_LIBC_ERRNO (ENOTNAM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTRECOVERABLE)
    IDIO_LIBC_ERRNO (ENOTRECOVERABLE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTSOCK)
    IDIO_LIBC_ERRNO (ENOTSOCK);
#endif

    /* FreeBSD, OSX, Solaris */
#if defined (ENOTSUP)
    IDIO_LIBC_ERRNO (ENOTSUP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTTY)
    IDIO_LIBC_ERRNO (ENOTTY);
#endif

    /* Linux, Solaris */
#if defined (ENOTUNIQ)
    IDIO_LIBC_ERRNO (ENOTUNIQ);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENXIO)
    IDIO_LIBC_ERRNO (ENXIO);
#endif

    /* FreeBSD, Linux, OSX, OSX, Solaris */
#if defined (EOPNOTSUPP)
    IDIO_LIBC_ERRNO (EOPNOTSUPP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOVERFLOW)
    IDIO_LIBC_ERRNO (EOVERFLOW);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOWNERDEAD)
    IDIO_LIBC_ERRNO (EOWNERDEAD);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPERM)
    IDIO_LIBC_ERRNO (EPERM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPFNOSUPPORT)
    IDIO_LIBC_ERRNO (EPFNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPIPE)
    IDIO_LIBC_ERRNO (EPIPE);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCLIM)
    IDIO_LIBC_ERRNO (EPROCLIM);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCUNAVAIL)
    IDIO_LIBC_ERRNO (EPROCUNAVAIL);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGMISMATCH)
    IDIO_LIBC_ERRNO (EPROGMISMATCH);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGUNAVAIL)
    IDIO_LIBC_ERRNO (EPROGUNAVAIL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTO)
    IDIO_LIBC_ERRNO (EPROTO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTONOSUPPORT)
    IDIO_LIBC_ERRNO (EPROTONOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTOTYPE)
    IDIO_LIBC_ERRNO (EPROTOTYPE);
#endif

    /* OSX */
#if defined (EPWROFF)
    IDIO_LIBC_ERRNO (EPWROFF);
#endif

    /* OSX */
#if defined (EQFULL)
    IDIO_LIBC_ERRNO (EQFULL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ERANGE)
    IDIO_LIBC_ERRNO (ERANGE);
#endif

    /* Linux, Solaris */
#if defined (EREMCHG)
    IDIO_LIBC_ERRNO (EREMCHG);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EREMOTE)
    IDIO_LIBC_ERRNO (EREMOTE);
#endif

    /* Linux */
#if defined (EREMOTEIO)
    IDIO_LIBC_ERRNO (EREMOTEIO);
#endif

    /* Linux, Solaris */
#if defined (ERESTART)
    IDIO_LIBC_ERRNO (ERESTART);
#endif

    /* Linux */
#if defined (ERFKILL)
    IDIO_LIBC_ERRNO (ERFKILL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EROFS)
    IDIO_LIBC_ERRNO (EROFS);
#endif

    /* FreeBSD, OSX */
#if defined (ERPCMISMATCH)
    IDIO_LIBC_ERRNO (ERPCMISMATCH);
#endif

    /* OSX */
#if defined (ESHLIBVERS)
    IDIO_LIBC_ERRNO (ESHLIBVERS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESHUTDOWN)
    IDIO_LIBC_ERRNO (ESHUTDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESOCKTNOSUPPORT)
    IDIO_LIBC_ERRNO (ESOCKTNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESPIPE)
    IDIO_LIBC_ERRNO (ESPIPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESRCH)
    IDIO_LIBC_ERRNO (ESRCH);
#endif

    /* Linux, Solaris */
#if defined (ESRMNT)
    IDIO_LIBC_ERRNO (ESRMNT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESTALE)
    IDIO_LIBC_ERRNO (ESTALE);
#endif

    /* Linux, Solaris */
#if defined (ESTRPIPE)
    IDIO_LIBC_ERRNO (ESTRPIPE);
#endif

    /* Linux, OSX, Solaris */
#if defined (ETIME)
    IDIO_LIBC_ERRNO (ETIME);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETIMEDOUT)
    IDIO_LIBC_ERRNO (ETIMEDOUT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETOOMANYREFS)
    IDIO_LIBC_ERRNO (ETOOMANYREFS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETXTBSY)
    IDIO_LIBC_ERRNO (ETXTBSY);
#endif

    /* Linux */
#if defined (EUCLEAN)
    IDIO_LIBC_ERRNO (EUCLEAN);
#endif

    /* Linux, Solaris */
#if defined (EUNATCH)
    IDIO_LIBC_ERRNO (EUNATCH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EUSERS)
    IDIO_LIBC_ERRNO (EUSERS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EWOULDBLOCK)
    IDIO_LIBC_ERRNO (EWOULDBLOCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EXDEV)
    IDIO_LIBC_ERRNO (EXDEV);
#endif

    /* Linux, Solaris */
#if defined (EXFULL)
    IDIO_LIBC_ERRNO (EXFULL);
#endif

    /*
     * OpenIndiana anomalies -- strerror returns a non "Error X"
     * string
     *
     * num	errno?		strerror
     *
     * 135	EUCLEAN		Structure needs cleaning
     * 137	ENOTNAM		Not a name file
     * 138	?		Not available
     * 139	EISNAM		Is a name file
     * 140	EREMOTEIO	Remote I/O error
     * 141	?		Reserved for future use
     */

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FERRNO ; i < IDIO_LIBC_NERRNO ; i++) {
	if ('\0' == *(idio_libc_errno_names[i])) {
	    char err_name[IDIO_LIBC_ERRNAMELEN + 2];
	    sprintf (err_name, "ERRUNKNOWN%d", i);
	    IDIO err_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (err_sym, idio_C_int (i));
	    sprintf (idio_libc_errno_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped errno numbers:\n");
		fprintf (stderr, " %3.3s %-*.*s %s\n", "id", IDIO_LIBC_ERRNAMELEN, IDIO_LIBC_ERRNAMELEN, "Idio name", "strerror ()");
	    }
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_ERRNAMELEN, err_name, strerror (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_errno_name (int errnum)
{
    if (errnum < 0 ||
	errnum > IDIO_LIBC_NERRNO) {
	idio_error_param_type ("int < 0 (or > NERRNO)", idio_C_int (errnum), IDIO_C_LOCATION ("idio_libc_errno_name"));
    }

    return idio_libc_errno_names[errnum];
}

IDIO_DEFINE_PRIMITIVE1 ("errno-name", libc_errno_name, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0 ("errno-names", libc_errno_names, ())
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_errno_name (i))), r);
    }

    return idio_list_reverse (r);
}

/*
 * Moral equivalent of strsignal(3) -- identical to errno-name, above.
 */
IDIO_DEFINE_PRIMITIVE1 ("strerrno", libc_strerrno, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0 ("errno/get", libc_errno_get, (void))
{
    return idio_C_int (errno);
}

/*
 * How many rlimits are there?
 *
 * RLIMIT_NLIMITS
 */

#define IDIO_LIBC_FRLIMIT 0
#define IDIO_LIBC_NRLIMIT RLIMIT_NLIMITS

/*
 * How many chars in RLIMIT_-somename- ?
 *
 * Empirical study suggests RLIMIT_SIGPENDING is the longest
 * RLIMIT_-name at 17 chars.
 */
#define IDIO_LIBC_RLIMITNAMELEN 20

static void idio_libc_set_rlimit_names ()
{
    idio_libc_rlimit_names = idio_alloc ((IDIO_LIBC_NRLIMIT + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT; i++) {
	idio_libc_rlimit_names[i] = idio_alloc (IDIO_LIBC_RLIMITNAMELEN);
	*(idio_libc_rlimit_names[i]) = '\0';
    }
    idio_libc_rlimit_names[i] = NULL;

    /* Linux, Solaris */
#if defined (RLIMIT_CPU)
    IDIO_LIBC_RLIMIT (RLIMIT_CPU);
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_FSIZE)
    IDIO_LIBC_RLIMIT (RLIMIT_FSIZE);
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_DATA)
    IDIO_LIBC_RLIMIT (RLIMIT_DATA);
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_STACK)
    IDIO_LIBC_RLIMIT (RLIMIT_STACK);
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_CORE)
    IDIO_LIBC_RLIMIT (RLIMIT_CORE);
#endif

    /* Linux */
#if defined (RLIMIT_RSS)
    IDIO_LIBC_RLIMIT (RLIMIT_RSS);
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_NOFILE)
    IDIO_LIBC_RLIMIT (RLIMIT_NOFILE);
#endif

    /* Solaris */
#if defined (RLIMIT_VMEM)
    IDIO_LIBC_RLIMIT (RLIMIT_VMEM);
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_AS)
    IDIO_LIBC_RLIMIT (RLIMIT_AS);
#endif

    /* Linux */
#if defined (RLIMIT_NPROC)
    IDIO_LIBC_RLIMIT (RLIMIT_NPROC);
#endif

    /* Linux */
#if defined (RLIMIT_MEMLOCK)
    IDIO_LIBC_RLIMIT (RLIMIT_MEMLOCK);
#endif

    /* Linux */
#if defined (RLIMIT_LOCKS)
    IDIO_LIBC_RLIMIT (RLIMIT_LOCKS);
#endif

    /* Linux */
#if defined (RLIMIT_SIGPENDING)
    IDIO_LIBC_RLIMIT (RLIMIT_SIGPENDING);
#endif

    /* Linux */
#if defined (RLIMIT_MSGQUEUE)
    IDIO_LIBC_RLIMIT (RLIMIT_MSGQUEUE);
#endif

    /* Linux */
#if defined (RLIMIT_NICE)
    IDIO_LIBC_RLIMIT (RLIMIT_NICE);
#endif

    /* Linux */
#if defined (RLIMIT_RTPRIO)
    IDIO_LIBC_RLIMIT (RLIMIT_RTPRIO);
#endif

    /* Linux */
#if defined (RLIMIT_RTTIME)
    IDIO_LIBC_RLIMIT (RLIMIT_RTTIME);
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FRLIMIT ; i < IDIO_LIBC_NRLIMIT ; i++) {
	if ('\0' == *(idio_libc_rlimit_names[i])) {
	    char err_name[IDIO_LIBC_RLIMITNAMELEN + 2];
	    sprintf (err_name, "RLIMIT_UNKNOWN%d", i);
	    IDIO rlimit_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (i));
	    sprintf (idio_libc_rlimit_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped rlimit numbers:\n");
		fprintf (stderr, " %3.3s %-*.*s %s\n", "id", IDIO_LIBC_RLIMITNAMELEN, IDIO_LIBC_RLIMITNAMELEN, "Idio name", "strerror ()");
	    }
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_RLIMITNAMELEN, err_name, strerror (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_rlimit_name (int rlim)
{
    if (rlim < IDIO_LIBC_FRLIMIT ||
	rlim > IDIO_LIBC_NRLIMIT) {
	idio_error_param_type ("int < FRLIMIT (or > NRLIMIT)", idio_C_int (rlim), IDIO_C_LOCATION ("idio_libc_rlimit_name"));
    }

    return idio_libc_rlimit_names[rlim];
}

IDIO_DEFINE_PRIMITIVE1_DS ("rlimit-name", libc_rlimit_name, (IDIO irlim), "irlim", "\
return the string name of the getrlimit(2)      \n\
C macro						\n\
						\n\
:param irlim: the C-int value of the macro	\n\
						\n\
:return: a string				\n\
						\n\
:raises: ^rt-parameter-type-error		\n\
")
{
    IDIO_ASSERT (irlim);
    IDIO_VERIFY_PARAM_TYPE (C_int, irlim);

    return idio_string_C (idio_libc_rlimit_name (IDIO_C_TYPE_INT (irlim)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("rlimit-names", libc_rlimit_names, (), "", "\
return a list of pairs of the getrlimit(2)      \n\
C macros					\n\
						\n\
each pair is the C value and string name	\n\
of the macro					\n\
						\n\
:return: a list of pairs			\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_rlimit_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO idio_libc_getrlimit (int resource)
{
    struct rlimit rlim;

    if (getrlimit (resource, &rlim) == -1) {
	idio_error_system_errno ("getrlimit", idio_S_nil, IDIO_C_LOCATION ("idio_libc_rlimit"));
    }

    return idio_struct_instance (idio_libc_struct_rlimit, IDIO_LIST2 (idio_C_int (rlim.rlim_cur),
								      idio_C_int (rlim.rlim_max)));
}

IDIO_DEFINE_PRIMITIVE1 ("getrlimit", libc_getrlimit, (IDIO iresource))
{
    IDIO_ASSERT (iresource);
    IDIO_VERIFY_PARAM_TYPE (C_int, iresource);

    return idio_libc_getrlimit (IDIO_C_TYPE_INT (iresource));
}

void idio_libc_setrlimit (int resource, struct rlimit *rlimp)
{
    if (setrlimit (resource, rlimp) == -1) {
	idio_error_system_errno ("setrlimit", idio_S_nil, IDIO_C_LOCATION ("idio_libc_rlimit"));
    }
}

IDIO_DEFINE_PRIMITIVE2 ("setrlimit", libc_setrlimit, (IDIO iresource, IDIO irlim))
{
    IDIO_ASSERT (iresource);
    IDIO_ASSERT (irlim);
    IDIO_VERIFY_PARAM_TYPE (C_int, iresource);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, irlim);

    IDIO cur = idio_struct_instance_ref_direct (irlim, IDIO_STRUCT_RLIMIT_RLIM_CUR);
    IDIO max = idio_struct_instance_ref_direct (irlim, IDIO_STRUCT_RLIMIT_RLIM_MAX);

    struct rlimit rlim;
    rlim.rlim_cur = (rlim_t) idio_C_int_get (cur);
    rlim.rlim_max = (rlim_t) idio_C_int_get (max);

    idio_libc_setrlimit (IDIO_C_TYPE_INT (iresource), &rlim);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0 ("EGID/get", EGID_get, (void))
{
    return idio_integer (getegid ());
}

IDIO_DEFINE_PRIMITIVE1 ("EGID/set", EGID_set, (IDIO iegid))
{
    IDIO_ASSERT (iegid);

    gid_t egid = -1;

    if (idio_isa_fixnum (iegid)) {
	egid = IDIO_FIXNUM_VAL (iegid);
    } else if (idio_isa_bignum (iegid)) {
	egid = idio_bignum_intmax_value (iegid);
    } else if (idio_isa_C_int (iegid)) {
	egid = IDIO_C_TYPE_INT (iegid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", iegid, IDIO_C_LOCATION ("EGID/set"));
    }

    int r = setegid (egid);

    if (-1 == r) {
	idio_error_system_errno ("setegid", iegid, IDIO_C_LOCATION ("EGID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("EUID/get", EUID_get, (void))
{
    return idio_integer (geteuid ());
}

IDIO_DEFINE_PRIMITIVE1 ("EUID/set", EUID_set, (IDIO ieuid))
{
    IDIO_ASSERT (ieuid);

    uid_t euid = -1;

    if (idio_isa_fixnum (ieuid)) {
	euid = IDIO_FIXNUM_VAL (ieuid);
    } else if (idio_isa_bignum (ieuid)) {
	euid = idio_bignum_intmax_value (ieuid);
    } else if (idio_isa_C_int (ieuid)) {
	euid = IDIO_C_TYPE_INT (ieuid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", ieuid, IDIO_C_LOCATION ("EUID/set"));
    }

    int r = seteuid (euid);

    if (-1 == r) {
	idio_error_system_errno ("seteuid", ieuid, IDIO_C_LOCATION ("EUID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("GID/get", GID_get, (void))
{
    return idio_integer (getgid ());
}

IDIO_DEFINE_PRIMITIVE1 ("GID/set", GID_set, (IDIO igid))
{
    IDIO_ASSERT (igid);

    gid_t gid = -1;

    if (idio_isa_fixnum (igid)) {
	gid = IDIO_FIXNUM_VAL (igid);
    } else if (idio_isa_bignum (igid)) {
	gid = idio_bignum_intmax_value (igid);
    } else if (idio_isa_C_int (igid)) {
	gid = IDIO_C_TYPE_INT (igid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", igid, IDIO_C_LOCATION ("GID/set"));
    }

    int r = setgid (gid);

    if (-1 == r) {
	idio_error_system_errno ("setgid", igid, IDIO_C_LOCATION ("GID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("UID/get", UID_get, (void))
{
    return idio_integer (getuid ());
}

IDIO_DEFINE_PRIMITIVE1 ("UID/set", UID_set, (IDIO iuid))
{
    IDIO_ASSERT (iuid);

    uid_t uid = -1;

    if (idio_isa_fixnum (iuid)) {
	uid = IDIO_FIXNUM_VAL (iuid);
    } else if (idio_isa_bignum (iuid)) {
	uid = idio_bignum_intmax_value (iuid);
    } else if (idio_isa_C_int (iuid)) {
	uid = IDIO_C_TYPE_INT (iuid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", iuid, IDIO_C_LOCATION ("UID/set"));
    }

    int r = setuid (uid);

    if (-1 == r) {
	idio_error_system_errno ("setuid", iuid, IDIO_C_LOCATION ("UID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("STDIN/get", libc_STDIN_get, (void))
{
    return idio_thread_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("STDOUT/get", libc_STDOUT_get, (void))
{
    return idio_thread_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("STDERR/get", libc_STDERR_get, (void))
{
    return idio_thread_current_error_handle ();
}

void idio_init_libc_wrap ()
{
    idio_libc_wrap_module = idio_module (idio_symbols_C_intern ("libc"));

    idio_module_export_symbol_value (idio_symbols_C_intern ("0U"), idio_C_uint (0U), idio_libc_wrap_module);

    /* fcntl.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD"), idio_C_int (F_DUPFD), idio_libc_wrap_module);
#if defined (F_DUPFD_CLOEXEC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD_CLOEXEC"), idio_C_int (F_DUPFD_CLOEXEC), idio_libc_wrap_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("FD_CLOEXEC"), idio_C_int (FD_CLOEXEC), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_GETFD"), idio_C_int (F_GETFD), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_SETFD"), idio_C_int (F_SETFD), idio_libc_wrap_module);

    /* signal.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_DFL"), idio_C_pointer (SIG_DFL), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_IGN"), idio_C_pointer (SIG_IGN), idio_libc_wrap_module);

    /* stdio.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("NULL"), idio_C_pointer (NULL), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("EOF"), idio_C_int (EOF), idio_libc_wrap_module);

    /* stdint.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MAX"), idio_C_int (INTMAX_MAX), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MIN"), idio_C_int (INTMAX_MIN), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("UINTMAX_MAX"), idio_C_uint (UINTMAX_MAX), idio_libc_wrap_module);

    /* sys/resource.h */
    /*
     * NB RLIM_SAVED_* not defined in FreeBSD (10)
     */
#ifdef RLIM_SAVED_MAX
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_MAX"), idio_C_int (RLIM_SAVED_MAX), idio_libc_wrap_module);
#endif
#ifdef RLIM_SAVED_CUR
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_CUR"), idio_C_int (RLIM_SAVED_CUR), idio_libc_wrap_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_INFINITY"), idio_C_int (RLIM_INFINITY), idio_libc_wrap_module);

    /* sys/wait.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("WAIT_ANY"), idio_C_int (WAIT_ANY), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WNOHANG"), idio_C_int (WNOHANG), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WUNTRACED"), idio_C_int (WUNTRACED), idio_libc_wrap_module);

    /* termios.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSADRAIN"), idio_C_int (TCSADRAIN), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSAFLUSH"), idio_C_int (TCSAFLUSH), idio_libc_wrap_module);

    /* unistd.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("PATH_MAX"), idio_C_int (PATH_MAX), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDIN_FILENO"), idio_C_int (STDIN_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDOUT_FILENO"), idio_C_int (STDOUT_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDERR_FILENO"), idio_C_int (STDERR_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("R_OK"), idio_C_int (R_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("W_OK"), idio_C_int (W_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("X_OK"), idio_C_int (X_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_OK"), idio_C_int (F_OK), idio_libc_wrap_module);

    IDIO geti;
    IDIO seti;
    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("errno"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDIN_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDIN"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDOUT_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDOUT"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDERR_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDERR"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    IDIO name;
    name = idio_symbols_C_intern ("struct-sigaction");
    idio_libc_struct_sigaction = idio_struct_type (name,
						   idio_S_nil,
						   idio_pair (idio_symbols_C_intern ("sa_handler"),
						   idio_pair (idio_symbols_C_intern ("sa_sigaction"),
						   idio_pair (idio_symbols_C_intern ("sa_mask"),
						   idio_pair (idio_symbols_C_intern ("sa_flags"),
						   idio_S_nil)))));
    idio_module_export_symbol_value (name, idio_libc_struct_sigaction, idio_libc_wrap_module);

    name = idio_symbols_C_intern ("struct-utsname");
    idio_libc_struct_utsname = idio_struct_type (name,
						 idio_S_nil,
						 idio_pair (idio_symbols_C_intern ("sysname"),
						 idio_pair (idio_symbols_C_intern ("nodename"),
						 idio_pair (idio_symbols_C_intern ("release"),
						 idio_pair (idio_symbols_C_intern ("version"),
						 idio_pair (idio_symbols_C_intern ("machine"),
						 idio_S_nil))))));
    idio_module_export_symbol_value (name, idio_libc_struct_utsname, idio_libc_wrap_module);

    {
	char *field_names[] = { "sb_dev", "sb_ino", "sb_mode", "sb_nlink", "sb_uid", "sb_gid", "sb_rdev", "sb_size", "sb_blksize", "sb_blocks", "sb_atime", "sb_mtime", "sb_ctime", NULL };
	IDIO_DEFINE_MODULE_STRUCTn (idio_libc_wrap_module, idio_libc_struct_stat, "struct-stat", idio_S_nil);
    }

    name = idio_symbols_C_intern ("Idio/uname");
    idio_module_export_symbol_value (name, idio_libc_uname (), idio_libc_wrap_module);

    name = idio_symbols_C_intern ("struct-rlimit");
    idio_libc_struct_rlimit = idio_struct_type (name,
						idio_S_nil,
						idio_pair (idio_symbols_C_intern ("rlim_cur"),
						idio_pair (idio_symbols_C_intern ("rlim_max"),
						idio_S_nil)));
    idio_module_export_symbol_value (name, idio_libc_struct_rlimit, idio_libc_wrap_module);

    idio_vm_signal_handler_conditions = idio_array (IDIO_LIBC_NSIG + 1);
    idio_gc_protect (idio_vm_signal_handler_conditions);
    /*
     * idio_vm_run1() will be indexing anywhere into this array when
     * it gets a signal so make sure that the "used" size is up there
     * by poking something at at NSIG.
     */
    idio_array_insert_index (idio_vm_signal_handler_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NSIG);
    idio_libc_set_signal_names ();

    idio_vm_errno_conditions = idio_array (IDIO_LIBC_NERRNO + 1);
    idio_gc_protect (idio_vm_errno_conditions);
    idio_array_insert_index (idio_vm_errno_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NERRNO);
    idio_libc_set_errno_names ();

    idio_libc_set_rlimit_names ();

    /*
     * Define some host/user/process variables
     */
    IDIO main_module = idio_Idio_module_instance ();

    if (getenv ("HOSTNAME") == NULL) {
	struct utsname u;
	if (uname (&u) == -1) {
	    idio_error_system_errno ("uname", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
	}
	idio_module_set_symbol_value (idio_symbols_C_intern ("HOSTNAME"), idio_string_C (u.nodename), main_module);
    }

    /*
     * From getpwuid(3) on CentOS
     */

    /*
    struct passwd pwd;
    struct passwd *pwd_result;
    char *pwd_buf;
    size_t pwd_bufsize;
    int pwd_s;

    pwd_bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
    if (pwd_bufsize == -1)
	pwd_bufsize = 16384;

    pwd_buf = idio_alloc (pwd_bufsize);

    int pwd_exists = 1;
    pwd_s = getpwuid_r (getuid (), &pwd, pwd_buf, pwd_bufsize, &pwd_result);
    if (pwd_result == NULL) {
	if (pwd_s) {
	    errno = pwd_s;
	    idio_error_system_errno ("getpwnam_r", idio_integer (getuid ()), IDIO_C_LOCATION ("idio_init_libc_wrap"));
	}
	pwd_exists = 0;
    }

    IDIO blank = idio_string_C ("");
    IDIO HOME = blank;
    IDIO SHELL = blank;
    if (pwd_exists) {
	HOME = idio_string_C (pwd.pw_dir);
	SHELL = idio_string_C (pwd.pw_shell);
    }

    if (getenv ("HOME") == NULL) {
	IDIO name = idio_symbols_C_intern ("HOME");
	idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (0));
	idio_module_export_symbol_value (name, HOME, idio_libc_wrap_module);
    }

    if (getenv ("SHELL") == NULL) {
	IDIO name = idio_symbols_C_intern ("SHELL");
	idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (0));
	idio_module_export_symbol_value (name, SHELL, idio_libc_wrap_module);
    }

    free (pwd_buf);
    */

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, UID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, UID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("UID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EUID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EUID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EUID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, GID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, GID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("GID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EGID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EGID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EGID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    int ngroups = getgroups (0, (gid_t *) NULL);

    if (-1 == ngroups) {
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
    }

    gid_t grp_list[ngroups];

    int ng = getgroups (ngroups, grp_list);
    if (-1 == ng) {
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
    }

    /*
     * Could this ever happen?
     */
    if (ngroups != ng) {
	idio_error_C ("getgroups", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
    }

    IDIO GROUPS = idio_array (ngroups);

    for (ng = 0; ng < ngroups ; ng++) {
	idio_array_insert_index (GROUPS, idio_integer (grp_list[ng]), ng);
    }
    idio_module_set_symbol_value (idio_symbols_C_intern ("GROUPS"), GROUPS, main_module);

    idio_module_set_symbol_value (idio_symbols_C_intern ("PID"), idio_integer (getpid ()), main_module);
    idio_module_set_symbol_value (idio_symbols_C_intern ("PPID"), idio_integer (getppid ()), main_module);

}

void idio_libc_wrap_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_system_error);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_access);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_close);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_dup);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_dup2);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_exit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fcntl);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fileno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fork);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getcwd);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getpid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_isatty);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_kill);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkdtemp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkstemp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe_reader);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe_writer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_read);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_setpgid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_handler);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sleep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_stat);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strerror);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strsignal);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcgetattr);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcgetpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcsetattr);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcsetpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_uname);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_unlink);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_waitpid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WEXITSTATUS);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFEXITED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFSIGNALED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFSTOPPED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WTERMSIG);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_write);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sig_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sig_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strerrno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rlimit_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rlimit_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getrlimit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_setrlimit);
}

void idio_final_libc_wrap ()
{
    int i;

    idio_gc_expose (idio_vm_signal_handler_conditions);
    for (i = IDIO_LIBC_FSIG; NULL != idio_libc_signal_names[i]; i++) {
        free (idio_libc_signal_names[i]);
    }
    free (idio_libc_signal_names);

    idio_gc_expose (idio_vm_errno_conditions);
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
        free (idio_libc_errno_names[i]);
    }
    free (idio_libc_errno_names);

    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT; i++) {
        free (idio_libc_rlimit_names[i]);
    }
    free (idio_libc_rlimit_names);

    idio_gc_expose (idio_libc_struct_stat);
}

