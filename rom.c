#include "csp.h"
const char rom_str_len RODATA = 24;
const char rom_str[24] RODATA = {
0,5,'S','t','a','t','e',0,4,'I','N','I','T',0,6,'N',
'O','R','M','A','L',0,0,0,};
const int rom_n_decl RODATA = 2;
const csp_decl_t rom_decl[2] RODATA = {
  {.va={.type=DECL_VARIABLE,.dir=0,.name=2,.vt=V_INTEGER,.res=31,.init={.u=0}}},
  {.type=DECL_END,.dir=0,.name=23,.vt=V_INTEGER,.res=31},
};
const int rom_n_instr RODATA = 0;
const csp_instr_t rom_instr[0] RODATA = {
};
