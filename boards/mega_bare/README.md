# mega_bare

An Arduino Mega 2560 with the Arduino core taken out: `port/csp_avr.c` linked by
avr-gcc, the same port `uno_bare` uses, on a part with room for the whole thing
— compiler and REPL included, not exec-only.

    make -f Makefile.board BOARD=mega_bare            # build
    make -f Makefile.board BOARD=mega_bare upload     # arduino-cli, prebuilt hex

## Why this board

`uno_bare` does not fit in 32K and therefore has never run. This one fits with
room to spare and runs in Wokwi, so the port can be exercised at a prompt rather
than only linked. It also measures the core's cost a second time — on a
different part, and against a FULL image rather than an exec-only one.

## Wokwi

`wokwi.toml` and `diagram.json` are here, and **this directory is the project
root** — the extension looks for `wokwi.toml` in the folder VS Code has open, not
anywhere up the tree. So: `code boards/mega_bare` (or File → Open Folder), build,
then **F1 → Wokwi: Start Simulator**. Opening the repository root instead finds
no `wokwi.toml` and the simulator does not start.

The paths in `wokwi.toml` are relative to it, and `build/default/` is right here,
so nothing has to be copied. The names are ours -- Wokwi takes whatever the toml
says and does not care about `.ino.elf`. The diagram is a Mega, an LED with
a 220R resistor on pin 13, and a button to ground on pin 2.

The board carries `pins.csp` in ROM, which is the blink that matches it: the LED
toggles twice a second, and **holding the button freezes it**. The pin has its
pullup on, so an open button reads 1 and a pressed one reads 0 — one LED
answering both "does the timer run" and "is the input being read".

    #digital Led out 13
    #digital Btn in pullup 2

    #timer Blink 500 = 1
    Blink = 1 ? timeout(Blink)
    Led = !Led ? timeout(Blink) && Btn

The timer arms itself again on its own timeout. Without that line it fires once
and stops, which looks exactly like a board that never started.

It is also a REPL — this is the full build — so the prompt is live over the same
serial connection. `/list` prints the program above, and anything typed is
compiled on the part.

The console is USART0 at **38400** (`{console, {uart0, 38400}}` in the terms).
If the serial monitor shows nothing or shows garbage, that is the first thing to
check — the port sets the divisor from `F_CPU` and that baud, and nothing else
negotiates it.

## What differs from the 328P in the port

Two things, both behind `CSP_AVR_MEGA` in `port/csp_avr.c`:

* **The pin map is a table.** On a 328P the Arduino numbering is arithmetic —
  0..7 PORTD, 8..13 PORTB, 14..19 PORTC — and three comparisons answer it. On a
  Mega it is not: pin 4 is PG5 between pin 3 on PE5 and pin 5 on PE3. So one
  byte per pin (port index and bit) plus a table of the eleven port addresses,
  taken from `&PORTx` rather than computed — `PORTA..PORTG` are three bytes
  apart from 0x22 and `PORTH..PORTL` three bytes apart from 0x102, and hand-
  computing across that gap writes a plausible wrong pointer.
* **Sixteen ADC channels.** The sixth mux bit is `MUX5` in `ADCSRB`, not in
  `ADMUX`. Writing only the low bits reads channel `ch-8` — a real reading off
  the wrong pin, which is the hardest kind of wrong to notice.

`A0` is pin 54 here and pin 14 there; the port takes either a pin number or a
channel number, so a program can say whichever it means.
