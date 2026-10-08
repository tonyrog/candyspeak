# Where the flash goes on AVR

Notes from measuring `mega_bare` and `uno_bare` with `-flto` off, which is how
you get per-function numbers out of `avr-nm`. Everything here is a measurement,
not a rule of thumb; the numbers are reproducible from the tree.

## The 64-byte window

AVR reaches a struct member with

    ldd Rd, Z+q

and **`q` is six bits: 0..63**. There is no wider displacement. A member past
byte 63 cannot be addressed at all until the address has been built:

    movw r30, r24        ; Z = st
    subi r30, lo8(-off)  ; Z += off
    sbci r31, hi8(-off)

Six bytes, before anything is loaded. gcc hoists that out of loops where it can,
but it has three pointer registers (X/Y/Z) and spills the rest to the frame.

`csp_rt_t` is about 1400 bytes. That makes *which member sits where* a code-size
decision. `csp_route_run` was 430 bytes for fifteen lines of C, and its
instruction histogram said why: of 208 instructions, 69 were loads/stores, 40
were multi-byte address arithmetic, 28 were register moves, and 5 were calls.
The function is almost entirely address computation. Fifty of those bytes are
the prologue hoisting five `st` field addresses into the frame before the loop
starts.

### The hot block

The first 64 bytes of `csp_rt_t` are now a marked block holding the members the
runtime touches most, ranked by uses per byte:

| member | uses | bytes |
|---|---|---|
| `ps` | 218 | 32 |
| `cs` | 193 | 2 |
| `cur` | 57 | 1 |
| `buf` | 55 | 2 |
| `rom_nn` | 38 | 2 |
| `heap` | 32 | 4 |
| `rom_nd` | 26 | 2 |
| `set_used` | 22 | 2 |
| `cbase`, `gsx`, `dset`, `view`, `nio`, `route`, `nroute`, `nbuf` | 12–18 each | 2 each |

Moving them cost nothing but their position and bought **2698 bytes on
mega_bare, 1206 on uno_bare**.

The block ends at byte 63 exactly. There is no slack, so a `CSP_STATIC_ASSERT`
under `csp_rt_t` says so when a member is added or grows. It is `__AVR__`-only
because 64 is an AVR number — host pointers are four times as wide and the same
members do not fit — but that does not make it a target-only check:
`utils/width_check.sh` compiles the tree with `avr-gcc` and runs inside
`make test`, so it fires on a workstation.

**The fix is never to raise the bound.** It is to take something back out.

### What does NOT work: a pointer to a big substruct

`es` (`csp_estate_t`, 91 bytes, 173 read sites) cannot fit in the block. The
obvious move is to make it a pointer, so that every one of its members is at a
small offset from something. Measured, that is **worse**: mega_bare +106,
uno_bare +56. gcc already hoists `st + offsetof(es)` into a register for the
length of a function, and loading a pointer out of RAM costs more than the
arithmetic it replaces. `es` stays a member.

Position still matters for it: moving `es` from offset 1165 to just past the hot
block was worth another 42 bytes on mega_bare, 34 on uno_bare.

## Reading a bit-field out of PROGMEM

A PROGMEM bit-field cannot be read through a pointer at all. The record has to
reach RAM first, and `memcpy_P` is the only way in.

`decl(st,i,fld)` used to be `csp_get_decl(st,i).fld` — an eight-byte struct
returned by value. Every call site spilled all eight bytes to its own frame
before extracting the four bits it wanted:

    call csp_get_decl
    std Y+9, r18    ; ... eight of these, sixteen bytes ...
    std Y+16, r25
    andi r18, 0x0F

141 sites on mega_bare. Forty-eight of the uses want only `type`, four bits of
byte 0.

`csp_decl_ref(st,i)` unpacks **once** into `st->dcache` and returns a
**pointer**. Bit-field access is then a native `ldd Z+q` with `q` in 0..7, and
the RAM half — every board with no ROM bound — copies nothing at all. **4340
bytes on mega_bare.**

One slot is enough because `decl()` is a statement expression:

    #define decl(st,i,fld) \
        (__extension__({ const csp_decl_t* d_ = csp_decl_ref((st),(i)); d_->fld; }))

