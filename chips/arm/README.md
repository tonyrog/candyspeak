# CMSIS-Core — ARM's, not any vendor's

`cmsis/` holds ARM's Cortex-M Core Peripheral Access Layer: `core_cm3.h`,
`core_cm4.h` and the three files they include. Vendor-neutral by origin —
copyright ARM Limited, version 3.20 — and every silicon vendor ships an
identical copy inside its own package.

## Why there is a copy here

ST does not ship it. `cmsis-device-f4` is the *device* half — `stm32f405xx.h`,
the register map, one header per part — and its first include is:

    #include "core_cm4.h"    /* Cortex-M4 processor and core peripherals */

which is in ARM's CMSIS repository, not ST's. NXP takes the other approach and
bundles a copy in every LPCOpen family, which is why this tree already had eight
of them before ST arrived.

So a port had two bad options: clone a second large repository for five headers,
or `-I` its way into `chips/nxp/lpcopen/lpc11u6x/lpc_chip_11u6x/inc` from an ST
build — which compiles, and reads as a mistake to everyone who sees it later.

These files are the ninth copy and the one a port should use.

## Which parts need which

    core_cm3.h   LPC1754, LPC17xx        (Cortex-M3)
    core_cm4.h   STM32F405              (Cortex-M4, with FPU and DSP)

An ARM7 (LPC212x) has no CMSIS core at all — no NVIC, no SysTick, no SCB.
`chips/nxp/drivers/212x/vic_212x.c` provides `NVIC_EnableIRQ` and friends over
the VIC precisely so the layer above needs no `#if`; see its header.

## Provenance

Taken verbatim from `chips/nxp/lpcopen/lpc11u6x/lpc_chip_11u6x/inc/` (the M4
files) and `chips/nxp/lpcopen/lpc175x_6x/lpc_chip_175x_6x/inc/` (the M3 one).
Unmodified. Keep them that way: a local fix to a CMSIS header is a fix that
disappears the next time someone syncs from upstream, and these are ABI, not
policy.
