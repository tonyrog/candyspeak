// micro-csp meets the runtime. THE BRIDGE, and the only one: a board that runs
// the words as bytecode and a board that links them as C differ in this object
// file and nowhere else. Callers include gen/csp_words.h, which is prototypes,
// and are not entitled to know which back end answered.
//
// THE LEAVES are what this file really is. A word can only reach outside its
// own bytes two ways, and both of them are here:
//
//   a NATIVE -- an ordinary C function in the leaf table, generated from a
//   {native, ...} line in utils/words.terms;
//
//   a RECORD HOOK -- a pointer to a declaration, instruction, buffer or view
//   that is already in RAM. Bit-fields cannot be read through a PROGMEM
//   pointer, so the record has to arrive first; csp_decl_ref owns that copy
//   and its cache, and a second copy here would be a second thing to keep in
//   step with it.
//
// Everything else a word does, it does out of its own token stream.

#include "csp.h"
#include "csp_mcsp.h"
#include "csp_print.h"   // the pure natives the words call
#include "csp_words.h"

#if defined(CSP_WORDS_BC)

// The stacks. One set, sized once: on a 2K part these are RAM the pool does
// not get (see doc/AVR_CODE_SIZE.md -- the pool and the stack are the same
// memory). The generator knows every word's frame, and csp_word_run refuses an
// entry that would not fit rather than writing past the end.
#ifndef CSP_WORD_DS
#define CSP_WORD_DS 24
#endif
#ifndef CSP_WORD_RS
#define CSP_WORD_RS 8
#endif
#ifndef CSP_WORD_LV
#define CSP_WORD_LV 24
#endif

static mc_cell_t word_ds[CSP_WORD_DS];
static uint16_t  word_rs[CSP_WORD_RS];
static mc_cell_t word_lv[CSP_WORD_LV];

// The last thing that went wrong, and it stays until somebody clears it. A
// word that faults returns 0, which is a perfectly ordinary answer for most of
// them -- without this the failure would be indistinguishable from a result.
uint8_t csp_word_fault;

// --------------------------------------------------------------- the hooks

static csp_instr_t word_instr;

static const void* word_decl_hook(void* ctx, mc_cell_t i)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    // csp_decl_ref does not bound the top: past nd it reads RAM declarations
    // that are not there. A word checks its own index before it asks -- this
    // is what happens when one does not.
    if ((index_t)i >= st->ps.nd)
	return NULL;
    return csp_decl_ref(st, (index_t)i);
}

static const void* word_instr_hook(void* ctx, mc_cell_t n)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if ((index_t)n >= st->ps.nn)
	return NULL;
    // ONE slot, overwritten per access: a word reads a field and is done with
    // it, and the alternative is a cache that has to be invalidated by every
    // writer in the runtime.
    csp_load_instr(st, (index_t)n, &word_instr);
    return &word_instr;
}

static const void* word_buf_hook(void* ctx, mc_cell_t b)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if (((index_t)b >= st->nbuf) || (st->buf == NULL))
	return NULL;
    return &st->buf[b];
}

static const void* word_view_hook(void* ctx, mc_cell_t v)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if (((index_t)v >= st->view_cap) || (st->view == NULL))
	return NULL;
    return &st->view[v];
}

// THE WRITE SIDE. A different pointer from the read side, on purpose: a read
// of a ROM declaration comes back out of st->dcache, which is a RAM copy that
// the next cache miss overwrites. A write there would land in the copy and
// disappear with it, and nothing would say so. These reach the RAM slots and
// refuse anything below the ROM base -- an image is in flash and is not
// writable from a word or from anywhere else.
//
// Nothing to invalidate afterwards: the cache only ever holds ROM records, and
// those are exactly what these refuse.
static void* word_decl_wr(void* ctx, mc_cell_t i)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if (((index_t)i < st->rom_nd) || ((index_t)i >= st->ps.nd))
	return NULL;
    return ram_decl_at(st, (index_t)i);
}

static void* word_instr_wr(void* ctx, mc_cell_t n)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if (((index_t)n < st->rom_nn) || ((index_t)n >= st->ps.nn))
	return NULL;
    return ram_instr_at(st, (index_t)n);
}

// Buffers and views are plain RAM either way, so the write hook is the read
// hook without the const -- but it is still its OWN hook, because the day one
// of them grows a cache is the day the two have to differ.
static void* word_buf_wr(void* ctx, mc_cell_t b)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if (((index_t)b >= st->nbuf) || (st->buf == NULL))
	return NULL;
    return &st->buf[b];
}

static void* word_view_wr(void* ctx, mc_cell_t v)
{
    csp_rt_t* st = (csp_rt_t*)ctx;

    if (((index_t)v >= st->view_cap) || (st->view == NULL))
	return NULL;
    return &st->view[v];
}

// -------------------------------------------------------------- the caller

static mc_cell_t csp_word_run(csp_rt_t* st, uint16_t entry,
			      const mc_cell_t* args, uint8_t nargs,
			      uint8_t frame);

#define CSP_WORDS_TRAMPOLINES
#include "csp_words_bc.h"

void csp_words_init(csp_rt_t* st)
{
    (void)st;
    mc_decl_hook  = word_decl_hook;
    mc_instr_hook = word_instr_hook;
    mc_buf_hook   = word_buf_hook;
    mc_view_hook  = word_view_hook;
    mc_state_hook = csp_word_state;    // generated; see utils/words.terms
    mc_decl_wr    = word_decl_wr;
    mc_instr_wr   = word_instr_wr;
    mc_buf_wr     = word_buf_wr;
    mc_view_wr    = word_view_wr;
    mc_state_set  = csp_word_state_set;
    mc_array_hook = csp_word_array;
    mc_array_set  = csp_word_array_set;
    csp_word_fault = 0;
}

static mc_cell_t csp_word_run(csp_rt_t* st, uint16_t entry,
			      const mc_cell_t* args, uint8_t nargs,
			      uint8_t frame)
{
    mc_vm_t vm;
    mc_cell_t r;
    int e;

    if (frame > CSP_WORD_LV) {
	csp_word_fault = MC_E_BOUNDS;
	return 0;
    }
    memset(&vm, 0, sizeof(vm));
    vm.code = csp_words_bc;
    vm.code_len = (uint16_t)sizeof(csp_words_bc);
    vm.ds = word_ds;  vm.ds_size = CSP_WORD_DS;
    vm.rs = word_rs;  vm.rs_size = CSP_WORD_RS;
    vm.lv = word_lv;  vm.lv_size = CSP_WORD_LV;
    vm.leaf = csp_word_leaves;    vm.nleaf = csp_word_leaves_N;
    vm.leafn = csp_word_leavesn;  vm.nleafn = csp_word_leavesn_N;
    vm.ctx = st;
    vm.arg = args;
    vm.nargs = nargs;
    r = 0;
    if ((e = csp_mcsp_run(&vm, entry, &r)) != MC_OK) {
	csp_word_fault = (uint8_t)e;
	return 0;
    }
    return r;
}

#else  /* the C back end */

// Nothing to install: the C bodies reach the runtime by calling it.
uint8_t csp_word_fault;

void csp_words_init(csp_rt_t* st)
{
    (void)st;
    csp_word_fault = 0;
}

#include "csp_words_c.h"

#endif
