#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

DIE=0

# Check for availability
(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "**Error**: You must have \`autoconf' installed."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "**Error**: You must have \`automake' installed."
	echo "You can get it from: ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
	NO_AUTOMAKE=yes
}

# if no automake, don't bother testing for aclocal
test -n "$NO_AUTOMAKE" || (aclocal --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "**Error**: Missing \`aclocal'.	The version of \`automake'"
	echo "installed doesn't appear recent enough."
	echo "You can get automake from ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
}

libtoolize=""
(grep "^LT_INIT" $srcdir/configure.ac >/dev/null) && {
	for l in glibtoolize libtoolize15 libtoolize14 libtoolize ; do
		( $l --version < /dev/null > /dev/null 2>&1 ) && {
			libtoolize=$l
			break
		}
	done
}

if test "x$libtoolize" = "x" ; then
	echo
	echo "**Error**: You must have \`libtool' installed."
	echo "You can get it from: ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
fi


if test "$DIE" -eq 1; then
	exit 1
fi

if test -z "$*"; then
	echo "**Warning**: I am going to run \`configure' with no arguments."
	echo "If you wish to pass any to it, please specify them on the"
	echo \`$0\'" command line."
	echo
fi

case $CC in
xlc )
	am_opt=--include-deps;;
esac

# Clean up the generated crud
rm -f configure config.log config.guess config.sub config.cache
rm -f libtool ltmain.sh missing mkinstalldirs install-sh
rm -f autoconfig.h.in
rm -f config.status aclocal.m4
rm -f `find . -name 'Makefile.in'` `find . -name 'Makefile'`

# touch the configure.ac file to force rebuilding configure
touch configure.ac

# Regenerate everything
echo "Running aclocal $aclocalinclude ..."
aclocal $aclocalincludes
echo "Running $libtoolize..."
$libtoolize --force --copy
echo "Running autoheader..."
autoheader
echo "Running automake --foreign $am_opt ..."
automake --add-missing --copy --foreign $am_opt
echo "Running autoconf ..."
autoconf 

if test x$NOCONFIGURE = x; then
	echo Running $srcdir/configure "$@" ...
	$srcdir/configure "$@" \
	&& echo Now type \`make\' to compile. || exit 1
else
	echo Skipping configure process.
fi

