dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(mdcomp, 0.2.0)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.14])

AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE

AC_PROG_CXX

CXXFLAGS="$CXXFLAGS -Wall -Wextra -pedantic -Wcast-qual -Wold-style-cast"
CXXFLAGS="$CXXFLAGS -Wwrite-strings -Wredundant-decls -fcheck-new"
CXXFLAGS="$CXXFLAGS -Wstrict-overflow -Wdisabled-optimization -Wctor-dtor-privacy"
CXXFLAGS="$CXXFLAGS -Wnon-virtual-dtor -Woverloaded-virtual"

dnl ***************************************************************************
dnl Internatinalization
dnl ***************************************************************************
IT_PROG_INTLTOOL([0.35.0])

GETTEXT_PACKAGE=mdcomp
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])
AM_GLIB_GNU_GETTEXT

LT_INIT

AC_OUTPUT([
Makefile
src/Makefile
src/lib/Makefile
src/tools/Makefile
po/Makefile.in
])
