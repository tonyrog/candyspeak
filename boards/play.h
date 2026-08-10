// Adafruit Circuit Playground Express -- SAMD21 with the on-board sensor and
// NeoPixel set. CSP_CPX turns on the board layer that names them.
#include "embedded.h"

#define CSP_NO_IMAGE_REGISTRY 1

#define CSP_CPX 1
//#define CSP_CPX_OWNSTRIP 1
//#define CSP_NEO 1
//#define CSP_NEO_PIN 8
//#define CSP_NEO_COUNT 10
#define CSP_RAM_RESERVE 512
// Same SAMD21 as the MKR Zero, so the arena comes from the heap here too. Without
// it this board falls to the static-buffer backend and carries CSP_CODE_BUDGET
// (12K) as one object in .bss -- which is what made the linker report "changing
// start of section .bss by 4 bytes" on this target and no other.
#define CSP_ARENA_MALLOC 1
