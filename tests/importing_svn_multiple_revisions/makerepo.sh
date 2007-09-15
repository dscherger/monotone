#!/bin/sh

REPOSITORY=`pwd`/svn-repository
export REPO

# deleting the existing cvs-repository
rm -vrf $REPOSITORY

# create the repository
svnadmin create $REPOSITORY

# checkout the empty repository
svn co file://$REPOSITORY svn-co

# commit common trunk, branches and tags structure
cd svn-co
svn mkdir trunk
svn mkdir branches
svn mkdir tags
svn commit -m "initial subversion structure"

# add a test file
echo "This is version 1 of fileA" > trunk/fileA
svn add trunk/fileA
svn commit -m "a simple first commit"

# update the test file several times
echo "This is version 2 of fileA" > trunk/fileA
svn commit -m "a second commit"

echo "This is version 3 of fileA" > trunk/fileA
svn commit -m "a third commit"

echo "This is version 4 of fileA" > trunk/fileA
svn commit -m "a fourth commit"

echo "This is version 5 of fileA" > trunk/fileA
svn commit -m "a fifth commit"

cd ..

# create the dump
svnadmin dump $REPOSITORY > svn-repository.dump

# clean up
rm -rf svn-co
rm -rf svn-repository

