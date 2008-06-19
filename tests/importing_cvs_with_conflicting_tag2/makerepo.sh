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
echo "version 0 of test fileA" > test/fileA
echo "version 0 of test fileB" > test/fileB
echo "version 0 of test fileC" > test/fileC
echo "version 0 of test fileD" > test/fileD
echo "version 0 of test fileE" > test/fileE
echo "version 0 of test fileF" > test/fileF
cvs add test/file?
cvs commit -m "commit 0" test/file?

# tag fileA here
cvs rtag NASTY_TAG test/fileA

# continue committing
echo "version 1 of test fileA" > test/fileA
echo "version 1 of test fileB" > test/fileB
echo "version 1 of test fileC" > test/fileC
echo "version 1 of test fileD" > test/fileD
echo "version 1 of test fileE" > test/fileE
echo "version 1 of test fileF" > test/fileF
cvs commit -m "commit 1" test/file?

# tag fileB here
cvs rtag NASTY_TAG test/fileB

# continue committing
echo "version 2 of test fileA" > test/fileA
echo "version 2 of test fileB" > test/fileB
echo "version 2 of test fileC" > test/fileC
echo "version 2 of test fileD" > test/fileD
echo "version 2 of test fileE" > test/fileE
echo "version 2 of test fileF" > test/fileF
cvs commit -m "commit 2" test/file?

# tag fileC here
cvs rtag NASTY_TAG test/fileC

# continue committing
echo "version 3 of test fileA" > test/fileA
echo "version 3 of test fileB" > test/fileB
echo "version 3 of test fileC" > test/fileC
echo "version 3 of test fileD" > test/fileD
echo "version 3 of test fileE" > test/fileE
echo "version 3 of test fileF" > test/fileF
cvs commit -m "commit 3" test/file?

# tag fileD here
cvs rtag NASTY_TAG test/fileD

# continue committing
echo "version 4 of test fileA" > test/fileA
echo "version 4 of test fileB" > test/fileB
echo "version 4 of test fileC" > test/fileC
echo "version 4 of test fileD" > test/fileD
echo "version 4 of test fileE" > test/fileE
echo "version 4 of test fileF" > test/fileF
cvs commit -m "commit 4" test/file?

# tag fileE here
cvs rtag NASTY_TAG test/fileE

# continue committing
echo "version 5 of test fileA" > test/fileA
echo "version 5 of test fileB" > test/fileB
echo "version 5 of test fileC" > test/fileC
echo "version 5 of test fileD" > test/fileD
echo "version 5 of test fileE" > test/fileE
echo "version 5 of test fileF" > test/fileF
cvs commit -m "commit 5" test/file?

# tag fileF here
cvs rtag NASTY_TAG test/fileF

cd ..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
