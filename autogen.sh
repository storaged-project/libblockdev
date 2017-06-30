#!/bin/bash -e
test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

pushd $srcdir

(test -f configure.ac) || {
        echo "*** ERROR: Directory "\`$srcdir\'" does not look like the top-level project directory ***"
        exit 1
}

PKG_NAME=`autoconf --trace 'AC_INIT:$1' configure.ac`

aclocal --install || exit 1
autoreconf --verbose --force --install -Wno-portability || exit 1
popd
