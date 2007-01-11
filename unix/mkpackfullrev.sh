#! /bin/sh

# $1	source directory
# $2	top build directory

sourcedir=$1
topbuilddir=$2
if [ -z "$sourcedir" ]; then
    echo Give me a source directory as 1st argument >&2
    exit 1
fi
if [ -z "$topbuilddir" ]; then
    echo Give me the top build directory as 2nd argument >&2
    exit 1
fi
if [ ! -x ./txt2c ]; then
    echo I need txt2c in current directory >&2
    exit 1
fi

rawfile=package_full_revision_raw.txt
distfile=package_full_revision_dist.txt
fullfile=package_full_revision

# Produce the raw file --------------------------------------------------------
( cd $sourcedir && ( $topbuilddir/mtn --root=. automaget get_revision \
		     || mtn --root=. automate get_revision ) ) \
		   2>/dev/null >$rawfile \
    || rm -f $rawfile

# Produce the full revision file for distribution -----------------------------
if [ -f $rawfile ]; then
    cp $rawfile $distfile && \
	( echo ''
	  echo '  Generated from data cached in the distribution;'
	  echo '  further changes may have been made.' ) >> $distfile
fi
[ -f $distfile ] || echo "unknown" > $distfile

# Produce the full revision file from the raw file or the distributed ---------
# revision file, whichever comes first ----------------------------------------
rm -f $fullfile.txt
for SRC in $rawfile $distfile; do
    ( [ -f $SRC -a ! -f $fullfile.txt ] && cp -f $SRC $fullfile.txt )
done

./txt2c --no-static $fullfile.txt package_full_revision >$fullfile.tmp
cmp -s $fullfile.tmp $fullfile.c || mv -f $fullfile.tmp $fullfile.c
rm -f $fullfile.tmp
