#!/bin/bash
# crc_destroyer -- systematically flip bits across a saved EEPROM image and
# confirm every load is handled gracefully: it either restores, rejects with a
# message, or (once ROM markers land here) recovers -- but NEVER crashes. Run
# under AddressSanitizer + UBSan so a corruption that reads out of bounds or
# faults is caught as a hard failure instead of passing silently.
#
#   1-bit: EXHAUSTIVE (every bit of the image).
#   multi-bit: SAMPLED (random 2- and 3-bit combinations).
#
# Exit 0 = no crash across all corruptions. Exit 1 = a corruption crashed (bug).
set -u
cd "$(dirname "$0")/.." || exit 1
D=tmp/crc_destroyer
mkdir -p "$D"

SANFLAGS='-fsanitize=undefined,address -fno-omit-frame-pointer -O0'
# exitcode=99: a sanitizer error exits 99 so we can tell it from a clean reject.
export ASAN_OPTIONS=detect_leaks=0:exitcode=99
export UBSAN_OPTIONS=halt_on_error=1:exitcode=99

echo "building ASan csp + bitflip ..."
make clean >/dev/null 2>&1
if ! make all SAN="$SANFLAGS" >/dev/null 2>&1; then echo "build failed"; exit 1; fi
gcc -Wall -O0 -o "$D/bitflip" tests/bitflip.c || exit 1

# Save the target program to a base EEPROM image. Interactive (-i): the auto-load
# and /save commands only run in the REPL; a batch run would execute the program.
printf '/save\n/quit\n' | timeout 20 ./csp -i -e "$D/base.db" tests/crc_prog.csp >/dev/null 2>&1
if [ ! -s "$D/base.db" ]; then echo "save produced no eeprom image"; exit 1; fi
SIZE=$(wc -c < "$D/base.db")
BITS=$((SIZE * 8))
echo "eeprom image: $SIZE bytes ($BITS bits)"

# Classify one corrupted image. The EEPROM auto-loads at REPL boot (-i, no
# program given); /quit then exits. Echoes CRASH / loaded / rejected. A non-zero
# exit (99 = sanitizer, 139 = SIGSEGV) or a timeout (124 = infinite loop) is a
# crash-class failure -- the loader must always terminate gracefully.
classify() {
    local db="$1" out rc
    out=$(printf '/quit\n' | timeout 6 ./csp -i -e "$db" 2>&1); rc=$?
    if [ $rc -ne 0 ]; then echo "CRASH:$rc"; echo "$out" | tail -4 >&2; return; fi
    if echo "$out" | grep -qiE "sanitizer|runtime error"; then echo "CRASH:san"; echo "$out" | tail -4 >&2; return; fi
    if echo "$out" | grep -qi "restored"; then echo loaded; return; fi
    echo rejected
}

# CRC_MAX1 caps the 1-bit sweep (default: all bits); CRC_MULTI the multi-bit
# sample count. Lower them for a quick smoke run, raise for a deep soak.
N1=${CRC_MAX1:-$BITS}
NMULTI=${CRC_MULTI:-1000}
[ "$N1" -gt "$BITS" ] && N1=$BITS

crashes=0 loaded=0 rejected=0
echo "1-bit ($N1 of $BITS) ..."
for ((b=0; b<N1; b++)); do
    cp "$D/base.db" "$D/c.db"
    "$D/bitflip" "$D/c.db" "$b" || continue
    r=$(classify "$D/c.db")
    case "$r" in
	CRASH*) crashes=$((crashes+1)); echo "  CRASH at bit $b ($r)";;
	loaded) loaded=$((loaded+1));;
	*)      rejected=$((rejected+1));;
    esac
done
echo "  -> $loaded loaded, $rejected rejected, $crashes crashes"

# Sampled multi-bit: random 2- and 3-bit flips.
mcrash=0
for n in 2 3; do
    echo "$n-bit sampled ($NMULTI) ..."
    for ((s=0; s<NMULTI; s++)); do
	cp "$D/base.db" "$D/c.db"
	for ((k=0; k<n; k++)); do
	    "$D/bitflip" "$D/c.db" $(( RANDOM % BITS )) || true
	done
	r=$(classify "$D/c.db")
	case "$r" in CRASH*) mcrash=$((mcrash+1)); echo "  CRASH ($n-bit, $r)";; esac
    done
done
echo "  -> $mcrash multi-bit crashes"

# Restore a normal (non-sanitized) build so the tree is left clean.
make clean >/dev/null 2>&1
make >/dev/null 2>&1

total=$((crashes + mcrash))
echo "================================================"
if [ $total -eq 0 ]; then
    echo "crc_destroyer PASS -- no corruption crashed the loader"
    exit 0
else
    echo "crc_destroyer FAIL -- $total corruptions crashed"
    exit 1
fi
