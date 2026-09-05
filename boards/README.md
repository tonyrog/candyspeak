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
