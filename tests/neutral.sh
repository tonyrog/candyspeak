#!/bin/bash
# The same program compiled by ./csp (Q16.16) and ./csp-ieee (IEEE floats)
# has to be the same image: a float constant is IEEE bits in both (format 23).
# Usage: tests/neutral.sh FILE.csp...
cd "$(dirname "$0")/.."
T=tmp/neutral
mkdir -p $T
n=0; bad=0
for f in "$@"; do
    b=$(echo "${f%.csp}" | tr / _)
    ./csp -n -C -O $T/$b.fix.c "$f" >/dev/null 2>&1 || continue
    ./csp-ieee -n -C -O $T/$b.ieee.c "$f" >/dev/null 2>&1 || continue
    n=$((n+1))
    if ! cmp -s <(grep -v '^//' $T/$b.fix.c) <(grep -v '^//' $T/$b.ieee.c); then
	echo "FAIL  neutral: $f"
	bad=$((bad+1))
    fi
done
[ $bad -eq 0 ] && echo "neutral: ok -- $n programs, one image for both floats"
exit $bad
