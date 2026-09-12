// Generated CandySpeak image (rom_*) -- do not edit.
//   source:  examples/empty.csp
// modified:  Wed Jul 22 21:40:41 2026
//   version: b
//   built:   Sep 12 2026 22:36:28
//   size:    33 instr, 11 decl, 61 str, 3 states

#include "csp.h"
#if ROM_FORMAT_VERSION != 19
#error "rom.c is stale: generated for ROM format 19, csp.h is newer -- regenerate with 'csp -C'"
#endif

CSP_IMAGE_TYPE(rom_image_t, 3,12,34,1,1,1);
CSP_IMAGE_CHECK(rom_image_t, 64,76,180,324,336,348,352);

static const rom_image_t rom_image_data RODATA = {
  .s_str = { { CSP_SECT_STR }, 4 },
  .str = {

(char)0xff,255,255,},
  .s_decl = { { CSP_SECT_DECL }, 96 },
  .decl = {
  {0x01,0x01,0x1f,0x01,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=1,.vt=V_INTEGER,.res=31,.init={.u=0}} */
  {0x06,0x02,0x03,0x04,0x00,0x00,0x00,0x00},  /* .s6={.type=DECL_STATES,.cont=0,.local=0,.dir=0,.name=2,.name2=3,.name3=4,.name4=0,.name5=0,.name6=0} */
  {0x03,0x05,0x1f,0x01,0x05,0x00,0x00,0x00},  /* .md={.type=DECL_MODULE,.cont=0,.local=0,.dir=0,.name=5,.vt=V_INTEGER,.res=31,.n=5,.ent=0} */
  {0x01,0x06,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=6,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x22,0x07,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=7,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x22,0x08,0x1f,0x04,0x00,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=8,.vt=V_STRING,.res=31,.init={.u=0}} */
  {0x01,0x09,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=9,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x22,0x0a,0x1f,0x02,0xff,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=10,.vt=V_UNSIGNED,.res=31,.init={.u=255}} */
  {0x04,0x00,0x1f,0x01,0x00,0x00,0x00,0x00},  /* .type=DECL_END,.cont=0,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31 */
  {0x05,0x0b,0x1f,0x01,0x02,0x00,0x01,0x00},  /* .mq={.type=DECL_OBJECT,.cont=0,.local=0,.dir=0,.name=11,.vt=V_INTEGER,.res=31,.mx=2,.m=1} */
  {0x04,0x00,0x1f,0x01,0x00,0x00,0x00,0x00},  /* .type=DECL_END,.cont=0,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31 */
  {0x0f,0x00,0x00,0x00,0xcf,0x3b,0x00,0x00},  /* .em={.type=DECL_END_MARK,.crc=15311,._res=0} */
  },
  .s_instr = { { CSP_SECT_INSTR }, 136 },
  .instr = {
  {0x11,0x08,0x3d,0x00},  /* .sg={.op=OP_SEGMENT,.num=32,.used=61} */
  {0x05,'S','t','a'},  /* segment payload */
  {'t','e',0x04,'I'},  /* segment payload */
  {'N','I','T',0x06},  /* segment payload */
  {'N','O','R','M'},  /* segment payload */
  {'A','L',0x08,'F'},  /* segment payload */
  {'A','I','L','S'},  /* segment payload */
  {'A','F','E',0x03},  /* segment payload */
  {'S','y','s',0x06},  /* segment payload */
  {'S','e','r','i'},  /* segment payload */
  {'a','l',0x02,'I'},  /* segment payload */
  {'d',0x04,'N','a'},  /* segment payload */
  {'m','e',0x05,'I'},  /* segment payload */
  {'m','a','g','e'},  /* segment payload */
  {0x04,'B','o','o'},  /* segment payload */
  {'t',0x03,'s','y'},  /* segment payload */
  {'s',0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x3f,0x00,0x63,0x2f},  /* .em={.op=OP_END_MARK,.crc=12131,._res=0} */
  },
  .s_idg = { { CSP_SECT_IDG }, 4 },
  .idg = {0},
  .s_ofs = { { CSP_SECT_OFS }, 4 },
  .ofs = {0},
  .s_edg = { { CSP_SECT_EDG }, 4 },
  .edg = {0},
  .hdr = {
    .magic = { CSP_IMAGE_MAGIC0, CSP_IMAGE_MAGIC1, CSP_IMAGE_MAGIC2, CSP_IMAGE_MAGIC3 },
    .size=352, .version=19, .role=0, .generation=0,
    .n_str=0, .n_decl=11, .n_instr=33, .n_edg=0,
    .crc_str=65535, .crc_decl=4984, .crc_instr=43945, .crc_graph=0,
    .ofs_str=64, .ofs_decl=76, .ofs_instr=180, .ofs_idg=324,
    .ofs_ofs=336, .ofs_edg=348,
    .crc_hdr=40850 }
};
const csp_image_ref_t rom_image RODATA = { (const uint8_t*)&rom_image_data };
CSP_REGISTER_IMAGE(rom_image_data);
