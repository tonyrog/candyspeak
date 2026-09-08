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
# Start every run from empty eeproms. They are NOT removed -- truncated -- but a
# db left holding the previous run's patch makes a case pass or fail on what the
# run before it did. It surfaced as a settings test seeing entries from a module
# that had since been renamed; any case that saves has the same exposure.
for f in "$D"/*.db; do [ -e "$f" ] && : > "$f"; done

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

# (over-long lines: tests/slow.sh -- they wait on timeouts)

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

# (overflow recovery: tests/slow.sh -- 400-byte lines and a reader that has
# to be allowed to run out of input, which means waiting on a timeout)

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
# The LAST SEVEN matches, not the last LINE. `]}.` is appended with no newline
# in front of it, so it used to share the line with the variables -- until a
# program had an object to dump, which put it on a line of its own and left
# tail -1 with nothing but the bracket. Every node has one now (the built-in
# Sys), so this reads the values wherever the closing bracket lands.
got=$(./csp -c 4 -s /dev/stdout "$D/str.csp" 2>&1 |
	  grep -o '"\(Same\|Cross\|Diff\|Lc\|Lv\|Le\|Ln\)",[0-9]*' |
	  tail -7 | tr '\n' ' ')
ck "string == compares positions, .len reads the length byte" \
'"Same",1 "Cross",1 "Diff",0 "Lc",6 "Lv",3 "Le",0 "Ln",0 ' "$got"

# --- 17b. a patch does not cross a program change, a setting does -----------
# What happens to a saved EEPROM when the program underneath it changes. The
# patch references ROM declarations BY INDEX, so against another program those
# indices mean something else -- it must not load. A setting is keyed by NAME
# and is the unit's own word on a value, so it must.
#
# Two whole firmwares, because that is the only way to move the fingerprint:
# rom_fp is the linked image's crc_hdr, and nothing short of a different image
# changes it.
echo "patch across a program change:"
printf '#param Kp : 16 = 10\n#variable X : 16 = 0\nX = Kp\n' > "$D/fpa.csp"
printf '#param Kp : 16 = 10\n#variable X : 16 = 0\n#variable Y : 16 = 0\nX = Kp\nY = Kp + 1\n' > "$D/fpb.csp"
fw() {  # fw <out> <csp>   -- a host firmware carrying that program as its ROM
    ./csp -n -C -O "$2.rom.c" "$2" >/dev/null 2>&1 &&
    gcc -DCSP_VERSION='"test"' -DCSP_ARENA_MALLOC -Iinclude -Igen -Isrc \
	port/csp_linux.c src/csp_rt.c src/csp_crc.c src/csp_line.c \
	src/csp_repl.c src/csp_compile.c src/csp_tok.c port/csp_dump.c \
	src/csp_eeprom.c src/csp_parse.c src/csp_print.c gen/csp_strings.c \
	src/csp_transport.c src/csp_console.c src/csp_flash.c port/csp_devices.c port/csp_flash_host.c \
	"$2.rom.c" -o "$1" >/dev/null 2>&1
}
if fw "$D/fw_a" "$D/fpa.csp" && fw "$D/fw_b" "$D/fpb.csp"; then
    : > "$D/fp.db"
    got=$(printf '> Kp = 42\n> sys.Id = 7\n#variable Z : 16 = 7\nZ = X + 1\n/save\n/quit\n' \
	  | "$D/fw_a" -i -e "$D/fp.db" 2>&1 | sed -n 's|^Saved to .* (\(.*\))$|\1|p')
    ck "the patch and the settings are saved" \
       "1 RAM decls, 40 RAM instrs, 274 bytes" "$got"

    # The reported size is what the file WEIGHS. It was 332 for this 274-byte
    # save -- a leftover string term counting bytes that have ridden in the
    # instructions since v16 -- and /memory calls that (FULL).
    ck "and the size it reports is the size on disk" \
       "274" "$(wc -c < "$D/fp.db" | tr -d ' ')"

    got=$(printf '/settings\n/quit\n' | "$D/fw_b" -i -e "$D/fp.db" 2>&1 |
	  grep -E '^(Kp|sys\.Id) = ' | tr '\n' ' ')
    ck "settings survive the program change" "Kp = 42 sys.Id = 7 " "$got"

    # Kp reads 42 through the new program, which is the point of keeping it.
    got=$(printf '> Kp\n/quit\n' | "$D/fw_b" -i -e "$D/fp.db" 2>&1 |
	  sed -n '/^> > Kp$/{n;p;}')
    ck "and they are in effect, not just stored" "42" "$got"

    got=$(printf '/list\n/quit\n' | "$D/fw_b" -i -e "$D/fp.db" 2>&1 | grep -c 'Z')
    ck "the patch does not load against another program" "0" "$got"


    # ...and it is still THERE. Nothing was rewritten on the way past, so the
    # old firmware finds its own patch again: this is the rollback.
    got=$(printf '/list\n/quit\n' | "$D/fw_a" -i -e "$D/fp.db" 2>&1 |
	  sed -n 's|^#variable Z.*// \(.\)$|\1|p')
    ck "and rolling back to the old program finds it" "E" "$got"

    # The fingerprint has to be the image that BOOTED, not the one that was
    # LINKED. A firmware may link several; the registry picks the highest
    # generation. So two firmwares can share a rom_image and still run
    # different programs -- and reading rom_image gave them the same
    # fingerprint. The patch loaded into the wrong one, where a saved `Z` came
    # back as a second `R`: same indices, different table.
    printf '#variable X : 16 = 0\nX = 1\n' > "$D/i1.csp"
    printf '#variable X : 16 = 0\n#variable Y : 16 = 0\nX = 2\nY = 9\n' > "$D/i2.csp"
    printf '#variable X : 16 = 0\n#variable Q : 16 = 0\n#variable R : 16 = 0\nX = 7\nQ = 3\nR = 4\n' > "$D/i3.csp"
    ./csp -n -C -O "$D/i1.rom.c" --prefix rom --generation 0 "$D/i1.csp" >/dev/null 2>&1
    ./csp -n -C -O "$D/i2.rom.c" --prefix alt --generation 5 "$D/i2.csp" >/dev/null 2>&1
    ./csp -n -C -O "$D/i3.rom.c" --prefix alt --generation 5 "$D/i3.csp" >/dev/null 2>&1
    fw2() {  # fw2 <out> <rom.c> <rom.c>  -- a firmware carrying TWO images
	gcc -DCSP_VERSION='"test"' -DCSP_ARENA_MALLOC -Iinclude -Igen -Isrc \
	    port/csp_linux.c src/csp_rt.c src/csp_crc.c src/csp_line.c \
	    src/csp_repl.c src/csp_compile.c src/csp_tok.c port/csp_dump.c \
	    src/csp_eeprom.c src/csp_parse.c src/csp_print.c gen/csp_strings.c \
	    src/csp_transport.c src/csp_console.c src/csp_flash.c port/csp_devices.c port/csp_flash_host.c \
	    "$2" "$3" -o "$1" >/dev/null 2>&1
    }
    if fw2 "$D/fw2a" "$D/i1.rom.c" "$D/i2.rom.c" &&
       fw2 "$D/fw2b" "$D/i1.rom.c" "$D/i3.rom.c"; then
	got=$(printf '/images\n/quit\n' | "$D/fw2a" -i --no-eeprom 2>&1 |
	      grep -E '^[01]: ' | tr '\n' ' ')
	ck "the higher generation is the one that runs" \
	   "0: ROM gen=0 size=376 rules=37 1: ROM gen=5 size=400 rules=41 " "$got"

	: > "$D/two.db"
	printf '#variable Z : 16 = 5\nZ = Y + 1\n/save\n/quit\n' |
	    "$D/fw2a" -i -e "$D/two.db" >/dev/null 2>&1
	got=$(printf '/list\n/quit\n' | "$D/fw2b" -i -e "$D/two.db" 2>&1 | grep -c '// E')
	ck "a patch does not cross to a firmware sharing only its rom_image" \
	   "0" "$got"

	got=$(printf '/list\n/quit\n' | "$D/fw2a" -i -e "$D/two.db" 2>&1 |
	      sed -n 's|^#variable Z.*// \(.\)$|\1|p')
	ck "and the firmware that saved it still has it" "E" "$got"

	# sys.Boot: WHICH image to run. sys.Image says what happened; the two
	# are deliberately separate, because one number cannot both report and
	# request. Stored as a setting, so it is name-keyed and survives.
	: > "$D/boot.db"
	got=$(printf '> sys.Image\n> sys.Boot\n/quit\n' |
	      "$D/fw2a" -i -e "$D/boot.db" 2>&1 | sed -n '5p;7p' | tr '\n' ' ')
	ck "with no preference the highest generation runs" "1 255 " "$got"

	printf '> sys.Boot = 0\n/save\n/quit\n' |
	    "$D/fw2a" -i -e "$D/boot.db" >/dev/null 2>&1
	got=$(printf '> sys.Image\n/list\n/quit\n' |
	      "$D/fw2a" -i -e "$D/boot.db" 2>&1 | sed -n '5p;/^X=/p' | tr '\n' ' ')
	ck "asking for image 0 boots image 0" "0 X=1  // 1 F " "$got"

	# The loose coupling, and what it is FOR: a request that cannot be met
	# is not fatal. The node comes up on the automatic choice and the two
	# fields disagree, which is the diagnosis.
	printf '> sys.Boot = 7\n/save\n/quit\n' |
	    "$D/fw2a" -i -e "$D/boot.db" >/dev/null 2>&1
	got=$(printf '> sys.Boot\n> sys.Image\n/quit\n' |
	      "$D/fw2a" -i -e "$D/boot.db" 2>&1 | sed -n '5p;7p' | tr '\n' ' ')
	ck "a request that cannot be met still boots, and shows" "7 1 " "$got"

	# Ranking can only see the HEADER. A damaged SECTION is discovered when
	# the image is loaded -- by which point the choice has been made. So a
	# refusal has to send the loader back for the next candidate, or a
	# half-written slot takes the node down with it, which is precisely the
	# case A/B exists to survive. Corrupt one payload word of the higher
	# generation and the older, healthy image must win.
	awk '/\.raw={{/ && !done { sub(/{{[0-9]+/, "{{200"); done=1 } {print}' \
	    "$D/i2.rom.c" > "$D/i2bad.rom.c"
	if cmp -s "$D/i2.rom.c" "$D/i2bad.rom.c"; then
	    echo "  FAIL could not corrupt the image"; fail=$((fail+1))
	elif fw2 "$D/fw2bad" "$D/i1.rom.c" "$D/i2bad.rom.c"; then
	    got=$(printf '/quit\n' | "$D/fw2bad" -i --no-eeprom 2>&1 |
		  sed -n '/^ROM rejected/p')
	    ck "a damaged section is named, not guessed at" \
	       "ROM rejected: CRC mismatch in instr section (corrupt flash image)" \
	       "$got"

	    got=$(printf '> sys.Image\n/list\n/quit\n' |
		  "$D/fw2bad" -i --no-eeprom 2>&1 |
		  sed -n '6p;/^X=/p' | tr '\n' ' ')
	    ck "and the healthy older image runs instead of nothing" \
	       "0 X=1  // 1 F " "$got"
	else
	    echo "  FAIL the damaged-image firmware did not build"; fail=$((fail+1))
	fi
    else
	echo "  FAIL the two-image firmwares did not build"; fail=$((fail+1))
    fi
else
    echo "  FAIL the two firmwares did not build"; fail=$((fail+1))
fi

