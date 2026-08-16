#!/bin/bash
# repl.sh -- tests for what only exists at the REPL/persistence level: the /list
# segment tags, what survives a /clear, and whether a generated ROM image loads
# back into a firmware that links it.
#
# The unit suite (tests/unit, run_tests.escript) checks VALUES: it runs a program
# for N cycles and reads variables out of the state dump. None of that reaches a
# /list line, an eeprom round trip or a linked ROM image, so those live here.
#
# Fast -- part of `make test`. Exit 0 = all cases pass.
set -u
cd "$(dirname "$0")/.." || exit 1
D=tmp/repl
mkdir -p "$D"

pass=0; fail=0

# ck NAME EXPECTED ACTUAL -- compare verbatim, print a diff on mismatch.
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

# repl BIN EEPROM [FILE...] < commands -- run the REPL and strip the banner plus
# the echoed input, so a case compares the ANSWERS and nothing else. The REPL
# prints "> " and then the line it read, so a line starting with "> " is input
# coming back, never output.
repl() {
    local bin="$1" ee="$2"; shift 2
    timeout 20 "$bin" -i -e "$ee" -c 0 "$@" 2>&1 |
	grep -v '^CandySpeak Interactive Mode$' |
	grep -v '^Type /help for commands' |
	grep -v '^> ' | grep -v '^$'
}

# Link a host binary that carries PROGRAM as its firmware ROM image, the same way
# a board does: -C generates the image, the image compiles in place of rom_host.c.
# Without this there is no way to see an F tag at all -- the stock ./csp links the
# neutral image, so every line it can ever list is RAM.
#
# `make rom` and not a gcc line of its own: the source list belongs in one place,
# and this used to be a second copy of it that nothing kept in step.
build_rom() {
    local src="$1" out="$2"
    make -s rom PROG="$src" OUT="$out" >/dev/null 2>&1
}

cat > "$D/prog.csp" <<'EOF'
#digital Led out 0:13
#timer Beat 500 = 1
#variable Seq = 0
Beat = 1 ? timeout(Beat)
Seq = Seq + 1 ? timeout(Beat)
EOF

# --- 1. tags on a program typed into RAM ------------------------------------
# Nothing has been saved, so nothing is recoverable: every line is R.
echo "list tags:"
rm -f "$D/t1.db"
got=$(printf '/list\n/quit\n' | repl ./csp "$D/t1.db" "$D/prog.csp")
ck "fresh RAM program is all R" \
'#digital Led out 0:13  // R
#timer Beat 500 = 1  // R
#variable Seq:32 integer = 0  // R
Beat=1 ? timeout(Beat)  // 1 R
Seq=Seq+1 ? timeout(Beat)  // 2 R' "$got"

# --- 2. after a save the same lines are backed ------------------------------
rm -f "$D/t2.db"
got=$(printf '/save\n/list\n/quit\n' | repl ./csp "$D/t2.db" "$D/prog.csp" | grep -v '^Saved')
ck "after /save every line is E" \
'#digital Led out 0:13  // E
#timer Beat 500 = 1  // E
#variable Seq:32 integer = 0  // E
Beat=1 ? timeout(Beat)  // 1 E
Seq=Seq+1 ? timeout(Beat)  // 2 E' "$got"

# --- 3. reload, then patch: the patch is NOT backed -------------------------
# This is the case the tag exists for. Both halves live in the same RAM patch and
# look identical in /state -- only the eeprom watermark separates them.
got=$(printf '#variable Extra = 0\nExtra = Extra + 2 ? timeout(Beat)\n/list\n/quit\n' |
	  repl ./csp "$D/t2.db" | grep -v '^Restored' | grep -v '^OK$')
ck "a patch added after /load lists R among E" \
'#digital Led out 0:13  // E
#timer Beat 500 = 1  // E
#variable Seq:32 integer = 0  // E
#variable Extra:32 integer = 0  // R
Beat=1 ? timeout(Beat)  // 1 E
Seq=Seq+1 ? timeout(Beat)  // 2 E
Extra=Extra+2 ? timeout(Beat)  // 3 R' "$got"

# --- 4. saving again promotes the patch -------------------------------------
got=$(printf '#variable Extra = 0\n/save\n/list\n/quit\n' |
	  repl ./csp "$D/t2.db" | grep -v '^Restored' | grep -v '^OK$' |
	  grep -v '^Saved' | grep Extra)
ck "a second /save promotes R to E" '#variable Extra:32 integer = 0  // E' "$got"

# --- 5. /clear says the eeprom copy survives --------------------------------
# "Cleared" on its own reads like the program is gone; it is not, and the next
# boot brings it back. A user deciding whether to type /clear needs to know.
echo "clear:"
got=$(printf '/clear\n/list\n/quit\n' | repl ./csp "$D/t2.db" | grep -v '^Restored')
ck "/clear reports the eeprom copy is kept" \
'Cleared RAM patches -- ROM restored (eeprom copy kept, /load restores it)' "$got"

got=$(printf '/quit\n' | repl ./csp "$D/t2.db" | grep -c 'Restored')
ck "and the copy really is still there" "1" "$got"

# --- 6. a generated ROM image loads back ------------------------------------
# The generator writes the image and its CRCs; the runtime checks them at boot.
# If the two disagree the board rejects its own firmware and comes up empty --
# silently, apart from one line, and only on a board that HAS a ROM.
echo "rom image:"
if ! build_rom "$D/prog.csp" "$D/csprom"; then
    echo "  FAIL could not build a ROM-linked binary"; fail=$((fail+1))
else
    got=$(printf '/quit\n' | repl "$D/csprom" "$D/t6.db" --no-eeprom)
    ck "firmware accepts its own image" "" "$got"

    # --- 7. F for ROM, R for what is typed on top ---------------------------
    got=$(printf '/list\n/quit\n' | repl "$D/csprom" "$D/t7.db" --no-eeprom |
	      grep -v '^ROM rejected')
    ck "ROM lines list F" \
'#digital Led out 0:13  // F
#timer Beat 500 = 1  // F
#variable Seq:32 integer = 0  // F
Beat=1 ? timeout(Beat)  // 1 F
Seq=Seq+1 ? timeout(Beat)  // 2 F' "$got"

    rm -f "$D/t8.db"
    got=$(printf '#variable Extra = 0\n/list\n/quit\n' |
	      repl "$D/csprom" "$D/t8.db" | grep -v '^ROM rejected' |
	      grep -v '^OK$' | grep Extra)
    ck "a RAM patch on top of ROM lists R" '#variable Extra:32 integer = 0  // R' "$got"
