#!/bin/sh

# Script to automate use of 'mtn bisect'.

usage () {
    echo Usage: $0 --build '"foo"' --test '"bar"' [ --quiet ] >&2
    printf "\t--build  command used to build (probably 'make')\n" >&2
    printf "\t--test   command used to test, success/failure determined from\n\t\t exit code\n" >&2
    printf "\t--quiet  Hide build/test output, and instead show 'mtn bisect\n\t\t status' once per iteration\n" >&2
    echo You need to mark at least one good and one bad revision manually before >&2
    echo running this, with "'mtn bisect good'" and "'mtn bisect bad'" >&2
    echo You probably also want to run "'mtn bisect reset'" when you"'"re done. >&2
    exit 1
}

status () {
    TMP=/tmp/bisect.$$
    mtn bisect status 2>$TMP
    RET=$?
    if grep -q 'to start search' $TMP; then
	RET=1
    fi
    if grep -q ' 0 remaining' $TMP; then
	RET=1
    fi
    if [ $RET -ne 0 ]; then
	cat $TMP
    fi
    rm $TMP
    return $RET
}

QUIET=false

while [ $# -gt 0 ]; do
    case $1 in
	--build)
	    shift;
	    BUILD="$1";;
	--test)
	    shift;
	    TEST="$1";;
	--quiet)
	    QUIET=true;;
	*)
	    usage;;
    esac
    shift
done

if [ -z "$BUILD" -o -z "$TEST" ]; then
    usage
fi

# Make sure there's actually a bisection in progress
status || exit 1

if $QUIET; then
    exec 3>&1
    exec >/dev/null 2>/dev/null
else
    exec 3>/dev/null
fi

while status; do
    mtn bisect status >&3 2>&3
    (eval $BUILD)
    if [ $? -ne 0 ]; then
	# fail build
	mtn bisect skip
    else
	# build OK
	(eval $TEST)
	if [ $? -ne 0 ]; then
	    # test fail
	    mtn bisect bad
	else
	    # test OK
	    mtn bisect good
	fi
    fi
done