# --- 18. the exec-only build still runs -------------------------------------
# CSP_EXEC_ONLY drops the scanner, the parser and the command layer. What is
# left has to still run a linked ROM image -- which is the whole point of the
# tier, and exactly the kind of thing that rots silently because no normal build
# exercises it. Link one against a generated image and check it computes.
echo "exec-only:"
printf '#digital Led out 0:13\n#timer T 500 = 1\n#variable N = 0\nT = 1 ? timeout(T)\nN = N + 1 ? timeout(T)\n' > "$D/eo.csp"
if ./csp -n -C -O "$D/eo_rom.c" "$D/eo.csp" >/dev/null 2>&1 &&
   gcc -DCSP_VERSION='"test"' -DCSP_ARENA_MALLOC -DCSP_EXEC_ONLY -Iinclude -Igen -Isrc \
       port/csp_linux.c src/csp_rt.c src/csp_crc.c src/csp_line.c src/csp_repl.c \
       src/csp_compile.c src/csp_tok.c port/csp_dump.c src/csp_eeprom.c \
       src/csp_parse.c src/csp_print.c gen/csp_strings.c src/csp_transport.c src/csp_console.c src/csp_flash.c \
       port/csp_devices.c port/csp_flash_host.c \
       "$D/eo_rom.c" -o "$D/csp_exec" \
       >/dev/null 2>&1; then
    # Last two matches, not the last LINE -- see the note on the string case
    # above: `]}.` moved onto a line of its own once every program had an object
    # to dump.
    got=$("$D/csp_exec" -c 6 --no-eeprom -s /dev/stdout 2>&1 |
	      grep -o '"State",[0-9]*\|"N",[0-9]*' | tail -2 | tr '\n' ' ')
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
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/bits_cmp" tests/bits_cmp.c >/dev/null 2>&1; then
    ck "csp_bits matches bitpack bit for bit" "ok, identical" "$("$D/bits_cmp")"
else
    echo "  FAIL bits_cmp did not build"; fail=$((fail+1))
fi

# --- 19b. the flash geometry -------------------------------------------------
# Flash sectors are not the same size -- an LPC212x is 8 x 8K, 2 x 64K, 7 x 8K --
# so "the application starts at 128K" is not something you can say: erasing is
# per sector, and a byte offset does not tell you which. Every offset here is a
# running sum, and the host device is deliberately non-uniform so an off-by-one
# at the step from small sectors to big ones has somewhere to show up.
# The line editor cannot be exercised through the REPL above: everything a pipe
# holds is available at once, so the reader drains past the newline and the rest
# lands in the paste QUEUE, where cursor keys and history are deliberately off.
# A ^P sent down a pipe is ignored BY DESIGN and proves nothing -- which is
# exactly how a broken editor once passed for working here. So: drive
# csp_line_input a byte at a time, the way a serial port delivers them.
# A line past MAX_LINE_TOKENS has to SAY SO. It used to segfault instead:
# ERR_TOO_MANY_TOKENS was in the enum and absent from err_tab -- a designated
# initialiser array, so the row was NULL and csp_print_error walked it from
# address zero. The compiler dumped core while reporting an ordinary mistake,
# which sends you looking at your program instead of at the message.
#
# The limit is in the text because "too complex" without a number gives no idea
# how much to split off.
# #define is a COMPILE-TIME name: the value folds into the code and the name is
# forgotten. The whole point is that it never reaches the string table or a ROM
# image, and neither of those is visible from the state dump -- so it is checked
# here, against a generated image.
#
# lib/analog.csp is the case that motivated it: nine long ADC_ flag names put
# that module at 492 bytes of a 512-byte ceiling (a declaration's name field is
# 9 bits, so ROM and RAM names together cannot pass 512 whatever the buffer).
echo "#define:"
cat > "$D/def.csp" <<'EOD'
#define A_DELIBERATELY_LONG_FLAG_NAME 0x04
#define ANOTHER_LONG_ONE_HERE         0x10
#define BOTH_OF_THEM  A_DELIBERATELY_LONG_FLAG_NAME | ANOTHER_LONG_ONE_HERE
#variable v:8 = 0
v = v | BOTH_OF_THEM
EOD
if ./csp -n -C -O "$D/def.rom.c" "$D/def.csp" >/dev/null 2>&1; then
    got=$(grep -c 'A_DELIBERATELY_LONG_FLAG_NAME' "$D/def.rom.c")
    ck "a #define name is not in the ROM image" "0" "$got"
    # The value folded, and folded through another define.
    got=$(printf '/list\n/quit\n' | ./csp -i --no-eeprom -c 0 "$D/def.csp" 2>&1 |
	      grep -a '^v=' | sed 's/  *\/\/.*//')
    ck "a #define folds into the rule, through another define" "v=v|20" "$got"
    # And it costs nothing in the string table beyond the baseline. 58 is that
    # baseline -- State, INIT, NORMAL, FAILSAFE, the sys object and `v` -- and
    # the same program with the three names written out as 0x14 measures 58 too.
    # (74 before v14 dropped the nul terminator, 59 before v15 stopped reserving
    # byte 0, 58 before sys.Boot added a name to the Sys namespace. What the
    # check is about is the DIFFERENCE, which is zero.)
    got=$(sed -n 's|^//   size:.*[^0-9]\([0-9]*\) str.*|\1|p' "$D/def.rom.c")
    ck "three long #define names cost no string space" "63" "$got"
else
    echo "  FAIL #define image did not build"; fail=$((fail+1))
fi

echo "over-long line:"
{ printf '#variable a = 0\n#variable b = 0\na = b'; \
  for i in $(seq 1 80); do printf ' + b'; done; printf '\n'; } > "$D/toks.csp"
got=$(./csp -n "$D/toks.csp" 2>&1 | sed 's|.*toks.csp:||')
ck "an over-long line reports instead of crashing" \
   "3 line too complex -- more than 64 tokens; split it" "$got"

echo "line editor:"
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/line_edit" tests/line_edit.c src/csp_line.c \
       >/dev/null 2>&1; then
    got=$("$D/line_edit" | tail -1)
    ck "cursor, history and the paste guard" "line editor: ok" "$got"
else
    echo "  FAIL line_edit did not build"; fail=$((fail+1))
fi

echo "flash guard:"
# What csp_flash_put must REFUSE. The caller of a flash write is, by definition,
# the part that gets rewritten next -- a firmware-update mode, a command not
# written yet -- so the guard lives in csp_flash_put and this proves nothing
# routes around it. Removing the guard fails four of these and nothing else.
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/flash_guard" tests/flash_guard.c \
       src/csp_transport.c src/csp_console.c src/csp_flash.c src/csp_crc.c port/csp_devices.c port/csp_flash_host.c \
       gen/csp_strings.c >/dev/null 2>&1; then
    got=$(cd "$(dirname "$0")/.." && "$D/flash_guard" | tail -1)
    ck "runtime and the last failsafe are refused" "ok, refused" "$got"
else
    echo "  FAIL flash_guard did not build"; fail=$((fail+1))
fi

echo "flash geometry:"
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/flash_geom" tests/flash_geom.c \
       src/csp_transport.c src/csp_console.c src/csp_flash.c src/csp_crc.c port/csp_devices.c port/csp_flash_host.c \
       gen/csp_strings.c \
       >/dev/null 2>&1; then
    got=$(cd "$(dirname "$0")/.." && "$D/flash_geom" | tail -1)
    ck "sector sums, regions and the file backend" "flash geometry: ok" "$got"
else
    echo "  FAIL flash_geom did not build"; fail=$((fail+1))
fi

echo "firmware upgrade mode:"
# The whole upgrade path with no board: csp-image turns a program into the bytes
# a target would hold, and /upgrade receives them into a simulated flash file. What this is really for is the failure
# cases -- a short image, a refused region, bad hex -- because those are the
# ones a real board answers by not booting.
# Through tools/csp-image, which is the one way to a flashable program -- so
# this exercises the script as well as the receiver.
if tools/csp-image -q -o "$D/up" examples/arith.csp >/dev/null 2>&1; then
    blank() { head -c 38912 /dev/zero | tr '\000' '\377' > "$1"; }
    up() {  # up <flashfile> <lines...>  -- runs a REPL session, prints its output
	f=$1; shift
	printf '%s\n' "$@" | ./csp -i --no-eeprom --flash="$f" --part=ab 2>&1
    }

    blank "$D/up.bin"
    got=$({ echo "/upgrade A"; cat "$D/up.hex"; echo "."; echo "/images"; \
	    echo "/quit"; } \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n 's|^A: ||p')
    ck "a whole image lands in slot A" "ROM gen=0 size=1324 rules=268" "$got"

    got=$({ echo "/upgrade A"; cat "$D/up.hex"; echo "."; echo "/images"; \
	    echo "/quit"; } \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n 's|^B: ||p')
    ck "the other slot is untouched" "erased" "$got"

    # A COMPLETE transfer of an INCOMPLETE image. The header arrives first and
    # its CRC covers only itself, so without the size check this reads back as
    # a perfectly good image describing bytes that never arrived.
    blank "$D/up.bin"
    got=$({ echo "/upgrade B"; head -5 "$D/up.hex"; echo "."; \
	    echo "/images"; echo "/quit"; } \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n 's|^B: ||p')
    ck "a short image leaves the slot erased" "erased" "$got"

    # ...and says so. A silent "OK" on a slot that will not boot is the one
    # answer this must never give.
    got=$({ echo "/upgrade B"; head -5 "$D/up.hex"; echo "."; echo "/quit"; } \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n '/^ERR /p' | tail -1)
    ck "and the operator is told" "ERR incomplete -- slot is erased" "$got"

    # Refused BEFORE the erase: the region still holds what it held. Proved on
    # the file, not on the message -- a guard that prints and erases anyway
    # passes every test that only reads stdout.
    blank "$D/up.bin"
    printf 'HELLO' | dd of="$D/up.bin" bs=1 seek=0 conv=notrunc status=none
    got=$(up "$D/up.bin" "/upgrade runtime" "/quit" | sed -n '/^ERR /p')
    ck "the runtime region is refused" "ERR protected" "$got"
    got=$(head -c 5 "$D/up.bin")
    ck "and nothing was erased" "HELLO" "$got"

    got=$(up "$D/up.bin" "/upgrade nosuch" "/quit" | sed -n '/^ERR /p')
    ck "an unknown region is named as such" "ERR no such region" "$got"

    # Unsaved work stops it before the erase. The upgrade takes the board down
    # and it is rebooted afterwards, so a RAM program that never reached the
    # EEPROM does not come back.
    blank "$D/up.bin"
    got=$(printf '#variable q = 1\n/upgrade A\n/quit\n' \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n '/^ERR unsaved/p' | sed 's| RAM instrs.*||')
    ck "unsaved work stops the upgrade" "ERR unsaved -- 1 RAM decls, 33" "$got"

    got=$(printf '#variable q = 1\n/upgrade A force\n!\n/quit\n' \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n '/^OK 16384/p')
    ck "and forcing it says so out loud" \
       "OK 16384 bytes, hex lines then '.' ('!' aborts)" "$got"

    # More bytes than the region holds. Caught while receiving, not after: the
    # next block would have been written past the slot and into `store`.
    blank "$D/up.bin"
    got=$({ echo "/upgrade A"; \
	    for i in $(seq 1 600); do \
	      echo "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF"; \
	    done; echo "."; echo "/quit"; } \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n '/^ERR full/p')
    ck "more than the region holds is refused" "ERR full" "$got"

    # Bad hex stops the transfer rather than guessing. The slot stays erased --
    # the header block is only written on a clean `.`.
    blank "$D/up.bin"
    got=$({ echo "/upgrade A"; head -2 "$D/up.hex"; echo "zz"; echo "."; \
	    echo "/images"; echo "/quit"; } \
	  | ./csp -i --no-eeprom --flash="$D/up.bin" --part=ab 2>&1 \
	  | sed -n 's|^A: ||p')
    ck "bad hex leaves the slot erased" "erased" "$got"
else
    echo "  FAIL csp-image did not produce an image"; fail=$((fail+1))
fi

