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
| `pins.csp` | the hardware: relays, battery, pressure, the charger and two TMP117 |
| `main.csp` | the logic: measurements, four alarms, relays, the status report |
| `tests/` | power fail, power back, a glitch under the delay, frost, a relay |

## Hardware

| What | Part | How it is read |
|---|---|---|
| Mains or battery | BQ24195 charger on the board, 0x6B | register 0x08, bit 2 power good |
| Battery | LiPo on the JST connector | `ADC_BATTERY`, 4.3 V full scale |
| Temperature | 2 × TMP117, 0x48 inside and 0x49 outside | register 0, 7.8125 m°C a step |
| Pressure | 0.5–4.5 V transducer, 0–10 bar, divided 2/3 | `A1` |
| Relays | 4-channel opto-isolated module, active low | `D1`..`D4` |

The relay coils run off the USB 5 V, so on battery they drop out by themselves
and come back in their commanded state when the mains returns. "10 A" on these
modules means a resistive load; put a contactor after the relay for a pump or a
fan. Fixed 230 V wiring is a job for an electrician.

Check that the operator delivers SMS over LTE-M on the SIM you mean to use
before building more than one.

## SMS

The plan is an `sms` transport, routed through the interpreter:

    #buffer Sms:160 inout sms "+46701234567" "+46731234567"
    #buffer Rp:160  inout repl
    #route  Sms Rp
    #route  Rp  Sms

A message from a number on the list is run as if it had been typed, and what it
prints goes back. Messages from other numbers are dropped without an answer and
counted. Commands:

    > R1On = 1          relay 1 on, remembered across a restart
    > Status = 1        temperatures, mains, battery, relays
    TinC                one value
    > FrostLim = 30     move a limit

Units are tenths of a degree, mV and mbar.

## Still to build

- **The `sms` transport**, on the MKRNB library's `NB_SMS`, with a file/pty
  backend on the host so the tests can drive it.
- **Reply or alarm.** Output printed while a received line runs goes back to its
  sender only. Output printed by a rule is an alarm and goes to the whole list.
  The console ring needs a mark at the switch; guessing by timing would send an
  alarm that coincides with a command to one person only.
- **No prompt and no echo** on a `repl` fed by SMS.
- **A restricted mode** for lines that arrive by SMS: read expressions and `>`
  on a `#param`, nothing else. No `/` commands and no new rules.
- **Framing and a cap.** One message when the output has been quiet for a
  cycle, or at 160 characters. Also a hard limit per hour, so that a rule that
  prints every cycle cannot empty the SIM.
- **I2C in `port/csp_arduino.c`.** Until then the charger and the TMP117s read
  zero on the board.
- **The settings store on the SAMD21**, which has no EEPROM, only flash.
- **Sleep.** Without it a 2000 mAh LiPo lasts about two to three days.
