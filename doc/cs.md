# cs

`./cs` is one command for building, loading and testing the applications and
boards in this tree, and for talking to a node that is running. `./cs help`
gives the short list; this is the long one.

## Where things come from

- An application is `apps/<app>/app.terms`, a board
  `boards/<board>/<board>.terms`. cs reads nothing else to know what to do.
- make does the building (`Makefile.board`). cs only picks the target and
  passes on what it needs. A `NAME=VALUE` anywhere on the line goes on to make
  as it is (`WATCH=1`, `MAP=...`).
- A build that is newer than every file it was made from is not built again.
- An upload runs the board's loader directly, on the files make left.
- Every command is printed before it runs, so whatever cs did can be done
  again by hand.
- A board may be named short: `./cs build wintersbone s3` means
  `wintersbone_s3`.

## Commands

| command | does |
|---|---|
| `./cs` | lists the applications and boards, and what is plugged in |
| `./cs info <app\|board> [board]` | the board: part, toolchain, how it is built and flashed |
| `./cs build <app> [board]` | builds the application for the board |
| `./cs upload [<app> [board]]` | loads it, building first if it is out of date; with no app, the one last built |
| `./cs test <app>` | runs the application's tests on the host |
| `./cs ports` | the serial ports and what is on them |
| `./cs node <command>` | talks to a running node through its console (below) |

## node

The node is found on `-D` (a serial port or `HOST:PORT`), otherwise on the
port plugged in with the board's USB id, otherwise on the one in its terms.

| command | does |
|---|---|
| `info [file]` | what the node is: board, part, firmware, number format, RAM, flash slots, images and the transports it has. With `-use`: the transports its program uses (or the program in `file`: `.csp`, `.img`, `.hex`) and those it runs as the null device. With `-detail`: every endpoint. |
| `list` | the program it runs, as source |
| `state [name ...]` | its values. A name picks that value and what is under it (`P[i]`, `T.fired`). |
| `get <name ...>` | values, bare, one per line (`X`, `s1.X`, `Red[2]`) |
| `set <name> <value> ...` | sets values by name: `42`, `0x2a`, `1.5`, `"text"`. All are checked before any is set. On a `#param` or a pin part it is a setting, kept by `save`. |
| `watch [name ...]` | the values as they change, until ^C |
| `pause`, `go`, `reset` | stop, run, back to INIT |
| `step [N]` | runs N cycles (1) while paused |
| `live` | rules frozen, I/O running: a `set` is on the pin at once. `go` runs again. |
| `latch on`, `latch off` | device output on or off |
| `settings` | what is set by hand, saved or not |
| `save`, `load` | patch and settings to storage, and back |
| `patch <file>` | adds declarations, rules, `#disable` and `#enable` to what it runs, all or nothing. Not saved until `save`. A node with no compiler is sent a hex patch compiled here (`-hex` does that for any node). |
| `disable <rule ...>`, `enable <rule ...>` | turns rules off or on, by the numbers `list` shows (`3 5 2-6`) |
| `undo [N]` | takes back the last patch or line, or the last N |
| `send <line>` | sends one console line and prints what came back |

## Options

| option | means |
|---|---|
| `-D <device>` | the port; for `node` also `HOST:PORT` |
| `-b <baud>` | the upload speed where the loader has one; for `node`, the console's (115200) |
| `-n` | prints what would run and runs nothing |
| `-use`, `-detail` | `node info`: the transports the program uses, and every endpoint |
| `-hex` | `node patch`: compile here and send hex even if the node has a compiler |
