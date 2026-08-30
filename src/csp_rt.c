// CandySpeak runtime
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "csp.h"
#include "csp_strings.h"   // shared RODATA strings (generated from strings.tab)
#include "csp_parse.h"
#include "csp_tok.h"
#include "csp_compile.h"
#include "csp_print.h"
#include "csp_bits.h"
#include "csp_part.h"    // .part bit layout inside a value slot (see tests/part_layout.c)
#ifdef DEBUG
#include "csp_dump.h"
#include <stdio.h>
extern int debug;
#endif

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

// convert integer to -1 if y != 0  0 otherwise
#define BOOL(y) (-((y)!=0))

static const char tag_tab[] RODATA = {
    [DECL_OBJECT] = 'q',
    [DECL_MODULE] = 'm',
    [DECL_CONSTANT] = 'c',
    [DECL_VARIABLE] = 'v',
    [DECL_DIGITAL] = 'd',
    [DECL_ANALOG] = 'a',
    [DECL_TIMER] = 't',
    [DECL_FIELD] = 'f',
};

// --- stack watch ------------------------------------------------------------
// Declarations grow DOWN from the arena top; the stack grows DOWN from RAMEND
// toward the very same address. Nothing enforces a gap between them, and when
// they met the newest declarations were silently overwritten (see TODO). So
// measure the thing that actually matters -- not how deep the stack is, but how
// close it got to the arena -- and keep the WORST value ever seen.
//
// A negative low-water mark means the stack has already been inside the
// declarations. CSP_STACK_RESERVE is only the guess made at boot; this is the
// measurement.
// Diagnostic-only: gated behind CSP_STACK_WATCH so a normal build carries none
// of it (the csp_stack_mark() calls compile to nothing -- see csp.h). Enable via
// the `watch` make target. csp_stack_mark measures how close the stack has come
// to the arena; the -finstrument hooks below find the deepest function.
#ifdef CSP_STACK_WATCH
char* csp_arena_top = NULL;      // set by csp_mem_init
long  csp_stack_low = 0x7fffffffL;
void* csp_stack_low_fn = NULL;   // the function that set the record

void csp_stack_mark(void)
    __attribute__((no_instrument_function));

void csp_stack_mark(void)
{
    char probe;
    long m;
    if (csp_arena_top == NULL)
	return;
    m = (long)(&probe - csp_arena_top);
    if (m < csp_stack_low)
	csp_stack_low = m;
}

// Built with -finstrument-functions, gcc calls these on EVERY function entry
// and exit -- so the deepest point is found by measurement instead of by
// guessing where to put a probe. Costs a call pair per function. Both must
// carry no_instrument_function or they recurse.
//
//   make -f Makefile.mega watch
void __cyg_profile_func_enter(void* fn, void* call)
    __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void* fn, void* call)
    __attribute__((no_instrument_function));

// Records WHICH function was entered at the deepest point. Print it with
// /memory and resolve it with:  avr-nm -C <elf> | sort | grep -i <addr>
// Note avr-gcc reports fn as a BYTE address; nm prints byte addresses too, so
// they compare directly.
void __cyg_profile_func_enter(void* fn, void* call)
{
    char probe;
    long m;
    (void)call;
    if (csp_arena_top == NULL)
	return;
    m = (long)(&probe - csp_arena_top);
    if (m < csp_stack_low) {
	csp_stack_low = m;
	csp_stack_low_fn = fn;
    }
}

void __cyg_profile_func_exit(void* fn, void* call)
{
    (void)fn; (void)call;
}
#endif

NOINLINE int ro_strlen(rostring_t s)
{
    int n = 0;
    if (s) while (ro_byte((const uint8_t*)s + n)) n++;
    return n;
}

NOINLINE int ro_strncmp(const char* a, rostring_t b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
	uint8_t cb = ro_byte((const uint8_t*)b + i);
	uint8_t ca = (uint8_t)a[i];
	if (ca != cb) return (int)ca - (int)cb;
	if (cb == 0) return 0;          // both ended here
    }
    return 0;
}

NOINLINE int ro_strcmp(const char* a, rostring_t b)
{
    rochar* bp = (rochar*)b;
    uint8_t ca, cb;
    do {
	ca = *a++;
	cb = ro_byte(bp++);
	if (ca != cb) return (int)ca - (int)cb;
    } while(cb);
    return 0;
}

NOINLINE int ro_strcpy(char* dst, rostring_t src, int max)
{
    rochar* sp = (rochar*)src;
    int n = 0;
    uint8_t c;
    while ((n < max-1) && (c = ro_byte(sp+n)) != 0)
	dst[n++] = (char)c;
    dst[n] = '\0';
    return n;
}


// The firmware ROM image lives in rom.c, which every build links exactly once
// (an empty default rom.c provides zero-sized stubs when no program is baked
// in). These are plain externs -- NOT weak fallbacks defined here: a weak
// definition in this TU would be bound locally by csp_load_rom and let
// -fdata-sections/--gc-sections drop the strong rom.c copy, so the firmware
// would boot with an empty ROM. rom_n_edg > 0 means the ROM carries its own
// precomputed reactive graph (ROM decl -> ROM rules), consumed by
// csp_enq_elist alongside the runtime RAM graph. Emitted by csp -C -r.
// The firmware's default image, as one descriptor (csp_image_t). The seven
// tables are still separate symbols in rom.c -- rom_image just names them
// together, so nothing below the loader has to know what an image is called.
// The counts and integrity live in the image's header (see csp_image_header_t).
// n_edg > 0 means the image carries its own precomputed reactive graph (emitted
// by csp -C -r). Present in the empty rom.c too (all zero). Read via ro_header
// so a PROGMEM header on AVR is copied out, never dereferenced in place.

// The ROM CRC is a byte-CRC over the RAW flash image (str, decls, instrs,
// states, sizes). That is only reproducible between the host generator and the
// AVR runtime because both are LITTLE-ENDIAN: a big-endian machine packs the
// same bitfields MSB-first (measured: arm-BE decl `05 aa 8f e5` vs LE
// `41 55 e3 57`), so a CRC baked on a BE host would never match an LE target,
// and the board would reject its own ROM. If CandySpeak is ever built on a
// big-endian host, or ported to a big-endian target, this assert fires and the
// fix is canonical field-by-field serialization instead of a raw byte-CRC.
#if defined(__BYTE_ORDER__)
CSP_STATIC_ASSERT(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
    "ROM/EEPROM byte-CRC assumes little-endian; a big-endian host or target "
    "needs canonical field serialization");
#endif

// CRC-16/CCITT, incremental: folds n bytes into `crc` and returns it, so a
// caller can chain several regions (str, then decls, then instrs, ...). is_rom
// selects ro_byte, which is memcpy_P on AVR (the region is PROGMEM) and a plain
// read on the host; pass 0 for ordinary RAM. Table-free. Seed with 0xFFFF.
NOINLINE uint16_t csp_crc16(uint16_t crc, const void* data, size_t n, int is_rom)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t k;
    unsigned b;

    for (k = 0; k < n; k++) {
	uint8_t byte = is_rom ? ro_byte(p + k) : p[k];
	crc ^= (uint16_t)((uint16_t)byte << 8);
	for (b = 0; b < 8; b++)
	    crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
				 : (uint16_t)(crc << 1);
    }
    return crc;
}

#if defined(__AVR__)
// The one definition -- see the declaration in csp.h for why this is not a
// static inline like the other ro_* record readers.
csp_image_header_t ro_header(const csp_image_header_t* p)
{
    csp_image_header_t h;
    memcpy_P(&h, p, sizeof(h));
    return h;
}

csp_image_ref_t ro_ref(const csp_image_ref_t* p)
{
    csp_image_ref_t v;
    memcpy_P(&v, p, sizeof(v));
    return v;
}
#endif

// Segment-aware reads (see csp.h). NOINLINE keeps the flash-copy in one place
// instead of expanding it at every decl()/instr() call site.
NOINLINE csp_decl_t csp_get_decl(csp_rt_t* st, index_t i)
{
    if (i < st->rom_nd)
	return ro_decl(&st->rom_p.decl[i]);
    return st->ram_decl[st->rom_nd - i];   // RAM decls grow down (see ram_decl_at)
}

NOINLINE void csp_copy_decl(csp_rt_t* st, index_t i, csp_decl_t* dst)
{
    if (i < st->rom_nd)
	ro_copy_decl(&st->rom_p.decl[i], dst);
    else
	// RAM decls grow down (see ram_decl_at)
	*dst = st->ram_decl[st->rom_nd - i];
}

NOINLINE csp_instr_t csp_get_instr(csp_rt_t* st, index_t n)
{
    if (n < st->rom_nn)
	return ro_instr(&st->rom_p.instr[n]);
    return st->ram_instr[n - st->rom_nn];
}

const char csp_tag(csp_rt_t* st, index_t n)
{
    return tag_tab[decl(st,INDEX(n),type)];
}

static rostring_t const pindir_tab[] RODATA = {
    [DIR_NONE] = ros_none,
    [DIR_IN]   = ros_in,
    [DIR_OUT]  = ros_out,
    [DIR_INOUT]  = ros_inout
};

rostring_t csp_fmt_pindir(uint8_t dir)
{
    return ro_ptr(&pindir_tab[dir&0x3]);
}

rostring_t csp_fmt_pull(csp_rt_t* st, int ix)
{
    csp_decl_t d = csp_get_decl(st, ix);
    if (d.di.pullup)
	return ros_pullup;
    else if (d.di.pulldown)
	return ros_pulldown;
    else
	return ros_undefined;  // floating
}

rostring_t csp_fmt_pwm(csp_rt_t* st, int ix)
{
    if (decl(st,ix,an.pwm))
	return ros_pwm;
    else
	return ros_undefined;
}

static rostring_t const vtype_tab[] RODATA = {
    [V_VOID] = ros_void,
    [V_INTEGER] = ros_integer,
    [V_UNSIGNED] = ros_unsigned,
    [V_FLOAT] = ros_float,
    [V_STRING] = ros_string,
    [V_INDEX] = ros_index,
    [V_NUMBER] = ros_number,
    [V_ANY] = ros_any,
    [V_DIGITAL] = ros_digital,
    [V_ANALOG] = ros_analog,
    [V_TIMER] = ros_timer,
    [V_FIELD] = ros_field,
};

rostring_t csp_fmt_vtype(vtype_t vt)
{
    return (rostring_t) ro_ptr(&vtype_tab[vt & 0xf]);
}

static rostring_t  const endian_tab[] RODATA = {
    [E_NATIVE] = ros_native,
    [E_LITTLE] = ros_little,
    [E_BIG] = ros_big,
    [0x3] = ros_undefined
};

rostring_t csp_fmt_endian(vendian_t et)
{
    return endian_tab[et&0x3];
}


#define ify(x) #x
#define stringify(x) ify(x)

// Error texts live in strings.tab (s_err_*), so they are in FLASH like every
// other string in that table instead of being RAM literals. static on purpose:
// the returned format is only meaningful to csp_print_error, and handing it to
// printf/csp_print_str reads the wrong address space on AVR.

static rostring_t  const err_tab[] RODATA = {
    [ERR_OK] =                     ros_err_ok,
    [ERR_SYNTAX] =                 ros_err_syntax,
    [ERR_STRING_SPACE_EXHUSTED] =  ros_err_string_space,
    [ERR_TOO_MANY_DECLARATIONS] =  ros_err_many_decls,
    [ERR_TOO_MANY_INSTRUCTIONS] =  ros_err_many_instrs,
    [ERR_TOO_MANY_OBJECTS] =       ros_err_many_objects,
    [ERR_MODULE_NOT_DECLARED] =    ros_err_no_module,
    [ERR_TOO_MANY_STATES] =        ros_err_many_states,
    [ERR_STATE_NOT_DECLARED] =     ros_err_no_state,
    [ERR_NOT_A_MODULE] =           ros_err_not_module,
    [ERR_NOT_A_BUFFER] =           ros_err_not_buffer,
    [ERR_END_MISMATCH] =           ros_err_end_mismatch,
    [ERR_OBJECT_NOT_DECLARED] =    ros_err_no_object,
    [ERR_VARIABLE_NOT_DECLARED] =  ros_err_no_variable,
    [ERR_FIELD_NOT_FOUND] =        ros_err_no_field,
    [ERR_FUNCTION_DOES_NOT_EXIST] =  ros_err_no_function,
    [ERR_ALREADY_DEFINED] =        ros_err_defined,
    [ERR_INTERNAL_ERROR] =         ros_err_internal,
    [ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH] =  ros_err_arg_mismatch,
    [ERR_NAME_TOO_LONG] =          ros_err_name_long,
    [ERR_BAD_RULE_RANGE] =         ros_err_rule_range,
    [ERR_NO_SUCH_RULE] =           ros_err_no_rule,
    [ERR_CANNOT_SAVE] =            ros_err_cannot_save,
    [ERR_CANNOT_LOAD] =            ros_err_cannot_load,
    [ERR_NUMBER_RANGE] =           ros_err_num_range,
    [ERR_OUT_OF_MEMORY] =          ros_err_out_of_memory,
    [ERR_INDEX_RANGE] =            ros_err_index_range,
    [ERR_ASSIGN_TO_LOCAL] =        ros_err_assign_local,
    [ERR_ASSIGN_TO_PARAM] =        ros_err_assign_param,
    [ERR_PARAM_SHAPE] =            ros_err_param_shape,
    [ERR_TOO_MANY_TOKENS] =        ros_err_many_tokens,
    [ERR_TOO_MANY_DEFINES] =       ros_err_many_defines,
};

// err_tab is a designated-initialiser array, so ANY code without a row in it
// is a NULL -- and csp_print_error walked it with ro_byte, which is a read of
// address zero. `#local` with a long expression hit exactly that:
// ERR_TOO_MANY_TOKENS was in the enum and not in the table, so a line that was
// merely too complex crashed the compiler instead of saying so.
//
// The row is there now, but the guard stays: the next error code added will be
// missing from this table too, and it should print something useless rather
// than segfault while reporting a different mistake.
NOINLINE static rostring_t csp_format_error(csp_err_t err)
{
    rostring_t f = ((unsigned)err < (sizeof(err_tab)/sizeof(err_tab[0])))
	? err_tab[err] : NULL;
    return f ? f : ros_err_internal;
}

// Print the current error with its arguments substituted. A light printf:
// %s, %d and %% -- that is every conversion the error texts use.
//
// Needed because embedded has no stdio, and printing the format string raw
// (which is what csp_print_str did) shows the user a literal
// "variable %s is not declared". The format is read with ro_byte so it works
// whether it sits in flash or RAM; the arguments are always plain RAM strings
// (csp_set_err_arg_ix copies ROM names out for exactly this reason).
void csp_print_error(csp_rt_t* st)
{
    rostring_t f = csp_format_error(st->ps.err);
    const uint8_t* fp = (const uint8_t*) f;
    int ai = 0;
    char c;

    while ((c = (char)ro_byte(fp++)) != '\0') {
	if (c != '%') {
	    csp_print_char(c);
	    continue;
	}
	c = (char)ro_byte(fp);
	if (c) fp++;
	switch (c) {
	case 's': {
	    const char* s = (ai < 3) ? (const char*)st->ps.err_args[ai++] : NULL;
	    if (s) csp_print_str(s);
	    break;
	}
	case 'd':
	    csp_print_int((ivalue_t)((ai < 3) ? st->ps.err_args[ai++] : 0));
	    break;
	case '%':
	    csp_print_char('%');
	    break;
	default:                        // unknown conversion: show it verbatim
	    csp_print_char('%');
	    if (c) csp_print_char(c);
	    break;
	}
    }
}



int csp_set_error(csp_rt_t* st, csp_err_t err)
{
    // Don't overwrite a more specific error with generic SYNTAX
    if (st->ps.err == ERR_OK) {
	st->ps.err = err;
	return 1;
    }
    else if ((st->ps.err == ERR_SYNTAX) && (err != ERR_SYNTAX)) {
	st->ps.err = err;
	return 1;
    }
    return 0;
}

void csp_set_err_arg_int(csp_rt_t* st, int i, int ival)
{
    st->ps.err_args[i] = ival;
}

// Token string to temp area (grows down), set error with it.
//
// The temp area and err_strp are RAM-LOCAL (0..MAX_STR_BUF, top of ram_str),
// but ps.strp is a LOGICAL string position that includes rom_strp. Comparing
// them directly meant that with a ROM linked (rom_strp = 130 for cpx) the guard
// read `err_strp(128) >= 130+...` = false, so no error arg was ever stored and
// every "%s" in a message printed blank on the board. Subtract rom_strp to get
// the RAM string usage the err area actually shares space with. (All three
// csp_set_err_arg_* had this.)
void csp_set_err_arg_tstr(csp_rt_t* st, int i, const tstr_t* str)
{
    if (st->ps.err_strp >= (st->ps.strp - st->rom_strp) + (uint32_t)str->len + 1) {
	st->ps.err_strp -= str->len + 1;
	memcpy(&st->ram_str[st->ps.err_strp], str->ptr, str->len);
	st->ram_str[st->ps.err_strp + str->len] = '\0';
	st->ps.err_args[i] = (uintptr_t)&st->ram_str[st->ps.err_strp];
    }
}

// Same, for a RODATA string. Copied byte by byte for the same reason
// csp_set_err_arg_ix is: every err_arg has to end up a plain RAM string,
// because neither fprintf nor csp_print_error can tell the segments apart.
void csp_set_err_arg_rostr(csp_rt_t* st, int i, rostring_t str)
{
    int len = ro_strlen(str);

    if (st->ps.err_strp >= (st->ps.strp - st->rom_strp) + (uint32_t)len + 1) {
	st->ps.err_strp -= len + 1;
	ro_strcpy(&st->ram_str[st->ps.err_strp], str, len + 1);
	st->ps.err_args[i] = (uintptr_t)&st->ram_str[st->ps.err_strp];
    }
}

// Decl name. COPIED into the error temp area rather than pointed at in place:
// a ROM-range name lives in FLASH on AVR and a RAM-range one does not, and the
// formatter cannot tell the two apart from the pointer. Copying makes every
// err_arg a plain RAM string, which is what both fprintf and csp_print_error
// expect.
void csp_set_err_arg_ix(csp_rt_t* st, int i, index_t ix)
{
    sindex_t pos = decl_name_pos(st, ix);
    int len = pos ? csp_str_byte(st, pos - 1) : 0;

    if (st->ps.err_strp >= (st->ps.strp - st->rom_strp) + (uint32_t)len + 1) {
	int k;
	st->ps.err_strp -= len + 1;
	for (k = 0; k < len; k++)
	    st->ram_str[st->ps.err_strp + k] = (char)csp_str_byte(st, pos + k);
	st->ram_str[st->ps.err_strp + len] = '\0';
	st->ps.err_args[i] = (uintptr_t)&st->ram_str[st->ps.err_strp];
    }
}

void csp_clr_error(csp_rt_t* st)
{
    st->ps.err = ERR_OK;
    st->ps.err_strp = MAX_STR_BUF;  // reset temp strings
}

// pointer to a VIEW_SLOT's value_t struct inside its buffer (in the heap)
static inline value_t* csp_slot(csp_rt_t* st, csp_view_t* v, dio_t dir)
{
    // An owner's heap offset is in the view. This used to load st->buf[v->buf]
    // for the two fields it wanted out of sixteen bytes; now there is no buffer
    // to load, and the access is one add.
    //
    // A #local is single-buffered: both directions resolve to the DIN half, so
    // the value a rule writes is readable by the rules after it in the SAME
    // cycle. Everything else keeps the transaction -- read DIN, write DOUT.
    if (v->flags & VIEW_F_LOCAL)
	dir = DIN;
    return (value_t*)(st->heap[dir] + v->pos);
}

// return pointer to the object/field value slot (VIEW_SLOT only)
value_t* csp_dio_slot(csp_rt_t* st, index_t ix, dio_t dir)
{
    return csp_slot(st, csp_view(st, ix), dir);
}

// return pointer to value pointer for input and output (VIEW_SLOT only)
int csp_dio_slots(csp_rt_t* st, index_t ix, value_t** iptr, value_t** optr)
{
    csp_view_t* v = csp_view(st, ix);
    *iptr = csp_slot(st, v, DIN);
    *optr = csp_slot(st, v, DOUT);
    return 0;
}

// Helper functions for builtin ops
static inline ivalue_t imax(ivalue_t a, ivalue_t b)
{
    return (a > b) ? a : b;
}

static inline ivalue_t imin(ivalue_t a, ivalue_t b)
{
    return (a < b) ? a : b;
}

static inline ivalue_t iabs(ivalue_t a)
{
    return (a < 0) ? -a : a;
}

static inline ivalue_t isign(ivalue_t a)
{
    return (a < 0) ? -1 : (a ? 1 : 0);
}

static inline ivalue_t fsign(fvalue_t a)
{
    return (a < 0.0) ? -1 : (a ? 1 : 0);
}

static inline ivalue_t iclip(ivalue_t x, ivalue_t a, ivalue_t b)
{
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

// v zero-padded to the width nw implies (nw = 100000 -> six digits). Stops at
// nw > 1, not nw > 0: v == 0 satisfies v < nw all the way down to nw == 0, so
// it used to emit six zeros AND then csp_print_uint's "0" -- one digit too many,
// which is why a fixpoint zero printed as 0.0000000 against 1.500000.
int csp_print_uintw(uvalue_t v, int nw)
{
    int n = 0;                 // was uninitialised: the leading-zero count was garbage
    while ((nw > 1) && (v < (uvalue_t)nw)) {
	csp_print_char('0');
	nw /= 10;
	n++;
    }
    return n+csp_print_uint(v);
}

#if FVALUE_IS_FIXPOINT
int csp_print_fixpoint(fvalue_t v)
{
    // Print Q16.16 as decimal
    int n;
    int neg = (v < 0);
    uint32_t absv = neg ? -v : v;
    int32_t intpart = absv >> FIX_SHIFT;
    uint32_t fracpart = absv & FIX_MASK;
    // Use 64-bit to avoid overflow: fracpart * 1000000 can exceed 32 bits
    fracpart = (uint32_t)(((uint64_t)fracpart * 1000000) >> FIX_SHIFT);
    if (neg) {
	csp_print_char('-');
	n = 1 + csp_print_uint(intpart);
    }
    else {
	n = csp_print_uint(intpart);
    }
    csp_print_char('.'); n++;
    return n+csp_print_uintw(fracpart, 100000);    
}
#endif

NOINLINE int csp_print_value(csp_rt_t* st, vtype_t vt, value_t val)
{
    switch(CSP_MASK(vt, TYPE_BITS)) {
    case V_INTEGER: return csp_print_int(val.i);
    case V_UNSIGNED: return csp_print_uint(val.u);
    case V_FLOAT: return csp_print_float(val.f);
    // Position 0 is "no string": a string variable that was declared without an
    // initialiser holds it. Printing it walked to pos-1 for the length byte and
    // read outside the table -- a segfault on the host, and whatever a board
    // has at that address otherwise. An empty string is the honest rendering.
    case V_STRING:
	if (val.s > 0)
	    csp_print_str_at(st, val.s);
	return 1;
    case V_TIMER: return csp_print_int(val.t.val);
    // csp_print_lit is a statement, so it cannot sit in a return expression
    default: csp_print_lit("???"); return 3;
    }
}

// Built-in function implementations - args are pre-evaluated
// fvalue_t is a signed scalar in both builds -- int32_t for Q16.16, float
// otherwise -- and the fixpoint encoding is monotonic, so a plain comparison
// orders values correctly either way. No op_ wrapper needed for min/max/clip.

// float -> int, toward zero, and to the nearest with halves away from zero.
// Both builds must agree, so the fixpoint side goes through fix_trunc/fix_round
// in csp_fixpoint.h -- the same pair FIX_TO_INT and op_CVTFI now use, so an
// explicit trunc() and an implicit narrowing can no longer disagree.
static ivalue_t fv_trunc(fvalue_t v)
{
#if FVALUE_IS_FIXPOINT
    return (ivalue_t) fix_trunc(v);
#else
    return (ivalue_t)v;                  // a C cast already truncates
#endif
}

static ivalue_t fv_round(fvalue_t v)
{
#if FVALUE_IS_FIXPOINT
    return (ivalue_t) fix_round(v);
#else
    return (v < 0) ? -(ivalue_t)((fvalue_t)0.5 - v) : (ivalue_t)(v + (fvalue_t)0.5);
#endif
}

static value_t fn_min(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    if ((type & 0xf) == V_FLOAT)
	ret.f = (args[0].f < args[1].f) ? args[0].f : args[1].f;
    else
	ret.i = imin(args[0].i, args[1].i);
    return ret;
}

static value_t fn_max(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    if ((type & 0xf) == V_FLOAT)
	ret.f = (args[0].f > args[1].f) ? args[0].f : args[1].f;
    else
	ret.i = imax(args[0].i, args[1].i);
    return ret;
}

static value_t fn_trunc(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    // An integer argument is already whole -- pass it through so generic code
    // does not have to know which it got.
    ret.i = ((type & 0xf) == V_FLOAT) ? fv_trunc(args[0].f) : args[0].i;
    return ret;
}

static value_t fn_round(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    ret.i = ((type & 0xf) == V_FLOAT) ? fv_round(args[0].f) : args[0].i;
    return ret;
}

// V_NUMBER argument: no coercion happens on the way in, so the argument still
// carries its own representation and `type` (the call's argcode) says which.
// Dispatch on it the way fn_sign does.
static value_t fn_abs(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    if ((type & 0xf) == V_FLOAT) {
	fvalue_t arg = args[0].f;
	ret.f = (arg < 0) ? op_FNEG(arg) : arg;
    }
    else
	ret.i = iabs(args[0].i);
    return ret;
}

static value_t fn_clip(csp_rt_t* st,uint16_t type,value_t* args, uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    if ((type & 0xf) == V_FLOAT) {
	fvalue_t x = args[0].f, lo = args[1].f, hi = args[2].f;
	ret.f = (x < lo) ? lo : ((x > hi) ? hi : x);
    }
    else
	ret.i = iclip(args[0].i, args[1].i, args[2].i);
    return ret;
}

static value_t fn_sign(csp_rt_t* st,uint16_t type, value_t* args, uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    if ((type&0xf) == V_INTEGER)
	ret.i = isign(args[0].i);
    else
	ret.i = fsign(args[0].f);
    return ret;
}

// Did this timer fire on the cycle now committed?
//
// Not a builtin function any more -- `timeout(T)` compiles to OP_TMO. The body
// lives here rather than in eval_op because the COMPILER needs it too: an
// immediate `> timeout(T)` at the prompt has no instruction stream to run, and
// the alternative was a second copy of these three lines on that side of the
// line.
//
// DIN, not DOUT: `fired` is set by the timer sweep and committed, so a rule
// guarded on it reads the same answer wherever in the cycle it sits.
int csp_timer_fired(csp_rt_t* st, index_t ix)
{
    value_t* vptr = csp_dio_slot(st, ix, DIN);
    return BOOL(vptr->t.fired);
}

//  FIXME: if not running?
static value_t fn_elapsed(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ix = args[0].u; // timer
    index_t tx = ix+1;
    value_t* vptr = csp_dio_slot(st, ix, DIN);
    if (vptr->t.running)
	ret.u = csp_time_ms() - csp_uvalue(st, tx);
    else
	ret.u = vptr->t.period;
    return ret;
}

//  FIXME: if not running?
static value_t fn_progress(csp_rt_t* st,uint16_t type,
			   value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ix = args[0].u; // timer
    value_t* vptr = csp_dio_slot(st, ix, DIN);

    if (vptr->t.running) {
	index_t tx = ix+1;      // start time
	uint32_t td = csp_time_ms() - csp_uvalue(st, tx);
	uint32_t period = vptr->t.period;
	ret.f = op_FDIV(op_CVTIF(td), op_CVTIF(period));
    }
    else {
	ret.f = op_CVTIF(1);
    }
    return ret;
}

// FIXME: compile  changed(X) -> OP_CHG
static value_t fn_changed(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    int i = st_index(st, ty);
    ret.i = BOOL(bitset_tst(st->dset, i));
    return ret;
}

static value_t fn_rising(csp_rt_t* st,uint16_t type,
			 value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    value_t* iptr;    
    value_t* optr;

    csp_dio_slots(st, ty, &iptr, &optr);
    ret.i = !(optr->d.val & 1) && (iptr->d.val & 1);
    return ret;
}

static value_t fn_falling(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    value_t* iptr;    
    value_t* optr;

    csp_dio_slots(st, ty, &iptr, &optr);
    ret.i = (optr->d.val & 1) && !(iptr->d.val & 1);
    return ret;
}

static value_t fn_print(csp_rt_t* st, uint16_t type,
			 value_t* args,uint8_t nargs)
{
    value_t ret;
    int i;
    ret.i = 0;
    for (i = 0; i < nargs; i++) {
	ret.i += csp_print_value(st, type & 0xf, args[i]);
	type >>= 4;
    }
    return ret;
}

static value_t fn_println(csp_rt_t* st, uint16_t type,
			  value_t* args,uint8_t nargs)
{
    value_t ret;    
    ret = fn_print(st, type, args, nargs);
    ret.i += csp_println();
    csp_flush();
    return ret;
}

static value_t fn_tick(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)args; (void)nargs;
    ret.i = csp_time_ms();
    return ret;
}

