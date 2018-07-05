## Process this file with automake to produce Makefile.in
## Created by Anjuta

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = src

mdcompdocdir = ${prefix}/doc/mdcomp

mdcompdoc_DATA = \
	COPYING \
	INSTALL

EXTRA_DIST = $(mdcompdoc_DATA)

DISTCLEANFILES =

includedir = ${prefix}/include/mdcomp

include_HEADERS = \
	src/lib/bigendian_io.hh \
	src/lib/basic_decoder.hh \
	src/lib/moduled_adaptor.hh \
	src/lib/artc42.hh \
	src/lib/comper.hh \
	src/lib/enigma.hh \
	src/lib/kosinski.hh \
	src/lib/kosplus.hh \
	src/lib/nemesis.hh \
	src/lib/saxman.hh \
	src/lib/rocket.hh

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(mdcompdocdir)

