#/bin/bash

cd $(dirname $0)
TESTS="basic info"
for x in $TESTS
do
	echo "# test $x"
	test-$x/test-$x.sh
done
