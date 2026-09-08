# A remote REPL over UDP, on one laptop

Two windows:

    ./csp -i examples/relay/node.csp        # the node being driven
    ./csp -i examples/relay/master.csp      # type here

In the master window, `/latch off` in both, then **Ctrl-]**. The prompt answers
`[remote]` and every keystroke goes to the node instead. Ctrl-] again gives the
prompt back (`[local]`).

Swap the two `udp` buffers in `node.csp` for `can` and nothing else changes --
`repl` names the INTERPRETER, not a serial port, so a board with no UART runs
the same six rules.

## What it does and does not do

It relays, both ways: keystrokes reach the node's interpreter and what the node
prints comes back to this screen. It is a demo of the transports, not a finished
console.

**It mangles bursts.** A whole-buffer assignment carries 32 bits, so the relay
moves four bytes per cycle -- at the host's ~9 Hz that is about 36 bytes a
second. Typing is fine; a `/list` is not. The ring does not overflow (`/state`
would say `console lost N`), so what goes missing under a burst is in the
RULES, not the transport: `.dlc`, the bytes and `.tx` are three separate rules
on one buffer and each reads its source from the committed half.

Raising the rate wants the two transports wired to each other without a rule in
between -- a route as a runtime object. See TODO.md, "Konsol-routing".

## The trap this file already stepped in

Four buffers, not two. `in` and `out` on ONE buffer share one heap block, so a
buffer cannot be both the key source and the screen sink: the display bytes land
on top of the keys and get sent straight back out as input. The first version
did exactly that, and the far end received

    sys.sys.Id =Id = 42
