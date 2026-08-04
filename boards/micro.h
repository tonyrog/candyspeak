// Arduino Micro -- ATmega32U4, 2 560 bytes of RAM but only 28 672 usable flash:
// the Caterina USB bootloader takes 4K where the UNO's optiboot takes 512, and
// the software USB CDC stack costs another ~2.9K on top. Exec-only does not fit
// yet.
#include "embedded.h"

#define CSP_ARENA_MALLOC 1
#define CSP_ROM_RECOVER  0
