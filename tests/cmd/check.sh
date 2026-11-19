#!/bin/sh
# ./cs node against a node of our own, its console a telnet shell on tcp:
# csp_cmd's connection and commands, the way ./cs says them.
D=tmp/cmd_check
mkdir -p $D
./csp -i --no-eeprom -e $D/node.db \
    tests/cmd/node.csp < /dev/null > $D/node.out 2>&1 &
cpid=$!
sleep 0.7
fail=0
node() { ./cs node "$@" -D 127.0.0.1:55971 2>&1; }
ck() {   # what, expected, got
    if [ "$2" = "$3" ]; then echo "ok    $1"
    else echo "FAIL  $1"; echo "  want: $2" | head -5; echo "  got:  $3" | head -5
	 fail=1; fi
}
x() { node state | sed -n 's/^  X  *//p'; }

got=$(node info | head -1 | sed 's/fw [^,]*/fw V/; s/RAM [0-9]* free of [0-9]*/RAM/')
ck "info: what the node is, in a line" \
   "node        host-ab fw V, Q16.16 little-endian, RAM, A: erased, B: erased" "$got"
ck "info: its rom format" "rom         format 23" "$(node info | grep '^rom')"
ck "info: the transports it has, the program not read" \
'transp      can, console, repl, sms, tcp, udp' "$(node info | grep '^transp')"
ck "info -use: the ones the program uses, and what of it runs on nothing" \
'transp      can, console, repl, sms, tcp, udp
transp-use  tcp, repl, i2c1
transp-null i2c1 -- the null device here, never delivers' "$(node info -use | grep '^transp')"
ck "info -use -detail: every endpoint, no names" \
'transp-use  tcp 55971, repl, i2c 1 0x68 0x3b
transp-null i2c 1 0x68 0x3b -- the null device here, never delivers' \
"$(node info -use -detail | grep '^transp-')"
ck "info FILE: another program held against the node, all of it there" \
'transp-use  can' "$(node info tests/cmd/other.csp | grep '^transp-')"
ck "list: the program it runs" \
'#buffer Net:160 inout tcp 55971  // E
#buffer Rp:160 inout repl  // E
#route Net Rp  // E
#route Rp Net  // E
#buffer Imu:2 in i2c 1 0x68 0x3b  // E
#variable X:32 integer = 7  // E
#variable Red[3]:8 unsigned = 0  // E
#param P:32 integer = 3  // E
#param Name:32 string = "n1"  // E
#variable F:32 float = 1.500000  // E
X=X+1  // 1 E' "$(node list)"
ck "state names an array's elements as list does, the head too" \
'  Red[0]           0
  Red[1]           0
  Red[2]           0' "$(node state Red | tail -n +2)"
ck "get: one value, bare" "0" "$(node get 'Red[2]')"
ck "get: a name that is more than one value says so" \
   "cs: Red is not one value: name an element, Red[0]" "$(node get Red)"
ck "get: several at once, a string and a float among them" \
'P 3
Name "n1"
F 1.5' "$(node get P Name F)"
ck "get: a name not there" "cs: no Nope on the node" "$(node get Nope)"
ck "pause" "Paused (execution stopped; edit/inspect, then /resume)" "$(node pause)"
a=$(x)
ck "paused, it stays where it is" "$a" "$(x)"
node step 3 >/dev/null
ck "step 3 is three cycles" "$((a + 3))" "$(x)"
node set X 100 'Red[1]' 5 >/dev/null
node step >/dev/null
ck "a value set while paused is read by the next step" "101" "$(x)"
ck "set takes more than one" "5" "$(node get 'Red[1]')"
ck "go" "Resumed" "$(node go)"
ck "live: rules frozen, I/O running" \
   "Live (rules frozen, I/O running -- poke away; /resume to run)" "$(node live)"
node set X 42 P 5 >/dev/null
ck "live: a value set is there at once" "42" "$(x)"
sleep 0.3
ck "live: and stays, the rules do not run" "42" "$(x)"
ck "settings: a #param set by hand" "P = 5
9 of 1024 bytes, UNSAVED" "$(node settings)"
ck "latch off and on: nothing said when it took" "" "$(node latch off; node latch on)"
ck "save" "Saved to $D/node.db" "$(node save | cut -d' ' -f1-3)"
ck "load" "Loaded from $D/node.db" "$(node load | cut -d' ' -f1-3)"
ck "settings: saved" "9 of 1024 bytes" "$(node settings | tail -1)"
ck "go again" "Resumed" "$(node go)"
ck "send: a console line, and what it said" "0: ROM" \
   "$(node send /images | head -1 | cut -c1-6)"
