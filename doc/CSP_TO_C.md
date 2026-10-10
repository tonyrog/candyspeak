# CandySpeak to C

Where the translation stands, what it costs, and where it could go.

## What there is

`utils/candyspeak_c.erl` translates a `.csp` -- imports and all -- into one C
file against `include/csp_lib.h` (the language's semantics: arithmetic, timers,
analog scaling) and `include/csp_chip_io.h` (the pins, one library per chip).
No interpreter, no compiler and no declaration table go along: the rules ARE the
C.

- **The oracle**, `tests/clib_oracle.escript`, runs every program both ways --
  `./csp` and the translation -- and holds the final value of every name against
  each other. `tests/clib`, `tests/unit` and `boards/bridgezone/tests` pass.
- **Backends**: bare AVR (`chips/atmel/drivers/avr`), LPC
  (`chips/nxp/drivers/common/csp_chip_lpc.c`), Arduino
  (`port/csp_lib_arduino.cpp`, `BACKEND=c`), the host (`port/csp_lib_host.c`,
  `-P` for a live program on a pty).
- **A program's link**: a `link.c` beside the `.csp` is compiled into the same
  unit and reaches the program by member -- `boards/coco/link.c` is CoCo's
  master protocol.

## The state

A cycle is a transaction, so a variable has two copies, `in` (committed, what
the rules read) and `out` (what they write). That is the runtime's DIN/DOUT,
and the translation keeps it -- but only for what needs it:

| | Where it lives | Copies |
|---|---|---|
| `#variable`, pins, timers | `M_t`, the struct of the module | 2: `csp_in`, `csp_out` |
| `#param` | `M_p`, the same tree shape | 1: `csp_par` |
| `#local` read only by its own module | a C variable in `M_run` | 0 -- a register |
| `#local in`, `out`, read as `x.Name`, or by `changed()` | `M_t` | 2 |

A `#local` holds for one cycle: it is a formula, evaluated where it stands. So a
C local is all it is, and the compiler keeps it in a register when it can. A
`#param` is not written by the rules, it is set from outside -- so it takes no
part in the transaction, and a second copy would only hold the same value twice.

What that came to on BridgeZone's `main.csp` (8 inputs, 10 drives, 10 outputs,
4 analog filters), ARM7 Thumb, `-Os`:

| | flash | RAM |
|---|---|---|
| runtime + ROM image | 118 KB | does not fit the pool (5.7 KB) |
| C, every name a field in both copies | 9.5 KB | 13.0 KB |
| C, `#local` as C locals | 8.5 KB | 5.4 KB |
| C, and `#param` once | 8.8 KB | 4.2 KB |

CoCo (`boards/coco`) on an ATmega328P: 1379 B RAM to 589 B.

## Ideas

### `#param` once in the runtime too (investigated 2026-10-08)

How a leaf is stored today (`src/csp_rt.c`):

- every leaf has a 6-byte `csp_view_t`, and its bytes in the heap;
- the heap is ONE block of `2 * hbytes`, DIN first and DOUT after it, and
  `csp_slot` is `heap[dir] + pos`;
- a `#local` was a pair like any variable, its write mirrored into DIN by
  `csp_set_value` so the same cycle could read it (`VIEW_F_LOCAL` was tested
  only by `csp_slot`, which an OWN view never reaches);
- a `#param` is a `DECL_CONSTANT` with the local bit, set up by `setup_slot`
  as a `VIEW_SLOT` with no flags: double-buffered like a variable.

So the single copy is half there. What it would take:

1. **A `#param` single-buffered:** `setup_slot` sets `VIEW_F_LOCAL` on a
   param's view, as `setup_variable` does for a `#local`. One line, and the
   semantics are already right: a param is written from outside, never by the
   transaction. (A rule that writes a param would then be seen at once, not
   next cycle -- worth refusing in the compiler, since a param that a rule
   writes is a variable.)
