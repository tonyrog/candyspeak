# Board configuration

One header per target. `csp_config.h` includes the one named by `CSP_BOARD`,
then supplies a default for every setting the board did not pick, so a board
file holds only its deltas and a new setting needs a default in exactly one
place.

    make -f Makefile.board BOARD=uno exec    # passes -DCSP_BOARD=csp_board.h

No quotes in the -D: `csp_config.h` stringifies the token itself. Quoting a
path through make -> arduino-cli --build-property -> the compiler is three
chances to lose a character, and this way there are none to lose.

WHAT BELONGS HERE: what is true about the BOARD. Memory, peripherals it has,
how the arena is claimed.

WHAT DOES NOT: compiler flags (-Os, -fno-inline-..., --gc-sections) -- those are
not preprocessor and stay in the Makefile -- and build VARIANTS. CSP_EXEC_ONLY
and CSP_NO_EEPROM say what kind of image you want, not what the board is, so
they stay make targets. The same board builds both.

The exception is a board that cannot build both. `uno_bare` states
`{define, ['CSP_EXEC_ONLY']}` because on 32256 bytes of flash the full build is
not a variant that is merely large -- it is one that does not exist, and never
will on this part. There the exec-only image *is* what the board is, and saying
so in the terms is what makes plain `make` produce the only image the board can
carry.

## Boards with more than one architecture

An RP2350 carries two Cortex-M33 **and** two Hazard3 RISC-V cores on the same
die, and which pair runs is chosen at boot out of OTP — not by the program, and
not at run time. To a build it is a different toolchain end to end, so it is a
variant in the same sense a region map is on the bare-metal side.

A board that has them lists them, first one the default:

    {arch_variants, [{arm,   "arm-none-eabi-nm"},
                     {riscv, "riscv32-unknown-elf-nm"}]},

The string is that variant's `nm`, because the binutils change with the
toolchain — `arm-none-eabi-nm` on a RISC-V elf reports "File format not
recognized", which reads as a broken build rather than as the wrong tool. A
variant that needs no separate binutils can be a bare name.

    make -f Makefile.board BOARD=rp2350_can variants
    make -f Makefile.board BOARD=rp2350_can                     # arm
    make -f Makefile.board BOARD=rp2350_can ARCH_VARIANT=riscv

The variant goes in the profile name **and** the build path, always — never only
one of them. A directory called `rp2350_can` that is secretly the ARM build is
how the wrong image gets flashed, and the bare-metal side already names its
directories after the region map for that reason. `make boards_all` builds every
variant, so one cannot rot unbuilt.

Measured on the CAN build: arm 134312 bytes of flash, riscv 160376 — about 19%
more, and 24% on CandySpeak's own text. That is what RV32IMAC costs against
Thumb-2.

**PIO has nothing to do with this.** Those are eight state machines with a
nine-instruction ISA for clocking bits onto pins; they cannot run C, and they
are not the RISC-V cores.

## Bare-metal AVR

Two boards, the same port (`port/csp_avr.c`), the Arduino core taken out:

* **`uno_bare`** — ATmega328P. Exec-only, because 32256 bytes is not room for
  the compiler. Does not fit yet.
* **`mega_bare`** — ATmega2560. The FULL build, compiler and REPL included, and
  it fits with half the flash to spare. This is the one that can actually be
  run: `boards/mega_bare/` carries a `wokwi.toml` and a `diagram.json`.

    make -f Makefile.board BOARD=uno_bare            # the image
    make -f Makefile.board BOARD=uno_bare upload     # still through arduino-cli

Each is a separate board from its arduino-cli twin rather than a flag on it, for
the same reason a region map is a separate build directory: two images that look
alike and are not interchangeable are how the wrong one gets flashed.

Three things are different from every other bare-metal board here, and the
`avr8` branch of `Makefile.board` is mostly a list of them:

* **No generated link.** avr-gcc supplies the crt, the vector table and the
  linker script; the part has no `{map, ...}` to build regions from, and its
  boot ROM checks no checksum word. So no startup, no sysinit, no `<board>.ld`,
  no `csp_chip` table -- and a `.hex` straight off the elf rather than a
  gap-filled `.bin` that would run to the EEPROM image at 0x810000.
* **No pin mux.** An AVR has no mux register: a pin is a bit in a DDR, and the
  port decodes the Arduino number to a port and bit with three comparisons. A
  `{pin, ...}` or `{irq, ...}` line on one of these boards is refused by
  `make check-boards` rather than quietly generating nothing.
* **The toolchain is arduino-cli's avr-gcc, not the one on PATH.** 7.3.0
  against a distribution's 14.3.0 -- different compilers, different sizes. Build
  the bare image with one and compare it against the `uno` image built with the
  other, and what is being measured is the compiler. `CROSS=` overrides the
  prefix.

A fourth difference is the part, not the toolchain: the ATmega2560's Arduino pin
numbering is not arithmetic (pin 4 is PG5, between pin 3 on PE5 and pin 5 on
PE3) and it has sixteen ADC channels whose sixth mux bit lives in `ADCSRB`. Both
are handled behind `CSP_AVR_MEGA` in the port; see `boards/mega_bare/README.md`.

The `uno_bare` image does not fit yet, and the build says so:

    Program   33750 bytes  103.0%
    ** DOES NOT FIT on atmega328p: Program over 100% **

That line is there because the link will not produce one: avr-ld's script gives
the text region no length, so an image over 32K links quietly.

### What the core costs

Same program, built two ways:

| | flash | | RAM |
|---|---|---|---|
| `uno` exec-only, arduino-cli | 37408 | | 960 static + heap arena |
| `uno_bare` exec-only, avr-gcc | 33750 | **−3658** | 1264 static (512 arena) |
| `mega` full, arduino-cli | 122208 | | 2335 static + heap arena |
| `mega_bare` full, avr-gcc | 116788 | **−5420** | 5557 static (3072 arena) |

The RAM columns are not comparable: the arduino boards claim their pool from the
heap at boot and size it to whatever is left, so their arena is not in the
static figure. The bare boards state a `{code_budget, ...}` and it is `.bss`,
which is why `mega_bare` says 3072 rather than 4096 — at 4096 the stack was left
1611 bytes, under the 2048 csp.h asks for.
