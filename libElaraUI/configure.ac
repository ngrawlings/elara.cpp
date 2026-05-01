AC_PREREQ([2.69])
AC_INIT([libelaraui], [0.1], [ngrawlings@gmail.com])
AC_CONFIG_SRCDIR([libelaraui])
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

AC_ARG_ENABLE(
    [gtk],
    [AS_HELP_STRING([--disable-gtk], [Disable GTK4 backend build])],
    [],
    [enable_gtk=yes]
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

GTK_CFLAGS=""
GTK_LIBS=""
GTK_BACKEND_ENABLED=0
GTK_BACKEND_EXCLUDE=""

if test "x$enable_gtk" != "xno"; then
    AC_PATH_PROG([PKG_CONFIG], [pkg-config])

    if test "x$PKG_CONFIG" = "x"; then
        AC_MSG_ERROR([pkg-config is required for the GTK4 backend. Use --disable-gtk to build only the backend-neutral API.])
    fi

    if $PKG_CONFIG --exists gtk4; then
        GTK_CFLAGS=`$PKG_CONFIG --cflags gtk4`
        GTK_LIBS=`$PKG_CONFIG --libs gtk4`
        GTK_BACKEND_ENABLED=1
        AC_DEFINE([WITH_GTK_BACKEND], [1], [Define to 1 to build the GTK4 UI backend])
    else
        AC_MSG_ERROR([GTK4 backend requested but gtk4 was not found. Use --disable-gtk to build only the backend-neutral API.])
    fi
else
    GTK_BACKEND_EXCLUDE=" -o -path './libelaraui/backends/gtk' -o -path './libelaraui/backends/gtk/*' "
fi

AC_SUBST([GTK_CFLAGS])
AC_SUBST([GTK_LIBS])
AC_SUBST([GTK_BACKEND_ENABLED])
AC_SUBST([GTK_BACKEND_EXCLUDE])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
