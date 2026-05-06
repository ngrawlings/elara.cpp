AC_PREREQ([2.69])
AC_INIT([libelarapythonthreads], [0.1], [ngrawlings@gmail.com])
AC_CONFIG_SRCDIR([libelarapythonthreads])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_CXX
AC_LANG([C++])

AC_SUBST([CC], [$CXX])

AC_ARG_ENABLE(
    [debugging],
    [AS_HELP_STRING([--enable-debugging], [Enable debug compiler flags])],
    [],
    [enable_debugging=no]
)

AC_ARG_ENABLE(
    [64bit],
    [AS_HELP_STRING([--enable-64bit], [Compile for x86_64 architecture])],
    [],
    [enable_64bit=no]
)

AC_ARG_WITH(
    [toolpath],
    [AS_HELP_STRING([--with-toolpath=PATH], [Set the path prefix for makefile tools])],
    [TOOLPATH="$withval"],
    [TOOLPATH=""]
)
AC_SUBST([TOOLPATH])

DEBUG_FLAGS=""
if test "x$enable_debugging" = "xyes"; then
    DEBUG_FLAGS="-Wall -g3 -gdwarf-2 -D DEBUG"
fi
AC_SUBST([DEBUG_FLAGS])

ARCH=""
if test "x$enable_64bit" = "xyes"; then
    ARCH="-m64"
fi
AC_SUBST([ARCH])

AC_PATH_PROG([PYTHON3], [python3])
AC_PATH_PROG([PYTHON3_CONFIG], [python3-config])

if test -z "$PYTHON3"; then
    AC_MSG_ERROR([python3 is required to build libElaraPythonThreads])
fi

if test -z "$PYTHON3_CONFIG"; then
    AC_MSG_ERROR([python3-config is required to build libElaraPythonThreads])
fi

AC_SUBST([PYTHON3])
AC_SUBST([PYTHON3_CONFIG])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
