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

# checkout the empty repository and commit some files
echo "version 1.1 of test file1" > file1
echo "version 1.1 of test file2" > file2
cvs add file1 file2
cvs commit -m "initial import" file1 file2

# now we create a branch A
cvs tag -b A
cvs update -r A

# a commit, from which we well later branch into the
# conflicting branch
echo "version 1.1.2.1 of test file1" > file1
cvs commit -m "commit in branch A" file1

# go back to the trunk and branch into B
cvs update -A
cvs tag -b B
cvs update -r B

# a conflicting commit with (file2 of) branch A
echo "version 1.1.4.1 of test file2" > file2
cvs commit -m "commit in branch B" file2

# update only file1 to branch A
cvs update -r A file1

# start a branch from this conflicting posititon
cvs tag -b CONFLICTING_BRANCH
cvs update -r CONFLICTING_BRANCH
echo "version 1.1.2.1.2.1 of test file1" > file1
echo "version 1.1.4.1.2.1 of test file2" > file2
cvs commit -m "commit in CONFLICTING_BRANCH" file1 file2

cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT

