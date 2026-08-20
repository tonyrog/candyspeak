#ifndef __CSP_DUMP_H__
#define __CSP_DUMP_H__

#include <stdio.h>
#include "csp.h"

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

typedef enum {
    TEXT,
    ERLANG
} csp_lang_t;


extern void    csp_dump(FILE*, csp_rt_t* st);
// Provenance stamped into the generated rom.c so a baked firmware says where it
// came from. src/version/date come from outside (the tool that runs the dump
// knows them; the dumper does not); pass NULL for any that is unknown.
typedef struct {
    const char* src;      // the .csp file this was compiled from
    const char* modified; // the .csp file modification
    const char* version;  // CandySpeak version (git describe / short hash)
    const char* date;     // build date/time
    // Symbol prefix for the emitted image: `rom` gives rom_str/rom_decl/...,
    // `failsafe` gives failsafe_str/failsafe_decl/... NULL means "rom". This is
    // what lets a firmware carry more than one image -- they are the same
    // format and the same generator, told apart only by the name they answer
    // to. The generated file guards itself with <prefix>.c in its messages.
    const char* prefix;
    // What the image is for and which revision of it this is. role picks the
    // slot a boot-time scan will fill from; generation orders two images with
    // the same role (higher is newer), which is how an A/B pair is told apart.
    unsigned role;
    unsigned generation;
} csp_rom_meta_t;
extern void    csp_dump_code(FILE* f, csp_rt_t* st, const csp_rom_meta_t* meta);
extern void    csp_dump_tokens(FILE* f,token_t* tv, int n);
extern void    csp_dump_state(FILE* f, csp_rt_t* st,csp_lang_t lang);
extern void    csp_dump_result(FILE* f,csp_rt_t* st,index_t x,csp_lang_t lang);

extern void    csp_print_expr(FILE*, csp_rt_t* st, index_t x);
extern index_t csp_dump_instr(FILE* f, int lev, csp_rt_t* st, int i,char* eot);
extern index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i, char* eot);

extern void csp_fprint_tag(FILE* f, csp_rt_t* st, index_t n);
extern void csp_print_tag(csp_rt_t* st, index_t n);

extern void csp_list_declarations(FILE* f, csp_rt_t* st);
extern void csp_list_rules(FILE* f, csp_rt_t* st);
extern int  csp_list_rule(csp_rt_t* st, int i);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