static value_t fn_cycle(csp_rt_t* st,uint16_t type,value_t* args, uint8_t nargs)
{
    value_t ret;
    (void)args; (void)nargs;
    ret.i = st->cycle;
    return ret;
}

// set latch state and return previous value
static value_t fn_latch(csp_rt_t* st,uint16_t type,value_t* args, uint8_t nargs)
{
    value_t ret;
    (void)args; (void)nargs;
    ret.i = csp_set_latch(st, args[0].i);
    return ret;
}


#define CSP_FUNC_ENT(str, a, p, rt, args, f)	\
    {.name=(str),.namelen=sizeof((str))-1,.arity=(a),			\
	    .flags=((p)?FUNC_PURE:0)|FUNC_RONAME,			\
	    .rtype=(rt),.argtypes=(args),.fn=(f)}

// Built-in function table
// { name, namelen, nargs, rtype, argtypes, fn }
const csp_func_t csp_builtin_funcs[] RODATA = {
    // match functions
    // V_NUMBER in and out -- see fn_abs. A float anywhere makes the whole call
    // float, and process_fcall promotes the other arguments to match so the
    // callee never compares an integer against a fixpoint word.
    CSP_FUNC_ENT(s_min,     2, 1, V_NUMBER,  MAKE_TYPE2(V_NUMBER,V_NUMBER), fn_min ),
    CSP_FUNC_ENT(s_max,     2, 1, V_NUMBER,  MAKE_TYPE2(V_NUMBER,V_NUMBER), fn_max ),
    // V_NUMBER in and V_NUMBER out: abs(int) is an int, abs(float) a float.
    // One entry, no overload -- the function-index field is 5 bits and the
    // call instruction is exactly 32, so entries are not cheap.
    CSP_FUNC_ENT(s_abs,     1, 1, V_NUMBER,  MAKE_TYPE1(V_NUMBER),  fn_abs ),
    CSP_FUNC_ENT(s_sign,    1, 1, V_INTEGER, MAKE_TYPE1(V_NUMBER),  fn_sign ),
    CSP_FUNC_ENT(s_clip,    3, 1, V_NUMBER,  MAKE_TYPE3(V_NUMBER,V_NUMBER,V_NUMBER), fn_clip),
    // Explicit narrowing. Needed once an implicit float->int in an argument
    // position stops being silently accepted -- and they differ: trunc goes
    // toward zero, round to the nearest with halves away from zero.
    CSP_FUNC_ENT(s_trunc,   1, 1, V_INTEGER, MAKE_TYPE1(V_NUMBER),  fn_trunc),
    CSP_FUNC_ENT(s_round,   1, 1, V_INTEGER, MAKE_TYPE1(V_NUMBER),  fn_round),
    // timer functions
    // (timeout is OP_TMO, not a call -- see csp_timer_fired)
    CSP_FUNC_ENT(s_elapsed,  1, 0, V_INTEGER, MAKE_TYPE1(V_TIMER), fn_elapsed),
    CSP_FUNC_ENT(s_progress, 1, 0, V_FLOAT, MAKE_TYPE1(V_TIMER), fn_progress),
    // variable changed detection
    CSP_FUNC_ENT(s_changed, 1, 0, V_INTEGER, MAKE_TYPE1(V_INDEX), fn_changed),
    CSP_FUNC_ENT(s_rising,  1, 0, V_INTEGER, MAKE_TYPE1(V_DIGITAL), fn_rising),
    CSP_FUNC_ENT(s_falling, 1, 0, V_INTEGER, MAKE_TYPE1(V_DIGITAL), fn_falling),

    // print 1..4 arguments
    CSP_FUNC_ENT(s_print,   1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_print),
    CSP_FUNC_ENT(s_print,   2, 0, V_INTEGER, MAKE_TYPE2(V_ANY,V_ANY),  fn_print),
    CSP_FUNC_ENT(s_print,   3, 0, V_INTEGER, MAKE_TYPE3(V_ANY,V_ANY,V_ANY),  fn_print),
    CSP_FUNC_ENT(s_print,   4, 0, V_INTEGER, MAKE_TYPE4(V_ANY,V_ANY,V_ANY,V_ANY),  fn_print),
    // println 0..4 arguments    
    CSP_FUNC_ENT(s_println, 0, 0, V_INTEGER, MAKE_TYPE0(),       fn_println),
    CSP_FUNC_ENT(s_println, 1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_println),
    CSP_FUNC_ENT(s_println, 2, 0, V_INTEGER, MAKE_TYPE2(V_ANY,V_ANY),  fn_println),
    CSP_FUNC_ENT(s_println, 3, 0, V_INTEGER, MAKE_TYPE3(V_ANY,V_ANY,V_ANY),  fn_println),
    CSP_FUNC_ENT(s_println, 4, 0, V_INTEGER, MAKE_TYPE4(V_ANY,V_ANY,V_ANY,V_ANY),  fn_println),
    CSP_FUNC_ENT(s_tick,    0, 0, V_INTEGER, MAKE_TYPE0(),       fn_tick),
    CSP_FUNC_ENT(s_cycle,   0, 0, V_INTEGER, MAKE_TYPE0(),       fn_cycle),
    CSP_FUNC_ENT(s_latch,   1, 0, V_INTEGER, MAKE_TYPE1(V_INTEGER), fn_latch),
};

const uint8_t csp_num_builtin_funcs = sizeof(csp_builtin_funcs)/sizeof(csp_builtin_funcs[0]);

// Declaration index of object number `m`.
//
// object[] is a cache that csp_rt_start rebuilds, and it lives with the other
// derived tables in the middle of the arena -- so between a `#Module inst` line
// and the next rebuild it does not cover the new object at all. That window is
// real: /pause defers the rebuild, and /list reads object names through here.
//
// The durable record is in the declaration itself (mq.m), so fall back to a scan
// when the cache cannot answer. The scan is O(nd) and only listing ever takes
// it; everything on a cycle runs after a rebuild and hits the table.
index_t csp_object_decl(csp_rt_t* st, unsigned m)
{
    index_t i;
    if (st->object && (m < st->obj_cap))
	return st->object[m];
    for (i = 0; i < st->ps.nd; i++) {
	csp_decl_t d = csp_get_decl(st, i);
	if ((d.type == DECL_OBJECT) && (d.mq.m == m))
	    return i;
    }
    return BAD_INDEX;
}

// enq all rules that depend on declaration x
NOINLINE void csp_enq_elist(csp_rt_t* st, index_t x)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int i;
    index_t ix = INDEX(x);
    uint8_t obj = OBJ(x);
    // A CURRENT-relative write (a module rule touching its own object's field)
    // must enqueue under the concrete object, not the CURRENT placeholder, or
    // the dispatcher can't restore the right instance context.
    if (obj == CURRENT)
	obj = st->cur;
    // baked ROM graph in flash: ROM decl -> ROM rules that read it
    if (st->rom_nedg && (ix < st->rom_nd)) {
	index_t base = ro_word(&st->rom_p.ofs[ix]);
	index_t n    = ro_word(&st->rom_p.idg[ix]);
	for (i = 0; i < (int)n; i++)
	    csp_enq(st, obj, ro_word(&st->rom_p.edg[base+i]));
    }
    // runtime RAM graph: any decl -> RAM rules that read it. Only decls the graph
    // was built for have edges; one added since (ix >= graph_n) has none yet.
    if (ix < st->es.graph_n) {
	index_t base = st->es.ofs[ix];
	for (i = 0; i < st->es.idg[ix]; i++)
	    csp_enq(st, obj, st->es.edg[base+i]);  // rule instruction index
    }
#endif
}

// --- buffer heap access (VIEW_HEAP) ----------------------------------------
// Dormant in step 2 (no HEAP views are emitted yet); exercised from step 3.

// Where a HEAP or OWN view's bits live: the base pointer, the start bit inside
// it, and how many bytes a whole-storage access covers.
//
// The two kinds differ only here. A VIEW_HEAP reads its base and size from the
// buffer it looks into; an owner has neither, so the base comes from its own
// heap offset, the start bit is zero (it owns the whole thing) and the size
// follows from the width the declaration gave.
NOINLINE static uint8_t* heap_base(csp_rt_t* st, csp_view_t* vw, dio_t dir,
				   uint16_t* bitp, uint8_t* nbytesp)
{
    if (vw->kind == VIEW_HEAP) {
	csp_buf_t* b = &st->buf[vw->buf];
	*bitp = vw->pos;
	*nbytesp = (uint8_t)b->nbytes;
	return st->heap[dir] + b->hp;
    }
    // NO local redirect here, deliberately. BUF_F_LOCAL was read by csp_slot
    // only, and a #local is a DECL_VARIABLE -- an OWN view, never a SLOT -- so
    // the redirect never applied to it and the value path has always been the
    // ordinary DIN/DOUT transaction. Adding it here broke `#local sum` down to
    // 0: writes went to DIN and the commit then copied the stale DOUT back over
    // them. Whether a #local really gets its same-cycle read is a question that
    // predates this and is not answered by the flag.
    *bitp = 0;
    *nbytesp = (uint8_t)((vw->len + 8) >> 3);   // (len+1 bits) rounded up
    return st->heap[dir] + vw->pos;
}

NOINLINE static value_t csp_heap_get(csp_rt_t* st, csp_view_t* vw, dio_t dir)
{
    uint16_t bit;
    uint8_t nbytes;
    uint8_t* p = heap_base(st, vw, dir, &bit, &nbytes);
    value_t v;
    v.u = 0;
    if (vw->flags & VIEW_F_SIMPLE) {       // whole storage, byte aligned
	uint8_t n = nbytes;
	if (n > sizeof(value_t)) n = sizeof(value_t);
	memcpy(&v, p, n);
    }
    else {
	csp_bits_get(p, &v.u, bit, vw->len + 1, vw->endian == E_BIG);
	// Sign-extend a signed field from its own width up to the container, so a
	// negative CAN signal reads back negative. get_bits zero-extends; unsigned
	// fields keep that, as do 32-bit-wide ones (no spare high bits to fill).
	if (vw->vt == V_INTEGER) {
	    uint8_t nbits = vw->len + 1;
	    if ((nbits < 32) && (v.u & ((uvalue_t)1 << (nbits - 1))))
		v.u |= ~(((uvalue_t)1 << nbits) - 1);
	}
    }
    return v;
}

NOINLINE static void csp_heap_set(csp_rt_t* st, csp_view_t* vw, dio_t dir,
				  value_t v)
{
    uint16_t bit;
    uint8_t nbytes;
    uint8_t* p = heap_base(st, vw, dir, &bit, &nbytes);
    if (vw->flags & VIEW_F_SIMPLE) {       // whole storage, byte aligned
	uint8_t n = nbytes;
	if (n > sizeof(value_t)) n = sizeof(value_t);
	memcpy(p, &v, n);
    }
    else
	csp_bits_set(p, v.u, bit, vw->len + 1, vw->endian == E_BIG);
}

// A digital/analog/timer decl carries vt=V_INTEGER (its value type); the
// union member for its config lives under the decl *type*. Map type -> the
// vtype the pin/port/dir/... helpers switch on. Plain vars keep their vt.
NOINLINE static vtype_t decl_cfg_vt(decl_t dt, vtype_t vt)
{
    switch (CSP_MASK(dt,CSP_DECL_TYPE_BITS)) {
    case DECL_DIGITAL: return V_DIGITAL;
	// An analog reading is SIGNED unless the declaration says otherwise --
	// the same default a #variable has, and the right one for a sensor that
	// swings both ways. `unsigned` is the opt-in, for a reading that is a
	// magnitude or a packed word (an RGB565 pixel) rather than a quantity.
	// The declaration's vt used to be dropped on this path entirely.
    case DECL_ANALOG:
	return (vtype_t)(V_ANALOG |
			 ((CSP_MASK(vt,TYPE_BITS) == V_UNSIGNED) ? 0
							         : CFG_SIGNED));
    case DECL_TIMER:   return V_TIMER;
    case DECL_FIELD:   return V_FIELD;
    default:           return vt;
    }
}

// The type a leaf's value SLOT is laid out with. Four callers used to write
// decl_cfg_vt(decl(st,i,type), decl(st,i,vt)) -- two csp_get_decl calls, each
// spilling the whole decl to the stack (see setup_buffer). One read serves both
// fields.
NOINLINE static vtype_t leaf_cfg_vt(csp_rt_t* st, index_t ix)
{
    csp_decl_t d = csp_get_decl(st, INDEX(ix));
    return decl_cfg_vt(d.type, d.vt);
}

NOINLINE void csp_string_set_part(csp_rt_t* st, value_t* vslot,
				  csp_part_t part, value_t v)
{
    if (part == PART_VAL)
	vslot->s = v.s;
}

NOINLINE void csp_view_set_part(csp_rt_t* st, csp_view_t* vw,
				value_t v, csp_part_t part, dio_t dir)
{
    if (part == PART_VAL)
	csp_heap_set(st, vw, dir, v);
    // Endian lives in the view, so it works for either kind.
    else if (part == PART_ENDIAN) {
	if ((v.i >= E_NATIVE) && (v.i <= E_BIG))
	    vw->endian = v.i;
    }
    // Everything below reads st->buf[vw->buf], and an OWNER has no buffer --
    // `buf` is not a valid index for one. A #variable has no transport state to
    // set anyway, so this is the right answer as well as the safe one.
    else if (vw->kind != VIEW_HEAP)
	return;
    // A frame's transport state lives on the BUFFER, not in a value slot,
    // and is a command rather than a value -- so it is not DIN/DOUT
    // shadowed and does not go through the dirty set.
    else if (part == PART_DIR)
	st->buf[vw->buf].dir = v.i;
    else if (st->buf[vw->buf].transport == TR_CAN) {
	csp_buf_t* bp = &st->buf[vw->buf];
	if (part == PART_TX) {
	    if (v.i)
		bp->flags |= BUF_F_TX;
	    else
		bp->flags &= ~BUF_F_TX;
	}
	else if (part == PART_DLC) {
	    // Clamped, not rejected: the heap holds nbytes and no more, so
	    // a longer frame would read past the buffer.
	    ivalue_t n = v.i;
	    if (n < 0) n = 0;
	    if (n > bp->nbytes) n = bp->nbytes;
	    bp->dlc = (uint8_t)n;
	}
    }
}

// Set value part in dio (config data & value)
NOINLINE void csp_dio_set_part(csp_rt_t* st, index_t ix, value_t v,
			       csp_part_t part, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    // Anything that is not a plain value_t slot carries only a VALUE -- a
    // bit-field has no pin, port or period. VIEW_OWN joins VIEW_HEAP here: it is
    // the same bit-field access, just over storage it owns rather than borrows.
    if (vw->kind != VIEW_SLOT) {
	csp_view_set_part(st, vw, v, part, dir);
	return;
    }
    else {
	value_t* vslot = csp_slot(st, vw, dir);
	vtype_t cvt = leaf_cfg_vt(st, ix);  // read the decl only on this path
	// A string slot holds a whole position, not bitfields -- it is the one
	// value type csp_part.h does not describe. Everything else is a row in
	// the layout table (and a type with no rows writes nothing).
	if (cvt == V_STRING)
	    csp_string_set_part(st, vslot, part, v);
	else
	    csp_part_set(vslot, cvt, part, v);
    }
}

NOINLINE void csp_dio_set_val_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t v)
{
    switch(CSP_MASK(vt, TYPE_BITS)) {
    case V_TIMER:   vslot->t.val = v.i; break;
    case V_DIGITAL: vslot->d.val = v.i; break;
    case V_ANALOG:  vslot->a.val = v.i; break;
	//case V_STRING:  vslot->s = v.i; break;
    case V_STRING:  vslot->s = v.s; break;		
    default: *vslot = v; break;
    }
}

// The VALUE-SLOT string (a #constant): the position is the whole slot, so read
// it as one -- like csp_string_set_part and every other slot helper here.
// It must NOT go through csp_heap_get: setup_slot fills in kind/vt/buf and
// leaves pos/len/endian zero, so the bit path would read len+1 == ONE bit of
// the position and answer 0 or 1. A string VARIABLE is a heap view with a real
// pos/len and answers from csp_view_get_part instead.
NOINLINE void csp_string_get_part(csp_rt_t* st, value_t* vslot, value_t* vp,
				  csp_part_t part)
{
    sindex_t s = vslot->s;
    if (part == PART_VAL)
	vp->i = s;
    else if (part == PART_LEN)
	vp->i = (s == 0) ? 0 : csp_str_byte(st, s - 1);
}

NOINLINE void csp_dio_get_val_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t* vp)
{
    switch(CSP_MASK(vt, TYPE_BITS)) {
    case V_TIMER:   vp->i = vslot->t.val; break;
    case V_DIGITAL: vp->i = vslot->d.val & 1; break;
	// a.val is unsigned:16 and cannot hold a negative, so a signed analog
	// stores the low 16 bits and gets them sign-extended back here. That
	// round-trips: csp_dio_set_val_part truncates the same 16 bits.
    case V_ANALOG:
	vp->i = (vt & CFG_SIGNED) ? (ivalue_t)(int16_t)vslot->a.val
	                          : (ivalue_t)vslot->a.val;
	break;
    case V_STRING:  vp->i = vslot->s; break;
    default: *vp = *vslot; break;
    }
}

NOINLINE void csp_view_get_part(csp_rt_t* st, csp_view_t* vw, value_t* vp,
				csp_part_t part, dio_t dir)
{
    // An OWNER has no buffer, so `bp` must not be dereferenced for one. Only
    // PART_VAL, PART_ENDIAN and PART_LEN answer from the view itself; every
    // other case here is buffer state, and reads 0 for a leaf that has none --
    // which is the truth: a #variable has no transport, no frame id, no dlc.
    csp_buf_t* bp = (vw->kind == VIEW_HEAP) ? &st->buf[vw->buf] : NULL;
    vp->u = 0;
    switch (CSP_MASK(part, PART_BITS)) {
    case PART_VAL: *vp = csp_heap_get(st, vw, dir); break;
	// Direction is a property of the buffer, so it answers for a plain
	// #buffer as well as a CAN frame -- and for a #field,  which reads
	// its frame's direction.
    case PART_DIR: if (bp) vp->i = bp->dir; break;
	// Endianness is a property of the VIEW (a bound field / #field decides
	// how its bits are laid out), so it answers from there and not from a
	// value slot -- a heap view has none.
    case PART_ENDIAN: vp->i = vw->endian; break;
	// Frame state, read off the buffer. A #field answers for its frame
	// too: `A.rx` and `F201.rx` are the same fact.
    case PART_RX:
	if (bp && (bp->transport == TR_CAN))
	    vp->i = BOOL(bp->flags & BUF_F_RX);
	break;
    case PART_TX:
	if (bp && (bp->transport == TR_CAN))
	    vp->i = BOOL(bp->flags & BUF_F_TX);
	break;
    case PART_ID:
	if (bp && (bp->transport == TR_CAN))
	    vp->i = (ivalue_t)bp->xref;
	break;
    case PART_DLC:
	if (bp && (bp->transport == TR_CAN))
	    vp->i = bp->dlc;
	break;
	// A plain #variable gets an auto-buffer (setup_variable -> setup_buffer),
	// so a string variable is a HEAP view and lands HERE, not in the value-slot
	// switch below -- which is for the config+value leaves (digital, analog,
	// timer, constant). A string CONSTANT does take that path, so both need it.
    case PART_LEN: {
	value_t sv = csp_heap_get(st, vw, dir);
	if ((vw->vt == V_STRING) && (sv.s > 0))
	    vp->i = csp_str_byte(st, sv.s - 1);
	break;	
    }
    default: break;
    }
}

NOINLINE void csp_dio_get_part(csp_rt_t* st, index_t ix, value_t* vp,
			       csp_part_t part, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    if (vw->kind != VIEW_SLOT) {  // bit-fields only carry a value, no pin/port
	vp->u = 0;
	csp_view_get_part(st, vw, vp, part, dir);
    }
    else {
	value_t* vslot = csp_slot(st, vw, dir);
	vtype_t cvt = leaf_cfg_vt(st, ix);  // read the decl only on this path
	if (cvt == V_STRING)                // see csp_dio_set_part
	    csp_string_get_part(st, vslot, vp, part);
	else
	    csp_part_get(vslot, cvt, part, vp);
    }
}

NOINLINE void csp_dio_set(csp_rt_t* st, index_t ix, value_t v, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    if (vw->kind != VIEW_SLOT) {
	csp_heap_set(st, vw, dir, v);
	return;
    }
    csp_dio_set_val_part(st, csp_slot(st, vw, dir), leaf_cfg_vt(st, ix), v);
}

NOINLINE void csp_dio_get(csp_rt_t* st, index_t ix, value_t* vp, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    if (vw->kind != VIEW_SLOT) {
	*vp = csp_heap_get(st, vw, dir);
	return;
    }
    csp_dio_get_val_part(st, csp_slot(st, vw, dir), leaf_cfg_vt(st, ix), vp);
}

NOINLINE void csp_set_value(csp_rt_t* st, index_t n, value_t v)
{
    value_t cv;
    csp_dio_get(st, n, &cv, DOUT);
    if (v.u != cv.u) {
	int i = st_index(st, n);
	bitset_set(st->dset, i);
	st->es.anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st,n);
#endif
	csp_dio_set(st, n, v, DOUT);
	st->es.update++;
    }
}

// A DECL_VARIABLE named "State" holds a state NUMBER; show it symbolically, the
// way /list does, so "ON" does not read as "3". Same rule as the disassembler's
// is_state_var: only that name, so a plain "T=1" is never mistaken for a state.
NOINLINE int state_is_state_var(csp_rt_t* st, int di)
{
    if (decl(st, di, type) != DECL_VARIABLE)
	return 0;
    return csp_str_eq_ro(st, decl_name_pos(st, MAKE_INDEX(0, di)), ros_State, 5);
}

NOINLINE value_t csp_value(csp_rt_t* st, index_t n)
{
    value_t cv;
    csp_dio_get(st, n, &cv, DIN);
    return cv;
}

NOINLINE void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v)
{
    value_t vv;
    vv.i = v;
    csp_set_value(st, n, vv);
}

NOINLINE void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v)
{
    value_t vv;
    vv.f = v;
    csp_set_value(st, n, vv);
}

NOINLINE csp_func_fn func_fn(const csp_func_t* fn, int i, int rom)
{
    return (csp_func_fn) rdvp(&fn[i].fn, rom);
}

NOINLINE uint8_t func_arity(const csp_func_t* fn, int i, int rom)
{
    return rd8(&fn[i].arity, rom);
}

// Maybe used for special opcode without registers
NOINLINE value_t eval0(opcode_t op)
{
    value_t x;    
    switch(CSP_MASK(op, CSP_OPCODE_BITS)) {
    default: x.i = 0; break;
    }
    return x;
}

