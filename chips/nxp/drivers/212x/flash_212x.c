// The IAP flash backend for LPC2000.
//
// IAP is a routine in the part's BOOT ROM, called through a fixed address. That
// it lives in ROM rather than in flash is what makes writing flash from a
// running program possible at all: the eraser is not in the thing being erased.
// What IS in flash is the code that calls it -- so erasing the runtime region
// does not fail, it stops mid-sector. csp_flash_writable refuses that, and this
// file assumes it already did.
//
// Five things here are not obvious and none of them report an error:
//
//   INTERRUPTS. Flash is unreadable while it is being written. An interrupt
//   during the operation vectors into flash, fetches nothing, and takes the
//   data abort handler -- which is also in flash. They are off across every
//   IAP call and restored after.
//
//   PREPARE. Sectors re-protect themselves after every successful erase or
//   write, so prepare is needed before EACH operation, not once per session.
//
//   THE CLOCK, in kHz, passed to IAP. It times the flash charge pump from it.
//   Wrong and the write appears to succeed while the cells are underprogrammed
//   -- it verifies now and fails months later. Read live from the clock driver
//   rather than from a constant that a PLL change would leave behind.
//
//   BLOCK SIZE AND ALIGNMENT. IAP copies 256, 512, 1024 or 4096 bytes, the
//   destination must be aligned to the size, and the source must be word
//   aligned RAM. A caller's buffer is none of those things, hence the staging
//   buffer below.
//
//   THE SOURCE MUST BE RAM. IAP reads it while flash is inaccessible. Handing
//   it a pointer into flash -- a string literal, a const table -- reads
//   garbage, silently.

#include <string.h>
#include "csp.h"
#include "csp_flash.h"
#include "chip_212x.h"
#include "vic_212x.h"

// IAP entry and command codes. Not pulled from nxp_files/2129/iap_lpc.c: that
// file carries a part table, a sector table and an ISP re-invoke this does not
// want, and duplicating twenty lines beats linking all of it into every board.
#define IAP_ENTRY                 0x7FFFFFF0
#define CMD_PREPARE               50
#define CMD_COPY_RAM_TO_FLASH     51
#define CMD_ERASE                 52
#define CMD_BLANK_CHECK           53
#define IAP_CMD_SUCCESS            0

// Thumb: the entry address has bit 0 set. Getting this wrong is an undefined
// instruction, and _undef on this part is `b .`.
#define IAP_CALL(cmd, res) \
    ((void(*)(uint32_t*, uint32_t*))((uint32_t)IAP_ENTRY + 1))((cmd), (res))

// The smallest block IAP will copy. 256 is legal on this family, but the
// erratum note in the vendor sources says 512 on some 212x steppings -- and a
// block that is refused returns an error rather than writing short, so the
// conservative choice costs a little RAM and nothing else.
#define IAP_BLOCK 512

// Word aligned because IAP requires it of the source, and static because it
// must not be on a stack that an interrupt-free window is not protecting.
static uint32_t stage[IAP_BLOCK / sizeof(uint32_t)];

static uint32_t iap_cmd[5];
static uint32_t iap_res[3];

static uint32_t clk_khz(void)
{
    return Chip_Clock_GetSystemClockRate() / 1000u;
}

// Every IAP call goes through here, so the interrupt window is one place.
static uint32_t iap(uint32_t c0, uint32_t c1, uint32_t c2,
		    uint32_t c3, uint32_t c4)
{
    uint32_t saved, r;

    iap_cmd[0] = c0; iap_cmd[1] = c1; iap_cmd[2] = c2;
    iap_cmd[3] = c3; iap_cmd[4] = c4;
    saved = DisableIRQ();
    IAP_CALL(iap_cmd, iap_res);
    RestoreIRQ(saved);
    r = iap_res[0];
    return r;
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
    // The destination has to be aligned to the block size, so the caller's
    // offset does too. A region always starts on a sector, and every sector on
    // this family is a multiple of 512, so this only fires on a caller that
    // invented an offset of its own.
    if (base % IAP_BLOCK)
	return CSP_FLASH_ERR;

    while (len) {
	uint32_t n = (len > IAP_BLOCK) ? IAP_BLOCK : len;
	uint8_t  sec_first, sec_last;

	// A short tail is padded with 0xFF -- erased flash -- so the block is a
	// legal size and the padding is indistinguishable from never-written.
	memset(stage, 0xFF, sizeof(stage));
	memcpy(stage, p, n);

	sec_first = (uint8_t)csp_sector_of(&d->flash, base - d->flash.base);
	sec_last  = sec_first;
	// Prepare again for THIS block: the previous write re-protected it.
	if (iap(CMD_PREPARE, sec_first, sec_last, 0, 0) != IAP_CMD_SUCCESS)
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
    // Memory mapped, which is the one place this part is easier than the host:
    // no IAP, no interrupt window, and it works while the program runs.
    memcpy(data, (const void*)(uintptr_t)(d->flash.base + off), len);
    return CSP_FLASH_OK;
}
