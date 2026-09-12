#!/bin/bash
# Values that do not survive a 16-bit `int`.
#
# On AVR an `int` is SIXTEEN bits. A 32-bit value handed to an int parameter, or
# assigned to an int, is TRUNCATED -- silently, and only there. The host has
# 32-bit ints and cannot reproduce it: there is no -fshort-int, and -m16 is 16-bit
# code generation, not a 16-bit int.
#
# So ask the compiler that knows. avr-gcc -Wconversion says everything, most of
# it sign conversions that are harmless here (size_t is 16 bits on AVR anyway);
# this filters to the one shape that loses data:
#
#     conversion to 'int' from 'long int' may alter its value
#
# That is what made /memory print MAX_INSTRS as -32768 -- mem_int_r took an
# `int`, so 32768 was truncated at the CALL, before the function saw it. The row
# then reported the very bug that had just been fixed.
#
# Zero hits on this tree (2026-09-09), so this is a floor to hold, not a backlog.
set -u
cd "$(dirname "$0")/.."

BIN=$(echo "$HOME"/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-*/bin)
CC="$BIN/avr-gcc"
[ -x "$CC" ] || CC=$(command -v avr-gcc)
if [ -z "${CC:-}" ] || [ ! -x "$CC" ]; then
    echo "width_check: no avr-gcc -- skipped (this check needs a 16-bit int target)"
    exit 0
fi

TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
escript utils/gen_chips.erl --board mega_bare "$TMP/csp_board.h" >/dev/null || exit 1

F="-mmcu=atmega2560 -Os -std=gnu11 -Iinclude -Igen -Isrc -I$TMP
   -DCSP_BOARD=csp_board.h -DCSP_VERSION=\"wc\""
# The shapes that LOSE data. Not sign conversions: size_t is already 16 bits on
# this target, so `size_t -> int` changes no value that fits.
#
# TWO WORDINGS, and missing the second cost a bug. gcc says
#
#     conversion to 'int' from 'long int' may alter its value
#
# for a runtime value, but for a CONSTANT it knows, and says
#
#     conversion to 'int' alters 'long int' constant value
#
# The second is the one that matters most here: `i = MAX_INSTRS` with the
# ceiling defined as (1L << 15) is exactly that, 32768 truncated to -32768 in an
# int that is sixteen bits wide. -Wall is silent for it. This check filtered on
# the first wording only and let it through.
PAT="conversion to '(int|unsigned int|short int|short unsigned int)' (from '(long int|long unsigned int|long long int|long long unsigned int)'|alters '(long int|long unsigned int|long long int|long long unsigned int)' constant value)"

hits=0
broke=0
for f in src/*.c port/csp_avr.c gen/csp_strings.c; do
    # Two results from one compile, and they must not be confused. Piping
    # straight into grep threw the EXIT STATUS away, so a tree that did not
    # compile for AVR at all still reported "ok" -- including a failed
    # _Static_assert, which is how csp_rt_t's hot block is checked. Keep the
    # output, then look at it twice.
    all=$($CC $F -Wconversion -fsyntax-only "$f" 2>&1)
    if [ $? -ne 0 ]; then
	echo "$all" | grep -E "error|assertion" | head -5
	broke=$((broke + 1))
	continue
    fi
    out=$(echo "$all" | grep -E "$PAT")
    if [ -n "$out" ]; then
	echo "$out"
	hits=$((hits + $(echo "$out" | wc -l)))
    fi
done

if [ "$broke" -ne 0 ]; then
    echo
    echo "width_check: $broke file(s) do not COMPILE for avr-gcc (see above)."
    exit 1
fi

if [ "$hits" -eq 0 ]; then
    echo "width_check: ok -- nothing 32-bit is truncated by a 16-bit int"
    exit 0
fi
echo
echo "width_check: $hits value(s) truncated on a target where int is 16 bits."
echo "Widen the parameter or the variable -- see mem_int_r in src/csp_repl.c."
exit 1
