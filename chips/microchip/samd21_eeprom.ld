/* The CandySpeak EEPROM on a SAMD21: the last rows of flash.
 *
 * NOT a linker script of its own -- the variant's flash_with_bootloader.ld is
 * that, and it lives in the core under ~/.arduino15, shared by every sketch.
 * This is handed to ld as one more INPUT FILE (-Wl,<this>), which ld reads as
 * an implicit script: it adds to the variant's, and may name the variant's
 * FLASH region and its symbols.
 *
 * What it adds:
 *
 *   __csp_eeprom_start .. __csp_eeprom_end   the region port/csp_arduino.c
 *                                             reads and writes
 *   the check that the image stops short of it
 *
 * Outside the image, so an upload leaves it alone: bossac without -e erases
 * only the pages it writes (Makefile.board uploads that way for these boards).
 * The image is .text up to __etext and then .data's initial values, which is
 * what the ASSERT adds up.
 *
 * The SIZE is here and nowhere else. A multiple of 256, the SAMD21's erase row.
 */

__csp_eeprom_end   = ORIGIN(FLASH) + LENGTH(FLASH);
__csp_eeprom_start = __csp_eeprom_end - 2048;

ASSERT((__csp_eeprom_start % 256) == 0,
       "CandySpeak EEPROM region is not row-aligned")
ASSERT(__etext + (__data_end__ - __data_start__) <= __csp_eeprom_start,
       "sketch grows into the CandySpeak EEPROM region (chips/microchip/samd21_eeprom.ld)")