ck "patch: rules added and one switched off, not saved" \
   "patched: 2 lines, 1 #disable/#enable -- not saved (node undo takes it back, node save keeps it)" \
   "$(node patch tests/cmd/patch_ok.csp)"
ck "patch: what the node runs now" \
'#variable Y:32 integer = 0  // E
X=X+1  // 1 E!
Y=X ? 1  // 2 E' "$(node list | tail -3)"
ck "undo: the patch's lines go as one; a #disable stays" \
'Took back 1 line
X=X+1  // 1 E!' "$(node undo; node list | tail -1)"
ck "enable: the rule back on" "X=X+1  // 1 E" "$(node enable 1; node list | tail -1)"
ck "patch: a line the node refuses, and nothing of it is left" \
'cs: tests/cmd/patch_bad.csp:3: Z = (
  Error: syntax error
nothing of the patch is left on the node
#variable F:32 float = 1.500000  // E
X=X+1  // 1 E' "$(node patch tests/cmd/patch_bad.csp; node list | tail -2)"
ck "patch: a #disable refused puts back the rules it had switched" \
'cs: tests/cmd/patch_badrule.csp:2: #disable 1 9
  Error: no rule 9, the program has 1
nothing of the patch is left on the node
X=X+1  // 1 E' "$(node patch tests/cmd/patch_badrule.csp; node list | tail -1)"
ck "patch: a command is no patch, refused before anything is sent" \
'cs: tests/cmd/patch_cmd.csp:2: /clear
  a patch is declarations, rules and #disable/#enable -- not a command
nothing of the patch is left on the node' "$(node patch tests/cmd/patch_cmd.csp)"
ck "patch -hex: compiled here, only what it adds sent" \
   "patched, compiled here: OK 1 decls, 6 instrs added -- not saved" \
   "$(node patch -hex tests/cmd/patch_ok.csp)"
ck "patch -hex: the node runs it, a rule off and one added" \
'#variable Y:32 integer = 0  // E
X=X+1  // 1 E!
Y=X ? 1  // 2 E' "$(node list | tail -3)"
ck "patch -hex: undo takes it back as one" \
'Took back 1 line
X=X+1  // 1 E' "$(node undo; node enable 1; node list | tail -1)"
ck "patch -hex: a line the host refuses, nothing sent" \
'cs: tests/cmd/patch_bad.csp:3: Z = (
  Error: syntax error
nothing of the patch is left on the node
X=X+1  // 1 E' "$(node patch -hex tests/cmd/patch_bad.csp; node list | tail -1)"
ck "/patch: a transfer abandoned changes nothing" \
'OK
ERR aborted -- nothing changed' "$(node send /patch; node send '!')"
ck "/patch: a head for another program is refused at once" \
'ERR made for another image' \
   "$(node send /patch >/dev/null; node send '0 435350440100341200000000000000000000000000000000 5364' | head -1; node send '!' >/dev/null)"
ck "no node there: said, not a crash" "cs: 127.0.0.1:1: econnrefused" \
   "$(./cs node info -D 127.0.0.1:1 2>&1)"
kill $cpid
wait $cpid 2>/dev/null

# A rexec node: the program is its linked image, and it has commands but no
# compiler -- a patch is compiled here and sent as hex, a source line refused.
sleep 0.3
tmp/cmd_check/rexec -i --no-eeprom -e $D/rexec.db < /dev/null > $D/rexec.out 2>&1 &
cpid=$!
sleep 0.7
ck "rexec: no compiler among its features" "features    eeprom" \
   "$(node info | grep '^features')"
ck "rexec: a patch goes as hex without asking" \
   "patched, compiled here: OK 1 decls, 39 instrs added -- not saved" \
   "$(node patch tests/cmd/patch_ok.csp)"
ck "rexec: the patch runs on the image" \
'#variable Y:32 integer = 0  // E
X=X+1  // 1 F!
Y=X ? 1  // 2 E' "$(node list | tail -3)"
ck "rexec: a source line is refused" \
   "ERR no compiler here -- patch from the host" "$(node send 'Y = 3')"
