AC_INIT([spirit.cpp], [0.1])
AC_CONFIG_SRCDIR([libElaraCore])

AC_ARG_ENABLE([libelaracore], [AS_HELP_STRING([--disable-libelaracore], [Skip building libElaraCore])], [], [enable_libelaracore=yes])
AC_ARG_ENABLE([libelaradebug], [AS_HELP_STRING([--disable-libelaradebug], [Skip building libElaraDebug])], [], [enable_libelaradebug=yes])
AC_ARG_ENABLE([libelarathreads], [AS_HELP_STRING([--disable-libelarathreads], [Skip building libElaraThreads])], [], [enable_libelarathreads=yes])
AC_ARG_ENABLE([libelaraencryption], [AS_HELP_STRING([--disable-libelaraencryption], [Skip building libElaraEncryption])], [], [enable_libelaraencryption=yes])
AC_ARG_ENABLE([libelaraevent], [AS_HELP_STRING([--disable-libelaraevent], [Skip building libElaraEvent])], [], [enable_libelaraevent=yes])
AC_ARG_ENABLE([libelaraformat], [AS_HELP_STRING([--disable-libelaraformat], [Skip building libElaraFormat])], [], [enable_libelaraformat=yes])
AC_ARG_ENABLE([libelaraio], [AS_HELP_STRING([--disable-libelaraio], [Skip building libElaraIO])], [], [enable_libelaraio=yes])
AC_ARG_ENABLE([libelaradata], [AS_HELP_STRING([--disable-libelaradata], [Skip building libElaraData])], [], [enable_libelaradata=yes])
AC_ARG_ENABLE([libelarasockets], [AS_HELP_STRING([--disable-libelarasockets], [Skip building libElaraSockets])], [], [enable_libelarasockets=yes])
AC_ARG_ENABLE([libelaraui], [AS_HELP_STRING([--disable-libelaraui], [Skip building libElaraUI])], [], [enable_libelaraui=yes])
AC_ARG_ENABLE([libelarauirpc], [AS_HELP_STRING([--disable-libelarauirpc], [Skip building libElaraUiRpc])], [], [enable_libelarauirpc=yes])
AC_ARG_ENABLE([libelarapythonthreads], [AS_HELP_STRING([--disable-libelarapythonthreads], [Skip building libElaraPythonThreads])], [], [enable_libelarapythonthreads=yes])
AC_ARG_ENABLE([libelaraparallelassembly], [AS_HELP_STRING([--disable-libelaraparallelassembly], [Skip building libElaraParallelAssembly])], [], [enable_libelaraparallelassembly=yes])
AC_ARG_ENABLE([epadbg], [AS_HELP_STRING([--disable-epadbg], [Skip building epa-dbg])], [], [enable_epadbg=yes])
AC_ARG_ENABLE([libelaraserializer], [AS_HELP_STRING([--disable-libelaraserializer], [Skip building libElaraSerializer])], [], [enable_libelaraserializer=yes])
AC_ARG_ENABLE([libelaravector], [AS_HELP_STRING([--disable-libelaravector], [Skip building libElaraVector])], [], [enable_libelaravector=yes])
AC_ARG_ENABLE([elaraunittests], [AS_HELP_STRING([--disable-elaraunittests], [Skip building ElaraUnitTests])], [], [enable_elaraunittests=yes])
AC_ARG_ENABLE([elaraprojectbuilder], [AS_HELP_STRING([--disable-elaraprojectbuilder], [Skip building ElaraProjectBuilder])], [], [enable_elaraprojectbuilder=yes])
AC_ARG_ENABLE([elaracpplint], [AS_HELP_STRING([--disable-elaracpplint], [Skip building ElaraCppLint])], [], [enable_elaracpplint=yes])
AC_ARG_ENABLE([mysql], [AS_HELP_STRING([--disable-mysql], [Disable MySQL support in libElaraData])], [], [enable_mysql=yes])
AC_ARG_ENABLE([sqlite], [AS_HELP_STRING([--disable-sqlite], [Disable SQLite support in libElaraData])], [], [enable_sqlite=yes])
AC_ARG_ENABLE([xml], [AS_HELP_STRING([--disable-xml], [Disable XML support in libElaraFormat])], [], [enable_xml=yes])

