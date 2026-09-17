# Load codes

A node prints a line when it REFUSES an image -- a bad CRC, a settings section
in a format it does not know, a patch built against another ROM version. On a
full build that line is a sentence. On a board with `{load_diag, false}` in its
terms it is the message's number instead, because fifteen sentences were 718
bytes on `uno_bare` and the part has 32256.

What does NOT change: the line is still printed, at the same point, with the
same numbers after it. `L1 3` is `L1` followed by the ROM format it found,
exactly where a full build says `eeprom rejected: patch is ROM format 3`.

| code | message |
|---|---|
| L1 | `eeprom rejected: patch is ROM format ` |
| L2 | `, firmware is ` |
| L3 | ` -- clear it and re-enter` |
| L4 | `eeprom: settings in an unknown format -- ignored` |
| L5 | `eeprom: settings too large for this build -- ignored` |
| L6 | `eeprom rejected: CRC mismatch in settings section` |
| L7 | `eeprom rejected: CRC mismatch in ` |
| L8 | ` section` |
| L9 | `eeprom: disable set dropped (saved for ` |
| L10 | `eeprom rejected: CRC mismatch in disable set` |
| L11 | `ROM rejected: format ` |
| L12 | `ROM header CRC bad -- sections verified by walk` |
| L13 | `ROM rejected: CRC mismatch in ` |
| L14 | ` section (corrupt flash image)` |
| L15 | `ROM graph corrupt -- running sequential` |

Some messages are halves of one line: L1/L2/L3 print around two numbers, and
L7/L8 and L13/L14 print around a section name. A refused EEPROM patch therefore
reads `L1 3L2 4L3` where a full build reads the sentence -- one line, the same
two numbers, and which check failed is the code.

The switch is `LOADTXT(n, "...")` in the source and `CSP_LOAD_DIAG` in csp.h.
A board turns it off with `{load_diag, false}`; every other board keeps the
text, which is why this file has to stay in step with the source. The numbers
are permanent: renumbering one silently changes what a fielded board means.
