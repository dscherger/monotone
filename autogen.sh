#! /bin/sh

# Run this script to regenerate all configure scripts & Makefiles in
# this source directory.  Mostly, we use autoreconf, but autoreconf
# does not understand our nested directory structure, so it needs a
# bit of help.  Arguments to this will be passed down to autoreconf.
# Note that the botan directory does not have anything needing
# regenerating.

set -e

# Do the top level first, then the monotone directory.  This has the
# side effect of creating a bunch of files, in the top level and in
# m4/, that are expected by some of the libraries.  (This order
# dependence would not exist if we were using verbatim copies of the
# upstream library distributions.)

autoreconf -i "$@"
(cd monotone && exec autoreconf -i "$@")

# Library subdirs in alphabetical order.
(cd idna && exec autoreconf -i "$@")
(cd lua && exec autoreconf -i "$@")
(cd netxx && exec autoreconf -i "$@")
(cd pcre && exec autoreconf -i "$@")
(cd sqlite && exec autoreconf -i "$@")
