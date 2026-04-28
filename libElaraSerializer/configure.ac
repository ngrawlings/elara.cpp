AC_INIT([libelaraserializer], [0.1])
AC_CONFIG_SRCDIR([elaraserializer/Serializable.cpp])
AC_CONFIG_HEADERS([config.h])

echo "                             Testing for a C compiler"
AC_PROG_CC
echo "                             Testing for a C++ compiler"
AC_PROG_CXX

AC_LANG([C++])

AC_SUBST([CC], [g++])

AC_ARG_ENABLE([debugging], AS_HELP_STRING([--enable-debugging], [Enable debug support]))
AC_ARG_ENABLE([64bit], AS_HELP_STRING([--enable-64bit], [Compile for x86_64 architecture]))

DEBUG_FLAGS=
AS_IF([test "x$enable_debugging" = "xyes"], [
	[DEBUG_FLAGS="-Wall -g3 -gdwarf-2 -D DEBUG"]
])
AC_SUBST([DEBUG_FLAGS])

ARCH=
AS_IF([test "x$enable_64bit" = "xyes"], [
	[ARCH="-m64"]
])
AC_SUBST([ARCH])

TOOLPATH=
AC_ARG_WITH(
	[toolpath],
	AS_HELP_STRING(
		[--with-toolpath=PATH],
		[set the path to makefile tools]
	),
	[TOOLPATH="$withval"]
)
AC_SUBST([TOOLPATH])

AC_OUTPUT([Makefile])
