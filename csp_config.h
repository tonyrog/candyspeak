#ifndef __CSP_CONFIG_H__
#define __CSP_CONFIG_H__

// Build configuration: the board first, then a default for everything the board
// did not pick.
//
// The board file holds ONLY its deltas. Everything else falls through to the
// #ifndef defaults below, so adding a setting means adding one default here --
// not editing eleven board files, and not discovering later that three of them
// were missed. See boards/README.md for what belongs in a board file (what is
// true about the hardware) and what does not (compiler flags, and build
// variants like CSP_EXEC_ONLY, which describe the image rather than the board).
//
// CSP_BOARD is stringified HERE rather than quoted in the Makefile:
//
//     make -f Makefile.uno exec      ->  -DCSP_BOARD=boards/uno.h
//
// A quoted path has to survive make, then arduino-cli --build-property, then
// the shell that invokes the compiler. That is three chances to lose a quote;
// passing a bare token has none.
// NOTE: settings ONLY. No float poisoning here -- see the end of csp.h. This
// file has to be safe to include as the very first thing a sketch does, before
// any board library header, because a board file may decide WHICH libraries the
// sketch includes (CSP_CPX picks Adafruit_CircuitPlayground, CSP_NEO picks
// Adafruit_NeoPixel). The poison must land AFTER those headers are parsed --
// they have float members -- so it belongs with csp.h, which the sketch
// includes once its library choices are made.
#define CSP_STR_(x) #x
#define CSP_STR(x)  CSP_STR_(x)
#ifdef CSP_BOARD
#include CSP_STR(CSP_BOARD)
#endif

// RAM the system reports having. The host default assumes plenty; a small
// target sets its own (or leaves it alone -- nothing is dimensioned from it,
// it only feeds /memory).
#ifndef SYSTEM_RAM_CAPACITY
#define SYSTEM_RAM_CAPACITY (256*1024)
#endif

#ifndef MAX_STATES
#define MAX_STATES 16
#endif

#ifndef MAX_IN_STATES
#define MAX_IN_STATES 8   // max states in one `#in A B C ...` OR-list
#endif

#ifndef REACTIVE_DEFAULT
#define REACTIVE_DEFAULT 0
#endif

#ifndef SUPPORT_REACTIVE
#define SUPPORT_REACTIVE    1 // enq/deq
#endif

#ifndef USE_STATISTICS
#define USE_STATISTICS  1    // need some accounting
#endif

// Recover a ROM image whose header CRC is damaged by walking the section
// prologues instead (csp_load_image). Costs ~1 040 bytes of flash and covers
// exactly one failure: a bad crc_hdr with the sections intact. Off, that image
// is rejected with a message and the node runs empty -- the same path a damaged
// SECTION already takes, recovery or not. A board that is short on flash and
// has a FAILSAFE bank to fall back on can afford to say no.
#ifndef CSP_ROM_RECOVER
#define CSP_ROM_RECOVER 1
#endif

// Q16.16 fixed point instead of float (saves ~4KB on AVR). Keyed on the
// ARCHITECTURE, not on the board: it follows from what the target's FPU and
// libgcc look like, so it belongs here rather than repeated in board files.
#ifndef USE_FIXPOINT
#if defined(__AVR__)
#define USE_FIXPOINT        1
#elif defined(__SAMD21G18A__)  // every MKR + Zero + Nano 33 IoT
#define USE_FIXPOINT        1
#elif defined(ESP32)
#define USE_FIXPOINT        0
#elif defined(ESP8266)
#define USE_FIXPOINT        0
#elif defined(__SAM3X8E__)     // Arduino Due
#define USE_FIXPOINT        1
#else
#define USE_FIXPOINT        1  // 0=float, 1=Q16.16 fixpoint
#endif
#endif

#endif
