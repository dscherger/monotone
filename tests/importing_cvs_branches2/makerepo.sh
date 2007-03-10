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
echo "first changelog entry" > changelog
cvs add file1 file2 changelog
cvs commit -m "initial import" file1 file2 changelog

# commit first changes
echo "version 1.2 of test file1" > file1
echo "second changelog entry" >> changelog
cvs commit -m "first commit" file1 changelog

# now we create a branch A
cvs tag -b A
cvs update -r A

# alter the files on branch A
echo "version 1.1.2.1 of test file2" > file2
echo "third changelog -on branch A-" >> changelog
cvs commit -m "commit on branch A" file2 changelog

# branch again into B
cvs tag -b B
cvs update -r B

# branch B is left untouched

# go back to A and branch into C
cvs update -r A -A
cvs tag -b C
cvs update -r C

# add a file3
echo "version 1.1.2.1 of test file3" > file3
echo "fourth changelog -on branch C-" >> changelog
cvs add file3
cvs commit -m "commit on branch C" file3 changelog

# branch into D
cvs tag -b D
cvs update -r D
echo "version 1.2.8.1 of test file1" > file1
echo "version 1.1.2.1.6.1 of test file2" > file2
echo "version 1.1.2.1.2.1 of test file3" > file3
echo "fifth changelog -on branch D-" >> changelog
cvs commit -m "commit on branch D" file1 file2 file3 changelog

# and create some mainline changes after the branch
cvs update -A
echo "version 1.3 of test file1" > file1
echo "third changelog -not on branch-" >> changelog
cvs commit -m "commit on mainline after branch" file1 changelog

cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
