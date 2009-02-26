#!/bin/bash

mkdir -p do-fuzz.d
cd do-fuzz.d

rm -rf *
mkdir keys

cat >rcfile <<EOF
function get_passphrase(key)
   return "foo"
end
function get_netsync_write_permitted(ident)
   return true
end
EOF

KEYOPTS="-k testkey --keydir keys --rcfile rcfile"

mtn -d server.mtn db init
mtn -d client.mtn db init
printf "foo\nfoo\n" | mtn --keydir keys genkey testkey --root .
mtn -d client.mtn setup . -b testbranch $KEYOPTS

serve() {
    SRV_COUNT=0
    while true
      do
      ~/src/nvm/build/mtn -d server.mtn serve --bind localhost:12345 \
          --log server-log-$SRV_COUNT \
          --dump server-dump-$SRV_COUNT \
          --quiet $KEYOPTS
      echo "Server died, number $SRV_COUNT"
      SRV_COUNT=$(expr $SRV_COUNT + 1)
    done
}

client_iter() {
    if [ $(expr $RANDOM % 50) -eq 0 ]
    then
        KEYNAME=key-$CLI_COUNT
        printf "foo\nfoo\n" | mtn --keydir keys genkey $KEYNAME
        PUSHKEY="--key-to-push $KEYNAME"
        echo "Pushing a key."
    else
        PUSHKEY=""
    fi
    FILE=file-$RANDOM
    dd if=/dev/urandom of=$FILE bs=1 count=$RANDOM 2>/dev/null
    mtn add $FILE --quiet
    mtn ci -m "This is a random $RANDOM commit message." --quiet $KEYOPTS --root .
    ~/src/nvm-fuzzy-networking/build/mtn sy localhost:12345 \* \
        --ticker none $KEYOPTS $PUSHKEY
}
client() {
    CLI_COUNT=0
    while true
      do
      client_iter
      CLI_COUNT=$(expr $CLI_COUNT + 1)
      echo "Tried $CLI_COUNT connections so far"
    done
}
watchdog() {
    while sleep 2
      do
      X=$(ps -Af | grep -E 'pts/7.*m[t]n sy' | awk '{print $2};')
      sleep 5
      test "$X" -a -d /proc/$X && kill $X
    done
}

serve &
client &
#watchdog &

do_kill() {
	PGID=$(echo $(ps -o pgid $$ | tail -n1))
        echo "PGID is $PGID"
        echo "Killing '/bin/kill INT -$PGID'"
	/bin/kill INT -$PGID
        sleep 2
}

trap do_kill int

wait

