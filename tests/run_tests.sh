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
    # Capture stdout and stderr separately, then concatenate stdout first.
    # How the two streams interleave depends on buffering per platform, so
    # the canonical order is: all stdout, then all stderr.
    tmpout=$(mktemp)
    tmperr=$(mktemp)
    "$CHAN" "$ex" >"$tmpout" 2>"$tmperr"
    out=$(cat "$tmpout" "$tmperr")
    rm -f "$tmpout" "$tmperr"
    want=$(cat "$exp")
    # Normalize CRLF so error output (stderr) matches on Windows and Linux.
    out=$(printf '%s' "$out" | tr -d '\r')
    want=$(printf '%s' "$want" | tr -d '\r')
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
