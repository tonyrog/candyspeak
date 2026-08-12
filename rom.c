// Generated CandySpeak image (rom_*) -- do not edit.
//   source:  ./examples/empty.csp
//   version: 1.0-38-g9fada5a-dirty
//   built:   Aug 12 2026 01:33:23
//   size:    0 instr, 3 decl, 34 str, 3 states

#include "csp.h"
#if ROM_FORMAT_VERSION != 10
#error "rom.c is stale: generated for ROM format 10, csp.h is newer -- regenerate with 'csp -C'"
#endif

CSP_IMAGE_TYPE(rom_image_t, 37,4,1,1,1,1);
CSP_IMAGE_CHECK(rom_image_t, 64,112,152,164,176,188,192);

static const rom_image_t rom_image_data RODATA = {
  .s_str = { { CSP_SECT_STR }, 40 },
  .str = {
0,5,'S','t','a','t','e',0,4,'I','N','I','T',0,6,'N',
'O','R','M','A','L',0,8,'F','A','I','L','S','A','F','E',0,0,0,
(char)0xff,21,16,},
  .s_decl = { { CSP_SECT_DECL }, 32 },
  .decl = {
  {.va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=2,.vt=V_INTEGER,.res=31,.init={.u=0}}},
  {.s6={.type=DECL_STATES,.cont=0,.local=0,.dir=0,.name=9,.name2=15,.name3=23,.name4=0,.name5=0,.name6=0}},
  {.type=DECL_END,.cont=0,.local=0,.dir=0,.name=33,.vt=V_INTEGER,.res=31},
  {.em={.type=DECL_END_MARK,.crc=62402,._res=0}},
  },
  .s_instr = { { CSP_SECT_INSTR }, 4 },
  .instr = {
  {.em={.op=OP_END_MARK,.crc=31943,._res=0}},
  },
  .s_idg = { { CSP_SECT_IDG }, 4 },
  .idg = {0},
  .s_ofs = { { CSP_SECT_OFS }, 4 },
  .ofs = {0},
  .s_edg = { { CSP_SECT_EDG }, 4 },
  .edg = {0},
  .hdr = {
    .magic = { CSP_IMAGE_MAGIC0, CSP_IMAGE_MAGIC1, CSP_IMAGE_MAGIC2, CSP_IMAGE_MAGIC3 },
    .size=192, .version=10, .role=0, .generation=0,
    .n_str=34, .n_decl=3, .n_instr=0, .n_edg=0,
    .crc_str=4117, .crc_decl=47180, .crc_instr=65535, .crc_graph=0,
    .ofs_str=64, .ofs_decl=112, .ofs_instr=152, .ofs_idg=164,
    .ofs_ofs=176, .ofs_edg=188,
    .crc_hdr=26148 }
};
const csp_image_ref_t rom_image RODATA = { (const uint8_t*)&rom_image_data };
CSP_REGISTER_IMAGE(rom_image_data);
