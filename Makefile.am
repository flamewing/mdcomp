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
	include/mdcomp/artc42.hh \
	include/mdcomp/basic_decoder.hh \
	include/mdcomp/bigendian_io.hh \
	include/mdcomp/bitstream.hh \
	include/mdcomp/comper.hh \
	include/mdcomp/enigma.hh \
	include/mdcomp/ignore_unused_variable_warning.hh \
	include/mdcomp/kosinski.hh \
	include/mdcomp/kosplus.hh \
	include/mdcomp/lzkn1.hh \
	include/mdcomp/lzss.hh \
	include/mdcomp/moduled_adaptor.hh \
	include/mdcomp/nemesis.hh \
	include/mdcomp/rocket.hh \
	include/mdcomp/saxman.hh \
	include/mdcomp/snkrle.hh

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(mdcompdocdir)

