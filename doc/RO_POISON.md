# The read-only poison

`make ro_poison`

The host has **one** address space. An AVR has two, and `RODATA` there is
`PROGMEM` — flash. A read that forgets its `ro_` accessor is therefore *correct*
on the host and reads the data space at a flash address on AVR: not the table,
and on a part with more flash than RAM, not anything at all.

Four tables shipped that way (2026-09-09). One of them was the **error table**,
so every error on a mega printed `internal error` whatever had actually gone
wrong — the machine that says what broke, broken, lying with a plausible answer.
It cost an hour of chasing the wrong thing.

This gives the host the second address space it lacks, so the same read faults
here.

## How it works

Three pieces, and no annotation anywhere:

**1. The linker gathers the core's constants.** `utils/ro_ld.sh` emits a
fragment naming the core object files:

    SECTIONS
    {
      csp_ro : ALIGN(4096)
      {
        obj/csp_rt.o(.rodata .rodata.*)
        ...
        . = ALIGN(4096);
      }
    }
    INSERT AFTER .rodata

By **object file**, not by annotation — which is the whole reason this is a
linker script and not a macro. A table that forgot its `RODATA` marker is
covered too, and there is no eighth build list to keep in step.

`INSERT AFTER` is an *addition* to the default script rather than a replacement,
so nothing else about the layout has to be restated. The trailing `ALIGN` pads
the section out to a whole page, because `mprotect` works in pages and would
otherwise take the next object's data with it.

`__start_csp_ro` and `__stop_csp_ro` are **defined in the script**, not
inherited. ld generates those automatically only for *orphan* sections — ones it
placed itself because nothing named them. A section created by an explicit
`SECTIONS` block is not an orphan and gets none, and the link then fails with
`undefined reference to __start_csp_ro` from the one file that needs them.
`__stop_` goes after the padding `ALIGN`, so the range handed to `mprotect` is a
whole number of pages.

**The list is exactly what a board links** — `Makefile.board`'s CORE, plus the
neutral image. Nothing else. `port/csp_devices.c` and `port/csp_flash_host.c`
were in it at first and they are host-only; the poison then fired inside
`csp_sector_offset`, walking a device table no target ever compiles. That reads
as a finding and is not one.

The rule is worth stating plainly: **poison what a board would carry.** A file
that only ever runs on the host has one address space and cannot be wrong about
which one it is in. `port/csp_linux.c` and `port/csp_dump.c` are out for the
same reason — though they still have to *read* poisoned data belonging to the
core, which is what `ro_maybe_ptr` is for.

**2. The runtime moves the bytes.** `csp_ro_init()` in `port/csp_linux.c`, first
thing in `main`:

    shadow = mmap(...); memcpy(shadow, section);   the copy that EXISTS
    mprotect(section, PROT_NONE);                  the place that does NOT
    csp_ro_delta = shadow - section;               what ro_* adds

**3. The accessors add the delta.** `ro_byte`, `ro_word`, `ro_ptr`, `ro_decl`,
`ro_instr`, … in `include/csp.h`. A plain `err_tab[i]` does not, and takes
SIGSEGV.

## It names the read

A poison that only says `Segmentation fault` is barely worth having: the whole
point is to name the read, and a bare core file sends you for a debugger to
learn something the process already knew. So `csp_ro_init` installs a SIGSEGV
handler that recognises its own section:

    *** RODATA read without an ro_ accessor: 0x64e5d31441a0 (csp_ro+0x1a0)
    *** On AVR this reads the data space at a flash address.
    cspp(csp_print_str+0x39)
    cspp(csp_print_rostr+0x1c)
    cspp(csp_line_prompt+0x2b)
    cspp(main+0x19cd)

A fault anywhere else is re-raised with the handler removed, so a real bug still
dies the way it should rather than being swallowed by the tool meant to find
bugs. It exits 90, which is distinct from a plain crash.

**The message goes out with `write(2)` BEFORE the backtrace, and the order is not
style.** `fprintf` takes a lock and `backtrace()` calls into the dynamic linker,
which takes another; neither is async-signal-safe. If the faulting read happened
while libc already held one of those, the handler **deadlocks** — the process
then sits there until something kills it, and every byte of buffered stdout dies
with it.

That is not a theory. It is what one test looked like: `got` empty, `want` five
lines, and no sign anywhere that a fault had even happened. The tool swallowed
its own finding, and it cost four wrong hypotheses before the handler itself
became the suspect. The finding underneath was real and reproduced six times out
of six once the message came first.

So the address goes out unbuffered before anything that can block, and the
backtrace is best effort after it: a hang there costs the stack, not the
finding. Link with `-rdynamic` for names rather than bare offsets.

The first thing it found was the host's own `csp_print_rostr`, which cast a
rostring to `const char*` and let `csp_print_str` walk it. That worked only
because the host has one address space; `port/csp_avr.c` has always had to read
those bytes one at a time. Both ports read a rostring the same way now.

## Two things that are not obvious

**The delta is computed at run time, not by the linker.** The tempting version
is an LMA/VMA split — the section linked for one address and loaded at another,
which is exactly how `.data` works on bare metal. It does nothing in a hosted
process: Linux loads at `p_vaddr` and ignores `p_paddr` entirely. Computing the
delta at startup also survives ASLR, which a link-time constant would not.

