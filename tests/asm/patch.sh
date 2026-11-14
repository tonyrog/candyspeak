#!/bin/bash
# csp_asm's patches against a node's: a patch typed at the prompt over a ROM
# image and saved, taken apart and put together again, has to be the store
# /save wrote, byte for byte -- and a store csp_asm BUILDS has to be one the
# node loads: tests/asm/patch.escript changes it and the node lists the change.
#
# The node is a host binary with tests/asm/rom.csp linked in as its ROM, the
# way a board carries one (`make rom`).
cd "$(dirname "$0")/../.."
T=tmp/asm_patch
mkdir -p $T
: > $T/ee.db; : > $T/ee2.db
make -s rom PROG=tests/asm/rom.csp OUT=$T/node >/dev/null || exit 1
# a declaration, a string param, an object of the image's module, rules, a
# #disable and a setting
{ printf '#variable X = 7\n#param Who string = "kalle"\n#Cnt b Step = 2\n'
  printf 'X = X + 2 ? timeout(T)\nX = 0 ? X > 100\n#disable 2\n> Kp = 9\n'
  printf '/save\n/dump rom\n/list\n/quit\n'; } |
    $T/node -i -e $T/ee.db > $T/out.txt 2>&1
escript tests/asm/patch.escript $T
