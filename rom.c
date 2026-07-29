#include "csp.h"
// Empty default image -- linked when no program is baked into the firmware.
// Every build links exactly one rom.c; this is the fallback with no program.
// Bake a program by overwriting this file:
//     csp -C -n prog.csp > rom.c      (sequential)
//     csp -C -r prog.csp > rom.c      (reactive: also carries its own graph)
//     csp -C -n --prefix failsafe f.csp > failsafe.c   (a second image)

#define R_NSTR   1
#define R_NDECL  1
#define R_NINSTR 1
#define R_NIDG   1
#define R_NOFS   1
#define R_NEDG   1
#define R_NSTATE 1

CSP_IMAGE_TYPE(rom_image_t, R_NSTR, R_NDECL, R_NINSTR, R_NIDG, R_NOFS, R_NEDG,
	       R_NSTATE);

#define R_SP       ((uint32_t)sizeof(csp_sect_t))
#define R_O_STR    ((uint32_t)sizeof(csp_image_header_t) + R_SP)
#define R_O_DECL   (R_O_STR   + R_NSTR     + CSP_PAD4(R_NSTR)   + R_SP)
#define R_O_INSTR  (R_O_DECL  + R_NDECL*8                       + R_SP)
#define R_O_IDG    (R_O_INSTR + R_NINSTR*4                      + R_SP)
#define R_O_OFS    (R_O_IDG   + R_NIDG*2   + CSP_PAD4(2*R_NIDG) + R_SP)
#define R_O_EDG    (R_O_OFS   + R_NOFS*2   + CSP_PAD4(2*R_NOFS) + R_SP)
#define R_O_STATES (R_O_EDG   + R_NEDG*2   + CSP_PAD4(2*R_NEDG) + R_SP)
#define R_SIZE     (R_O_STATES+ R_NSTATE*2 + CSP_PAD4(2*R_NSTATE))

CSP_IMAGE_CHECK(rom_image_t, R_O_STR, R_O_DECL, R_O_INSTR, R_O_IDG, R_O_OFS,
		R_O_EDG, R_O_STATES, R_SIZE);

// csp_load_image returns as soon as n_decl is 0, so an empty image is never
// rejected and never read past its header. The magic and the offsets are still
// well-formed, so a flash scan finds it and reports it as what it is: a ROM
// with nothing in it.
static const rom_image_t rom_image_data RODATA = {
  .hdr = {
    .magic = { CSP_IMAGE_MAGIC0, CSP_IMAGE_MAGIC1, CSP_IMAGE_MAGIC2,
	       CSP_IMAGE_MAGIC3 },
    .size = R_SIZE, .version = ROM_FORMAT_VERSION,
    .role = CSP_ROLE_ROM, .generation = 0,
    .n_str = 0, .n_decl = 0, .n_instr = 0, .n_edg = 0, .n_state = 0,
    .crc_str = 0, .crc_decl = 0, .crc_instr = 0, .crc_state = 0, .crc_graph = 0,
    .ofs_str = R_O_STR, .ofs_decl = R_O_DECL, .ofs_instr = R_O_INSTR,
    .ofs_idg = R_O_IDG, .ofs_ofs = R_O_OFS, .ofs_edg = R_O_EDG,
    .ofs_states = R_O_STATES,
    .crc_hdr = 0
  },
  .s_str    = { { CSP_SECT_STR }, R_NSTR + CSP_PAD4(R_NSTR) },
  .str      = { 0 },
  .s_decl   = { { CSP_SECT_DECL }, R_NDECL*8 },
  .decl     = { {{0}} },
  .s_instr  = { { CSP_SECT_INSTR }, R_NINSTR*4 },
  .instr    = { {{0}} },
  .s_idg    = { { CSP_SECT_IDG }, R_NIDG*2 + CSP_PAD4(2*R_NIDG) },
  .idg      = { 0 },
  .s_ofs    = { { CSP_SECT_OFS }, R_NOFS*2 + CSP_PAD4(2*R_NOFS) },
  .ofs      = { 0 },
  .s_edg    = { { CSP_SECT_EDG }, R_NEDG*2 + CSP_PAD4(2*R_NEDG) },
  .edg      = { 0 },
  .s_states = { { CSP_SECT_STATES }, R_NSTATE*2 + CSP_PAD4(2*R_NSTATE) },
  .states   = { {0} }
};

// The handle the runtime takes. It never names rom_image_t -- it works from the
// base and the offsets in the header.
const csp_image_ref_t rom_image RODATA = { (const uint8_t*)&rom_image_data };