# The linker script is generated FROM the region map, because the two say the
# same thing -- `runtime 0..7` and `LENGTH = 0x10000` -- and two copies of a
# statement drift. Generating it means the LINKER enforces the map: an
# interpreter too big for its region fails to link instead of being flashed over
# slot A.
#
# The RAM line is checked against the hand-written LPC2129-ROM.ld it replaces:
# same origin, same length, same 64 reserved bytes at the bottom.
got=$(escript utils/gen_chips.erl --ld lpc2129 | sed -n 's/^  DATA  *(rw) : \(.*\)   \/\*.*/\1/p')
ck "the generated RAM line matches the hand-written script" \
   'ORIGIN = 0x40000040, LENGTH = 0x00003FC0' "$got"

# One MEMORY entry per region, at the sector offsets. `A` starting at 0x10000 is
# the sum of eight 8K sectors -- not sector*size, which is the whole point.
got=$(escript utils/gen_chips.erl --ld lpc2129 |
	  sed -n 's/^  \([A-Za-z0-9]*\)  *(r[x]*)  *: ORIGIN = \(0x[0-9A-F]*\).*/\1 \2/p')
ck "regions land at their sector offsets" 'runtime 0x00000000
A 0x00020000
store 0x00030000' "$got"

# An unknown part is refused rather than silently emitting nothing.
escript utils/gen_chips.erl --ld nosuchpart >/dev/null 2>&1
ck "an unknown part is refused" 1 $?

# The chip tables are generated from chips/<vendor>/*.terms. Two copies of a
# part's geometry drift -- the hand-written one this replaced had the LPC1754 at
# 160K when the part has 128 -- so the generator is checked against what the
# data sheets say rather than against a second table.
echo "chip tables:"
# --list rather than a compiled table: the parts are read from the terms, so
# this checks the source of truth and not a copy of it.
got=$(escript utils/gen_chips.erl --list |
	  sed -n 's/^\(lpc[0-9]*\) .*(\([0-9]*\)K usable, \([0-9]*\) sectors).*/\1 \2 \3/p' |
	  grep -E '^lpc(2129|2138|1754) ')
ck "the generated geometry matches the data sheets" 'lpc1754 128 18
lpc2129 248 17
lpc2138 500 27' "$got"

# A group's map has to land on whole sectors of that group. The 212x slots are
# the two 64K ones, which is the property that makes A/B cheap on that part.
# The map, read back out of the generated linker script -- which is the form
# that actually gets used, so this checks the thing rather than a listing of it.
got=$(escript utils/gen_chips.erl --ld lpc2129 |
	  sed -n 's/^  \([A-Za-z0-9]*\)  *(r[x]*)  *:.*sectors\{0,1\} \(.*\) \*\//\1 \2/p')
ck "the 212x map holds a full runtime" 'runtime 0..8
A 9
store 10..16' "$got"

# --- 19b. interrupt sources --------------------------------------------------
# A CHANNEL IS THE THING TWO PINS CANNOT SHARE, and every way of getting it
# wrong is silent on the hardware: the second PINSEL or SYSCFG_EXTICR write
# wins, the first pin goes quiet, and there is nothing to find but a signal
# nobody answers. So the checks are the whole point, and they are what is
# tested here rather than the listing that shows them off.
echo "interrupts:"

# pin_function derives its pins from the pin table rather than repeating them,
# so this checks the derivation: eint2 is on P0.7 and P0.15 on an LPC2000, and
# the board has already spent both -- which is the answer someone picking a
# free interrupt pin actually needs.
got=$(escript utils/gen_chips.erl --irq-of bridgezone |
	  sed -n 's/^ *\(eint[0-3]\)  \(.*\)/\1 \2/p')
# `*` is a pin that IS the interrupt; a name in parentheses is what else took
# it. P0.16 is the AVR's wakeup line and P0.30 is Ain4 muxed to eint3 so the
# path can be driven by hand -- take that line out of bridgezone.terms and this
# expectation goes back to `P0.30(ain3)`.
ck "EINT pins come from the pin table, with what took them" 'eint0 P0.1(rxd0) P0.16*
eint1 P0.3(sda0) P0.14
eint2 P0.7(pwm2) P0.15(gpio)
eint3 P0.9(rxd1) P0.20(gpio) P0.30*' "$got"

# per_bit is a rule, not a list: the EXTI line IS the bit number, so PA1 and
# PB1 are the same channel and only one of them can be a source.
got=$(escript utils/gen_chips.erl --irq-of crazyflie | sed -n 's/^ *PC13 *//p')
ck "an STM32 pin reports its EXTI line" 'falling  exti/13' "$got"

# The four silent failures, plus the one an Arduino board cannot express. A
# scratch terms directory rather than a real board file: CSP_PATH is searched
# first, so these exist only for the length of this case.
mkdir -p "$D/terms"
cat > "$D/terms/irq.terms" <<'EOF'
{board, tirqcollide,                       %% PA1 and PB1 are both EXTI line 1
 [{chip, stm32f405rg}, {xtal, 8000000}, {core, 168000000},
  {pin, 'PA1', gpio_in}, {pin, 'PB1', gpio_in},
  {irq, 'PA1', falling}, {irq, 'PB1', rising}]}.
{board, tirqlevel,                         %% EXTI has no level trigger
 [{chip, stm32f405rg}, {xtal, 8000000}, {core, 168000000},
  {pin, 'PC13', gpio_in}, {irq, 'PC13', low}]}.
{board, tirqunmuxed,                       %% never configured, never fires
 [{chip, stm32f405rg}, {xtal, 8000000}, {core, 168000000},
  {irq, 'PC13', falling}]}.
{board, tirqnocap,                         %% P0.23 has no eint function at all
 [{chip, lpc2129}, {xtal, 12000000}, {core, 60000000},
  {pin, 'P0.23', gpio}, {irq, 'P0.23', falling}, {enable, [gpio]}]}.
{board, tirqwrongmux,                      %% right pin, muxed as gpio
 [{chip, lpc2129}, {xtal, 12000000}, {core, 60000000},
  {pin, 'P0.16', gpio}, {irq, 'P0.16', falling}, {enable, [gpio]}]}.
{board, tirqarduino,                       %% the core owns the pin map
 [{toolchain, arduino_cli}, {chip, atmega328p},
  {fqbn, "arduino:avr:uno"}, {irq, 2, falling}]}.
EOF
# Board AND reason: six boards each reporting SOME error would also pass if
# they all reported the same one.
got=$(CSP_PATH="$D/terms" escript utils/gen_chips.erl --check tirq 2>&1 |
	  sed -n 's/^\(tirq[a-z]*\): ERROR .*\(on an Arduino board\|is claimed by\|cannot trigger on\|cannot be an interrupt source\|never mentions\|cannot reach it\).*/\1 -- \2/p')
ck "every silent way to claim an interrupt is refused" 'tirqarduino -- on an Arduino board
tirqcollide -- is claimed by
tirqlevel -- cannot trigger on
tirqnocap -- cannot be an interrupt source
tirqunmuxed -- never mentions
tirqwrongmux -- cannot reach it' "$got"

# And a board that is right stays right -- the checks above are worthless if
# they also fire on the two boards that actually claim an interrupt.
escript utils/gen_chips.erl --check >/dev/null 2>&1
ck "the real boards still pass" 0 $?

# --- 20. the part layout -----------------------------------------------------
# csp_part.h hand-writes the bit position of every .part inside value_t. Those
# are bitfields in different union arms, so a wrong number corrupts data instead
# of failing to compile. This probes the real structs (all-ones into one field,
# read the word back) and checks every row against the probe, then round-trips
# the engine. It is the reason the table is allowed to be hand-written at all.
echo "part layout:"
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/part_layout" tests/part_layout.c >/dev/null 2>&1; then
    ck "csp_part table matches the value_t structs" "ok, identical" "$("$D/part_layout")"
else
    echo "  FAIL part_layout did not build"; fail=$((fail+1))
fi

# csp_states_t packs six state names into one declaration, and the whole design
# rests on slot 0 aliasing DECL_COMMON's `name` -- an alignment nothing would
# fail to compile over. This probes the real struct instead of restating the
# numbers, so reordering the fields or changing NAMEID_BITS is caught here
# rather than as first-state-of-every-block lookups quietly missing.
# OP_SEGMENT puts raw string bytes in the INSTRUCTION pool. Execution is safe by
# construction (the header is a jump), but the loops that walk the stream
# linearly are not -- the payload is TEXT, and one opcode nibble in four reads as
# something with operands. A step that is off by one walks into a segment.
echo "segment span:"
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/segment_span" tests/segment_span.c >/dev/null 2>&1; then
    ck "instr_next steps over a whole string segment" "ok, stepped over" "$("$D/segment_span")"
else
    echo "  FAIL segment_span did not build"; fail=$((fail+1))
fi

echo "states layout:"
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/states_layout" tests/states_layout.c >/dev/null 2>&1; then
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
# The instruction NUMBER is dropped: OP_SEGMENT runs carry identifier text in
# the same stream, so every index after them moves when a name is added. What
# the check is about is that a runtime subscript emits SETOX carrying the bound
# (len 3) and the stride -- not where in the stream it landed.
got=$(./csp -n -P "$D/arrwr.csp" 2>&1 | sed -n "/SETOX/p" | sed "s/^{instr,[0-9]*,/{instr,/")
ck "a runtime subscript write emits a bounds-checked SETOX" \
   "{instr,'SETOX',[r1,{len,3},{stride,1}]}." "$got"

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
if gcc -Iinclude -Igen -Isrc -O2 -o "$D/instr_layout" tests/instr_layout.c >/dev/null 2>&1; then
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
#
# And it lists as $N rather than by name. A local is a formula, not a value that
# lives somewhere -- nothing outside the module may read it (ERR_LOCAL_SCOPE), so
# a name in the listing would suggest a handle that does not exist. The number is
# its position among the module's locals, generated at listing time and stored
# nowhere.
cat > "$D/loc.csp" <<'EOF'
#variable a = 7
#local sum = a + 1
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/loc2.db" --no-eeprom "$D/loc.csp" |
	  grep -E '^#local')
ck "a #local lists as \$N, not by name" '#local $1:32 integer  // R' "$got"

# Baked into a ROM and loaded back. `local` is REAL DATA -- it decides whether a
# leaf is single-buffered -- so an emitter that dropped it would both fail the
# decl-section CRC and, if it somehow loaded, turn every local into an ordinary
# variable that lags a cycle. Checking the bit is in the generated C is not
# enough: the image has to load and the chain has to still resolve in one cycle.
if build_rom tests/unit/local.csp "$D/loc_fw"; then
    # Read through x (`x = sum ? 1`), because a #local has no /state row of its
    # own -- it is a formula, not state. x reaching 10 is still the property
    # under test: the local resolved a+b in the cycle it was written, and an
    # ordinary variable in its place would have lagged.
    got=$(printf '/list\n/state\n/quit\n' | repl "$D/loc_fw" "$D/loc3.db" --no-eeprom |
	      grep -E '^#local \$1:|^x ')
    ck "a #local survives a ROM round trip" \
       '#local $1:32 integer  // F
x                                     = 10' "$got"
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
if gcc -g -Wall -Iinclude -Igen -Isrc -Itests/lpcstub -Ichips/nxp/drivers/212x \
       -DCSP_VERSION='"test"' -o "$D/lpc_fw" \
       port/csp_lpcopen.c src/csp_rt.c src/csp_crc.c src/csp_line.c src/csp_compile.c \
       src/csp_parse.c src/csp_tok.c src/csp_print.c src/csp_repl.c \
       port/csp_dump.c src/csp_eeprom.c gen/csp_strings.c gen/rom_host.c \
       src/csp_transport.c src/csp_console.c src/csp_flash.c chips/nxp/drivers/212x/flash_212x.c port/csp_devices.c \
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
    # The remaining count is dropped. /state prints a timer as period/remaining,
    # and remaining depends on how much WALL CLOCK passed between the two /state
    # lines -- which under a sanitized build is enough to tick. What this case is
    # about is the PERIOD: 250 is the saved setting, 500 is what the source says.
    got=$(printf '/state\n/state\n/quit\n' | repl "$D/pinit_fw" "$D/pi2.db" |
	      sed -n '/^Tick\|^Pt/p' | tail -2 | tr -s ' ' |
	      sed 's#\(timer [0-9]*\)/[0-9]*#\1#')
    ck "a saved param reaches the timer period and the variable" \
       'Tick running timer 250
