#!/bin/sh

# update-policy.sh [-fg] CONFDIR [-server SERVER] BRANCHES

# The local server, that this is pulling from
# and updating the configuration of.
SERVER="localhost"

if [ $MTN_SERVER_ADDR ]
then
  SERVER=$MTN_SERVER_ADDR
fi

if [ "$1" = "-fg" ]
then
  shift
  FG=true
fi
BASEDIR="$1"
shift
if [ "$1" = "-server" ]
then
  shift
  SERVER="$1"
  shift
fi
BRANCHES="$1"
shift


DB="--db $BASEDIR/policy.mtn"
CONFDIR="--confdir $BASEDIR"
RCFILE="--rcfile $BASEDIR/update-policy.lua"
CLIENTCONF="--root $BASEDIR ${DB} ${CONFDIR} ${RCFILE}"

update_policy_branch () {
    export DELEGATIONS
    export PREFIX
    echo "Prefix: $PREFIX" >&2
    if [ -d $CODIR ]; then
	BEFORE=$(cd $CODIR && mtn $CLIENTCONF automate get_base_revision_id)
	(cd $CODIR && mtn $CLIENTCONF update -b $1 --quiet)
	AFTER=$(cd $CODIR && mtn $CLIENTCONF automate get_base_revision_id)
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
    grep '^[[:space:]]*delegate' $DELEGATIONS 2>/dev/null |
    while read DUMMY PREFIX PBRANCH
      do
      PREFIX=$(eval echo $PREFIX)
      PBRANCH=$(eval echo $PBRANCH)
      CODIR=$1/delegations.d/checkouts/$PREFIX
      update_policy_branch "$PBRANCH"
      update_policy_children $CODIR
    done
}

do_work() {

[ -f $BASEDIR/policy.mtn ] || mtn $CLIENTCONF db init

mtn $CLIENTCONF pull $SERVER "$BRANCHES" --quiet || return $?

update_policy_children policy

mtn $CLIENTCONF pull $SERVER '' --exclude 'policy-branches-updated' --quiet
}

run() {
cd $BASEDIR

echo "Updating policies for $SERVER..." >&2
echo "Policy list: $BRANCHES">&2

while ! mkdir update-policy.lock
do
  sleep 1
done

do_work

rmdir update-policy.lock

echo "Policy update done." >&2
}

if [ $FG ]
then
  shift
  run
  exit $?
else
  run &
fi
