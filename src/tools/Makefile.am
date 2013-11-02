
bin_PROGRAMS = \
	enicmp\
	koscmp \
	nemcmp \
	compcmp \
	saxcmp

compcmp_SOURCES =  \
	compcmp.cc

compcmp_CXXFLAGS = -I$(srcdir)/../lib 

compcmp_LDFLAGS = $(builddir)/../lib/libcomper.la 

saxcmp_SOURCES =  \
	saxcmp.cc

saxcmp_CXXFLAGS = -I$(srcdir)/../lib 

saxcmp_LDFLAGS = $(builddir)/../lib/libsaxman.la 

nemcmp_SOURCES = \
	nemcmp.cc

nemcmp_LDFLAGS = \
	$(builddir)/../lib/libnemesis.la

nemcmp_CXXFLAGS = \
	-I$(srcdir)/../lib

koscmp_SOURCES = \
	koscmp.cc

koscmp_LDFLAGS = \
	$(builddir)/../lib/libkosinski.la

koscmp_CXXFLAGS = \
	-I$(srcdir)/../lib

enicmp_SOURCES = \
	enicmp.cc

enicmp_LDFLAGS = \
	$(builddir)/../lib/libenigma.la

enicmp_CXXFLAGS = \
	-I$(srcdir)/../lib

AM_CXXFLAGS = 

## File created by the gnome-build tools