Pt = 42' "$got"
else
    echo "  FAIL param-init ROM firmware did not build"; fail=$((fail+1))
fi

# The initialiser is an EXPRESSION now, so an unknown name in it has to be an
# error. It used to fail the whole optional and leave a silent zero behind.
got=$(printf '#variable Q = Zork\n/quit\n' | repl ./csp "$D/pi3.db")
ck "an undeclared name in an initialiser is refused" \
   'Error: variable Zork is not declared' "$got"

echo "eeprom format:"

# A patch carries the ROM FORMAT it was written with. Nothing used to check it:
# EEPROM_VERSION versions the eeprom's own header and a ROM format change leaves
# it untouched, and the section CRCs prove the bytes survived storage -- not that
# they still MEAN the same thing. So a patch from older firmware loaded cleanly
# and was then read with the wrong layout.
#
# That is not theoretical: after v16 moved identifier text into OP_SEGMENT runs,
# an older patch left ps.strp at 0 and every name handle addressed past the
# table. On an LPC2129 that is a data abort -- boot LED blinking four, watchdog
# rebooting into it forever, on the one unit that happened to have a save.
printf '#variable Vv:8 unsigned = 7\nVv = Vv + 1 ? Vv < 100\n/save\n/quit\n' |
    repl ./csp "$D/fmt.db" > /dev/null
ck "a matching patch loads" "#variable Vv:8 unsigned = 7  // E" \
   "$(printf '/list\n/quit\n' | repl ./csp "$D/fmt.db" | grep '^#variable Vv')"

# Re-stamp it as an older firmware would have: the payload version AND the
# header CRC that covers it, so it is a well-formed save from another format
# rather than a corrupt one -- which is exactly the case the CRCs cannot catch.
python3 - "$D/fmt.db" "$D/fmt14.db" <<'PYEOF'
import struct, sys
def crc16(b, crc=0xFFFF):
    for x in b:
        crc ^= x << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc
d = bytearray(open(sys.argv[1], 'rb').read())
struct.pack_into('<H', d, 24, 14)              # ram.version
struct.pack_into('<H', d, 76, crc16(d[:76]))   # crc_hdr over it
open(sys.argv[2], 'wb').write(d)
PYEOF
got=$(printf '/quit\n' | repl ./csp "$D/fmt14.db")
ck "a patch from another ROM format is refused, and says why" \
   "eeprom rejected: patch is ROM format 14, firmware is 18 -- clear it and re-enter" \
   "$got"

# --- transports: declaration, listing and refusal ----------------------------
# Everything about a transport that can be checked WITHOUT hardware: that the
# endpoint survives the round trip through a constant, that /list gives back a
# line that can be typed again, that /state names the far end, and that a
# nonsense endpoint is refused where a typo has a line number.
# --- 25c. a string taken between two rebuilds ---------------------------------
# csp_mid_reset places the derived tables (view, heap, buffer table, graph)
# immediately above the RAM instructions, ONCE PER REBUILD. new_string can take
# a whole 132-byte segment between two rebuilds -- a name typed at the prompt,
# a string setting -- and that grows the instruction area underneath them.
#
# With a ROM program there are no RAM instructions at all, so the middle started
# just CSP_SCRATCH above zero and the segment landed squarely in the tables. The
# symptom was NOT a crash: a pin's VALUE SLOT went to zeroes while its
# declaration still read correctly, so /state showed `none digital 0:1` for a
# pin declared `in digital 0:16`. Found on a BridgeZone, 2026-09-08.
echo "string segments:"

cat > "$D/ss.csp" <<'CSPEOF'
#digital Pin in falling 0:16
#variable N = 0
N = N + 1
CSPEOF
if build_rom "$D/ss.csp" "$D/ss_fw"; then
    got=$(printf '> sys.Name = "Node1"\n/state\n/quit\n' |
	      repl "$D/ss_fw" "$D/ss.db" --no-eeprom |
	      grep -E "^Pin" | tr -s ' ' | cut -d= -f1 | sed 's/ *$//')
    ck "a string setting does not overwrite a value slot" \
       "Pin in digital 0:16" "$got"
else
    ck "a string setting does not overwrite a value slot" "built" "build failed"
fi

# --- 26a. #when blocks -------------------------------------------------------
# `#when <condition> ... #end` gates a whole block; `#in <state>+` gates on the
# state machine. Two words, deliberately: `#in Idle` reads as "in this state",
# and one word with two senses makes both harder to read.
#
# The listing is how a program is serialised, so a block that lists back as
# anything else is a program that changes meaning on the way home.
echo "when:"

cat > "$D/wn.csp" <<'CSPEOF'
#digital Drdy in falling 2:13
#variable A = 0
#variable B = 0
#when Drdy.fired && A < 100
  A = A + 1
  B = B + 2
#end
CSPEOF

# The condition is rendered from the gate's own instructions -- the same
# machinery a rule's `?` clause uses -- so what comes out is what went in.
got=$(printf '/list\n/quit\n' | repl ./csp "$D/wn.db" "$D/wn.csp" |
	  grep -E '^(#when|#end|  )' | sed 's; *// .*;;')
ck "a #when block lists back as itself" \
'#when Drdy.fired&&A<100
  A=A+1
  B=B+2
#end' "$got"

# ...and re-enters. A form that lists but does not parse back is worse than one
# that does neither, because it looks like it worked.
printf '/list\n/quit\n' | repl ./csp "$D/wn.db" "$D/wn.csp" |
    grep -E '^(#|  )' | sed 's; *// .*;;' | sed 's/^  //' > "$D/wn2.csp"
got=$(printf '/list\n/quit\n' | repl ./csp "$D/wn2.db" "$D/wn2.csp" |
	  grep -E '^(#when|#end|  )' | sed 's; *// .*;;')
ck "the listing parses back to the same block" \
'#when Drdy.fired&&A<100
  A=A+1
  B=B+2
#end' "$got"

# THE STATE MACHINE IS UNTOUCHED. #when reuses OP_NINSTATE with a different
# immediate, and the listing tells the two gates apart by shape -- so this is
# the case that catches a #when header rendered over an #in one.
got=$(printf '#states Idle Run\n#variable X = 0\n#in Idle\nX = 1\n#end\n#in Run\nX = 2\n#end\n/list\n/quit\n' |
	  repl ./csp "$D/wn3.db" | grep -E '^(#in|#end|  X)' | sed 's; *// .*;;')
ck "#in still lists as #in" \
'#in Idle
  X=1
#end
#in Run
  X=2
#end' "$got"

# One gate for N rules instead of N copies of the condition. Measured on the
# program above with four rules rather than two: 81 instructions written out,
# 67 as a block -- and the condition is evaluated ONCE per cycle instead of
# four times, which is the part that does not show up in a size.
got=$(printf '#digital D in falling 2:13\n#variable A=0\n#when D.fired\nA=A+1\n#end\n#when D.fired\nA=A+2\n#end\n/quit\n' |
	  repl ./csp "$D/wn4.db" | grep -c 'end mismatch')
ck "two blocks in a row are both accepted" "0" "$got"

# A BARE EXPRESSION INSIDE AN OPEN BLOCK IS A RULE, not a query.
#
# `println("hi")` at the prompt is a query -- run it, show the answer -- and
# line_is_rule says so from the text alone, because there is no '=' and no '?'.
# But the SAME line inside a #when, an #in or a #module is the block's body.
# Evaluating it once there printed at the wrong time and left the block empty:
# the rules the user typed were simply not in the program afterwards.
#
# The nesting is what decides, not the text.
got=$(printf '#variable X=1\n#when X\nprintln("in")\n#end\n/list\n/quit\n' |
	  repl ./csp "$D/wq1.db" | grep -E '(#when|#end|println)' | sed 's; *// .*;;')
ck "a bare call inside a #when is a rule" '#when X
  println("in")
#end' "$got"

got=$(printf '#states Idle\n#in Idle\nprintln("in")\n#end\n/list\n/quit\n' |
	  repl ./csp "$D/wq2.db" | grep -E '(#in |#end|println)' | sed 's; *// .*;;')
ck "a bare call inside an #in is a rule" '#in Idle
  println("in")
#end' "$got"

got=$(printf '#module M\n#variable A out\nprintln("in")\n#end\n/list\n/quit\n' |
	  repl ./csp "$D/wq3.db" | grep -E '(#module|#end|println)' | sed 's; *// .*;;')
ck "a bare call inside a #module is a rule" '#module M
  println("in")
#end' "$got"

# ...and at the top level it is still a query: evaluated once, nothing stored.
got=$(printf '#variable X=7\nX+1\n/list\n/quit\n' |
	  repl ./csp "$D/wq4.db" | grep -cE '^X\+1')
ck "a bare expression at the prompt stores nothing" "0" "$got"

# ONE STACK FOR #in, #when AND #module, so `#end` closes the group that opened
# last. With a flag per kind it closed the wrong one -- `#when X / #in Idle /
# ... / #end` shut the #when and left the #in open, and every line after it was
# swallowed into a block that never ended.
got=$(printf '#states Idle\n#variable X=1\n#variable Z=0\n#when X\n#in Idle\nZ=1\n#end\nZ=2\n#end\n/list\n/quit\n' |
	  repl ./csp "$D/wm1.db" | grep -E '(#when|#in |#end|Z=)' | sed 's; *// .*;;')
ck "#end closes the innermost block, whatever kind" '#when X
  #in Idle
    Z=1
  #end
  Z=2
#end' "$got"

# A BLOCK LEFT OPEN AT THE END OF A FILE used to pass in silence: everything
# after the missing `#end` was swallowed, the file parsed, and the program ran
# with rules that fire only under a condition their author meant for two lines.
#
# Reported by the line it OPENED on -- where the file ends is where you notice,
# the opening is where the mistake is. Innermost first.
printf '#variable X=1\n#variable Y=0\n#when X\nY=1\n' > "$D/op1.csp"
printf '#module M\n#variable A out\n' > "$D/op2.csp"
printf '#states Idle\n#variable X=0\n#in Idle\nX=1\n' > "$D/op3.csp"
got=$(for f in op1 op2 op3; do ./csp -n "$D/$f.csp" 2>&1 | sed 's;.*csp:;;'; done)
ck "a block left open at end of file is refused" '5 #when opened on line 3 was never closed
3 #module opened on line 1 was never closed
5 #in opened on line 3 was never closed' "$got"

# ...but NOT at the prompt, where a block is legitimately open while its rules
# are being typed.
got=$(printf '#variable X=1\n#when X\n/quit\n' | repl ./csp "$D/wm2.db" |
	  grep -c 'never closed')
ck "an open block at the prompt is not an error" "0" "$got"

# AN UNCLOSED BLOCK MUST NOT SPIN. The skip distance is patched at `#end`, and
# at the REPL a block is open for as long as it takes to type the rules -- with
# the cycle running the whole time. A distance of zero is a jump to the gate
# itself, so a false condition span the machine forever. It ends the cycle now.
got=$(printf '#variable X=0\n#when X\n/quit\n' | repl ./csp "$D/wn7.db" | wc -l)
ck "an unclosed block does not spin" "2" "$got"

# A gate with no condition is nothing, and nesting is refused rather than
# half-supported: there is one in_marker, so there is one gate.
got=$(printf '#when\n/quit\n' | repl ./csp "$D/wn5.db" | grep -c 'syntax error')
ck "a #when with no condition is refused" "1" "$got"

# BLOCKS NEST. A condition refining another one is the ordinary case --
# lib/analog.csp puts `#when latch && edge` inside `#when due && free` -- and
# each level keeps its own gate, so the inner `#end` patches the inner one.
got=$(printf '#variable X=1\n#variable Y=1\n#variable Z=0\n#when X\n#when Y\nZ=7\n#end\n#end\n/list\n/quit\n' |
	  repl ./csp "$D/wn6.db" | grep -E '(#when|#end|Z=)' | sed 's; *// .*;;')