fi

# --- 8. buffers and fields in /state ----------------------------------------
# A buffer is neither digital nor analog and has no pin. Reading one as analog
# printed a port:pin pair off the wrong union arm -- numbers that looked like
# configuration and were the frame's first bytes.
echo "state rows:"
cat > "$D/buf.csp" <<'EOF'
#buffer Tx:8 out can 0x201
#buffer Rx:8 in  can 0x200
#field TxSeq:16 unsigned Tx[0..15]
EOF
# The field is written from the PROMPT, not by a rule: an immediate assignment
# lands before the next /state, whereas a rule would make the case depend on how
# many cycles the REPL happened to run between two piped lines.
got=$(printf 'TxSeq = 7\n/state\n/quit\n' |
	  repl ./csp "$D/t9.db" --no-eeprom "$D/buf.csp" |
	  sed -n '/^Tx\|^Rx\|^TxSeq/p')
ck "buffer and field rows show what a buffer HAS" \
'Tx           out     buffer   0x201/8 = 07 00 00 00 00 00 00 00  TX
Rx           in      buffer   0x200/8 = 00 00 00 00 00 00 00 00
TxSeq        out     field    [0..15] = 7' "$got"

# A plain RAM buffer has no frame id -- the column stays empty rather than
# inventing one out of the transport union. And it takes FIELDS: a field is a bit
# window into storage, and the transport says how that storage reaches the
# outside world, which is none of the window's business. #field used to demand
# TR_CAN and refuse this with "word  not a module".
printf '#buffer B:4 out\n#field Lo:8 unsigned B[0..7]\n#field Hi:8 unsigned B[8..15]\n' \
    > "$D/ram.csp"
got=$(printf 'Lo = 9\nHi = 255\n/state\n/quit\n' |
	  repl ./csp "$D/t10.db" --no-eeprom "$D/ram.csp" |
	  sed -n '/^B \|^Lo\|^Hi/p')
ck "a plain RAM buffer takes fields and has no id column" \
'B            out     buffer           = 09 ff 00 00
Lo           out     field    [0..7]  = 9
Hi           out     field    [8..15] = 255' "$got"

# ...and a field over something that is not a buffer says so, by name.
got=$(printf '#variable X = 0\n#field Bad:8 unsigned X[0..7]\n/quit\n' |
	  repl ./csp "$D/t10b.db" --no-eeprom | grep -v '^OK$')
ck "a field over a non-buffer names it" "Error: X is not a buffer" "$got"

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
got=$(printf '%s\n/quit\n' "$long" | repl ./csp "$D/t11.db" --no-eeprom --board mega |
	  tr -d '\a')
ck "an over-long line is refused with a reason" \
"Error: line too long, max 95 characters -- line ignored" "$got"

# ...and nothing from it reached the program: refused, not run short.
got=$(printf '%s\n/list\n/quit\n' "$long" | repl ./csp "$D/t12.db" --no-eeprom --board mega |
	  tr -d '\a' | grep -c 'Wide')
ck "and nothing from it was declared" "0" "$got"

# The buffer is sized from the pool, which is the whole point of moving it there:
# the same paste that a mega refuses goes through on a board with room.
got=$(printf '%s\n/list\n/quit\n' "$long" | repl ./csp "$D/t13.db" --no-eeprom |
	  tr -d '\a' | grep -c 'Wide')
ck "a board with room accepts the same line" "1" "$got"

# --- 10b. a declaration that does not FIT is refused, not fatal --------------
# Whether a declaration fits is not known at parse time: it is the derived tables
# (view, heap, buffer table) that run out of arena, and those are laid out by the
# rebuild AFTER the line parses. That result used to be discarded -- the line was
# answered "OK", the runtime was left with every table NULL, and the next cycle
# read a null heap slot. A segfault two frames into states_advance, with nothing
# on screen to say the program had outgrown the board.
echo "out of memory:"
oom=$(printf '#buffer B1:1023\n#buffer B2:1023\n/quit\n' |
	  repl ./csp "$D/t10c.db" --no-eeprom --board mega | grep -v '^OK$')
ck "a declaration too big for the board is refused with a reason" \
"Error: out of memory -- program does not fit" "$oom"

# ...and the refused line costs nothing: the program is what it was before it,
# and the REPL still runs. Both matter -- the rollback has to put the tables
# back, or everything after this is talking to a runtime with no storage.
got=$(printf '#buffer B1:1023\n#buffer B2:1023\n#variable V = 42\n> V\n/quit\n' |
	  repl ./csp "$D/t10d.db" --no-eeprom --board mega | tail -1)
ck "and the REPL still evaluates after the refusal" "42" "$got"

got=$(printf '#buffer B1:1023\n#buffer B2:1023\n/list\n/quit\n' |
	  repl ./csp "$D/t10e.db" --no-eeprom --board mega | grep -c 'B2')
ck "and nothing from the refused line was declared" "0" "$got"

# --- 11. a module lists as a block ------------------------------------------
# The members were inside `#module ... #end`, but the RULES came after it with a
# "Mod: " prefix -- which is not source, so the one listing you would actually
# paste back could not be. Rules belong in the block, indented like the source.
# The numbering stays ABSOLUTE (module rules first here, because the body comes
# first in the instruction stream), so #disable still means the same rule.
echo "module block:"
cat > "$D/mod.csp" <<'EOF'
#digital Led out 0:13
#module Blink
  #digital P 0:1
  #timer T 500
  #variable V = 0
  #in INIT
    P.dir = out
  #end
  P = V, V = !V ? timeout(T)
#end
#Blink b
Led = 1 ? 1
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/t14.db" --no-eeprom "$D/mod.csp")
ck "module rules list inside the block, indented" \
'#digital Led out 0:13  // R
#module Blink  // R
  #digital P in 0:1  // R
  #timer T 500  // R
  #variable V:32 integer = 0  // R
  #in INIT  // R
    P.dir=out  // 1 R
  #end   // R
  P=V,V=!V ? timeout(T)  // 2 R
#end   // R
#Blink b  // R
Led=1 ? 1  // 3 R' "$got"

# --- 11b. a filtered listing shows no empty scaffolding ----------------------
# `#in` headers and the `#module` wrapper were printed as the walk passed them,
# before knowing whether anything inside would survive the filter. `/list Led`
# on this program answered with more lines of empty block than of matching rule.
# They are held back now and go out with the first line under them.
echo "list filters:"
got=$(printf '/list Led\n/quit\n' | repl ./csp "$D/t14b.db" --no-eeprom "$D/mod.csp")
ck "a filtered listing drops blocks nothing survived in" \
'#digital Led out 0:13  // R
Led=1 ? 1  // 3 R' "$got"