2. **The memory back:** single-buffered leaves out of the doubled range. Lay the
   block out as `[DIN, doubled][DOUT, doubled][single]`, the singles at
   positions from `2 * hd` up -- `csp_slot` already forces them to DIN, so
   `heap[DIN] + pos` lands right. `csp_estimate` counts the two kinds apart,
   and `heap_dset_copy` and every raw `heap[dir]` access must skip a single
   leaf, since its DOUT address is past the block. That audit is the work; the
   last time raw heap accesses were changed, the misses were silent.

What it buys, on BridgeZone's `main.csp` (about 1740 leaves: 1160 `#local`,
320 `#param`, the rest variables and pins):

| | views | heap | total |
|---|---|---|---|
| today | 10.4 KB | 13.9 KB | 24.3 KB |
| locals and params single | 10.4 KB | 8.0 KB | 18.4 KB |

Worth having on every board -- it is a quarter of the derived tables -- but it
does not bring BridgeZone into an LPC2129's 5.7 KB pool. The views are the
larger half, and a `#local` needs one per INSTANCE.

**Done for `#local` (2026-10-10): the DLOCAL region.** The heap block is
`[DIN][DOUT][DLOCAL]` now. A `#local` takes its one copy from DLOCAL
(`csp_lheap_alloc`, sized by `csp_estimate.lheap`), `heap_base` sends both
directions there, and the commit skips it -- it is still marked dirty, since
`changed()` reads the dirty set. On BridgeZone's `main.csp` the derived tables
went from 28.8 KB to 24.2 KB. `#param` is not moved yet.