**The translation is range-checked**, and it has to be. `ro_memcmp` is called
with the RAM side first (`ro_memcmp(s->ptr, s_low, 3)`) while other accessors
take the poisoned side first. Translating a pointer that is not in the section
would corrupt the RAM side, so the test is on the **address**, not on the
argument position: outside the section, a pointer passes through unchanged. One
compare per read, in a build whose only job is to find bugs.

## What it catches, and what it does not

Catches: any **runtime-indexed** read of the core's constants on a path the
tests execute — including a pointer that changes address space at run time,
which is what `csp_seg_slot` does and what no source check can see.

Does not catch:

* **Compile-time-constant reads.** gcc folds `tab[2]` on `static const` data
  without touching memory. The real bugs are all runtime-indexed
  (`err_tab[err]`, `tag_tab[type]`), so this costs little in practice.
* **Paths the tests never run.** For that, `make ro_check` reads the source
  instead and needs nothing to execute.
* **Constants outside the core objects.**

## What the two nets actually see

They are not each other's backup. Two findings from the sweep make the
difference concrete:

`csp_opcode_name` reads its table **correctly** —

    const rochar* csp_opcode_name(opcode_t op)
    {
        return (rochar*) ro_ptr(&op_info[op].name);
    }

— so `ro_check` says nothing, and rightly. But it hands back a pointer *into*
the other address space, and `snprintf("%s", ...)` then reads it as ordinary
memory. Only the poison sees that.

`csp_num_builtin_funcs` is the opposite: a **scalar** in RODATA, read as a plain
variable. `ro_check` looks for `name[` and cannot see it — a scalar read is
textually indistinguishable from any other variable. Only the poison sees that
either. (It turned out not to be in RODATA at all, which is the other half of
the finding — see below.)

| | sees | does not see |
|---|---|---|
| `ro_check` | RODATA **tables** read without an accessor | scalars; pointers that leave a table |
| `ro_poison` | anything that **runs**: table, scalar or pointer | code no test runs; constant-folded reads |

## Status

Run it and it dies, and that is the point of the exercise so far. What the
sweeps have said:

    repl        unit    what the next fault was
    30 / 220    0 / 78   the host's csp_print_rostr, in the prompt
    114         0        csp_boot_pick's boot_path
    120         0        csp_fmt_* and tok_table in the dump
    123         70       DNAME  -- the "deferred" copy-out
    123         71       csp_str_at, same
    142         71       csp_print_just("")
    189         71       csp_opcode_name to snprintf
    219         78       csp_num_builtin_funcs
    220 / 220   78 / 78  exprbuf_str(bp, "..")

**The suite is clean under the poison.** Ten fixes. **Three of them were not wrong reads at all** — they were const data
sitting in RAM on the target:

* `boot_path` — `static const char[] = "sys.Boot"` with no `RODATA`: eight bytes
  of RAM on a part that has two kilobytes.
* `csp_num_builtin_funcs` — a compile-time constant, `const uint8_t` with no
  `RODATA`, one byte of RAM on every target since it was written.
* `csp_print_just("", ...)` — a string literal passed only to be measured, where
  `NULL` meaning "pad" was the answer all along.
* `exprbuf_str(bp, "..")` and its two siblings `<-` and `==` — two-character
  operators walked as strings. Two `exprbuf_char` calls need no string at all,
  and with the last caller gone `exprbuf_str` went with them.

The reads were correct. Nothing on the host can see any of it. It fell out of
forcing the two address spaces apart, and it is the half of the tool that was
not designed in.

`make ro_poison` stays a target of its own rather than part of `make test`: it
does two clean rebuilds, which is minutes rather than seconds. Run it when the
core's constants or their accessors move.

## ro_maybe_ptr

Some places must hand a plain pointer to something else rather than walk it a
byte at a time — `fprintf("%s", ...)`, a libc string function — and
reimplementing printf to avoid it would be worse than the problem.

    #define ro_maybe_ptr(p) ((const char*)csp_ro_real(p))

It hands back an address the ordinary machinery can read: the shadow under the
poison, the pointer itself otherwise. Safe on **any** pointer, because the
translation is range-checked — one that is not in the section comes back
unchanged. That is what makes it usable where the argument is sometimes RODATA
and sometimes RAM, which is most of `port/csp_dump.c`.

It is deliberately **undefined on a board**: there is no shadow there, and a
flash pointer cannot be made readable by arithmetic. A use that reaches a target
is a compile error. On AVR the answer is `ro_byte` in a loop.

This is what `csp_str_at`'s note in `csp.h` has called "a copy-out API —
deferred" for a long time. It turned out not to need a copy: the translation is
an address, so there is no buffer, no lifetime and no length limit.

## The other two nets

| check | when | finds |
|---|---|---|
| `make ro_check` | source, in `make test` | a RODATA **table** read without an accessor |
| `make ro_poison` | run time, on demand | a **pointer** into RODATA read without one |
| `make width_check` | avr-gcc, in `make test` | a 32-bit value truncated by a 16-bit `int` |

`ro_check` and `ro_poison` are complements, not alternatives: the first sees
code no test runs, the second sees pointers no source scan can follow.