// opcodes that do not return x register, return
// return 0: stop
// return 1: continue with next istruction
// return n: jump relative, continue with n+x
// set *leave=1 for leaving again loop
//
// `ci` is the instruction word at n, read ONCE by the caller. csp_get_instr is
// NOINLINE and returns csp_instr_t by value, so every instr(st,n,fld) in here
// used to be its own call plus a spill (same trap as decl() -- see
// setup_buffer). There were thirty-five of them, all on the same n, in the
// largest function in the image. csp_eval_rule has already read the word to get
// the opcode, so passing it costs nothing and saves a re-read per instruction.
// Named ci (current instruction) and not `in`: that name is the OP_INSTATE arm.
NOINLINE int eval_op(csp_rt_t* st, int n, csp_instr_t ci, int* leave)
{
    // The ALU operands, loaded ONCE for every opcode instead of once per arm.
    // ci.a.y/ci.a.z are REG_BITS wide and MAX_REGS is 1 << REG_BITS, so the
    // index is in range whatever instruction format the word really holds -- a
    // load or a store just reads a register it will never look at.
    value_t y = st->es.reg[ci.a.y];
    value_t z = st->es.reg[ci.a.z];
    value_t x;

    switch(CSP_MASK(ci.op, CSP_OPCODE_BITS)) {
    case OP_BNOT: x.i = op_BNOT(y.i); goto store;
    case OP_NEG: x.i = op_NEG(y.i); goto store;
    case OP_MOV: x.i = op_MOV(y.i); goto store;
    case OP_NOT: x.i = op_NOT(y.i); goto store;
    case OP_CVTIF: x.f = op_CVTIF(y.i); goto store;
    case OP_CVTFI: x.i = op_CVTFI(y.f); goto store;
    case OP_FNEG: x.f = op_FNEG(y.f); goto store;
    case OP_FMOV: x.f = op_FMOV(y.f); goto store;
    case OP_ADD:
#if FVALUE_IS_FIXPOINT
    case OP_FADD:
#endif
        x.i = op_ADD(y.i, z.i); goto store;
    case OP_SUB:
#if FVALUE_IS_FIXPOINT
    case OP_FSUB:
#endif
        x.i = op_SUB(y.i, z.i); goto store;
    case OP_MUL: x.i = op_MUL(y.i, z.i); goto store;
    // The seven that read the operands' SIGN. `#variable Pi unsigned` stored the
    // right bits and then divided, compared and shifted them as if they were
    // signed: Pi = 4294967287 gave `Pi % 10` = 4294967287 (the signed -9 % 10,
    // printed back through the unsigned type) instead of 7. csp_instr_alu_t.u
    // says which arm to take; the compiler sets it when either operand's
    // declared type is unsigned (see process_op).
    case OP_DIV:
	if (ci.a.u) { x.u = op_DIV(y.u, z.u); goto store; }
	x.i = op_DIV(y.i, z.i); goto store;
    case OP_REM:
	if (ci.a.u) { x.u = op_REM(y.u, z.u); goto store; }
	x.i = op_REM(y.i, z.i); goto store;
    case OP_SLA: x.i = op_SLA(y.i, z.i); goto store;
    case OP_SRA:
	if (ci.a.u) { x.u = op_SRA(y.u, z.u); goto store; }  // logical shift
	x.i = op_SRA(y.i, z.i); goto store;
    case OP_BAND: x.i = op_BAND(y.i, z.i); goto store;
    case OP_BOR: x.i = op_BOR(y.i, z.i); goto store;
    case OP_BXOR: x.i = op_BXOR(y.i, z.i); goto store;
    case OP_AND:  x.i = op_AND(y.i, z.i); goto store;
    case OP_OR:   x.i = op_OR(y.i, z.i); goto store;
    // The four ORDER comparisons. Their RESULT is a truth value either way
    // (CSP_TRUE/CSP_FALSE), so only the operands change arm.
    case OP_LT:
#if FVALUE_IS_FIXPOINT
    case OP_FLT:
#endif
	if (ci.a.u) { x.i = op_LT(y.u, z.u); goto store; }
        x.i = op_LT(y.i, z.i); goto store;
    case OP_LTE:
#if FVALUE_IS_FIXPOINT
    case OP_FLTE:
#endif
	if (ci.a.u) { x.i = op_LTE(y.u, z.u); goto store; }
        x.i = op_LTE(y.i, z.i); goto store;
    // No OP_GT/OP_GTE arms. `a > b` arrives here as `b < a` with
    // csp_instr_alu_t.swap set for the listing's benefit, so the LT arms above
    // already compute it -- and the swap bit is never read at run time.
    case OP_EQEQ:
#if FVALUE_IS_FIXPOINT
    case OP_FEQEQ:
#endif
        x.i = op_EQEQ(y.i, z.i); goto store;
    case OP_NEQ:
#if FVALUE_IS_FIXPOINT
    case OP_FNEQ:
#endif
        x.i = op_NEQ(y.i, z.i); goto store;
    case OP_FMUL: x.f = op_FMUL(y.f, z.f); goto store;
    case OP_FDIV: x.f = op_FDIV(y.f, z.f); goto store;
	//case OP_COMMA: x.i = op_COMMA(y.i, z.i); goto store;
#if !FVALUE_IS_FIXPOINT
    case OP_FADD: x.f = op_FADD(y.f, z.f); goto store;
    case OP_FSUB: x.f = op_FSUB(y.f, z.f); goto store;
    case OP_FLT:   x.i = op_FLT(y.f, z.f); goto store;
    case OP_FLTE:  x.i = op_FLTE(y.f, z.f); goto store;
    case OP_FEQEQ: x.i = op_FEQEQ(y.f, z.f); goto store;
    case OP_FNEQ:  x.i = op_FNEQ(y.f, z.f); goto store;
#endif
    default:
	// No arm: yield 0 through the store, which is what the arity-2 default
	// did. evalx's old `*leave = 1` default was unreachable -- op_info gives
	// arity 3 to exactly the opcodes evalx handled, so anything unlisted read
	// arity 0 and was skipped.
	x.i = 0;
	goto store;
    case OP_NOP:
	break;
    case OP_LD:
	st->es.reg[ci.m.x] = csp_value(st, ci.m.mem);
	break;
    case OP_LDP:
	csp_dio_get_part(st, ci.m.mem, &st->es.reg[ci.m.x],
			 ci.m.y, DIN);
	break;
//    case OP_EQI:
//	st->es.reg[ci.mi.x].i =
//	    csp_value(st, ci.mi.mem).i == ci.mi.imm;
//	break;	
    case OP_STI: {  // store immediate to memory (mirror of EQI)
	index_t sm = ci.mi.mem;
	value_t v;
	v.i = ci.mi.imm;
	// Sticky FAILSAFE: once the State variable holds it, only a reset leaves
	// it -- a rule that tries to set State to anything else is ignored, so a
	// flaky guard cannot bounce the device out of its safe configuration.
	// Cheap guard: the name check runs only when the slot already reads
	// FAILSAFE (rare) and the write is not re-asserting it.
	if ((v.i != STATE_FAILSAFE) &&
	    (csp_value(st, sm).i == STATE_FAILSAFE) &&
	    state_is_state_var(st, INDEX(sm)))
	    break;
	csp_set_value(st, sm, v);
	break;	
    }
    case OP_STIMP:  // same as ST, but marks reactive assignment
    case OP_ST:
	csp_set_value(st, ci.m.mem, st->es.reg[ci.m.x]);
	break;
    case OP_STP: {
	index_t mm = ci.m.mem;
	csp_dio_set_part(st, mm, st->es.reg[ci.m.x],
			 ci.m.y, DOUT);
	bitset_set(st->dset, st_index(st, mm));  // config change must commit
	st->es.anyd = CSP_TRUE;
	break;	
    }
    case OP_TMO:    // timeout(T): one bit, straight out of the timer's slot
	st->es.reg[ci.m.x].i = csp_timer_fired(st, ci.m.mem);
	break;
    case OP_CHG: {  // r |= dset[ix]  (force-true on the seed cycle)
	int i = st_index(st, ci.m.mem);
	st->es.reg[ci.m.x].i |=
	    (st->es.seed_all || bitset_tst(st->dset, i)) ? 1 : 0;
	break;
    }
    case OP_LI:
	st->es.reg[ci.i.x].i = ci.i.imm;  // sign extend
	break;
    case OP_LIU:
	st->es.reg[ci.i.x].u = (uint16_t)ci.i.imm; // zero extend
	break;
    case OP_LIH:
	st->es.reg[ci.i.x].u |= ((uint32_t)(uint16_t)ci.i.imm) << 16;
	break;
    case OP_ARG:
	st->es.arg[ci.i.imm] = st->es.reg[ci.i.x];
	break;
    case OP_RULE:
	// Bare NORMAL+ rule (implicit): it has no block gate, so gate it on
	// State in {INIT, NORMAL} HERE. Sequential needs this (no gate walked);
	// reactive already gated via rule_state but re-checking is harmless. This
	// replaces the folded State==INIT||State==NORMAL that used to sit in the
	// condition -- so a bare rule quiesces in FAILSAFE/user states.
	if (ci.r.implicit) {
	    int sv = csp_value(st, st->gsx).i;
	    if ((sv != STATE_INIT) && (sv != STATE_NORMAL)) {
		return n + ci.r.nxt;
	    }
	}
	// #disable. Keyed by the OP_RULE's OWN ip: it is the one instruction
	// every rule has exactly one of, and both dispatch paths run through it
	// (csp_react enters at rule_ip[ord], which is the condition, and falls
	// into the OP_RULE that closes it). The r.nxt jump that skips a
	// false-guard body is exactly the skip a disabled rule needs, so
	// disabling costs nothing but the test.
	if (((st->dis_ip == NULL) || !bitset_tst(st->dis_ip, n)) &&
	    st->es.reg[ci.r.cnd].i)
	    break;
	return n+ci.r.nxt;
    case OP_INSTATE:  // #in block gate: skip the whole block if State != imm
	if (st->es.reg[ci.in.x].i != ci.in.imm) {
	    *leave = 1;
	    return n + ci.in.nxt;
	}
	break;
    case OP_NINSTATE: // OR-chain gate: jump INTO the block if State == imm
	if (st->es.reg[ci.in.x].i == ci.in.imm) {
	    *leave = 1;
	    return n + ci.in.nxt;
	}
	break;
    case OP_SETO:
	// Point CURRENT at a NAMED object (`safe.State`) for the ONE memory
	// instruction that follows. Returns straight out rather than breaking: the
	// restore in the tail below must not fire on the instruction that armed it.
	st->es.sobj = st->cur + 1;         // remember who we were (0 = nothing armed)
	st->cur     = ci.o.obj;
	st->cbase   = st->offs[ci.o.obj];
	return n+1;
    case OP_SETOX: {
	// Array element chosen at runtime (`P[Idx]`). Same one-shot as OP_SETO,
	// but it shifts the base by an ELEMENT instead of naming an object.
	//
	// The register holds whatever the program computed, so this is the one
	// place a wild value reaches the base. Out of range: DO NOT ARM. The
	// access that follows then lands on element 0 of the array -- wrong data,
	// but a defined read inside the arena instead of a base from beyond the
	// tables, which on a small target is a fault or silent corruption spread
	// over every later access. The error is sticky, so a bad index reports
	// once and does not spam a 50 Hz loop.
	ivalue_t ix = st->es.reg[ci.ox.x].i;

	if ((ix < 0) || (ix >= (ivalue_t)ci.ox.len)) {
	    csp_set_error(st, ERR_INDEX_RANGE);
	    return n+1;
	}
	// offs[cur], not cbase: inside a module the array's index is
	// module-relative, and reading offs makes this idempotent -- two SETOX in
	// a row cannot compound the way `cbase +=` would. At global level
	// offs[0] is 0, so the base is just the element offset.
	st->es.sobj = st->cur + 1;         // arm; cur itself does not change
	st->cbase   = st->offs[st->cur] + (index_t)(ix * ci.ox.stride);
	return n+1;
    }
    case OP_NEXT: // rule is done executing
	*leave = 1;
	return n+1;
    case OP_ENTER: // skip y + 2
	*leave = 1;
	return n+ci.e.num+2;
    case OP_NEW:
	// Enter the object like a call -- but only during a full sweep (csp_eval:
	// non-reactive execution and the reactive SEED). csp_react dispatches
	// single rules by ip; if one reaches OP_NEW it must be a no-op, else esp
	// grows unboundedly and corrupts the struct.
	if (st->es.sweep) {
	    index_t ent = ci.n.ent;
	    index_t obj = ci.n.obj;
	    st->stack[st->esp].ix = n+1;      // return address
	    st->stack[st->esp].cur = st->cur;  // store current module
	    st->esp++;
	    st->cur = decl(st, INDEX(obj), mq.m);    // set current module
	    st->cbase = st->offs[st->cur];          // setup locals
	    *leave = 1;
	    return INDEX(ent)+1; // first instruction
	}
	break;
    case OP_LEAVE:
	if (st->es.sweep) {
	    if (st->esp == 0)
		return st->ps.nn; // make it stop
	    st->esp--;
	    st->cur = st->stack[st->esp].cur;
	    n = st->stack[st->esp].ix;
	    st->cbase = st->offs[st->cur];
	    *leave = 1;
	    return n;
	}
	break;
    case OP_CALL: {
	index_t idx = ci.f.idx;
	uint8_t arity;
	csp_func_fn fn = NULL;

	// Get function pointer
	if (ci.f.usr) {
	    if (st->ufuncs && (idx < st->num_ufuncs)) {
		arity = func_arity(st->ufuncs, idx, st->ufuncs_rom);
		fn    = func_fn(st->ufuncs, idx, st->ufuncs_rom);
	    }
	}
	else {
	    if (idx < csp_num_builtin_funcs) {
		arity = func_arity(csp_builtin_funcs, idx, BUILTIN_ROM);
		fn    = func_fn(csp_builtin_funcs, idx, BUILTIN_ROM);
	    }
	}
	if (fn) {
	    value_t val = fn(st, ci.f.avt, st->es.arg, arity);
	    st->es.reg[ci.f.x] = val;
	}
	break;
    }
    }
    // Consume a one-shot OP_SETO: the memory instruction above has had its named
    // object, so put the executing object back. A load and a branch on every
    // instruction that reaches here, which buys OP_SETO not needing a paired
    // restore instruction -- and a pair can be split by the forward jump that
    // skips a rule body (csp_instr_rule_t.nxt), leaving the base on the wrong
    // object for whatever ran next.
    if (st->es.sobj) {
	st->cur     = st->es.sobj - 1;
	st->cbase   = st->offs[st->cur];
	st->es.sobj = 0;
    }
    return n+1;
store:
    // One store site for every ALU arm, instead of one per arm.
    st->es.reg[ci.a.x] = x;
    return n + 1;
}


// eval until NEXT!
int csp_eval_rule(csp_rt_t* st, int n)
{
    int leave = 0;
    
    csp_stack_mark();
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    st->num_eval_rule++;
#endif
    while(!leave) {
        // never walk past the last instruction into garbage
	if (n >= (int)st->ps.nn) 
	    return n;
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
	st->num_eval0++;
#endif
	n = eval_op(st, n, csp_get_instr(st, n), &leave);
    }
    return n;
}

// The heap region a dirty leaf commits: offset, and the size through *np.
//
// A view into a buffer commits the WHOLE buffer -- one field's write makes the
// frame due, which is what a CAN TPDO needs. An owner commits its own bytes,
// and gets them from the view: a SLOT is a whole value_t, an OWN is the width
// its declaration asked for.
static uint16_t leaf_region(csp_rt_t* st, csp_view_t* v, uint16_t* np)
{
    if (v->kind == VIEW_HEAP) {
	csp_buf_t* b = &st->buf[v->buf];
	*np = b->nbytes;
	return b->hp;
    }
    *np = (v->kind == VIEW_SLOT) ? (uint16_t)sizeof(value_t)
				 : (uint16_t)((v->len + 8) >> 3);
    return v->pos;
}

// mirror dirty leaf storage between the two heaps (everything lives in the heap)
NOINLINE static void heap_dset_copy(csp_rt_t* st, dio_t to, dio_t from)
{
    int g, i;
    set_group_t bits;

    for (g = 0; g < (int)BITSET_GROUPS(st->view_cap); g++) {
	if ((bits = st->dset[g]) == 0)
	    continue;
	i = g*BITSET_GROUP_BITS;
	while (bits) {
	    if (bits & 1) {
		csp_view_t* v = &st->view[i];
		uint16_t n;
		uint16_t hp = leaf_region(st, v, &n);
		memcpy(st->heap[to] + hp, st->heap[from] + hp, n);
		// Committing a change into a CAN frame is what makes it due for
		// sending. Only a buffer can be one.
		if ((to == DIN) && (v->kind == VIEW_HEAP)) {
		    csp_buf_t* b = &st->buf[v->buf];
		    if (b->transport == TR_CAN)
			b->flags |= BUF_F_DIRTY;
		}
	    }
	    bits >>= 1;
	    i++;
	}
    }
}

// undo all values (revert dirty out slots to committed values)
void csp_undo(csp_rt_t* st)
{
    if (st->es.anyd)
	heap_dset_copy(st, DOUT, DIN);
    st->es.anyd = CSP_FALSE;
    memset(st->dset, 0, BITSET_GROUPS(st->view_cap) * sizeof(set_group_t));
}

// commit changed values to the in buffer
void csp_commit(csp_rt_t* st)
{
    index_t b;
    if (st->es.anyd)
	heap_dset_copy(st, DIN, DOUT);
    // Promote arrival flags across the commit. A frame received during
    // csp_input landed in the DOUT shadow, so it becomes readable only now --
    // and `.rx` has to become true now too, or a rule guarded on it would read
    // the PREVIOUS frame. One cycle of life, then gone.
    for (b = 0; b < st->nbuf; b++) {
	csp_buf_t* bp = &st->buf[b];
	if (bp->transport != TR_CAN)
	    continue;
	bp->flags &= ~BUF_F_RX;
	if (bp->flags & BUF_F_RXPEND)
	    bp->flags = (bp->flags & ~BUF_F_RXPEND) | BUF_F_RX;
    }
    memset(st->dset, 0, BITSET_GROUPS(st->view_cap) * sizeof(set_group_t));
    st->es.anyd = CSP_FALSE;
}

// run eval_rule sequentially over an instruction range [start, stop)
index_t csp_eval_range(csp_rt_t* st, index_t start, index_t stop)
{
    index_t n = start;
    index_t x = BAD_INDEX;
    st->es.sweep = 1;   // full sweep: OP_NEW/LEAVE enter/leave objects
    while(n < stop) {
	n = csp_eval_rule(st, n);
	x = n;
    }
    st->es.sweep = 0;
    return x;
}

// run eval_rule on all nodes (ROM + RAM), sequentially
index_t csp_eval(csp_rt_t* st)
{
    return csp_eval_range(st, 0, st->ps.nn);
}

// run queue until cycle boundary
index_t csp_react(csp_rt_t* st)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    index_t x1 = BAD_INDEX;
    if (st->reactive && st->es.pending_cap) {
	set_group_t* cur = st->es.pending[st->es.gen];
	uint32_t ngrp = BITSET_GROUPS(st->es.pending_cap);
	uint32_t w;
	// Swap generations FIRST: whatever a rule enqueues while we run belongs to
	// the next cycle, so it must land in the other set. (The old code took a
	// snapshot of the queue tail for the same reason.)
	st->es.gen ^= 1;
	memset(st->es.pending[st->es.gen], 0, ngrp * sizeof(set_group_t));

	// Walk the set UPWARDS. A key is (ordinal << obj_shift) | obj and ordinals
	// are handed out in instruction order, so this evaluates rules in the order
	// they are written -- which is exactly what makes a later rule override an
	// earlier one. Empty words are skipped whole, so the cost follows the
	// pending work, not the key space.
	for (w = 0; w < ngrp; w++) {
	    set_group_t bits = cur[w];
	    cur[w] = 0;              // consumed: this set is the free one next cycle
	    while (bits) {
		uint32_t b = (uint32_t)__builtin_ctz(bits);
		index_t  e = (index_t)(w * BITSET_GROUP_BITS + b);
		uint8_t  obj = QENTRY_OBJ(st, e);
		index_t  ord = QENTRY_ORD(st, e);
		index_t  ip  = st->es.rule_ip[ord];
		csp_gate_mask_t sm = st->es.rule_state[ord];
		bits &= (bits - 1);              // drop the bit we just took
		// State gate at DISPATCH, not in the rule: skip a State-scoped rule
		// (#in / NORMAL+) whose block does not include the current State.
		// sm == 0 is ungated (module bodies) and always runs. This replaces
		// the per-rule State test that used to live in every rule's condition.
		if (sm) {
		    int sv = csp_value(st, st->gsx).i;
		    if ((sv < 0) || (sv >= MAX_GATE_STATES) ||
			!((csp_gate_mask_t)(1u << sv) & sm))
			continue;
		}
		// Restore the object context so a module rule's CURRENT-relative
		// field access hits the right instance (sequential does this via
		// OP_NEW; reactive skips NEW). obj 0 = global (offs[0] == 0),
		// leaving global rules unchanged.
		st->cur = obj;
		st->cbase = st->offs[obj];
		csp_eval_rule(st, ip);
		x1 = ip;
	    }
	}
    }
    return x1;
#else
    return BAD_INDEX;
#endif
}

// Run one cycle's evaluation. The transaction model makes reactive and
// sequential yield the SAME committed state -- but only when the WHOLE program
// runs in one mode. A sequential ROM re-asserts its outputs every cycle, so a
// reactive RAM rule that overrides a ROM output would lose once its trigger
// stabilizes (the RAM rule stops firing while the ROM rule keeps writing).
// Hence reactive runs only when the whole program can: no ROM (everything is
// RAM) or the ROM carries its own precomputed graph (rom_n_edg > 0). Otherwise
// fall back to full sequential, which keeps override consistent.
// INIT is a ONE-CYCLE state. At the end of a cycle spent in it, State steps to
// NORMAL by itself, so an `#in INIT` block is setup and not a loop that re-runs
// forever -- which is what it was when nothing moved State along.
//
// A rule that assigned State this cycle WINS: the step is the default for the
// case where INIT said nothing about where to go next, so `#in INIT State = red`
// still lands in red. The dirty set is what tells the two apart -- it already
// marks every leaf written this cycle, and the write is still in the DOUT shadow
// (uncommitted), so reading the committed value here cannot see it.
//
// Written through csp_set_value rather than poked into the slot, so the reactive
// graph and the dirty set stay in step and a rule watching State still wakes.
static void state_advance(csp_rt_t* st, index_t sx)
{
    value_t v;
    if (csp_value(st, sx).i != STATE_INIT)
	return;
    if (bitset_tst(st->dset, st_index(st, sx)))
	return;
    v.i = STATE_NORMAL;
    csp_set_value(st, sx, v);
}

// Every State in the program: the global one and each object's own. An object
// re-enters INIT on its own (`safe.State = INIT ? Panic`), so its INIT block has
// to be one-shot for the same reason the global one is. A module's State is the
// first declaration inside it -- see csp_parse_module, which creates it before
// anything the user wrote.
//
// The global one comes from gsx, NOT sx. This runs on every cycle, and a module
// being typed at the prompt leaves sx pointing at that module's own State,
// CURRENT-relative, until its #end. Reading -- and worse, WRITING -- through it
// mid-definition hit whatever offs[CURRENT] happened to be, which showed up as
// the REPL wedging part-way through a pasted module.
static void states_advance(csp_rt_t* st)
{
    int m;
    uint8_t save_cur   = st->cur;
    index_t save_cbase = st->cbase;

    state_advance(st, st->gsx);
    // Each object's State is addressed CURRENT-relative, so walk the objects by
    // pointing the context at them in turn -- an encoded index cannot name one.
    // Safe here and nowhere near a rule: this runs at a cycle boundary, where
    // nothing is mid-execution with a base of its own.
    // ps.nq counts what the PARSER has seen; object[]/offs[] cover what the last
    // rebuild laid out. Those agree on a cycle (csp_cycle rebuilds first), and
    // the bound keeps a half-parsed program from reading past the tables.
    for (m = 1; (m < (int)st->obj_cap) && (m <= (int)st->ps.nq); m++) {
	index_t ix = st->object[m];
	index_t mx = decl(st, INDEX(ix), mq.mx);
	// "The first declaration inside the module" is where a State is, but not
	// every module HAS one -- a data-only namespace declares no states and is
	// built without it (csp_sys_module, and csp_parse_module's no_state). Ask
	// whether member 0 really is a State instead of assuming.
	//
	// Assuming stepped INIT->NORMAL straight into whatever member 0 was:
	// `sys.Serial` read 0 on the first cycle and 1 on every one after, with
	// nothing in the program touching it.
	if (!state_is_state_var(st, INDEX(mx) + 1))
	    continue;
	csp_ctx_set(st, m);
	state_advance(st, MAKE_INDEX(CURRENT, INDEX(mx) + 1));
    }
    st->cur   = save_cur;
    st->cbase = save_cbase;
}

index_t csp_cycle(csp_rt_t* st)
{
    // Pick up anything added since the last rebuild. Two signals: `edited` is
    // set by the add paths, and the rule-body counter is an independent check
    // that costs one comparison and needs no scan -- it catches an emission on
    // a path that forgot to mark. Doing it here, at a cycle boundary, is the
    // only safe point: mid_reset moves every derived table.
    if (st->started && (st->edited || (st->n_rule_emit != st->graph_rules)))
	csp_rebuild(st);
    // A rebuild that ran out of arena left every derived table NULL and cleared
    // `started`. Evaluating anyway read a null heap slot -- a segfault two
    // frames into states_advance, with nothing on screen to say the program had
    // outgrown the board. Refuse the cycle instead; the error is already set and
    // the driver reports it.
    if (!st->started)
	return BAD_INDEX;

    // Refresh rate, over a one-second window. Here rather than in a driver so
    // the host and the board measure the same thing the same way. csp_time_ms is
    // the clock the timers use -- with --virtual-time on the host that is
    // SIMULATED time, so the number then means cycles per simulated second; on
    // hardware it is wall clock and means what it says.
    {
	uint32_t now = csp_time_ms();
	uint32_t dt  = now - st->hz_t0;
	if (dt >= 1000) {
	    st->hz = (uint16_t)(((st->cycle - st->hz_c0) * 1000UL) / dt);
	    st->hz_t0 = now;
	    st->hz_c0 = st->cycle;
	}
    }

    // A definition still being typed is not a runnable program. `#module` emits
    // an OP_ENTER whose length is patched at its `#end`, and `#in` an OP_INSTATE
    // whose skip is patched the same way; until then those offsets are zero or
    // stale and a sweep walks straight into them. Feeding a module in line by
    // line -- which is what pasting a .csp file IS -- hung the REPL part-way
    // through, on the host and on a board alike. Evaluate nothing until it
    // closes.
    //
    // AFTER the rebuild above, not before: the derived tables (heap included)
    // are laid out just past the end of instr[], so every line that emits more
    // instructions grows the code pool towards them. Skipping the rebuild as
    // well left the heap where it was, and the next leaf write landed inside the
    // module body -- one instruction came out zeroed, which /list then rendered
    // as a NOP where a call belonged.
    // "If there is a compiler, and it is part way through a module": a node
    // that cannot be typed at can never be, so it never skips a rebuild.
    if (st->cs && ((st->cs->mdef != BAD_INDEX) || (st->cs->sdef >= 0)))
	return BAD_INDEX;

    // First cycle: force OP_CHG true so every <- binding fires once and seeds
    // its initial value. Same boundary as the reactive seed sweep below.
    st->es.seed_all = (st->cycle <= 1);
    {
	index_t x;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive && ((st->rom_nn == 0) || st->rom_nedg)) {
	    // The reactive queue is change-driven, so it starts empty. Seed it
	    // with a full sequential first cycle (csp_set_value enqueues each
	    // dependent for the next cycle); run reactively thereafter.
	    x = (st->cycle <= 1) ? csp_eval(st) : csp_react(st);
	}
	else
#endif
	    x = csp_eval(st);  // sequential ROM + RAM (override stays consistent)
	// After the rules, before the commit: the step out of INIT rides the same
	// commit as whatever INIT itself wrote, so the two are never half applied.
	states_advance(st);
	return x;
    }
}

// Walk every state slot in declaration order, newest last, calling back with
// (number, name position). States are packed CSP_STATES_PER_DECL to a block and
// filled front to back, so the running count IS the state number -- the same
// derivation state_count uses, kept in one place so the two cannot drift.
// Returns the number of states visited when the walk runs to the end.
typedef int (*state_visit_fn)(csp_rt_t*, int snum, sindex_t pos, void* arg);

NOINLINE static int state_walk(csp_rt_t* st, state_visit_fn fn, void* arg)
{
    index_t i;
    int n = 0;
    for (i = 0; i < st->ps.nd; i++) {
	csp_decl_t d = csp_get_decl(st, i);
	int k;
	if (d.type != DECL_STATES)
	    continue;
	for (k = 0; k < CSP_STATES_PER_DECL; k++) {
	    sindex_t np = csp_states_name(&d, k);
	    if (np == 0)
		continue;                  // padding at the end of a block
	    if (fn && fn(st, n, np, arg))
		return n;                  // visitor claimed this one
	    n++;
	}
    }
    return n;
}

struct state_find { const tstr_t* name; sindex_t pos; int snum; };

static int state_by_name(csp_rt_t* st, int snum, sindex_t pos, void* arg)
{
    struct state_find* f = (struct state_find*)arg;
    if (!csp_str_eq(st, pos, f->name->ptr, f->name->len))
	return 0;
    f->snum = snum;
    return 1;
}