That sequences the call with the load, so `f(decl(st,a,x), decl(st,b,y))` is
safe whichever argument gcc evaluates first. Without it gcc could issue both
calls before either load and the second would evict the first. `&decl(...)` no
longer compiles, which is the point. The cache is compiled in on every target,
not just AVR, so host tests run the same eviction.

Never hold a `csp_decl_ref` pointer across another call to it.

## Keeping csp_rt_t maintainable

A hand-ranked hot block rots: "how hot is this member" is a measurement, and
nobody re-measures before adding a field. The tree already has a better
criterion, and `CSP_DEFINE_BYTES` is the model for it:

    //   CSP_EXEC_ONLY   0. There is no parser, so a #define cannot be created --
    //                   the buffer would be RAM spent on a feature the build
    //                   does not have.
    //   ARDUINO         128.
    //   host            512.

Sized by **what the build can do**, not by which chip it is for. That is a
capability question, which whoever adds a field can answer without measuring
anything, and it needs no pointer, no NULL check and no indirection — which
matters, because indirection measured *worse* (see `es` above).

### The three tiers

| tier | test | members |
|---|---|---|
| **exec** | every node that runs a program | `ps`, `es`, `buf`, `heap`, `route`, `rom_*`, `settings`, the io/timer lists, `view`/`dset` |
| **compile** | the node can parse | `cs` (a pointer; 161 compile uses vs 20 exec), `def_str` (`CSP_DEFINE_BYTES`) |
| **console** | the node has a prompt | `undo` (`CSP_UNDO_DEPTH`), `line` (`CSP_LINE_SIMPLE`), `list_state`/`list_states`/`list_nstate`/`list_implicit`, `up_active` |

`settings` is sized per board from `boards/*.terms` (`CSP_SETTINGS_BYTES`: 256
on mega_bare, 128 on uno_bare), which is the same idea at board granularity.

The layout rule then falls out of the tiers instead of being imposed on top: the
compile and console tiers shrink to nothing on the boards that cannot use them,
and what is left at the front of the struct is the exec tier's scalars and
pointers — which is what the hot block wanted to be all along.

### Not yet tiered

- ~~`err_str[128]`~~ **done**: it is `{err_str_bytes, N}` in the board terms
  now, and `uno_bare` says 0. See below for why an exec-only node wants that.
- `list_state`/`list_states`/`list_nstate`/`list_implicit` (13 bytes) and
  `up_active` (1) are console-tier members that are still unconditional.

### The error texts were never the cost

The obvious saving on an exec-only node looks like `err_tab` and the 1294 bytes
of `err_*` text behind it -- print a CODE and all of that goes. Measured: it is
already gone. Nothing on such a node calls `csp_print_error`, so `-ffunction-
sections -fdata-sections` plus `--gc-sections` drops the formatter, the table
and every string it points at. `strings uno_bare/firmware.elf | grep -c 'is not
declared'` is 0; on `mega_bare`, which has the compiler, it is 9.

What is NOT free is `err_str`, the scratch the `%s` arguments are copied into.
That is a member of `csp_rt_t`, and `.bss` is not garbage-collected per field,
so an exec-only node carried 128 bytes of buffer for a formatter that is not in
the image. `{err_str_bytes, 0}` removes the field: **-128 bytes of RAM**, -16 of
flash, on a part with 2048 and a stack that was down to 872.

The one thing to get right is what `csp_set_err_arg_*` do when there is nowhere
to copy. They used to leave `err_args[i]` untouched, which was already wrong on
a full buffer -- the next message's `%s` printed the previous one's pointer.
They clear it first now, so a missing argument prints nothing.

### Two things this found

`mod_mark` (53 bytes), `mdef` and `ent` were members of BOTH `csp_rt_t` and
`csp_cstate_t`. Only the `cs` copies are read; the `csp_rt_t` ones were dead,
and 57 bytes of RAM on every board. Removing them also orphaned a comment —
`gsx`'s, which had been separated from its field by an earlier move.

`undo[CSP_UNDO_DEPTH]` was a flat 8 everywhere. `csp_repl.c` compiles the whole
feature out under `CSP_EXEC_ONLY`, but `.bss` is not garbage-collected per
field, so uno_bare carried 66 bytes of ring for a command it cannot reach — on
a part with 2048 bytes of RAM.

