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