static int state_by_num(csp_rt_t* st, int snum, sindex_t pos, void* arg)
{
    struct state_find* f = (struct state_find*)arg;
    (void)st;
    if (snum != f->snum)
	return 0;
    f->pos = pos;
    return 1;
}

// State number for `name`, or -1.
NOINLINE int lookup_state(csp_rt_t* st, const tstr_t* name)
{
    struct state_find f;
    f.name = name; f.pos = 0; f.snum = -1;
    state_walk(st, state_by_name, &f);
    return f.snum;
}

// Name position of state `snum`, or 0. The reverse, for listing.
NOINLINE sindex_t state_name_pos(csp_rt_t* st, int snum)
{
    struct state_find f;
    f.name = NULL; f.pos = 0; f.snum = snum;
    state_walk(st, state_by_num, &f);
    return f.pos;
}

static int state_by_pos(csp_rt_t* st, int snum, sindex_t pos, void* arg)
{
    struct state_find* f = (struct state_find*)arg;
    (void)st; (void)snum;
    if (pos != f->pos)
	return 0;
    f->snum = snum;
    return 1;
}

// State number of the state whose NAME is at `pos`. For a listing that is
// walking the slots of a block and needs each slot's number without searching
// by text.
NOINLINE int lookup_state_pos(csp_rt_t* st, sindex_t pos)
{
    struct state_find f;
    f.name = NULL; f.pos = pos; f.snum = -1;
    state_walk(st, state_by_pos, &f);
    return f.snum;
}

// How many states are declared.
NOINLINE int csp_num_states(csp_rt_t* st)
{
    return state_walk(st, NULL, NULL);
}
// Compare n bytes at a logical string position against a RAM string (memcmp-
// like: 0 == equal). Segment-aware per byte, so it is PROGMEM-safe on AVR where
// the ROM half of the string table lives in flash.
NOINLINE int csp_str_ncmp(csp_rt_t* st, sindex_t pos, const char* s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
	int d = (int)csp_str_byte(st, pos+i) - (uint8_t)s[i];
	if (d) return d;
    }
    return 0;
}

// True when the length-prefixed string at `pos` equals the n-byte RAM string s.
NOINLINE int csp_str_eq(csp_rt_t* st, sindex_t pos, const char* s, int n)
{
    return (csp_str_byte(st, pos-1) == (uint8_t)n) &&
	   (csp_str_ncmp(st, pos, s, n) == 0);
}

// Same, but the reference string is in flash. Kept separate from csp_str_eq
// rather than copying the RODATA name out to a stack buffer first: the callers
// sit in /state and the disassembler, once per declaration printed.
NOINLINE int csp_str_eq_ro(csp_rt_t* st, sindex_t pos, rostring_t s, int n)
{
    int i;
    if (csp_str_byte(st, pos-1) != (uint8_t)n)
	return 0;
    for (i = 0; i < n; i++) {
	if (csp_str_byte(st, pos+i) != ro_byte((rochar*)s + i))
	    return 0;
    }
    return 1;
}

// Print the length-prefixed string at logical position `pos`, byte by byte.
NOINLINE void csp_print_str_at(csp_rt_t* st, sindex_t pos)
{
    int len = csp_str_byte(st, pos-1);
    int i;
    for (i = 0; i < len; i++)
	csp_print_char(csp_str_byte(st, pos+i));
}

// look for symbol among nodes in range [start, stop)
// A states block holds up to CSP_STATES_PER_DECL names, so a name search has to
// look at every slot -- `d.name` is only the first of them. This is what makes
// ONE namespace real: a `#states` name and a `#variable` name collide here and
// csp_new_udecl reports it, instead of both existing and the meaning of the
// token depending on which lookup a call site happens to try first.
NOINLINE static int states_block_has(csp_rt_t* st, const csp_decl_t* d,
				     const tstr_t* name)
{
    int k;
    for (k = 0; k < CSP_STATES_PER_DECL; k++) {
	sindex_t np = csp_states_name(d, k);
	if ((np > 0) && csp_str_eq(st, np, name->ptr, name->len))
	    return k;
    }
    return -1;
}

NOINLINE index_t lookup_decl_in(csp_rt_t* st, const tstr_t* name,
				       int start, int stop)
{
    int i = start;
    while(i < stop) {
	csp_decl_t d = csp_get_decl(st, i);   // one read per node, not three
	if (d.type == DECL_STATES) {
	    if (states_block_has(st, &d, name) >= 0)
		return MAKE_INDEX(0,i);
	}
	else if ((d.name > 0) && csp_str_eq(st, d.name, name->ptr, name->len))
	    return MAKE_INDEX(0,i);
	if (d.type == DECL_MODULE)            // skip module def
	    i += (d.md.n+1);                  // skip elements and END
	i++;
    }
    return BAD_INDEX;
}



// Lookup for REFERENCING a name.
// How many elements the array headed by declaration `i` has: the head plus every
// declaration above it carrying `cont`. Recovering the length by a SCAN
// is what saves storing it -- csp_variable_t has no room and there is exactly one
// decl type left to spend. The runtime never calls this; it gets the count baked
// into the SETOX. The compiler needs it to emit that count, and the listing needs
// it to print `A[3]` instead of three declarations that all claim to be A.
//
// A plain variable answers 1, which is what makes `A` and `A[0]` the same thing.
NOINLINE uint16_t csp_array_len(csp_rt_t* st, index_t i)
{
    int j = INDEX(i) + 1;
    uint16_t k = 1;

    while ((j < (int)st->ps.nd) && (k < 0xffff) && decl(st, j, cont)) {
	k++;
	j++;
    }
    return k;
}

NOINLINE index_t csp_lookup_decl(csp_rt_t* st, const tstr_t* name)
{
    // Module-body scope, and only while one is being defined -- which needs a
    // compiler to be doing the defining. An image's declarations are already
    // flat by the time this runs.
    if (st->cs && (st->cs->mdef != BAD_INDEX)) {
	index_t ix;
	// Module body first, so a local shadows a global of the same name.
	if ((ix = lookup_decl_in(st, name, INDEX(st->cs->mdef)+1, st->ps.nd))
	    != BAD_INDEX)
	    return ix;
	// Then the globals declared before this module. lookup_decl_in skips
	// other modules' bodies, so their members stay private. Without this a
	// module body could not see ANY global -- not a constant, not a timer,
	// nothing.
	return lookup_decl_in(st, name, 0, INDEX(st->cs->mdef));
    }
    return lookup_decl_in(st, name, 0, st->ps.nd);
}



// each string is installed like
//  [3] 'a' 'b' 'c' '\0'
// length byte characters terminated with 0
// position return is pos efter length byte
NOINLINE int new_string(csp_rt_t* st, char* name, int len)
{
    sindex_t pos = st->ps.strp;               // logical position
    sindex_t next = pos + (len+2);
    if ((next - st->rom_strp) >= MAX_STR_BUF) {  // check RAM-local room
	csp_set_error(st, ERR_STRING_SPACE_EXHUSTED);
	return -1;
    }
    // The returned position (pos+1) is stored in a decl's NAMEPOS_BITS-wide name
    // field. rom_strp + MAX_STR_BUF can exceed that on a board with a large ROM
    // string table; fail loudly rather than truncate the field to garbage (which
    // is exactly what the old STRING_BITS-wide field did on AVR).
    if ((pos + 1) >= (sindex_t)(1u << NAMEPOS_BITS)) {
	csp_set_error(st, ERR_STRING_SPACE_EXHUSTED);
	return -1;
    }
    st->ps.strp = next;  // allocate
    ram_str_at(st, pos) = len;
    if (len > 0)                  // len==0 (empty string) may pass a NULL name
	memcpy(&ram_str_at(st, pos+1), name, len);
    ram_str_at(st, pos+1+len) = '\0';
    return pos+1;
}

// Find a string in string buffer (ROM + RAM, by logical position)
NOINLINE int lookup_string(csp_rt_t* st, char* name, int name_len)
{
    int pos = 1;  // search from pos=1 in str buf
    while(pos < st->ps.strp) {
	int len = csp_str_byte(st, pos);
	if (csp_str_eq(st, pos+1, name, name_len))
	    return pos+1;
	pos += (len+2);  // length byte and \0
    }
    return -1;
}

// True if `add` more arena bytes still fit inside the usable budget. Used bytes
// = RAM instructions + RAM declarations; this is the byte-level cap that now
// binds before (or alongside) the index-count caps.
NOINLINE int mem_fits(csp_rt_t* st, size_t add)
{
    size_t ib = (size_t)(st->ps.nn - st->rom_nn) * sizeof(csp_instr_t);
    size_t db = (size_t)(st->ps.nd - st->rom_nd) * sizeof(csp_decl_t);
    return (ib + db + add) <= st->mem_limit;
}

NOINLINE static index_t next_decl_index(csp_rt_t* st)
{
    index_t ix;
    // ps.nd is a LOGICAL count (ROM base + RAM); RAM storage is ram_decl[local].
    // Two caps: the index encoding (MAX_DECLS) and the arena byte budget.
    if ((st->ps.nd - st->rom_nd) >= MAX_DECLS || !mem_fits(st, sizeof(csp_decl_t))) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return BAD_INDEX;
    }
    ix = MAKE_INDEX(0, st->ps.nd);
    st->ps.nd++;
    return ix;
}

// install a new decl (default to INTEGER 32 bit

NOINLINE index_t csp_new_decl(csp_rt_t* st, const tstr_t* name, decl_t type,
			      int sys)
{
    index_t ix;
    int i, pos;

    if ((ix = next_decl_index(st)) == BAD_INDEX)
	return BAD_INDEX;
    pos = 0;
    if (name != NULL) {
	if ((pos = new_string(st, name->ptr, name->len)) < 0)
	    return BAD_INDEX;
    }
    i = INDEX(ix);
    // Clear the slot before filling it. The pool is REUSED -- a /clear, a ROM
    // load or a parse rollback rewinds ps.nd and the next declaration lands on
    // physical storage the previous one wrote -- and this only sets four of the
    // common fields, so every other bit was inherited from whatever was there.
    //
    // That was harmless while every arm wrote all of its own fields. It stopped
    // being harmless with DECL_STATES, which uses bits 26..31 as the low six of
    // name3: a states block holding FAILSAFE (string position 23 == 0b010111)
    // left bound = 1 behind, and the next variable to take that slot was set up
    // as a bit-field view into a buffer instead of getting storage of its own.
    // It read 0 whatever its initialiser said.
    memset(ram_decl_at(st,i), 0, sizeof(csp_decl_t));
    ram_decl_at(st,i)->type = type;
    // ram_decl_at(st,i)->sys = sys;
    ram_decl_at(st,i)->name = pos;
    ram_decl_at(st,i)->res = MAKE_RES(8*sizeof(value_t));
    ram_decl_at(st,i)->vt = V_INTEGER;
    return i;
}

// Build reactive dependency graph: declaration -> rules that depend on it
// When a declaration changes, we enqueue all rules that read from it (via LD)
// --- middle-region bump allocator ------------------------------------------
// Reset the middle to the gap between the code growing in from both ends. Called
// once per csp_rebuild; every derived table is then handed out from here, so a
// rebuild simply forgets the old layout instead of freeing eleven blocks.
NOINLINE void csp_mid_reset(csp_rt_t* st)
{
    size_t ib = (size_t)(st->ps.nn - st->rom_nn) * sizeof(csp_instr_t);
    size_t db = (size_t)(st->ps.nd - st->rom_nd) * sizeof(csp_decl_t);
    st->mid_base = CSP_A8(ib + CSP_SCRATCH);
    st->mid      = st->mid_base;
    st->mid_full = 0;
    // The decl end grows DOWN from the top of the pool, so the middle must stop
    // short of it. Guard the subtraction: a program that already fills the pool
    // leaves no middle at all rather than wrapping to a huge end.
    if (st->mem_limit > (db + CSP_SCRATCH + st->mid_base))
	st->mid_end = st->mem_limit - db - CSP_SCRATCH;
    else
	st->mid_end = st->mid_base;
}

// Hand out `n` bytes from the middle, 8-aligned so every table start is aligned
// regardless of what preceded it. NULL when the middle is exhausted -- the caller
// reports it, and mid_full makes the reason visible in /memory rather than
// looking like a mysterious out-of-memory.
NOINLINE void* csp_mid_alloc(csp_rt_t* st, size_t n)
{
    void* p;
    n = CSP_A8(n ? n : 1);
    if ((st->mid + n) > st->mid_end) {
	st->mid_full = 1;
	return NULL;
    }
    p = st->mem + st->mid;
    st->mid += n;
    memset(p, 0, n);
    return p;
}

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
// If `ip` is a #in block gate -- LD State ; NINSTATE* ; INSTATE (open_in_block)
// -- return the ip just PAST it (the block's first real instruction); else `ip`
// unchanged. The reactive entry point must skip the gate: csp_react runs a rule
// with ONE csp_eval_rule call, but a gate jumps by `return n+nxt` (INSTATE skips,
// NINSTATE enters), which would END the call before the body ran. The block's
// State membership is enforced at dispatch instead (see csp_react); sequential
// still walks the gate linearly.
NOINLINE static int skip_gate(csp_rt_t* st, int ip, int hi)
{
    // Only a GLOBAL-State gate (LD reads st->gsx). A module's own #in gates on the
    // object's per-instance State (a different decl); those keep their gate at the
    // reactive entry and are handled the old way (their INSTATE falls through).
    csp_instr_t g, g1;

    if (ip + 1 >= hi)                   // keeps the original short-circuit: no
	return ip;                      // instruction read when the range is short
    g  = csp_get_instr(st, ip);
    g1 = csp_get_instr(st, ip+1);
    if ((g.op == OP_LD) && (g.m.mem == st->gsx) &&
	((g1.op == OP_NINSTATE) || (g1.op == OP_INSTATE))) {
	int j = ip + 1;
	while (j < hi) {                      // one read per instruction: the old
	    opcode_t o = csp_get_instr(st, j).op;  // form re-read the word the loop
	    if (o == OP_NINSTATE) { j++; continue; }   // had just looked at
	    if (o == OP_INSTATE) j++;
	    break;
	}
	return j;
    }
    return ip;
}

// True if instruction i is a #in block gate's LD State (LD st->gsx immediately
// before an INSTATE/NINSTATE). That LD belongs to the gate, not to a rule's data
// dependencies -- csp_csr adds the State edge per gated rule from rule_state.
NOINLINE static int is_gate_ld(csp_rt_t* st, int i)
{
    csp_instr_t g = csp_get_instr(st, i);
    csp_instr_t g1;

    if ((g.op != OP_LD) || (g.m.mem != st->gsx) || (i + 1 >= (int)st->ps.nn))
	return 0;
    g1 = csp_get_instr(st, i+1);
    return (g1.op == OP_INSTATE) || (g1.op == OP_NINSTATE);
}

// csp_csr pass 3: add the State -> `ord` edge for a State-gated rule, with the
// same consecutive-duplicate dedup the instruction fills use. Paired with the
// per-gated-ordinal count in pass 1.
NOINLINE static void add_state_edge(csp_rt_t* st, index_t* wr, int ord)
{
    index_t sd = INDEX(st->gsx);
    if (st->es.rule_state[ord] && (sd < st->ps.nd) &&
	((wr[sd] == st->es.ofs[sd]) || (st->es.edg[wr[sd]-1] != (index_t)ord)))
	st->es.edg[wr[sd]++] = (index_t)ord;
}

// Number the rule bodies of instruction range [lo,hi) in scan order, continuing
// from ordinal `ord`. A body starts at the range base (code before any NEXT/ENTER
// belongs to it -- csp_csr seeds current_rule with the base for exactly that
// reason) and again after every NEXT/ENTER. Fills rule_ip[ord] = ip when given a
// table, otherwise just counts. Returns the next free ordinal. rule_ip is the
// reactive entry point, so it points PAST any block gate at the body start.
//
// Calling this on [0,rom_nn) reproduces the ordinals a baked rom_edg holds: the
// dump ran this same walk over the same instructions with rom_nn == 0.
NOINLINE static int number_rules(csp_rt_t* st, int lo, int hi,
				 index_t* rule_ip, int ord)
{
    int i;
    if (rule_ip != NULL) rule_ip[ord] = skip_gate(st, lo, hi);
    ord++;                              // the implicit body at the range base
    for (i = lo; i < hi; i++) {
	opcode_t o = instr(st, i, op);
	if ((o == OP_NEXT) || (o == OP_ENTER)) {
	    if (rule_ip != NULL) rule_ip[ord] = skip_gate(st, i+1, hi);
	    ord++;
	}
    }
    return ord;
}

// If `ip` is a #in block gate, return the OR of its states as a bitmask (bit
// snum) and set *bend to the ip past the block; else return 0 (and leave *bend).
NOINLINE static csp_gate_mask_t gate_mask(csp_rt_t* st, int ip, int hi, int* bend)
{
    // Global-State gates only (see skip_gate): an object's own #in gates on its
    // per-instance State, which this global-State mask cannot represent.
    csp_instr_t g, g1;

    if (ip + 1 >= hi)                   // as in skip_gate: short range, no read
	return 0;
    g  = csp_get_instr(st, ip);
    g1 = csp_get_instr(st, ip+1);
    if ((g.op == OP_LD) && (g.m.mem == st->gsx) &&
	((g1.op == OP_NINSTATE) || (g1.op == OP_INSTATE))) {
	csp_gate_mask_t m = 0;
	int j = ip + 1;
	while (j < hi) {
	    csp_instr_t gj = csp_get_instr(st, j);
	    if (gj.op != OP_NINSTATE)
		break;
	    m |= (csp_gate_mask_t)(1u << gj.in.imm);
	    j++;
	}
	if (j < hi) {
	    csp_instr_t gj = csp_get_instr(st, j);
	    if (gj.op == OP_INSTATE) {
		m |= (csp_gate_mask_t)(1u << gj.in.imm);
		*bend = j + gj.in.nxt;
	    }
	}
	return m;
    }
    return 0;
}

// True if the body starting at `ip` is a bare NORMAL+ rule (its OP_RULE carries
// the implicit flag). Scans only within the body (up to the closing NEXT).
NOINLINE static int body_implicit(csp_rt_t* st, int ip, int hi)
{
    int i;
    for (i = ip; i < hi; i++) {
	csp_instr_t ci = csp_get_instr(st, i);
	if (ci.op == OP_RULE) return ci.r.implicit;
	if ((ci.op == OP_NEXT) || (ci.op == OP_ENTER)) break;
    }
    return 0;
}

// Fill rule_state[ord] with each rule body's State membership mask, mirroring
// number_rules' ordinal walk. The mask is the enclosing #in block's states
// (gate_mask), or {INIT,NORMAL} for a bare NORMAL+ rule (body_implicit), or 0
// for an ungated rule (module bodies, run whenever their object is active). This
// is what csp_react gates on -- so no per-rule State test lives in the stream.
NOINLINE static int number_rule_states(csp_rt_t* st, int lo, int hi,
				       csp_gate_mask_t* rs, int ord)
{
    const uint16_t normal_plus =
	(uint16_t)((1u << STATE_INIT) | (1u << STATE_NORMAL));
    uint16_t mask = 0;
    int bend = -1, i;

    mask = gate_mask(st, lo, hi, &bend);
    rs[ord] = body_implicit(st, lo, hi) ? normal_plus : mask;
    ord++;
    for (i = lo; i < hi; i++) {
	opcode_t o = instr(st, i, op);
	if ((o == OP_NEXT) || (o == OP_ENTER)) {
	    int p = i + 1;
	    uint16_t gm;
	    if ((bend >= 0) && (p >= bend)) { mask = 0; bend = -1; }
	    gm = gate_mask(st, p, hi, &bend);
	    if (gm) mask = gm;
	    rs[ord] = body_implicit(st, p, hi) ? normal_plus : mask;
	    ord++;
	}
    }
    return ord;
}
#endif

void csp_csr(csp_rt_t* st)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int i;
    int r_rom = 0;                 // ordinals [0,r_rom) belong to baked ROM rules
    int current_rule = -1;
    int ord = 0;                   // ordinal of the rule body being scanned
    int next_ord = 0;              // next ordinal to hand out
    index_t* wr;                   // pass-3 write cursors (into the idg block)
    // timeout(T)/elapsed(T)/... pass the timer as an immediate index (LI -> ARG
    // -> CALL), not an OP_LD, so track LI values per register and per arg slot to
    // recover the timer a CALL depends on and give it a graph edge.
    index_t reg_imm[MAX_REGS];
    index_t arg_imm[MAX_ARGS];
    int n = st->ps.nd;             // graph nodes = declarations

    // Size the graph to the actual node/edge counts. idg[n], ofs[n+1] and the
    // pass-3 write cursors wr[n] share one bump block (idg is its base). edg is
    // sized after the counting pass. Nothing is freed -- csp_rebuild resets the
    // middle. wr used to be an index_t[MAX_DECLS] local -- 4K of stack at
    // DECL_BITS=11, and the last thing dimensioned by that width.
    st->es.edg = NULL;
    st->es.graph_n = 0;               // enq skips the graph until it is fully built
    st->es.idg = (index_t*)csp_mid_alloc(st, (size_t)(n + (n+1) + n) * sizeof(index_t));
    if (st->es.idg == NULL) { st->es.ofs = NULL; return; }  // middle full: no graph
    st->es.ofs = st->es.idg + n;
    wr      = st->es.ofs + (n+1);

    // Number every rule body 0..n_rule-1 and build the ordinal -> ip map. ROM
    // first (so its ordinals match what rom_edg was baked with), then RAM.
    st->es.rule_ip = NULL;
    st->es.rule_state = NULL;
    st->es.n_rule = 0;
    {
	int nr;
	if (st->rom_nn > 0)
	    r_rom = number_rules(st, 0, st->rom_nn, NULL, 0);
	nr = number_rules(st, st->rom_nn, st->ps.nn, NULL, r_rom);
	st->es.rule_ip = (index_t*)csp_mid_alloc(st, (size_t)nr * sizeof(index_t));
	if (st->es.rule_ip == NULL) { st->es.ofs = NULL; return; }  // middle full
	if (st->rom_nn > 0)
	    number_rules(st, 0, st->rom_nn, st->es.rule_ip, 0);
	number_rules(st, st->rom_nn, st->ps.nn, st->es.rule_ip, r_rom);
	// Parallel: each ordinal's State membership mask (gates reactive dispatch).
	st->es.rule_state = (uint16_t*)csp_mid_alloc(st, (size_t)nr * sizeof(uint16_t));
	if (st->es.rule_state == NULL) { st->es.ofs = NULL; return; }  // middle full
	if (st->rom_nn > 0)
	    number_rule_states(st, 0, st->rom_nn, st->es.rule_state, 0);
	number_rule_states(st, st->rom_nn, st->ps.nn, st->es.rule_state, r_rom);
	st->es.n_rule = (index_t)nr;
    }

    // Size the two pending sets to the (ordinal, object) key space. Two things
    // keep it small. Keying by ORDINAL rather than raw ip: by ip the space would
    // be MAX_INSTRS wide per object and almost all holes, since only rule bodies
    // can ever be pending. And sizing the object field to the objects that EXIST
    // rather than to OBJ_BITS: a program with none gets a 0-bit field (one slot
    // per rule) instead of reserving all 32.
    // On failure pending_cap stays 0 and csp_enq drops the mark -- reactive then
    // does nothing, which is visible rather than silently wrong.
    {
	size_t bits, grp;
	st->es.obj_shift = 0;
	while ((1u << st->es.obj_shift) < (unsigned)(st->ps.nq + 1))
	    st->es.obj_shift++;
	bits = (size_t)st->es.n_rule << st->es.obj_shift;
	grp  = BITSET_GROUPS(bits);
	st->es.pending[0] = (set_group_t*)csp_mid_alloc(st, grp * sizeof(set_group_t));
	st->es.pending[1] = (set_group_t*)csp_mid_alloc(st, grp * sizeof(set_group_t));
	if (!st->es.pending[0] || !st->es.pending[1])
	    st->es.pending_cap = 0;
	else
	    st->es.pending_cap = (uint32_t)bits;
	st->es.gen = 0;
    }

    // Clear in-degree counts
    memset(st->es.idg, 0, n * sizeof(index_t));
    memset(reg_imm, 0, sizeof(reg_imm));
    memset(arg_imm, 0, sizeof(arg_imm));

    // Pass 1: Count how many rules depend on each declaration
    // A rule depends on a declaration if it contains an LD from that declaration
    // Only RAM rules go into the runtime graph; ROM rules run sequentially (or
    // from their own baked graph). Scan from the RAM instruction base.
    current_rule = st->rom_nn;
    for (i = st->rom_nn; i < st->ps.nn; i++) {
	csp_instr_t ci = csp_get_instr(st, i);   // one read per instruction
	switch (ci.op) {
	case OP_RULE:
	    current_rule = -1;
	    break;
	case OP_NEXT:
	    current_rule = i+1;
	    break;
	case OP_LD:
	case OP_CHG:
	    if ((current_rule >= 0) && !is_gate_ld(st, i)) {
		index_t mem = INDEX(ci.m.mem);
		if (mem < st->ps.nd) {
		    st->es.idg[mem]++;
		}
	    }
	    break;
//	case OP_EQI:
//	    if (current_rule >= 0) {
//		index_t mem = INDEX(ci.mi.mem);
//		if (mem < st->ps.nd) {
//		    st->es.idg[mem]++;
//		}
//	    }
//	    break;
	case OP_LI:
	case OP_LIU:
	    reg_imm[ci.i.x] = (index_t)ci.i.imm;
	    break;
	case OP_ARG:
	    arg_imm[ci.i.imm] = reg_imm[ci.i.x];
	    break;
	case OP_CALL:   // timer args (timeout(T), ...) become timer -> rule edges
	    if (current_rule >= 0) {
		uint16_t avt = ci.f.avt;
		int a;
		for (a = 0; a < MAX_ARGS; a++) {
		    if (((avt >> (a*4)) & 0xf) == V_TIMER) {
			index_t mem = INDEX(arg_imm[a]);
			if (mem < st->ps.nd)
			    st->es.idg[mem]++;
		    }
		}
	    }
	    break;
	case OP_ENTER:
	    current_rule = i+1;
	    break;
	case OP_LEAVE:
	    current_rule = -1;  // reset at module boundaries
	    break;
	default:
	    break;
	}
    }
    // State dependency for every State-gated RAM rule -- replaces the folded EQI's
    // edge (gate LD skipped above). One per gated ordinal; over-counting is fine,
    // pass 3 leaves holes (see below).
    {
	index_t sd = INDEX(st->gsx);
	int o;
	if (sd < st->ps.nd)
	    for (o = r_rom; o < (int)st->es.n_rule; o++)
		if (st->es.rule_state[o])
		    st->es.idg[sd]++;
    }

    // Pass 2: Calculate offsets into edge array
    st->es.ofs[0] = 0;
    for (i = 0; i < st->ps.nd; i++) {
	st->es.ofs[i+1] = st->es.ofs[i] + st->es.idg[i];
    }

    // Now the total edge count is known -> size edg exactly. It comes out zeroed
    // because pass 3 leaves HOLES: pass
    // 1 counts every reference but pass 3 drops consecutive duplicates, so decl i
    // fills only [ofs[i], ofs[i]+idg[i]) and the rest of its span stays unwritten.
    // enq_elist reads only the filled part, but csp_dump_code bakes the whole
    // array into rom_edg -- leaving it uninitialised put heap garbage in flash.
    {
	size_t edges = st->es.ofs[st->ps.nd];
	st->es.edg = (index_t*)csp_mid_alloc(st, (edges ? edges : 1) * sizeof(index_t));
	if (st->es.edg == NULL) return;   // middle full: leave idg/ofs, no edges
    }

    // Pass 3: Fill in rule ORDINALS for each declaration. edg stores the ordinal,
    // not the ip, so csp_enq needs no lookup (and dumping bakes rom_edg with the
    // same numbering). `ord` walks in lockstep with current_rule, matching the
    // order number_rules handed ordinals out in: the implicit body at the RAM
    // base first (r_rom), then one per NEXT/ENTER.
    memcpy(wr, st->es.ofs, st->ps.nd * sizeof(index_t));
    memset(reg_imm, 0, sizeof(reg_imm));
    memset(arg_imm, 0, sizeof(arg_imm));

    // Only RAM rules go into the runtime graph; ROM rules run sequentially (or
    // from their own baked graph). Scan from the RAM instruction base.
    current_rule = st->rom_nn;
    ord = r_rom;
    next_ord = r_rom + 1;
    add_state_edge(st, wr, ord);        // the implicit body at the RAM base
    for (i = st->rom_nn; i < st->ps.nn; i++) {
	csp_instr_t ci = csp_get_instr(st, i);   // one read per instruction
	switch (ci.op) {
	case OP_RULE:
	    current_rule = -1;
	    break;
	case OP_NEXT:
	    current_rule = i+1;
	    ord = next_ord++;
	    add_state_edge(st, wr, ord);    // State edge first, so a user State
	    break;                          // read in the condition dedups against it
	case OP_LD:
	case OP_CHG:
	    if ((current_rule >= 0) && !is_gate_ld(st, i)) {
		index_t mem = INDEX(ci.m.mem);
		if (mem < st->ps.nd &&
		    (wr[mem] == st->es.ofs[mem] || st->es.edg[wr[mem]-1] != ord))
		    st->es.edg[wr[mem]++] = ord;
	    }
	    break;
//	case OP_EQI:
//	    if (current_rule >= 0) {
//		index_t mem = INDEX(ci.mi.mem);
//		if (mem < st->ps.nd &&
//		    (wr[mem] == st->es.ofs[mem] || st->es.edg[wr[mem]-1] != ord))
//		    st->es.edg[wr[mem]++] = ord;
//	    }
//	    break;
	case OP_LI:
	case OP_LIU:
	    reg_imm[ci.i.x] = (index_t)ci.i.imm;
	    break;
	case OP_ARG:
	    arg_imm[ci.i.imm] = reg_imm[ci.i.x];
	    break;
	case OP_CALL:   // timer args (timeout(T), ...) become timer -> rule edges
	    if (current_rule >= 0) {
		uint16_t avt = ci.f.avt;
		int a;
		for (a = 0; a < MAX_ARGS; a++) {
		    if (((avt >> (a*4)) & 0xf) == V_TIMER) {
			index_t mem = INDEX(arg_imm[a]);
			if (mem < st->ps.nd &&
			    (wr[mem] == st->es.ofs[mem] || st->es.edg[wr[mem]-1] != ord))
			    st->es.edg[wr[mem]++] = ord;
		    }
		}
	    }
	    break;
	case OP_ENTER:  // start of object
	    current_rule = i+1;
	    ord = next_ord++;
	    break;
	case OP_LEAVE:  // end of object
	    current_rule = -1;
	    break;
	default:
	    break;
	}
    }
    // Compact. Pass 1 counts every reference but pass 3 keeps only distinct
    // consecutive ones, so each decl's span ends in unwritten holes. Slide the
    // edges down and rebuild ofs/idg dense: enq_elist gets a tighter walk, edg
    // shrinks to the real edge count, and csp_dump_code (which bakes the whole
    // [0,ofs[nd]) span into rom_edg) no longer emits hole padding.
    // Safe in place: the new write cursor never overtakes the read base, since
    // every decl's deduplicated count is <= the counted one.
    {
	index_t w = 0;
	for (i = 0; i < st->ps.nd; i++) {
	    index_t base = st->es.ofs[i];             // read BEFORE overwriting it
	    index_t cnt  = wr[i] - base;           // edges pass 3 actually wrote
	    index_t j;
	    st->es.ofs[i] = w;
	    for (j = 0; j < cnt; j++)
		st->es.edg[w++] = st->es.edg[base+j];
	    st->es.idg[i] = cnt;
	}
	st->es.ofs[st->ps.nd] = w;
	// Give the holes back. edg is the last thing bumped so far, so the cursor
	// can simply be rewound over them -- a bump allocator can free its top.
	st->mid = (size_t)((uint8_t*)st->es.edg - st->mem) + CSP_A8((size_t)w * sizeof(index_t));
    }
    st->es.graph_n = n;   // graph is complete: enq may now read it for ix < n