ck "#when blocks nest" '#when X
  #when Y
    Z=7
  #end
#end' "$got"

# --- 26b. interrupt triggers -------------------------------------------------
# A trigger is an OPTION on the pin, like the pull -- an interrupt is a property
# of how the pin is configured, and there is no #pullup declaration for the same
# reason. The listing is how a program is serialised (/save, a ROM image, a paste
# back), so a trigger dropped from it is a program that comes home without its
# interrupts -- the failure a timer's `= 1` once had.
echo "events:"

cat > "$D/ev.csp" <<'CSPEOF'
#digital Drdy in falling 2:13
#digital Btn  in pullup rising 2:7
#analog  Adc:10 in ready 0:3
CSPEOF

got=$(printf '/list\n/quit\n' | repl ./csp "$D/ev.db" "$D/ev.csp" |
	  grep -E '^#(digital|analog)')
ck "a trigger lists with the other options" \
'#digital Drdy in falling 2:13  // R
#digital Btn in pullup rising 2:7  // R
#analog Adc:10 in ready 0:3  // R' "$got"

# ...and survives being baked into a ROM image. `irq` is REAL DATA -- it is what
# decides whether the source is armed at all -- so an emitter that dropped it
# would both fail the decl-section CRC and, if it somehow loaded, produce a
# program whose interrupts silently never fire.
if build_rom "$D/ev.csp" "$D/ev_fw"; then
    got=$(printf '/list\n/quit\n' | repl "$D/ev_fw" "$D/ev8.db" --no-eeprom |
	      grep -E '^#(digital|analog)' | sed 's;  // .*;;')
    ck "a trigger survives a ROM round trip" \
'#digital Drdy in falling 2:13
#digital Btn in pullup rising 2:7
#analog Adc:10 in ready 0:3' "$got"
else
    ck "a trigger survives a ROM round trip" "built" "build failed"
fi

# THREE KINDS OF SOURCE LOOK THE SAME BETWEEN EDGES, and only one of them
# cannot miss a short pulse. /state says which:
#
#   (nothing)  the silicon arms it
#   ~          software: the level is compared each cycle
#   !          armed by neither, and will never fire
#
# On the host every edge is software -- there is no interrupt controller -- and
# `ready` is refused outright, because a conversion finishing is not a level
# anything can compare.
# `soft` is the word that says sampling is acceptable, so the mark drops: the
# board is doing what the program asked rather than falling short of it.
got=$(printf '#analog Adc:10 in ready 0:3\n#digital D in falling 2:1\n#digital S in soft falling 2:2\n/state\n/quit\n' |
	  repl ./csp "$D/ev7.db" | grep -E '^(Adc|D|S) ' | tr -s ' ' | cut -d= -f2)
ck "/state says which kind of source each pin got" \
' 0 ready!
 0 falling~!
 0 falling~' "$got"

# ...and it lists back, with the other options and before the trigger.
got=$(printf '#digital S in soft falling 2:2\n/list\n/quit\n' |
	  repl ./csp "$D/ev9b.db" | grep '^#digital' | sed 's; *// .*;;')
ck "soft lists with the other options" '#digital S in soft falling 2:2' "$got"

# It is a NAME like the triggers are -- reserving it would have cost more than
# it bought, and this is the case that says so.
got=$(printf '#variable soft = 5\n> soft\n/quit\n' | repl ./csp "$D/ev9c.db" | tail -1)
ck "soft is still usable as a name" "5" "$got"

# TRIGGER WORDS ARE ORDINARY NAMES. parse_opts stops on a word that is not a
# trigger, so a name is still a name -- examples/can_pack.csp already has
# `#variable ready`, and `high` and `low` are names anyone would reach for.
got=$(printf '#variable ready = 7\n#variable high = 8\n> ready\n/quit\n' |
	  repl ./csp "$D/ev6.db" | tail -1)
ck "a trigger word is still usable as a name" "7" "$got"

# And a pin with no trigger stays exactly as it listed before -- the option is
# absent, not printed as `none`.
got=$(printf '#digital Plain in 2:2\n/list\n/quit\n' |
	  repl ./csp "$D/ev9.db" | grep '^#digital')
ck "a pin with no interrupt lists unchanged" '#digital Plain in 2:2  // R' "$got"

# AN OPTION AFTER THE PIN WAS SILENTLY DROPPED. pmatch reads the options before
# the pin and never looks past it, so `#digital D in 2:1 pullup` was a pin with
# no pull and nothing said so. A trigger written there is the same failure with
# a longer fuse -- a pin that never interrupts, on a program that looks right.
got=$(printf '#digital D in 2:1 falling\n#digital E in 2:2 pullup\n/quit\n' |
	  repl ./csp "$D/eva.db" | grep -c 'comes BEFORE the pin')
ck "an option written after the pin is refused" "2" "$got"

# ...and the array form still reads its own tail, which is a pin spec and not
# a stray option.
got=$(printf '#digital G[3] in 0:1..3\n/list\n/quit\n' |
	  repl ./csp "$D/evb.db" | grep '^#digital')
ck "a device array still parses its pin list" '#digital G[3] in 0:1..3  // R' "$got"

echo "transports:"

cat > "$D/tr.csp" <<'CSPEOF'
#define GROUND 0xC0A80102
#buffer Imu:14  in  i2c 3 0x68 0x3B
#buffer Gyro:6  in  spi 1 2:4 0x28
#buffer Rx:16   in  udp 5000
#buffer Tlm:16  out udp 5000 GROUND
#buffer Frame:8 in  can 0x201
CSPEOF

# RE-ENTERABLE, which is the contract /list has: what comes out is what went in,
# modulo the #define. Each endpoint has to survive being packed into a constant
# and unpacked again -- a shift off by four in TR_SPI_XREF would show here and
# nowhere else until a scope came out.
got=$(printf '/list\n/quit\n' | repl ./csp "$D/tr.db" "$D/tr.csp" | grep '^#buffer')
ck "every transport lists the way it was written" \
'#buffer Imu:14 in i2c 3 0x68 0x3b  // R
#buffer Gyro:6 in spi 1 2:4 0x28  // R
#buffer Rx:16 in udp 5000  // R
#buffer Tlm:16 out udp 5000 192.168.1.2  // R
#buffer Frame:8 in can 0x201  // R' "$got"

# /state names the FAR END, which differs per bus: a device address, a chip
# select, a port, a frame id. The second half is the length that last moved.
got=$(printf '/state\n/quit\n' | repl ./csp "$D/tr.db" "$D/tr.csp" |
	  grep -E '^(Imu|Gyro|Rx|Tlm|Frame) ' | tr -s ' ' | cut -d= -f1 | sed 's/ *$//')
ck "/state names the far end per transport" \
'Imu in buffer 0x68/14
Gyro in buffer 2:4/6
Rx in buffer 5000/16
Tlm out buffer 5000/16
Frame in buffer 0x201/8' "$got"

# A 7-bit bus and an 8-bit register. Out of range is a TYPO, and a typo caught
# at declaration has a line number -- csp_buf_setup runs long after the text is
# gone, and a truncated address would just talk to the wrong device.
got=$(printf '#buffer B:4 in i2c 3 0x99 0x00\n/quit\n' | repl ./csp "$D/tr2.db" |
	  grep -c 'syntax error')
ck "an I2C address past 7 bits is refused" "1" "$got"

# Two transports on one buffer. Each optional block backs off cleanly, so both
# match and only the COUNT notices -- which is the thing worth testing, because
# it is the one place a second transport could be silently ignored.
got=$(printf '#buffer B:4 in can 0x201 udp 5000\n/quit\n' | repl ./csp "$D/tr3.db" |
	  grep -c 'syntax error')
ck "a buffer naming two transports is refused" "1" "$got"

# A transport with no driver: the weak stubs in csp_transport.c. This is what
# lets a program be written on the host and moved to a board -- it has to RUN,
# not fail to link, and `.rx` has to stay false so a rule guarded on it does
# not fire on a reading that never happened.
cat > "$D/nodrv.csp" <<'CSPEOF'
#buffer Imu:4 in i2c 3 0x68 0x3B
#variable Fired:8 = 0
Fired = 1 ? Imu.rx
CSPEOF
got=$(printf '/latch off\n/state\n/quit\n' | repl ./csp "$D/nodrv.db" "$D/nodrv.csp" |
	  grep -E '^Fired ' | tr -s ' ' | sed 's/ *$//')
ck "a bus with no driver runs and never delivers" "Fired = 0" "$got"

# A port already held by something else. SO_REUSEADDR used to be set on these
# sockets, which on UDP lets a second process bind the SAME port -- the bind
# succeeds, the kernel gives each datagram to one of them, and the loser
# receives nothing and says nothing. Every symptom pointed at the sender.
#
# Now the bind fails and the failure is REPORTED, on stderr, once. The test is
# on the message: silence is the bug.
echo "udp bind:"
if command -v python3 >/dev/null 2>&1; then
    python3 -c "
