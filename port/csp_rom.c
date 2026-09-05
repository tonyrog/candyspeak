// The sketch's ROM translation unit -- which PROGRAM the board carries.
//
// arduino-cli compiles every .c in the sketch folder and nothing outside it, so
// the image cannot simply be handed to the link the way Makefile.board does it
// for a bare-metal board ($(B)/rom.o). This file is the indirection that makes
// the same PROG= work on both: it is a one-line include of an image that
// Makefile.board generated into the board's build directory, which is on the
// include path as -I$(B).
//
// Before this existed, CandySpeak/rom.c was a fixed symlink to gen/rom.c -- so
// every one of the ten Arduino boards carried the SAME image, whatever `make
// rom-image` last wrote, with nothing about BOARD entering into it.
//
// CSP_ROM is a bare token, not a quoted string, and is stringified here. The
// same reasoning as CSP_BOARD, whose comment in csp_config.h has the long
// version: a quoted path has to survive make, then arduino-cli
// --build-property, then the shell, and that is three chances to lose a quote.
//
// The stringify is spelled out here rather than taken from csp_config.h, which
// has the identical CSP_STR. This file must include NOTHING before the image:
// csp_config.h alone does not compile -- it declares csp_gate_mask_t and the
// types come from csp.h, which the generated image includes as its first line.
//
// PROG= (empty) points it at gen/rom_host.c instead -- the neutral image, two
// declarations and no instructions. That is what a bare-metal board links in
// the same case, so the two toolchains answer an empty PROG the same way.
#define CSP_ROM_STR_(x) #x
#define CSP_ROM_STR(x)  CSP_ROM_STR_(x)

#ifndef CSP_ROM
#define CSP_ROM csp_rom.c
#endif

// CSP_ROM must not name a file that exists in the SKETCH folder. A quoted
// include searches the including file's own directory before -I, and this file
// is compiled as CandySpeak/rom.c -- so -DCSP_ROM=rom.c makes it include
// itself, which gcc reports as a thousand-deep nesting rather than as the
// mistake it is. csp_rom.c and rom_host.c exist only under -I, which is why
// those are the two names used.
#include CSP_ROM_STR(CSP_ROM)
