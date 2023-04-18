#!/bin/bash

HERE=$(dirname $0)
BINDER=$(which afb-binder)
CLIENT=$(which afb-client)
SPAWN=$HERE/../../build/src/afb-spawn.so
PORT=7946
BOUT=$HERE/test-encoders.binder.result
COUT=$HERE/test-encoders.client.result
BREF=$HERE/test-encoders.binder.reference
CREF=$HERE/test-encoders.client.reference

DIRTOLIST=$HERE $BINDER --binding $SPAWN:$HERE/test-encoders.json -p $PORT --trap-faults=off >& $BOUT &
BPID=$!

trap "kill $BPID" EXIT

sleep 1
$CLIENT --sync --echo --human localhost:$PORT/api >& $COUT << EOC
encoders ping true
encoders default {"action":"start"}
encoders wait {"action":"start"}
encoders text {"action":"start"}
encoders wait {"action":"start"}
encoders sync {"action":"start"}
encoders wait {"action":"start"}
encoders line {"action":"start"}
encoders wait {"action":"start"}
encoders raw {"action":"start"}
encoders wait {"action":"start"}
encoders json {"action":"start"}
encoders wait {"action":"start"}
encoders log {"action":"start"}
encoders wait {"action":"start"}
EOC

kill $BPID
trap "" EXIT

sed -i '/"pid"/s/: *[0-9]*/:/' $COUT

if cmp --silent $BOUT $BREF && cmp --silent $COUT $CREF
then
	echo "ok - test encoders"
else
	echo "not ok - test encoders"
	echo "  ---"
	echo "  diff $BOUT $BREF"
	diff $BOUT $BREF | sed 's/^/  /'
	echo "  diff $COUT $CREF"
	diff $COUT $CREF | sed 's/^/  /'
	echo "  ..."
fi

