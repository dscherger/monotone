#! /bin/sh -x

dir="$1"
TEST_PORT=$2
TEST_OUTPUT="`pwd`/$3"
export TEST_PORT TEST_OUTPUT

buildbot start "$dir"
