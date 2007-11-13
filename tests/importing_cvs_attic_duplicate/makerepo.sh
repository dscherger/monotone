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

# do some commits on file foo
echo "version 0 of test file foo" > test/foo
cvs add test/foo
cvs commit -m "commit 0" test/foo

# make a backup of the current file foo
cp $CVSROOT/test/foo,v $CVSROOT/test/foo,v.bak

# delete file foo with CVS, so is gets moved to the attic
rm test/foo
cvs remove test/foo
cvs commit -m "deleted test/foo" test/foo

cd ..
rm -rf full_checkout

# restore the backup repository file
mv $CVSROOT/test/foo,v.bak $CVSROOT/test/foo,v

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