#endif
}

// The section pointers of one image, derived from its base. The runtime never
// names an image's struct type -- it takes the base and works in offsets, so
// the same code loads rom, a FAILSAFE, or a copy someone flashed onto a spare
// page. Two ways to fill it in: from the header (fast) or by walking the
// section prologues (when the header is the casualty).
/* img_p_t now lives in csp.h -- csp_rt_t holds one (rom_p). */

static void img_from_hdr(const uint8_t* base, const csp_image_header_t* h,
			 img_p_t* p)
{
    p->str    = (const char*)      (base + h->ofs_str);
    p->decl   = (const csp_decl_t*)(base + h->ofs_decl);
    p->instr  = (const csp_instr_t*)(base + h->ofs_instr);
    p->idg    = (const index_t*)   (base + h->ofs_idg);
    p->ofs    = (const index_t*)   (base + h->ofs_ofs);
    p->edg    = (const index_t*)   (base + h->ofs_edg);
}

// Walk the section prologues and fill in what is found. Touches NOTHING in the
// header -- the first section starts at sizeof(csp_image_header_t), each
// prologue says what follows and how many bytes it is, and the next prologue is
// right after. This is what makes a rotten header survivable: the offsets it
// carries are the fast path, not the only path.
//
// An UNKNOWN tag is skipped, not fatal -- `len` is in bytes precisely so a
// reader can step over a section it does not understand. That is what lets this
// walk survive an image written by a later generator.
//
// Bounded by CSP_SECT_MAXWALK prologues and by a length that has to advance, so
// a corrupt prologue stops the walk instead of running away through flash.
// ---------------------------------------------------------------------------
// ROM HEADER RECOVERY -- optional, see CSP_ROM_RECOVER in csp_config.h.
//
// Everything down to the end of rom_scan_state exists for ONE case: crc_hdr is
// damaged but the sections themselves are whole. Then the header's counts, CRCs
// and offsets are all suspect, so the offsets are recovered by walking the
// section prologues and each section proves itself through its own end marker.
//
// A damaged SECTION is not recoverable and never was -- the marker folds the
// same bad bytes -- so that case rejects with or without this code.
//
// It costs about 1 040 bytes: img_from_walk plus four scanners. On a part where
// that is 3 % of the flash it is cheap insurance; on one where it is 19 % of
// what you are short, it is a choice. Turning it off leaves the reject path,
// which is what a damaged section already takes: say so and run empty, and let
// the FAILSAFE ladder take over.
// ---------------------------------------------------------------------------
#if defined(CSP_ROM_RECOVER) && (CSP_ROM_RECOVER==1)

#define CSP_SECT_MAXWALK 16
#define CSP_SECT_NEEDED   7

static int img_from_walk(const uint8_t* base, img_p_t* p)
{
    uint32_t off = (uint32_t)sizeof(csp_image_header_t);
    int seen = 0;
    int i;

    memset(p, 0, sizeof(*p));
    for (i = 0; i < CSP_SECT_MAXWALK; i++) {
	csp_sect_t sc = ro_sect((const csp_sect_t*)(base + off));
	const uint8_t* data = base + off + sizeof(csp_sect_t);
	if (sc.len == 0 || sc.len > (uint32_t)MAX_INSTRS * sizeof(csp_instr_t))
	    break;                             // nonsense length: stop here
	if (csp_tag_is(sc.tag, CSP_SECT_DECL))    { p->str    = (const char*)data;        seen++; }
	else if (csp_tag_is(sc.tag, CSP_SECT_DECL))   { p->decl   = (const csp_decl_t*)data;  seen++; }
	else if (csp_tag_is(sc.tag, CSP_SECT_INSTR))  { p->instr  = (const csp_instr_t*)data; seen++; }
	else if (csp_tag_is(sc.tag, CSP_SECT_IDG))    { p->idg    = (const index_t*)data;     seen++; }
	else if (csp_tag_is(sc.tag, CSP_SECT_OFS))    { p->ofs    = (const index_t*)data;     seen++; }
	else if (csp_tag_is(sc.tag, CSP_SECT_EDG))    { p->edg    = (const index_t*)data;     seen++; }
	/* else: a section this build does not know -- step over it. A v9 image
	   still carries a 'STAT' section and lands in exactly this arm. */
	off += (uint32_t)sizeof(csp_sect_t) + sc.len;
	if (seen == CSP_SECT_NEEDED)
	    return 1;
    }
    return 0;
}

// True when firmware with executable rules is linked in (rom.c).
#endif /* CSP_ROM_RECOVER -- rom_verify/rom_graph_ok below are NOT optional */

int csp_has_firmware(void)
{
    const uint8_t* base = ro_ref(&rom_image).base;
    return ro_header((const csp_image_header_t*)base).n_instr > 0;
}

// Verify the image against its baked header, section by section. Returns 0 on
// success, or a rostring naming the corrupt section for the caller to print.
// Per-section so a flipped flash cell is PINPOINTED, not just "corrupt". The
// header itself is checked first (crc_hdr over every byte above it -- magic,
// size, role, generation, counts, section CRCs and the offsets), so neither a
// corrupt count nor a corrupt offset can mislead the section checks. Byte-CRC
// is valid because host and target are both little-endian and str/decl/instr/
// state are all byte-stable (see the CSP_STATIC_ASSERT at csp_crc16).
static rostring_t rom_verify(const img_p_t* p, const csp_image_header_t* h)
{
    if (csp_crc16(0xFFFF, h, sizeof(*h) - sizeof(uint16_t), 0) != h->crc_hdr)
	return ros_hdr;
    if (csp_crc16(0xFFFF, p->str, h->n_str, 1) != h->crc_str)
	return ros_str;
    if (csp_crc16(0xFFFF, p->decl, (size_t)h->n_decl * sizeof(csp_decl_t), 1)
	!= h->crc_decl)
	return ros_decl;
    if (csp_crc16(0xFFFF, p->instr, (size_t)h->n_instr * sizeof(csp_instr_t), 1)
	!= h->crc_instr)
	return ros_instr;
    // NOTE: the reactive graph is checked SEPARATELY (rom_graph_ok), not here --
    // a corrupt graph is recoverable (run sequential), a corrupt anything-else is
    // not. Keeping it out of rom_verify keeps this function "fatal sections only".
    return NULL;
}

// Verify the reactive-graph section on its own. Returns 1 if intact (or absent),
// 0 if corrupt. Separate from rom_verify because a bad graph is NOT fatal: the
// graph only tells the reactive scheduler which rules a change wakes -- pure
// optimization. If just it is corrupt the instructions and decls are still
// trustworthy, so csp_load_image drops the graph (rom_nedg = 0) and the program
// runs full sequential (csp_cycle -> csp_eval), which the transaction model
// guarantees yields the same committed state. Degrade toward running, not dead.
// The three arrays are folded in the same order and sizes the generator used:
// idg[n_decl], ofs[n_decl+1], edg[n_edg].
static int rom_graph_ok(const img_p_t* p, const csp_image_header_t* h)
{
    uint16_t crc;
    if (h->n_edg == 0)
	return 1;   // no graph: stub sections, never read -- nothing to verify
    crc = csp_crc16(0xFFFF, p->idg, (size_t)h->n_decl * sizeof(index_t), 1);
    crc = csp_crc16(crc, p->ofs, (size_t)(h->n_decl + 1) * sizeof(index_t), 1);
    crc = csp_crc16(crc, p->edg, (size_t)h->n_edg * sizeof(index_t), 1);
    return crc == h->crc_graph;
}

// --- header-corruption recovery: self-verifying section END markers ---------
// When crc_hdr fails, the header's counts, CRCs and OFFSETS are all suspect. The
// offsets are recovered by walking the prologues (img_from_walk); the counts and
// the integrity come from the sections themselves. decl and instr each end with
// a self-verifying marker (DECL_END_MARK / OP_END_MARK, see csp_dump_code) whose
// crc covers the section data + itself with the crc field zeroed, and str/state
// carry the same idea as a sentinel plus trailer. Scanning for the marker
// recovers the entry count (its position) AND confirms integrity, INDEPENDENT of
// the header. Each scan is bound by the array's compile-time max, so a missing
// or corrupt marker cannot read unboundedly.

#if defined(CSP_ROM_RECOVER) && (CSP_ROM_RECOVER==1)
// Recover the decl entry count via its END marker; -1 if not intact.
NOINLINE static int rom_scan_decl(const csp_decl_t* decl)
{
    int p;
    if (decl == NULL) return -1;
    // CSP_PROVISION_DECLS, not MAX_DECLS: the encoding ceiling is 32768 entries
    // now, and a header-less scan that walks that far past a corrupt marker reads
    // 256K of flash that no target has. The bound is "no image is bigger than
    // this", which is what MAX_DECLS happened to mean when the scan was written.
    for (p = 0; p <= CSP_PROVISION_DECLS; p++) {
	csp_decl_t m = ro_decl(&decl[p]);
	if (m.type == DECL_END_MARK) {
	    // csp_decl_t m = ro_decl(&decl[p]);
	    uint16_t stored = m.em.crc, crc;
	    m.em.crc = 0;                          // the crc field reads 0 in the fold
	    crc = csp_crc16(0xFFFF, decl, (size_t)p * sizeof(csp_decl_t), 1);
	    crc = csp_crc16(crc, &m, sizeof(m), 0);   // marker copy is in RAM
	    return (crc == stored) ? p : -1;
	}
    }
    return -1;
}

// Recover the instr entry count via its END marker; -1 if not intact.
NOINLINE static int rom_scan_instr(const csp_instr_t* instr)
{
    int p;
    if (instr == NULL) return -1;
    for (p = 0; p <= MAX_INSTRS; p++) {
	csp_instr_t m = ro_instr(&instr[p]);
	if (m.op == OP_END_MARK) {
	    uint16_t stored = m.em.crc, crc;
	    m.em.crc = 0;
	    crc = csp_crc16(0xFFFF, instr, (size_t)p * sizeof(csp_instr_t), 1);
	    crc = csp_crc16(crc, &m, sizeof(m), 0);
	    return (crc == stored) ? p : -1;
	}
    }
    return -1;
}

// Recover the str byte count via its 0xFF sentinel trailer (0xFF is never a
// valid length byte or ASCII char), then verify the 2-byte CRC after it. -1 if
// not intact. Bound by the name-position range.
NOINLINE static int rom_scan_str(const char* str)
{
    int p;
    if (str == NULL) return -1;
    for (p = 0; p < (1 << NAMEPOS_BITS); p++) {
	if ((uint8_t)ro_byte((const uint8_t*)&str[p]) == 0xFF) {
	    uint16_t stored = (uint8_t)ro_byte((const uint8_t*)&str[p+1]) |
			      ((uint16_t)(uint8_t)ro_byte((const uint8_t*)&str[p+2]) << 8);
	    return (csp_crc16(0xFFFF, str, p, 1) == stored) ? p : -1;
	}
    }
    return -1;
}

#endif /* CSP_ROM_RECOVER */

// Activate an image: run it in place from flash by setting the RAM base offsets
// to the image's sizes. No copy -- csp_get_decl/instr read flash for logical
// indices below the base. The parse_file DECL_END terminator at the end of the
// image is dropped so RAM decls append seamlessly.
//
// `base` is the first byte of the image object; everything else is reached by
// offset from it, so this is the same code for the linked rom, a FAILSAFE bank
// or a copy found by scanning flash.
NOINLINE void csp_load_image(csp_rt_t* st, const uint8_t* base)
{
    csp_image_header_t h = ro_header((const csp_image_header_t*)base);
    img_p_t p;
    rostring_t bad;
    index_t nd;

    if ((h.magic[0] != CSP_IMAGE_MAGIC0) || (h.magic[1] != CSP_IMAGE_MAGIC1) ||
	(h.magic[2] != CSP_IMAGE_MAGIC2) || (h.magic[3] != CSP_IMAGE_MAGIC3))
	return;                   // not an image at all
    if (h.n_decl == 0)            // no firmware linked
	return;
    nd = h.n_decl;
    img_from_hdr(base, &h, &p);
    // Reject a stale or corrupt generate before touching ps.*: an incompatible
    // image would otherwise be read as garbage decls/instructions. Version
    // catches "generated by an older csp" (layout changed); the per-section CRC
    // catches a damaged flash image and names the section. Either way run EMPTY
    // (return before rom_* are set) with a message, so the board is usable and
    // the cause is visible instead of a silent crash.
    if (h.version != ROM_FORMAT_VERSION) {
	csp_print_lit("ROM rejected: format ");
	csp_print_uint(h.version);
	csp_print_lit(", firmware expects ");
	csp_print_uint(ROM_FORMAT_VERSION);
	csp_print_line(" -- regenerate the image");
	return;
    }
    if ((bad = rom_verify(&p, &h)) != NULL) {
	// Recover ONLY when the header itself (crc_hdr) is the casualty: then its
	// counts, CRCs and offsets are all suspect, but the sections may be whole.
	// Walk the prologues for the offsets, then let each section prove itself
	// through its own marker. A DATA corruption (a section CRC failing with
	// crc_hdr intact) is NOT recoverable -- the marker folds the same bad
	// bytes -- so those still reject.
	int rnd = -1, rnn = -1, rns = -1;
#if defined(CSP_ROM_RECOVER) && (CSP_ROM_RECOVER==1)
	if (bad == ros_hdr) {
	    img_p_t w;
	    if (img_from_walk(base, &w)) {
		p = w;
		rnd     = rom_scan_decl(p.decl);
		rnn     = rom_scan_instr(p.instr);
		rns     = rom_scan_str(p.str);
	    }
	}
#endif
	// With recovery off the three stay -1, so this is dead and the reject
	// below is the only outcome -- exactly what a damaged section gets.
	if ((rnd >= 0) && (rnn >= 0) && (rns >= 0)) {
	    // Every section self-verified via its own marker, and the prologues
	    // gave their positions -- fully independent of the rotten header.
	    // States need no recovery of their own: they are declarations, so
	    // rom_scan_decl covers them.
	    h.n_decl  = (uint16_t)rnd;
	    h.n_instr = (uint16_t)rnn;
	    h.n_str   = (uint16_t)rns;
	    h.n_edg   = 0;            // the graph has no marker: drop it, run seq
	    nd = h.n_decl;
	    csp_print_line("ROM header CRC bad -- sections verified by walk");
	}
	else {
	    csp_print_lit("ROM rejected: CRC mismatch in ");
	    csp_print_rostr(bad);
	    csp_print_line(" section (corrupt flash image)");
	    return;
	}
    }
    // Committed as one descriptor, and only now -- everything above may still
    // reject the image, and a half-set st would leave the runtime pointing into
    // flash it has not verified.
    st->rom_p = p;
    if ((nd > 0) && (ro_decl(&p.decl[nd-1]).type == DECL_END))
	nd--;                     // drop the trailing terminator
    st->rom_nd   = nd;
    st->rom_nn   = h.n_instr;
    st->rom_strp = h.n_str;
    st->rom_nedg = h.n_edg;
    // A corrupt graph is recoverable where a corrupt section above is not: drop
    // the baked graph and let csp_cycle fall back to full sequential (rom_nedg
    // == 0 -> csp_eval). The program still runs -- reactive is just an
    // optimization -- so warn instead of rejecting. (See rom_graph_ok.)
    if (!rom_graph_ok(&p, &h)) {
	st->rom_nedg = 0;
	csp_print_line("ROM graph corrupt -- running sequential");
    }
    // Rebase the parse state onto ROM: RAM starts empty above the ROM sizes.
    // This discards the RAM State/strings csp_rt_init created -- State is now
    // ROM decl 0, and the ROM string prefix mirrors init's so the states table
    // (INIT/NORMAL name offsets) still resolves correctly through the flash.
    st->ps.nd   = st->rom_nd;
    st->ps.nn   = st->rom_nn;
    st->ps.strp = st->rom_strp;
    st->gsx = 0;                      // State is ROM decl 0
    if (st->cs)
	st->cs->sx = st->gsx;         // the parse cursor follows it
    // Nothing to restore: the program's states came back with its DECLARATIONS,
    // as DECL_STATES blocks, which the rebase above already pointed at. The
    // image's state section is empty (see csp_dump_code) and this loop used to
    // be the only thing that read it.
}

// --- the linked-image registry -----------------------------------------------
// csp -C emits a CSP_REGISTER_IMAGE for every image, so the linker collects
// their addresses into one array. This answers "what did this build link in",
// which is a different question from "what is on the chip" -- the latter needs a
// flash scan, and finds images nobody linked (a FAILSAFE flashed on its own, an
// A/B slot updated in the field). Same loader either way; only discovery
// differs.

#if defined(CSP_NO_IMAGE_REGISTRY)
// No section, so nothing to enumerate. csp_load_rom does not come through here
// -- it takes rom_image directly -- so the firmware still runs its own image.
int csp_image_count(void) { return 0; }
const uint8_t* csp_image_at(int i) { (void)i; return NULL; }
#else
int csp_image_count(void)
{
    return (int)(__stop_csp_images - __start_csp_images);
}

const uint8_t* csp_image_at(int i)
{
    if ((i < 0) || (i >= csp_image_count()))
	return NULL;
    return __start_csp_images[i];
}
#endif

// Pick the best linked image for a role: the highest generation whose header
// verifies. Header only -- the section CRCs are csp_load_image's business, and
// checking them twice would double the boot cost for no new information.
const uint8_t* csp_find_image(unsigned role)
{
    return csp_find_image_no(role, NULL);
}

// The same choice, with the registry INDEX it landed on -- the number /images
// prints, and what sys.Image reports so a node can say which of its images is
// actually running. -1 when nothing matched (the caller then falls back to the
// linked rom_image, which is not in the registry and has no index).
const uint8_t* csp_find_image_no(unsigned role, int* nop)
{
    const uint8_t* best = NULL;
    unsigned best_gen = 0;
    int best_no = -1;
    int n = csp_image_count();
    int i;

    if (nop != NULL)
	*nop = -1;

    for (i = 0; i < n; i++) {
	const uint8_t* base = csp_image_at(i);
	csp_image_header_t h;
	if (base == NULL)
	    continue;
	h = ro_header((const csp_image_header_t*)base);
	if ((h.magic[0] != CSP_IMAGE_MAGIC0) || (h.magic[1] != CSP_IMAGE_MAGIC1) ||
	    (h.magic[2] != CSP_IMAGE_MAGIC2) || (h.magic[3] != CSP_IMAGE_MAGIC3))
	    continue;
	if (h.role != role)
	    continue;
	if (csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0) != h.crc_hdr)
	    continue;             // a rotten header cannot be ranked; skip it
	if ((best == NULL) || (h.generation >= best_gen)) {
	    best = base;
	    best_gen = h.generation;
	    best_no = i;
	}
    }
    if (nop != NULL)
	*nop = best_no;
    return best;
}

// The firmware's own image, by the name every backend already calls.
NOINLINE void csp_load_rom(csp_rt_t* st)
{
    // Ask the registry for the best ROM-role image, so linking a second one with
    // a higher generation takes precedence -- A/B without touching the build.
    // Falling back to rom_image directly is deliberate: if --gc-sections ever
    // does collect the registry on some toolchain, the board still boots with
    // the image it was built with instead of nothing at all.
    int no;
    const uint8_t* base = csp_find_image_no(CSP_ROLE_ROM, &no);
    if (base == NULL)
	base = ro_ref(&rom_image).base;
    // Recorded, not written: sys.Image is a value SLOT and there are none until
    // csp_rt_start has laid the tables out. It publishes this then.
    st->image_no = no;
    csp_load_image(st, base);
    // The image replaces every declaration, so the Sys handles csp_rt_init just
    // set describe THIS firmware's runtime region and not the one the image was
    // built against. They agree whenever both were built with a Sys -- it sits at
    // the same offset, right after State -- but an image from a firmware that had
    // none puts the PROGRAM's declarations there, and the listing would then hide
    // the first five lines of it. Confirm what is actually at those indices
    // rather than assume the two builds match.
    if ((st->sys_mod != BAD_INDEX) &&
	((INDEX(st->sys_mod) >= st->ps.nd) ||
	 (decl(st, INDEX(st->sys_mod), type) != DECL_MODULE) ||
	 !csp_str_eq_ro(st, decl_name_pos(st, st->sys_mod), ros_Sys, 3) ||
	 (INDEX(st->sys_obj) >= st->ps.nd) ||
	 (decl(st, INDEX(st->sys_obj), type) != DECL_OBJECT)))
	st->sys_mod = st->sys_obj = BAD_INDEX;
}

// Backend hook: hand out the raw RAM for the code arena. The memory source is a
// per-target choice; the layout below is shared. Default is a static buffer so a
// new backend (LPCopen, bare-metal, no-heap) works with no porting. A weak
// symbol would be cleaner but gets stripped under -ffunction-sections/gc-sections
// on some targets (see the weak-ROM note), so select at build time instead.
// `want` is how much the runtime would like to claim; *got is set to what the
// backend can actually hand over (a claim may cap out, a static buffer is fixed).
#if defined(CSP_ARENA_CUSTOM)
// backend provides its own csp_arena_mem() (e.g. sbrk-based claim of free RAM)
#elif defined(CSP_ARENA_MALLOC)
uint8_t* csp_arena_mem(size_t want, size_t* got)   // claim ONCE, then reuse
{
    // Allocated once and cached: the pool lives for the whole run, so a second
    // call (csp_rt_init re-runs on /load) must return the SAME block. malloc'ing
    // again would leak the first arena AND, on a RAM-tight target, shrink freeRam
    // so the recomputed `want` is smaller -- csp_rebuild then OOMs ("too many
    // declarations") even though the program fit at boot. The first claim (boot,
    // max freeRam) is the one to keep; later `want` values are ignored.
    static uint8_t* arena = NULL;
    static size_t   arena_size = 0;
    if (arena == NULL) {
	arena = (uint8_t*)malloc(want);
	arena_size = arena ? want : 0;
    }
    *got = arena_size;
    return arena;
}
#else
uint8_t* csp_arena_mem(size_t want, size_t* got)   // portable default: no heap
{
    static uint8_t arena[CSP_ARENA_BYTES] __attribute__((aligned(8)));
    *got = (want < sizeof(arena)) ? want : sizeof(arena);  // fixed ceiling
    return arena;
}
#endif