import socket,time
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.bind(('',55733)); time.sleep(3)" &
    hogpid=$!
    sleep 0.4
    printf '#buffer B:4 in udp 55733\n' > "$D/hog.csp"
    ( printf '/latch off\n'; sleep 0.6; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/hog.csp" >/dev/null 2>"$D/hog.err"
    kill $hogpid 2>/dev/null; wait $hogpid 2>/dev/null
    got=$(grep -c 'cannot listen on port 55733' "$D/hog.err")
    ck "a port already taken is reported, not suffered in silence" "1" "$got"
    # ONCE, not once per cycle: this is polled every pass through the loop.
    ck "and reported once, not every cycle" "1" \
       "$(grep -c 'cannot listen' "$D/hog.err")"

    # And the state SAYS SO afterwards. One line on stderr, in a banner, is
    # gone by the time anyone wonders why nothing arrives -- and a silent
    # buffer looks exactly like a quiet peer. This is the difference between
    # "nobody is sending" and "this program never listened".
    python3 -c "
import socket,time
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.bind(('',55734)); time.sleep(3)" &
    hog2=$!
    sleep 0.4
    printf '#buffer B:4 in udp 55734\n' > "$D/hog2.csp"
    got=$(( printf '/latch off\n'; sleep 0.6; printf '/state\n/quit\n' ) |
	       ./csp -i --no-eeprom "$D/hog2.csp" 2>/dev/null |
	       grep -E '^B ' | grep -c 'DEAD')
    kill $hog2 2>/dev/null; wait $hog2 2>/dev/null
    ck "a refused port is marked DEAD in /state" "1" "$got"
else
    echo "  SKIP no python3 to hold the port"
fi

# --- transports: udp end to end ---------------------------------------------
# TWO CandySpeak programs, a real socket between them. Nothing is simulated:
# one declares an out buffer, the other an in buffer on the same port, and the
# receiving program's RULE has to fire.
#
# This exists because the transport plumbing failed in FOUR separate places
# while it was written, and every one of them looked like the others: a test
# that had checked only "the datagram left" would have passed with the receiver
# stone deaf. The assertion is on `Seen`, which is only set by a rule guarded on
# `.rx` -- so it covers the send, the socket, the delivery, the RX flag, the
# field unpacking and the rule.
echo "udp:"
cat > "$D/utx.csp" <<'CSPEOF'
#define LOOPBACK 0x7F000001
#buffer Tx:4 out udp 55731 LOOPBACK
#field  Out:16 Tx[0..15]
#timer Tick 50 = 1
Out = 4711 ? Tick
CSPEOF
cat > "$D/urx.csp" <<'CSPEOF'
#buffer Rx:4 in udp 55731
#field  Val:16 Rx[0..15]
#variable Seen:16
Seen = Val ? Rx.rx
CSPEOF
ubuild() {
    ./csp -n -C -O "$2.rom.c" "$2" >/dev/null 2>&1 &&
    gcc -DCSP_VERSION='"test"' -DCSP_ARENA_MALLOC -Iinclude -Igen -Isrc \
	port/csp_linux.c src/csp_rt.c src/csp_crc.c src/csp_line.c src/csp_repl.c \
	src/csp_compile.c src/csp_tok.c port/csp_dump.c src/csp_eeprom.c \
	src/csp_parse.c src/csp_print.c gen/csp_strings.c src/csp_transport.c \
	src/csp_console.c \
	src/csp_flash.c port/csp_devices.c port/csp_flash_host.c \
	"$2.rom.c" -o "$1" >/dev/null 2>&1
}
if ubuild "$D/utx" "$D/utx.csp" && ubuild "$D/urx" "$D/urx.csp"; then
    # `/latch off` FIRST, on both. The host build starts latched, which blocks
    # every device output including the transports -- a program that looks
    # perfect and sends nothing.
    ( printf '/latch off\n'; sleep 2; printf '/state\n/quit\n' ) |
	"$D/urx" -i --no-eeprom > "$D/urx.out" 2>&1 &
    rxpid=$!
    sleep 0.5
    ( printf '/latch off\n'; sleep 1; printf '/quit\n' ) |
	"$D/utx" -i --no-eeprom >/dev/null 2>&1
    wait $rxpid 2>/dev/null
    got=$(grep -E '^(Val|Seen) ' "$D/urx.out" | tr -s ' ' | sed 's/ *$//')
    ck "a datagram crosses two programs and fires a rule" \
"Val in field [0..15] = 4711
Seen = 4711" "$got"
else
    echo "  FAIL the two udp programs did not build"; fail=$((fail+1))
fi

# --- transports: udp drops what it cannot read -------------------------------
# A datagram is a SNAPSHOT of the sender at the moment it left, and an `in`
# buffer holds exactly one. So a backlog is not data waiting to be read, it is
# data that was already stale when we did not read it -- and a peer faster than
# the cycle would build one that never drains, leaving the program permanently
# behind reality.
#
# The test stalls the receiver with /pause, queues twenty datagrams in the
# socket, and lets it go. Right is ONE delivery carrying the LAST value. The
# assertion is on the COUNT as much as the value: draining four per cycle and
# carrying the rest over also arrives at 20 eventually, and would pass a test
# that only looked at the number.
echo "udp drop:"
cat > "$D/udrop.csp" <<'CSPEOF'
#buffer Rx:4 in udp 55735
#field  Val:16 big Rx[0..15]
println("rx", Val) ? Rx.rx
CSPEOF
if command -v python3 >/dev/null 2>&1; then
    ( printf '/latch off\n'; sleep 0.5; printf '/pause\n'; sleep 1.2;
      printf '/resume\n'; sleep 0.5; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/udrop.csp" > "$D/udrop.out" 2>&1 &
    dpid=$!
    sleep 1.0
    python3 -c "
import socket,struct
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
for i in range(1,21):
    s.sendto(struct.pack('>HH', i, 0), ('127.0.0.1', 55735))"
    wait $dpid 2>/dev/null
    # grep -o, not '^rx': the print lands at the prompt, so the line reads
    # "> rx20".
    ck "a stalled port delivers the newest datagram" "rx20" \
       "$(grep -o 'rx[0-9]*' "$D/udrop.out" | tail -1)"
    ck "and delivers it once -- the other nineteen are dropped" "1" \
       "$(grep -c 'rx[0-9]' "$D/udrop.out")"
else
    echo "  SKIP no python3 to send datagrams"
fi

# --- transports: two buffers on one port are two VIEWS ------------------------
# A port is bound ONCE, so two `in udp <port>` buffers cannot be two consumers
# of it: the second would take whichever datagrams the first happened not to
# grab, which is not a shape anyone can write a program against. They parse the
# SAME datagram, each with its own fields.
echo "udp views:"
cat > "$D/uview.csp" <<'CSPEOF'
#buffer A:4 in udp 55736
#buffer B:4 in udp 55736
#field  Av:16 big A[0..15]
#field  Bv:16 big B[16..31]
println("both", Av, Bv) ? A.rx && B.rx
CSPEOF
if command -v python3 >/dev/null 2>&1; then
    ( printf '/latch off\n'; sleep 1.5; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/uview.csp" > "$D/uview.out" 2>&1 &
    vpid=$!
    sleep 0.8
    python3 -c "
import socket,struct
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.sendto(struct.pack('>HH', 1234, 5678), ('127.0.0.1', 55736))"
    wait $vpid 2>/dev/null
    ck "one datagram reaches both buffers on the port" "both12345678" \
       "$(grep -o 'both[0-9]*' "$D/uview.out" | head -1)"
else
    echo "  SKIP no python3 to send datagrams"
fi

# --- ipv4 literals ------------------------------------------------------------
# `1.2.3.4` is another SPELLING of 0x01020304, decided at the second dot: one
# part is an integer, two are a float, four are an address. The scanner cannot
# know which until it has looked past the second dot, so the part after the
# first one is scanned once and the branch taken afterwards.
echo "ipv4 literals:"

got=$(printf '#variable A = 1.2.3.4\nprintln(A)\n/quit\n' |
	  repl ./csp "$D/ip1.db" | grep -E '^[0-9-]+$' | head -1)
ck "a dotted quad is the same bit pattern as the hex" "16909060" "$got"

# 192.168.1.2 is 0xC0A80102, which is NEGATIVE as an int32 -- the same value the
# hex literal has always produced. The quad takes the hex path, not the decimal
# one, so it is not refused for being past INT32_MAX.
got=$(printf '#variable A = 192.168.1.2\nprintln(A)\n/quit\n' |
	  repl ./csp "$D/ip2.db" | grep -E '^-?[0-9]+$' | head -1)
ck "an address past INT32_MAX is a bit pattern, not an overflow" "-1062731518" "$got"

# THREE parts is not an address, and saying so is the point: `1.2.3` used to
# scan as FLT DOT INT and die further along as a syntax error at the dot.
got=$(printf '#variable A = 1.2.3\n/quit\n' | repl ./csp "$D/ip3.db" |
	  grep -c 'ipv4 literal')
ck "three parts is refused, and named" "1" "$got"

got=$(printf '#variable A = 1.2.3.4.5\n/quit\n' | repl ./csp "$D/ip4.db" |
	  grep -c 'ipv4 literal')
ck "five parts is refused" "1" "$got"

got=$(printf '#variable A = 1.2.300.4\n/quit\n' | repl ./csp "$D/ip5.db" |
	  grep -c 'ipv4 literal')
ck "a part above 255 is refused" "1" "$got"

# The two things the quad must not have eaten. A float is two parts, and `..` is
# still a range -- the test is on a DIGIT after the dot, which is what keeps
# `0..15` scanning as INT DOTDOT INT.
got=$(printf '#variable F float = 1.25\nprintln(F)\n/quit\n' |
	  repl ./csp "$D/ip6.db" | grep -E '^[0-9.]+$' | head -1)
ck "a float is still a float" "1.250000" "$got"

got=$(printf '#buffer Q:4\n#field Y:16 Q[0..15]\n/list\n/quit\n' |
	  repl ./csp "$D/ip7.db" | grep '^#field')
ck "a bit range is still a range" '#field Y:16 integer Q[0..15]  // R' "$got"

# --- transports: udp sender filter -------------------------------------------
# The address on an `in` buffer is an ACCEPT TEST, not a destination: 0.0.0.0 --
# which is what no address at all compiles to -- takes anyone, anything else
# takes that peer alone. Two senders on the loopback net, one of each.
#
# The filter is applied BEFORE the bytes land. That is the part worth testing:
# the datagram is read straight into the buffer's own shadow, so a rejected one
# read there would overwrite the last good one with bytes nothing ever marks.
echo "udp filter:"
cat > "$D/ufilt.csp" <<'CSPEOF'
#buffer Rx:4 in udp 55740 127.0.0.1
#field  Val:16 big Rx[0..15]
println("rx", Val) ? Rx.rx
CSPEOF
if command -v python3 >/dev/null 2>&1; then
    got=$(printf '/list\n/quit\n' | repl ./csp "$D/ufilt.db" "$D/ufilt.csp" |
	      grep '^#buffer')
    ck "a filtered listener lists its peer as a dotted quad" \
       '#buffer Rx:4 in udp 55740 127.0.0.1  // R' "$got"

    ( printf '/latch off\n'; sleep 3; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/ufilt.csp" > "$D/ufilt.out" 2>&1 &
    fpid=$!
    python3 -c "
import socket,struct,time
time.sleep(0.8)
a=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); a.bind(('127.0.0.2',0))
a.sendto(struct.pack('>HH',111,0),('127.0.0.1',55740))
time.sleep(0.5)
b=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); b.bind(('127.0.0.1',0))
b.sendto(struct.pack('>HH',222,0),('127.0.0.1',55740))"
    wait $fpid 2>/dev/null
    ck "only the declared peer is delivered" "rx222" \
       "$(grep -o 'rx[0-9]*' "$D/ufilt.out" | tail -1)"
    ck "and the other peer leaves no trace" "1" \
       "$(grep -c 'rx[0-9]' "$D/ufilt.out")"
else
    echo "  SKIP no python3 to send datagrams"
fi

# --- the console wire --------------------------------------------------------
# The two ends of the wire between a node's serial port and its interpreter.
# Normally one feeds the other; a buffer on either end splices in so a rule can
# carry the bytes somewhere else -- over CAN to a node with no serial port,
# which is the whole point.
#
#   console   the serial port.  in = what was TYPED, out = what is SHOWN
#   repl      the interpreter.  in = what it PRINTED, out = fed in as typed
echo "console wire:"

got=$(printf '#buffer Rp:32 in repl\n#buffer Cn:32 inout console\n/list\n/quit\n' |
	  repl ./csp "$D/con0.db" | grep '^#buffer')
ck "both ends of the wire list the way they were written" \
'#buffer Rp:32 in repl  // R
#buffer Cn:32 inout console  // R' "$got"

# In PATTERN ORDER, which is where the count can see both: the optionals are
# tried can, i2c, spi, udp, console -- so `repl can 0x201` is a console buffer
# with trailing words, while `can 0x201 repl` is the two-transport line the
# count exists to refuse.
got=$(printf '#buffer B:4 in can 0x201 repl\n/quit\n' | repl ./csp "$D/con1.db" |
	  grep -c 'syntax error')
ck "an end of the wire is a transport like any other" "1" "$got"

# THE TAP. What the interpreter prints lands in an `in repl` buffer, which is
# how a node with no serial port gets its output anywhere at all.
cat > "$D/tap.csp" <<'CSPEOF'
#buffer Rp:8 in repl
#field  C0:8 Rp[0..7]
#variable Seen:8 = 0
Seen = C0 ? Rp.rx
CSPEOF
( printf '/latch off\nprintln("Zebra")\n'; sleep 0.5; printf '/quit\n' ) |
    ./csp -i --no-eeprom "$D/tap.csp" > "$D/tap.out" 2>&1
# '^Zebra', not 'Zebra': the prompt echoes the line that produced it too, and
# the tap is non-consuming, so the local console still shows the output.
ck "what the interpreter printed reaches a rule" "1" \
   "$(grep -c '^Zebra' "$D/tap.out")"
# 5a 65 62 72 61 = "Zebra". The assertion is on the BYTES, not on Seen: Seen
# holds whichever chunk landed last, and /state's own output is tapped too.
got=$(printf '/latch off\nprintln("Zebra")\n/state\n/quit\n' |
	  ./csp -i --no-eeprom "$D/tap.csp" 2>&1 |
	  grep -o '5a 65 62 72 61' | head -1)
ck "and arrives as the bytes that were printed" "5a 65 62 72 61" "$got"

# THE FEED, the other direction: a rule writes bytes to an `out repl` buffer
# and the interpreter runs them as if they had been typed. This is the half
# that makes a remote REPL a REPL rather than a log.
cat > "$D/feed.csp" <<'CSPEOF'
#variable Q:8 = 0
#buffer Fd:4 out repl
#field  F0:8 Fd[0..7]
#field  F1:8 Fd[8..15]
#field  F2:8 Fd[16..23]
#field  F3:8 Fd[24..31]
#timer  T 300 = 1
F0 = 81 ? timeout(T)
F1 = 61 ? timeout(T)
F2 = 55 ? timeout(T)
F3 = 10 ? timeout(T)
Fd.tx = 1 ? timeout(T)
CSPEOF
got=$(( printf '/latch off\n'; sleep 1.2; printf '/state\n/quit\n' ) |
	  ./csp -i --no-eeprom "$D/feed.csp" 2>&1 |
	  grep -E '^Q ' | tr -s ' ' | sed 's/ *$//')
ck "a rule can type at the interpreter" "Q = 7" "$got"

# THE ESCAPE. While diverted every keystroke belongs to the far end, so the
# local prompt is unreachable -- and if it is the RELAYING RULE that is wrong
# there is no way back short of a reset. So the way back is one character
# compare in C, in front of everything, and it is tested as such: ^] in, two
# characters that must NOT reach the prompt, ^] out.
cat > "$D/esc.csp" <<'CSPEOF'
#buffer Cn:8 in console
#field  K0:8 Cn[0..7]
#variable Key:8 = 0
CSPEOF
( printf '/latch off\n'; sleep 0.3; printf '\035'; sleep 0.3; printf 'Zx';
  sleep 0.3; printf '\035'; sleep 0.3; printf '/state\n/quit\n' ) |
    ./csp -i --no-eeprom "$D/esc.csp" > "$D/esc.out" 2>&1
ck "the escape says which way it went" "[remote]
[local]" "$(grep -oE '\[(remote|local)\]' "$D/esc.out")"
ck "keystrokes go to the buffer, not the prompt" "5a 78" \
   "$(grep -o '5a 78' "$D/esc.out" | head -1)"
# And the prompt got them back: /state ran, which it could not have done from
# inside the diversion.
ck "and the escape gives the prompt back" "1" \
   "$(grep -c '^cycle ' "$D/esc.out")"

# --- buffer slices in the listing --------------------------------------------
# `Buf[0]` and `Buf[2..3]` compile to a synthesised DECL_VIEW with no name of
# its own, and the listing rendered that name -- which printed nothing. So a
# rule that RAN correctly listed as `=65`, and the line did not go back in.
#
# The damage is not the missing text. A listing is how a program comes off a
# board, and reading this output is what made buffer byte-assignment look like a
# feature that did not exist: the rules worked all along.
echo "buffer slices:"

got=$(printf '#buffer A:4\n#buffer B:4\n#variable T:8 = 0\nA[0] = 65\nA[1..2] = 300\nB[0] = A[0]\nT = B[0]\nB = A\n/list\n/quit\n' |
	  repl ./csp "$D/slice.db" | grep -E '^(A\[|B\[|T=|B=|=)')
ck "a buffer slice lists with its subscript" \
'A[0]=65  // 1 R
A[1..2]=300  // 2 R
B[0]=A[0]  // 3 R
T=B[0]  // 4 R
B=A  // 5 R' "$got"

# And the point of the subscript being there: the listing goes back in.
printf '#buffer A:4\n#variable T:8 = 0\nA[0] = 65\nT = A[0]\n/list\n/quit\n' |
    repl ./csp "$D/slice2.db" | grep -E '^(#|A\[|T=)' | sed 's|  // .*||' > "$D/slice.csp"
got=$(printf '/list\n/quit\n' | repl ./csp "$D/slice3.db" "$D/slice.csp" |
	  grep -E '^(A\[|T=)')
ck "and the listing goes back in" \
'A[0]=65  // 1 R
T=A[0]  // 2 R' "$got"

# The slice WORKS, and always did -- this is the assertion that says the listing
# was the only thing wrong. B is one cycle behind A because a rule reads the
# committed half, which is the ordinary rule and not a fault.
cat > "$D/slice4.csp" <<'CSPEOF'
#buffer A:4
#buffer B:4
#field  Av:8 A[0..7]
#field  Bv:8 B[0..7]
A[0] = 65
B[0] = A[0]
CSPEOF
got=$(( printf '/latch off\n'; sleep 0.3; printf '/state\n/quit\n' ) |
	  ./csp -i --no-eeprom "$D/slice4.csp" 2>&1 |
	  grep -E '^(Av|Bv) ' | tr -s ' ' | sed 's/.*= //')
ck "a byte written through a slice is really there" "65
65" "$got"

# --- a udp BUS, and --id/--name ----------------------------------------------
# Several nodes on one port. That needs SO_REUSEADDR, which was deliberately
# taken OFF these sockets because on UNICAST it lets a forgotten process hold a
# port and silently swallow half the traffic. On BROADCAST it means the
# opposite: every bound socket gets a copy, which is exactly a bus.
#
# So the flag follows the DECLARATION, and the address says which it is -- a
# broadcast address can never be a sender, so the operand carries both meanings
# with no keyword to tell them apart.
echo "udp bus:"
cat > "$D/bus.csp" <<'CSPEOF'
#buffer Rx:4 in udp 56200 127.255.255.255
#field  V:8 Rx[0..7]
#variable Seen:8 = 0
Seen = V ? Rx.rx
println("bus", V) ? Rx.rx
CSPEOF
if command -v python3 >/dev/null 2>&1; then
    ( printf '/latch off\n'; sleep 2; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/bus.csp" > "$D/bus1.out" 2>&1 &
    b1=$!
    ( printf '/latch off\n'; sleep 2; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/bus.csp" > "$D/bus2.out" 2>&1 &
    b2=$!
    sleep 0.8
    python3 -c "
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.sendto(b'\x41BCD', ('127.255.255.255', 56200))"
    wait $b1 $b2 2>/dev/null
    ck "two nodes bind one bus port and both hear it" "1 1" \
       "$(grep -c 'bus65' "$D/bus1.out") $(grep -c 'bus65' "$D/bus2.out")"
    # The bind that would have failed: without the flag the second node reports
    # "cannot listen on port" and receives nothing for the rest of its life.
    ck "and neither was refused the port" "0" \
       "$(cat "$D/bus1.out" "$D/bus2.out" | grep -c 'cannot listen')"
else
    echo "  SKIP no python3 to drive the bus"
fi

# --id / --name: an override for a test run. They go in as IMMEDIATES, which is
# what makes them behave: recorded in the RAM store (so they survive a rebuild
# and show in /settings) and UNSAVED, so eeprom.db is not rewritten for every
# experiment.
echo "id and name overrides:"
got=$(( printf '/latch off\n'; sleep 0.3; printf '/state\n/quit\n' ) |
	  ./csp -i --no-eeprom --id=123 --name=Node1 2>&1 |
	  grep -E '^sys\.(Id|Name) ' | tr -s ' ' | sed 's/ *$//')
ck "--id and --name set the sys members" \
'sys.Id param = 123
sys.Name param = Node1' "$got"

got=$(printf '/settings\n/quit\n' | ./csp -i --no-eeprom --id=123 --name=Node1 2>&1 |
	  grep -E 'UNSAVED')
ck "and are recorded UNSAVED, not written to the store" \
   "32 of 1024 bytes, UNSAVED" "$got"

# The point of the pair: run a node without rewriting eeprom.db every time.
printf '/quit\n' | ./csp -i -e "$D/never.db" --id=55 --name=Zed >/dev/null 2>&1
ck "no store file is created for an override" "no" \
   "$([ -e "$D/never.db" ] && echo yes || echo no)"

# --- .dlc arrives WITH the bytes ---------------------------------------------
# The length used to be a live field while the bytes were double-buffered.
# csp_buf_input runs before the rules, so a rule guarded on `.rx` read the
# NEWEST length against the PREVIOUS delivery's bytes.
#
# On a byte stream that eats a character at every boundary where the next chunk
# is shorter, and nothing anywhere reports it: `abcdefghijklmnopqrstuvwxyz`
# relayed as `...uvwyz`. On CAN it meant `F201.dlc` in a rule was the length of
# a frame the rule had not been shown yet.
#
# Two datagrams of DIFFERENT lengths, spaced so each gets its own cycle. The
# assertion is that the pairs agree: 4 with ABCD, 2 with EF.
echo "dlc in step:"
cat > "$D/dlc.csp" <<'CSPEOF'
#buffer Rx:8 in udp 56300
#field  B0:8 Rx[0..7]
println("n", Rx.dlc, "first", B0) ? Rx.rx
CSPEOF
if command -v python3 >/dev/null 2>&1; then
    ( printf '/latch off\n'; sleep 2.5; printf '/quit\n' ) |
	./csp -i --no-eeprom "$D/dlc.csp" > "$D/dlc.out" 2>&1 &
    dpid=$!
    python3 -c "
import socket,time
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
time.sleep(0.6); s.sendto(b'ABCD', ('127.0.0.1', 56300))
time.sleep(0.6); s.sendto(b'EF',   ('127.0.0.1', 56300))"
    wait $dpid 2>/dev/null
    # 65 is 'A', 69 is 'E'. The lengths must pair with THOSE bytes.
    ck "a length arrives with the bytes it describes" \
"n4first65
n2first69" "$(grep -o 'n[0-9]*first[0-9]*' "$D/dlc.out")"
else
    echo "  SKIP no python3 to send datagrams"
fi

echo "settings:"

# A setting is a value for something the firmware ALREADY declares, kept in its
# own eeprom store so it outlives a reflash. The patch cannot do that job: it is
# fingerprinted against rom_header.crc_hdr and dropped the moment the program
# changes, which is right for program text and wrong for a calibration.
cat > "$D/set1.csp" <<'EOF'
#param Kp:16 = 5
#digital Led out 13
#timer T 500
#variable Out = 0
Out = Kp * 2 ? 1
EOF
# The same program with one rule and one declaration added: a DIFFERENT firmware
# as far as the fingerprint is concerned, which is the point.
cat > "$D/set2.csp" <<'EOF'
#param Kp:16 = 5
#digital Led out 13
#timer T 500
#variable Out = 0
#variable Other = 0
Out = Kp * 2 ? 1
Other = Other + 1 ? 1
EOF
# Kp gone entirely -- an orphan entry.
cat > "$D/set3.csp" <<'EOF'
#digital Led out 13
#variable Out = 0
EOF
# Kp still there but widened: the shape check must refuse the stored value
# rather than drop a 16-bit tuning into a rule compiled for 32.
cat > "$D/set4.csp" <<'EOF'
#param Kp:32 = 5
#digital Led out 13
EOF

if build_rom "$D/set1.csp" "$D/set_fw1" &&
   build_rom "$D/set2.csp" "$D/set_fw2" &&
   build_rom "$D/set3.csp" "$D/set_fw3" &&
   build_rom "$D/set4.csp" "$D/set_fw4"; then

    # An immediate write records; /save writes the store.
    got=$(printf '> Kp = 9\n> Led.pin = 7\n> T.period = 900\n/settings\n/quit\n' |
	      repl "$D/set_fw1" "$D/s1.db")
    ck "an immediate records a setting" '9
7
900
Kp = 9
Led.pin = 7
T.period = 900
30 of 1024 bytes, UNSAVED' "$got"

    # Applied in csp_rt_start, BEFORE csp_setup -- so a re-pinned output is
    # never configured on the pin the source named, not even for one cycle.
    printf '> Kp = 9\n> Led.pin = 7\n> T.period = 900\n/save\n/quit\n' |
	repl "$D/set_fw1" "$D/s2.db" >/dev/null 2>&1
    got=$(printf '> Kp\n> Led.pin\n> T.period\n/quit\n' |
	      repl "$D/set_fw1" "$D/s2.db" | grep -v '^Restored')
    ck "settings come back after a restart" '9
7
900' "$got"

    # The whole reason for a store of its own.
    got=$(printf '> Kp\n> Led.pin\n/quit\n' | repl "$D/set_fw2" "$D/s2.db" |
	      grep -v '^Restored')
    ck "settings survive a reflash that drops the patch" '9
7' "$got"

    # /list shows what the SOURCE says, so a line the store overrides has to say
    # so -- otherwise it prints a pin the unit is not running.
    got=$(printf '/list\n/quit\n' | repl "$D/set_fw1" "$D/s2.db" |
	      grep -v '^Restored' | sed -n '/^#digital Led/p;/^#param Kp/p')
    ck "an overridden declaration lists tagged S" '#param Kp:16 integer = 5  // S
#digital Led out 0:13  // S' "$got"

    # ...and /state has the live value, which is the number that matters.
    got=$(printf '/state\n/quit\n' | repl "$D/set_fw1" "$D/s2.db" |
	      sed -n '/^Led/p' | tr -s ' ')
    ck "/state shows the applied pin" 'Led out digital 0:7 = 0' "$got"

    # Kept, not applied, and SAID. Dropping it would lose a calibration the next
    # firmware may well want back; hiding it is how a store stops being trusted.
    got=$(printf '/settings\n/quit\n' | repl "$D/set_fw3" "$D/s2.db" |
	      grep -v '^Restored' | sed -n '/^Kp/p')
    ck "a vanished name becomes a visible orphan" 'Kp = 9   // orphan' "$got"

    # The boot-time twin of ERR_PARAM_SHAPE. The declaration wins.
    got=$(printf '> Kp\n/settings\n/quit\n' | repl "$D/set_fw4" "$D/s2.db" |
	      grep -v '^Restored' | sed -n '/^5$/p;/^Kp/p')
    ck "a widened param refuses the stored value" '5
Kp = 9   // not applied: width or type moved' "$got"

    # ...and it must not be tagged as if it were in effect.
    got=$(printf '/list\n/quit\n' | repl "$D/set_fw4" "$D/s2.db" |
	      grep -v '^Restored' | sed -n '/^#param Kp/p')
    ck "a refused setting does not tag the declaration" \
       '#param Kp:32 integer = 5  // F' "$got"

    # A value equal to the declaration is not a setting. Storing it would fill
    # the store with no-ops and shadow the default the day it changes.
    got=$(printf '> Kp = 9\n> Kp = 5\n/settings\n/quit\n' |
	      repl "$D/set_fw1" "$D/s3.db")
    ck "setting a value back to the default drops the entry" '9
5
no settings' "$got"

    # Only an IMMEDIATE records. A rule writing a config part is the program
    # doing its job, and freezing that would restore a value the rule recomputes.
    got=$(printf 'T.period = 700 ? 1\n/settings\n/quit\n' |
	      repl "$D/set_fw1" "$D/s4.db")
    ck "a rule writing a part does not record" 'OK
no settings' "$got"

    # PART_VAL on anything that is not a param is state, not configuration.
    got=$(printf '> Out = 3\n/settings\n/quit\n' | repl "$D/set_fw1" "$D/s5.db")
    ck "poking a variable does not record" '3
no settings' "$got"
else
    echo "  FAIL settings ROM firmware did not build"; fail=$((fail+1))
fi

# A module member costs nothing extra: the path is a string, so the dot is just
# a character and there is no object index to find room for in a declaration.
#
# NOT named Sys: the runtime declares a built-in namespace by that name (see
# csp_sys_module), and the names Sys and sys are taken the way State is.
cat > "$D/setmod.csp" <<'EOF'
#module Node
  #param NodeName string    = "Node1"
  #param NodeID:32 unsigned = 123
#end
#Node node
EOF
if build_rom "$D/setmod.csp" "$D/setmod_fw"; then
    printf '> node.NodeID = 124\n> node.NodeName = "Node2"\n/save\n/quit\n' |
	repl "$D/setmod_fw" "$D/s6.db" >/dev/null 2>&1
    got=$(printf '> node.NodeID\n> node.NodeName\n/settings\n/quit\n' |
	      repl "$D/setmod_fw" "$D/s6.db" | grep -v '^Restored')
    ck "a module member is set by path and survives" '124
Node2
node.NodeID = 124
node.NodeName = "Node2"
42 of 1024 bytes' "$got"
else
    echo "  FAIL settings module firmware did not build"; fail=$((fail+1))
fi

echo "mirrored comparisons:"

# `>` and `>=` are not opcodes. The compiler emits `b < a` with
# csp_instr_alu_t.swap set, so the runtime computes the answer with the LT arms
# and four encodings come free. What has to survive is the LISTING: undoing the
# swap takes BOTH halves -- exchange the operands AND mirror the operator --
# and doing only one renders a different program (`a < b`, or `b > a`).
cat > "$D/gt.csp" <<'EOF'
#variable A = 7
#variable B = 3
#variable R1 = 0
#variable R2 = 0
#variable R3 = 0
#variable R4 = 0
R1 = 1 ? A > B
R2 = 1 ? A >= B
R3 = 1 ? B > A
R4 = 1 ? A < B
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/gt1.db" "$D/gt.csp" | sed -n '/^R[0-9]=/p')
ck "a mirrored comparison lists back as it was written" 'R1=1 ? A>B  // 1 R
R2=1 ? A>=B  // 2 R
R3=1 ? B>A  // 3 R
R4=1 ? A<B  // 4 R' "$got"

# ...and computes the same thing it always did. A=7, B=3.
got=$(./csp -c 3 -s /dev/stdout "$D/gt.csp" 2>&1 |
	  grep -o '"R[0-9]",[0-9-]*' | tail -4 | tr '\n' ' ')
ck "a mirrored comparison computes the same" '"R1",1 "R2",1 "R3",0 "R4",0 ' "$got"

# The folder has to mirror too: eval2 is handed the MIRRORED opcode, because the
# runtime has no arm for `>` at all. Fold it unmirrored and `3 > 7` comes back as
# whatever the default arm left behind.
got=$(printf '#variable F1 = 0\n#variable F2 = 0\nF1 = 1 ? 7 > 3\nF2 = 1 ? 3 > 7\n/list\n/quit\n' |
	  repl ./csp "$D/gt2.db" | sed -n '/^F[0-9]=/p')
ck "a mirrored comparison folds" 'F1=1  // 1 R
F2=1 ? 0  // 2 R' "$got"

# Through an image: the swap bit rides in the raw instruction word, so a dumper
# that forgets it fails the section CRC at boot -- and a program that does not
# load answers "not declared" to every line typed after it.
if build_rom "$D/gt.csp" "$D/gt_fw"; then
    got=$(printf '/list\n> R1\n> R3\n/quit\n' | repl "$D/gt_fw" "$D/gt3.db" |
	      sed -n '/^R1=/p;/^R3=/p;/^[01]$/p')
    ck "the swap bit survives a ROM image" 'R1=1 ? A>B  // 1 F
R3=1 ? B>A  // 3 F
1
0' "$got"
else
    echo "  FAIL mirrored-comparison ROM firmware did not build"; fail=$((fail+1))
fi

echo "timeout as an instruction:"

# timeout(T) is OP_TMO, not a call. The call form cost three instructions -- an
# OP_LI for the timer's index, an OP_ARG to move it into place, and the OP_CALL
# -- to read one bit out of the timer's slot.
#
# What has to hold: it still LISTS as timeout(T) (the listing cannot go through
# exprbuf_fcall any more -- there is no function index to look up), it still
# computes, and the timer's index survives the image. OP_TMO is a MEMORY
# instruction, so a dumper that emits it through the ALU arm truncates mem to
# four bits and any timer past index 15 fails the section CRC at boot.
cat > "$D/tmo.csp" <<'EOF'
#timer T 100 = 1
#variable N = 0
T = 1 ? timeout(T)
N = N + 1 ? timeout(T)
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/tmo1.db" "$D/tmo.csp" | sed -n '/timeout/p')
ck "timeout lists back as a call" 'T=1 ? timeout(T)  // 1 R
N=N+1 ? timeout(T)  // 2 R' "$got"

# A timer inside a module: the index is CURRENT-relative and asm_mem lays down
# the OP_SETO, the same binding the call path did through call_obj.
cat > "$D/tmomod.csp" <<'EOF'
#module M
  #timer T 100 = 1
  #variable C = 0
  C = C + 1 ? timeout(T)
#end
#M m1
EOF
got=$(printf '/list\n/quit\n' | repl ./csp "$D/tmo2.db" "$D/tmomod.csp" | sed -n '/timeout/p')
ck "timeout on an object timer lists back" '  C=C+1 ? timeout(T)  // 1 R' "$got"

# It runs: a 100 ms timer over ~450 ms fires four times.
got=$(./csp -T 450 -s /dev/stdout "$D/tmo.csp" 2>&1 | grep -o '"N",[0-9]*' | tail -1)
ck "timeout still fires" '"N",4' "$got"

# Through an image, where the CRC checks the instruction word bit for bit.
if build_rom "$D/tmo.csp" "$D/tmo_fw"; then
    got=$(printf '/list\n/quit\n' | repl "$D/tmo_fw" "$D/tmo3.db" | sed -n '/timeout/p')
    ck "timeout survives a ROM image" 'T=1 ? timeout(T)  // 1 F
N=N+1 ? timeout(T)  // 2 F' "$got"
else
    echo "  FAIL timeout ROM firmware did not build"; fail=$((fail+1))
fi

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

echo "/undo:"

# Taking back the last typed line is a TRUNCATION to where the line began -- the
# same thing /clear does, to a nearer floor. The cases below are the four ways
# that can go wrong.

# The one that prompted it: a rule typed with the wrong target, taken back.
got=$(printf '#digital A out 1:22\n#digital B out 1:23\nA=1\nA=0\n/undo\n/list\n' |
	  repl ./csp "$D/undo.db")
ck "undo takes back the last rule" \
   'OK
OK
OK
OK
Took back 1 line
#digital A out 1:22  // R
#digital B out 1:23  // R
A=1  // 1 R' "$got"

# Undoing a DECLARATION has to return the name it introduced, or the string
# table leaks on every typo and the feature needs a compaction pass to be worth
# having. ps.strp is one of the four cursors precisely so this works.
before=$(printf '#digital A out 1:22\n/memory\n' | repl ./csp "$D/undo2.db" |
	     sed -n 's/^  string *\([0-9]*\).*/\1/p')
after=$(printf '#digital A out 1:22\n#digital Bbbbbbbbbb out 1:23\n/undo\n/memory\n' |
	    repl ./csp "$D/undo3.db" | sed -n 's/^  string *\([0-9]*\).*/\1/p')
ck "undo returns the string space a declaration took" "$before" "$after"

# A line that edits IN PLACE moves no cursor, so it must not push a mark --
# otherwise the next /undo withdraws some older line the user had stopped
# thinking about. #disable is the case that matters: it is what one reaches for
# right before reaching for undo.
got=$(printf '#digital A out 1:22\nA=1\nA=0\n#disable 1\n/undo\n/list\n' |
	  repl ./csp "$D/undo4.db")
ck "a #disable does not consume the undo history" \
   'OK
OK
OK
OK
Took back 1 line
#digital A out 1:22  // R
A=1  // 1 R!' "$got"

# Asking for more than the ring holds takes back what it has and says how many.
# Then the disable bit must be GONE: a rule added afterwards inherits the number
# of one that was withdrawn, and inheriting its disable is silent and baffling.
got=$(printf '#digital A out 1:22\nA=1\n#disable 1\n/undo 9\n#digital B out 1:23\nB=1\n/list\n/undo 9\n/undo\n' |
	  repl ./csp "$D/undo5.db")
ck "undo past the end stops at the floor and clears stale disables" \
   'OK
OK
OK
Took back 2 lines
OK
OK
#digital B out 1:23  // R
B=1  // 1 R
Took back 2 lines
Nothing to take back' "$got"

echo "================================================"
echo "repl: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