# ...but an UNFILTERED listing stays faithful: `#in INIT` with an empty body is
# something the source says, and a listing you can paste back has to keep it.
printf '#digital L out 0:13\n#in INIT\n#end\nL = 1 ? 1\n' > "$D/empty_in.csp"
got=$(printf '/list\n/quit\n' | repl ./csp "$D/t14c.db" --no-eeprom "$D/empty_in.csp")
ck "an unfiltered listing keeps a block that is empty in the source" \
'#digital L out 0:13  // R
#in INIT  // R
#end   // R
L=1 ? 1  // 1 R' "$got"

# `:S` asks which rules RUN in S. It registered the state and then tested nothing,
# so it listed the whole program. A rule matches when its `#in` block covers S.
printf '#variable X = 0\n#states one two\n#in one\n  X = 1\n#end\n#in two\n  X = 2\n#end\n' \
    > "$D/twost.csp"
got=$(printf '/list :one\n/quit\n' | repl ./csp "$D/t14d.db" --no-eeprom "$D/twost.csp")
ck "a :state filter keeps only the block that covers it" \
'#variable X:32 integer = 0  // R
#states one two  // R
#in one  // R
  X=1  // 1 R
#end   // R' "$got"

# A bare top-level rule is NOT ungated: the implicit NORMAL+ wrap gives it a real
# gate over INIT and NORMAL, so it answers to those two states and no others.
printf '#variable Y = 0\n#states three\nY = 9 ? 1\n' > "$D/bare.csp"
got=$(printf '/list :NORMAL\n/quit\n' | repl ./csp "$D/t14e.db" --no-eeprom "$D/bare.csp" |
	  grep -c 'Y=9')
ck "a bare rule answers to NORMAL" "1" "$got"

got=$(printf '/list :three\n/quit\n' | repl ./csp "$D/t14f.db" --no-eeprom "$D/bare.csp" |
	  grep -c 'Y=9')
ck "and not to a user state" "0" "$got"

# --- 12. pasting a source file -----------------------------------------------
# Every comment line came back as "Unknown command: // ...": `//` matched the
# leading-'/' command test. Pasting a .csp file into the prompt is what the
# prompt is FOR, and most files open with a comment header.
echo "paste source:"
got=$(printf '// a header comment\n//\n   // indented, after blanks\n#digital Led out 0:13\nLed = 1 ? 1\n/list\n/quit\n' |
	  repl ./csp "$D/t15.db" --no-eeprom | grep -v '^OK$')
ck "comment lines are quietly ignored" \
'#digital Led out 0:13  // R
Led=1 ? 1  // 1 R' "$got"

# --- 13. a burst larger than one line ----------------------------------------
# The buffer doubles as the input QUEUE: what arrives while a line is running is
# stored behind it and re-fed afterwards, instead of being left to back up in the
# driver's FIFO. The lines that matter here are the ones that ADD something --
# each triggers a rebuild, which is the slow window a paste has to survive.
#
# All of it goes in as one write, so the reader really does drain past several
# newlines in a single pass. Every line must still be seen, in order, once.
echo "burst:"
burst='#variable A = 0
#variable B = 0
#variable C = 0
A = 1 ? 1
B = 2 ? 1
C = 3 ? 1
/list
/quit'
got=$(printf '%s\n' "$burst" | repl ./csp "$D/t16.db" --no-eeprom | grep -v '^OK$')
ck "a multi-line burst arrives whole and in order" \
'#variable A:32 integer = 0  // R
#variable B:32 integer = 0  // R
#variable C:32 integer = 0  // R
A=1 ? 1  // 1 R
B=2 ? 1  // 2 R
C=3 ? 1  // 3 R' "$got"

# The same burst on a board whose line buffer is small: the queue fills, the
# reader stops draining, and the rest waits in the port. Nothing may be lost.
got=$(printf '%s\n' "$burst" | repl ./csp "$D/t17.db" --no-eeprom --board mega |
	  grep -v '^OK$')
ck "and again with a 96-byte buffer" \
'#variable A:32 integer = 0  // R
#variable B:32 integer = 0  // R
#variable C:32 integer = 0  // R
A=1 ? 1  // 1 R
B=2 ? 1  // 2 R
C=3 ? 1  // 3 R' "$got"

# --- 14. pasting a whole program, module and all -----------------------------
# A definition being typed is not runnable: `#module` emits an OP_ENTER whose
# length is patched at its `#end`, and `#in` an OP_INSTATE whose skip is patched
# the same way. Running a cycle in between walked into unpatched offsets and hung
# the REPL part-way through -- which is exactly what pasting a .csp file does.
# `timeout` in repl() turns a hang into a failure rather than a stuck suite.
#
# Its OWN fixture, not examples/traffic.csp. The case pastes a program and
# compares the listing line for line, so pointing it at a live example coupled a
# REPL behaviour test to a demo that is meant to be edited: changing the demo
# broke the test, which says nothing about the REPL. What is under test is that a
# module survives being typed in a burst -- any module will do.
echo "paste a program:"
cat > "$D/paste.csp" <<'EOF'
#digital Red    out 1
#timer   Phase  500 = 1
#states  red green

#module Failsafe
  #digital P1  1
  #digital P5  5
  #digital P9  9
  #timer   T   500
  #variable V = 0

  #in INIT
    P1.dir = out, P1 = 0
    P5.dir = out, P5 = 0
    P9.dir = out, P9 = 0
    V=1
  #end

  P5=V, V=!V ? timeout(T)
  T=1 ? timeout(T)
#end

#Failsafe safe
EOF
got=$(printf '%s\n/list\n/quit\n' "$(cat "$D/paste.csp")" |
	  repl ./csp "$D/t18.db" --no-eeprom | grep -v '^OK$' |
	  sed -n '/^#module/,/^#end/p')
ck "a pasted module survives and lists back" \
'#module Failsafe  // R
  #digital P1 in 0:1  // R
  #digital P5 in 0:5  // R
  #digital P9 in 0:9  // R
  #timer T 500  // R
  #variable V:32 integer = 0  // R
  #in INIT  // R
    P1.dir=out,P1=0  // 1 R
    P5.dir=out,P5=0  // 2 R
    P9.dir=out,P9=0  // 3 R
    V=1  // 4 R
  #end   // R
  P5=V,V=!V ? timeout(T)  // 5 R
  T=1 ? timeout(T)  // 6 R
#end   // R' "$got"

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
	  repl ./csp "$D/t19.db" --no-eeprom --board mega |
	  tr -d '\a' | grep -v '^OK$')
ck "the REPL keeps working after an over-long line" \
'Error: line too long, max 95 characters -- line ignored
#variable Before:32 integer = 1  // R
#variable After:32 integer = 2  // R' "$got"

# The same with no trailing newline on the long line until much later: the
# discard has to survive being interrupted by the reader running out of input.
got=$(printf '#variable A = 1\n%s' "$over" |
	  repl ./csp "$D/t20.db" --no-eeprom --board mega |
	  tr -d '\a' | grep -c 'A integer = 1')
ck "an unterminated over-long line strands nothing" "0" "$got"

# --- 16. string variables ----------------------------------------------------
# A string variable holds a POSITION in the string table, and assignment moves
# the position -- no copying, no heap. So it can only ever be another constant,
# which is the whole point: nothing mutates, so nothing needs allocating.
#
# Quoting matters as much as the value: a listing has to paste back, and
# `= World` reads as a reference to something named World.
echo "strings:"
got=$(printf '#variable A string = "World"\n#constant Y string = "Foo"\nA = "hello there" ? 1\n/list\n/quit\n' |
	  repl ./csp "$D/t21.db" --no-eeprom | grep -v '^OK$')
ck "strings list quoted, in decls and in rules" \
'#variable A:32 string = "World"  // R
#constant Y:32 string = "Foo"  // R
A="hello there" ? 1  // 1 R' "$got"

# Assign from another string constant, and print it.
got=$(printf '#variable A string = "World"\n#constant Y string = "Foo"\nA = Y\nprintln(A)\n/quit\n' |
	  repl ./csp "$D/t22.db" --no-eeprom | grep -v '^OK$' | grep -v '^[0-9]*$')
ck "a string variable takes another constant" "Foo" "$got"

# A string declared without an initialiser holds position 0. Printing it walked
# to pos-1 for the length byte and read outside the table -- a segfault here,
# and whatever a board has at that address otherwise.
got=$(printf '#variable S string\n/list\n/state\n/quit\n' |
	  repl ./csp "$D/t23.db" --no-eeprom | grep -v '^OK$' | grep '^#variable S\|^S ')
ck "an uninitialised string is empty, not a crash" \
'#variable S:32 string = ""  // R
S                                     = ' "$got"

# --- 17. string equality and .len --------------------------------------------
# Equality is POSITION equality, which works because lookup_string deduplicates:
# the same text always lands at the same position, whoever writes it. So `==`
# needed nothing -- this case exists to keep it that way.
#
# .len reads the length byte in front of the text. It has to answer from BOTH
# view kinds: a plain #variable gets an auto-buffer and is a HEAP view, while a
# #constant is a value slot.
echo "string ops:"
cat > "$D/str.csp" <<'EOF'
#constant Y string = "Hello!"
#variable A string = "Foo"
#variable B string = "Bar"
#variable Empty string
#variable N = 9
#variable Same = 0
#variable Cross = 0
#variable Diff = 0
#variable Lc = 0
#variable Lv = 0
#variable Le = 0
#variable Ln = 0
B = "Foo"
Same  = 1 ? A == "Foo"
Cross = 1 ? A == B
Diff  = 1 ? A == "Bar"
Lc = Y.len ? 1
Lv = A.len ? 1
Le = Empty.len ? 1
Ln = N.len ? 1
EOF
got=$(./csp -c 4 -s /dev/stdout "$D/str.csp" 2>&1 | tail -1 |
	  grep -o '"\(Same\|Cross\|Diff\|Lc\|Lv\|Le\|Ln\)",[0-9]*' | tr '\n' ' ')
ck "string == compares positions, .len reads the length byte" \
'"Same",1 "Cross",1 "Diff",0 "Lc",6 "Lv",3 "Le",0 "Ln",0 ' "$got"

# --- 18. the exec-only build still runs -------------------------------------
# CSP_EXEC_ONLY drops the scanner, the parser and the command layer. What is
# left has to still run a linked ROM image -- which is the whole point of the
# tier, and exactly the kind of thing that rots silently because no normal build
# exercises it. Link one against a generated image and check it computes.
echo "exec-only:"
printf '#digital Led out 0:13\n#timer T 500 = 1\n#variable N = 0\nT = 1 ? timeout(T)\nN = N + 1 ? timeout(T)\n' > "$D/eo.csp"
if ./csp -n -C -O "$D/eo_rom.c" "$D/eo.csp" >/dev/null 2>&1 &&
   gcc -DCSP_VERSION='"test"' -DCSP_ARENA_MALLOC -DCSP_EXEC_ONLY -I. \
       csp_linux.c csp_rt.c csp_repl.c csp_compile.c csp_tok.c csp_dump.c \
       csp_eeprom.c csp_parse.c csp_print.c csp_strings.c "$D/eo_rom.c" -o "$D/csp_exec" \
       >/dev/null 2>&1; then
    got=$("$D/csp_exec" -c 6 --no-eeprom -s /dev/stdout 2>&1 | tail -1 |
	      grep -o '"State",[0-9]*\|"N",[0-9]*' | tr '\n' ' ')
    ck "an exec-only build runs its ROM" '"State",1 "N",2 ' "$got"
else
    echo "  FAIL exec-only build did not link"; fail=$((fail+1))
fi

# --- 19. the bit engine ------------------------------------------------------
# csp_bits.h replaced bitpack.h under csp_heap_get/set (2 698 -> 804 bytes on
# AVR). Nothing in tests/unit reaches this far down -- a wrong bit order would
# show up as a corrupted CAN field, not a failed rule -- so the equivalence with
# the old implementation is proven directly: every position and width the view
# encoding allows, both orders, against a buffer that already has content.
echo "bit engine:"
if gcc -I. -O2 -o "$D/bits_cmp" tests/bits_cmp.c >/dev/null 2>&1; then
    ck "csp_bits matches bitpack bit for bit" "ok, identical" "$("$D/bits_cmp")"
else
    echo "  FAIL bits_cmp did not build"; fail=$((fail+1))
fi

# --- 20. the part layout -----------------------------------------------------
# csp_part.h hand-writes the bit position of every .part inside value_t. Those
# are bitfields in different union arms, so a wrong number corrupts data instead
# of failing to compile. This probes the real structs (all-ones into one field,
# read the word back) and checks every row against the probe, then round-trips
# the engine. It is the reason the table is allowed to be hand-written at all.
echo "part layout:"
if gcc -I. -O2 -o "$D/part_layout" tests/part_layout.c >/dev/null 2>&1; then
    ck "csp_part table matches the value_t structs" "ok, identical" "$("$D/part_layout")"
else
    echo "  FAIL part_layout did not build"; fail=$((fail+1))
fi

# csp_states_t packs six state names into one declaration, and the whole design
# rests on slot 0 aliasing DECL_COMMON's `name` -- an alignment nothing would
# fail to compile over. This probes the real struct instead of restating the
# numbers, so reordering the fields or changing NAMEPOS_BITS is caught here
# rather than as first-state-of-every-block lookups quietly missing.
echo "states layout:"
if gcc -I. -O2 -o "$D/states_layout" tests/states_layout.c >/dev/null 2>&1; then
    ck "csp_states_t packs six names, slot 0 aliases name" "ok, identical" "$("$D/states_layout")"
else
    echo "  FAIL states_layout did not build"; fail=$((fail+1))
fi

# --- arrays -----------------------------------------------------------------
# `#variable A[3]` is three declarations: the head keeps the name, the tail two
# carry `cont` and no name at all. A[<const>] folds to the element's own
# declaration (no instruction, bounds checked here); A[<expr>] becomes an
# OP_SETOX in front of the access, bounds checked every cycle against the length
# baked into it.
echo "arrays:"

cat > "$D/arr.csp" <<'EOF'
#variable A[3] = 0
#variable I = 0
#variable x = 0
EOF

# The declaration collapses back to one source line with its length on. Without
# the `[3]` a /list pastes back a scalar and the other two elements are gone.
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr1.db" --no-eeprom "$D/arr.csp" |
	  sed -n '/^#variable A/p')
ck "an array lists as one line with its length" \
   '#variable A[3]:32 integer = 0  // R' "$got"

# Constant subscripts: distinct elements, and A with no subscript is element 0.
got=$(printf 'A[0] = 10\nA[1] = 20\nA[2] = 30\n> A[0]\n> A[1]\n> A[2]\n/quit\n' |
	  repl ./csp "$D/arr2.db" --no-eeprom "$D/arr.csp" | grep -v '^OK$')
ck "constant subscripts address distinct elements" \
   '10
20
30' "$got"

# Out of range is caught at COMPILE time for a constant -- it costs nothing at
# run time and the message arrives on the line that is wrong.
got=$(printf 'A[3] = 1\n/quit\n' |
	  repl ./csp "$D/arr3.db" --no-eeprom "$D/arr.csp" |
	  sed -n '/^Error/p')
ck "a constant subscript past the end is refused" \
   'Error: index out of range' "$got"

# The runtime subscript, read and write. Both go through OP_SETOX.
got=$(printf 'I = 1\nx = A[I] ? 1\n/quit\n' |
	  repl ./csp "$D/arr4.db" --no-eeprom "$D/arr.csp" |
	  sed -n '/^Error/p')
ck "a runtime subscript read compiles" '' "$got"

# A runtime index past the end is caught every cycle by the length baked into
# the SETOX. It cannot be caught at compile time, and reading outside the array
# is exactly what the check exists to stop.
cat > "$D/arrbad.csp" <<'EOF'
#variable A[3] = 0
#variable I = 9
#variable x = 0
x = A[I] ? 1
EOF
got=$(printf '/state\n/quit\n' | repl ./csp "$D/arr6.db" --no-eeprom "$D/arrbad.csp" |
	  sed -n '/index out of range/p' | head -1)
ck "a runtime subscript past the end is caught at run time" \
   'index out of range' "$got"

# A constant array lists its whole init list back. The head alone would paste
# back as a scalar with the other elements gone.
cat > "$D/arrc.csp" <<'EOF'
#constant CT[4] = { -100, -81, 31, 100 }
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr9.db" --no-eeprom "$D/arrc.csp" |
	  sed -n '/^#constant/p')
ck "a constant array lists its init list back" \
   '#constant CT[4]:32 integer = { -100, -81, 31, 100 }  // R' "$got"

# ...and survives being baked into a ROM image and loaded back. Every element is
# its own declaration carrying `cont`, and that bit is real data now: an emitter
# that dropped it would both unmake the array and fail the decl-section CRC.
if build_rom "$D/arrc.csp" "$D/arrc_fw"; then
    got=$(printf '/list\n/quit\n' | repl "$D/arrc_fw" "$D/arr10.db" --no-eeprom |
	      sed -n '/^#constant/p')
    ck "a constant array survives a ROM round trip" \
       '#constant CT[4]:32 integer = { -100, -81, 31, 100 }  // F' "$got"
else
    echo "  FAIL constant-array ROM did not build"; fail=$((fail+1))
fi

# An element of an init list is a constant EXPRESSION, and it is one because the
# list is matched by the same P_CONST_S a scalar initialiser uses instead of by
# a hand-written scan over INT tokens -- that scan understood a leading '-' and
# nothing else. Strings come along for the ride, under the same `string` the
# scalar form needs.
cat > "$D/arrce.csp" <<'EOF'
#constant N = 4
#constant EX[3] = { 1+2, N*2, -5 }
#constant SS[2] string = { "a", "bc" }
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr14.db" --no-eeprom "$D/arrce.csp" |
	  sed -n '/^#constant EX\|^#constant SS/p')
ck "init list elements are constant expressions" \
   '#constant EX[3]:32 integer = { 3, 8, -5 }  // R
#constant SS[2]:32 string = { "a", "bc" }  // R' "$got"

# A declared length and a list that disagree is a mistake, not something to pad
# or truncate. Checked BEFORE any declaration is made, which is why the list is
# walked twice -- counted, then written.
cat > "$D/arrcbad.csp" <<'EOF'
#constant CT[4] = { 1, 2, 3 }
EOF
./csp -n -c 0 "$D/arrcbad.csp" >/dev/null 2>&1
ck "an init list that disagrees with the length is refused" "1" "$?"

# `A[<expr>] = rhs`. The LEFT of a rule body is matched by pat_body, not by the
# expression parser, so this is a separate path from the reads -- it emits the
# SETOX in front of the STORE, and the arming has to happen after the right side
# is loaded or that load's own access consumes the one-shot.
cat > "$D/arrwr.csp" <<'EOF'
#variable A[3] = 0
#variable I = 2
A[I] = 99 ? 1
EOF
got=$(./csp -n -P "$D/arrwr.csp" 2>&1 | sed -n "/SETOX/p")
ck "a runtime subscript write emits a bounds-checked SETOX" \
   "{instr,5,'SETOX',[r1,{len,3},{stride,1}]}." "$got"

# A rule that uses a subscript has to LIST with it. Without this an array
# program pasted back out of a board came home reading element 0 everywhere:
# the runtime index vanished, and a constant one folded to a continuation, which
# has no name and printed nothing at all.
cat > "$D/arrls.csp" <<'EOF'
#variable A[3] = 0
#variable I = 0
#variable x = 0
x = A[I] ? 1
x = A[2] ? 1
A[I] = 5 ? 1
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr12.db" --no-eeprom "$D/arrls.csp" |
	  grep -E '^(x=|A\[)')
ck "rules list their subscripts back" \
   'x=A[I] ? 1  // 1 R
x=A[2] ? 1  // 2 R
A[I]=5 ? 1  // 3 R' "$got"

# --- device arrays ----------------------------------------------------------
# One declaration line, one pin per element. Possible at all because the pin
# lives in the per-element STORAGE, seeded from the declaration -- so ten
# elements sharing one declaration still drive ten different outputs.
cat > "$D/arrd.csp" <<'EOF'
#analog P[10]:16 out 9:0..9
#digital D[5] in 0:1..3,7,9
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr11.db" --no-eeprom "$D/arrd.csp" |
	  sed -n '/^#analog\|^#digital/p')
ck "device arrays list their pin spec back" \
   '#analog P[10]:16 out 9:0..9  // R
#digital D[5] in 0:1..3,7,9  // R' "$got"

# A pin LIST with no range in it, and pins spread over SEVERAL PORTS. The plain
# list is the case that never worked: read as a port, the leading integer's
# stop-set was ':' alone, so in `0:1,4,7` the scan ran to the next colon on the
# line -- or off the end -- and folded the whole list into one number. Every
# form here has to survive a listing, since that is what a board hands back.
cat > "$D/arrdp.csp" <<'EOF'
#analog C[3]:16 out 0:1,4,7
#digital E[4] in 0:2,1:5,2:6,3:7
#analog D[9]:16 out 1:1..3,2:1,3,5,9:,7..9
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr13.db" --no-eeprom "$D/arrdp.csp" |
	  sed -n '/^#analog\|^#digital/p')
ck "pin lists and several ports list back" \
   '#analog C[3]:16 out 0:1,4,7  // R
#digital E[4] in 0:2,1:5,2:6,3:7  // R
#analog D[9]:16 out 1:1..3,2:1,3,5,9:7..9  // R' "$got"

# An #analog is SIGNED by default, so `unsigned` has to survive a listing --
# without it the line pastes back signed and every reading above half scale
# comes home negative. Nothing else prints the type, so nothing else caught it.
cat > "$D/arru.csp" <<'EOF'
#analog U:16 out unsigned 9:0
#analog S:16 out 9:1
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/arr15.db" --no-eeprom "$D/arru.csp" |
	  sed -n '/^#analog/p')
ck "an unsigned analog lists as unsigned" \
   '#analog U:16 out unsigned 9:0  // R
#analog S:16 out 9:1  // R' "$got"

# A length and a pin list that disagree is a typo. Silently padding would leave
# the extra elements on pin 0, which is a real pin on every board here.
cat > "$D/arrdbad.csp" <<'EOF'
#analog P[10]:16 out 9:0..3
EOF
./csp -n -c 0 "$D/arrdbad.csp" >/dev/null 2>&1
ck "too few pins for the declared length is refused" "1" "$?"

# The flagship: the array rewrite of cpx_ball, which is what the whole feature
# was for. 50 rules become 7 and it has to still compile as a ROM.
if build_rom examples/cpx_ball_array.csp "$D/ball_fw"; then
    ck "cpx_ball_array builds and its ROM loads" "0" "0"
else
    echo "  FAIL cpx_ball_array did not build"; fail=$((fail+1))
fi

# OP_SETO and OP_SETOX mean nearly the same thing and their payloads OVERLAP, so
# emitting one through the other's arm compiles fine and produces a plausible
# wrong word. That is the shape of all three CRC mismatches this project has had.
echo "instr layout:"
if gcc -I. -O2 -o "$D/instr_layout" tests/instr_layout.c >/dev/null 2>&1; then
    ck "SETO/SETOX formats stay distinct" "ok, distinct" "$("$D/instr_layout")"
else
    echo "  FAIL instr_layout did not build"; fail=$((fail+1))
fi

# --- #local -----------------------------------------------------------------
# A #local BINDS a formula. The mistake it invites is assigning to it later, and
# "unknown variable" would be a lie -- the name resolves fine, it is what it
# MEANS that is wrong.
echo "local:"
got=$(printf '#local q = 1\nq = 5\n/quit\n' | repl ./csp "$D/loc1.db" --no-eeprom |
	  sed -n '/^Error/p')
ck "assigning to a #local is refused by name" \
   'Error: cannot assign to a #local -- it binds a formula' "$got"

# It lists as #local, not as #variable: pasted back as a variable, every step of
# a chain would lag a cycle instead of resolving in one.
cat > "$D/loc.csp" <<'EOF'
#variable a = 7
#local sum = a + 1
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/loc2.db" --no-eeprom "$D/loc.csp" |
	  grep -E '^#local')
ck "a #local lists as a #local" '#local sum:32 integer  // R' "$got"

# Baked into a ROM and loaded back. `local` is REAL DATA -- it decides whether a
# leaf is single-buffered -- so an emitter that dropped it would both fail the
# decl-section CRC and, if it somehow loaded, turn every local into an ordinary
# variable that lags a cycle. Checking the bit is in the generated C is not
# enough: the image has to load and the chain has to still resolve in one cycle.
if build_rom tests/unit/local.csp "$D/loc_fw"; then
    # `sum` after ONE cycle: 10, not 0. That is the single-buffering surviving
    # the round trip -- an ordinary variable would still read its init here,
    # because a rule's write is not visible until the commit.
    got=$(printf '/list\n/state\n/quit\n' | repl "$D/loc_fw" "$D/loc3.db" --no-eeprom |
	      grep -E '^#local sum|^sum ')
    ck "a #local survives a ROM round trip" \
       '#local sum:32 integer  // F
sum                                   = 10' "$got"
else
    echo "  FAIL #local ROM did not build"; fail=$((fail+1))
