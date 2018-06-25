dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(mdcomp, 0.2.0)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11])

AM_SILENT_RULES([yes])

AC_PROG_CXX
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_14([noext],[mandatory])
AC_HEADER_STDC

# ---------------------------------------------------------------------
# Optimization options
# ---------------------------------------------------------------------

# First, lets clean up CXXFLAGS of -O[0-3gs]? and -g[0-3]?
for cxxflag in $CXXFLAGS; do
	AS_CASE([$cxxflag],
		[-O|-O0|-O1|-O2|-O3|-Os|-Og|-g|-g1|-g2|-g3], [],
		[NEWCXXFLAGS="$NEWCXXFLAGS $cxxflag"])
done
CXXFLAGS="$NEWCXXFLAGS"

AC_ARG_WITH([optimization],
            AS_HELP_STRING([--with-optimization=none|size|debug|light|normal|heavy],
                           [Select optimization level @<:@default heavy@:>@]),
            [opt_level="$withval"], [opt_level="heavy"])
AC_MSG_CHECKING([desired optimization level])
if test x$opt_level = xnone; then
	AC_MSG_RESULT(none)
	OPT_FLAGS="$OPT_FLAGS -O0"
elif test x$opt_level = xsize; then
	AC_MSG_RESULT(size)
	AX_CHECK_COMPILE_FLAG([-Os], [have_os_opt=yes], [have_os_opt=no], [$OPT_FLAGS -Werror])
	if test x$have_os_opt = xyes; then
		OPT_FLAGS="$OPT_FLAGS -Os"
	else
		# Sane default if compiler does not support -Os
		OPT_FLAGS="$OPT_FLAGS -O2"
		for opt_flag in -falign-functions -falign-jumps -falign-loops -falign-labels -freorder-blocks -freorder-blocks-and-partition -fprefetch-loop-arrays -ftree-vect-loop-version
		do
			AX_CHECK_COMPILE_FLAG([$opt_flag], [OPT_FLAGS="$OPT_FLAGS $opt_flag"], [], [$OPT_FLAGS -Werror])
		done
	fi
elif test x$opt_level = xdebug; then
	AC_MSG_RESULT(debug)
	AX_CHECK_COMPILE_FLAG([-Og], [have_og_opt=yes], [have_og_opt=no], [$OPT_FLAGS -Werror])
	if test x$have_og_opt = xyes; then
		OPT_FLAGS="$OPT_FLAGS -Og"
	else
		# Sane default if compiler does not support -Og
		OPT_FLAGS="$OPT_FLAGS -O1"
		for opt_flag in -fno-branch-count-reg -fno-if-conversion -fno-if-conversion2 -fno-inline-functions-called-once -fno-move-loop-invariants -fno-ssa-phiopt -fno-tree-bit-ccp -fno-tree-pta -fno-tree-sra
		do
			AX_CHECK_COMPILE_FLAG([$opt_flag], [OPT_FLAGS="$OPT_FLAGS $opt_flag"], [], [$OPT_FLAGS -Werror])
		done
	fi
elif test x$opt_level = xlight; then
	AC_MSG_RESULT(light)
	OPT_FLAGS="$OPT_FLAGS -O1"
elif test x$opt_level = xnormal; then
	AC_MSG_RESULT(normal)
	OPT_FLAGS="$OPT_FLAGS -O2"
elif test x$opt_level = xheavy; then
	AC_MSG_RESULT(heavy)
	OPT_FLAGS="$OPT_FLAGS -O3"
else
	AC_MSG_RESULT(invalid)
	echo "Invalid option given for --with-optimization: $opt_level."
	echo "Expected one of 'none', 'size', 'debug', 'light', 'normal', 'heavy'."
	exit 1
fi

CXXFLAGS="$CXXFLAGS $OPT_FLAGS"

# ---------------------------------------------------------------------
# Debugging options
# ---------------------------------------------------------------------

AC_ARG_WITH([debug],
            AS_HELP_STRING([--with-debug=no|symbols|extreme],
                           [Enable debug mode @<:@default no@:>@]),
            [dbg_level="$withval"], [dbg_level="no"])
AC_MSG_CHECKING([whether to enable debugging mode])
if test x$dbg_level = xno; then
	AC_MSG_RESULT(no)
elif test x$dbg_level = xsymbols -o x$dbg_level = xextreme; then
	AC_MSG_RESULT($dbg_level)
	if test x$dbg_level = xextreme; then
		DEBUG_FLAGS="$CXXFLAGS -g3"
	else
		DEBUG_FLAGS="$CXXFLAGS -g"
	fi
fi

AC_ARG_WITH([sanitizers],
            AS_HELP_STRING([--with-sanitizers=no|address|thread|memory|yes],
                           [Enable sanitizers @<:@default no@:>@]),
            [enable_sanitizers="$withval"], [enable_sanitizers="no"])
