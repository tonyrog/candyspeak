// Arduino UNO -- ATmega328P, 32 256 bytes of usable flash (optiboot takes 512)
// and 2 048 of RAM. The tightest AVR target that still fits an exec-only image.
// RAM, not flash, is what limits programs here.
#include "embedded.h"

#define CSP_ARENA_MALLOC 1
