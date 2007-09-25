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

# initiate the repository with two files 
echo "version 1.1 of test file foo" > test/foo
echo "version 1.1 of test file bar" > test/bar
cvs add test/foo test/bar
cvs commit -m "initial import" test/foo test/bar

# commit file foo of blob A
echo "version 1.2 of test file foo" > test/foo
cvs commit -m "blob A" test/foo

# blob B
echo "version 1.3 of test file foo" > test/foo
echo "version 1.2 of test file bar" > test/bar
cvs commit -m "blob B" test/foo test/bar

# blob C
echo "version 1.4 of test file foo" > test/foo
echo "version 1.3 of test file bar" > test/bar
cvs commit -m "blob C" test/foo test/bar

# commit file bar of blob A, creates the dependency cycle
echo "version 1.4 of test file bar" > test/bar
cvs commit -m "blob A" test/bar

cd ..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT

