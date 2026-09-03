// CRC-16/CCITT-FALSE, and nothing else.
//
// Its own translation unit because of who needs it: the runtime, the eeprom, the
// ROM generator -- and csp_flash.c, which is otherwise free of the interpreter
// entirely and builds from four files. Leaving the CRC in csp_rt.c meant the
// flash layer could not check an image header without dragging in the whole
// engine, and the tests that build the flash layer alone could not link.
//
// The polynomial and the 0xFFFF seed are what every CandySpeak image and eeprom
// header was folded with, so this is not a place to be clever.

#include "csp.h"

// CRC-16/CCITT, incremental: folds n bytes into `crc` and returns it, so a
// caller can chain several regions (str, then decls, then instrs, ...). is_rom
// selects ro_byte, which is memcpy_P on AVR (the region is PROGMEM) and a plain
// read on the host; pass 0 for ordinary RAM. Table-free. Seed with 0xFFFF.
NOINLINE uint16_t csp_crc16(uint16_t crc, const void* data, size_t n, int is_rom)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t k;
    unsigned b;

    for (k = 0; k < n; k++) {
	uint8_t byte = is_rom ? ro_byte(p + k) : p[k];
	crc ^= (uint16_t)((uint16_t)byte << 8);
	for (b = 0; b < 8; b++)
	    crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
				 : (uint16_t)(crc << 1);
    }
    return crc;
}
