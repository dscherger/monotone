#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT
CVSSNAP=`pwd`/cvs-repository-snap
export CVSSNAP

# deleting the existing cvs-repository and
# snapshots
rm -vrf $CVSROOT
rm -vrf `pwd`/cvs-repository-snap

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

# a commit in branch A
echo "version 1.1.2.1 of test file2" > file2
cvs commit -m "commit in branch A before snapshot" file2

# go back to the trunk and branch into B
cvs update -A

sleep 3

# take a snapshot of the current repository, to be
# imported in advance. All of the following changes
# to the repository will be imported later on.
cp -auxv ${CVSROOT} ${CVSSNAP}
rm -rf ${CVSSNAP}/CVSROOT

# add a tag after the snapshot
cvs tag TAG_AFTER_SNAPSHOT

# a commit touching both files
echo "version 1.2 of test file1" > file1
echo "version 1.2 of test file2" > file2
cvs commit -m "first commit after snapshot" file1 file2

# also add a new branch here
cvs tag -b B
cvs update -r B

# a commit in the branch
echo "version 1.2.2.1 of test file1" > file1
cvs commit -m "commit in branch B after snapshot" file1

cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT

