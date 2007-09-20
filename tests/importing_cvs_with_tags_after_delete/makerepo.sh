#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT

# deleting the existing cvs-repository
rm -vrf $CVSROOT

# initializing a new repository
cvs init

# do a full checkout of the repository
mkdir full_checkout
cd full_checkout
cvs co .
mkdir test
cvs add test
cd test

# two files to begin with
echo "foo" > foo
echo "bar" > bar
cvs add foo bar
cvs commit -m "Initial import"

# tag both files
cvs tag FOO_AND_BAR

# make sure the timestamps different enough
sleep 5

# delete file bar and tag again
cvs remove -f bar
cvs commit -m "removed bar"

# tag only foo
cvs tag FOO_ONLY

cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