### On permuting csp_estate_t

`reg[MAX_REGS]` is 64 bytes at offset 0, so it fills `es`'s own displacement
window by itself and every scalar in that struct sits past it. Moving `reg` and
`arg` to the end measured **+22 on both boards** — no help, because `es` itself
starts past byte 63 of `csp_rt_t`, so its scalars were never reachable with a
short displacement anyway.

The underlying rule is still right, and worth applying to whatever does sit at
offset 0: a member reached with a RUNTIME index needs an add to a base register
whatever the layout is, so its constant offset folds into that add for free. A
scalar is reached with `ldd Z+q` and does care. **Arrays last, scalars first**
— but only inside a struct whose own base is in the window.

### Two things that made the generated accessors cost more, not less

Both were found by measuring after the conversion, not before it.

**The setter must not widen.** The first generated setter read

    b_[0] = (uint8_t)((b_[0] & ~(0xFU >> 0)) | (uint8_t)(((uint32_t)v_ & 0xFU) << 0));

The `(uint32_t)` forces 32-bit arithmetic for a four-bit field inside one byte.
Across the 97 write sites that was **+2154 bytes**. The fix is to do the shift in
the value's own type, and to skip the read-modify-write entirely for a byte the
field owns whole — which is the concrete payoff for byte-aligning a field.

**A run of setters needs its pointer hoisted.** `ram_decl_at(st,i)` expands to
`&st->ram_decl[st->rom_nd - i]`, and a write through a pointer stops gcc
reusing it, so it is recomputed for every setter in the run. Measured on four
consecutive writes:

| | bytes |
|---|---|
| `ram_decl_at(st,i)->fld = v` ×4 (bit-fields) | 130 |
| `csp_decl_set_fld(ram_decl_at(st,i), v)` ×4 | 226 |
| same, pointer hoisted into a local | 130 |

A single write costs the same either way; the penalty starts at two. Hoisting
the 23 runs in the tree gave back **1986 bytes**. The runs are wrapped in their
own block, which is also where the reason is written down.

## Totals

| | before | after | |
|---|---|---|---|
| `mega_bare` text+data | 124554 | 117132 | −7422 (−6.0%) |
| `uno_bare` text+data | 39404 | 38128 | −1276 (−3.2%) |
| `mega_bare` bss | 3761 | 3715 | −46 |
| `uno_bare` bss | 1296 | 1184 | −112 |
| `csp_route_run` | 430 | 392 | |
| `setup_routes` | 366 | 322 | |

