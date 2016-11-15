## Process this file with automake to produce Makefile.in
## Created by Anjuta

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = src

mdcompdocdir = ${prefix}/doc/mdcomp

mdcompdoc_DATA = \
	COPYING \
	INSTALL

EXTRA_DIST = $(mdcompdoc_DATA) \
	$(INTLTOOL_FILES)

DISTCLEANFILES =

includedir = ${prefix}/include/mdcomp

include_HEADERS = \
	src/lib/bigendian_io.h \
	src/lib/basic_decoder.h \
	src/lib/moduled_adaptor.h \
	src/lib/artc42.h \
	src/lib/comper.h \
	src/lib/enigma.h \
	src/lib/kosinski.h \
	src/lib/kosplus.h \
	src/lib/nemesis.h \
	src/lib/saxman.h \
	src/lib/rocket.h

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(mdcompdocdir)

