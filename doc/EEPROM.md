# EEPROM: two stores, two lifetimes

What a board keeps across a power cycle is not one thing. It is two, and they
fail differently:

**The patch** is program text you added at the prompt — declarations, rules, the
`#disable` set. It belongs to *one* firmware. Reflash and it must go: the new
image presumably contains whatever you were patching around, and a stale patch
that survived would shadow the fix you just flashed.

**The settings** are values for things the firmware already declares — a `#param`
trimmed against this motor, a pin moved because this board is wired differently,
a timer period found by experiment. They belong to the *unit*, not the build.
Nothing about them goes back into the source, so a reflash must not take them.

One store cannot serve both. This is the split.

```
  0   eeprom_header_t                     magic, versions, sizes, CRCs
      SETTINGS payload                    name-keyed, own version + CRC
      PATCH payload                       str / decl / instr / disable
```

## Why settings come first

**They are readable when the patch is not.** The settings start at a fixed offset
— immediately after the header — so reaching them never involves parsing anything
of variable length. A patch that is corrupt, half-written, or built by a firmware
that is no longer flashed does not stand between you and the calibration.

It is *not* about wear. The Arduino backend writes through `EEPROM.update()`,
which skips a cell whose byte is already correct, so rewriting the whole store
with mostly unchanged content costs almost nothing. That is also why the
sequential `open/write/close` API is left alone: a seek primitive would have to
be added to three platform backends to buy something the cell-level update
already provides.

## Identity: names, not indices

The patch may address declarations by index. It is fingerprinted against
`rom_header.crc_hdr`, so the program it was made against is the program it is
applied to, and index 7 means the same declaration both times.

A setting has no such guarantee. It is meant to cross a reflash, and a reflash
renumbers everything. So a setting names its target **symbolically** — as
characters, resolved at boot:

```
["Kp"]          PART_VAL      9
["Led"]         PART_PIN      7
["T"]           PART_PERIOD   900
["sys.NodeID"]  PART_VAL      124
```

The path is the same text you would type at the prompt, dots and all, which is
what makes a module member cost nothing extra: `sys.NodeID` is a string here, so
there is no object index to find room for in an 8-byte declaration and no new
declaration type to invent.

## Entry format

```
  uint8_t   plen        path length, characters
  char      path[plen]  "Kp", "Led", "sys.NodeID"
  uint8_t   part        csp_part_t
  uint8_t   vt          vtype_t of the value
  uint8_t   res         declared width in bits
  value_t   val         4 bytes  -- or, when vt == V_STRING:
  uint8_t   slen        string length
  char      s[slen]     the characters
```

7 + `plen` bytes for a scalar. `Kp` costs 9, `sys.NodeID` 18.

The same bytes live in RAM, in `csp_rt_t.settings` — `CSP_SETTINGS_BYTES`, 256 on
a board and 1024 on the host, overridable in `boards/*.h`. A save is then a block
write and a load a block read, and `/settings` needs no second representation to
list. It costs the 256 bytes outright: mega RAM went 1406 → 1671.

**A string value is stored as characters, not as a string index.** `sindex_t` is
a position in a string table that a reflash rebuilds; the position would land
somewhere else, or nowhere. Loading pushes the characters into the RAM string
table and writes the fresh position into the slot.

**Entries are read through a byte copy, never cast in place.** `val` sits at
7 + `plen`, which is odd for half the paths there are, and an unaligned 32-bit
load HardFaults on Cortex-M0. `memcpy` into an aligned local, then use it.

**`res` and `vt` are the shape check.** A firmware that redeclares `Kp` as `:32`
gets a setting that says 16, and the setting is not applied — the declaration's
value stands. This is the boot-time twin of the `ERR_PARAM_SHAPE` check the
parser already makes when a `#param` line redeclares one.

So an entry is in one of three states, and `/settings` prints all three:

```
Kp = 9                                       applied
Kp = 9   // orphan                           the name is gone
Kp = 9   // not applied: width or type moved  the name is there, the shape is not
```

## What may be a setting

| | recorded |
|---|---|
| a `#param` value | yes |
| `pin` `port` `dir` `pullup` `pulldown` `pwm` `endian` `period` | yes |
| `val` — an output poked by hand | no |
| `fired` `rx` `tx` `dlc` `len` `id` | no |

The line is **configuration versus state**. `> Led.pin = 7` says how this board
is wired; `> Led = 1` turns an LED on to see which one it is. Only the first is
worth carrying across a reboot, and keeping the second out is what makes the
prompt safe to experiment at.

**Only an IMMEDIATE records.** A rule writing a config part — `T.period = X ? c`
— is the program doing its job, not someone configuring the unit, and freezing
that into the store would restore on the next boot a value the rule recomputes
one cycle later. The recording hook therefore sits in the immediate branch of
`process_assign`, not in `csp_dio_set_part` where both paths meet.

`#param Kp = 9` records too. It sets a param exactly as `> Kp = 9` does, so it
would be strange for one to survive a reflash and the other not. Only a
RE-declaration reaches that path — the declaration that creates a param goes
through `csp_new_udecl` — so a program's own `#param` lines do not fill the store
with their own defaults.

> `pullup`, `pulldown` and `pwm` are also declaration OPTIONS
> (`#digital B in 2 pullup`), so the tokenizer returns them as keywords, and every
> `.part` path was written against `WORD`. `D.pullup = 1` parsed, wrote nothing
> and read back 0 with no complaint. `csp_scan_line` now retags an option keyword
> that follows a dot as a `WORD`: after a dot it can only be a part name, so one
> rule at the scanner serves the grammar patterns and the expression parser both.
> `endian` is not an option keyword, which is why it always worked and hid the
> shape of it.