build_projects=""
disabled_projects=""
libelaradata_configure_flags=""
libelaraformat_configure_flags=""

append_project() {
	if test -z "$build_projects"; then
		build_projects="$1"
	else
		build_projects="$build_projects $1"
	fi
}

append_disabled() {
	if test -z "$disabled_projects"; then
		disabled_projects="$1"
	else
		disabled_projects="$disabled_projects $1"
	fi
}

require_enabled() {
	project_name="$1"
	project_enabled="$2"
	dependency_name="$3"
	dependency_enabled="$4"

	if test "x$project_enabled" = "xyes" && test "x$dependency_enabled" != "xyes"; then
		AC_MSG_ERROR([$project_name requires $dependency_name to be enabled])
	fi
}

require_enabled [libElaraDebug] [$enable_libelaradebug] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraDebug] [$enable_libelaradebug] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraDebug] [$enable_libelaradebug] [libElaraIO] [$enable_libelaraio]
require_enabled [libElaraThreads] [$enable_libelarathreads] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraEncryption] [$enable_libelaraencryption] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraEncryption] [$enable_libelaraencryption] [libElaraDebug] [$enable_libelaradebug]
require_enabled [libElaraEvent] [$enable_libelaraevent] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraEvent] [$enable_libelaraevent] [libElaraDebug] [$enable_libelaradebug]
require_enabled [libElaraEvent] [$enable_libelaraevent] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraFormat] [$enable_libelaraformat] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraFormat] [$enable_libelaraformat] [libElaraIO] [$enable_libelaraio]
require_enabled [libElaraIO] [$enable_libelaraio] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraIO] [$enable_libelaraio] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraData] [$enable_libelaradata] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraSockets] [$enable_libelarasockets] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraSockets] [$enable_libelarasockets] [libElaraDebug] [$enable_libelaradebug]
require_enabled [libElaraSockets] [$enable_libelarasockets] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraSockets] [$enable_libelarasockets] [libElaraEvent] [$enable_libelaraevent]
require_enabled [libElaraUI] [$enable_libelaraui] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraUI] [$enable_libelaraui] [libElaraVector] [$enable_libelaravector]
require_enabled [libElaraUI] [$enable_libelaraui] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraUI] [$enable_libelaraui] [libElaraFormat] [$enable_libelaraformat]
require_enabled [libElaraUI] [$enable_libelaraui] [libElaraIO] [$enable_libelaraio]
require_enabled [libElaraUiRpc] [$enable_libelarauirpc] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraUiRpc] [$enable_libelarauirpc] [libElaraUI] [$enable_libelaraui]
require_enabled [libElaraUiRpc] [$enable_libelarauirpc] [libElaraFormat] [$enable_libelaraformat]
require_enabled [libElaraUiRpc] [$enable_libelarauirpc] [libElaraSockets] [$enable_libelarasockets]
require_enabled [libElaraUiRpc] [$enable_libelarauirpc] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraPythonThreads] [$enable_libelarapythonthreads] [libElaraCore] [$enable_libelaracore]
require_enabled [libElaraPythonThreads] [$enable_libelarapythonthreads] [libElaraThreads] [$enable_libelarathreads]
require_enabled [libElaraParallelAssembly] [$enable_libelaraparallelassembly] [libElaraCore] [$enable_libelaracore]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraCore] [$enable_libelaracore]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraDebug] [$enable_libelaradebug]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraThreads] [$enable_libelarathreads]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraEvent] [$enable_libelaraevent]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraFormat] [$enable_libelaraformat]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraIO] [$enable_libelaraio]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraSockets] [$enable_libelarasockets]
require_enabled [epa-dbg] [$enable_epadbg] [libElaraParallelAssembly] [$enable_libelaraparallelassembly]
require_enabled [libElaraSerializer] [$enable_libelaraserializer] [libElaraCore] [$enable_libelaracore]
require_enabled [ElaraUnitTests] [$enable_elaraunittests] [libElaraCore] [$enable_libelaracore]
require_enabled [ElaraUnitTests] [$enable_elaraunittests] [libElaraDebug] [$enable_libelaradebug]
require_enabled [ElaraProjectBuilder] [$enable_elaraprojectbuilder] [libElaraCore] [$enable_libelaracore]
require_enabled [ElaraProjectBuilder] [$enable_elaraprojectbuilder] [libElaraIO] [$enable_libelaraio]
require_enabled [ElaraCppLint] [$enable_elaracpplint] [libElaraCore] [$enable_libelaracore]
require_enabled [ElaraCppLint] [$enable_elaracpplint] [libElaraIO] [$enable_libelaraio]

