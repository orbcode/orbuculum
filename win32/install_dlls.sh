#!/bin/sh

for f in $MESON_INSTALL_DESTDIR_PREFIX/bin/*.exe
do
    echo $f
    ldd $f | grep -vi System32 | grep -vi liborb | gawk '{ print $3 }' | xargs -rt cp -t $MESON_INSTALL_DESTDIR_PREFIX/bin
    echo "---"
done
