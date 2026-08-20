// Generated CandySpeak image (rom_*) -- do not edit.
//   source:  examples/empty.csp
// modified:  Wed Jul 22 21:40:41 2026
//   version: unknown
//   built:   Aug 20 2026 19:09:19
//   size:    0 instr, 9 decl, 64 str, 3 states

#include "csp.h"
#if ROM_FORMAT_VERSION != 13
#error "rom.c is stale: generated for ROM format 13, csp.h is newer -- regenerate with 'csp -C'"
#endif

CSP_IMAGE_TYPE(rom_image_t, 67,10,1,1,1,1);
CSP_IMAGE_CHECK(rom_image_t, 64,140,228,240,252,264,268);

static const rom_image_t rom_image_data RODATA = {
  .s_str = { { CSP_SECT_STR }, 68 },
  .str = {
0,5,'S','t','a','t','e',0,4,'I','N','I','T',0,6,'N',
'O','R','M','A','L',0,8,'F','A','I','L','S','A','F','E',0,3,'S','y','s',0,6,'S','e','r','i','a','l',0,2,'I','d',
0,4,'N','a','m','e',0,0,0,3,'s','y','s',0,0,0,
(char)0xff,89,10,},
  .s_decl = { { CSP_SECT_DECL }, 80 },
  .decl = {
  {.va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=2,.vt=V_INTEGER,.res=31,.init={.u=0}}},
  {.s6={.type=DECL_STATES,.cont=0,.local=0,.dir=0,.name=9,.name2=15,.name3=23,.name4=0,.name5=0,.name6=0}},
  {.md={.type=DECL_MODULE,.cont=0,.local=0,.dir=0,.name=33,.vt=V_INTEGER,.res=31,.n=3,.ent=0}},
  {.va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=38,.vt=V_UNSIGNED,.res=31,.init={.u=0}}},
  {.cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=46,.vt=V_UNSIGNED,.res=31,.init={.u=0}}},
  {.cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=50,.vt=V_STRING,.res=31,.init={.u=0}}},
  {.type=DECL_END,.cont=0,.local=0,.dir=0,.name=56,.vt=V_INTEGER,.res=31},
  {.mq={.type=DECL_OBJECT,.cont=0,.local=0,.dir=0,.name=58,.vt=V_INTEGER,.res=31,.mx=2,.m=1}},
  {.type=DECL_END,.cont=0,.local=0,.dir=0,.name=63,.vt=V_INTEGER,.res=31},
  {.em={.type=DECL_END_MARK,.crc=9885,._res=0}},
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
    .size=268, .version=13, .role=0, .generation=0,
    .n_str=64, .n_decl=9, .n_instr=0, .n_edg=0,
    .crc_str=2649, .crc_decl=5918, .crc_instr=65535, .crc_graph=0,
    .ofs_str=64, .ofs_decl=140, .ofs_instr=228, .ofs_idg=240,
    .ofs_ofs=252, .ofs_edg=264,
    .crc_hdr=28920 }
};
const csp_image_ref_t rom_image RODATA = { (const uint8_t*)&rom_image_data };
CSP_REGISTER_IMAGE(rom_image_data);
