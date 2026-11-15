# apps

An application is a program that does a job -- an SMS alarm, a code lock --
kept apart from the boards it runs on. Each has

    main.csp      the program; it imports `board "pins.csp"`
    pins.csp      the host's pins, for its tests
    tests/        X.csp + X.dat + X.expect, run on the host

and `app.terms`, which says which board it is for and how it gets its pins.
Built, loaded and tested with `./cs` at the top of the tree:

    ./cs                          the applications, the boards, what is plugged in
    ./cs wintersbone              build it for its own board
    ./cs wintersbone s3           ...for another (a board may be named short)
    ./cs upload wintersbone s3    load it, built first if it is not up to date
    ./cs upload                   the one last built, again
    ./cs test wintersbone         its tests, on the host
    ./cs info wintersbone s3      the board, the make line, how to flash by hand
    ./cs -n ...                   print what would run, run nothing

`cs` prints every command before it runs it, so anything it does can be done
by hand. `-D /dev/ttyACM1` picks the port when the board's USB id does not,
`-b 460800` the upload speed. NAME=VALUE on the line goes on to
`Makefile.board` unchanged: `WATCH=1` (measure the stack), `MAP=...`.

An upload whose build is newer than everything it was made from (the
program's files, the runtime's sources, the board directory, the same
NAME=VALUEs) runs the loader straight away, without make.

## Where the pins come from

Two arrangements, both in use:

* **The application names its pins per board**: `apps/lock/mega_bare/pins.csp`
  says which pin is K1 and which is Lock on that board. Built with `APP=lock`;
  the output goes to `apps/lock/<board>/build`.
* **The board is the application's own**: `boards/wintersbone` exists for the
  SMS alarm, and its `pins.csp` already uses the application's names. Built
  with `PROG=apps/wintersbone/main.csp`; the output goes to the board's
  `build/`.

`app.terms` says which (`{pins, app}` or `{pins, board}`), the board it is
for unless told otherwise (`{board, ...}`), and for `{pins, board}` the
boards it runs on (`{boards, [...]}`). A new application is its directory
and that file.

## Why not `make -f Makefile.board BOARD=wintersbone`

Without `PROG` or `APP` that builds the board's `pins.csp` ALONE -- a node
with its pins declared and no program -- and says nothing about it. It is
the trap `./cs` is for: it always says which program.
