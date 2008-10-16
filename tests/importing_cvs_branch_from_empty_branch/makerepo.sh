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
cvs commit -m "Add test dir"

# start with four initial files
cd test
echo "1.1" > fileA
echo "1.1" > fileB
echo "1.1" > fileC
echo "1.1" > fileD
cvs add fileA fileB fileC fileD
cvs commit -m "initial versions"

# start branch A1 from here
cvs tag -b A1

# some changes (we are still on HEAD)
echo "1.2" > fileD
cvs commit -m "revision 1.2 of fileD"

# start branch A2 from here
cvs tag -b A2

# switch to branch A1 and do a commit there
cvs update -r A1
echo "1.1.2.1" > fileB
cvs commit -m "revision 1.1.2.1 of fileB"

# create branch "B" and switch to that branch
cvs tag -b B
cvs update -r B
echo "1.1.6.1" > fileC
cvs commit -m "revision 1.1.6.1 of fileC"

# tag this with X
cvs tag X

cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
