#! /bin/sh

# $1	source directory

sourcedir=$1
topbuilddir=$2
if [ -z "$sourcedir" ]; then
    echo Give me a source directory as 1st argument >&2
    exit 1
fi
if [ -z "$topbuilddir" ]; then
    echo Give me a binary directory as 2nd argument >&2
    exit 1
fi
if [ ! -x ./txt2c ]; then
    echo I need txt2c in current directory >&2
    exit 1
fi

rawfile=package_revision_raw.txt
packrevfile=package_revision

( cd $sourcedir && ( $topbuilddir/mtn --root=. automate get_base_revision_id \
		     || mtn --root=. automate get_base_revision_id ) ) \
		   2>/dev/null >$rawfile \
    || rm -f $rawfile

if [ -f $rawfile ]; then
    cp $rawfile $packrevfile.txt
fi
[ -f $packrevfile.txt ] || echo unknown > $packrevfile.txt

./txt2c --strip-trailing --no-static $packrevfile.txt package_revision \
    > $packrevfile.tmp
cmp -s $packrevfile.tmp $packrevfile.c || mv -f $packrevfile.tmp $packrevfile.c
rm -f $packrevfile.tmp
