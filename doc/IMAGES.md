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