// Lay out the RAM code arena as two forward-indexed regions: instr[] (MAX_INSTRS
// +1 slots -- the last is the immediate-eval scratch slot) followed by decl[].
// `size` is reserved for a future single shared pool; step 1 uses the fixed
// layout that exactly matches the old static arrays. The raw memory comes from
// the csp_arena_mem() backend hook. Returns 0 on success, -1 on failure.
// Struct size the RAM model should charge. On a target it is the real thing; on
// the host --board overrides it with the target's (16/32-bit) size so the model
// is not skewed by the host's wider pointers.
size_t csp_sim_state = 0;
// Not static: cmd_memory in csp_repl.c reports it, and it was the ONLY tie the
// command layer had back into the runtime when the two were separated.
uint32_t model_state(void)
{
    return (uint32_t)(csp_sim_state ? csp_sim_state : sizeof(csp_rt_t));
}

int csp_mem_init(csp_rt_t* st, size_t size)
{
    size_t want;
    // How much to CLAIM. With no explicit budget, take everything the system left
    // us less the stack we still grow into -- csp_system_ram_avail() already
    // excludes the system (freeRam() on a target: what is free after the core and
    // every linked library took theirs), so nothing about the board's overhead is
    // guessed. This one pool holds the program AND its derived data, so it is
    // CandySpeak's whole RAM budget, fixed at boot from what is actually free
    // rather than a per-board constant. `size` (linux -m) overrides, for testing a
    // tighter budget than the machine really has.
    if (size > 0)
	want = size;
#if !defined(CSP_ARENA_CUSTOM) && !defined(CSP_ARENA_MALLOC)
    // A FIXED STATIC ARENA is already reserved. It is a `static uint8_t
    // arena[CSP_ARENA_BYTES]` in .bss, nothing else can ever have those bytes,
    // and no amount of care here gives them back -- so ask for all of it.
    //
    // Deriving the claim from free RAM is right for a backend that ALLOCATES
    // (malloc, sbrk): there the pool competes with the stack and with whatever
    // else may take memory later. With a static array it is not merely
    // unnecessary, it is backwards -- the array is counted in .bss, so it comes
    // off the free figure the claim is computed from, and every byte added to
    // CSP_CODE_BUDGET is a byte taken from the pool it was meant to grow.
    //
    // Measured on bridgezone: a 9216-byte arena handed out 1032 bytes. The
    // other eight kilobytes were reserved and never used, and /memory reported
    // them as `system` -- which reads as the runtime being large rather than
    // the pool being starved.
    //
    // The stack reserve is not lost with it. It exists to keep a GROWING pool
    // away from a growing stack; a static array in .bss cannot grow into
    // anything.
    else
	want = CSP_ARENA_BYTES;
#else
    else {
	uint32_t avail = csp_system_ram_avail();
	// Charge for the struct ONCE. On a board csp_system_ram_avail() is
	// freeRam(), measured from the end of .bss -- which is where `state`
	// already sits -- so subtracting model_state() again takes
	// sizeof(csp_rt_t) away from the program for nothing. On the host it is a
	// MODEL, capacity minus the board's system figure, with no .bss in it at
	// all; there the struct has to come off or the simulation is optimistic
	// by exactly its size.
#if defined(ARDUINO)
	uint32_t over  = CSP_RAM_RESERVE + CSP_STACK_RESERVE;
#else
	uint32_t over  = model_state() + CSP_RAM_RESERVE + CSP_STACK_RESERVE;
#endif
	want = (avail > over) ? (avail - over) : 0;
    }
#endif
    want = CSP_A8(want);

    // The backend hands back what it can actually give (a claim may cap out, a
    // static buffer is fixed). The whole block is the pool; CSP_CODE_BUDGET is no
    // longer the ceiling -- the claim is.
    st->mem = csp_arena_mem(want, &st->mem_size);
    if ((st->mem == NULL) || (st->mem_size < 16))
	return -1;
    st->mem_size &= ~(size_t)7;          // keep the decl anchor 8-aligned

    // The REPL line buffer, carved off the TOP of the pool before anything else
    // is laid out. NOT from csp_mid_alloc: a rebuild resets that allocator, and a
    // rebuild happens inside csp_process_line -- which is running out of this
    // very buffer, so it would be handed to something else mid-command. Taking it
    // off the top and lowering mem_limit makes it permanent for the life of the
    // arena, and both the decl anchor and the middle allocator stay below it
    // without needing to know it is there.
    {
	size_t want_line = st->mem_size / CSP_LINE_SHARE;
	if (want_line < CSP_LINE_MIN) want_line = CSP_LINE_MIN;
	if (want_line > CSP_LINE_MAX) want_line = CSP_LINE_MAX;
	want_line = CSP_A8(want_line);
	// A pool too small to hold both a line and a program is not a pool. Keep
	// the arena valid and let the caller's own size checks report it.
	if (st->mem_size <= want_line + 16)
	    return -1;
	st->mem_limit  = st->mem_size - want_line;
	st->line.buf   = (char*)st->mem + st->mem_limit;
	st->line.buf_size  = (uint16_t)want_line;

	// The history, below the line buffer and off the same top. Carved only
	// when the pool can spare it: a board that has to choose should spend
	// its bytes on the program, and the editor works without a history.
	{
	    size_t want_hist = st->mem_size / CSP_HIST_SHARE;

	    if (want_hist > CSP_HIST_MAX) want_hist = CSP_HIST_MAX;
	    want_hist = CSP_A8(want_hist);
	    if ((want_hist < CSP_HIST_MIN) ||
		(st->mem_limit <= want_hist + 64)) {
		st->line.hist = NULL;
		st->line.hist_size = 0;
	    }
	    else {
		st->mem_limit -= want_hist;
		st->line.hist = (char*)st->mem + st->mem_limit;
		st->line.hist_size = (uint16_t)want_hist;
	    }
	    st->line.hist_used = st->line.hist_at = 0;
	}
    }
    // Clear the POOL, not the whole block: /load re-runs csp_rt_init from inside
    // csp_process_line, which is reading the line out of the buffer above the
    // limit. Wiping it there pulled the command out from under its own handler.
    memset(st->mem, 0, st->mem_limit);
#ifdef CSP_STACK_WATCH
    // Top of the pool = the address the stack must never reach. mem_SIZE, not
    // mem_limit: the line buffer sits in the gap above the limit and the stack
    // must not reach that either.
    csp_arena_top = (char*)st->mem + st->mem_size;
#endif

    // Double-ended pool: instr[] grows up from the base, decl[] grows down from
    // the top of the CLAIMED block. ram_decl points at the topmost decl slot and
    // is indexed with a negated local index (ram_decl_at / csp_get_decl). Base and
    // top are both 8-aligned. They can never overlap: mem_fits keeps instr+decl,
    // and the middle bump allocator keeps the derived tables, inside mem_limit.
    st->ram_instr = (csp_instr_t*)st->mem;
    st->ram_decl  = (csp_decl_t*)(st->mem + st->mem_limit) - 1;
    // view/dset/buf/heap are all sized to the estimate in csp_rt_start.
    st->view = NULL;
    st->dset = NULL;
    st->view_cap = 0;
    st->heap[DIN] = st->heap[DOUT] = NULL;
    st->buf = NULL;
    st->buf_cap = 0;
    st->heap_cap = 0;
    // input/output/timer are sized to the estimate in csp_rt_start too.
    st->offs = NULL;   st->object = NULL;    st->obj_cap = 0;
    st->module = NULL; st->mod_cap = 0;
    st->io = NULL;     st->io_obj = NULL;    st->io_cap = 0;
    st->timer = NULL;  st->timer_obj = NULL; st->timer_cap = 0;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    // pending sets, rule_ip and idg/ofs/edg are all sized to actual in csp_csr.
    st->es.pending[0] = st->es.pending[1] = NULL;
    st->es.pending_cap = 0;
    st->es.obj_shift = 0;
    st->es.gen = 0;
    st->es.rule_ip = NULL;
    st->es.rule_state = NULL;
    st->es.n_rule = 0;
    st->es.idg = st->es.ofs = st->es.edg = NULL;
#endif
    return 0;
}

// Not the compiler's: csp_rt_init creates INIT and NORMAL before any program
// exists, so an exec-only build needs it too. It is a symbol-table operation
// that the parser also happens to call.
// How many states are declared so far. States live in DECL_STATES blocks of up
// to CSP_STATES_PER_DECL names each, filled front to back and never reordered,
// so counting the non-empty slots in declaration order gives both the total and
// -- since add_state only ever appends -- the number the next one gets.
//
// Derived rather than stored: ROM declarations come before RAM ones, a parse
// rollback rewinds the declaration count and takes the states with it, and
// nothing renumbers declarations. So the scan is the same answer every time,
// which is what lets the number a rule baked into OP_INSTATE stay valid.
#define state_count(st) csp_num_states(st)

