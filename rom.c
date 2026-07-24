#include "csp.h"
// Empty default ROM image -- linked when no program is baked into the firmware.
// Every build links exactly one rom.c; this is the fallback with no program.
// Bake a program by overwriting this file:
//     csp -C -n prog.csp > rom.c      (sequential)
//     csp -C -r prog.csp > rom.c      (reactive: also carries its own graph)
const char        rom_str[1]   RODATA = { 0 };
const csp_decl_t  rom_decl[1]  RODATA = { {{0}} };
const csp_instr_t rom_instr[1] RODATA = { {{0}} };
const index_t     rom_idg[1]   RODATA = { 0 };
const index_t     rom_ofs[1]   RODATA = { 0 };
const index_t     rom_edg[1]   RODATA = { 0 };
const state_t     rom_states[1] RODATA = { {0} };
// csp_load_rom verifies the header CRCs only when n_instr != 0, so an empty
// image is never rejected. All-zero here; the extern references still resolve.
const csp_image_header_t rom_header RODATA = {
  .version = ROM_FORMAT_VERSION,
  .n_str = 0, .n_decl = 0, .n_instr = 0, .n_edg = 0, .n_state = 0,
  .crc_str = 0, .crc_decl = 0, .crc_instr = 0, .crc_state = 0,
  .crc_graph = 0, .crc_hdr = 0
};
