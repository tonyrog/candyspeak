#!/bin/bash
# utils/csp_cc against the C compiler: every program here, compiled by
# tools/csp-image and by csp_cc, has to give the same terms -- names,
# declarations, instructions. What csp_cc cannot compile yet is counted, not
# failed: the count is how far it has come.
# Usage: tests/compile/check.sh [-v] FILE.csp...
cd "$(dirname "$0")/../.."
T=tmp/compile
mkdir -p $T
V=
[ "$1" = "-v" ] && { V=-v; shift; }
pairs=()
for f in "$@"; do
    b=$(echo "${f%.csp}" | tr / _)
    # A program csp-image refuses (a board-only device, a deliberate error)
    # has nothing to compare with.
    CSP_ROOTS=lib=$PWD/lib tools/csp-image -q -o $T/$b "$f" >/dev/null 2>&1 || continue
    pairs+=("$f=$T/$b.img")
done
escript tests/compile/check.escript $V "${pairs[@]}"
