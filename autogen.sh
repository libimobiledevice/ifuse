#!/bin/sh

olddir=`pwd`
srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

(
  cd "$srcdir"

  aclocal
  autoheader
  automake --add-missing
  autoconf

  cd "$olddir"
)

if [ -z "$NOCONFIGURE" ]; then
  $srcdir/configure "$@"
fi
