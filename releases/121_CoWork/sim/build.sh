#!/bin/sh
# Build and run the host-side tests for CoWork's pure modules.
# Usage: cd sim && ./build.sh
set -e

CXX="${CXX:-c++}"
FLAGS="-std=c++17 -O1 -Wall -Wextra -Wdouble-promotion -Wfloat-conversion"

mkdir -p build
fail=0
for t in test_sequencer test_clock_recovery test_engine test_flashmap; do
	$CXX $FLAGS -o "build/$t" "$t.cpp"
	if ! "./build/$t"; then
		fail=1
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "SIM TESTS FAILED"
	exit 1
fi
echo "ALL SIM TESTS PASSED"
