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
echo "version 1.1 of test file foo" > test/foo
cvs add test/foo
cvs commit -m "initial import" test/foo

# create branchA
cvs tag -b branchA
cvs update -r branchA

echo "version 1.1.2.1 of test file foo" > test/foo
cvs commit -m "an irritating commit message" test/foo

# back on mainline, create a branchB
cvs update -A
cvs tag -b branchB
cvs update -r branchB

echo "version 1.1.4.1 of test file foo" > test/foo
cvs commit -m "an irritating commit message"

cd ..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT

