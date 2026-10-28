#!/bin/bash
# utils/csp_list against /list: every program here, compiled to an image by
# tools/csp-image and listed in Erlang, has to read exactly as ./csp lists the
# same source. ./csp loads it as a RAM patch, so its tags are R where the
# image's are F; and a #disable (`!`) is run-time state the image does not
# carry. Those two are evened out, nothing else.
# Usage: tests/list/check.sh FILE.csp...
cd "$(dirname "$0")/../.."
T=tmp/list
mkdir -p $T
tag='  // ([0-9]+ )?[FREPS]!?$'
args=()
for f in "$@"; do
    b=$(echo "${f%.csp}" | tr / _)
    # A program csp-image refuses (a board-only device, a deliberate error)
    # has no image to list.
    tools/csp-image -q -o $T/$b "$f" >/dev/null 2>&1 || continue
    printf '/list\n/quit\n' |
	timeout 20 ./csp -i -e $T/$b.db --no-eeprom -c 0 "$f" 2>&1 |
	grep -E "$tag" | sed -e 's|// \([0-9]* \)\?[RF]!\?$|// \1F|' > $T/$b.want
    args+=("$T/$b.img" "$T/$b.want")
done
escript tests/list/check.escript "${args[@]}"
