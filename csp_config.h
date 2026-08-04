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

// Poison float on a FIXPOINT build, so a stray float in the firmware fails at
// the line that wrote it instead of dragging in soft-float. Outside the #ifndef
// above on purpose: the guard tracks the VALUE, however it was arrived at, so
// -DUSE_FIXPOINT=1 from a board Makefile is protected the same as the default.
// And `#if defined(X) && (X == 1)`, not `#ifdef X && ...` -- #ifdef takes one
// identifier and silently discards the rest, which made this unconditional and
// poisoned the float targets (ESP32/ESP8266 have USE_FIXPOINT 0) as well.
//
// Only for a BOARD build. The host tools print with printf and the poison would
// fail them at the first %f -- and a host that drags in soft-float costs
// nothing anyway. That is why this lived in the sketch's own csp_config.h
// before the two files were merged.
#if defined(CSP_BOARD) && defined(USE_FIXPOINT) && (USE_FIXPOINT == 1)
#define float   _Pragma("GCC error \"float not allowed\"") float
#define double  _Pragma("GCC error \"double not allowed\"") double
#endif

#endif
