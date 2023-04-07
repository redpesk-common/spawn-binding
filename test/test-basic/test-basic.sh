#!/bin/bash

HERE=$(dirname $0)
BINDER=$(which afb-binder)
CLIENT=$(which afb-client)
SPAWN=$HERE/../../build/src/afb-spawn.so
PORT=7946
BOUT=$HERE/test-basic.binder.result
COUT=$HERE/test-basic.client.result
BREF=$HERE/test-basic.binder.reference
CREF=$HERE/test-basic.client.reference

DIRTOLIST=$HERE $BINDER --binding $SPAWN:$HERE/test-basic.json -p $PORT --trap-faults=off >& $BOUT &
BPID=$!

trap "kill $BPID" EXIT

sleep 1
$CLIENT --sync --echo --human localhost:$PORT/api >& $COUT << EOC
basic ping true
basic distro {"action":"start"}
basic sync {"action":"start"}
EOC

sed -i '/"pid"/d' $COUT

if cmp --silent $BOUT $BREF && cmp --silent $COUT $CREF
then
	echo "ok - test basic"
else
	echo "not ok - test basic"
	echo "  ---"
	{ diff $BOUT $BREF ; diff $COUT $CREF ; } |
	sed 's/^/  /'
	echo "  ..."
fi

