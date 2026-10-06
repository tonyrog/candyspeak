# wintersbone

An SMS alarm node on an Arduino MKR NB 1500. It measures the temperature inside
and outside, notices when the mains adapter goes and the node is running on its
battery, switches four 230 V relays, and optionally watches the pressure in a
water line. Alarms go out, and commands come in, by SMS.

    ./csp boards/wintersbone/main.csp                  run it on the host
    escript tests/run_tests.escript boards/wintersbone/tests
    make -f Makefile.board BOARD=wintersbone           build for the board

| File | What it is |
|---|---|
| `pins.csp` | the hardware: relays, battery, pressure, the charger and two DS18B20 |
| `main.csp` | the logic: measurements, four alarms, relays, the status report |
| `tests/` | power fail, power back, a glitch under the delay, frost, a relay |

## Hardware

| What | Part | How it is read |
|---|---|---|
| Mains or battery | BQ24195 charger on the board, 0x6B | register 0x08, bit 2 power good |
| Battery | LiPo on the JST connector | `ADC_BATTERY`, 4.3 V full scale |
| Temperature | 2 × DS18B20 (waterproof, on cables), both on D5, 4.7k pull-up | 1-Wire by ROM id, 1/16 °C a step |
| Pressure | 0.5–4.5 V transducer, 0–10 bar, divided 2/3 | `A1` |
| Relays | 4-channel opto-isolated module, active low | `D1`..`D4` |

**A LiPo must be connected, even on the bench.** On USB alone the board runs
for 5–10 seconds and then resets. Something draws more than USB can supply
through the BQ24195, the supply dips twice within half a second, and the SAMD
bootloader reads that as a double tap on reset and stays in. You see the USB
port disappear, `dmesg` shows it come back as product `0055` (the bootloader)
instead of `8055`, and the LED breathes. Nothing in the program is involved.
Found 2026-10-06 the hard way.

The relay coils run off the USB 5 V, so on battery they drop out by themselves
and come back in their commanded state when the mains returns. "10 A" on these
modules means a resistive load; put a contactor after the relay for a pump or a
fan. Fixed 230 V wiring is a job for an electrician.

Check that the operator delivers SMS over LTE-M on the SIM you mean to use
before building more than one.

## The temperature sensors

The ROM ids in `pins.csp` are placeholders. On the board:

    > /onewire 0:5

prints one `#buffer` line per sensor. Paste them over `Tin` and `Tout` in
`pins.csp` (keep the names). Hold one sensor in your hand and see which reading
rises, to know which is inside.

## SMS

The modem is an `sms` transport routed through the interpreter
(`main.csp`, at the end):

    #param  Owners string = "+46701234567 +46731234567"
    #buffer Sms:160 inout sms Owners
    #buffer Rp:160  inout repl
    #route  Sms Rp
    #route  Rp  Sms

- **A message from a number in `Owners`** runs as a line typed at the prompt,
  and what it prints goes back **to that number**. "OK" if it printed nothing.
- **What a rule prints** (`println(...) ? pwr.Send`) is an event and goes to
  **every** number in `Owners`.
- **A message from anyone else** gets no answer at all, and is counted.
- **Only reading, and `>` on a `#param` or on a `#variable` declared `in`.**
  `in` is how the program says "this is set from outside" -- `Status` is one.
  A message cannot run `/` commands, declare anything, add a rule, or set
  anything else. It gets `denied` instead.
- **At most 20 messages an hour** go out (`CSP_SMS_PER_HOUR`), counted per
  recipient. The rest are dropped. A rule that prints every cycle cannot
  empty the SIM.

Numbers compare by their digits: `+46 70-123`, `+4670123` and `004670123`
are the same phone. On the list itself, spaces and commas separate the numbers,
so write each one without spaces. Change the list in the field with
`> Owners = "..."` and keep it with `/save`.

Commands:

    > R1On = 1          relay 1 on, remembered across a restart
    TinC                one value, back to whoever asked
    > println("in ", TinC, " out ", ToutC)   several, back to whoever asked
    > Status = 1        the status report: printed by a rule, so to everyone
    > FrostLim = 30     move a limit

Units are tenths of a degree, mV and mbar.

On the host, `tests/` drives it: a stimulus row `<ms> sms "<from>" "<text>"`
delivers a message, and every message sent is a line `sms> <to>: <text>` on
stdout.

## On the board

The modem side is `port/csp_arduino.c` (`CSP_HAS_SMS`, Arduino's MKRNB
library): one AT command in flight at a time, stepped once a cycle, so neither
registering nor sending stops the rules. Unread messages are listed every 5 s
(`CSP_SMS_POLL_MS`) and deleted once taken. If the SIM has a PIN, give it as
`{'CSP_SMS_PIN', "\"1234\""}` in `wintersbone.terms`.

Not yet tried against a real network. Things that may need a look:

- the `+CMGL` listing format the parser expects (text mode, set by `NB::begin`);
- the two seconds `AT+CMGS` waits for its `>` prompt;
- what the operator does with SMS over LTE-M.

## Still to build

- **The settings store on the SAMD21**, which has no EEPROM, only flash. Until
  then `/save` of `Owners` and the relay states is untested on this board.
- **Sleep.** Without it a 2000 mAh LiPo lasts about two to three days.
