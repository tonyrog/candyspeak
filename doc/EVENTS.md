# Interrupts

A pin declaration can name an **interrupt trigger**, and then `.fired` is true
for exactly one cycle after it — the same shape `timeout(T)` has.

    #digital Drdy in falling 2:13
    #digital Btn  in pullup rising 2:7

    Sample = Imu ? Drdy.fired

**It is an option, not a declaration of its own.** An interrupt is a property of
how the pin is configured — the same kind of thing `pullup` is, and there is no
`#pullup Drdy` for the same reason. It sits with the other options, before the
pin, and `.fired` sits on the device where it was going to sit anyway.

Backends, all three run on hardware:

| port | mechanism | notes |
|---|---|---|
| LPC17xx | GPIO interrupts, ports 0 and 2 | any pin, either edge, no pin function; all arrive on the EINT3 vector |
| LPC2000 | the four EINTs | a PIN FUNCTION — the board file must mux it; one direction only, `both` is refused |
| STM32F4 | EXTI | the line IS the bit number, so 16 sources at a time; no level trigger |
| host | sampled | compares the pin's level between cycles — what makes a trigger testable under `-F` |

Every other port links the weak defaults in `src/csp_transport.c`, so a program
with a trigger compiles and runs there. The source is simply never armed, and
`/state` marks it with `!` so that is visible rather than silent.

## The surface

    #digital <name> [<option>...] [<port>:]<pin>
    #analog  <name>[:<bits>] [<option>...] [<port>:]<pin>

where an option may be a trigger:

| Trigger | Means |
|---------|-------|
| `rising` | 0 → 1 |
| `falling` | 1 → 0 |
| `both` | either direction |
| `high` | asserted for as long as the pin is high |
| `low` | asserted for as long as the pin is low |
| `ready` | the device's own event — a conversion finished, a frame arrived |

Not every part has all six; the board terms know which, `--irq-of` prints them,
and `csp_board_irq_attach` refuses the rest.

**There was a `#event` declaration first, and it was the wrong shape.** It took
two forms — one attaching to an existing device, one declaring and attaching at
once — a keyword, two patterns, two error codes and a second listing line, all
to say what one option word says. The question that killed it: what would a
`#pullup Drdy` declaration have been for?

**Trigger words are ordinary NAMEs**, not reserved words — the same call the
`.part` names make. `parse_opts` looks a word up and *stops* on one that is not
a trigger, so a name is still a name: `ready` is a `#variable` in
`examples/can_pack.csp`, and `high` and `low` are names anyone would reach for.

**No `event()` function.** `.fired` is already a part — `PART_FIRED`, read today
as `T.fired` on a timer. So the guard needs no new opcode and no new built-in,
only a `fired` bit in the device's value word: the top bit of `dvalue_t` (taken
off `val`, of which only bit 0 was ever read) and the spare one in `avalue_t`.
The function table has a 32-entry wall, and `event(X)` would spend one of those
saying what a part already says.

## What an interrupt buys

`rising(X)` already exists as a function: the software edge on a sampled value.
A trigger is that same edge caught in **hardware, between cycles**, so a pulse
shorter than one cycle is not lost. That is the only difference, and the shared
word says so.

It does not make a response faster than one cycle — the rule still runs in the
next one. Where it does buy latency is out of sleep: an external interrupt is
the only thing that wakes a powered-down part.

The listing carries the trigger with the other options. Dropped from it, a
program copied out of a board comes home with its interrupts gone — the failure
a timer's `= 1` once had.

**The host backend samples**, because a host has no interrupt controller: it
compares the pin's level between cycles. That is what makes a trigger testable
under `-F` — a stimulus row writes `Drdy = 1` and the next input phase reports
the edge — and it is honest about being the software version.

## How it works

### Rules never run in interrupt context

The arena is not re-entrant. An ISR that evaluated a rule would be walking the
same structures the cycle is halfway through. So an ISR does exactly one thing:
OR its bit into a pending word, and return.

`csp_input_event()` is `csp_input_timer()` with the clock taken out. It clears
last cycle's `fired`, reads the pending word once, deals the bits out and
enqueues the sources that fired. Two hooks, declared in `include/csp.h`:

    csp_board_irq_attach(st, ix, trig, slot)   arm it; -1 if the silicon cannot
    csp_board_irq_take(st)                     read AND clear the pending set

`take` must read and clear as one operation. An edge landing between the two
would be dropped, and a dropped edge is the single failure this mechanism exists
to prevent.

### There is no event[] list

A source is an `io[]` entry whose declaration carries an `irq`, and `io[]` is
walked every cycle anyway — so a source's **slot is its position among those
entries**, and `csp_setup_events` and `csp_input_event` agree because both walk
it the same way. One list less to size, allocate and keep in step.

A source the board refuses keeps its slot, with its bit clear in `irq_hw`.
Dropping it would renumber every source after it, and the numbering is the only
thing tying a bit in the pending word to a pin.

### Arming happens after the pins are configured

`csp_setup_events` is called at the end of each port's `csp_setup`. Arming an
interrupt on a pin still at its reset default arms it on whatever the pin
happened to be.

## What a board file says

A board marks the pins it has wired as sources:

    {pin, 'PC13', gpio_in},          %% the pin, as always
    {irq, 'PC13', falling}           %% and it interrupts

    utils/gen_chips.erl --irq-of crazyflie
    utils/gen_chips.erl --irq-of bridgezone
    utils/gen_chips.erl --irq-of lpc2129     %% a chip works too

prints what can interrupt on that part, the channel budget, and — for a board —
which pins it has claimed and which are still free.

