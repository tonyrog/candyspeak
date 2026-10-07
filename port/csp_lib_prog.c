// The generated program, for a sketch that compiles C translated from a .csp.
//
// arduino-cli compiles only what is in the sketch folder, so the program the
// board carries -- $(B)/csp_prog.c, written by utils/candyspeak_c.erl -- is
// reached through an #include on the board's build directory rather than handed
// to the link. The same arrangement as port/csp_rom.c for the runtime's image.

// The program's link to the outside, when it has one: a link.c beside the
// .csp, which Makefile.board names as CSP_LIB_LINK. Compiled HERE, in the same
// unit as the program, so it reaches csp_in and csp_out by their members --
// `csp_out.a1.delta = v` -- with no name table between. It defines
// csp_lib_poll.

#define CSP_STR_(x) #x
#define CSP_STR(x)  CSP_STR_(x)

#include "csp_prog.c"

#ifdef CSP_LIB_LINK
#include CSP_STR(CSP_LIB_LINK)
#endif
