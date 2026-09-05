// The flash backend for the STM32F405: erase, write, read.
//
// The counterpart to flash_212x.c, and considerably smaller, because there is
// no IAP ROM to call. An LPC hands a command array to a routine at a fixed
// address and gets a status back; here the flash controller IS registers, and
// programming is a store instruction to the destination while the controller is
// unlocked. What that removes is the whole staging buffer: flash_212x.c copies
// into RAM because IAP writes a fixed 512-byte block from an aligned source.
//
// Offsets are from the FLASH BASE (0x08000000), not absolute addresses -- the
// same contract csp_flash.h states for every backend, so csp_flash.c does not
// know which part it is talking to.

#include <stdint.h>
#include <stddef.h>
#include "csp_config.h"
#include "stm32f405xx.h"
#include "csp_flash.h"

// The unlock sequence. Two magic words in this order, and getting it wrong
// latches the controller until reset -- there is no retry.
#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL

// PSIZE: how wide a program access is, and it is NOT free choice -- it says
// what the supply can drive. x32 needs 2.7V and no external Vpp, which is what
// a 3.3V board has. Setting it wider than the supply allows CORRUPTS the write
// rather than refusing it, which is the one failure here with no error bit.
#define PSIZE_32    FLASH_CR_PSIZE_1        // 0b10

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
	FLASH->KEYR = FLASH_KEY1;
	FLASH->KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_wait(void)
{
    while (FLASH->SR & FLASH_SR_BSY)
	;
}

// Clear the sticky error bits before an operation, so what is read afterwards
// belongs to it and not to something three commands ago.
static void flash_clear_errors(void)
{
    FLASH->SR = FLASH_SR_PGSERR | FLASH_SR_PGPERR | FLASH_SR_PGAERR |
		FLASH_SR_WRPERR | FLASH_SR_EOP;
}

static int flash_error(void)
{
    return (FLASH->SR & (FLASH_SR_PGSERR | FLASH_SR_PGPERR |
			 FLASH_SR_PGAERR | FLASH_SR_WRPERR)) ? -1 : 0;
}

int csp_flash_erase(uint8_t first, uint8_t last)
{
    uint8_t s;

    if (first > last)
	return -1;
    if (last >= csp_device()->flash.n)
	return -1;

    flash_unlock();
    flash_wait();

    for (s = first; s <= last; s++) {
	flash_clear_errors();
	// SNB is the sector NUMBER in bits 3..6. On a 1M F405 the numbering is
	// linear 0..11, which is why this is a shift and not a table -- the 2M
	// parts split the number across a bank bit and would need one.
	FLASH->CR = (FLASH->CR & ~(FLASH_CR_SNB | FLASH_CR_PSIZE)) |
		    FLASH_CR_SER | PSIZE_32 | ((uint32_t)s << FLASH_CR_SNB_Pos);
	FLASH->CR |= FLASH_CR_STRT;
	flash_wait();
	FLASH->CR &= ~FLASH_CR_SER;
	if (flash_error()) {
	    flash_lock();
	    return -1;
	}
    }
    flash_lock();
    return 0;
}

int csp_flash_write(uint32_t off, const void* data, uint32_t len)
{
    const uint8_t* src = (const uint8_t*)data;
    uint32_t base = csp_device()->flash.base;
    uint32_t i;

    if (len == 0)
	return 0;

    flash_unlock();
    flash_wait();
    flash_clear_errors();
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PG | PSIZE_32;

    // Word at a time where both ends allow it, byte at a time otherwise. PSIZE
    // has to match the access width -- a x32 store to an unaligned address is a
    // PGAERR -- so the width is switched rather than the address padded, which
    // would write bytes the caller did not ask for.
    i = 0;
    while (i < len) {
	if ((((off + i) & 3u) == 0) && ((len - i) >= 4) &&
	    ((((uintptr_t)(src + i)) & 3u) == 0)) {
	    *(volatile uint32_t*)(base + off + i) = *(const uint32_t*)(src + i);
	    i += 4;
	}
	else {
	    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PG;  // x8
	    *(volatile uint8_t*)(base + off + i) = src[i];
	    flash_wait();
	    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PG | PSIZE_32;
	    i += 1;
	}
	flash_wait();
	if (flash_error()) {
	    FLASH->CR &= ~FLASH_CR_PG;
	    flash_lock();
	    return -1;
	}
    }

    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();

    // Read it back. Flash reports success for a write into a cell that was not
    // erased -- the AND of old and new lands, no error bit is set -- and a
    // verify is the only thing that catches it.
    for (i = 0; i < len; i++)
	if (*(volatile uint8_t*)(base + off + i) != src[i])
	    return -1;
    return 0;
}

int csp_flash_read(uint32_t off, void* data, uint32_t len)
{
    const uint8_t* src = (const uint8_t*)(csp_device()->flash.base + off);
    uint8_t* dst = (uint8_t*)data;
    uint32_t i;

    // Memory-mapped, so this is a copy and not a peripheral transaction. The
    // data cache is enabled (see sysinit_f405.c) but the controller invalidates
    // it on erase and program, so a read after a write sees the new bytes.
    for (i = 0; i < len; i++)
	dst[i] = src[i];
    return 0;
}
