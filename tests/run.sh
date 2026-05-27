#!/bin/sh
# tests/run.sh - golden-file regression tests for xcull
#
# Run with:  make test
# or:        sh tests/run.sh
#
# Each case is a directory under tests/cases/ with three files:
#   in.txt        input URLs (one per line)
#   flags         xcull flags to use (one line, may be empty)
#   expected.txt  expected stdout, sorted
#
# Output passes if  sort(xcull <flags> < in.txt) == expected.txt.

set -eu

cd "$(dirname "$0")/.."
[ -x ./xcull ] || { echo "build xcull first: make"; exit 2; }

pass=0
fail=0
for case_dir in tests/cases/*/; do
    name=$(basename "$case_dir")
    in="$case_dir/in.txt"
    expected="$case_dir/expected.txt"
    flags_file="$case_dir/flags"
    flags=""
    [ -f "$flags_file" ] && flags=$(cat "$flags_file")

    got=$(./xcull $flags < "$in" | sort)
    want=$(sort "$expected")

    if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
        printf "ok   %s\n" "$name"
    else
        fail=$((fail + 1))
        printf "FAIL %s\n" "$name"
        printf -- "--- expected\n%s\n" "$want"
        printf -- "+++ got\n%s\n" "$got"
    fi
done

printf "\n%d passed, %d failed\n" "$pass" "$fail"
[ "$fail" = "0" ]