ck "rexec: undo takes the patch back" \
'Took back 1 line
X=X+1  // 1 F' "$(node undo; node enable 1; node list | tail -1)"
ck "rexec: set, as a patch's values" "OK 2 set" "$(node set 'Red[1]' 5 P 7)"
ck "rexec: and get reads them" "5 7" "$(node get 'Red[1]') $(node get P)"
kill $cpid
wait $cpid 2>/dev/null

# A slim node, uno_bare's tier: /dump and /patch and nothing else, refusals
# by number -- the words are csp_cmd's. Values and /save go through /patch.
sleep 0.3
rm_db() { : > $D/slim.db; }
rm_db
tmp/cmd_check/slim -i -e $D/slim.db < /dev/null > $D/slim.out 2>&1 &
cpid=$!
sleep 0.7
ck "slim: no compiler, and what it is" "features    eeprom" \
   "$(node info | grep '^features')"
ck "slim: a refusal comes as a number, said in words here" \
   "ERR no compiler here -- patch from the host" "$(node send 'Y = 3')"
ck "slim: a command it does not have" "ERR no such command" \
   "$(node send /list)"
ck "slim: set, as a patch's values" "OK 2 set" "$(node set 'Red[1]' 5 P 7)"
ck "slim: get reads them" "5 7" "$(node get 'Red[1]') $(node get P)"
ck "slim: a value not there, and nothing of the set is" \
   "cs: ERR no such value -- nothing changed: Nope
5" "$(node set 'Red[1]' 6 Nope 1; node get 'Red[1]')"
ck "slim: a string and a float, by name" 'OK 2 set
Name "n2"
F 2.25' "$(node set Name '"n2"' F 2.25; node get Name F)"
ck "slim: a value of the wrong kind is refused" \
   "cs: ERR a value of the wrong kind for it -- nothing changed: P" \
   "$(node set P '"x"')"
ck "slim: an integer set on a float is a float" "3.0" "$(node set F 3 >/dev/null; node get F)"
ck "slim: a patch, compiled here" \
   "patched, compiled here: OK 1 decls, 6 instrs added -- not saved" \
   "$(node patch tests/cmd/patch_ok.csp)"
ck "slim: listed from what /dump says" \
'#variable Y:32 integer = 0  // E
X=X+1  // 1 F!
Y=X ? 1  // 2 E' "$(node list | tail -3)"
ck "slim: save, as a patch" "saved" "$(node save)"
kill $cpid
wait $cpid 2>/dev/null
sleep 0.3
tmp/cmd_check/slim -i -e $D/slim.db < /dev/null > $D/slim.out 2>&1 &
cpid=$!
sleep 0.7
ck "slim: after a restart the patch and the #param are back" \
'Y=X ? 1  // 2 E
7' "$(node list | tail -1; node get P)"
kill $cpid
wait $cpid 2>/dev/null

# examples/tcp_serv.csp as a rexec image: an empty node whose port is a
# #param. Two of the one image on this machine, a port and an id each.
at() { ./cs node "$@" 2>&1; }
: > $D/s1.db
: > $D/s2.db
tmp/cmd_check/serv -i -e $D/s1.db --set Port=15561 --id=1 < /dev/null > $D/s1.out 2>&1 &
c1=$!
tmp/cmd_check/serv -i -e $D/s2.db --set Port=15562 --id=2 < /dev/null > $D/s2.out 2>&1 &
c2=$!
sleep 0.7
ck "serv: one image, two nodes, a port and an id each" "1 2" \
   "$(at get sys.Id -D 127.0.0.1:15561) $(at get sys.Id -D 127.0.0.1:15562)"
ck "serv: the port is listed by its name" \
'#param Port:32 integer = 15555  // F
#buffer Net:160 inout tcp Port  // F' "$(at list -D 127.0.0.1:15561 | head -2)"
ck "serv: set and saved, the node comes back on its new port" "15563" \
   "$(at set Port 15563 -D 127.0.0.1:15561 >/dev/null; at save -D 127.0.0.1:15561 >/dev/null
      kill $c1; wait $c1 2>/dev/null
      tmp/cmd_check/serv -i -e $D/s1.db < /dev/null > $D/s1.out 2>&1 &
      echo $! > $D/c1.pid; sleep 0.7; at get Port -D 127.0.0.1:15563)"
kill $c2 $(cat $D/c1.pid) 2>/dev/null
exit $fail
