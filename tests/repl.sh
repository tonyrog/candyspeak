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
# a board does: -C generates the image, the image compiles in place of rom.c.
# Without this there is no way to see an F tag at all -- the stock ./csp links an
# empty ROM, so every line it can ever list is RAM.
build_rom() {
    local src="$1" out="$2"
    ./csp -n -C -O "$D/rom_gen.c" "$src" >/dev/null 2>&1 || return 1
    gcc -g -DCSP_VERSION='"test"' -DCSP_ARENA_MALLOC -I. -o "$out" \
	csp_linux.c csp_rt.c csp_repl.c csp_compile.c csp_tok.c csp_dump.c \
	csp_eeprom.c csp_parse.c csp_print.c csp_strings.c "$D/rom_gen.c" >/dev/null 2>&1
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

# NOT YET IMPLEMENTED, pinned so it stays a clean refusal rather than a crash.
# `A[<expr>] = rhs` needs pat_body -- the LEFT of a rule body is matched by that
# pattern, not by the expression parser, and its subscript is P_INTEGER_S. So the
# whole optional backs off and the line is swallowed as an r-value expression,
# which made the expression parser perform the store during the pass that only
# VALIDATES, before the leaf tables exist. That was a segfault; process_assign
# now refuses a store before st->started. See TODO.
cat > "$D/arrwr.csp" <<'EOF'
#variable A[3] = 0
#variable I = 2
A[I] = 99 ? 1
EOF
./csp -n -c 0 "$D/arrwr.csp" >/dev/null 2>&1
ck "a runtime subscript WRITE is refused, not a crash" "1" "$?"

# OP_SETO and OP_SETOX mean nearly the same thing and their payloads OVERLAP, so
# emitting one through the other's arm compiles fine and produces a plausible
# wrong word. That is the shape of all three CRC mismatches this project has had.
echo "instr layout:"
if gcc -I. -O2 -o "$D/instr_layout" tests/instr_layout.c >/dev/null 2>&1; then
    ck "SETO/SETOX formats stay distinct" "ok, distinct" "$("$D/instr_layout")"
else
    echo "  FAIL instr_layout did not build"; fail=$((fail+1))
fi

echo "================================================"
echo "repl: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
