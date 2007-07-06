#!/bin/sh

do_work() {
# The local server, that this is pulling from
# and updating the configuration of.
SERVER="localhost"

BRANCHES="$1"

[ -f policy.mtn ] || mtn -d policy.mtn db init

DB="--db $(pwd)/policy.mtn"
CONFDIR="--confdir $(pwd)"
RCFILE="--rcfile $(pwd)/update-policy.lua"
CLIENTCONF="${DB} ${CONFDIR} ${RCFILE}"

mtn $CLIENTCONF pull $SERVER $(cat serverctl-branch) --quiet || exit $?

if [ -d serverctl ]
then
    (cd serverctl && mtn $CLIENTCONF update)
else
    mtn $CLIENTCONF checkout -b $(cat serverctl-branch) serverctl
fi

mtn $CLIENTCONF pull $SERVER '' --exclude 'policy-branches-updated' --quiet
}

run() {
cd $1

while ! mkdir update-policy.lock
do
  sleep 1
done

do_work $2

rmdir update-policy.lock
}

run "$@" &