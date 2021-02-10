bin_PROGRAMS = \
	compcmp \
	enicmp \
	koscmp \
	kosplus \
	lzkn1cmp \
	nemcmp \
	rockcmp \
	saxcmp \
	snkcmp

compcmp_SOURCES = compcmp.cc
compcmp_LDADD = $(builddir)/../lib/libcomper.la

enicmp_SOURCES = enicmp.cc
enicmp_LDADD = $(builddir)/../lib/libenigma.la

lzkn1cmp_SOURCES = lzkn1cmp.cc
lzkn1cmp_LDADD = $(builddir)/../lib/liblzkn1.la

nemcmp_SOURCES = nemcmp.cc
nemcmp_LDADD = $(builddir)/../lib/libnemesis.la

koscmp_SOURCES = koscmp.cc
koscmp_LDADD = $(builddir)/../lib/libkosinski.la

kosplus_SOURCES = kosplus.cc
kosplus_LDADD = $(builddir)/../lib/libkosplus.la

rockcmp_SOURCES = rockcmp.cc
rockcmp_LDADD = $(builddir)/../lib/librocket.la

saxcmp_SOURCES = saxcmp.cc
saxcmp_LDADD = $(builddir)/../lib/libsaxman.la

snkcmp_SOURCES = snkcmp.cc
snkcmp_LDADD = $(builddir)/../lib/libsnkrle.la

AM_CPPFLAGS = -I$(abs_top_srcdir)/include
