# STM32F4

The third architecture in this tree, after LPC2000 (ARM7) and LPC1700
(Cortex-M3). It is built the same way every other bare-metal board is:

    make -f Makefile.board BOARD=crazyflie
    make -f Makefile.board BOARD=crazyflie PROG="lib/pid.csp app.csp"

## No vendor HAL

`cmsis-device-f4` supplies the register map (`stm32f405xx.h`) and
`chips/arm/cmsis` supplies ARM's core layer. Everything between those and
CandySpeak is `port/csp_stm32.c` — about a thousand lines for GPIO, SysTick, a
USART console, ADC, PWM, the memory report and the main loop.

That is the same shape the LPC2000 port has (`chips/nxp/drivers/212x/`, 1478
lines of its own drivers) rather than the LPC1700 one, which sits on LPCOpen.
ST's HAL would be a third layer with its own handle structs for six peripherals
we drive directly.

## The pieces

    chips/st/stm32f4.terms          geometry, flash map, what the part has
    chips/st/drivers/f405/
        startup_f405.c              vectors + .data/.bss, our linker symbols
        sysinit_f405.c              PLL, flash latency, FPU, bus dividers
        flash_f405.c                erase/write/read for /upgrade and /save
    port/csp_stm32.c                the port layer
    boards/crazyflie/               a board that uses it

## What is derived rather than stated

A board says `{xtal, 8000000}` and `{core, 168000000}` and nothing else about
the clock. `sysinit_f405.c` computes M/N/P/Q, the flash wait states and both bus
dividers from those two numbers, and refuses at COMPILE time — `#error`, not a
runtime surprise — if the combination is unreachable. `check_st_clock` in
gen_chips.erl does the same arithmetic so `make check-boards` catches it before
a build starts.

For 8 and 168: M=8, N=336, P=2, Q=7 (exactly 48 MHz for USB), 5 wait states,
APB1 at 42 and APB2 at 84 — both at their ceilings.

The same principle covers the pin table. A board writes `{pin, 'PA1',
tim2_ch2}`; the generator derives the alternate-function number (AF1, a property
of TIM2 and not of the pin), the MODER value, and the PWM map — `tim2_ch2`
already carries the timer and the channel, so the map is not stated twice. GPIO
clocks come from which ports the pins are on.

## Two orderings that cannot be got wrong twice

**Voltage scale before the PLL.** Above 144 MHz the regulator needs scale 1, and
setting it after the switch browns the core out under load rather than at boot.

**Flash latency before the clock rises.** Raising the clock over flash still set
for the old one makes the very next instruction fetch wrong, and there is no
recovering from that.

Both are in `sysinit_f405.c` in that order, with the reason written next to
them.

## What is not built

**CAN.** The F405 has two bxCAN controllers and no board here wires one up. The
stubs at the end of `csp_stm32.c` are the same no-bus ones `csp_lpcopen.c`
compiles when a board names no `{can, ...}`: a program with a `#buffer` links
and runs and stays quiet. A real backend is a filter bank, three transmit
mailboxes and two receive FIFOs — the shape `can_212x.c` has, in ST's spelling.

**I2C.** `boards/crazyflie` muxes I2C3 for the IMU, but nothing drives it yet:
`pilot/pins/imu.csp` reads the device as a `#buffer`, and filling that buffer is
a driver this port does not have.

**A pin-capability table.** `check-boards` verifies that a pin name parses, that
its function has an AF number, and that no pin is muxed twice. It does NOT
verify that *this* pin can have *that* function. An LPC pin table is a few
hundred entries; the F4's is sixteen functions across eighty-two pins, and
transcribing that from a datasheet would introduce more errors than it caught. A
wrong AF is a pin that does nothing, found with a scope.

## Measured

`boards/crazyflie`, ARM mode, `-Os`:

    text 78736  data 8  bss 36556       (neutral image)
    text 84720                          (with private/pilot linked)

The program's image lands on slot A's first byte, 0x08060000, with `JAM\n` on
byte zero — so `/upgrade A` can replace it without rebuilding the runtime, the
same as on an LPC.
