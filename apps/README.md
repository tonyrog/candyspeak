# apps

An application is a program that does a job -- an SMS alarm, a code lock --
kept apart from the boards it runs on. Each has

    main.csp      the program; it imports `board "pins.csp"`
    pins.csp      the host's pins, for its tests
    tests/        X.csp + X.dat + X.expect, run on the host

and is built for a board by `apps/Makefile`:

    make -C apps                          the applications and their boards
    make -C apps wintersbone              build it for its own board
    make -C apps wintersbone BOARD=wintersbone_s3
    make -C apps wintersbone-upload       build, then load it
    make -C apps wintersbone-test         its tests, on the host

Anything else on the line goes on to `Makefile.board` unchanged:
`WATCH=1` (measure the stack), `MAP=...`, `DEV=/dev/ttyACM1`,
`EXTRA="-DCSP_STACK_RESERVE=4096"`.

## Where the pins come from

Two arrangements, both in use:

* **The application names its pins per board**: `apps/lock/mega_bare/pins.csp`
  says which pin is K1 and which is Lock on that board. Built with `APP=lock`;
  the output goes to `apps/lock/<board>/build`.
* **The board is the application's own**: `boards/wintersbone` exists for the
  SMS alarm, and its `pins.csp` already uses the application's names. Built
  with `PROG=apps/wintersbone/main.csp`; the output goes to the board's
  `build/`.

`apps/Makefile` knows which one each application uses (`<app>_HOW`) and which
board it is for unless told otherwise (`<app>_BOARD`). A new application is
those two lines and its name in `APPS`.

## Why not `make -f Makefile.board BOARD=wintersbone`

Without `PROG` or `APP` that builds the board's `pins.csp` ALONE -- a node
with its pins declared and no program -- and says nothing about it. It is
the trap the Makefile here is for.
