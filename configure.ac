dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(mdcomp, 0.2.0)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11])

AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE

AC_PROG_CXX
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_14([],[mandatory])
AC_HEADER_STDC

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
CHK_WARN="$CHK_WARN -Wc++11-compat -Wc++14-compat -Wc++1z-compat"
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
# Makefiles
# ---------------------------------------------------------------------

AC_OUTPUT([
Makefile
src/Makefile
src/lib/Makefile
src/tools/Makefile
])

