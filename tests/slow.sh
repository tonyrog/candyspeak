#!/bin/bash
# slow.sh -- the cases that WAIT.
#
# Split out of repl.sh because they are the ones that cost wall clock: a line
# 400 bytes long with no terminator has to be allowed to run the reader out of
# input, and that means sitting on a timeout rather than on an answer. Four
# cases, and they were most of the suite's runtime.
#
# They are not less important -- an over-long line that runs SHORT is a
# different command, and a queue that fills with no complete line in it is the
# one state a UART reader cannot get out of. They just do not belong in the loop
# you run after every edit.
#
#     make test        fast: everything else
#     make test-slow   these
#     make test-all    both, plus every board
set -u
cd "$(dirname "$0")/.." || exit 1
D=tmp/repl
mkdir -p "$D"
for f in "$D"/slow*.db; do [ -e "$f" ] && : > "$f"; done

pass=0; fail=0

ck() {
    local name="$1" want="$2" got="$3"
    if [ "$want" = "$got" ]; then
	pass=$((pass+1)); echo "  PASS $name"
    else
	fail=$((fail+1)); echo "  FAIL $name"
	echo "    --- want ---"; printf '%s\n' "$want" | sed 's/^/    /'
	echo "    --- got  ---"; printf '%s\n' "$got"  | sed 's/^/    /'
    fi
}

repl() {
    local bin="$1" ee="$2"; shift 2
    timeout 20 "$bin" -i -e "$ee" -c 0 "$@" 2>&1 |
	grep -v '^CandySpeak Interactive Mode$' |
	grep -v '^Type /help for commands' |
	grep -v '^> ' | grep -v '^$'
}

# --- 10. an over-long line is refused, not truncated -------------------------
# A line past the buffer used to lose its tail silently and run anyway. That is
# not a partial command, it is a DIFFERENT one: `#disable 12` cut short disables
# rule 1. The paste that triggers it is exactly the case the REPL exists for.
echo "long line:"
# Padded with a comment, so the line is over-long while everything IN it is
# valid -- what comes back has to be the length complaint, not a parse error.
# --board pins the arena to a MEASURED board, so the number this asserts is the
# one a mega really has -- the buffer is a 32nd of the pool, so without a fixed
# pool the limit would change with the machine running the suite.
pad=$(printf 'x%.0s' $(seq 1 400))
long="#variable Wide = 1 // $pad"
got=$(printf '%s\n/quit\n' "$long" | repl ./csp "$D/slow1.db" --no-eeprom --board mega |
	  tr -d '\a')
ck "an over-long line is refused with a reason" \
"Error: line too long, max 95 characters -- line ignored" "$got"

# ...and nothing from it reached the program: refused, not run short.
got=$(printf '%s\n/list\n/quit\n' "$long" | repl ./csp "$D/slow2.db" --no-eeprom --board mega |
	  tr -d '\a' | grep -c 'Wide')
ck "and nothing from it was declared" "0" "$got"

# The buffer is sized from the pool, which is the whole point of moving it there:
# the same paste that a mega refuses goes through on a board with room.
got=$(printf '%s\n/list\n/quit\n' "$long" | repl ./csp "$D/t13.db" --no-eeprom |
	  tr -d '\a' | grep -c 'Wide')
ck "a board with room accepts the same line" "1" "$got"

# --- 15. an over-long line does not strand the reader ------------------------
# The queue must never reach "full with no complete line in it": that is the one
# state where the reader stops draining and nothing can make it start again --
# and on a UART it would leave XOFF asserted with no way to release it.
# csp_line_input stops storing at line_size - 1 and raises line_ovf instead, so
# the reader keeps draining and discarding until the newline. Proof: a line far
# past the limit, then ordinary work, in one burst.
echo "overflow recovery:"
over=$(printf 'y%.0s' $(seq 1 400))
got=$(printf '#variable Before = 1\n%s\n#variable After = 2\n/list\n/quit\n' "$over" |
	  repl ./csp "$D/slow3.db" --no-eeprom --board mega |
	  tr -d '\a' | grep -v '^OK$')
ck "the REPL keeps working after an over-long line" \
'Error: line too long, max 95 characters -- line ignored
#variable Before:32 integer = 1  // R
#variable After:32 integer = 2  // R' "$got"

# The same with no trailing newline on the long line until much later: the
# discard has to survive being interrupted by the reader running out of input.
got=$(printf '#variable A = 1\n%s' "$over" |
	  repl ./csp "$D/slow4.db" --no-eeprom --board mega |
	  tr -d '\a' | grep -c 'A integer = 1')
ck "an unterminated over-long line strands nothing" "0" "$got"


echo "================================================"
echo "slow: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
