# Images: several programs in one firmware

An *image* is one baked CandySpeak program: string table, declarations,
instructions, the reactive graph, the state table, and a header describing them.
A firmware used to link exactly one, called `rom_*`. The goal is several — a
program and a FAILSAFE, an A and a B, or three copies of the same FAILSAFE on
three flash pages — findable, tellable apart, and each verifiable on its own.

Format v8 is built. What is left is finding images by scanning flash, and
placing them on pages a tool can reflash independently.

## The layout

One contiguous C object per image. The generator knows every count, so it stamps
them into a macro and lets the **compiler** do the layout — which is what keeps
the byte-CRC honest (serialising by hand would reopen the endianness question the
`CSP_STATIC_ASSERT` at `csp_crc16` closes).

```
  0   csp_image_header_t                        64 bytes, PACKED
 64   csp_sect_t + str[n_str+3]     + pad       tag 1
      csp_sect_t + decl[n_decl+1]              tag 2
      csp_sect_t + instr[n_instr+1]            tag 3
      csp_sect_t + idg[..] + pad               tag 4
      csp_sect_t + ofs[..] + pad               tag 5
      csp_sect_t + edg[..] + pad               tag 6
      csp_sect_t + states[n_state+2] + pad     tag 7
```

```c
CSP_IMAGE_TYPE(failsafe_image_t, 76,12,39,1,1,1,5);
CSP_IMAGE_CHECK(failsafe_image_t, 72,156,260,424,436,448,460,472);
static const failsafe_image_t failsafe_image_data RODATA = { ... };
const csp_image_ref_t failsafe_image RODATA =
    { (const uint8_t*)&failsafe_image_data };
```

**Sections are reached by offset, not by pointer.** Offsets survive the image
being copied to another flash page or into RAM; pointers do not, and pointers
cannot be checked against anything.

**`crc_hdr` is last and covers every byte above it** — magic, size, role,
generation, the counts, the section CRCs *and the offsets*. An offset that rotted
would otherwise send the loader to a garbage address with nothing objecting,
which is the wrong failure mode for the part of the system whose job is to notice
failure.

**The generator computes the offsets itself.** It has to: `crc_hdr` covers them,
and a CRC cannot be taken over values only the C compiler knows. `CSP_IMAGE_CHECK`
then makes the compiler confirm every one with a static assert, so the *build*
fails if the two ever diverge.

**Alignment.** Element types are 8, 4 and 2 bytes, so a 4-aligned section start
serves all of them — and `index_t` genuinely needs 2 (an unaligned 16-bit load
faults on Cortex-M0). `CSP_PAD4` pads each section, and `aligned(4)` on the type
is required because the header is PACKED: without it the struct's own alignment
would come from `index_t` and every section could land on a 2-boundary.

**The runtime never names an image's struct type.** Each image has its own (the
counts differ). `csp_load_image(st, base)` takes the first byte and works in
offsets; `csp_image_ref_t` is the one-pointer handle that lets `csp_load_rom`
find the linked one.

## Two ways in, either sufficient alone

**The header is the index.** `ofs_*` gives O(1) access to any section. This is
the fast path and the only one the loader uses when the header verifies.

**The prologues are a cursor.** Each section is preceded by
`csp_sect_t { tag, len }` with `len` in BYTES. Start at `sizeof(header)`, read a
prologue, skip `len`, read the next. That walks the whole image with the header
ignored entirely — and because `len` is bytes rather than entries, a later reader
can skip a section whose tag it does not know. With fixed header fields alone,
every added section would break the walk.

There is no CRC in the prologue: the header carries one per section and the
sections carry their own end markers. A third copy would only give three things
to disagree.

## Recovery, measured

Every section still ends with a self-verifying marker — `DECL_END_MARK`,
`OP_END_MARK`, a `0xFF` sentinel in the string table, `snum 0x7f` in the state
table — whose CRC covers the section data plus the marker with its own crc field
zeroed. Scanning for it recovers the entry count (its position) *and* confirms
integrity, independently of the header.

v8 makes that strictly better: the positions are recovered too, by walking the
prologues, instead of coming from linker-supplied pointers.