AS_IF([test "x$enable_libelaracore" = "xyes"], [append_project libElaraCore], [append_disabled libElaraCore])
AS_IF([test "x$enable_libelaradebug" = "xyes"], [append_project libElaraDebug], [append_disabled libElaraDebug])
AS_IF([test "x$enable_libelarathreads" = "xyes"], [append_project libElaraThreads], [append_disabled libElaraThreads])
AS_IF([test "x$enable_libelaraencryption" = "xyes"], [append_project libElaraEncryption], [append_disabled libElaraEncryption])
AS_IF([test "x$enable_libelaraevent" = "xyes"], [append_project libElaraEvent], [append_disabled libElaraEvent])
AS_IF([test "x$enable_libelaraformat" = "xyes"], [append_project libElaraFormat], [append_disabled libElaraFormat])
AS_IF([test "x$enable_libelaraio" = "xyes"], [append_project libElaraIO], [append_disabled libElaraIO])
AS_IF([test "x$enable_libelaradata" = "xyes"], [append_project libElaraData], [append_disabled libElaraData])
AS_IF([test "x$enable_libelarasockets" = "xyes"], [append_project libElaraSockets], [append_disabled libElaraSockets])
AS_IF([test "x$enable_libelaraui" = "xyes"], [append_project libElaraUI], [append_disabled libElaraUI])
AS_IF([test "x$enable_libelarauirpc" = "xyes"], [append_project libElaraUiRpc], [append_disabled libElaraUiRpc])
AS_IF([test "x$enable_libelarapythonthreads" = "xyes"], [append_project libElaraPythonThreads], [append_disabled libElaraPythonThreads])
AS_IF([test "x$enable_libelaraparallelassembly" = "xyes"], [append_project libElaraParallelAssembly], [append_disabled libElaraParallelAssembly])
AS_IF([test "x$enable_epadbg" = "xyes"], [append_project epa-dbg], [append_disabled epa-dbg])
AS_IF([test "x$enable_libelaraserializer" = "xyes"], [append_project libElaraSerializer], [append_disabled libElaraSerializer])
AS_IF([test "x$enable_libelaravector" = "xyes"], [append_project libElaraVector], [append_disabled libElaraVector])
AS_IF([test "x$enable_elaraunittests" = "xyes"], [append_project ElaraUnitTests], [append_disabled ElaraUnitTests])
AS_IF([test "x$enable_elaraprojectbuilder" = "xyes"], [append_project ElaraProjectBuilder], [append_disabled ElaraProjectBuilder])
AS_IF([test "x$enable_elaracpplint" = "xyes"], [append_project ElaraCppLint], [append_disabled ElaraCppLint])

AS_IF([test "x$enable_mysql" = "xno"], [
	libelaradata_configure_flags="$libelaradata_configure_flags --disable-mysql"
])

AS_IF([test "x$enable_sqlite" = "xno"], [
	libelaradata_configure_flags="$libelaradata_configure_flags --disable-sqlite"
])

AS_IF([test "x$enable_xml" = "xno"], [
	libelaraformat_configure_flags="$libelaraformat_configure_flags --disable-xml"
])

AC_SUBST([BUILD_PROJECTS], [$build_projects])
AC_SUBST([DISABLED_PROJECTS], [$disabled_projects])
AC_SUBST([LIBELARADATA_CONFIGURE_FLAGS], [$libelaradata_configure_flags])
AC_SUBST([LIBELARAFORMAT_CONFIGURE_FLAGS], [$libelaraformat_configure_flags])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
