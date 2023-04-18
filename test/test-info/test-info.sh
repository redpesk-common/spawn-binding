#!/bin/bash

HERE=$(dirname $0)
BINDER=$(which afb-binder)
CLIENT=$(which afb-client)
SPAWN=$HERE/../../build/src/afb-spawn.so
PORT=7946
BOUT=$HERE/test-info.binder.result
COUT=$HERE/test-info.client.result
BREF=$HERE/test-info.binder.reference
CREF=$HERE/test-info.client.reference

$BINDER --binding $SPAWN:$HERE/test-info.json -p $PORT --trap-faults=off >& $BOUT &
BPID=$!

trap "kill $BPID" EXIT

sleep 1
$CLIENT --sync --echo --human localhost:$PORT/api >& $COUT << EOC
basic ping true
basic info true
EOC

kill $BPID
trap "" EXIT

if cmp --silent $BOUT $BREF && cmp --silent $COUT $CREF
then
	echo "ok - test info"
else
	echo "not ok - test info"
fi