Three cases, all measured on a baked `examples/traffic.csp`:

| damage | result |
|---|---|
| `crc_hdr` flipped | `ROM header CRC bad -- sections verified by walk`, program runs |
| `crc_hdr` flipped **and** offsets clobbered | same — the offsets are never consulted |
| one instruction altered, header intact | `ROM rejected: CRC mismatch in instr section` |

The last one is the point: data corruption is *not* recoverable, because the
marker folds the same bad bytes. Only a rotten index is survivable.

## Step 3 — finding images (not built)

The header already carries what this needs, so no format change is required:

| field | why |
|---|---|
| `magic` = `JAM\n` | so a scan can recognise an image it was never told about |
| `size` | total length: skip to the next, and copy the image as a unit |
| `role` | `CSP_ROLE_ROM`, `CSP_ROLE_FAILSAFE` — what the image is *for* |
| `generation` | higher is newer; orders A against B |

**Boot policy.** Scan flash, stepping by the smallest erase granularity we commit
to, testing for the magic and then `crc_hdr`. Per role, take the highest
generation whose sections verify. Both wanted behaviours fall out of that one
rule rather than being special cases:

- **Redundant FAILSAFE** — three copies on three pages: same role, same
  generation, take whichever verifies. Two may rot.
- **A/B rules** — same role, different generation: take the newer one that
  verifies, fall back to the older when it does not.

Cost is trivial: 256 kB at a 256-byte step is 1024 probes of four bytes.

### Wrinkles, measured not guessed

- **`ro_byte` is `pgm_read_byte`, which is 16-bit.** On mega2560 (256 kB flash) a
  flash-wide scan needs `pgm_read_byte_far` and RAMPZ. Either a far variant for
  the scanner only, or bound the scan to the low 64 kB.
- **The scan needs the flash address range**, which is board knowledge — a linker
  symbol or a per-board constant, not something the runtime can derive.
- **Flash is not uniformly scannable.** On SAMD and RP2040 it is memory-mapped
  and a plain pointer walk works. On AVR it is a separate address space.

Where scanning is impractical, a linked-in table of image bases gives the same
result; the loader is identical either way, only discovery differs.

## Step 4 — flash pages (not built)

Put each image in its own linker section, aligned to the *erase* granularity —
that is the one that matters, not the write granularity. AVR mega erases 256-byte
pages, SAMD21 writes 64-byte rows but erases in 4 kB blocks, RP2040 erases 4 kB
sectors. So 4 kB is the alignment that lets every current target replace one image
without touching another.

A custom section name is an **orphan** to the stock Arduino linker scripts. GNU ld
does place orphans — a read-only one lands near `.rodata`, so nothing is dropped —
but the address is not yours to choose, which is exactly what independent
reflashing needs. Two low-effort routes:

- **AVR**: `-Wl,--section-start=.failsafe=0x3E000` via `compiler.c.elf.extra_flags`.
  No script edit.
- **ARM**: a linker-script fragment with `INSERT AFTER .text;`, added with
  `-Wl,-T`. The stock script stays untouched.

Keep the name dot-prefixed and read-only-looking (`.failsafe`, not `failsafe`) so
ld's orphan heuristics group it with rodata rather than a writable segment.

**And a trap already sprung once:** Arduino builds with `-ffunction-sections
-fdata-sections -Wl,--gc-sections`. An image referenced only through a table the
linker cannot see gets collected — the same mechanism that ate the weak `rom_*`
symbols. It needs `KEEP()` in the script or `__attribute__((used))`.

Finally, the tooling caveat: `bossac` and most `avrdude` modes erase the whole
chip before writing. Selective reflashing needs our own bootloader or a tool that
can erase per sector. Laying the image out correctly now costs nothing and is what
makes such a tool possible later.

## Choosing which image runs — `sys.Image` and `sys.Boot`

Two fields, deliberately separate, because one number cannot both report and
request:

| field | kind | says |
|---|---|---|
| `sys.Image` | `#variable` | STATUS — which image `csp_load_rom` actually booted. Written by the loader, so a stored value must not be able to contradict it. |
| `sys.Boot` | `#param` | REQUEST — which one to boot next time. `255` (`CSP_BOOT_AUTO`) means no preference. |

```
> sys.Boot = 0          # ask for image 0
/save
                        # restart
> sys.Image             # 0
```

**Why 255 and not 0 for "no preference".** 0 is a real image number — the first
one `/images` prints.

**A candidate that fails to load is not the end of it.** Ranking can only see
the HEADER — `csp_find_image_no` checks `crc_hdr` and nothing else, because the
section CRCs are `csp_load_image`'s business. So an image can win the ranking
and then turn out to have a damaged section, at which point the choice has
already been made. `csp_load_image` returns 0 or -1 for that reason: a refusal
takes that candidate out of the running and the next best is ranked from what is
left. It touches nothing in `st` until it has committed, so going round again is
safe.

That is the case A/B exists to survive: a half-written slot must cost you the
slot, not the node. With a linked image it barely matters — it is part of the
firmware. With a slot it is the whole point.

```
ROM rejected: CRC mismatch in instr section (corrupt flash image)
> sys.Image
0                       <- the healthy older image, not an empty node
```

**They are LOOSELY COUPLED, and that is the point.** A request for an image that
is missing, or whose header does not verify, is not an error and not fatal: the
node comes up on the automatic choice (highest generation) and the two fields
then disagree. `sys.Boot = 7`, `sys.Image = 1` is the diagnosis, delivered by a
node that is running rather than by one that is not.

**Where the preference lives.** `sys.Boot` is a `#param`, so it is stored as a
SETTING — name-keyed, and therefore surviving both a dropped patch and a
reflash, which is what a boot preference has to do.

**Why it is read where it is.** The choice has to be made before an image is
loaded, and the normal settings path (`csp_eeprom_load`, then
`csp_settings_apply` in `csp_rt_start`) is far too late. So the boot path calls
`csp_eeprom_peek`, which reads the settings section ALONE. That is cheap and
safe only because of where those bytes sit — at a fixed offset immediately
behind the header, reachable without parsing anything of variable length. The
property was designed in for exactly this kind of question.

`csp_eeprom_load` then runs `csp_rt_init` a second time, which resets the
struct, so `boot_want` is carried across it the same way `reactive` is. Without
that the choice is stored and read back correctly and then silently replaced by
the automatic one.

**Nothing sets `sys.Boot` automatically.** `/upgrade` could set it to the slot it
just wrote, and it is a couple of lines — but keeping them loosely coupled is
what lets you flash a slot without committing to it, and commit to it later
without reflashing.

## Getting an image onto the part — `/upgrade`

Built. One region, hex on lines, `.` to finish. It runs over the same UART the
prompt uses, with execution stopped, on a part whose storage is being rewritten
underneath it — which is what every decision below is about.

```
> /upgrade A
OK 65536 bytes, hex lines then '.' ('!' aborts)
> 4A414D0A2405000010000000000000000E000C010000FFFF71F7BB8300004000
OK
  ...
> .
OK 1316 crc 56011
```

**Hex, not binary.** The line reader is what receives this, and a binary stream
cannot be told apart from a stuck link or a half-typed command. Hex costs twice
the bytes and buys a stream that is still text: an interrupted transfer leaves a
prompt, not a parser in an unknown state. The routing happens *before* the
comment test and before the `/` test, so a hex line beginning `2f2f` is data and
not a comment.

**Block by block, not one buffered image.** An image can be larger than the
arena, and the arena is the program's.

**The first block is written LAST.** The header is at byte 0 and its CRC covers
only itself, so a transfer that dies after the first block leaves a header that
reads as perfectly valid describing bytes that never arrived — `/images` said
`ROM gen=0 size=1316` about a slot holding 160. Holding block 0 back until the
terminator makes the slot self-describing with no extra reads and no CRC walk: a
torn transfer leaves `0xff` where the magic goes, which is what an erased slot
says, which is the truth. It costs one block of RAM (512 bytes) and it is why
nothing else has to ask "did all of it arrive".

