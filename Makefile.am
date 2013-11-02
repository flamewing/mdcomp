## Process this file with automake to produce Makefile.in
## Created by Anjuta

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = src po

mdcompdocdir = ${prefix}/doc/mdcomp

includedir = ${prefix}/include/mdcomp

mdcompdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS

INTLTOOL_FILES = intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

EXTRA_DIST = $(mdcompdoc_DATA) \
	$(INTLTOOL_FILES)

DISTCLEANFILES = intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache

include_HEADERS = \
	src/lib/comper.h \
	src/lib/enigma.h \
	src/lib/kosinski.h \
	src/lib/nemesis.h \
	src/lib/saxman.h

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(mdcompdocdir)

