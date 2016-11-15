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

CXXFLAGS="$CXXFLAGS -Wall -Wextra -pedantic -Wcast-qual -Wold-style-cast"
CXXFLAGS="$CXXFLAGS -Wwrite-strings -Wredundant-decls -fcheck-new"
CXXFLAGS="$CXXFLAGS -Wstrict-overflow -Wdisabled-optimization -Wctor-dtor-privacy"
CXXFLAGS="$CXXFLAGS -Wnon-virtual-dtor -Woverloaded-virtual"

LT_INIT

AC_OUTPUT([
Makefile
src/Makefile
src/lib/Makefile
src/tools/Makefile
])