fi

# --- the LPC / LPCOpen port -------------------------------------------------
# csp_lpcopen.c against stubbed chip drivers (tests/lpcstub). No LPC toolchain
# is involved: the stub declares the LPCOpen functions with the signatures the
# real headers have, so this compiles and LINKS the port the same way a firmware
# build does -- which is what catches the two ways a port rots as the core moves
# under it, a function the core now calls that nothing implements and one it no
# longer calls that two files define.
#
# It runs, too. The stub's __WFI calls SysTick_Handler, so the idle wait in the
# main loop actually completes, and its UART reads stdin -- so this exercises
# boot, line assembly, csp_process_line, the rebuild and the listing.
#
# The Arduino port has no equivalent and cannot easily have one; it needs a core
# that only arduino-cli can supply.
echo "lpcopen:"
if gcc -g -Wall -I. -Itests/lpcstub -DCSP_VERSION='"test"' -o "$D/lpc_fw" \
       csp_lpcopen.c csp_rt.c csp_compile.c csp_parse.c csp_tok.c csp_print.c \
       csp_repl.c csp_dump.c csp_eeprom.c csp_strings.c rom_host.c \
       tests/lpcstub/stub.c >/dev/null 2>&1; then
    ck "the LPC port builds and links against the core" "0" "0"
    # A GPIO pin, an ADC channel (port 15) and a rule -- then list them back.
    # `0:13` means GPIO port 0 pin 13 on this port, which is the chip's own
    # numbering rather than a board pin table.
    # Strip CR and the XON/XOFF bytes first. The firmware paces its peer with
    # software flow control, so ^S/^Q land mid-stream -- right there in front of
    # the first line of a listing, which is exactly where a rebuild happens.
    got=$(printf '#digital Led out 0:13\n#analog Pot:10 in 15:3\nLed = 1 ? 1\n/list\n' |
	      timeout 20 "$D/lpc_fw" 2>&1 | tr -d '\021\023\r' |
	      sed -n '/^#digital\|^#analog\|^Led=/p')
    ck "the LPC port boots, takes input and lists it back" \
       '#digital Led out 0:13  // R
#analog Pot:10 in 15:3  // R
Led=1 ? 1  // 1 R' "$got"
else
    echo "  FAIL csp_lpcopen.c did not build"; fail=$((fail+1))
fi

echo "params:"

# #param is a DECL_CONSTANT with the `local` bit set -- the same trick #local
# plays on DECL_VARIABLE. What it has to prove is that it does NOT fold, since
# that is the one thing separating it from #constant at the point of use, and a
# folded param would bake today's value into every rule that reads it.
cat > "$D/param.csp" <<'EOF'
#param Kp:16 = 5
#variable Out = 0
Out = Kp * 2 ? 1
EOF

got=$(printf '/list\n/quit\n' | repl ./csp "$D/p1.db" "$D/param.csp")
ck "a param lists back as #param, and its rule does not fold it" \
   '#param Kp:16 integer = 5  // R
#variable Out:32 integer = 0  // R
Out=Kp*2 ? 1  // 1 R' "$got"

# The same program with #constant, to show the difference is real and not a
# listing cosmetic: there the reference IS folded.
sed 's/^#param/#constant/' "$D/param.csp" > "$D/const.csp"
got=$(printf '/list\n/quit\n' | repl ./csp "$D/p2.db" "$D/const.csp" | sed -n '/^Out=/p')
ck "the same declaration as #constant folds" 'Out=5*2 ? 1  // 1 R' "$got"

# Set from outside. How many cycles pass between two REPL lines is not fixed, so
# this checks that the write LANDS -- that the rule then follows is the ordinary
# OP_LD path the listing above already proves it takes.
got=$(printf '> Kp = 7\n> Kp\n/quit\n' | repl ./csp "$D/p3.db" "$D/param.csp")
ck "an immediate sets a param" '7
7' "$got"

# A rule may not. This is the half that makes it a param and not a variable.
got=$(printf 'Kp = 9 ? 1\n/quit\n' | repl ./csp "$D/p4.db" "$D/param.csp")
ck "a rule assigning to a param is refused" \
   'Error: cannot assign to a #param in a rule -- set it with > name = value' "$got"

# /state carries it: a param is exactly the thing whose live value can differ
# from what the source says.
got=$(printf '> Kp = 7\n/state\n/quit\n' | repl ./csp "$D/p5.db" "$D/param.csp" |
	  sed -n '/^Kp/p' | tr -s ' ')
ck "a param shows in /state, as a param" 'Kp param = 7' "$got"

# Re-declaring a param SETS it -- that is the mechanism for saving a value, and
# it has to work against a param baked into ROM, where cn.init sits in flash and
# cannot be written. The override is declared as a RAM shadow and csp_rt_start
# applies it onto the ROM param's slot by NAME.
if build_rom "$D/param.csp" "$D/param_fw"; then
    got=$(printf '#param Kp:16 = 9\n> Kp\n> Out\n/quit\n' |
	      repl "$D/param_fw" "$D/pr1.db")
    ck "a ROM param can be re-declared, and the ROM rule follows" 'OK
9
18' "$got"

    # The listing shows it ONCE, as the override, tagged P: the ROM row says
    # what the program shipped with, which is no longer what it runs with.
    got=$(printf '#param Kp:16 = 9\n/list\n/quit\n' | repl "$D/param_fw" "$D/pr1b.db")
    ck "an overridden param lists once, tagged P" 'OK
#variable Out:32 integer = 0  // F
#param Kp:16 integer = 9  // P
Out=Kp*2 ? 1  // 1 F' "$got"

    # /state is the mirror: the override has a slot of its own that nothing
    # reads, so the row shown is the param it sets.
    got=$(printf '#param Kp:16 = 9\n/state\n/quit\n' | repl "$D/param_fw" "$D/pr1c.db" |
	      sed -n '/^Kp/p' | tr -s ' ')
    ck "an overridden param shows one /state row" 'Kp param = 9' "$got"

    # ...and it survives a restart, through the ordinary EEPROM patch: the
    # override is a RAM declaration like any other, so /save already writes it.
    printf '#param Kp:16 = 9\n/save\n/quit\n' |
	repl "$D/param_fw" "$D/pr2.db" >/dev/null 2>&1
    got=$(printf '> Kp\n> Out\n/quit\n' | repl "$D/param_fw" "$D/pr2.db" |
	      grep -v '^Restored')
    ck "a re-declared ROM param survives a restart" '9
