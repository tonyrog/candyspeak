#!/bin/sh
# Extract a board's real RAM footprint from a built firmware ELF and emit the
# defines csp_linux picks up for --board, so the host simulates measured numbers
# instead of hand-fed ones.
#
#   statics = __bss_end - __data_start   (what arduino-cli calls "Global variables")
#   system  = statics - arena            (the core + every linked library + our
#                                         own globals, i.e. what is taken BEFORE
#                                         CandySpeak claims what is left)
#
# The static arena is subtracted because csp_mem_init claims a pool of its own on
# top; counting it in `system` too would bill it twice. Its symbol is a file-local
# `arena` in csp_arena_mem, so this stays honest without being told the budget.
#
# usage: board_ram.sh <name> <ram-bytes> <eeprom-bytes> <nm-tool> <elf>
set -e

name=$1; ram=$2; eeprom=$3; nm=$4; elf=$5

sym() {   # value of symbol $1 (exact match), as decimal
    $nm --radix=d "$elf" | awk -v s="$1" '$3 == s { print $1+0; exit }'
}
symsize() {   # size of symbol matching regex $1, as decimal
    $nm --print-size --radix=d "$elf" | awk -v r="$1" '$4 ~ r { print $2+0; exit }'
}

# AVR uses __data_start/__bss_end, ARM the double-underscored spelling.
ds=$(sym __data_start); [ -n "$ds" ] || ds=$(sym __data_start__)
be=$(sym __bss_end);    [ -n "$be" ] || be=$(sym __bss_end__)
# CandySpeak's own two big statics: the runtime struct `state` and the static
# arena. Both are counted under CandySpeak (state row / arena row in /memory), so
# `system` is what is left after removing them -- the core plus every linked lib.
arena=$(symsize '^arena'); [ -n "$arena" ] || arena=0
state=$(symsize '^state$'); [ -n "$state" ] || state=0

# awk prints these numerically ($1+0): nm pads with leading zeros, which the
# shell would otherwise read as octal.
statics=$((be - ds))
system=$((statics - arena - state))

up=$(echo "$name" | tr 'a-z' 'A-Z')
echo "/* $name: statics $statics = system $system + state $state + arena $arena */"
echo "#define CSP_BOARD_${up}_RAM     $ram"
echo "#define CSP_BOARD_${up}_SYSTEM  $system"
echo "#define CSP_BOARD_${up}_STATE   $state   /* target sizeof(csp_rt_t): 16/32-bit, smaller than the 64-bit host */"
echo "#define CSP_BOARD_${up}_EEPROM  $eeprom"
