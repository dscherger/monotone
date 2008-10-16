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

# checkout the empty repository and commit a file
echo "version 1.1 of test file1" > file1
cvs add file1
cvs commit -m "initial import" file1

# now we create a branch A
cvs tag -b A
cvs update -r A
echo "version 1.1.2.1 of test file1" > file1
cvs commit -m "created branch A" file1

# add a commit which will later conflict with ones in
# branches B and C
sleep 2
echo "version 1.1.2.2 of test file1" > file1
cvs commit -m "CONFLICTING COMMIT" file1

# go back to the trunk and branch into B
cvs update -A
cvs tag -b B
cvs update -r B
echo "version 1.1.4.1 of test file1" > file1
cvs commit -m "created branch B" file1

# add the commit we want to conflict
sleep 2
echo "version 1.1.4.2 of test file1" > file1
cvs commit -m "CONFLICTING COMMIT" file1

# go back to the trunk and branch into C
cvs update -A
cvs tag -b C
cvs update -r C
echo "version 1.1.6.1 of test file1" > file1
cvs commit -m "created branch C" file1

# add yet another commit we want to conflict
sleep 2
echo "version 1.1.6.2 of test file1" > file1
cvs commit -m "CONFLICTING COMMIT" file1

# go back to the trunk
cvs update -A

# and add yet another commit to conflict
sleep 2
echo "version 1.2 of test file1" > file1
cvs commit -m "CONFLICTING COMMIT" file1


cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT

