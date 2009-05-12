#!/bin/sh
aclocal
autoheader
automake --add-missing
autoconf

if [ -z "$NOCONFIGURE" ]; then
    ./configure "$@"
fi
