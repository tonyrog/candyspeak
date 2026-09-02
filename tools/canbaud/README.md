# canbaud — measure a CAN bus bit rate on an RP2040

```bash
# Adafruit Feather RP2040 CAN
arduino-cli compile --fqbn rp2040:rp2040:adafruit_feather_can tools/canbaud

# Waveshare RP2350-CAN, or any RP2350 -- the core has no Waveshare entry, and
# generic_rp2350 is the right board for it: same RP2350A, same 2 MB flash.
arduino-cli compile --fqbn rp2040:rp2040:generic_rp2350 tools/canbaud

arduino-cli upload --fqbn <same> -p /dev/ttyACM0 tools/canbaud
```

RP2350 needs no source change. It runs at 150 MHz instead of 125, so the samples
get finer on their own:

| | sample | 8 Mbit bit | 1 Mbit bit |
|---|---|---|---|
| RP2040 @125 MHz | 8.00 ns | 16 samples | 125 |
| RP2350 @150 MHz | 6.67 ns | 19 samples | 150 |
| RP2350 @250 MHz (overclocked) | 4.00 ns | 31 samples | 250 |

Regenerate `canbaud.pio.h` after editing the `.pio`:

```bash
~/.arduino15/packages/rp2040/tools/pqt-pioasm/*/pioasm -o c-sdk \
    tools/canbaud/canbaud.pio tools/canbaud/canbaud.pio.h
```

## Wiring

Connect the transceiver's **RX** — the digital side, not CAN_H/CAN_L — to
GPIO6.

Both CAN-branded boards route the transceiver to a standalone controller rather
than to a GPIO, so both need one wire from the transceiver's RXD pin:

| board | controller | transceiver |
|---|---|---|
| Adafruit Feather RP2040 CAN | MCP2515 (SPI) | on-board |
| Waveshare RP2350-CAN | XL2515, an MCP2515 clone (SPI) | SIT65HVD230 |

A board with a bare transceiver — no controller in between — already has RX on a
header and needs no wire at all. That is the easier target if you are picking
hardware for this.

**Neither board's own transceiver can carry CAN FD data phases.** The
SIT65HVD230 is rated to 1 Mbit, and the MCP2515/XL2515 in front of it is
classic-only. The measurement does not care — it counts pulses, not frames — but
to see a 2–8 Mbit data phase at all you need an FD-rated transceiver wired to
the pin, whichever MCU you use.

Nothing is transmitted. The pin is an input and the bus is untouched.

## How it works

**PIO samples, it does not time edges.** Timing edges needs a counting loop
whose cycle cost must be right in every branch, and a miscount produces a
plausible-looking wrong answer. The PIO program is one instruction — `in pins,
1` — so every sample costs exactly one clock, autopush hands off a word every
32, and DMA writes them to RAM with no CPU involved. At 125 MHz that is 8 ns per
sample: 500 samples across a 250k bit, 15 across the shortest bit CAN FD can
carry.

**The analysis histograms run lengths rather than taking the minimum.** The
shortest pulse *is* one bit time, but a single glitch or a ringing edge makes it
shorter and nothing in that one sample says so. Every gap on a CAN bus is an
integer multiple of the bit time — bit stuffing allows at most five alike in a
row — so the histogram has peaks at 1x through 5x, and the answer is the
candidate that explains the most runs. Hundreds of edges vote instead of one.

Each candidate is scored by how many runs land within 15% of an integer multiple
of it. Scoring by runs SEEN rather than by distinct lengths keeps one rare glitch
from outvoting the bus.

## CAN FD

Arbitration and data run at different rates in the same frame, so a FD bus shows
two families of peaks. The data phase wins the first pass — it carries the most
bits — and a second pass over what the first could not explain recovers
arbitration.

**Where that fails, and it is the method rather than the code:** when the two
rates differ by a small integer factor, an arbitration bit is indistinguishable
from that many data bits by LENGTH alone. Measured against synthetic buses:

| arbitration | data | result |
|---|---|---|
| 500k | 5M  | both, cleanly |
| 1M   | 8M  | both, cleanly |
| 500k | 2M  | data correct, arbitration reported as a multiple (ratio is exactly 4x) |

Doing better needs the frames' *structure* — split each frame at its BRS bit and
analyse the halves — not a smarter histogram.

## Accuracy

Verified against synthetic buses with jitter and injected glitches: exact at
250k, 1M, 2M, 5M and 8M, and unmoved by 5–8% jitter or a handful of 1–2 sample
blips.

The limit is not the RP2040. A transceiver's loop delay is **asymmetric** —
recessive-to-dominant is faster than the other way — by tens of nanoseconds.
Against a 4000 ns bit that is noise; against a 125 ns bit at 8 Mbit it is 10–20%
on every individual pulse. The histogram survives it better than a single
measurement would, because the asymmetry shifts dominant and recessive runs in
opposite directions by the same amount and the SPACING between peaks is what
carries the answer. It is still why CAN FD transceivers are specified with a
loop delay symmetry figure.
