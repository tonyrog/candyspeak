#include "csp.h"
// Empty default ROM image -- linked when no program is baked into the firmware.
// Every build links exactly one rom.c; this is the fallback with no program.
// Bake a program by overwriting this file:
//     csp -C -n prog.csp > rom.c      (sequential)
//     csp -C -r prog.csp > rom.c      (reactive: also carries its own graph)
const int         rom_str_len RODATA = 0;
const char        rom_str[1]  RODATA = { 0 };
const int         rom_n_decl  RODATA = 0;
const csp_decl_t  rom_decl[1] RODATA = { {{0}} };
const int         rom_n_instr RODATA = 0;
const csp_instr_t rom_instr[1] RODATA = { {{0}} };
const int         rom_n_edg   RODATA = 0;
const index_t     rom_idg[1]  RODATA = { 0 };
const index_t     rom_ofs[1]  RODATA = { 0 };
const index_t     rom_edg[1]  RODATA = { 0 };
const int         rom_n_states  RODATA = 0;
const state_t     rom_states[1] RODATA = { {0} };
// Version/CRC checked by csp_load_rom -- but only when rom_n_decl != 0, so an
// empty ROM is never rejected. Present so the extern references resolve.
const uint16_t    rom_version RODATA = ROM_FORMAT_VERSION;
const uint16_t    rom_crc     RODATA = 0;
