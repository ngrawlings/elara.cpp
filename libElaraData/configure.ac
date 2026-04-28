AC_INIT([libelaradata], [0.1])
AC_CONFIG_SRCDIR([libelaradata/connectors/Connector.cpp])
AC_CONFIG_HEADERS([config.h])

echo "                             Testing for a C compiler"
AC_PROG_CC
echo "                             Testing for a C++ compiler"
AC_PROG_CXX

AC_LANG([C++])

AC_CHECK_HEADERS([mysql/mysql.h])
AC_CHECK_HEADERS([sqlite3.h])

AC_CHECK_LIB([mysqlclient], [mysql_init])
AC_CHECK_LIB([sqlite3], [sqlite3_open])

AC_SUBST([CC], [g++])

AC_ARG_ENABLE([debugging], AS_HELP_STRING([--enable-debugging], [Enable debug support]))
AC_ARG_ENABLE([64bit], AS_HELP_STRING([--enable-64bit], [Compile for x86_64 architecture]))
AC_ARG_ENABLE([mysql], AS_HELP_STRING([--disable-mysql], [Disable MySQL connector support]))
AC_ARG_ENABLE([sqlite], AS_HELP_STRING([--disable-sqlite], [Disable SQLite connector support]))

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

WITH_MYSQL=1
AS_IF([test "x$enable_mysql" = "xno" || test "x$ac_cv_header_mysql_mysql_h" != "xyes"], [
	WITH_MYSQL=0
	AC_DEFINE_UNQUOTED([MYSQL_DISABLED], [1], [Define to 1 to disable MySQL connector support])
])
AC_SUBST([WITH_MYSQL])

WITH_SQLITE=1
AS_IF([test "x$enable_sqlite" = "xno" || test "x$ac_cv_header_sqlite3_h" != "xyes"], [
	WITH_SQLITE=0
	AC_DEFINE_UNQUOTED([SQLITE_DISABLED], [1], [Define to 1 to disable SQLite connector support])
])
AC_SUBST([WITH_SQLITE])

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
