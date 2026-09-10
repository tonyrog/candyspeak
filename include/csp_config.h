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
//     make -f Makefile.board BOARD=uno exec  ->  -DCSP_BOARD=csp_board.h
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

// THE IMAGE REGISTRY IS OFF ON AVR, and it has to be.
//
// `csp_images` is an ORPHAN section -- no linker script on any of these
// platforms names it -- so where it lands is a heuristic, and on this
// architecture both answers are wrong. Measured 2026-09-09:
//
//   mega  (arduino-cli, 122 kB)  csp_images VMA 0x1dc72 -- in FLASH, and the
//                                runtime reads the array as DATA. A 16-bit data
//                                address cannot even name it, so /images
//                                reported `size=274371596 rules=4186
//                                (header CRC BAD)` -- an image read from
//                                nowhere.
//   mega_bare (avr-gcc, 116 kB)  csp_images VMA 0x8002ca -- which is also where
//                                .bss starts. The entry is either not copied or
//                                immediately zeroed, so it reads NULL.
//
// Reading it as flash instead is not the fix: pgm_read_word is LPM and reaches
// the first 64 kB, and the section lands past that on exactly the images big
// enough to care.
//
// Nothing is lost. The registry answers "what did this build LINK", which only
// /images and the A/B image choice ask -- and no AVR here defines
// CSP_HAVE_FLASH, so there are no slots to choose between. csp_load_rom reaches
// the firmware's own image through `rom_image` directly and boots exactly as
// before; /images now says "no images registered", which is true, instead of
// printing a header read from an address that is not in the data space.
#if defined(__AVR__) && !defined(CSP_IMAGE_REGISTRY)
#define CSP_NO_IMAGE_REGISTRY 1
#endif

// LINE INPUT OR A LINE EDITOR.
//
// The editor is cursor keys, history, insert-in-the-middle and the escape
// decoder that makes them work -- about 1500 bytes of AVR text. An exec-only
// node has no compiler and no prompt worth editing at: its input is a handful
// of commands and whatever a master relays to it.
//
// And on a RELAYED node the editing has already happened at the other end.
// Arrow keys and history on a node reached through a route are not merely
// unnecessary, they are WRONG -- the escape sequences would be interpreted
// twice, once by the master's editor and once by the slave's.
//
// So: exec-only gets the collector, everything else gets the editor. A board
// that wants the editor back in an exec image defines CSP_LINE_EDIT.
//
// It changes csp_line_t's LAYOUT (no history, no cursor), so it has to be the
// same answer in every translation unit -- which is why it is decided here,
// from the same board header and command line every file sees, and not per
// source file.
#if defined(CSP_EXEC_ONLY) && !defined(CSP_LINE_EDIT) && !defined(CSP_LINE_SIMPLE)
#define CSP_LINE_SIMPLE 1
#endif

// RAM the system reports having. The host default assumes plenty; a small
// target sets its own (or leaves it alone -- nothing is dimensioned from it,
// it only feeds /memory).
#ifndef SYSTEM_RAM_CAPACITY
#define SYSTEM_RAM_CAPACITY (256*1024)
#endif

// Was MAX_STATES, and it was never a count of states -- it is the WIDTH of the
// reactive gate mask. csp_estate_t.rule_state holds, per rule body, the set of
// States its `#in` block covers, and csp_react tests it with `(1u << sv) & sm`.
// So a state number the mask cannot represent is a state whose block never
// matches in reactive mode. Nothing about storage: states are declarations and
// are bounded by the declaration pool.
//
// Derived from the type so widening is one edit. uint32_t doubles it to 32 and
// costs 2 bytes per RULE BODY in the reactive tables -- weigh that against the
// program, not against a fixed table.
typedef uint16_t csp_gate_mask_t;
#define MAX_GATE_STATES ((int)(8 * sizeof(csp_gate_mask_t)))

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
