#!/bin/sh
aclocal
libtoolize
autoheader
automake --add-missing
autoconf

if [ -z "$NOCONFIGURE" ]; then
    ./configure "$@"
fi
