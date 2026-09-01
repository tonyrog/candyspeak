// Generated CandySpeak image (rom_*) -- do not edit.
//   source:  examples/empty.csp
// modified:  Wed Jul 22 21:40:41 2026
//   version: 1.0-63-g5e19b12-dirty
//   built:   Sep  1 2026 19:24:59
//   size:    33 instr, 10 decl, 56 str, 3 states

#include "csp.h"
#if ROM_FORMAT_VERSION != 16
#error "rom.c is stale: generated for ROM format 16, csp.h is newer -- regenerate with 'csp -C'"
#endif

CSP_IMAGE_TYPE(rom_image_t, 3,11,34,1,1,1);
CSP_IMAGE_CHECK(rom_image_t, 64,76,172,316,328,340,344);

static const rom_image_t rom_image_data RODATA = {
  .s_str = { { CSP_SECT_STR }, 4 },
  .str = {

(char)0xff,255,255,},
  .s_decl = { { CSP_SECT_DECL }, 88 },
  .decl = {
  {.va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=1,.vt=V_INTEGER,.res=31,.init={.u=0}}},
  {.s6={.type=DECL_STATES,.cont=0,.local=0,.dir=0,.name=2,.name2=3,.name3=4,.name4=0,.name5=0,.name6=0}},
  {.md={.type=DECL_MODULE,.cont=0,.local=0,.dir=0,.name=5,.vt=V_INTEGER,.res=31,.n=4,.ent=0}},
  {.va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=6,.vt=V_UNSIGNED,.res=31,.init={.u=0}}},
  {.cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=7,.vt=V_UNSIGNED,.res=31,.init={.u=0}}},
  {.cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=8,.vt=V_STRING,.res=31,.init={.u=0}}},
  {.va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=9,.vt=V_UNSIGNED,.res=31,.init={.u=0}}},
  {.type=DECL_END,.cont=0,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31},
  {.mq={.type=DECL_OBJECT,.cont=0,.local=0,.dir=0,.name=10,.vt=V_INTEGER,.res=31,.mx=2,.m=1}},
  {.type=DECL_END,.cont=0,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31},
  {.em={.type=DECL_END_MARK,.crc=10868,._res=0}},
  },
  .s_instr = { { CSP_SECT_INSTR }, 136 },
  .instr = {
  {.sg={.op=OP_SEGMENT,.num=32,.used=56}},
  {.raw={{5,83,116,97}}},
  {.raw={{116,101,4,73}}},
  {.raw={{78,73,84,6}}},
  {.raw={{78,79,82,77}}},
  {.raw={{65,76,8,70}}},
  {.raw={{65,73,76,83}}},
  {.raw={{65,70,69,3}}},
  {.raw={{83,121,115,6}}},
  {.raw={{83,101,114,105}}},
  {.raw={{97,108,2,73}}},
  {.raw={{100,4,78,97}}},
  {.raw={{109,101,5,73}}},
  {.raw={{109,97,103,101}}},
  {.raw={{3,115,121,115}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.raw={{0,0,0,0}}},
  {.em={.op=OP_END_MARK,.crc=46458,._res=0}},
  },
  .s_idg = { { CSP_SECT_IDG }, 4 },
  .idg = {0},
  .s_ofs = { { CSP_SECT_OFS }, 4 },
  .ofs = {0},
  .s_edg = { { CSP_SECT_EDG }, 4 },
  .edg = {0},
  .hdr = {
    .magic = { CSP_IMAGE_MAGIC0, CSP_IMAGE_MAGIC1, CSP_IMAGE_MAGIC2, CSP_IMAGE_MAGIC3 },
    .size=344, .version=16, .role=0, .generation=0,
    .n_str=0, .n_decl=10, .n_instr=33, .n_edg=0,
    .crc_str=65535, .crc_decl=65077, .crc_instr=29421, .crc_graph=0,
    .ofs_str=64, .ofs_decl=76, .ofs_instr=172, .ofs_idg=316,
    .ofs_ofs=328, .ofs_edg=340,
    .crc_hdr=52551 }
};
const csp_image_ref_t rom_image RODATA = { (const uint8_t*)&rom_image_data };
CSP_REGISTER_IMAGE(rom_image_data);