18' "$got"
else
    echo "  FAIL param ROM firmware did not build"; fail=$((fail+1))
fi

# A RAM param is written in place -- no shadow, one line in the listing.
got=$(printf '#param Kq:16 = 3\n#param Kq:16 = 8\n/list\n> Kq\n/quit\n' |
	  repl ./csp "$D/pr3.db")
ck "a RAM param is re-declared in place" 'OK
OK
#param Kq:16 integer = 8  // R
8' "$got"

# The width and type are what any compiled rule was built against.
got=$(printf '#param Kq:16 = 3\n#param Kq:32 = 8\n/quit\n' | repl ./csp "$D/pr4.db")
ck "a re-declaration may not change the width" 'OK
Error: #param Kq does not match the declaration it sets -- same width and type' "$got"

# And the exception is for params only.
got=$(printf '#variable V = 0\n#param V = 1\n/quit\n' | repl ./csp "$D/pr5.db")
ck "the exception does not extend to other declarations" 'OK
Error: name V is already defined' "$got"

# A #param where a CONSTANT is expected -- a timer period, a variable's
# initialiser. Folding it would defeat the point (the saved value would never
# reach the timer), so the declaration keeps the param's current value and the
# live one arrives through an INIT-time assignment. `#timer Tick Period = 1` was
# a syntax error before this, and `#variable Pt = SD` silently came out 0.
cat > "$D/pinit.csp" <<'EOF'
#param Period = 1000
#param SD = 7
#timer Tick Period = 1
#variable Pt = SD
EOF

# Consecutive declarations share ONE gate -- see asm_decl_init.
got=$(printf '/list\n/quit\n' | repl ./csp "$D/pi1.db" "$D/pinit.csp")
ck "a param initialiser becomes an INIT assignment" \
   '#param Period:32 integer = 1000  // R
#param SD:32 integer = 7  // R
#timer Tick 1000 = 1  // R
#variable Pt:32 integer = 7  // R
#in INIT  // R
  Tick.period=Period  // 1 R
  Pt=SD  // 2 R
#end   // R' "$got"

# ...and only while they ARE consecutive: a rule in between ends the block, so
# the next declaration opens its own rather than reaching back over it.
got=$(printf '#param A = 1\n#variable X = A\nX = X + 1 ? 1\n#variable Y = A\n/list\n/quit\n' |
	  repl ./csp "$D/pi1b.db" | grep -v '^OK$')
ck "a rule between two declarations ends the shared block" \
   '#param A:32 integer = 1  // R
#variable X:32 integer = 1  // R
#variable Y:32 integer = 1  // R
#in INIT  // R
  X=A  // 1 R
#end   // R
X=X+1 ? 1  // 2 R
#in INIT  // R
  Y=A  // 3 R
#end   // R' "$got"

# ...and that is what makes a saved setting reach them. Patch both params in a
# ROM image, restart, and read the timer period and the variable back.
if build_rom "$D/pinit.csp" "$D/pinit_fw"; then
    printf '#param Period = 250\n#param SD = 42\n/save\n/quit\n' |
	repl "$D/pinit_fw" "$D/pi2.db" >/dev/null 2>&1
    # TWO /state, and the second one is the answer: the INIT rule runs in cycle
    # 0 but its write sits in the DOUT shadow until the commit at the end of it,
    # so a /state issued while State is still INIT reads the DECLARED value.
    got=$(printf '/state\n/state\n/quit\n' | repl "$D/pinit_fw" "$D/pi2.db" |
	      sed -n '/^Tick\|^Pt/p' | tail -2 | tr -s ' ')
    ck "a saved param reaches the timer period and the variable" \
       'Tick running timer 250/250
Pt = 42' "$got"
else
    echo "  FAIL param-init ROM firmware did not build"; fail=$((fail+1))
fi

# The initialiser is an EXPRESSION now, so an unknown name in it has to be an
# error. It used to fail the whole optional and leave a silent zero behind.
got=$(printf '#variable Q = Zork\n/quit\n' | repl ./csp "$D/pi3.db")
ck "an undeclared name in an initialiser is refused" \
   'Error: variable Zork is not declared' "$got"

echo "unsigned through a ROM image:"

# The section CRC is folded over the RAW instruction words, so any bit the
# dumper does not emit fails the image at boot -- and the whole program then
# refuses to load, which reads as "variable X is not declared" on every line
# typed afterwards. Every payload field has had this bug once (see the OP_SETOX
# arm in csp_dump.c); csp_instr_alu_t.u is the newest.
#
# The unit suite cannot catch it: it runs RAM programs. The other build_rom
# cases here cannot either -- none of them does unsigned arithmetic, so .u was
# 0 in every instruction they ever dumped.
cat > "$D/uns.csp" <<'EOF'
#variable U:32 unsigned = 0xFFFFFFF7
#variable R:32 unsigned = 0
#variable L = 0
R = U % 10 ? 1
L = U < 10 ? 1
EOF

if build_rom "$D/uns.csp" "$D/uns_fw"; then
    got=$(printf '/state\n/state\n/quit\n' | repl "$D/uns_fw" "$D/u1.db" |
	      sed -n '/^R \|^L /p' | tail -2 | tr -s ' ')
    ck "an unsigned op survives the round trip through a ROM image" 'R = 7
L = 0' "$got"
else
    echo "  FAIL unsigned ROM firmware did not build"; fail=$((fail+1))
fi

echo "memory limit:"

# -m shrinks the usable code-memory budget so the out-of-memory path can be
# exercised without a 2K board. It had no test until now, which is how it came to
# be guarded by `if (debug)` for a while: a dangling `if` with no body picked up
# the statement after it, and -m then did nothing unless -d was given too. Both
# cases below run WITHOUT -d, which is the part that regressed.
cat > "$D/mem.csp" <<'EOF'
#variable A = 1
#variable B = 2
#variable C = 3
EOF

got=$(./csp -n "$D/mem.csp" 2>&1)
ck "no limit, no complaint" '' "$got"

# 200 bytes is below what three declarations plus the runtime baseline need, and
# far enough below that this will not need retuning every time a struct grows a
# field.
#
# rc=1 is half the point of the case: a setup failure used to report and carry
# on, leaving the exit code at 0, so `csp prog.csp || handle_it` saw success.
got=$(./csp -n -m 200 "$D/mem.csp" 2>&1; echo "rc=$?")
ck "-m refuses a program that does not fit, and says so in rc" \
   'setup failed: out of memory -- program does not fit
rc=1' "$got"

echo "================================================"
echo "repl: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
