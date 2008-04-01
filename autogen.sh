#! /bin/sh

# Run this script to regenerate all configure scripts & Makefiles in
# this source directory.  Mostly, we use autoreconf, but autoreconf
# does not understand our nested directory structure, so it needs a
# bit of help.  Arguments to this will be passed down to autoreconf.
# Note that the botan directory does not have anything needing
# regenerating.

set -e
autoreconf -i "$@"
for subdir in idna lua monotone netxx pcre sqlite
do (cd $subdir && exec autoreconf -i "$@")
done
