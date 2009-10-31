#!/bin/bash

# run this to get the db and key files for this test
# this needs tests/test_keys to be copied to the directory it's run in
EXE=mtn-0.44

MTN="$EXE --confdir=. --keydir=keydir --root=. --db=test.mtn --rcfile test_hooks.lua"

# (rm -rf scratch; mkdir scratch && cp tests/test_keys tests/test_hooks.lua scratch/ && cd scratch && ../tests/db_fix_keys/generate-db.sh) && ls -l scratch

set -e
set -x

mkdir keydir

$MTN db init
$MTN setup . -b testbranch
echo data >file
$MTN add file

# these certs should be fine
$MTN read <test_keys
$MTN ci -m message
REVISION=$($MTN au select h:)
$MTN dropkey tester@test.net

# this cert needs to be attached to a different key
printf "tester@test.net\ntester@test.net\n" | $MTN genkey tester@test.net
$MTN cert $REVISION mycert other
$MTN pubkey tester@test.net >other_key
$MTN dropkey tester@test.net

# this cert should not have a key to attach to
printf "tester@test.net\ntester@test.net\n" | $MTN genkey tester@test.net
$MTN cert $REVISION mycert missing
#$MTN pubkey tester@test.net >missing_key
$MTN dropkey tester@test.net

$MTN read <test_keys
$MTN cert $REVISION mycert good

# this cert should not have a key to attach to
sed 's/tester@test.net/renamed@test.net/' <test_keys | $MTN read
$MTN cert $REVISION mycert renamed -k renamed@test.net
#$MTN pubkey renamed@test.net >renamed_key
$MTN dropkey renamed@test.net

printf "renamed@test.net\nrenamed@test.net\n" | $MTN genkey renamed@test.net
