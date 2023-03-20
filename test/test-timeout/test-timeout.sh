#!/bin/bash

HERE=$(dirname $0)
BINDER=$(which afb-binder)
CLIENT=$(which afb-client)
SPAWN=$HERE/../../build/src/afb-spawn.so
PORT=7946
BOUT=$HERE/test-timeout.binder.result
COUT=$HERE/test-timeout.client.result
BREF=$HERE/test-timeout.binder.reference
CREF=$HERE/test-timeout.client.reference

$BINDER --binding $SPAWN:$HERE/test-timeout.json -p $PORT --trap-faults=off >& $BOUT &
BPID=$!

trap "kill $BPID" EXIT

sleep 1
$CLIENT --sync --echo --human localhost:$PORT/api >& $COUT << EOC
timeout ping true
timeout timeout {"action":"start"}
timeout no-timeout {"action":"start"}
EOC

sed -i '/"pid"/d' $COUT

if cmp --silent $BOUT $BREF && cmp --silent $COUT $CREF
then
	echo "ok - test timeout"
else
	echo "not ok - test timeout"
	echo "  ---"
	{ diff $BOUT $BREF ; diff $COUT $CREF ; } |
	sed 's/^/  /'
	echo "  ..."
fi

