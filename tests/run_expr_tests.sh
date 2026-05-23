#!/bin/bash
# Run expression tests and compare output

PASS=0
FAIL=0
EPS=0.0001

# Compare two values with epsilon for floats
compare_values() {
    local a="$1" b="$2"
    if [ "$a" = "$b" ]; then
        return 0
    fi
    # Try numeric comparison with epsilon
    awk -v a="$a" -v b="$b" -v eps="$EPS" 'BEGIN {
        if (a ~ /^-?[0-9.]+$/ && b ~ /^-?[0-9.]+$/) {
            diff = a - b; if (diff < 0) diff = -diff
            exit (diff < eps ? 0 : 1)
        }
        exit 1
    }'
}

compare_output() {
    local actual="$1" expected="$2"
    local IFS=$'\n'
    local -a alines=($actual) elines=($expected)
    [ ${#alines[@]} -ne ${#elines[@]} ] && return 1
    for i in "${!alines[@]}"; do
        compare_values "${alines[$i]}" "${elines[$i]}" || return 1
    done
    return 0
}

for f in tests/expr/*.csp; do
    base="${f%.csp}"
    expect="${base}.expect"

    if [ ! -f "$expect" ]; then
        echo "SKIP: $f (no expect file)"
        continue
    fi

    actual=$(./csp -c 1 "$f" 2>&1 | grep -v "^cycle=\|^num_eval\|^result=\|^max cycles")
    expected=$(cat "$expect")

    if compare_output "$actual" "$expected"; then
        echo "PASS: $(basename $f)"
        ((PASS++))
    else
        echo "FAIL: $(basename $f)"
        echo "  Expected: $(echo "$expected" | head -1)..."
        echo "  Actual:   $(echo "$actual" | head -1)..."
        ((FAIL++))
    fi
done

echo "========"
echo "Pass: $PASS, Fail: $FAIL"
[ $FAIL -eq 0 ]