AC_MSG_CHECKING([whether to enable sanitizers])
if test x$enable_sanitizers = xno; then
	AC_MSG_RESULT(no)
	CHK_SAN=""
elif test x$enable_sanitizers = xyes; then
	AC_MSG_RESULT($withval)
	CHK_SAN="-fsanitize=undefined -fsanitize=dataflow -fsanitize=safe-stack"
	# Requires LTO. Maybe in the future.
	#CHK_SAN="$CHK_SAN -fsanitize=cfi"
elif test x$enable_sanitizers = xaddress -o x$enable_sanitizers = xthread -o x$enable_sanitizers = xmemory; then
	AC_MSG_RESULT($withval)
	CHK_SAN="-fsanitize=$withval -fsanitize=undefined -fsanitize=dataflow -fsanitize=safe-stack"
	# Requires LTO. Maybe in the future.
	#CHK_SAN="$CHK_SAN -fsanitize=cfi"
else
	AC_MSG_RESULT(invalid)
	echo "Invalid option given for --with-sanitizers: $enable_sanitizers."
	echo "Expected one of 'no', 'address', 'thread', 'memory', 'yes'."
	exit 1
fi

SANITIZERS=""
for cxx_flag in $CHK_SAN
do
	AX_CHECK_COMPILE_FLAG([$cxx_flag], [SANITIZERS="$SANITIZERS $cxx_flag"], [], [$CXXFLAGS -Werror])
done

CXXFLAGS="$CXXFLAGS $SANITIZERS"

# ---------------------------------------------------------------------
# Warning level
# ---------------------------------------------------------------------

CHK_WARN="-Wall -Wextra -pedantic -Wcast-qual -Wwrite-strings -Wredundant-decls -Wdisabled-optimization -fcheck-new"
CHK_WARN="$CHK_WARN -Wctor-dtor-privacy -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wuseless-cast"
CHK_WARN="$CHK_WARN -Wno-format-nonliteral -Wformat-security -Wshift-negative-value -Wshift-overflow -Wtrigraphs"
CHK_WARN="$CHK_WARN -Wuninitialized -fstrict-aliasing -Wduplicated-cond -Wplacement-new -Wcast-align"
CHK_WARN="$CHK_WARN -Wlogical-op -Wlogical-not-parentheses -Wunused-const-variable -Wpointer-sign"
CHK_WARN="$CHK_WARN -Wundef"
#CHK_WARN="$CHK_WARN -Wpadded -Wshadow -Wconditionally-supported"
CHK_WARN="$CHK_WARN -Wc++17-compat -Wc++17-compat-pedantic -Wc++17-extensions -Wc++2a-compat -Wc++2a-extensions"
# GCC attributes
#CHK_WARN="$CHK_WARN -Wsuggest-attribute=pure -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=format"
# C++11 and newer
#CHK_WARN="$CHK_WARN -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override -Wzero-as-null-pointer-constant"
# Clang warnings
CHK_WARN="$CHK_WARN -Wunused-const-variables -Wabsolute-value -Wdeprecated-register -Wmismatched-tags -Wunused-private-field"
# Warnings give errors
AC_ARG_ENABLE(warning-errors, AS_HELP_STRING([--enable-warning-errors], [Turn all warnings into errors @<:@default no@:>@]),,enable_warning_errors=no)
if test x$enable_warning_errors = xyes; then
	CHK_WARN="-Werror $CHK_WARN"
fi

# Pedantic warnings give errors
AC_ARG_ENABLE(pedantic-errors, AS_HELP_STRING([--enable-pedantic-errors], [Turn all pedantic warnings into errors @<:@default no@:>@]),,enable_pedantic_errors=no)
if test x$enable_pedantic_errors = xyes; then
	CHK_WARN="-pedantic-errors $CHK_WARN"
fi

WARNINGS=""
for cxx_flag in $CHK_WARN
do
	AX_CHECK_COMPILE_FLAG([$cxx_flag], [WARNINGS="$WARNINGS $cxx_flag"], [], [$CXXFLAGS -Werror])
done

CXXFLAGS="$CXXFLAGS $WARNINGS"

# ---------------------------------------------------------------------
# Libtool
# ---------------------------------------------------------------------

LT_INIT

# ---------------------------------------------------------------------
# Boost
# ---------------------------------------------------------------------

AX_BOOST_BASE([1.54],, [AC_MSG_ERROR([mdcomp requires Boost, but it was not found in your system])])

# ---------------------------------------------------------------------
# Makefiles
# ---------------------------------------------------------------------

AC_OUTPUT([
Makefile
src/Makefile
src/lib/Makefile
src/tools/Makefile
])