**A new `#param` is not a setting.** It is a declaration, so it goes in the
patch, and it dies with the patch on reflash. That is correct rather than
unfortunate: a param the running firmware does not declare has nothing to
configure. New params belong in the source. Parts cover what field work actually
needs — re-pinning, pull-ups, timer periods, view endianness — without adding a
declaration to anything.

## Setting a value

Unchanged from how it already behaves: write it immediately, then save.

```
> Kp = 9
> Led.pin = 7
/save
```

The immediate write reaches the slot at once, as it always has. What is new is
that a write to a param or a config part also **records** the entry, and `/save`
writes the settings payload.

**A value equal to the declaration deletes its entry.** Setting `Kp` back to what
the source says does not store a redundant override. The store would otherwise
fill with no-ops, and — worse — the day the default changes in the source it
would be silently shadowed by an entry that happened to match it once.

A string is compared as **characters**, not by its `sindex_t`. Two identical
names sit at different positions — a declaration name is installed with no
lookup — so comparing positions would make every string setting look different
from the declaration and store even one that restores the shipped name.

## Reading it back

Three views, and they answer different questions:

| | shows |
|---|---|
| `/list` | what the **source** says, with an `S` tag on a line the store overrides |
| `/state` | the **live** value — the number the unit is actually running |
| `/settings` | what is **stored**, and whether each entry took |

The `S` tag matters more than it looks. Without it `/list` prints a pin or a
period that is not in effect and gives no hint to look further:

```
#param Kp:16 integer = 5  // S
#digital Led out 0:13     // S
#digital Btn in 0:2       // F
```

It is set last, after `P`, because a setting is applied last. A REFUSED entry
does not set it — a tag saying "overridden" on a declaration that won would be
worse than no tag.

## Boot order

```
csp_load_rom
  read eeprom header, verify crc_hdr
  verify crc_set                     settings are readable from here
  rom_fp matches?  -> read and verify the patch, install it
csp_rt_start
  build the derived tables
  apply patch param overrides        (declaration-keyed, as today)
  apply SETTINGS                     <-- here
  hardware setup
```

**Settings apply before hardware setup, and that is the whole reason they are not
`#in INIT` rules.** An INIT block runs at the end of the *first cycle*, after
setup has already configured the pin:

```
> Led.pin    13     first line, cycle 0
> Led.pin     7     after the first cycle
```

On a real board that means pin 13 is driven as an output for a cycle before the
configuration moves to pin 7. Applying at this point in `csp_rt_start` — where
the existing param overrides already land — means the wrong pin is never
configured at all.

`csp_settings_apply` sits immediately after `apply_param_overrides` in
`csp_rt_start`, and the platform main calls `csp_setup` only after that returns.

Two details the ordering forces:

**Settings are read before the `rom_fp` check.** They are keyed by name and are
meant to cross a reflash, so they must not be dropped along with the patch that
is not. That also means the load's error path — which a fingerprint mismatch
takes — leaves the store alone, and the `csp_rebuild` it does on the way out
applies it.

**And leaving the store alone is what makes a rollback possible.** A patch that
does not match is not loaded *and not erased*: nothing on that path writes to
the eeprom, so the previous firmware finds its own patch again, intact, tagged
`E`. The cost of that is silence — the boot line reads "No saved state, running
ROM", which is not what happened.

**A `PART_VAL` write goes to the slots directly**, the way
`apply_param_overrides` does, not through `csp_dio_set_part`. A plain integer
slot has no `PART_VAL` row in the layout table, so the part writer would find
`r == 0` and write nothing at all — silently, which is how the first version of
this passed every pin test and applied no param.

A string value is installed with `lookup_string` before `new_string`.
`csp_rt_start` reruns on every rebuild, and a bare `new_string` would push
another copy of the same characters each time — a leak that grows with the number
of edits rather than the number of settings.

## Orphans

A setting whose path resolves to nothing — the name was removed, or renamed, in
the firmware now flashed — is **kept, not applied, and shown**. Kept because the
next firmware may well reintroduce the name and a calibration is expensive to
recreate. Shown because a store that silently accumulates entries nobody can
account for is how you end up mistrusting the whole mechanism.

`/settings` lists the store: path, part, value, and whether it currently
resolves. Removing one is a command to add once there is something to remove.

An eeprom erase (`csp_eeprom_clear`) takes the settings with it. They outlive a
reflash, not a deliberate wipe of the store they live in.

## Versions

The settings payload carries **its own format version**, independent of
`ROM_FORMAT_VERSION` and of `EEPROM_VERSION`.

It has to. Those two describe things that die on reflash, so bumping them is
free; the settings are the one part of the store that a new firmware is required
to still understand. A firmware that changes the settings encoding must either
read the older one or drop it **and say so** — not silently, the way the
`rom_fp` mismatch drops a patch today:

```
eeprom: settings in an unknown format -- ignored
eeprom: settings too large for this build -- ignored
eeprom rejected: CRC mismatch in settings section
```

A rejected payload is still **stepped over** before the patch is read. The
backend is a sequential stream with no seek, so skipping the read would start the
patch at the wrong byte.

## CRC granularity

One CRC over the whole settings payload, not one per entry.

A per-entry CRC costs 2 bytes on a 9-byte record and buys a *partial* apply,
which for a set of related calibrations is worse than applying none of them. The
CRC is written **last**, so a power cut in the middle of a save leaves a payload
that fails its check rather than a truncated record that reads as valid.

The two stores do not share a string area for the same reason they do not share
a lifetime: the patch's string block is discarded on reflash, and it would take
the settings' path names with it. Settings carry their characters inline.
