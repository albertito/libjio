#!/usr/bin/env bash

# This is a convenience script for running some of the other tests without
# manual intervention, as a means of a fast and easy correctness test.
#
# If you are making an intrusive or risky change, please take the time to run
# the other tests by hand with more intensive parameters, and check the
# coverage and use the other tools mentioned in the README.

# Find our directory, which we will use to find the other tools
OURDIR=$(readlink -f $(dirname $0))


# Find the built versions of the library and Python bindings and add them to
# the lookup paths, so we run against them
. $OURDIR/build_lib_env.sh

cd $OURDIR/

if [ "$1" != "normal" ] && [ "$1" != "fiu" ]; then
	echo "Usage: `basename $0` [normal|fiu]"
	exit 1
fi

set -e

function get_tmp() {
	if which tempfile > /dev/null 2> /dev/null; then
		tempfile -p libjio-tests
	else
		echo ${TMPDIR:-/tmp}/libjio-tests-$$.tmp
	fi
}

case "$1" in
	normal)
		echo "behaviour tests (normal)"
		./wrap-python 2 ../behaviour/runtests normal
		echo
		echo "stress tests (normal)"
		./wrap-python 3 ../stress/jiostress \
			$(get_tmp -p libjio-tests) 20 -n 50 -p 3
		;;
	fiu)
		echo "behaviour tests (all)"
		./wrap-python 2 ../behaviour/runtests all
		echo
		echo "stress tests (normal)"
		./wrap-python 3 ../stress/jiostress \
			$(get_tmp -p libjio-tests) 20 -n 50 -p 3
		echo
		echo "stress tests (fiu)"
		./wrap-python 3 ../stress/jiostress \
			$(get_tmp -p libjio-tests) 20 -n 400 --fi
		;;
esac

echo
echo
echo Tests completed successfuly
echo

