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

# blob A
echo "version 1.1 of test file foo" > test/foo
echo "version 1.1 of test file bar" > test/bar
cvs add test/foo test/bar
cvs commit -m "blob A" test/foo test/bar

# Because timing *is* important for this test, we make sure the timestamps
# in the repository are actually different enough.
sleep 5

# blob B
echo "version 1.2 of test file foo" > test/foo
cvs commit -m "blob B" test/foo

sleep 5

# blob C
echo "version 1.2 of test file bar" > test/bar
cvs commit -m "blob C" test/bar

sleep 5

# blob D
echo "version 1.3 of test file bar" > test/bar
cvs commit -m "blob D" test/bar

sleep 5

# blob E
echo "version 1.3 of test file foo" > test/foo
cvs commit -m "blob E" test/foo

sleep 5

# blob F
echo "version 1.4 of test file foo" > test/foo
echo "version 1.4 of test file bar" > test/bar
cvs commit -m "blob F" test/foo test/bar

cd ..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT

