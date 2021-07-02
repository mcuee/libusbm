AC_INIT([libusb-compat], [0.1.4])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([libusb/core_linux.c])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PREREQ([2.50])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_C_INLINE
AM_PROG_CC_C_O

# Library versioning
LT_MAJOR="8"
LT_REVISION="4"
LT_AGE="4"
AC_SUBST(LT_MAJOR)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

# The libusb-0.1 version we pretend to be
LIBUSB01_VERSION="0.1.12"
AC_SUBST(LIBUSB01_VERSION)

PKG_CHECK_MODULES([LIBUSB_1_0],
    [libusb-1.0 >= 0.9.1],
    [],
    [AC_MSG_ERROR([libusb-1.0 not found. If you built it from source, append that location to PKG_@&t@CONFIG_PATH. (where libusb-1.0.pc resides) Check using "pkg-config" --modversion libusb-1.0])])

# PKG_CHECK_MODULES([LIBUSB_1_0], libusb-1.0 >= 0.9.1)

AC_SUBST(LIBUSB_1_0_CFLAGS)
AC_SUBST(LIBUSB_1_0_LIBS)

AC_DEFINE([_GNU_SOURCE], 1, [Use GNU extensions])

LTLDFLAGS="${LTLDFLAGS} -no-undefined"

AC_MSG_CHECKING([operating system])

case $host in
*-linux*)
	AC_MSG_RESULT([Linux])
	backend="linux"
	threads="posix"
	;;
*-darwin*)
	AC_MSG_RESULT([Darwin/Mac OS X])
	backend="darwin"
	threads="posix"
	;;
*-openbsd*)
	AC_MSG_RESULT([OpenBSD])
	backend="bsd"
	threads="posix"
	;;
*-netbsd*)
	AC_MSG_RESULT([NetBSD (using OpenBSD backend)])
	backend="bsd"
	threads="posix"
	;;
*-freebsd*)
	AC_MSG_RESULT([FreeBSD (using OpenBSD backend)])
	backend="bsd"
	threads="posix"
	;;
*-mingw*)
	AC_MSG_RESULT([Windows])
	backend="windows"
	threads="windows"
	;;
*-cygwin*)
	AC_MSG_RESULT([Cygwin (using Windows backend)])
	backend="windows"
	threads="posix"
	;;
*)
	AC_MSG_ERROR([unsupported operating system])
esac

case $backend in
linux)
	AC_DEFINE(OS_LINUX, 1, [Linux backend])
	AC_SUBST(OS_LINUX)
	AC_CHECK_LIB(rt, clock_gettime, PC_LIBS_PRIVATE="-lrt")
	THREAD_CFLAGS="-pthread"
	PC_LIBS_PRIVATE="${PC_LIBS_PRIVATE} -pthread"
	AC_CHECK_HEADERS([poll.h])
	AC_DEFINE([POLL_NFDS_TYPE],[nfds_t],[type of second poll() argument])
	;;
darwin)
	AC_DEFINE(OS_DARWIN, 1, [Darwin backend])
	AC_SUBST(OS_DARWIN)
	PC_LIBS_PRIVATE="-lobjc -Wl,-framework,IOKit -Wl,-framework,CoreFoundation"
	LTLDFLAGS="${LTLDFLAGS} -Wl,-prebind"
	AC_CHECK_HEADERS([poll.h])
	AC_CHECK_TYPE([nfds_t],
		[AC_DEFINE([POLL_NFDS_TYPE],[nfds_t],[type of second poll() argument])],
		[AC_DEFINE([POLL_NFDS_TYPE],[unsigned int],[type of second poll() argument])],
		[#include <poll.h>])
	;;
bsd)
	AC_DEFINE(OS_OPENBSD, 1, [OpenBSD/NetBSD backend])
	AC_SUBST(OS_OPENBSD)
	THREAD_CFLAGS="-pthread"
	PC_LIBS_PRIVATE="-pthread"
	AC_CHECK_HEADERS([poll.h])
	AC_DEFINE([POLL_NFDS_TYPE],[nfds_t],[type of second poll() argument])
	;;
windows)
	AC_DEFINE(OS_WINDOWS, 1, [Windows backend])
	AC_SUBST(OS_WINDOWS)
	PC_LIBS_PRIVATE=""
	LTLDFLAGS="${LTLDFLAGS} -avoid-version -Wl,--add-stdcall-alias"
	AC_DEFINE([POLL_NFDS_TYPE],[unsigned int],[type of second poll() argument])
	;;
esac

AC_SUBST(THREAD_CFLAGS)
AC_SUBST(PC_LIBS_PRIVATE)
LIBS="${LIBS} ${PC_LIBS_PRIVATE}"

AM_CONDITIONAL(OS_LINUX, test "x$backend" = xlinux)
AM_CONDITIONAL(OS_DARWIN, test "x$backend" = xdarwin)
AM_CONDITIONAL(OS_OPENBSD, test "x$backend" = xbsd)
AM_CONDITIONAL(OS_WINDOWS, test "x$backend" = xwindows)
AM_CONDITIONAL(THREADS_POSIX, test "x$threads" = xposix)
if test "$threads" = posix; then
	AC_DEFINE(THREADS_POSIX, 1, [Use POSIX Threads])
fi


# Message logging
AC_ARG_ENABLE([log], [AS_HELP_STRING([--disable-log], [disable all logging])],
	[log_enabled=$enableval],
	[log_enabled='yes'])
if test "x$log_enabled" != "xno"; then
	AC_DEFINE([ENABLE_LOGGING], 1, [Message logging])
fi

AC_ARG_ENABLE([debug-log], [AS_HELP_STRING([--enable-debug-log],
	[enable debug logging (default n)])],
	[debug_log_enabled=$enableval],
	[debug_log_enabled='no'])
if test "x$debug_log_enabled" != "xno"; then
	AC_DEFINE([ENABLE_DEBUG_LOGGING], 1, [Debug message logging])
fi

# Examples build
AC_ARG_ENABLE([examples-build], [AS_HELP_STRING([--enable-examples-build],
	[build example applications (default n)])],
	[build_examples=$enableval],
	[build_examples='no'])
AM_CONDITIONAL([BUILD_EXAMPLES], [test "x$build_examples" != "xno"])

# Restore gnu89 inline semantics on gcc 4.3 and newer
saved_cflags="$CFLAGS"
CFLAGS="$CFLAGS -fgnu89-inline"
AC_COMPILE_IFELSE(AC_LANG_PROGRAM([]), inline_cflags="-fgnu89-inline", inline_cflags="")
CFLAGS="$saved_cflags"

AC_DEFINE([API_EXPORTED], [__attribute__((visibility("default")))], [Default visibility])
AM_CFLAGS="-std=gnu99 $inline_cflags -Wall -Wundef -Wunused -Wstrict-prototypes -Werror-implicit-function-declaration -Wno-pointer-sign -Wshadow"
AC_SUBST(AM_CFLAGS)

AC_CONFIG_FILES([libusb.pc] [libusb-config] [Makefile] [libusb/Makefile] [examples/Makefile])
AC_CONFIG_COMMANDS([default],[[chmod +x libusb-config]],[[]])

AC_OUTPUT

