#!/bin/bash
# Chan test runner. Usage: CHAN=<path-to-chan-binary> ./tests/run_tests.sh
set -u
CHAN=${CHAN:-./build/Release/chan}
fail=0

for ex in examples/*.chan; do
    name=$(basename "$ex" .chan)
    exp="tests/expected/$name.txt"
    if [ ! -f "$exp" ]; then
        echo "SKIP $name (no expected file)"
        continue
    fi
    out=$("$CHAN" "$ex" 2>&1)
    want=$(cat "$exp")
    if [ "$out" = "$want" ]; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        echo "--- got ---"
        echo "$out"
        echo "--- want ---"
        echo "$want"
        fail=1
    fi
done

exit $fail
