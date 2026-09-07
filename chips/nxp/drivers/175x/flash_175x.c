// The IAP flash backend for LPC17xx.
//
// The SAME boot-ROM routine chips/nxp/drivers/212x/flash_212x.c calls, on a
// different core -- so this file is that one with three changes, and the long
// explanation of the traps lives there rather than being copied:
//
//   THE ENTRY ADDRESS. 0x1FFF1FF1 here, 0x7FFFFFF0 on an LPC2000. And it is
//   already Thumb-tagged: bit 0 is part of the documented address, so this one
//   must NOT have 1 added to it the way the ARM7 version does. Adding it again
//   lands on 0x1FFF1FF2, which is a hard fault inside the boot ROM -- with the
//   flash controller half configured.
//
//   MASKING INTERRUPTS. An ARM7 has DisableIRQ/RestoreIRQ out of vic_212x.c;
//   a Cortex-M has PRIMASK. Same purpose: flash is unreadable while it is being
//   written, and an interrupt taken then vectors into flash and fetches nothing.
//
//   THE BLOCK SIZE. 256, the smallest IAP copy on this family -- not the 512 the
//   212x file uses, which is there for a stepping erratum that does not apply
//   here. It is a STAGING BUFFER IN .bss on a part with 16K of RAM, and the
//   store backend keeps one of the same size, so the choice is 512 bytes of a
//   board's memory. Alignment is never the binding constraint: every region in
//   every 17xx map starts on a 4K or 32K sector.
//
// Everything else -- prepare before EVERY operation, the clock in kHz, a
// word-aligned staging buffer in RAM, blank-check after erase -- is the same
// and is explained in flash_212x.c.

#include <string.h>
#include "csp.h"
#include "csp_flash.h"
#include "chip.h"

// Documented entry, Thumb bit included. See the note above.
#define IAP_ENTRY                 0x1FFF1FF1
#define CMD_PREPARE               50
#define CMD_COPY_RAM_TO_FLASH     51
#define CMD_ERASE                 52
#define CMD_BLANK_CHECK           53
#define IAP_CMD_SUCCESS            0

#define IAP_CALL(cmd, res) \
    ((void(*)(uint32_t*, uint32_t*))((uint32_t)IAP_ENTRY))((cmd), (res))

#define IAP_BLOCK 256

// Word aligned because IAP requires it of the source, and static because it
// must not be on a stack that an interrupt-free window is not protecting.
static uint32_t stage[IAP_BLOCK / sizeof(uint32_t)];

static uint32_t iap_cmd[5];
static uint32_t iap_res[5];

static uint32_t clk_khz(void)
{
    return Chip_Clock_GetSystemClockRate() / 1000u;
}

// Every IAP call goes through here, so the interrupt window is one place.
static uint32_t iap(uint32_t c0, uint32_t c1, uint32_t c2,
		    uint32_t c3, uint32_t c4)
{
    uint32_t saved;

    iap_cmd[0] = c0; iap_cmd[1] = c1; iap_cmd[2] = c2;
    iap_cmd[3] = c3; iap_cmd[4] = c4;
    // PRIMASK saved and restored, not blindly re-enabled: this is called from
    // inside the cycle, and turning interrupts on regardless of what they were
    // is how a masked section stops being one.
    saved = __get_PRIMASK();
    __disable_irq();
    IAP_CALL(iap_cmd, iap_res);
    __set_PRIMASK(saved);
    return iap_res[0];
}

int csp_flash_erase(uint8_t first, uint8_t last)
{
    if (iap(CMD_PREPARE, first, last, 0, 0) != IAP_CMD_SUCCESS)
	return CSP_FLASH_ERR;
    if (iap(CMD_ERASE, first, last, clk_khz(), 0) != IAP_CMD_SUCCESS)
	return CSP_FLASH_ERR;
    // Confirmed, not assumed. A sector that reported a successful erase and did
    // not actually clear leaves the write below writing into ones, which
    // produces a value that is neither the old contents nor the new.
    if (iap(CMD_BLANK_CHECK, first, last, 0, 0) != IAP_CMD_SUCCESS)
	return CSP_FLASH_ERR;
    return CSP_FLASH_OK;
}

int csp_flash_write(uint32_t off, const void* data, uint32_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    const csp_device_t* d = csp_device();
    uint32_t base;

    if ((d == NULL) || (data == NULL))
	return CSP_FLASH_ERR;
    base = d->flash.base + off;
    if (base % IAP_BLOCK)
	return CSP_FLASH_ERR;

    while (len) {
	uint32_t n = (len > IAP_BLOCK) ? IAP_BLOCK : len;
	uint8_t  sec;

	// A short tail is padded with 0xFF -- erased flash -- so the block is a
	// legal size and the padding is indistinguishable from never-written.
	memset(stage, 0xFF, sizeof(stage));
	memcpy(stage, p, n);

	sec = (uint8_t)csp_sector_of(&d->flash, base - d->flash.base);
	// Prepare again for THIS block: the previous write re-protected it.
	if (iap(CMD_PREPARE, sec, sec, 0, 0) != IAP_CMD_SUCCESS)
	    return CSP_FLASH_ERR;
	if (iap(CMD_COPY_RAM_TO_FLASH, base, (uint32_t)(uintptr_t)stage,
		IAP_BLOCK, clk_khz()) != IAP_CMD_SUCCESS)
	    return CSP_FLASH_ERR;

	base += IAP_BLOCK;
	p    += n;
	len  -= n;
    }
    return CSP_FLASH_OK;
}

int csp_flash_read(uint32_t off, void* data, uint32_t len)
{
    const csp_device_t* d = csp_device();

    if ((d == NULL) || (data == NULL))
	return CSP_FLASH_ERR;
    // Memory mapped, so no IAP and no interrupt window -- and it works while
    // the program runs.
    memcpy(data, (const void*)(uintptr_t)(d->flash.base + off), len);
    return CSP_FLASH_OK;
}
