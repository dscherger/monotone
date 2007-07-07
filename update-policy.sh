#!/bin/sh

update_policy_branch () {
    export DELEGATIONS
    export PREFIX
    if [ -d $CODIR ]; then
	BEFORE=$(cd $CODIR && mtn automate get_base_revision_id)
	(cd $CODIR && mtn $CLIENTCONF update -b $1 --quiet)
	AFTER=$(cd $CODIR && mtn automate get_base_revision_id)
	if [ $BEFORE != $AFTER ]; then
	    echo $CODIR
	fi
    else
	mtn $CLIENTCONF checkout -b $1 $CODIR --quiet
	echo $CODIR
    fi
}

update_policy_children () {
    DELEGATIONS=$1/delegations
    grep '^[[:space:]]*delegate' $DELEGATIONS | while read DUMMY PREFIX PBRANCH
      do
      CODIR=$1/delegations.d/checkouts/$PREFIX
      update_policy_branch $PBRANCH
      update_policy_children $CODIR
    done
}

do_work() {
# The local server, that this is pulling from
# and updating the configuration of.
SERVER="localhost"

BASEDIR="$1"

BRANCHES="$2"

[ -f policy/policy.mtn ] || mtn -d policy/policy.mtn db init

DB="--db $BASEDIR/policy/policy.mtn"
CONFDIR="--confdir $BASEDIR"
RCFILE="--rcfile $BASEDIR/update-policy.lua"
CLIENTCONF="${DB} ${CONFDIR} ${RCFILE}"

mtn $CLIENTCONF pull $SERVER "$BRANCHES" --quiet || exit $?

update_policy_children $BASEDIR/policy/policy

mtn $CLIENTCONF pull $SERVER '' --exclude 'policy-branches-updated' --quiet
}

run() {
cd $1

while ! mkdir update-policy.lock
do
  sleep 1
done

do_work "$@"

rmdir update-policy.lock
}

run "$@" &