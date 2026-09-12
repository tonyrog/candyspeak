// Generated CandySpeak image (rom_*) -- do not edit.
//   source:  examples/cpx_rotate.csp
// modified:  Sun Aug 16 21:18:53 2026
//   version: b
//   built:   Sep 12 2026 22:36:28
//   size:    180 instr, 53 decl, 227 str, 3 states

#include "csp.h"
#if ROM_FORMAT_VERSION != 19
#error "rom.c is stale: generated for ROM format 19, csp.h is newer -- regenerate with 'csp -C'"
#endif

CSP_IMAGE_TYPE(rom_image_t, 3,54,181,1,1,1);
CSP_IMAGE_CHECK(rom_image_t, 64,76,516,1248,1260,1272,1276);

static const rom_image_t rom_image_data RODATA = {
  .s_str = { { CSP_SECT_STR }, 4 },
  .str = {

(char)0xff,255,255,},
  .s_decl = { { CSP_SECT_DECL }, 432 },
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
  {0x49,0x0c,0x00,0x01,0x04,0x10,0x00,0x00},  /* .di={.type=DECL_DIGITAL,.cont=0,.local=0,.dir=1,.name=12,.vt=V_INTEGER,.res=0,.pin=4,.port=0,.pullup=0,.pulldown=1,.irq=0,.soft=0} */
  {0x49,0x0d,0x00,0x01,0x05,0x10,0x00,0x00},  /* .di={.type=DECL_DIGITAL,.cont=0,.local=0,.dir=1,.name=13,.vt=V_INTEGER,.res=0,.pin=5,.port=0,.pullup=0,.pulldown=1,.irq=0,.soft=0} */
  {0x49,0x0e,0x00,0x01,0x07,0x08,0x00,0x00},  /* .di={.type=DECL_DIGITAL,.cont=0,.local=0,.dir=1,.name=14,.vt=V_INTEGER,.res=0,.pin=7,.port=0,.pullup=1,.pulldown=0,.irq=0,.soft=0} */
  {0x89,0x0f,0x00,0x01,0x0d,0x00,0x00,0x00},  /* .di={.type=DECL_DIGITAL,.cont=0,.local=0,.dir=2,.name=15,.vt=V_INTEGER,.res=0,.pin=13,.port=0,.pullup=0,.pulldown=0,.irq=0,.soft=0} */
  {0x4a,0x10,0x09,0x01,0x08,0x00,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=0,.local=0,.dir=1,.name=16,.vt=V_INTEGER,.res=9,.pin=8,.port=0,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x4a,0x11,0x09,0x01,0x09,0x00,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=0,.local=0,.dir=1,.name=17,.vt=V_INTEGER,.res=9,.pin=9,.port=0,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x4a,0x12,0x09,0x01,0x00,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=0,.local=0,.dir=1,.name=18,.vt=V_INTEGER,.res=9,.pin=0,.port=8,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x4a,0x13,0x09,0x01,0x01,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=0,.local=0,.dir=1,.name=19,.vt=V_INTEGER,.res=9,.pin=1,.port=8,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x4a,0x14,0x09,0x01,0x02,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=0,.local=0,.dir=1,.name=20,.vt=V_INTEGER,.res=9,.pin=2,.port=8,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x8a,0x15,0x0f,0x02,0x80,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=0,.local=0,.dir=2,.name=21,.vt=V_UNSIGNED,.res=15,.pin=0,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x81,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=1,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x82,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=2,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x83,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=3,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x84,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=4,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x85,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=5,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x86,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=6,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x87,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=7,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x88,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=8,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x9a,0x00,0x0f,0x02,0x89,0x04,0x00,0x00},  /* .an={.type=DECL_ANALOG,.cont=1,.local=0,.dir=2,.name=0,.vt=V_UNSIGNED,.res=15,.pin=9,.port=9,.pwm=0,.endian=0,.irq=0,.soft=0} */
  {0x22,0x16,0x1f,0x01,0x32,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=22,.vt=V_INTEGER,.res=31,.init={.u=50}} */
  {0x22,0x17,0x1f,0x01,0x64,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=23,.vt=V_INTEGER,.res=31,.init={.u=100}} */
  {0x22,0x18,0x1f,0x01,0x01,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=24,.vt=V_INTEGER,.res=31,.init={.u=1}} */
  {0x22,0x19,0x1f,0x01,0x01,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=1,.dir=0,.name=25,.vt=V_INTEGER,.res=31,.init={.u=1}} */
  {0x08,0x1a,0x1f,0x08,0x32,0x00,0x00,0x80},  /* .tm={.type=DECL_TIMER,.cont=0,.local=0,.dir=0,.name=26,.vt=V_TIMER,.res=31,.period=50,.init=1} */
  {0x01,0x00,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=0,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x08,0x1b,0x1f,0x08,0x64,0x00,0x00,0x80},  /* .tm={.type=DECL_TIMER,.cont=0,.local=0,.dir=0,.name=27,.vt=V_TIMER,.res=31,.period=100,.init=1} */
  {0x01,0x00,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=0,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x01,0x1c,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=28,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x01,0x1d,0x1f,0x01,0x01,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=29,.vt=V_INTEGER,.res=31,.init={.u=1}} */
  {0x01,0x1e,0x1f,0x02,0x00,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=30,.vt=V_UNSIGNED,.res=31,.init={.u=0}} */
  {0x01,0x1f,0x1f,0x01,0x01,0x00,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=31,.vt=V_INTEGER,.res=31,.init={.u=1}} */
  {0x02,0x20,0x1f,0x01,0xff,0xff,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=0,.dir=0,.name=32,.vt=V_INTEGER,.res=31,.init={.u=65535}} */
  {0x02,0x21,0x1f,0x01,0xe0,0xf8,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=0,.dir=0,.name=33,.vt=V_INTEGER,.res=31,.init={.u=63712}} */
  {0x02,0x22,0x1f,0x01,0xe0,0x07,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=0,.dir=0,.name=34,.vt=V_INTEGER,.res=31,.init={.u=2016}} */
  {0x02,0x23,0x1f,0x01,0x1f,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=0,.dir=0,.name=35,.vt=V_INTEGER,.res=31,.init={.u=31}} */
  {0x02,0x24,0x1f,0x01,0x20,0xfd,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=0,.dir=0,.name=36,.vt=V_INTEGER,.res=31,.init={.u=64800}} */
  {0x02,0x25,0x1f,0x01,0xe0,0xf8,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=0,.local=0,.dir=0,.name=37,.vt=V_INTEGER,.res=31,.init={.u=63712}} */
  {0x12,0x00,0x1f,0x01,0xe0,0x07,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=1,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31,.init={.u=2016}} */
  {0x12,0x00,0x1f,0x01,0x1f,0x00,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=1,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31,.init={.u=31}} */
  {0x12,0x00,0x1f,0x01,0x20,0xfd,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=1,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31,.init={.u=64800}} */
  {0x12,0x00,0x1f,0x01,0xff,0xff,0x00,0x00},  /* .cn={.type=DECL_CONSTANT,.cont=1,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31,.init={.u=65535}} */
  {0x01,0x26,0x0f,0x01,0xff,0xff,0x00,0x00},  /* .va={.type=DECL_VARIABLE,.cont=0,.local=0,.dir=0,.name=38,.vt=V_INTEGER,.res=15,.init={.u=65535}} */
  {0x04,0x00,0x1f,0x01,0x00,0x00,0x00,0x00},  /* .type=DECL_END,.cont=0,.local=0,.dir=0,.name=0,.vt=V_INTEGER,.res=31 */
  {0x0f,0x00,0x00,0x00,0x2b,0xbf,0x00,0x00},  /* .em={.type=DECL_END_MARK,.crc=48939,._res=0} */
  },
  .s_instr = { { CSP_SECT_INSTR }, 724 },
  .instr = {
  {0x11,0x08,0x79,0x00},  /* .sg={.op=OP_SEGMENT,.num=32,.used=121} */
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
  {'s',0x04,'B','t'},  /* segment payload */
  {'n','A',0x04,'B'},  /* segment payload */
  {'t','n','B',0x06},  /* segment payload */
  {'S','w','i','t'},  /* segment payload */
  {'c','h',0x03,'L'},  /* segment payload */
  {'e','d',0x05,'L'},  /* segment payload */
  {'i','g','h','t'},  /* segment payload */
  {0x04,'T','e','m'},  /* segment payload */
  {'p',0x04,'A','c'},  /* segment payload */
  {'c','X',0x04,'A'},  /* segment payload */
  {'c','c','Y',0x04},  /* segment payload */
  {'A','c','c','Z'},  /* segment payload */
  {0x01,'P',0x0a,'S'},  /* segment payload */
  {'t','e','p','P'},  /* segment payload */
  {'e','r','i','o'},  /* segment payload */
  {'d',0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x11,0x08,0x63,0x00},  /* .sg={.op=OP_SEGMENT,.num=32,.used=99} */
  {0x0b,'C','o','l'},  /* segment payload */
  {'o','r','P','e'},  /* segment payload */
  {'r','i','o','d'},  /* segment payload */
  {0x07,'S','t','e'},  /* segment payload */
  {'p','D','i','r'},  /* segment payload */
  {0x08,'C','o','l'},  /* segment payload */
  {'o','r','D','i'},  /* segment payload */
  {'r',0x08,'S','t'},  /* segment payload */
  {'e','p','T','i'},  /* segment payload */
  {'c','k',0x09,'C'},  /* segment payload */
  {'o','l','o','r'},  /* segment payload */
  {'T','i','c','k'},  /* segment payload */
  {0x02,'P','i',0x02},  /* segment payload */
  {'P','t',0x02,'C'},  /* segment payload */
  {'i',0x02,'C','t'},  /* segment payload */
  {0x05,'W','H','I'},  /* segment payload */
  {'T','E',0x03,'R'},  /* segment payload */
  {'E','D',0x05,'G'},  /* segment payload */
  {'R','E','E','N'},  /* segment payload */
  {0x04,'B','L','U'},  /* segment payload */
  {'E',0x05,'A','M'},  /* segment payload */
  {'B','E','R',0x05},  /* segment payload */
  {'C','O','L','O'},  /* segment payload */
  {'R',0x05,'C','o'},  /* segment payload */
  {'l','o','r',0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x00,0x00,0x00,0x00},  /* segment payload */
  {0x2c,0x00,0x00,0x00},  /* .m={.op=OP_LD,.x=0,.mem=0,.y=0} */
  {0x38,0x00,0x54,0x00},  /* .in={.op=OP_INSTATE,.x=0,.imm=0,.nxt=21,.implicit=0} */
  {0x32,0xfc,0xff,0x03},  /* .i={.op=OP_LI,.x=0,.imm=-1} */
  {0x27,0x0c,0x00,0x00},  /* .r={.op=OP_RULE,.cnd=0,.nxt=3,.implicit=0} */
  {0x2c,0x40,0x07,0x00},  /* .m={.op=OP_LD,.x=0,.mem=29,.y=0} */
  {0x2f,0x60,0x08,0x00},  /* .m={.op=OP_STP,.x=0,.mem=33,.y=8} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x32,0xfc,0xff,0x03},  /* .i={.op=OP_LI,.x=0,.imm=-1} */
  {0x27,0x0c,0x00,0x00},  /* .r={.op=OP_RULE,.cnd=0,.nxt=3,.implicit=0} */
  {0x2c,0x80,0x07,0x00},  /* .m={.op=OP_LD,.x=0,.mem=30,.y=0} */
  {0x2f,0xe0,0x08,0x00},  /* .m={.op=OP_STP,.x=0,.mem=35,.y=8} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x32,0xfc,0xff,0x03},  /* .i={.op=OP_LI,.x=0,.imm=-1} */
  {0x27,0x0c,0x00,0x00},  /* .r={.op=OP_RULE,.cnd=0,.nxt=3,.implicit=0} */
  {0x2c,0xc0,0x07,0x00},  /* .m={.op=OP_LD,.x=0,.mem=31,.y=0} */
  {0x2e,0x80,0x09,0x00},  /* .m={.op=OP_ST,.x=0,.mem=38,.y=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x32,0xfc,0xff,0x03},  /* .i={.op=OP_LI,.x=0,.imm=-1} */
  {0x27,0x0c,0x00,0x00},  /* .r={.op=OP_RULE,.cnd=0,.nxt=3,.implicit=0} */
  {0x2c,0x00,0x08,0x00},  /* .m={.op=OP_LD,.x=0,.mem=32,.y=0} */
  {0x2e,0x00,0x0a,0x00},  /* .m={.op=OP_ST,.x=0,.mem=40,.y=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x10,0x40,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=33,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x20,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=8,.implicit=1} */
  {0x6c,0x40,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=37,.y=0} */
  {0x32,0x24,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=9} */
  {0x87,0x04,0x04,0x00},  /* .a={.op=OP_ADD,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0x32,0x28,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=10} */
  {0x4b,0x08,0x04,0x00},  /* .a={.op=OP_REM,.x=1,.y=2,.z=0,.u=1,.swap=0} */
  {0x7b,0x28,0x00,0x04},  /* .ox={.op=OP_SETOX,.x=1,.len=10,.stride=1} */
  {0x37,0x00,0x13,0x80},  /* .mi={.op=OP_STI,.x=0,.mem=32787,.imm=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x10,0x40,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=33,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x20,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=8,.implicit=1} */
  {0x6c,0x40,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=37,.y=0} */
  {0x32,0x28,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=10} */
  {0x87,0x04,0x04,0x00},  /* .a={.op=OP_ADD,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0x32,0x28,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=10} */
  {0x4b,0x08,0x04,0x00},  /* .a={.op=OP_REM,.x=1,.y=2,.z=0,.u=1,.swap=0} */
  {0x7b,0x28,0x00,0x04},  /* .ox={.op=OP_SETOX,.x=1,.len=10,.stride=1} */
  {0x37,0x00,0x13,0x80},  /* .mi={.op=OP_STI,.x=0,.mem=32787,.imm=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x10,0x40,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=33,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x20,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=8,.implicit=1} */
  {0x6c,0x40,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=37,.y=0} */
  {0x32,0x04,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=1} */
  {0x87,0x04,0x04,0x00},  /* .a={.op=OP_ADD,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0x32,0x28,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=10} */
  {0x4b,0x08,0x04,0x00},  /* .a={.op=OP_REM,.x=1,.y=2,.z=0,.u=1,.swap=0} */
  {0x7b,0x28,0x00,0x04},  /* .ox={.op=OP_SETOX,.x=1,.len=10,.stride=1} */
  {0x37,0x00,0x13,0x80},  /* .mi={.op=OP_STI,.x=0,.mem=32787,.imm=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x10,0x40,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=33,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x20,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=8,.implicit=1} */
  {0x6c,0x40,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=37,.y=0} */
  {0x32,0x08,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=2} */
  {0x87,0x04,0x04,0x00},  /* .a={.op=OP_ADD,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0x32,0x28,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=10} */
  {0x4b,0x08,0x04,0x00},  /* .a={.op=OP_REM,.x=1,.y=2,.z=0,.u=1,.swap=0} */
  {0x7b,0x28,0x00,0x04},  /* .ox={.op=OP_SETOX,.x=1,.len=10,.stride=1} */
  {0x37,0x00,0x13,0x80},  /* .mi={.op=OP_STI,.x=0,.mem=32787,.imm=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x10,0x40,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=33,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x1c,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=7,.implicit=1} */
  {0x6c,0x40,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=37,.y=0} */
  {0x32,0x28,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=10} */
  {0x8b,0x04,0x04,0x00},  /* .a={.op=OP_REM,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0x2c,0xc0,0x0c,0x00},  /* .m={.op=OP_LD,.x=0,.mem=51,.y=0} */
  {0xbb,0x28,0x00,0x04},  /* .ox={.op=OP_SETOX,.x=2,.len=10,.stride=1} */
  {0x2e,0xc0,0x04,0x20},  /* .m={.op=OP_ST,.x=0,.mem=32787,.y=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x10,0x40,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=33,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x18,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=6,.implicit=1} */
  {0x77,0x04,0x21,0x00},  /* .mi={.op=OP_STI,.x=1,.mem=33,.imm=1} */
  {0x6c,0x40,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=37,.y=0} */
  {0x2c,0x80,0x09,0x00},  /* .m={.op=OP_LD,.x=0,.mem=38,.y=0} */
  {0x87,0x04,0x04,0x00},  /* .a={.op=OP_ADD,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0xae,0x40,0x09,0x00},  /* .m={.op=OP_ST,.x=2,.mem=37,.y=0} */
  {0xa8,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=2} */
  {0x10,0xc0,0x08,0x00},  /* .m={.op=OP_TMO,.x=0,.mem=35,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x30,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=12,.implicit=1} */
  {0x77,0x04,0x23,0x00},  /* .mi={.op=OP_STI,.x=1,.mem=35,.imm=1} */
  {0x6c,0xc0,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=39,.y=0} */
  {0x32,0x14,0x00,0x00},  /* .i={.op=OP_LI,.x=0,.imm=5} */
  {0x8b,0x04,0x04,0x00},  /* .a={.op=OP_REM,.x=2,.y=1,.z=0,.u=1,.swap=0} */
  {0xbb,0x14,0x00,0x04},  /* .ox={.op=OP_SETOX,.x=2,.len=5,.stride=1} */
  {0x2c,0x80,0x0b,0x20},  /* .m={.op=OP_LD,.x=0,.mem=32814,.y=0} */
  {0x2e,0xc0,0x0c,0x00},  /* .m={.op=OP_ST,.x=0,.mem=51,.y=0} */
  {0x2c,0xc0,0x09,0x00},  /* .m={.op=OP_LD,.x=0,.mem=39,.y=0} */
  {0xac,0x00,0x0a,0x00},  /* .m={.op=OP_LD,.x=2,.mem=40,.y=0} */
  {0x47,0x80,0x04,0x00},  /* .a={.op=OP_ADD,.x=1,.y=0,.z=2,.u=1,.swap=0} */
  {0x6e,0xc0,0x09,0x00},  /* .m={.op=OP_ST,.x=1,.mem=39,.y=0} */
  {0x68,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=1} */
  {0x2c,0x80,0x02,0x00},  /* .m={.op=OP_LD,.x=0,.mem=10,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x10,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=4,.implicit=1} */
  {0x6c,0x80,0x09,0x00},  /* .m={.op=OP_LD,.x=1,.mem=38,.y=0} */
  {0x03,0x04,0x00,0x00},  /* .a={.op=OP_NEG,.x=0,.y=1,.z=0,.u=0,.swap=0} */
  {0x2e,0x80,0x09,0x00},  /* .m={.op=OP_ST,.x=0,.mem=38,.y=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x2c,0xc0,0x02,0x00},  /* .m={.op=OP_LD,.x=0,.mem=11,.y=0} */
  {0x44,0x00,0x00,0x00},  /* .a={.op=OP_MOV,.x=1,.y=0,.z=0,.u=0,.swap=0} */
  {0x67,0x10,0x00,0x02},  /* .r={.op=OP_RULE,.cnd=1,.nxt=4,.implicit=1} */
  {0x6c,0x00,0x0a,0x00},  /* .m={.op=OP_LD,.x=1,.mem=40,.y=0} */
  {0x03,0x04,0x00,0x00},  /* .a={.op=OP_NEG,.x=0,.y=1,.z=0,.u=0,.swap=0} */
  {0x2e,0x00,0x0a,0x00},  /* .m={.op=OP_ST,.x=0,.mem=40,.y=0} */
  {0x28,0x00,0x00,0x00},  /* .x={.op=OP_NEXT,.x=0} */
  {0x3f,0x00,0x69,0x74},  /* .em={.op=OP_END_MARK,.crc=29801,._res=0} */
  },
  .s_idg = { { CSP_SECT_IDG }, 4 },
  .idg = {0},
  .s_ofs = { { CSP_SECT_OFS }, 4 },
  .ofs = {0},
  .s_edg = { { CSP_SECT_EDG }, 4 },
  .edg = {0},
  .hdr = {
    .magic = { CSP_IMAGE_MAGIC0, CSP_IMAGE_MAGIC1, CSP_IMAGE_MAGIC2, CSP_IMAGE_MAGIC3 },
    .size=1276, .version=19, .role=0, .generation=0,
    .n_str=0, .n_decl=53, .n_instr=180, .n_edg=0,
    .crc_str=65535, .crc_decl=49646, .crc_instr=21160, .crc_graph=0,
    .ofs_str=64, .ofs_decl=76, .ofs_instr=516, .ofs_idg=1248,
    .ofs_ofs=1260, .ofs_edg=1272,
    .crc_hdr=43695 }
};
const csp_image_ref_t rom_image RODATA = { (const uint8_t*)&rom_image_data };
CSP_REGISTER_IMAGE(rom_image_data);
