#!/bin/bash
# utils/csp_asm against the image it came from: every program here, compiled to
# an image by tools/csp-image, taken apart by csp_disasm and put together again
# by csp_asm, has to come back byte for byte -- header, CRCs and all.
# Usage: tests/asm/check.sh FILE.csp...
cd "$(dirname "$0")/../.."
T=tmp/asm
mkdir -p $T
imgs=()
for f in "$@"; do
    b=$(echo "${f%.csp}" | tr / _)
    # A program csp-image refuses (a board-only device, a deliberate error)
    # has no image to assemble.
    CSP_ROOTS=lib=$PWD/lib tools/csp-image -q -o $T/$b "$f" >/dev/null 2>&1 || continue
    imgs+=("$T/$b.img")
done
escript tests/asm/check.escript "${imgs[@]}"