## Measuring it yourself

    # per-function sizes (needs -flto commented out in Makefile.board)
    avr-nm -S --size-sort boards/mega_bare/build/default/firmware.elf | tail -40

    # what one function actually spends its bytes on
    avr-objdump -d --start-address=0x88d8 --stop-address=0x8a86 firmware.elf

    # which members are hot, and where they sit
    grep -ho "st->[a-z_0-9]*" src/*.c port/csp_avr.c | sort | uniq -c | sort -rn

`utils/check_size.sh` groups the symbols by area. Note that its `core/libc/USB`
row is the script's own label for "not one of the CandySpeak groups" — there is
no USB on these parts.

## What micro-csp is actually worth, measured

The 3-4x density figure was an estimate for a long time. Here is the measurement.

`setup_routes` hand-encoded as micro-csp:

| | bytes |
|---|---|
| bytecode | 75 |
| the four new native leaves it needs | 178 |
| **total** | **253** |
| the C it replaces | 322 |

**21%, not 4x** -- and that is the useful result, because it says which
functions are worth converting and which are not. `setup_routes` is short and
side-effect dense: almost everything it does is a store into a runtime
structure, and a store becomes a leaf, where nothing is saved. The bytecode
itself is 4.3x denser than the C; the leaves eat the win.

The good case is a function whose callees ALREADY EXIST. `cmd_list` is 4316
bytes -- the largest single function in a mega_bare image -- and it calls 43
distinct functions, every one of them already in the binary:

    34 csp_print_blank    17 csp_print_char     5 decl_name_pos
    31 csp_print_rostr    17 csp_decl_ref       4 csp_str_eq
    19 csp_print_uint     13 csp_print_str_at   4 csp_fmt_vtype

Converting it needs no new leaf at all: the leaf table is 43 entries, 86 bytes,
and every call site goes from a 4-byte `call` plus argument setup to a 2-byte
`NATIVE n`. The listing, dump and REPL paths are all this shape, and they are
where the flash is: csp_repl 17728, csp_print 8448, csp_dump, plus csp_compile
37448.

**So the rule is: convert functions that orchestrate, not functions that
mutate.** The first target is the listing path, not the setup path.

### The native convention

One arity does not fit both cases, so there are two opcodes.

`MC_NATIVE n` is one cell in, one cell out. On AVR r24:r25 is where avr-gcc
passes a single 16-bit argument AND where it returns one, so the call costs no
shuffling at all -- and an existing C function of the shape
`f(csp_rt_t* st, uint16_t x)` goes into the leaf table unchanged. Of the 43
functions `cmd_list` calls, **28 are exactly that shape**.

`MC_NATIVEN n` hands the leaf the stack pointer and takes back the new one:

    typedef mc_cell_t* (*mc_leafn_t)(void* ctx, mc_cell_t* sp);

The leaf reads `sp[0]` as the top, `sp[1]` as the next, and returns where the
stack should be left. One register carries both *how many I consumed* and *what
I put back*, with no out-parameters -- and a leaf may push MORE cells than it
popped, which an `(npop, value)` pair cannot express. (That form, from chine,
says the same thing in two memory writes.)

This is why the reference interpreter's stack grows DOWN: a leaf is ordinary C
compiled for both machines, and `sp[0]` must mean the top on each. The VM
checks only that the returned pointer is still inside the array -- nothing else
can know a leaf's arity -- and `tests/mcsp.c` includes a leaf that runs off the
end on purpose, to prove that check runs.

### What cmd_list would cost

4316 bytes of C. 43 callees, all already in the binary: 28 usable as one-cell
leaves unchanged, 6 needing a small stack-form wrapper, the rest nullary or
static. So the new code is about six wrappers plus a 43-entry table -- call it
170 bytes -- against a function that is almost entirely control flow, field
reads and call sequencing, which is what compresses.

At the 2.5x measured on `setup_routes`'s logic that is roughly 1900 bytes
where 4316 stood. **One function pays for the whole interpreter three times
over**, and csp_repl, csp_print and csp_dump are the same shape.

## Getting uno_bare under 32768

Measured one change at a time on `uno_bare`, from 33256 to 32654 (the part has
32768). Every number is the whole image, `-flto` on, nothing else changed.

| change | bytes |
|---|---|
| `setup_dio`: three setup routines that differed only in which values routine ran | **-156** |
| `st_index` hoisted in `setup_buffer` (two exits, one lookup) | **-132** |
| `str_seg_ensure`: one `ram_instr_at` for the memset and both setters | **-114** |
| `csp_new_decl`: same, memset included | **-66** |
| `setup_{timer,analog,digital}_values` through a local `value_t` | **-64** |
| `csp_part_cfg` holds the cfg BIT, not its position (`1u << pos` is a shift loop) | **-38** |
| slot lookup hoisted out of the type switch in `csp_input`/`csp_output` | **-14** |
| `csp_dio_slot` -- no change, it was already one call | -18 |

### Three that measured WORSE

**A local for a six-byte record.** The `value_t` trick above does not
generalise: `csp_view_t` is six bytes and `setup_view_values` writes every
field, so copying in and out cost **+30** against seven read-modify-writes in
place. The local pays only where the record fits in registers.

**A local for a three-setter run in the timer sweep.** `csp_input_timer` writes
`running`, `val` and `fired` -- all in byte 3 -- so the run touches ONE byte and
a four-byte copy in and out is strictly more work: **+20**.

**`NOINLINE` on `csp_csr` and `csp_rt_start`.** Both are called once and gcc
inlines them into `csp_rebuild` despite `-fno-inline-functions-called-once`.
Forcing the call back was **+28**: there was no duplication to recover.

The rule that falls out: a run of setters wants a pointer hoisted ALWAYS, and a
local copy only when the record is four bytes or fewer and the run touches more
than one of them.
