#!/bin/bash
# Run expression tests and compare output

PASS=0
FAIL=0

for f in tests/expr/*.csp; do
    base="${f%.csp}"
    expect="${base}.expect"

    if [ ! -f "$expect" ]; then
        echo "SKIP: $f (no expect file)"
        continue
    fi

    actual=$(./csp -c 1 "$f" 2>&1 | grep -v "^cycle=\|^num_eval\|^result=\|^max cycles")
    expected=$(cat "$expect")

    if [ "$actual" = "$expected" ]; then
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
