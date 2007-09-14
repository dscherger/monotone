#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT

# deleting the existing cvs-repository
rm -vrf $CVSROOT

# initializing a new repository
cvs init

# the first vendor import
mkdir vendorA_dir
cd vendorA_dir
echo "fileA from VendorA" > fileA
cvs import -m "Initial import from VendorA" test VendorA VendorA_REL_1
cd ..

sleep 5

# the second vendor import
mkdir vendorB_dir
cd vendorB_dir
echo "fileB from VendorB" > fileB
cvs import -m "Initial import from VendorB" test VendorB VendorB_REL_1
cd ..

sleep 5

# checkout the repository and commit some files
cvs co test
cd test
echo "our own additions" > fileC
cvs add fileC
cvs commit -m "commit 0"

sleep 5

# updates from VendorA
cd ../vendorA_dir
echo "fileA from VendorA - changed" > fileA
cvs import -m "Initial import from VendorA" test VendorA VendorA_REL_2

sleep 5

# updates from VendorB
cd ../vendorB_dir
echo "fileB from VendorB - changed" > fileB
cvs import -m "Initial import from VendorB" test VendorB VendorB_REL_2

cd ..
rm -rf test vendorA_dir vendorB_dir

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
