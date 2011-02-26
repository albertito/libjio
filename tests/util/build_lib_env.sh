#!/usr/bin/env bash

# Apply the environment changes needed to use the built (and not the
# installed) versions of the library and Python bindings.
#
# They are useful for running tests before installing.
#
# Is intended use is to be sourced in the shell, for example:
#
#   $ source build_lib_env.sh
#   $ ./some-binary-using-the-library
#
# Although the most common users will be the other test scripts.

# We assume we're in the tests/util directory of the libjio source tree.
OURDIR=$(readlink -f $(dirname $0))

LIBBIN=$(readlink -f "$OURDIR"/../../libjio/build/libjio.so)

if ! [ -x "$LIBBIN" ]; then
	echo "Can't find library (run make)"
	exit 1
fi

export "LD_LIBRARY_PATH=$(dirname $LIBBIN):$LD_LIBRARY_PATH"