**The bigger lever is the one the C translation found:** a `#local` lives only
while its own instance is being evaluated, so all instances of a module can
share ONE set of local slots -- a scratch area per module, as a C function's
locals are one stack frame whatever the caller. On BridgeZone that takes the
1160 local leaves down to about 140 (47 + 23 + 47 + 19, plus Main's own), and
the derived tables from 24 KB to around 9 KB. It needs a local's index to stop
being per instance (`st_index`), which is a deeper change than the two above.

### The translation inside the toolchain

Today `candyspeak_c` is Erlang, beside the C compiler in `src/csp_compile.c`.
Two front ends for one language is the arrangement every oracle test exists to
police. The alternative: the C compiler emits C -- from the same parse tree it
already builds, or from the instruction stream, which is closer to what the
runtime actually executes. Then `./csp -O prog.c prog.csp` and the translation
cannot disagree about what a program means, because there is one parser.

From the instructions is the more interesting of the two: the bytecode is
already the program with names resolved, constants folded and the evaluation
order fixed, and a translator from it is a table -- one C template per opcode,
the way `utils/words.terms` already describes the opcodes for micro-csp.

### #disable in a translated program (done 2026-10-08)

Every guard carries its rule number, `CSP_ON(n)`, counted the way the runtime
counts OP_RULEs -- source order, a module's body where `#module` stands, a
`#local` formula and each instance binding counting as one. `#disable 3 5-7`
in the program becomes calls in `csp_lib_setup`. With `-DCSP_LIB_RULES` the
guard tests `csp_lib_off[]` and `csp_lib_disable(n)` / `csp_lib_enable(n)`
switch a rule while the program runs; without it CSP_ON is 1 and costs nothing.
On BridgeZone (271 rules) the mask costs 1.1 KB of flash, 16 bytes of RAM.
`tests/clib/disable.csp` holds the numbering against the runtime's.

That is the half a hybrid needs: a translated program with a small runtime
beside it, where a rule typed at a prompt replaces a translated one by
disabling it and running in the interpreter after the C.

### From the instruction stream: utils/csp_to_c.erl (draft, 2026-10-08)

```
erl -noshell -pa utils -eval 'csp_to_c:main(["prog.csp", "prog.c"])'
escript tests/clib_oracle.escript --bytecode [dir ...]
```

Reads `./csp -P` (a `.csp` is compiled first and its dump kept as `prog.dump`
beside the C) and writes C against the same `csp_lib.h` contract as
`candyspeak_c`, so the host harness, `Makefile.board` and the oracle take it
unchanged. One C statement per instruction, registers as C locals, a label
per jump target. The dump learned five things on the way: RULE's `implicit`
State gate, the ALU's `unsigned`, the string table, the disabled rules, and
the settings store -- the program's `>` lines by path. It also had two bugs:
`tag_tab` stopped at DECL_FIELD, so a buffer or view printed `{,10}`, and an
ENTER body with strings in it ran past its LEAVE.

Held against the runtime: tests/clib and tests/unit 59 passed (the source
translator: 58), BridgeZone 15 of 15. Not yet: buffers and fields, parts
other than a timer's, arrays, floats, digital `.fired`. On BridgeZone it is
10.8 KB of text against the source translator's 8.9 -- gcc keeps the
registers in registers -- and 13 KB of RAM against 4, because every `#local`
and `#param` is still a field in both copies. The same two changes would
bring it down the same way.

### From the instruction stream (what it took)

`./csp -P` already prints the compiled program as Erlang terms:

```
{instr,66,'LD',[r0,{v,11}]}.          r0 = in->B
{instr,67,'LI',[r1,5]}.               r1 = 5
{instr,68,'LT',[r2,r0,r1]}.           r2 = r0 < r1
{instr,70,'RULE',[r1,5]}.             if (!r1) goto L75
{instr,73,'ADD',[r0,r1,r2]}.          r0 = r1 + r2
{instr,74,'ST',[r0,{v,10}]}.          out->A = r0
{instr,76,'ENTER','M',[{n,7}],[...    static void M_run(M_t* in, M_t* out)
{instr,85,'NEW',"M","m1",[{ent,76}]}  M_run(&in->m1, &out->m1)
```

It is register code, so a translation is one C statement per instruction and a
label per jump target -- no stack to simulate. What is missing:

- **The declarations in full.** The dump has names, kinds, widths, types and
  initial values; a translator also needs pins and directions, timers, buffers
  and fields with their bit positions, the `#local` and `#param` flags, and
  which declarations belong to which module and instance. Most of it is in the
  ROM image already; the dump does not print all of it.
- **One template per opcode**, about 60: loads and stores (global, `cur`, parts),
  the ALU, RULE / INSTATE / `#when` skips as gotos, ENTER / LEAVE / NEW / NEXT as
  module functions and calls, CALL through the builtin table to `csp_lib`,
  SEGMENT for the strings `println` prints.
- **The addressing.** `{v,11}` is a global, `{cur,v,14}` a member of the
  instance being run; a `#local` reads the working copy. The struct emission
  of the pretty translator serves both, so it should be shared, not written
  twice.
- **The oracle**, which needs nothing: it already holds any C against `./csp`.

What it shows is that the bytecode IS the semantics -- and its templates are
the JIT's, written in C instead of machine code. What it does not do is read
well: `r2 = r0 < r1` is what the program says after the compiler has had it.
The pretty translation stays the one to read.

### JIT

CandySpeak is small: a fixed set of opcodes, no loops inside a rule, no
recursion, no allocation in a cycle. That is the easy case for a compiler to
native code -- a straight-line template per opcode, registers for the
expression stack, branches only for guards and `#in` blocks.

The moment to do it is when a program is LOADED: from the ROM image at boot,
from EEPROM after `/save`, from a line typed at the prompt after a rebuild.
Everything the JIT needs is known then -- every declaration's slot, every
width, which `#local`s are read from outside -- and none of it changes until
the next load. The interpreter stays as the reference and the fallback, and the
oracle that holds the translation against it today holds the JIT the same way.

What makes it more than a speed-up:

- a program from EEPROM runs at the speed of a compiled one without a reflash;
- the RAM the interpreter needs for its tables is the RAM a 16K part does not
  have -- BridgeZone's `main.csp` fits translated and does not fit interpreted;
- the instruction templates are per architecture and few: ARM Thumb covers the
  LPCs, the SAMD and the STM32s; AVR is the hard one (no executable RAM -- it
  would have to go to flash, page by page, which `/save` already knows how to
  do).

Things to settle first:

- where generated code lives: RAM on a Cortex-M (executable), flash on an AVR
  and on parts with an MPU that forbids it;
- what a rebuild costs on a part where a page erase is milliseconds;
- the size of the template table against the size of the interpreter it would
  sit beside.