**And the size is checked against the count.** Holding the header back catches a
transfer that *stopped*; it does not catch one that ended tidily on a file that
was already short. `.` after 160 bytes of a 1316-byte image is a complete
transfer of an incomplete image and looks like success from every angle except
this one: the header states the size, the receiver counted the bytes, and if
they disagree nothing is stamped.

**Refusals come before the erase**, never after a transfer:

| answer | means |
|---|---|
| `ERR protected` | `csp_flash_writable` said no — the runtime, or the last failsafe |
| `ERR no such region` | not in this part's map |
| `ERR unsaved -- N RAM decls...` | prompt work that never reached the EEPROM; `/save`, or add `force` |
| `ERR full` | more bytes than the region holds, caught while receiving |
| `ERR hex` | a bad pair; the rest of the transfer is consumed quietly and `.` answers `ERR data` |
| `ERR incomplete` | the size check above; the slot is left erased |
| `ERR aborted` | `!`; the slot is left erased, and it says so |

**Unsaved work stops it.** `/upgrade` stops execution and the board is
power-cycled afterwards, so anything typed at the prompt and not `/save`d is
gone — and it would be gone three minutes into a transfer, which is not when to
find out. `force` as a second word is the whole confirmation; it is the operator
saying the RAM program is expendable, which is a thing only they know.

`/images` answers two different questions, and the second one is what an upgrade
changes: the registry lists what the *firmware linked*, then a flash scan lists
each APP region as an image, `erased`, `not an image` or `header CRC BAD`.

### Doing all of it without a board

```
tools/csp-image prog.csp                     # tmp/prog.rom.c, .img and .hex
head -c 38912 /dev/zero | tr '\000' '\377' > tmp/flash.bin
{ echo /upgrade A; tools/csp-image -p prog.csp; echo .; echo /images; } |
    ./csp -i --flash=tmp/flash.bin --part=ab
```

`tools/csp-image` is the one way to a flashable program: `-o` names the output,
`-r failsafe` and `-g N` set the role and generation the header carries, `-p`
writes the hex to stdout instead of to disk. It is a shell script rather than
make targets because there are no dependencies here worth expressing — one
compile, one link, one dump, always in that order — and what there IS is
options, which is how a Makefile becomes unreadable. `NAME.hex` is hex lines for
`/upgrade`, **not** Intel HEX; a board's `firmware.hex` is that.

`--flash=FILE` backs the simulated flash with a file and `--part=NAME` picks one
of three host layouts (`ab`, `apps`, `full`) — the same sectors spent three
ways, because the bugs worth finding are the ones that show up in one
arrangement and not another.

Underneath, `tools/mkimage.c` links the *generated* image and dumps what the
compiler laid out — byte for byte what the target reads back, alignment and padding included.
Deliberately not a second emitter: two emitters is how a header and a section
CRC come to disagree. Default output is hex lines, because that is the format
the receiver takes.

The whole path is in `tests/repl.sh` under "firmware upgrade mode", and the
cases that matter there are the failures: a short image, a refused region, bad
hex, an overflow, and a `runtime` region proved untouched **on the file** — a
guard that prints and erases anyway passes every test that only reads stdout.

### What is not built

Nothing boots from a slot yet: `csp_load_rom` still loads the image the firmware
linked, so flashing slot A stores a program that nothing runs.

The EEPROM patch is already ready for it. A patch addresses ROM declarations by
INDEX, so against another program those indices mean something else — and the
eeprom header carries `rom_fp`, the linked image's `crc_hdr`, which moves on any
change to the program at all. On a mismatch the patch is **not loaded and not
erased**: it stays exactly where it was, so going back to the previous program
finds it again. That is the rollback. Settings are read *before* that check,
because they are keyed by name and are the unit's own word on a value — a
`#param` and a `#Sys` param both survive a program change and are in effect
under the new one. See doc/EEPROM.md, and "patch across a program change" in
`tests/repl.sh`, which states the whole of it in six cases.

What it does not do is SAY so. The boot path's other answer is "No saved state,
running ROM", which is not what happened — the save is there and it is intact.