// Not the compiler's: csp_rt_init creates INIT and NORMAL before any program
// exists, so an exec-only build needs it too. It is a symbol-table operation
// that the parser also happens to call.
//
// `blk` is the caller's cursor over the block being filled -- BAD_INDEX to open
// a new one. It is a LOCAL of the statement doing the adding, deliberately not
// state on csp_rt_t: packing forward inside one `#states` is safe because a
// rollback that rewinds the declaration count un-creates those blocks whole,
// while reaching back into a block an EARLIER line created would survive the
// rewind and leave the name behind.
NOINLINE int add_state(csp_rt_t* st, const tstr_t* name, index_t* blk)
{
    int pos, s, k = -1;
    csp_decl_t* dp;

    if ((pos = lookup_string(st, name->ptr, name->len)) < 0) {
	if ((pos = new_string(st, name->ptr, name->len)) < 0)
	    return -1;
    }
    s = state_count(st);
    // OP_INSTATE carries the number in a signed 8-bit immediate.
    if (s > 127) {
	csp_set_error(st, ERR_TOO_MANY_STATES);
	return -1;
    }
    if (*blk != BAD_INDEX) {              // room left in the open block?
	csp_decl_t d = csp_get_decl(st, INDEX(*blk));
	for (k = 0; k < CSP_STATES_PER_DECL; k++)
	    if (csp_states_name(&d, k) == 0)
		break;
	if (k == CSP_STATES_PER_DECL)
	    k = -1;                       // full, open another
    }
    if (k < 0) {
	index_t ix;
	if ((ix = next_decl_index(st)) == BAD_INDEX)
	    return -1;
	dp = ram_decl_at(st, INDEX(ix));
	memset(dp, 0, sizeof(*dp));
	dp->type = DECL_STATES;
	*blk = ix;
	k = 0;
    }
    dp = ram_decl_at(st, INDEX(*blk));
    csp_states_set_name(dp, k, pos);
    DBG("added state %d slot %d\n", s, k);
    return s;
}
// The built-in Sys namespace: the node's own identity, in one place every
// program can name.
//
// Built HERE, by the runtime, for the same reason State is: an exec-only node
// has no parser, and a program that says `sys.Id` has to find the same field on
// a node that can compile and on one that cannot. Building it from source text
// would mean csp_rt.c calling csp_parse -- and that one reference is what stops
// --gc-sections from taking the whole compiler out of an exec-only image.
//
// NO INSTRUCTIONS. A module's OP_ENTER/OP_LEAVE pair exists to run its BODY, and
// the OP_NEW at an instantiation exists to point CURRENT at the instance before
// that body runs. A namespace has no body, so it needs none of the three:
// csp_rt_start walks DECL_OBJECT in declaration order, sizes offs[] from md.n
// and sets each member up without ever looking at md.ent. Five declarations,
// zero code.
//
// NO State either. A module normally gets one so that a state named inside it
// belongs to it; a namespace names no states. That is one whole leaf saved --
// see csp_parse_module's no_state, which is the same decision on the parser
// side.
//
// Fields, and who owns each (doc/manual_en.md carries the same table):
//   Serial  #variable  the HARDWARE's. The platform publishes it at boot, so a
//                      stored setting must not win -- csp_setup runs after
//                      csp_settings_apply, which is exactly that order.
//   Id      #param     the OPERATOR's. A setting wins and survives a reflash.
//   Name    #param     likewise.
NOINLINE static int csp_sys_module(csp_rt_t* st)
{
    RO_TSTR(Sys, ros_Sys);
    RO_TSTR(sys, ros_sys);
    RO_TSTR(Id, ros_Id);
    RO_TSTR(Name, ros_Name);
    RO_TSTR(Serial, ros_Serial);
    RO_TSTR(Image, ros_Image);    
    const tstr_t empty = { .ptr = NULL, .len = 0 };
    index_t mx, ex, ox;
    int i;

    if ((mx = csp_new_decl(st, &Sys, DECL_MODULE, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, INDEX(mx))->md.ent = 0;   // no body -- see above

    if ((i = csp_new_decl(st, &Serial, DECL_VARIABLE, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, i)->vt  = V_UNSIGNED;
    ram_decl_at(st, i)->res = MAKE_RES(32);

    // #param Id:32 unsigned
    if ((i = csp_new_decl(st, &Id, DECL_CONSTANT, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, i)->vt    = V_UNSIGNED;
    ram_decl_at(st, i)->res   = MAKE_RES(32);
    ram_decl_at(st, i)->local = 1;            // DECL_CONSTANT + local == #param

    // #param Name string
    if ((i = csp_new_decl(st, &Name, DECL_CONSTANT, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, i)->vt    = V_STRING;
    ram_decl_at(st, i)->local = 1;

    // #param Image:32 unsigned
    // STATUS, not a setting: the loader writes which image it actually booted,
    // so a #param would be a field a saved value could contradict. Asking for a
    // different image next boot is a REQUEST and wants a field of its own --
    // one number cannot both say what is running and what you would prefer.
    if ((i = csp_new_decl(st, &Image, DECL_VARIABLE, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, i)->vt  = V_UNSIGNED;
    ram_decl_at(st, i)->res = MAKE_RES(32);

    if ((ex = csp_new_decl(st, &empty, DECL_END, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, INDEX(mx))->md.n = (INDEX(ex) - INDEX(mx)) - 1;

    if ((ox = csp_new_decl(st, &sys, DECL_OBJECT, 1)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, INDEX(ox))->mq.mx = INDEX(mx);
    // The object NUMBER, and it is not optional. csp_rt_start renumbers the
    // objects itself, but the COMPILER reads mq.m to build the xindex for
    // `sys.Id` -- and ps.nq is what csp_parse_object counts from, so the first
    // user object must get 2 and not collide with this one.
    //
    // Left at 0 first time round, every member resolved as a GLOBAL declaration
    // index: `sys.Id = 7` wrote over State, and `sys.Name` read the whole string
    // table back.
    ram_decl_at(st, INDEX(ox))->mq.m = st->ps.nq + 1;
    st->ps.nq++;
    st->sys_obj = INDEX(ox);
    st->sys_mod = INDEX(mx);
    return 0;
}

NOINLINE static ivalue_t get_md_n(csp_rt_t* st, index_t mx);

// Write what the runtime KNOWS into the Sys namespace, once the tables exist.
//
// After csp_settings_apply on purpose: these are facts about the machine, not
// preferences, so a stored setting must not be able to contradict them. It is
// the same ordering rule csp_setup follows for a hardware-owned field.
//
// By NAME, not by member ordinal. The members are declared in one place and
// read here; an ordinal would be a second copy of that order, and adding a
// field between two others would move it silently.
NOINLINE static void sys_publish(csp_rt_t* st)
{
    RO_TSTR(Image, ros_Image);
    index_t mx, base, ix;
    ivalue_t dn;
    value_t v;

    if ((st->sys_mod == BAD_INDEX) || (st->sys_obj == BAD_INDEX))
	return;
    mx   = st->sys_mod;
    base = (index_t)(INDEX(mx) + 1);
    dn   = get_md_n(st, mx);
    if ((ix = lookup_decl_in(st, &Image, base, base + dn)) == BAD_INDEX)
	return;
    v.u = (st->image_no < 0) ? 0 : (uvalue_t)st->image_no;
    {
	int cur_save = st->cur;
	index_t cbase_save = st->cbase;
	value_t* iptr;
	value_t* optr;
	csp_ctx_set(st, decl(st, INDEX(st->sys_obj), mq.m));
	if (csp_dio_slots(st, MAKE_INDEX(CURRENT, INDEX(ix)), &iptr, &optr) == 0)
	    *iptr = *optr = v;
	st->cur = cur_save;
	st->cbase = cbase_save;
    }
}

int csp_rt_init(csp_rt_t* st, int reactive, csp_cstate_t* cs)
{
    memset(st, 0x00, sizeof(csp_rt_t));
    st->cs = cs;   // NULL on a node that only runs images

    // Allocate the RAM code arena (instr[]/decl[]) before anything parses. The
    // line buffer comes off the top of it, so the line state can only be set up
    // afterwards -- and must be, because the memset above cleared need_prompt,
    // which used to be a global initialized to 1.
    if (csp_mem_init(st, 0) < 0)
	return -1;
    csp_line_init(&st->line);

    // The compiler's own tables, initialised on its side of the line. Dropping
    // this call is what lets --gc-sections take the whole compiler with it: the
    
#ifdef DEBUG
    if (debug) // or precompile!
	dump_stop_sets();       // debugging
#endif
    // ONLY the fields whose initial value is not zero. The memset above cleared
    // the whole struct, so nbuf, ps.nn/nd/nq/ns/line, ps.err (ERR_OK == 0),
    // ps.err_args, nt, nio, nm, cur, cs.nvar/rimp/n_sdef/rule_implicit,
    // list_nstate, ram_str[0] (an in-struct array), ufuncs, num_ufuncs and
    // uconst are already what they need to be.
    //
    // Writing them anyway was not free the way it looks: csp_mem_init and
    // csp_line_init sit between the memset and this point and both take `st`,
    // so gcc must assume they wrote anywhere in the struct. That kills what it
    // knew about the memset, and every redundant `= 0` came back as a real
    // 4-byte absolute store. Twenty-one of them, ~84 bytes of nothing.
    st->reactive = reactive;
    st->ps.strp = 1;
    st->ps.err_strp = MAX_STR_BUF;
    if (st->cs) {
	st->cs->mdef = BAD_INDEX;  // no module being defined
	st->cs->sdef = -1;
	// dedicated scratch for the variable list during a <- parse
	st->cs->var = st->cs->var_buf;
    }
    st->sys_mod = st->sys_obj = BAD_INDEX;
    st->image_no = -1;    // until csp_load_rom picks one   // until csp_sys_module runs; the
					     // memset above would leave 0, and
					     // decl 0 is State, a real index
    st->n_rule_emit = 1;   // the implicit rule body at the RAM range base
    {
	RO_TSTR(State, ros_State);
	RO_TSTR(INIT, ros_INIT);
	RO_TSTR(NORMAL, ros_NORMAL);
	RO_TSTR(FAILSAFE, ros_FAILSAFE);
	st->gsx = csp_new_decl(st,&State,DECL_VARIABLE,1);
	if (st->cs)
	    st->cs->sx = st->gsx;
	// Reserved states in fixed order: INIT=0, NORMAL=1, FAILSAFE=2 (sticky
	// safe state). User states follow from 3. The numbers are contract --
	// STATE_* in csp.h and the sticky check depend on them. One cursor for
	// all three, so they share a single block.
	{
	    index_t blk = BAD_INDEX;
	    if (add_state(st, &INIT, &blk) != STATE_INIT) {
		DBG("unable to add INIT state\n");
		return -1;
	    }
	    if (add_state(st, &NORMAL, &blk) != STATE_NORMAL) {
		DBG("unable to add NORMAL state\n");
		return -1;
	    }
	    if (add_state(st, &FAILSAFE, &blk) != STATE_FAILSAFE) {
		DBG("unable to add FAILSAFE state\n");
		return -1;
	    }
	}
#if !defined(CSP_NO_SYS_MODULE)
	if (csp_sys_module(st) < 0)
	    return -1;
#endif
	// The runtime/program boundary -- see csp_rt_t. States need no separate
	// one any more: they are declarations, so sys_nd covers them too --
	// and so is the Sys namespace above, which is why /clear cannot drop it.
	st->sys_nd   = st->ps.nd;
	st->sys_nn   = st->ps.nn;
	st->sys_strp = st->ps.strp;
    }
    st->list_state = -1;         // no #in block being listed
    return 0;
}

// Set user function table (called before parsing)
// rom = 1 if the funcs table (and, per-entry FUNC_RONAME, its names) is in ROM.
void csp_set_ufuncs(csp_rt_t* st, const csp_func_t* funcs, uint8_t count, uint8_t rom)
{
    st->ufuncs = funcs;
    st->num_ufuncs = count;
    st->ufuncs_rom = rom;
}

void csp_set_uconst(csp_rt_t* st, csp_const_fn uconst)
{
    st->uconst = uconst;
}

static void setup_timer_values(value_t* ptr, csp_timer_t tm)
{
    ptr->t.fired   = 0;
    ptr->t.val     = tm.init;
    ptr->t.running = tm.init;
    ptr->t.period  = tm.period;
}

// copy config data to value slot config
NOINLINE static void setup_timer(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t d;
    csp_view_t* v;

    csp_copy_decl(st, INDEX(ix), &d);
    v = csp_view(st, ix);    
    // clear timeout flag and load config into the timer's value_t buffer.
    // The start-time slot (tx = ix+1) is an ordinary variable, initialised to 0
    // by its own setup_variable; the timer arms it at runtime.
    // csp_dio_slots(st, ix, &iptr, &optr);
    optr = csp_slot(st, v, DOUT);
    setup_timer_values(optr, d.tm);
    
    iptr = csp_slot(st, v, DIN);
    setup_timer_values(iptr, d.tm);    
}

static void setup_analog_values(value_t* ptr, csp_analog_t an)
{
    ptr->a.dir  = an.dir;
    ptr->a.pin  = an.pin;
    ptr->a.port = an.port;
    ptr->a.pwm  = an.pwm;
    // No endian: it stays in the declaration, where .endian reads it from.
    // csp_setup applies this configuration itself, so nothing is pending.    
    ptr->a.cfg = 0;
}

// copy config data to value slot config
NOINLINE static void setup_analog(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t d;
    csp_view_t* v;

    csp_copy_decl(st, INDEX(ix), &d);
    v = csp_view(st, ix);    
    
    optr = csp_slot(st, v, DOUT);
    setup_analog_values(optr, d.an);

    iptr = csp_slot(st, v, DIN);
    setup_analog_values(iptr, d.an);
}

static void setup_digital_values(value_t* ptr, csp_digital_t di)
{
    ptr->d.dir  = di.dir;
    ptr->d.pin  = di.pin;
    ptr->d.port = di.port;
    ptr->d.pullup = di.pullup;
    ptr->d.pulldown = di.pulldown;
    // csp_setup applies this configuration itself, so nothing is pending. Left
    // set, it would spend a pinMode on the first cycle saying what setup just
    // said -- and on a slot that was never zeroed it would be whatever was
    // there before.    
    ptr->d.cfg = 0;
}

// copy config data to value slot config
NOINLINE static void setup_digital(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t d;
    csp_view_t* v;

    csp_copy_decl(st, INDEX(ix), &d);
    v = csp_view(st, ix);    

    optr = csp_slot(st, v, DOUT);
    setup_digital_values(optr, d.di);

    iptr = csp_slot(st, v, DIN);
    setup_digital_values(iptr, d.di);
}

NOINLINE static index_t csp_buf_alloc(csp_rt_t* st, uint16_t nbytes,
				      uint8_t transport, uint32_t xref,
				      pindir_t dir);
NOINLINE static int parent_leaf(csp_rt_t* st, index_t ix);

// A bit view into someone else's buffer. Three callers fill exactly these seven
// fields from exactly the same places -- setup_field, setup_variable's bound
// branch, and the DECL_VIEW arm of csp_rt_start -- so the writes live here once,
// the same way setup_digital_values holds the digital slot fill.
//
// `ca` comes in BY VALUE, like the config arms do: it is four bytes, it arrives
// in registers, and it means the caller's whole csp_decl_t does not have to stay
// alive across the call.
static void setup_view_values(csp_view_t* vw, vtype_t vt, index_t buf,
			      csp_field_t ca)
{
    vw->kind   = VIEW_HEAP;
    vw->vt     = vt;
    vw->buf    = buf;
    vw->pos    = ca.bit;
    vw->len    = ca.len;      // already len-1
    vw->endian = ca.endian;
    vw->flags  = 0;           // sub-view -> generic bit path
}

// Bind a #field to its frame. ca.id is the #buffer decl; that buffer was
// already allocated by setup_buffer (it has a lower decl index, since the frame
// must be declared before a field can view it), so this is purely a view.
NOINLINE static int setup_field(csp_rt_t* st, index_t ix)
{
    csp_decl_t d; // = csp_get_decl(st, INDEX(ix));  // read ONCE -- see setup_buffer
    csp_decl_t dt;
    uint16_t pos; // = d.ca.bit;                     // ca.bit is 9 bits: 0..511
    csp_view_t* pv = &st->view[parent_leaf(st, ix)];
    csp_view_t* vw;

    csp_copy_decl(st, INDEX(ix), &d);
    pos = d.ca.bit;

    // ca.bit is 9 bits and csp_view_t.pos is 16, so every bit of a 64-byte FD
    // frame (0..511) is addressable. The frame itself is the only bound.
    // d.ca.id is the #buffer this field views, a DIFFERENT decl.
    csp_copy_decl(st, INDEX(d.ca.id), &dt);    
//    if (pos + d.ca.len + 1 > decl(st, d.ca.id, bf.nbytes)*8) {
    if (pos + d.ca.len + 1 > dt.bf.nbytes*8) {
	csp_set_error(st, ERR_SYNTAX);          // field reaches past the frame
	return -1;
    }
    vw = &st->view[st_index(st, ix)];
    setup_view_values(vw, d.vt, pv->buf, d.ca);  // shares the frame's buffer
    return 0;
}

// ============================================================
// CAN frame I/O
//
// A frame is one buffer; its fields are bit views into it. So receiving is a
// memcpy into the committed (DIN) heap and sending is a memcpy out of it --
// the packing and unpacking is the view machinery, already there.
//
// This is where the CANopen PDO shapes fall out, without any PDO code:
//   RPDO       frame arrives -> fields change -> dependent rules enqueue
//   TPDO event a rule writes a field -> commit marks dirty -> sent
//   TPDO cyclic  Frame.X = ... ? timeout(T)  -- the timer is the trigger
// ============================================================

// A received frame has to enter through the same door as any other device
// input: compare against the committed value, and where a field differs mark
// it dirty and push its dependents. That is what makes changed() see it and
// what wakes the reactive rules.
//
// No extra copy of the old bytes is needed -- the transaction already keeps
// one. csp_can_input drops the frame into the DOUT shadow, DIN still holds the
// previous frame, so the diff below is old-vs-new, and commit then moves DOUT
// to DIN exactly as it does for a value a rule wrote.
NOINLINE static void can_mark_fields(csp_rt_t* st, index_t b)
{
    int i;
    index_t own = st->buf[b].owner;
    // The frame's own leaf first. A frame declared as a plain #buffer (no #field
    // at all -- read with >>= or with bound variables) has nothing in
    // the input list, so without this nothing would be marked, commit would
    // copy nothing, and the received bytes would never reach the committed
    // half. heap_dset_copy moves the WHOLE buffer for any dirty leaf of it, so
    // this one mark is what actually lands the frame.
    //
    // Read off the buffer, not searched for: see csp_buf_t.owner.
    if (own != BAD_INDEX) {
	// Marked unconditionally, not diffed: a frame is wider than a value_t,
	// so there is no single value to compare. Granularity comes from the
	// per-field pass below; this mark only has to make the copy happen.
	bitset_set(st->dset, st_index(st, own));
	st->es.anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st, own);
#endif
    }
    // Then per-field, so changed() has real granularity and only the fields
    // that moved wake their rules.
    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);
	int leaf = st_index(st, ix);
	csp_view_t* vw = &st->view[leaf];
	// The VIEW answers this, not the declaration. io[] holds exactly three
	// kinds of leaf -- digital, analog and #field -- and the first two are
	// value slots (setup_slot) while a field is a bit view into its frame
	// (setup_field). So VIEW_HEAP identifies a field here, and the buffer id
	// then says whether it is a field of THIS frame.
	//
	// It used to ask decl(st, INDEX(ix), type), which is a csp_get_decl --
	// a flash copy of the whole declaration -- run for every entry in the I/O
	// list, on every received CAN frame. Two bytes of RAM answer the same
	// question, and the cheap test now comes first.
	if ((vw->kind != VIEW_HEAP) || (vw->buf != b))
	    continue;
	if (csp_heap_get(st, vw, DOUT).u == csp_heap_get(st, vw, DIN).u)
	    continue;  // this field of the frame is unchanged
	bitset_set(st->dset, leaf);
	st->es.anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st, ix);
#endif
	st->es.update++;
    }
    csp_ctx_reset(st);
}

// Does this program listen to the bus? A driver loop has to keep running for a
// program whose only input is CAN -- there is no timer and nothing changes, so
// every other "is there work left" test says no and the loop would quit before
// the first frame ever arrived.
int csp_can_active(csp_rt_t* st)
{
    index_t b;
    for (b = 0; b < st->nbuf; b++) {
	if ((st->buf[b].transport == TR_CAN) && (st->buf[b].dir & DIR_IN))
	    return 1;
    }
    return 0;
}

// Drain the receive side. One frame can feed several buffers only if the same
// id is declared twice, so the loop below keeps scanning rather than stopping
// at the first match.
void csp_can_input(csp_rt_t* st)
{
    uint8_t data[64];
    uint32_t id;
    uint8_t len;
    int guard;

    // Bounded: a busy bus would otherwise keep this loop fed forever and the
    // cycle would never run.
    for (guard = 0; guard < CSP_CAN_RX_BURST; guard++) {
	index_t b;
	if (csp_can_recv(st, &id, data, &len) != 1)
	    return;
	for (b = 0; b < st->nbuf; b++) {
	    csp_buf_t* bp = &st->buf[b];
	    uint8_t n;
	    if ((bp->transport != TR_CAN) || (bp->xref != id) ||
		!(bp->dir & DIR_IN))
		continue;
	    // Into the SHADOW, not the committed half: DIN must keep the previous
	    // frame so can_mark_fields can tell what actually changed.
	    // A short frame updates only the bytes it carried. Clamp per buffer,
	    // not once: the same id may feed several buffers of different sizes.
	    n = (len < bp->nbytes) ? len : bp->nbytes;
	    memcpy(st->heap[DOUT] + bp->hp, data, n);
	    bp->dlc = n;                   // what the sender actually sent
	    bp->flags |= BUF_F_RXPEND;     // csp_commit turns this into BUF_F_RX
	    can_mark_fields(st, b);
	}
    }
}

// Send every out frame that either changed or was explicitly asked for.
// BUF_F_TX is what makes a CYCLIC PDO expressible: an unchanged frame is not
// dirty, so `F.tx = 1 ? timeout(T)` is the only way to say "send it anyway".
void csp_can_output(csp_rt_t* st)
{
    index_t b;
    for (b = 0; b < st->nbuf; b++) {
	csp_buf_t* bp = &st->buf[b];
	if ((bp->transport != TR_CAN) || !(bp->flags & (BUF_F_DIRTY|BUF_F_TX)))
	    continue;
	bp->flags &= ~(BUF_F_DIRTY|BUF_F_TX);
	if (bp->dir & DIR_OUT)
	    csp_can_send(st, bp->xref, st->heap[DIN] + bp->hp, bp->dlc);
    }
}

// bump-allocate heap bytes, return the offset (or 0xffff on overrun)
//
// The cursor is st->hp and not "behind the last buffer": most leaves take heap
// without taking a buffer entry now, so the buffer table has stopped being a
// record of what the heap has handed out.
NOINLINE static uint16_t csp_heap_alloc(csp_rt_t* st, uint16_t nbytes)
{
    uint16_t hp = (st->hp + 3) & ~3;   // 4-align: value_t access must be aligned
    // Sized by csp_estimate; an overrun means the estimate was short -- fail
    // loudly (see the driver's rt_start check) instead of scribbling.
    if ((uint32_t)hp + nbytes > st->heap_cap) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return 0xffff;
    }
    st->hp = hp + nbytes;
    return hp;
}

// bump-allocate a buffer in the heap, return its id (or BAD_INDEX)
NOINLINE static index_t csp_buf_alloc(csp_rt_t* st, uint16_t nbytes,
				      uint8_t transport, uint32_t xref,
				      pindir_t dir)
{
    index_t b = st->nbuf;
    uint16_t hp;
    if (b >= st->buf_cap) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return BAD_INDEX;
    }
    if ((hp = csp_heap_alloc(st, nbytes)) == 0xffff)
	return BAD_INDEX;
    st->buf[b].hp        = hp;
    st->buf[b].nbytes    = nbytes;
    st->buf[b].transport = transport;
    st->buf[b].xref      = xref;
    st->buf[b].dir       = dir;
    st->buf[b].flags     = 0;
    st->buf[b].dlc       = nbytes;     // send the whole frame unless told less
    st->buf[b].owner     = BAD_INDEX;  // setup_buffer fills this in; setup_slot
				       // has no leaf of its own to record
    st->nbuf++;
    return b;
}

// allocate storage for a #buffer and point its own view at the whole buffer
//
// READ THE DECL ONCE. `decl(st,i,fld)` is csp_get_decl(st,i).fld, and
// csp_get_decl is NOINLINE and returns the whole csp_decl_t by value -- so
// every field access is a call plus a full 8-byte spill to a fresh stack slot
// (gcc cannot scalarise it: the union's bitfield arms need the temporary to
// have an address, and a non-pure call cannot be CSE'd). This function used to
// have eight of them, seven on the SAME decl: 628 bytes and a 64-byte stack
// frame for what is a handful of field copies. One local copy is the fix, and
// it applies to every setup_* here.
NOINLINE static int setup_buffer(csp_rt_t* st, index_t ix)
{
    csp_decl_t d;
    // Only a real #buffer carries bf.nbytes. This function is shared with the
    // storage a plain #variable gets, and there the size lives in res.
    int is_buf;
    uint16_t res, nbytes;
    uint8_t transport;
    uint32_t xref = 0;
    index_t b;
    csp_view_t* vw;

    csp_copy_decl(st, INDEX(ix), &d);
    is_buf    = (d.type == DECL_BUFFER);
    res       = is_buf ? d.bf.nbytes*8 : GET_RES(d.res);
    nbytes    = (res + 7) >> 3;
    transport = is_buf ? d.bf.transport : TR_NONE;

    // A plain #variable OWNS its bytes and nothing can view into it, so it takes
    // heap and no buffer entry. Only a real #buffer needs the other fifteen
    // bytes of a csp_buf_t -- a transport, a frame id, a dlc, an owner
    // back-reference -- and only a #buffer can be a view parent.
    if (!is_buf) {
	uint16_t hp;
	if ((hp = csp_heap_alloc(st, nbytes)) == 0xffff)
	    return -1;
	vw = &st->view[st_index(st, ix)];
	vw->kind   = VIEW_OWN;
	vw->vt     = d.vt;
	vw->pos    = hp;
	vw->len    = (res > VIEW_MAX_LEN) ? VIEW_MAX : (uint8_t)(res - 1);
	vw->endian = E_NATIVE;
	vw->flags  = ((res & 7) == 0) ? VIEW_F_SIMPLE : 0;
	return 0;
    }

    if (transport == TR_CAN) {               // the frame id, out of its constant
	csp_decl_t id;
	csp_copy_decl(st, INDEX(d.bf.id), &id);
	xref = (uint32_t)id.cn.init.i;
    }
    if ((b = csp_buf_alloc(st, nbytes, transport, xref, d.dir)) == BAD_INDEX)
	return -1;
    st->buf[b].owner = ix;             // ix, not the leaf: csp_enq_elist wants
				       // the object-qualified index
    vw = &st->view[st_index(st, ix)];
    vw->kind     = VIEW_HEAP;
    vw->vt       = d.vt;
    vw->buf    = b;
    vw->pos    = 0;
    // A whole-frame view would need len up to 511, but len is 8 bits. Cap it:
    // the frame is read and written field by field, and the whole-buffer view
    // only matters for a plain #buffer used as one value.
    vw->len    = (res > VIEW_MAX_LEN) ? VIEW_MAX : (uint8_t)(res - 1);
    vw->endian = E_NATIVE;
    vw->flags  = ((res & 7) == 0) ? VIEW_F_SIMPLE : 0;
    return 0;
}

// A variable lives in the heap: bound -> a view into an existing buffer,
// otherwise its own auto-buffer seeded with the init value. Works for globals
// and per-object fields alike (st_index/decl pick the right slot/template).
// Leaf of the buffer that decl `ix` is a view into. ca.id holds the parent's
// DECL index, which is not a leaf: for a member of an object, a parent that is
// a member of the SAME module lives in that object's storage, while a global
// parent does not. Getting this wrong aliases every instance onto the module
// template's slot.
NOINLINE static int parent_leaf(csp_rt_t* st, index_t ix)
{
    index_t p = decl(st, INDEX(ix), ca.id);
    // OBJ(ix) is a one-bit selector, so the object number comes from st->cur --
    // which the per-object setup loop points at the instance being built, exactly
    // as OP_NEW does during execution.
    int m = OBJ(ix) ? st->cur : 0;
    if (m != 0) {
	index_t ox = st->object[m];
	index_t mx = decl(st, INDEX(ox), mq.mx);
	int base = INDEX(mx) + 1;
	int dn = decl(st, INDEX(mx), md.n);
	if (((int)p >= base) && ((int)p < base + dn))
	    return st_index_obj(st, m, p);   // this instance's parent
    }
    return p;                                // a global parent (base 0)
}

NOINLINE static int setup_variable(csp_rt_t* st, index_t ix)
{
    csp_decl_t d;
    csp_view_t* vw = &st->view[st_index(st, ix)];

    csp_copy_decl(st, INDEX(ix), &d);
    if (d.bound) {                            // bit-field view into a buffer
	csp_view_t* pv = &st->view[parent_leaf(st, ix)];
	setup_view_values(vw, d.vt, pv->buf, d.ca);
	return 0;
    }
    if (setup_buffer(st, ix) < 0)         // its own storage
	return -1;
    // A #local is single-buffered -- see VIEW_F_LOCAL. Marked here, after
    // setup_buffer has filled the view in; heap_base and csp_slot do the rest.
    if (d.local)
	vw->flags |= VIEW_F_LOCAL;
    csp_heap_set(st, vw, DIN,  d.va.init);
    csp_heap_set(st, vw, DOUT, d.va.init);
    return 0;
}

// config+value types (constant/digital/analog/timer) live as a value_t struct
// of their own. Take the heap bytes and point a VIEW_SLOT at them; the caller
// then fills it through the normal csp_dio_slot(s)/PART path.
//
// No csp_buf_t. Nothing can view into one of these -- `bind` refuses anything
// that is not a #buffer -- so the only field such a leaf ever wanted from a
// buffer was the heap offset, and that is in the view now.
NOINLINE static int setup_slot(csp_rt_t* st, index_t ix)
{
    csp_decl_t d;
    uint16_t hp;
    csp_view_t* vw;

    csp_copy_decl(st, INDEX(ix), &d);
    if ((hp = csp_heap_alloc(st, sizeof(value_t))) == 0xffff)
	return -1;
    vw = &st->view[st_index(st, ix)];
    vw->kind  = VIEW_SLOT;
    vw->vt    = d.vt;
    vw->pos   = hp;
    vw->flags = 0;
    return 0;
}

// A device leaf is listed ONCE, whatever direction it was declared with. A pin
// filed by its declared direction is missing from the other phase the moment a
// rule turns it round, and "every pin can change direction" is the whole point.
// A #field with no direction is not device I/O at all and stays out.
NOINLINE static void add_io(csp_rt_t* st, index_t ix)
{
    csp_decl_t d = csp_get_decl(st, INDEX(ix));
    if ((d.type == DECL_FIELD) && !(d.dir & DIR_INOUT))
	return;
    if (st->nio < st->io_cap) {  // sized to csp_estimate.nio; guard is belt+braces
	st->io_obj[st->nio] = st->cur;   // the instance setup is currently building
	st->io[st->nio++] = ix;
    }
}


NOINLINE static int setup_decl(csp_rt_t* st, index_t ix, csp_decl_t d)
{
    switch(d.type) {
    case DECL_VARIABLE:
	if (setup_variable(st, ix) < 0)
	    return -1;	
	break;
    case DECL_CONSTANT: {
	value_t* iptr;
	value_t* optr;
	if (setup_slot(st, ix) < 0)
	    return -1;		
	csp_dio_slots(st, ix, &iptr, &optr);
	*iptr = *optr = d.cn.init;
	break;
    }
    case DECL_TIMER:
	if (setup_slot(st, ix) < 0)
	    return -1;
	setup_timer(st, ix);
	if (st->nt < st->timer_cap) {
	    st->timer_obj[st->nt] = st->cur;
	    st->timer[st->nt++] = ix;
	}
	break;
    case DECL_DIGITAL:
	if (setup_slot(st, ix) < 0)
	    return -1;
	setup_digital(st, ix);
	add_io(st, ix);
	break;
    case DECL_ANALOG:
	if (setup_slot(st, ix) < 0)
	    return -1;
	setup_analog(st, ix);
	add_io(st, ix);	
	break;
    case DECL_FIELD:
	if (setup_field(st, ix) < 0)
	    return -1;
	add_io(st, ix);
	break;
    case DECL_BUFFER:
	if (setup_buffer(st, ix) < 0)
	    return -1;	
	break;
    default:
	return -1;	
    }
    return 0;
}


// Count the buffer/heap/io a single value-leaf decl `j` needs, mirroring the
// setup_* functions (which decls get a buffer, its byte size, and whether it is
// device I/O). Used by csp_estimate; must track those functions to stay exact.
NOINLINE static void est_leaf(csp_rt_t* st, int j, csp_estimate_t* e)
{
    csp_decl_t d = csp_get_decl(st, j);
    uint16_t nbytes;              // matches csp_buf_t.nbytes; see csp_estimate_t.heap
    switch (d.type) {
    case DECL_VARIABLE:
	if (d.bound)                          // bit-field view: shares a buffer
	    return;
	nbytes = (GET_RES(d.res) + 7) >> 3;
	break;
    case DECL_BUFFER:
	nbytes = d.bf.nbytes;                // #buffer: size is in bf.nbytes
	break;
    case DECL_TIMER:
	e->nt++;                              // timer list entry (setup_timer)
	nbytes = sizeof(value_t);
	break;
    case DECL_CONSTANT:
	nbytes = sizeof(value_t);
	break;
    case DECL_DIGITAL:
    case DECL_ANALOG:
	nbytes = sizeof(value_t);
	e->nio++;                             // listed whatever the direction
	break;
    case DECL_FIELD:
	// A field is a view into its #buffer, which is counted as a buffer in
	// its own right. Only the I/O list entry belongs here.
	if (d.dir & DIR_INOUT) e->nio++;
	return;
    default:
	return;                               // module/end/object/view: no storage
    }
    // Only a real #buffer takes a csp_buf_t. Every other leaf owns its bytes and
    // carries the heap offset in its view -- see view_kind_t. Counting a buffer
    // for each of them was 16 bytes of table per declaration.
    if (d.type == DECL_BUFFER)
	e->nbuf++;
    e->heap += (uint16_t)((nbytes + 3) & ~3u);   // 4-aligned like csp_heap_alloc
}

// Memory an already-parsed program needs, WITHOUT running csp_rt_start: the same
// global + per-object walk (offs build included), counting only. nleaf must be
// exact (it sizes view[]/dset); nbuf/heap/ni/no match the setup_* allocations.
NOINLINE void csp_estimate(csp_rt_t* st, csp_estimate_t* e)
{
    int i, in_module = 0;
    index_t nd = st->ps.nd;
    int offs = nd;                            // object storage base (== rt_start)

    e->nleaf = nd;                            // globals occupy leaves [0, nd)
    e->nbuf = e->nio = e->nt = 0;
    e->nobj = e->nm = 0;
    e->heap = 0;

    for (i = 0; i < (int)nd; i++) {           // globals
	switch (decl(st,i,type)) {
	case DECL_MODULE: in_module = 1; e->nm++; break;
	case DECL_END:    in_module = 0; break;
	case DECL_OBJECT: e->nobj++; break;       // handled in the object pass
	default:
	    if (!in_module) est_leaf(st, i, e);
	    break;
	}
    }
    for (i = 0; i < (int)nd; i++) {           // objects: offs build + members
	index_t mx;
	int dn, base, j, top;
	csp_decl_t od = csp_get_decl(st, i);
	if (od.type != DECL_OBJECT)
	    continue;
	mx = od.mq.mx;
	dn = decl(st, INDEX(mx), md.n);
	base = INDEX(mx) + 1;                  // members' decl indices: base..base+dn-1
	top = offs + base + dn;               // one past this object's last leaf
	if (top > (int)e->nleaf) e->nleaf = top;
	for (j = 0; j < dn; j++)
	    est_leaf(st, base + j, e);
	offs += dn;
    }
}

// Lay the whole program out again: the reactive graph, then the leaf/device
// setup. These two used to be called separately, which was fine while each
// malloc'd its own tables -- but they now bump-allocate from one shared middle
// region, so running rt_start alone would hand csr's tables out a second time.
// One entry point keeps the region consistent, and it also means a declaration
// added at the prompt rewires the graph (running rt_start alone left the new
// decl above graph_n, i.e. with no edges, so it never fired reactively).
// Walk the instruction stream and, for every rule whose NUMBER is in dis_rule,
// set the bit for that rule's OP_RULE ip in dis_ip. Also refreshes n_rule_no.
//
// Rule N is the Nth OP_RULE in instruction order, 1-based. Every rule emits
// exactly one -- an unguarded rule gets an always-true LI first -- so the
// sequence has no holes. Note this is NOT the reactive ordinal from
// number_rules, which also numbers module entries and range-base bodies.
//
// Keying on the OP_RULE rather than on the rule's first instruction is
// deliberate. A rule's start can only be inferred statically, and the set of
// instructions that end a csp_eval_rule call is larger than it looks
// (OP_NEXT, OP_ENTER, OP_NEW, OP_LEAVE, and OP_INSTATE when it skips a block).
// Getting that list wrong put a disable bit on an OP_LEAVE, whose skip then
// swallowed the matching pops and ran the object stack off its end. The
// OP_RULE ip needs no inference: it is right here in the walk.
NOINLINE index_t csp_n_rules(csp_rt_t* st)
{
    index_t i, no = 0;
    for (i = 0; i < st->ps.nn; i++) {
	if (instr(st, i, op) == OP_RULE)
	    no++;
    }
    return no;
}

NOINLINE static void build_dis_ip(csp_rt_t* st)
{
    index_t i;
    int no = 0;
    int any = 0;

    st->dis_ip = NULL;
    st->n_rule_no = 0;
    if (st->ps.nn == 0)
	return;
    for (i = 0; i < BITSET_GROUPS(MAX_DIS_RULES); i++)
	any |= (st->dis_rule[i] != 0);

    for (i = 0; i < st->ps.nn; i++) {
	if (instr(st, i, op) != OP_RULE)
	    continue;
	no++;
	// Allocate lazily: a program with nothing disabled never pays for the
	// table, and a failed allocation leaves dis_ip NULL (nothing is
	// skipped) rather than silently skipping the wrong rules.
	if (any && (no <= MAX_DIS_RULES) && bitset_tst(st->dis_rule, no-1)) {
	    if (st->dis_ip == NULL) {
		st->dis_ip = (set_group_t*)csp_mid_alloc(st,
			      (size_t)BITSET_GROUPS(st->ps.nn) * sizeof(set_group_t));
		if (st->dis_ip == NULL)
		    return;
	    }
	    bitset_set(st->dis_ip, i);
	}
    }
    st->n_rule_no = (index_t)no;
}

int csp_rebuild(csp_rt_t* st)
{
    csp_mid_reset(st);          // forget the old layout; everything below re-bumps
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive)
	csp_csr(st);
#endif
    build_dis_ip(st);           // rule numbers -> start ips, after mid_reset
    // Everything emitted so far is now covered: clear both staleness signals.
    st->graph_rules = st->n_rule_emit;
    st->edited = 0;
    return csp_rt_start(st);
}

// given an object index get index of module def
NOINLINE static index_t get_mq_m(csp_rt_t* st, index_t ix)
{
    index_t mx = decl(st, INDEX(ix), mq.mx);  // module def
    return mx;
}

// given a module def index return number of elements
NOINLINE static ivalue_t get_md_n(csp_rt_t* st, index_t mx)
{
    return decl(st, INDEX(mx), md.n);
}

// copy constant and init values
// setup input, output and timer lists
//
// Two declaration names, compared through the string table. Both sides are
// positions, so neither can be handed to csp_str_eq (which wants a char*).
NOINLINE static int name_eq(csp_rt_t* st, sindex_t a, sindex_t b)
{
    uint8_t la, i;
    if ((a == 0) || (b == 0))
	return 0;
    // A name's LENGTH sits at pos-1 and its characters at pos -- the layout
    // csp_str_eq reads, and the reason this cannot just call it: both sides
    // here are positions, not a char*.
    la = csp_str_byte(st, a - 1);
    if (la != csp_str_byte(st, b - 1))
	return 0;
    for (i = 0; i < la; i++)
	if (csp_str_byte(st, a+i) != csp_str_byte(st, b+i))
	    return 0;
    return 1;
}

// Is this declaration a #param? DECL_CONSTANT with the `local` bit -- the same
// trick #local plays on DECL_VARIABLE.
static int is_param(const csp_decl_t* d)
{
    return (d->type == DECL_CONSTANT) && d->local && (d->name != 0);
}

// The RAM override declared for param `di`, or BAD_INDEX. A param that has one
// is not what any listing should show: its cn.init is the value the program
// SHIPPED with, and the override is the value it runs with.
index_t csp_param_shadow(csp_rt_t* st, index_t di)
{
    csp_decl_t d = csp_get_decl(st, di);
    index_t i;
    if (!is_param(&d))
	return BAD_INDEX;
    // From di+1, not from the RAM base: a shadow is always LATER than what it
    // sets, and starting at rom_nd made a shadow that happened to BE the first
    // RAM declaration find itself -- so the listing suppressed both rows and
    // the param vanished.
    for (i = di + 1; i < st->ps.nd; i++) {
	csp_decl_t o = csp_get_decl(st, i);
	if (is_param(&o) && name_eq(st, o.name, d.name))
	    return i;
    }
    return BAD_INDEX;
}

// The reverse: the param this RAM declaration SETS, or BAD_INDEX when it is a
// declaration in its own right.
index_t csp_param_target(csp_rt_t* st, index_t di)
{
    csp_decl_t d = csp_get_decl(st, di);
    index_t j;
    if (!is_param(&d))
	return BAD_INDEX;
    for (j = 0; j < di; j++) {
	csp_decl_t t = csp_get_decl(st, j);
	if (is_param(&t) && name_eq(st, d.name, t.name))
	    return j;
    }
    return BAD_INDEX;
}

// Apply #param overrides.
//
// A RAM param whose name matches an EARLIER param is not a declaration of its
// own: it is a saved value FOR that one. That is how `#param Kp = 9` works
// against a Kp baked into ROM -- the ROM declaration's cn.init sits in flash and
// cannot be written, but the slot it was seeded into is RAM, and the override
// rides into EEPROM as an ordinary RAM declaration with no new format.
//
// Here rather than in csp_compile.c because the EEPROM patch is loaded by every
// firmware, including an exec-only one that has no compiler at all.
//
// By NAME -- meaning the CHARACTERS, compared through the string table -- and
// not by comparing the two `name` fields, which are string POSITIONS. They never
// match: csp_new_decl calls new_string unconditionally, with no lookup, so the
// override's "Kp" is a second copy at its own position even though the ROM
// string table already holds that exact word. (add_state and push_str do look a
// string up first; declaration names do not.)
//
// It is NOT about surviving a reflash. csp_eeprom_load rejects the whole save
// when rom_header.crc_hdr moves, so a patch never crosses a changed program in
// the first place -- see the note there.
NOINLINE static void apply_param_overrides(csp_rt_t* st)
{
    index_t i;

    for (i = st->rom_nd; i < st->ps.nd; i++) {
	index_t j = csp_param_target(st, i);
	value_t* iptr;
	value_t* optr;
	if (j == BAD_INDEX)
	    continue;
	csp_dio_slots(st, MAKE_INDEX(0, j), &iptr, &optr);
	*iptr = *optr = csp_get_decl(st, i).cn.init;
    }
}

// ============================================================
// SETTINGS -- values for things the firmware ALREADY declares: a #param trimmed
// against this motor, a pin moved because this board is wired differently.
//
// A separate store from the eeprom patch because it has a separate LIFETIME.
// The patch is program text belonging to one firmware and is dropped when
// rom_fp moves. A setting belongs to the UNIT, and nothing about it goes back
// into the source, so a reflash must not take it -- see doc/EEPROM.md.
//
// That is what decides the encoding. Surviving a reflash means surviving a
// renumbering of every declaration, so an entry names its target as CHARACTERS
// and resolves it at boot:
//
//   u8 plen | char path[plen] | u8 part | u8 vt | u8 res | value
//
// where `value` is sizeof(value_t) raw bytes, or -- when vt is V_STRING -- a u8
// length followed by that many characters. A string CANNOT be stored as the
// sindex_t it is in a slot: that is a position in a string table the next boot
// rebuilds, and it would point somewhere else or nowhere.
// ============================================================

#define SET_PLEN(p)   ((p)[0])
#define SET_PATH(p)   ((const char*)((p) + 1))
#define SET_PART(p)   ((p)[1 + SET_PLEN(p)])
#define SET_VT(p)     ((p)[2 + SET_PLEN(p)])
#define SET_RES(p)    ((p)[3 + SET_PLEN(p)])
#define SET_VAL(p)    ((p) + 4 + SET_PLEN(p))
#define SET_FIX(plen) (4 + (plen))   // bytes before the value

// Bytes entry `p` occupies. The store is WALKED, not indexed: entries vary in
// length and there are few enough that a scan beats an offset table that would
// have to be kept in step with every edit.
static uint16_t set_len(const uint8_t* p)
{
    uint8_t plen = SET_PLEN(p);
    if (SET_VT(p) == V_STRING)
	return (uint16_t)(SET_FIX(plen) + 1 + SET_VAL(p)[0]);
    return (uint16_t)(SET_FIX(plen) + sizeof(value_t));
}

// Copy a declaration name out of the string table. Returns the length, or 0 for
// a nameless declaration or one that will not fit.
static uint8_t name_chars(csp_rt_t* st, sindex_t pos, char* buf, uint8_t room)
{
    uint8_t len, i;
    if (pos == 0)
	return 0;
    len = csp_str_byte(st, pos - 1);
    if ((len == 0) || (len > room))
	return 0;
    for (i = 0; i < len; i++)
	buf[i] = (char)csp_str_byte(st, pos + i);
    return len;
}

// The path an entry names: "Kp" for a global, "sys.NodeID" for a member of an
// object. The same text you would type at the prompt -- which is exactly why a
// module member costs nothing extra here. There is no object index to find room
// for in an 8-byte declaration and no new declaration type to invent, because
// the dot is just a character.
static uint8_t set_path(csp_rt_t* st, xindex_t ix, char* buf)
{
    unsigned m = XOBJ(ix);
    uint8_t n = 0, k;

    if (m == XOBJ_CURRENT)
	m = st->cur;
    if ((m != XOBJ_GLOBAL) && (m <= (unsigned)st->ps.nq)) {
	k = name_chars(st, decl(st, INDEX(st->object[m]), name), buf,
		       CSP_SETTINGS_MAX_PATH - 2);
	if (k == 0)
	    return 0;
	n = k;
	buf[n++] = '.';
    }
    k = name_chars(st, decl(st, XIDX(ix), name), buf + n,
		   (uint8_t)(CSP_SETTINGS_MAX_PATH - n));
    return k ? (uint8_t)(n + k) : 0;
}

// What the DECLARATION says this part is -- the value a setting has to differ
// from to be worth storing.
//
// Built by re-seeding a scratch slot through the same setup_*_values the real
// slot went through, rather than by a second switch over the union members. One
// place decides what a declared pin/period/pull-up is, so the two cannot drift.
static int set_declared(csp_rt_t* st, xindex_t ix, csp_part_t part, value_t* vp)
{
    csp_decl_t d;
    value_t ref;

    csp_copy_decl(st, XIDX(ix), &d);
    if (part == PART_VAL) {
	if ((d.type != DECL_CONSTANT) || !d.local)
	    return 0;               // only a #param has a settable value
	*vp = d.cn.init;
	return 1;
    }
    ref.u = 0;
    switch (CSP_MASK(d.type, CSP_DECL_TYPE_BITS)) {
    case DECL_DIGITAL: setup_digital_values(&ref, d.di); break;
    case DECL_ANALOG:  setup_analog_values(&ref, d.an);  break;
    case DECL_TIMER:   setup_timer_values(&ref, d.tm);   break;
    default: return 0;
    }
    csp_part_get(&ref, decl_cfg_vt(d.type, d.vt), part, vp);
    return 1;
}

// Find the entry for this path+part. Returns its offset, or set_used for "not
// there" -- which is also where an append goes.
static uint16_t set_find(csp_rt_t* st, const char* path, uint8_t plen,
			 csp_part_t part)
{
    uint16_t off = 0;
    while (off < st->set_used) {
	const uint8_t* p = st->settings + off;
	if ((SET_PLEN(p) == plen) && (SET_PART(p) == (uint8_t)part) &&
	    (memcmp(SET_PATH(p), path, plen) == 0))
	    return off;
	off += set_len(p);
    }
    return st->set_used;
}

static void set_remove(csp_rt_t* st, uint16_t off)
{
    uint16_t n;
    if (off >= st->set_used)
	return;
    n = set_len(st->settings + off);
    memmove(st->settings + off, st->settings + off + n,
	    (size_t)(st->set_used - off - n));
    st->set_used -= n;
    st->set_dirty = 1;
}

void csp_settings_clear(csp_rt_t* st)
{
    st->set_used = 0;
    st->set_dirty = 1;
}

int csp_settings_get(csp_rt_t* st, int n, csp_setting_t* sp)
{
    uint16_t off = 0;
    int k = 0;

    while (off < st->set_used) {
	const uint8_t* p = st->settings + off;
	if (k == n) {
	    sp->path = SET_PATH(p);
	    sp->plen = SET_PLEN(p);
	    sp->part = SET_PART(p);
	    sp->vt   = SET_VT(p);
	    sp->res  = SET_RES(p);
	    sp->str  = NULL;
	    sp->slen = 0;
	    sp->val.u = 0;
	    if (sp->vt == V_STRING) {
		sp->slen = SET_VAL(p)[0];
		sp->str  = (const char*)(SET_VAL(p) + 1);
	    }
	    else
		// memcpy, NOT a cast through value_t*: the value sits at
		// 4 + plen, which is odd for half the paths there are, and an
		// unaligned 32-bit load HardFaults on Cortex-M0.
		memcpy(&sp->val, SET_VAL(p), sizeof(value_t));
	    return 1;
	}
	off += set_len(p);
	k++;
    }
    return 0;
}

// Does this path still name something in the running program? Splits on the
// dot: left of it an object, right of it a member of that object's module.
//
// In csp_rt.c rather than in the compiler because an exec-only firmware has no
// parser and still has to apply its settings at boot.
int csp_settings_resolve(csp_rt_t* st, const csp_setting_t* sp,
			 xindex_t* ixp, index_t* objp)
{
    tstr_t nm;
    index_t ix;
    int dot = -1;
    int i;

    for (i = 0; i < (int)sp->plen; i++)
	if (sp->path[i] == '.') { dot = i; break; }

    if (dot < 0) {
	nm.ptr = (char*)sp->path;
	nm.len = sp->plen;
	if ((ix = lookup_decl_in(st, &nm, 0, st->ps.nd)) == BAD_INDEX)
	    return 0;
	*ixp = MAKE_XINDEX(XOBJ_GLOBAL, INDEX(ix));
	*objp = 0;
	return 1;
    }

    // The instance, by name. Walked over object[] rather than looked up as a
    // declaration: object numbers are what the context needs, and this table is
    // the one that has them.
    nm.ptr = (char*)sp->path;
    nm.len = dot;
    for (i = 1; i <= (int)st->ps.nq; i++) {
	index_t ox = st->object[i];
	index_t mx, base;
	ivalue_t dn;
	if (!csp_str_eq(st, decl(st, INDEX(ox), name), nm.ptr, nm.len))
	    continue;
	mx   = get_mq_m(st, ox);
	base = (index_t)(INDEX(mx) + 1);
	dn   = get_md_n(st, mx);
	nm.ptr = (char*)sp->path + dot + 1;
	nm.len = sp->plen - dot - 1;
	if ((ix = lookup_decl_in(st, &nm, base, base + dn)) == BAD_INDEX)
	    return 0;
	*ixp = MAKE_XINDEX(i, INDEX(ix));
	*objp = (index_t)i;
	return 1;
    }
    return 0;
}

// Would this entry actually be applied to `ix`?
//
// The boot-time twin of the ERR_PARAM_SHAPE the parser makes when a #param line
// redeclares one: a firmware that widened Kp to :32 gets an entry that still
// says 16, and the declaration wins. Shared with the listing so an entry that
// is stored but REFUSED does not get tagged as if it were in effect.
static int set_applies(csp_rt_t* st, const csp_setting_t* sp, xindex_t ix)
{
    csp_decl_t d;

    if (sp->part != PART_VAL)
	return 1;
    csp_copy_decl(st, XIDX(ix), &d);
    return (d.type == DECL_CONSTANT) && d.local &&
	   (sp->res == (uint8_t)GET_RES(d.res)) &&
	   (sp->vt == CSP_MASK(d.vt, TYPE_BITS));
}

// What became of this entry: CSP_SET_ORPHAN (no such name any more),
// CSP_SET_REFUSED (the name is there but the shape moved) or CSP_SET_LIVE.
// /settings prints all three -- a stored value that is not in effect and does
// not say so is worse than no store at all.
int csp_settings_status(csp_rt_t* st, const csp_setting_t* sp)
{
    xindex_t ix;
    index_t obj;

    if (!csp_settings_resolve(st, sp, &ix, &obj))
	return CSP_SET_ORPHAN;
    return set_applies(st, sp, ix) ? CSP_SET_LIVE : CSP_SET_REFUSED;
}

// Is declaration `di` overridden by a setting? For the listing, which otherwise
// shows a value that is not the one in effect: /list prints what the SOURCE
// says, and a setting is by definition a value that differs from it.
//
// Compares the resolved declaration index, so a module member answers yes when
// ANY instance of it is overridden -- the listing shows the template, and one
// line cannot say which instance. /settings has the detail.
int csp_settings_covers(csp_rt_t* st, index_t di)
{
    csp_setting_t s;
    int n;

    for (n = 0; csp_settings_get(st, n, &s); n++) {
	xindex_t ix;
	index_t obj;
	if (csp_settings_resolve(st, &s, &ix, &obj) && (XIDX(ix) == di) &&
	    set_applies(st, &s, ix))
	    return 1;
    }
    return 0;
}

// Record what an IMMEDIATE write set. Rules never reach here -- a rule writing
// a config part is the program doing its job, not someone configuring the unit,
// and freezing that into eeprom would restore a value the rule recomputes on
// the next cycle anyway.
int csp_settings_record(csp_rt_t* st, xindex_t ix, csp_part_t part, value_t v)
{
    char path[CSP_SETTINGS_MAX_PATH];
    // Staging for the value on its way to the wire. Sized for a STRING, not for
    // a value_t: a string setting is stored as characters, and cutting it to
    // sizeof(value_t) turned "Node2" into "Node".
    uint8_t plen, vt, res, buf[CSP_SETTINGS_MAX_STR + 1];
    uint8_t vlen;
    uint16_t off, need;
    value_t dv;
    csp_decl_t d;

    if (!CSP_PART_IS_CFG(part) && (part != PART_VAL))
	return 0;
    if (!set_declared(st, ix, part, &dv))
	return 0;               // not a param, not a config part of this type
    if ((plen = set_path(st, ix, path)) == 0)
	return 0;

    csp_copy_decl(st, XIDX(ix), &d);
    vt  = (part == PART_VAL) ? CSP_MASK(d.vt, TYPE_BITS) : V_UNSIGNED;
    res = (uint8_t)GET_RES(d.res);

    if (vt == V_STRING) {
	uint8_t n = (v.s == 0) ? 0 : csp_str_byte(st, v.s - 1);
	uint8_t j;
	if (n > CSP_SETTINGS_MAX_STR) {
	    csp_set_error(st, ERR_STRING_SPACE_EXHUSTED);
	    return -1;               // refuse, rather than store half a name
	}
	// A string setting stores CHARACTERS. v.s is a position in a table the
	// next boot rebuilds from the source, so the position itself is
	// meaningless once the firmware changes.
	buf[0] = n;
	for (j = 0; j < n; j++)
	    buf[1+j] = csp_str_byte(st, v.s + j);
	vlen = (uint8_t)(1 + n);
    }
    else {
	memcpy(buf, &v, sizeof(value_t));
	vlen = sizeof(value_t);
    }

    off = set_find(st, path, plen, part);

    // A value equal to the declaration is not a setting. Storing it would fill
    // the store with no-ops, and -- worse -- the day the default changes in the
    // source it would be silently shadowed by an entry that matched it once.
    if (vt == V_STRING) {
	// Compared as CHARACTERS. The two positions differ even for identical
	// text -- a declaration name is installed without a lookup -- so
	// dv.s == v.s would almost never be true and every string setting would
	// be stored, including one that restores the declared name.
	uint8_t dn = (dv.s == 0) ? 0 : csp_str_byte(st, dv.s - 1);
	uint8_t j;
	if (dn == buf[0]) {
	    for (j = 0; j < dn; j++)
		if (csp_str_byte(st, dv.s + j) != buf[1+j])
		    break;
	    if (j == dn) {
		set_remove(st, off);
		return 0;
	    }
	}
    }
    else if (dv.u == v.u) {
	set_remove(st, off);
	return 0;
    }

    if (off < st->set_used)
	set_remove(st, off);
    need = (uint16_t)(SET_FIX(plen) + vlen);
    if ((uint32_t)st->set_used + need > CSP_SETTINGS_BYTES) {
	csp_set_error(st, ERR_CANNOT_SAVE);
	return -1;
    }
    {
	uint8_t* p = st->settings + st->set_used;
	p[0] = plen;
	memcpy(p + 1, path, plen);
	p[1 + plen] = (uint8_t)part;
	p[2 + plen] = vt;
	p[3 + plen] = res;
	memcpy(p + SET_FIX(plen), buf, vlen);
	st->set_used += need;
	st->set_dirty = 1;
    }
    return 1;
}

// Lay the store over the declarations.
//
// Called from csp_rt_start, so it lands BEFORE csp_setup configures any
// hardware. That ordering is the whole reason a setting is not an `#in INIT`
// rule: an INIT block runs at the END of the first cycle, by which time setup
// has already driven the pin the declaration named.
void csp_settings_apply(csp_rt_t* st)
{
    csp_setting_t s;
    int n;

    for (n = 0; csp_settings_get(st, n, &s); n++) {
	xindex_t ix;
	index_t obj;
	value_t v;
	int cur_save;
	index_t cbase_save;

	// An orphan -- the name was removed or renamed in the firmware now
	// flashed. Kept, not applied: the next firmware may reintroduce it, and
	// a calibration is expensive to recreate. /settings shows it.
	if (!csp_settings_resolve(st, &s, &ix, &obj))
	    continue;
	if (!set_applies(st, &s, ix))
	    continue;

	if (s.vt == V_STRING) {
	    // lookup_string FIRST. csp_rt_start reruns on every rebuild, and a
	    // bare new_string would push another copy of the same characters into
	    // the table each time -- a leak that grows with the number of edits,
	    // not with the number of settings.
	    int pos = lookup_string(st, (char*)s.str, s.slen);
	    if (pos < 0)
		pos = new_string(st, (char*)s.str, s.slen);
	    if (pos < 0)
		continue;           // no string room: leave the declared value
	    v.s = (sindex_t)pos;
	}
	else
	    v = s.val;

	cur_save = st->cur;
	cbase_save = st->cbase;
	if (obj != 0)
	    csp_ctx_set(st, obj);
	{
	    index_t lx = MAKE_INDEX(obj ? CURRENT : GLOBAL, XIDX(ix));
	    if (s.part == PART_VAL) {
		// The slots directly, as apply_param_overrides does. A plain
		// integer slot has no PART_VAL row in the layout table, so
		// csp_dio_set_part would find r == 0 and write nothing at all.
		value_t* iptr;
		value_t* optr;
		if (csp_dio_slots(st, lx, &iptr, &optr) == 0)
		    *iptr = *optr = v;
	    }
	    else {
		// Both halves of the transaction. A config part written only to
		// DOUT would be read back from a DIN still holding the declared
		// value.
		csp_dio_set_part(st, lx, v, (csp_part_t)s.part, DOUT);
		csp_dio_set_part(st, lx, v, (csp_part_t)s.part, DIN);
	    }
	}
	st->cur = cur_save;
	st->cbase = cbase_save;
    }
}

int csp_rt_start(csp_rt_t* st)
{
    int i;
    int offs;
    int in_module = 0;
    // value_t* iptr;
    // value_t* optr;
    csp_estimate_t e;

    // Not started until the tables below exist. Cleared HERE, not only in the
    // failure path: a rebuild that runs out of arena used to leave `started` at
    // 1 from the previous successful layout, and csp_cycle's guard then let the
    // next cycle run against NULL view/heap pointers. `#buffer B:1023` twice on
    // a mega segfaulted in state_advance for exactly that reason.
    st->started = 0;

    // Size every derived table to what this program actually declares
    // (csp_estimate, no prior setup) -- nothing here is a MAX_* reservation.
    // Freed+reallocated on every rebuild; the fill below stays within these.
    // rt_start reruns on any decl add (see csp_process_persistent) so view_cap
    // always covers max st_index.
    {
	size_t hbytes;
	csp_estimate(st, &e);
	// The DIN/DOUT transaction halves are ONE block: heap[DOUT] points at the
	// second half. 8-aligned so a buffer at the same hp offset is equally
	// aligned in both halves.
	hbytes = CSP_A8(e.heap ? e.heap : 8);
	// Caps cleared BEFORE the allocations, not in the failure path. Any exit
	// from here on then leaves cap and pointer agreeing, and the five stores
	// exist once instead of once per path.
	st->heap[DOUT] = NULL;
	st->view_cap = 0; st->buf_cap = 0; st->heap_cap = 0;
	st->io_cap = 0; st->timer_cap = 0;
	st->obj_cap = 0; st->mod_cap = 0;
	// csp_mid_alloc already records a failed request in mid_full, so the six
	// pointers do not each need testing -- one flag answers for all of them.
	// Cleared here rather than trusted from csp_mid_reset: rt_start can be
	// called on its own (see csp_eeprom_load), and then the flag would be
	// whatever the previous layout left behind.
	st->mid_full = 0;
	// All of these come out of the middle, already zeroed. Nothing to free: the
	// next csp_rebuild resets the cursor and lays the region out again.
	st->view  = (csp_view_t*)csp_mid_alloc(st, (size_t)e.nleaf * sizeof(csp_view_t));
	st->dset  = (set_group_t*)csp_mid_alloc(st,
		     (size_t)BITSET_GROUPS(e.nleaf ? e.nleaf : 1) * sizeof(set_group_t));
	st->buf   = (csp_buf_t*)csp_mid_alloc(st, (size_t)e.nbuf * sizeof(csp_buf_t));
	st->heap[DIN] = (uint8_t*)csp_mid_alloc(st, 2 * hbytes);
	st->io     = (index_t*)csp_mid_alloc(st, (size_t)e.nio * sizeof(index_t));
	st->io_obj = (uint8_t*)csp_mid_alloc(st, (size_t)e.nio);
	st->timer  = (index_t*)csp_mid_alloc(st, (size_t)e.nt * sizeof(index_t));
	st->timer_obj = (uint8_t*)csp_mid_alloc(st, (size_t)e.nt);
	// +1: object numbers are 1-based, slot 0 is the global base (offs[0] == 0).
	st->offs   = (index_t*)csp_mid_alloc(st, (size_t)(e.nobj+1) * sizeof(index_t));
	st->object = (index_t*)csp_mid_alloc(st, (size_t)(e.nobj+1) * sizeof(index_t));
	st->module = (index_t*)csp_mid_alloc(st, (size_t)e.nm * sizeof(index_t));
	if (st->mid_full) {
	    // Bytes, not counts: the arena could not hold the derived tables.
	    csp_set_error(st, ERR_OUT_OF_MEMORY);
	    return -1;              // caps are already zero -- see above
	}
	st->heap[DOUT] = st->heap[DIN] + hbytes;   // second half of the same block
	st->view_cap = e.nleaf;
	st->buf_cap  = e.nbuf;
	st->heap_cap = e.heap;
	st->io_cap   = e.nio;
	st->timer_cap = e.nt;
	st->obj_cap  = e.nobj + 1;
	st->mod_cap  = e.nm;
    }

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->es.pending_cap) {          // drop any work left from a previous build
	uint32_t g = BITSET_GROUPS(st->es.pending_cap) * sizeof(set_group_t);
	memset(st->es.pending[0], 0, g);
	memset(st->es.pending[1], 0, g);
	st->es.gen = 0;
    }
#endif
    // SAFE state: unwind any runtime accumulators so /clear, /reset and a
    // post-parse rebuild all land on a coherent baseline (like boot's memset).
    st->esp = 0;              // drop half-run OP_NEW/OP_LEAVE call stack
    st->cur = 0;              // back to the global module
    memset(st->dset, 0, BITSET_GROUPS(st->view_cap) * sizeof(set_group_t));    // no stale dirty leaves survive into the rebuild
    st->nt = 0;
    st->nio = 0;
    st->nm = 0;
    st->nbuf = 0;
    st->hp = 0;      // the heap cursor is no longer derivable from buf[nbuf-1]
    st->ps.nq = 0;   // rebuilt from DECL_OBJECT below (parse-time table is not
		     // restored from ROM); idempotent for a freshly parsed program

    // (view/dset/heap were allocated + zeroed above; setup_* fills view below.)

    for (i = 0; i < st->ps.nd; i++) {
	index_t ix = MAKE_INDEX(0,i);
	csp_decl_t d = csp_get_decl(st, i);   // one read per declaration
	switch(d.type) {
	case DECL_MODULE:
	    in_module=1;
	    if (st->nm < st->mod_cap)
		st->module[st->nm++] = ix;
	    break;
	case DECL_END:
	    in_module = 0;
	    break;
	case DECL_OBJECT:
	    // Rebuild the object slot table (1-based, decl order == parse order),
	    // so ROM-baked objects get per-object storage and list with their real
	    // names. Per-object value init still happens after offs[] is allocated.
	    // Numbered here, NOT read from the declaration's mq.m: this walk visits
	    // the objects in the same order the parser created them, so the two
	    // agree -- and this is the side that also has to work for a ROM image,
	    // where no parser ever ran.
	    if (st->ps.nq + 1 < st->obj_cap)
		st->object[++st->ps.nq] = ix;
	    break;
	case DECL_VIEW: {
	    // synthetic Buf[a..b] view: translate to a HEAP view into the
	    // parent buffer (already set up, since it has a lower index)
	    csp_view_t* pv = &st->view[d.ca.id];
	    setup_view_values(&st->view[st_index(st, ix)], d.vt, pv->buf, d.ca);
	    break;
	}
	case DECL_VARIABLE:
	case DECL_CONSTANT:
	case DECL_TIMER:
	case DECL_DIGITAL:
	case DECL_ANALOG:
	case DECL_FIELD:
	case DECL_BUFFER:
	    if (!in_module) {
		if (setup_decl(st, ix, d) < 0)
		    return -1;
	    }
	    break;
	default:
	    break;
	}
    }
    // allocate object 1..nq storage
    offs = st->ps.nd;
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	index_t ix = st->object[m];
	index_t mx = get_mq_m(st, ix);   // get module
	ivalue_t dn = get_md_n(st, mx);  // number of decl elements
	st->offs[m] = offs;
	offs += dn;
	if (offs > MAX_DECLS) {
	    // when objects are included
	    csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	    return -1;
	}
    }
    // init per-object data (after offs[] is set)
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	int j;
	index_t ix = st->object[m];
	index_t mx = get_mq_m(st, ix);   // get module
	ivalue_t dn = get_md_n(st, mx);  // number of decl elements
	int base = INDEX(mx)+1;

	// Set up the object context the way OP_NEW would, so a member is
	// addressed CURRENT-relative: an encoded index carries a selector, not an
	// object number, and setup_decl/parent_leaf read st->cur to learn which
	// instance they are building.
	st->cur   = m;
	st->cbase = st->offs[m];

	for (j = 0; j < dn; j++) {
	    int dj = base + j;         // decl index
	    index_t fx = MAKE_INDEX(CURRENT, dj); // field index
	    csp_decl_t d = csp_get_decl(st, dj);   // one read per member
#ifdef DEBUG
	    DBG("init OBJECT %s, FIELD %s[%d]\n",
		decl_name(st, ix), decl_name(st, fx), dj);
#endif
	    switch (d.type) {
		// VIEW?
	    case DECL_VARIABLE:
	    case DECL_CONSTANT:
	    case DECL_TIMER:
	    case DECL_DIGITAL:
	    case DECL_ANALOG:
	    case DECL_BUFFER:
	    case DECL_FIELD:
		if (setup_decl(st, fx, d) < 0)
		    return -1;
		break;
		
	    default:
		break;
	    }
	}
    }
    apply_param_overrides(st);
    // After the patch overrides -- a setting is the unit's word on a value, and
    // it is the last one. Before csp_setup, which the platform main calls next:
    // a re-pinned output must never be configured on the pin the source named.
    csp_settings_apply(st);
    sys_publish(st);
    st->cur   = 0;     // back to global before anything executes
    st->cbase = 0;
    st->cycle = 0;  // init trace shows cycle 0
    st->started = 1;   // leaves allocated + set up; value ops are now valid
    return 0;
}

int csp_set_reactive(csp_rt_t* st, int onoff)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    st->reactive = onoff;
    return 0;
#endif
    return -1;
}

int csp_set_latch(csp_rt_t* st, int onoff)
{
    int old_value = st->latch;
    st->latch = onoff;
    return old_value;
}

// Common timer input (called from cs_input)

void csp_input_timer(csp_rt_t* st)
{
    int i;
    uvalue_t now_ms;

    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; i++) {
	index_t ix = csp_timer_at(st, i);
	value_t* iptr;
	value_t* optr;

	csp_dio_slots(st, ix, &iptr, &optr);
	iptr->t.fired = optr->t.fired = 0;
	// tx value: 0=stopped, >0=running (start_time+1)
	if (iptr->t.running) {
	    // The start-time slot is the next declaration, in the same object --
	    // and "same object" is now just the selector bit riding along, so
	    // plain +1 says it. It used to need MAKE_INDEX(OBJ(ix), INDEX(ix+1))
	    // to keep the object field from being carried into by the increment.
	    index_t tx = ix + 1;
	    value_t* txptr = csp_dio_slot(st, tx, DIN);
	    uvalue_t t0 = txptr->u;
	    if ((now_ms - t0) >= iptr->t.period) {
		iptr->t.running = optr->t.running = 0; // not running
		iptr->t.val = optr->t.val = 0;         // off
		iptr->t.fired = optr->t.fired = 1;     // fired
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
		if (st->reactive) {
		    csp_enq_elist(st, ix);
		}
#endif
	    }
	}
    }
    csp_ctx_reset(st);
}

// Expect current value to be in din (after commit)
void csp_output_timer(csp_rt_t* st)
{
    int i;
    uint32_t now_ms;
    uint32_t wait_ms = NOTIMEOUT;

    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; ++i) {
	index_t ix = csp_timer_at(st, i);
	// The timer's start-time slot: an ordinary variable at the next decl
	// index, in the same object. Computed once -- the stopped branch used to
	// declare a second `tx` that shadowed this one with the same expression.
	index_t tx = ix + 1;
	value_t* iptr;
	value_t* optr;

	csp_dio_slots(st, ix, &iptr, &optr);

	if (iptr->t.running) {
	    // running - calculate wait time (take minimum)
	    uvalue_t t0 = csp_dio_slot(st, tx, DIN)->u;
	    uvalue_t period = iptr->t.period;
	    uint32_t dt = (now_ms - t0);
	    uint32_t w = (dt >= period) ? 0 : (period - dt);
	    if (w < wait_ms)
		wait_ms = w;
	}
	else if (iptr->t.val) {
	    // stopped, and a start was requested
	    uvalue_t period = iptr->t.period;

	    iptr->t.running = optr->t.running = 1;
	    iptr->t.fired = optr->t.fired = 0;
	    csp_dio_slots(st, tx, &iptr, &optr);
	    iptr->u = optr->u = now_ms;

	    if (period < wait_ms)
		wait_ms = period;
	}
    }
    csp_ctx_reset(st);
    st->es.wait_ms = wait_ms;
}
