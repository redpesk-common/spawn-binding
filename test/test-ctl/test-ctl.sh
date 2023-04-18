#!/bin/bash

HERE=$(realpath $(dirname $0))
BUILD=$(realpath $HERE/../../build)
BINDER=$(which afb-binder)
CLIENT=$(which afb-client)
SPAWN=$BUILD/src/afb-spawn.so
PORT=7946
BOUT=$HERE/test-ctl.binder.result
COUT=$HERE/test-ctl.client.result
BREF=$HERE/test-ctl.binder.reference
CREF=$HERE/test-ctl.client.reference

PLUG=$BUILD/test-ctl-plug.so
PLUGC=$HERE/test-ctl-plug.c

if test '!' -e $PLUG -o $PLUG -ot $PLUGC
then
	echo CPATH=$cpath cc -g -O0 -fPIC -shared -I$HERE/../../src -o $PLUG $PLUGC
	CPATH=$cpath cc -fPIC -shared -I$HERE/../../src -o $PLUG $PLUGC
fi

$BINDER --workdir $HERE --binding $SPAWN:$HERE/test-ctl.json -p $PORT --trap-faults=off >& $BOUT &
BPID=$!

trap "kill $BPID 2>/dev/null" EXIT

sleep 0.25
$CLIENT --sync --echo --human localhost:$PORT/api >& $COUT << EOC
ctl ping true
ctl call true
ctl subcall true
ctl distro {"action":"start"}
ctl sync {"action":"start"}
ctl exit true
EOC

kill $BPID
trap "" EXIT

sed -i '/"pid"/s/: *[0-9]*/:/' $COUT

if cmp --silent $BOUT $BREF && cmp --silent $COUT $CREF
then
	echo "ok - test ctl"
else
	echo "not ok - test ctl"
	echo "  ---"
	{ diff $BOUT $BREF ; diff $COUT $CREF ; } |
	sed 's/^/  /'
	echo "  ..."
fi

exit

gdb afb-binder
b process_one_config
b pre_init_api_spawn
b init_api_spawn
r -vvv --binding src/afb-spawn.so:../test/test-ctl/test-ctl.json -p 3333 --trap-faults=off


afb-binder -vvv --binding src/afb-spawn.so:../test/test-ctl/test-ctl.json -p 3333 --trap-faults=off