This is **not** what a program reads: a program names its own pins and may name
ones no board file mentions. The board file is what `make check-boards` holds to
the silicon, and what the next person reads to find out why `PC13` is wired the
way it is.

### What a channel is

**The thing two pins cannot share.** It is the only fact in a board file that is
wrong *silently*: the second write to `SYSCFG_EXTICR` or `PINSEL` wins, the first
pin goes quiet, and there is nothing to find but a signal nobody answers.

Four schemes, in `chips/*/`:

| scheme | channel | parts |
|---|---|---|
| `pin_function` | the alternate function itself | LPC2000, LPC17xx's four EINTs |
| `per_bit` | the bit number, any port | STM32F4 (EXTI) |
| `port_any` | its own — no sharing | RP2040/RP2350, ESP32-S3, LPC17xx ports 0/2 |
| `runtime` | the Arduino core knows | AVR, SAMD21, RA4M1 |

`make check-boards` refuses a board that claims a pin which cannot interrupt, an
edge the mechanism does not have, two pins on one channel, or a pin it never
muxed — all four of which run and do nothing.

`pin_function` states no pins of its own: the pin table already says which pin
can be `eint2`, so the answer is derived. That is why `--irq-of bridgezone` can
also tell you which of the eight EINT pins are still free.

`runtime` states no pins either, for the opposite reason. The map is real but it
belongs to the Arduino core's variant file, and transcribing eighty rows of it
here would introduce more errors than it caught — the same call
`chips/st/README.md` makes about the F4 pin-capability table. So `{irq, ...}` on
an Arduino board is an error rather than a line that does nothing.

## What is not built

**The Arduino backend** — `attachInterrupt(digitalPinToInterrupt(pin), ...)`,
with the core answering which pins can. Small; the reason it is absent is that
no Arduino board in the tree has an interrupt source to drive it yet.

**`ready`** as anything but a refusal. It is in the grammar because a conversion
finishing and a frame arriving are the same shape as an edge, and naming it now
costs nothing. No backend implements it, so every port refuses it and `/state`
says so with `!`. On a `#buffer` the same fact is already `.rx`, which is why
there is no trigger there.

**A generated `CSP_BOARD_IRQS`.** The board header could carry the table the way
it carries `CSP_BOARD_PINS`, but EXTI needs none (the line is the bit number) and
nothing else has a backend. A table with no reader is a table that goes stale.

## Sharing a guard: `#when`

Several rules on one edge repeat the condition. `#when <condition> ... #end`
writes it once:

    #when Drdy.fired && A < 100
      X = f1
      Y = f2
    #end

**Not `#in`** — `#in <state>+` is the state machine's syntax and reads better
kept that way; a second meaning for the same word makes both harder to read.

It compiles to the condition plus **one `OP_NINSTATE` against zero** — the
opcode `#in` already uses, with a different immediate. `nxt` is patched past the
block at `#end`, so "if reg == 0, jump nxt" is exactly "skip unless the
condition holds". No new opcode, no ROM format change, and the same truthiness
`OP_RULE` tests (nonzero, not `== 1`).

The saving is the condition, not the gate. Measured on four rules:

    ? Drdy.fired               1 instruction per rule
    ? timeout(T)               1 instruction per rule
    ? Drdy.fired && A < 100    5 instructions per rule -- 81 written out, 67 as
                               a block, and evaluated ONCE per cycle instead of
                               four times

So a block earns its keep on a compound condition and barely registers on a
single part. That is worth knowing before reaching for one.

### Blocks nest, on one stack

`#in`, `#when` and `#module` share **one stack**, four deep, so `#end` closes
whichever opened last:

    #when Drdy.fired
      #in Armed
        Shot = 1
      #end
      Seen = Seen + 1
    #end

With a counter per kind it closed the wrong one — `#when X` then `#in Idle`
then `#end` shut the `#when` and left the `#in` open, swallowing every line
after it. Each stack entry also carries the enclosing **state context**, because
`#in` sets it for the rules inside and the block around needs its own back.

`#module` is on the same stack: its mark is the `OP_ENTER` rather than a gate,
which is why `blk_pop` reads `kind` before patching anything.

### Two ways a block used to fail quietly

**Left open at the end of a file.** Everything after the missing `#end` was
swallowed, the file parsed, and the program ran with rules that fire only under
a condition their author meant for two lines. Now:

    prog.csp:5 #when opened on line 3 was never closed

Innermost first, and by the line it **opened** on — where the file ends is where
you notice, the opening is where the mistake is. Not at the prompt, where a
block is legitimately open while its rules are being typed.

**A bare expression inside one.** `println("hi")` at the prompt is a query — run
it, show the answer — and the text alone says so, since there is no `=` and no
`?`. But the same line inside a block is the block's *body*. It used to be
evaluated once, printing at the wrong time and leaving the block empty. The
nesting decides now, not the text.

**An unclosed block ends the cycle.** The skip distance is patched at `#end`,
and at the REPL a block is open for as long as it takes to type the rules —
with the cycle running the whole time. A distance of zero is a jump to the gate
itself, so a false condition used to spin the machine forever. Both gate
opcodes now treat `nxt == 0` as "end of stream"; nothing after an unclosed
block has been written yet.

## Capturing at interrupt time

TODO.md sketched:

    #in ISR
      Buffer[I] = CREG
      I = I + 1
    #end

Read as a block gate, that says "these statements run in the handler", which is
the one thing that must not happen — the arena is not re-entrant. But the
*intent* is real and neither `#when` nor a trigger reaches it: grab something
before the cycle can get to it. That wants a fixed action the ISR performs —
latch a register into a buffer — not a rule. A later thing, if a board ever
needs it.
