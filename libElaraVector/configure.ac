AC_PREREQ([2.69])
AC_INIT([libelaravector], [0.1])
AC_CONFIG_SRCDIR([libelaravector/elara_vector.h])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_CXX
AC_LANG([C])

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

EXCLUDE=
AC_SUBST([EXCLUDE])

AC_SUBST([CC])
AC_SUBST([CXX])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
