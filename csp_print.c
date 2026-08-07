/*
 * output function, string,  expr etc
 */

#include "csp_print.h"
#include "csp_strings.h"
#include "csp_tok.h"   // the operator table, through its accessors

#define MAX_STRPTRS 64
#define MAX_BODY 16
#define PRINT_STACK 16
#define STRREF(i)  (0x80|(i))
#define IS_REF(x)  ((x) & 0x80)
#define REF_IDX(x) ((x) & 0x7f)

typedef struct {
    uint8_t pos;
    uint8_t buf[MAX_STR_BUF];
    uint8_t nstrptrs;
    uint8_t* strptrs[MAX_STRPTRS];
    uint8_t strlens[MAX_STRPTRS];   // length of strptrs[i]
    bitset_decl(strflags, MAX_STRPTRS);   // RODATA or not
    //
    uint8_t prio[MAX_REGS];
    uint8_t reg[MAX_REGS];    // INDEX!
    uint8_t arg[MAX_ARGS];    // INDEX
    // Raw immediate carried alongside the rendered register/arg. An index passed
    // to a function (timeout(T), changed(X)) reaches us as an LI/LIU immediate,
    // and we need the VALUE back to name the variable. Recovering it by parsing
    // the rendered string cannot work: OP_LI renders signed decimal ("-2035") and
    // OP_LIU renders hex ("0xF80D"), and neither survives a decimal atoi. Keep
    // the number instead of re-reading the text.
    uint16_t regi[MAX_REGS];
    uint16_t argi[MAX_ARGS];
    // body side-effects list
    uint8_t nbody;
    uint8_t body[MAX_BODY];   // strptrs indices
    // Object named by a preceding OP_SETO, consumed by the next variable the
    // walk renders -- the same one-shot the runtime uses. 0 = none, so a
    // CURRENT-relative index lists bare (it is a member of the module body
    // being listed) and only a NAMED object gets its `obj.` prefix back.
    uint8_t seto;
} csp_exprbuf_t;

static void exprbuf_init(csp_exprbuf_t* bp)
{
    bp->pos = 0;
    bp->nstrptrs = 0;
    bp->nbody = 0;
    bp->seto = 0;
}

// return current pointer
static uint8_t* exprbuf_ptr(csp_exprbuf_t* bp)
{
    return bp->buf + bp->pos;
}

// return length given start pointer
static uint8_t exprbuf_len(csp_exprbuf_t* bp, uint8_t* ptr0)
{
    return (bp->buf + bp->pos) - ptr0;
}

#if 0
// pre-allocate len bytes
static uint8_t* exprbuf_alloc(csp_exprbuf_t* bp, int len)
{
    uint8_t* ptr = bp->buf + bp->pos;
    bp->pos += len;
    return ptr;
}
#endif

// print expresssion r must be a index or tagged index
void exprbuf_print(csp_exprbuf_t* bp, unsigned idx)
{
    typedef struct { unsigned char *p; unsigned char *end; } frame_t;
    frame_t stack[PRINT_STACK];
    int top = 0;

    stack[top].p = bp->strptrs[REF_IDX(idx)];
    stack[top].end = stack[top].p + bp->strlens[REF_IDX(idx)];
    top++;

    while (top > 0) {
        frame_t *sp = &stack[--top];
        while (sp->p < sp->end) {
            unsigned char b = *sp->p++;
            if (IS_REF(b)) {
                if (sp->p < sp->end)
                    stack[top++] = *sp;
                stack[top].p   = bp->strptrs[REF_IDX(b)];
                stack[top].end = stack[top].p + bp->strlens[REF_IDX(b)];
                top++;
                goto next_frame;
            }
#ifdef __AVR__
            // pgm_read_byte om romflag satt — hanteras i strentry
#endif
	    csp_print_char(b);
        }
        continue;
next_frame:;
    }
}

// return strptrs index
static uint8_t exprbuf_intern(csp_exprbuf_t* bp, uint8_t* ptr, uint8_t len)
{
    int i;
    int n = bp->nstrptrs;
    for (i = 0; i < n; i++) {
	// fixme check same memspace (AVR)
	if ((bp->strptrs[i] == ptr) && (bp->strlens[i] == len))
	    return i;
	// optimise string extension?
    }
    bp->strptrs[n] = ptr;
    bp->strlens[n] = len;
    bp->nstrptrs++;
    return n;
}

static void exprbuf_char(csp_exprbuf_t* bp, char c)
{
    bp->buf[bp->pos++] = c;
}

static void exprbuf_strref(csp_exprbuf_t* bp, uint8_t ix)
{
    bp->buf[bp->pos++] = STRREF(ix);
}

static void exprbuf_str(csp_exprbuf_t* bp, const char *s)
{
    char c;
    while ((c = *s++)) exprbuf_char(bp, c);
}

// append a nul-terminated RODATA string (operator/keyword names live in flash);
// AVR-PROGMEM-safe. On the host ro_byte==plain so it matches exprbuf_str.
static void exprbuf_rostr(csp_exprbuf_t* bp, rostring_t s)
{
    uint8_t c;
    const uint8_t* sp = (const uint8_t*) s;
    while ((c = ro_byte(sp)) != 0) {
	exprbuf_char(bp, c);
	sp++;
    }
}

// append the length-prefixed string at a logical position (ROM flash or RAM);
// AVR-PROGMEM-safe (reads byte by byte via csp_str_byte)
static void exprbuf_str_at(csp_rt_t* st, csp_exprbuf_t* bp, sindex_t pos)
{
    int len = csp_str_byte(st, pos-1);
    int i;
    for (i = 0; i < len; i++) exprbuf_char(bp, csp_str_byte(st, pos+i));
}

static uint8_t exprbuf_var(csp_rt_t* st, csp_exprbuf_t* bp, uint16_t ix)
{
    uint8_t *start = exprbuf_ptr(bp);
    int m = bp->seto;      // set by the OP_SETO in front of this access
    bp->seto = 0;

    if (m != 0) {
	exprbuf_str_at(st, bp, decl_name_pos(st, csp_object_decl(st, m)));
	exprbuf_char(bp, '.');
    }
    exprbuf_str_at(st, bp, decl_name_pos(st, ix));
    return exprbuf_intern(bp, start, exprbuf_len(bp, start));
}

static void exprbuf_wrap(csp_exprbuf_t* bp, unsigned r, int outer)
{
    if (bp->prio[r] < outer) {
        exprbuf_char(bp, '(');
        exprbuf_strref(bp, bp->reg[r]);
	exprbuf_char(bp, ')');
    }
    else {
        exprbuf_strref(bp, bp->reg[r]);
    }
}

static void exprbuf_uint16(csp_exprbuf_t* bp, uint16_t v)
{
    // build digits least-significant first, then emit reversed. The old
    // if (v>=N) chain dropped interior zeros (100 -> "10", 205 -> "25").
    char tmp[6];   // 65535 == 5 digits
    int n = 0;
    if (v == 0) { exprbuf_char(bp, '0'); return; }
    while (v > 0) { tmp[n++] = (v % 10) + '0'; v /= 10; }
    while (n > 0) exprbuf_char(bp, tmp[--n]);
}

static void exprbuf_int16(csp_exprbuf_t* bp, int16_t v)
{
    if (v < 0) {
	exprbuf_char(bp, '-');
	exprbuf_uint16(bp, -v);
    }
    else {
	exprbuf_uint16(bp, v);
    }
}

// convert string(ref) to integer
static int exprbuf_reftoi(csp_exprbuf_t* bp, uint8_t ref)
{
    uint8_t* ptr = bp->strptrs[REF_IDX(ref)];
    int len = bp->strlens[REF_IDX(ref)];
    int val = 0;
    while(len--)
	val = val*10 + (*ptr++ - '0');
    return val;
}

// true if ref string is a plain decimal number (an immediate)
static int exprbuf_ref_isnum(csp_exprbuf_t* bp, uint8_t ref)
{
    uint8_t* ptr = bp->strptrs[REF_IDX(ref)];
    int len = bp->strlens[REF_IDX(ref)];
    if (len == 0) return 0;
    while(len--) {
	if (*ptr < '0' || *ptr > '9') return 0;
	ptr++;
    }
    return 1;
}

 static char hex(uint8_t v)
{
    if (v < 10) return v+'0';
    return (v-10) + 'a';
}

static void __xuint16(csp_exprbuf_t* bp, uint16_t v)
{
    exprbuf_char(bp, hex((v >> 12)&0xf));
    exprbuf_char(bp, hex((v >> 8)&0xf));
    exprbuf_char(bp, hex((v >> 4)&0xf));
    exprbuf_char(bp, hex((v >> 0)&0xf));
}

static void exprbuf_xuint16(csp_exprbuf_t* bp, uint16_t v)
{
    exprbuf_char(bp, '0');
    exprbuf_char(bp, 'x');
    __xuint16(bp, v);
}

static void exprbuf_xint16(csp_exprbuf_t* bp, int16_t v)
{
    if (v < 0) {
	exprbuf_char(bp, '-');
	v = -v;
    }
    exprbuf_char(bp, '0');
    exprbuf_char(bp, 'x');	
    __xuint16(bp, v);
}

static void exprbuf_alu(csp_exprbuf_t* bp,
			csp_instr_t* ip,
			rostring_t op,
			int arity, int prio)
{
    uint8_t* start = exprbuf_ptr(bp);
    if (arity == 1) {
	exprbuf_rostr(bp, op);
	exprbuf_wrap(bp, ip->a.y, prio);
    }
    else { // assume 2
	// Skip empty operands (from CHG in <- rules)
	int y_empty = (bp->strlens[bp->reg[ip->a.y]] == 0);
	int z_empty = (bp->strlens[bp->reg[ip->a.z]] == 0);
	if (y_empty && z_empty) {
	    bp->reg[ip->a.x] = bp->reg[ip->a.y];  // both empty
	    bp->prio[ip->a.x] = prio;
	    return;
	}
	if (y_empty) {
	    bp->reg[ip->a.x] = bp->reg[ip->a.z];
	    bp->prio[ip->a.x] = bp->prio[ip->a.z];
	    return;
	}
	if (z_empty) {
	    bp->reg[ip->a.x] = bp->reg[ip->a.y];
	    bp->prio[ip->a.x] = bp->prio[ip->a.y];
	    return;
	}
	exprbuf_wrap(bp, ip->a.y, prio);
	exprbuf_rostr(bp, op);
	exprbuf_wrap(bp, ip->a.z, prio);
    }
    bp->reg[ip->a.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    bp->prio[ip->a.x] = prio;
}

static void exprbuf_fcall(csp_rt_t* st,
			  csp_exprbuf_t* bp,
			  csp_instr_t* ip)
{
    int i;
    uint8_t* start;
    const char* fname;
    int fnamelen;
    uint8_t fn;
    uint16_t argtypes;
    int usr = ip->f.usr;
    const csp_func_t* tab = usr ? st->ufuncs : csp_builtin_funcs;
    int rom = usr ? st->ufuncs_rom : BUILTIN_ROM;   // func table in ROM?
    int idx = ip->f.idx;
    int roname;

    // rom-aware field reads (host: ro_*==plain, so rom is a no-op there)
    fname    = rom ? (const char*)ro_ptr(&tab[idx].name) : tab[idx].name;
    fnamelen = rom ? ro_byte(&tab[idx].namelen)          : tab[idx].namelen;
    argtypes = rom ? ro_word(&tab[idx].argtypes)         : tab[idx].argtypes;
    roname   = (rom ? ro_byte(&tab[idx].flags) : tab[idx].flags) & FUNC_RONAME;

    // Copy the name into the buffer and intern THAT, so the interned reference
    // is deref-safe on AVR even when the name string lives in flash.
    {
	uint8_t* ns = exprbuf_ptr(bp);
	for (i = 0; i < fnamelen; i++)
	    exprbuf_char(bp, roname ? ro_byte((const uint8_t*)fname + i)
				    : (uint8_t)fname[i]);
	fn = exprbuf_intern(bp, ns, exprbuf_len(bp, ns));
    }
    start = exprbuf_ptr(bp);   // the fcall expression starts after the name copy

    exprbuf_strref(bp, fn);
    exprbuf_char(bp, '(');
    for (i = 0; i < MAX_ARGS; i++) {
	int a = (ip->f.avt >> i*4) & 0xf;    // actual argument type
	int d = (argtypes >> i*4) & 0xf;     // declared argument type
	if (a == 0) break;
	if (i > 0) exprbuf_char(bp, ',');
	// a string literal arg is an immediate index into the string buffer
	if ((a == V_STRING) && exprbuf_ref_isnum(bp, bp->arg[i])) {
	    uint16_t sx = exprbuf_reftoi(bp, bp->arg[i]);
	    exprbuf_char(bp, '"');
	    exprbuf_str_at(st, bp, sx);
	    exprbuf_char(bp, '"');
	    continue;
	}
	switch(d) {
	case V_INDEX:
	case V_TIMER:
	case V_ANALOG:
	case V_DIGITAL: {
	    // convert argument assumed index integer. exprbuf_var writes the
	    // name inline into the fcall buffer (and interns it); we want the
	    // inline copy only -- an extra strref would render the name twice
	    // (the "T1T1"/"XX" bug), since here `start` precedes the loop.
	    // Take the RAW value (argi), never the rendered text: a CURRENT-
	    // relative index exceeds int16 range, so it renders as "-2035" or
	    // "0xF80D", and parsing either back as decimal gives a bogus decl
	    // index -> SIGSEGV in the name lookup.
	    (void)exprbuf_var(st, bp, bp->argi[i]);
	    break;
	}
	default:
	    exprbuf_strref(bp, bp->arg[i]);
	    break;
	}
    }
    exprbuf_char(bp, ')');
    bp->reg[ip->f.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    // printf("FCALL: R%d = '%s'\n", ip->f.x, bp->reg[ip->f.x]);
    bp->prio[ip->f.x] = 110;
}

// Name of a config part (<var>.<part>), for disassembly only.
static rostring_t part_name(csp_part_t part)
{
    switch (part) {
    case PART_PIN:      return ros_pin;
    case PART_PORT:     return ros_port;
    case PART_DIR:      return ros_dir;
    case PART_PWM:      return ros_pwm;
    case PART_ENDIAN:   return ros_endian;
    case PART_PULLUP:   return ros_pullup;
    case PART_PULLDOWN: return ros_pulldown;
    case PART_PERIOD:   return ros_period;
    case PART_FIRED:    return ros_fired;
    case PART_ID:       return ros_id;
    case PART_RX:       return ros_rx;
    case PART_TX:       return ros_tx;
    case PART_DLC:      return ros_dlc;
    case PART_LEN:      return ros_len;
    case PART_VAL:
    default:            return ros_value;
    }
}

// The per-module (and global) state variable is always the internally-created
// DECL_VARIABLE named "State". State numbers render symbolically only for it,
// so a plain "T==1" or "T=1" is never mistaken for a state.
static int is_state_var(csp_rt_t* st, uint16_t mem)
{
    index_t i = INDEX(mem);
    if (i >= st->ps.nd)
	return 0;
    if (decl(st, i, type) != DECL_VARIABLE)
	return 0;
    return csp_str_eq_ro(st, decl_name_pos(st, mem), ros_State, 5);
}

// True if `snum` is one of the states of the #in block currently being listed --
// used to drop its State==<s> term from a rule's condition (the #in header shows
// the states). See the multi-state block reconstruction in cmd_list.
static int in_list_states(csp_rt_t* st, int snum)
{
    int k;
    for (k = 0; k < st->list_nstate; k++)
	if (st->list_states[k] == snum)
	    return 1;
    return 0;
}

// If mem is the state variable and imm names a declared state, append that
// state's name and return 1; otherwise leave the buffer untouched, return 0.
// Shared by the EQI (State==) and STI (State=) disassembly.
static int exprbuf_state_name(csp_rt_t* st, csp_exprbuf_t* bp,
			      uint16_t mem, int imm)
{
    int s;
    if (!is_state_var(st, mem))
	return 0;
    for (s = 0; s < st->ps.ns; s++) {
	if (st->states[s].snum == imm) {
	    exprbuf_str_at(st, bp, st->states[s].name);
	    return 1;
	}
    }
    return 0;
}

static void exprbuf_store(csp_rt_t* st,
			  csp_exprbuf_t* bp,
			  csp_instr_t* ip, int rimp)
{
    uint8_t* start;
    uint8_t  var;

    var = exprbuf_var(st, bp, ip->m.mem);
    start = exprbuf_ptr(bp);
    exprbuf_strref(bp, var);
    if (rimp) exprbuf_str(bp, "<-");
    else exprbuf_char(bp, '=');
    // A string literal compiles to an OP_LI carrying a POSITION in the string
    // table, and the instruction stream keeps no type -- so on its own the
    // disassembler renders `A = 54`. The destination decl knows better: when it
    // is V_STRING and the source is a literal (prio 110, what OP_LI/OP_LIU leave
    // behind), print the string it points at instead of the index. Without this
    // a listing with a string in it cannot be pasted back.
    if ((decl(st, INDEX(ip->m.mem), vt) == V_STRING) &&
	(bp->prio[ip->m.x] == 110)) {
	exprbuf_char(bp, '"');
	if (bp->regi[ip->m.x] > 0)
	    exprbuf_str_at(st, bp, (sindex_t)bp->regi[ip->m.x]);
	exprbuf_char(bp, '"');
    }
    else
	exprbuf_strref(bp, bp->reg[ip->m.x]);

    bp->reg[ip->m.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    bp->prio[ip->m.x] = 5;
    if (bp->nbody < MAX_BODY)
	bp->body[bp->nbody++] = bp->reg[ip->m.x];
}

// STP: config-part assignment (<var>.<part> = rhs). The part is in ip->m.y.
static void exprbuf_store_part(csp_rt_t* st,
			       csp_exprbuf_t* bp,
			       csp_instr_t* ip)
{
    uint8_t* start;
    uint8_t  var;

    var = exprbuf_var(st, bp, ip->m.mem);
    start = exprbuf_ptr(bp);
    exprbuf_strref(bp, var);
    exprbuf_char(bp, '.');
    exprbuf_rostr(bp, part_name((csp_part_t)ip->m.y));
    exprbuf_char(bp, '=');
    // A direction is written `out` in the source, so list it that way instead of
    // the 2 it compiles to -- otherwise the line reads like a magic number and
    // says nothing about which way the pin turned. Only for a LITERAL right-hand
    // side (prio 110 is what OP_LI/OP_LIU leave behind); an expression that
    // computes a direction has no name to print and stays as written.
    if (((csp_part_t)ip->m.y == PART_DIR) && (bp->prio[ip->m.x] == 110))
	exprbuf_rostr(bp, csp_fmt_pindir((uint8_t)bp->regi[ip->m.x]));
    else
	exprbuf_strref(bp, bp->reg[ip->m.x]);

    bp->reg[ip->m.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    bp->prio[ip->m.x] = 5;
    if (bp->nbody < MAX_BODY)
	bp->body[bp->nbody++] = bp->reg[ip->m.x];
}

// STI: store immediate to memory (<var> = imm). The immediate is in ip->mi.imm;
// ip->mi.x is the (runtime-dead) register the rule's NEXT points at, so the
// interned string is recorded there to render the rule body cleanly.
static void exprbuf_store_imm(csp_rt_t* st,
			      csp_exprbuf_t* bp,
			      csp_instr_t* ip)
{
    uint8_t* start;
    uint8_t  var;

    var = exprbuf_var(st, bp, ip->mi.mem);
    start = exprbuf_ptr(bp);
    exprbuf_strref(bp, var);
    exprbuf_char(bp, '=');
    if (!exprbuf_state_name(st, bp, ip->mi.mem, ip->mi.imm))
	exprbuf_int16(bp, ip->mi.imm);

    bp->reg[ip->mi.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    bp->prio[ip->mi.x] = 5;
    if (bp->nbody < MAX_BODY)
	bp->body[bp->nbody++] = bp->reg[ip->mi.x];
}

static void exprbuf_ld(csp_rt_t* st,
		       csp_exprbuf_t* bp,
		       csp_instr_t* ip)
{
    uint8_t  var = exprbuf_var(st, bp, ip->m.mem);
    bp->reg[ip->m.x] = var;
    bp->prio[ip->m.x] = 110;
}

// LDP: config-part read (<var>.<part>). The part is in ip->m.y.
static void exprbuf_ld_part(csp_rt_t* st,
			    csp_exprbuf_t* bp,
			    csp_instr_t* ip)
{
    uint8_t* start;
    uint8_t  var = exprbuf_var(st, bp, ip->m.mem);

    start = exprbuf_ptr(bp);
    exprbuf_strref(bp, var);
    exprbuf_char(bp, '.');
    exprbuf_rostr(bp, part_name((csp_part_t)ip->m.y));
    bp->reg[ip->m.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    bp->prio[ip->m.x] = 110;
}

// exprbuf contains rule condition
static void exprbuf_rule(csp_rt_t* st, csp_exprbuf_t* bp, csp_instr_t* ip)
{
    if (csp_will_output())
	exprbuf_print(bp, bp->reg[ip->r.cnd]);
}

// exprbuf contains rule body - print side-effects then final expression
static void exprbuf_body(csp_rt_t* st, csp_exprbuf_t* bp, csp_instr_t* ip)
{
    int i;
    for (i = 0; i < bp->nbody; i++) {
	if (i > 0) csp_print_char(',');
	exprbuf_print(bp, bp->body[i]);
    }
    // add final expression if different from last body element
    if (bp->nbody == 0 || bp->body[bp->nbody-1] != bp->reg[ip->x.x]) {
	if (bp->nbody > 0) csp_print_char(',');
	exprbuf_print(bp, bp->reg[ip->x.x]);
    }
}

// Is register reg (produced at instr i) read by a later instruction
// before being redefined or the rule ends? Used to tell a void
// statement-call (print) from a call whose result is consumed (x=f()).
static int reg_consumed(csp_rt_t* st, int i, int reg)
{
    int j;
    tok_t t;

    for (j = i+1; j < st->ps.nn; j++) {
	csp_instr_t ipv = csp_get_instr(st, j);
	csp_instr_t* ip = &ipv;
	switch (ip->op) {
	case OP_NEXT:
	case OP_RULE:
	    return 0;  // rule boundary, never read
	case OP_ST:
	case OP_STP:
	case OP_STIMP:
	case OP_CHG:
	    if (ip->m.x == reg) return 1;
	    break;
	case OP_ARG:
	    if (ip->i.x == reg) return 1;
	    break;
	case OP_LD:
	case OP_LDP:
	    if (ip->m.x == reg) return 0;  // redefined
	    break;
//	case OP_EQI:
//	    if (ip->mi.x == reg) return 0; // redefined
//	    break;
	case OP_LI:
	case OP_LIU:
	case OP_LIH:
	    if (ip->i.x == reg) return 0;  // redefined
	    break;
	case OP_CALL:
	    if (ip->f.x == reg) return 0;  // redefined
	    break;
	case OP_MOV:
	case OP_CVTIF:
	case OP_CVTFI:
	    if (ip->a.y == reg) return 1;
	    if (ip->a.x == reg) return 0;  // redefined
	    break;
	default:
	    t = ro_byte(&op_info[ip->op].tok);
	    if (op_table_arity(t) >= 0) {
		if (ip->a.y == reg || ip->a.z == reg) return 1;
		if (ip->a.x == reg) return 0;  // redefined
	    }
	    break;
	}
    }
    return 0;
}

//
// trace instructions util
// 1. OP_RULE  then we have a condition expression
// 2. OP_NEXT  then we have a body expression
//
static int exprbuf_expr(csp_rt_t* st, csp_exprbuf_t* bp, int i)
{
    while(i < st->ps.nn) {
	tok_t t;
	csp_instr_t ipv = csp_get_instr(st, i);
	csp_instr_t* ip = &ipv;
	switch(ip->op) {
	case OP_SETO:
	    // Names the object for the NEXT variable rendered. Mirrors the
	    // runtime's one-shot exactly, so the listing cannot disagree with
	    // what the instruction stream actually does.
	    bp->seto = (uint8_t)ip->o.obj;
	    break;
	case OP_RULE:
	    exprbuf_rule(st, bp, ip);
	    return i+1;
	case OP_NEXT:
	    exprbuf_body(st, bp, ip);
	    return i+1;
        case OP_ST:  // assignment in body
	    exprbuf_store(st, bp, ip, 0);
	    break;
	case OP_STP:  // config-part assignment (<var>.<part> = rhs)
	    exprbuf_store_part(st, bp, ip);
	    break;
	case OP_STI:  // store immediate (<var> = imm, mirror of EQI)
	    exprbuf_store_imm(st, bp, ip);
	    break;
	case OP_STIMP:  // reactive assignment (<-)
	    exprbuf_store(st, bp, ip, 1);
	    break;
	case OP_CHG:
	    // Mark register as "reactive condition" - empty string for AND to skip
	    bp->reg[ip->m.x] = exprbuf_intern(bp, (uint8_t*)"", 0);
	    bp->prio[ip->m.x] = 110;
	    break;
	case OP_ARG:
	    bp->arg[ip->i.imm] = bp->reg[ip->i.x];
	    bp->argi[ip->i.imm] = bp->regi[ip->i.x];
	    break;
	case OP_CALL:
	    exprbuf_fcall(st, bp, ip);
	    // A call result that is not consumed later is a void
	    // statement (e.g. print) and belongs in the body list.
	    if (!reg_consumed(st, i, ip->f.x)) {
		if (bp->nbody < MAX_BODY)
		    bp->body[bp->nbody++] = bp->reg[ip->f.x];
	    }
	    break;
        case OP_LD:
	    exprbuf_ld(st, bp, ip);
	    break;
	case OP_LDP:  // config-part read (<var>.<part>)
	    exprbuf_ld_part(st, bp, ip);
	    break;
        case OP_LIU: {
            uint8_t *start = exprbuf_ptr(bp);
            exprbuf_xuint16(bp, (uint16_t)ip->i.imm);
	    bp->reg[ip->i.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
	    bp->regi[ip->i.x] = (uint16_t)ip->i.imm;   // keep the number, not the text
            bp->prio[ip->i.x] = 110;
            break;
	}
	    /*
	case OP_EQI: {
	    uint8_t *start = exprbuf_ptr(bp);
	    // Inside a listed #in <S> block, the per-rule State==S gate is implied
	    // by the block header -- render it empty so AND drops it from the cond.
    // A bare NORMAL+ rule (list_implicit) is guarded by the injected
	    // State==INIT||State==NORMAL; drop both terms so it lists bare (the OR
	    // of two empty operands collapses to empty in exprbuf_alu). Inside a
	    // multi-state `#in A B C` block, drop every State==<listed> term the
	    // same way, so the block's OR-guard vanishes under the #in header.
	    if (is_state_var(st, ip->mi.mem) &&
		(((st->list_state >= 0) && (ip->mi.imm == st->list_state)) ||
		 in_list_states(st, ip->mi.imm) ||
		 (st->list_implicit && ((ip->mi.imm == STATE_INIT) ||
					(ip->mi.imm == STATE_NORMAL))))) {
		bp->reg[ip->a.x] = exprbuf_intern(bp, (uint8_t*)"", 0);
		bp->prio[ip->a.x] = 110;
		break;
	    }
	    exprbuf_var(st, bp, ip->mi.mem);
	    exprbuf_str(bp, "==");
	    if (!exprbuf_state_name(st, bp, ip->mi.mem, ip->mi.imm))
		exprbuf_int16(bp, ip->mi.imm);
	    bp->reg[ip->a.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
	    bp->prio[ip->a.x] = 60;
	    break;
	}
	    */
        case OP_LIH: {
            uint8_t *start = exprbuf_ptr(bp);
	    uint8_t ih;
	    
            exprbuf_xint16(bp, ip->i.imm);
	    ih = exprbuf_intern(bp,start,exprbuf_len(bp, start));

            start = exprbuf_ptr(bp);
	    exprbuf_strref(bp, ih);
	    exprbuf_char(bp, '.');	    
	    exprbuf_strref(bp, bp->reg[ip->i.x]);  // from LIU
	    
	    bp->reg[ip->i.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
            bp->prio[ip->i.x] = 110;
            break;
	}
        case OP_LI: {
            uint8_t *start = exprbuf_ptr(bp);
            exprbuf_int16(bp, ip->i.imm);
	    bp->reg[ip->i.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
	    bp->regi[ip->i.x] = (uint16_t)ip->i.imm;   // keep the number, not the text
            bp->prio[ip->i.x] = 110;
            break;
	}
	case OP_MOV:
	case OP_CVTIF:   // type conversion is implicit in display
	case OP_CVTFI: {
	    bp->reg[ip->a.x] = bp->reg[ip->a.y];
	    bp->prio[ip->a.x] = bp->prio[ip->a.y];
	    break;
	}
	default:
	    t = ro_byte(&op_info[ip->op].tok);
	    if (op_table_arity(t) >= 0) {
		exprbuf_alu(bp, ip, op_table_name(t),
			    op_table_arity(t), op_table_prec(t));
	    }
	    break;
	}
	i++;
    }
    return i;
}

// ci:
//    Condition
// ri: OP_RULE: if !ri then NEXT
//
// ni: OP_NEXT:
//
int csp_print_rule(csp_rt_t* st, int i)
{
    int Lc = i;
    // STATIC, not a local. This is ~420 bytes on AVR (buf[MAX_STR_BUF] plus 64
    // string pointers and their lengths), and the stack it sat on grows DOWN
    // from RAMEND toward the arena -- whose top is where RAM declarations live,
    // because they grow down from there. On a mega the gap measured 626 bytes
    // while /list alone needed this 423 plus two nested token_t tv[24]. The
    // stack reached the arena and the newest declarations were overwritten;
    // what came back was a ro_decl() stack temporary (a verbatim ROM record).
    // As .bss it is accounted for by freeRam() at boot, so the arena simply
    // claims that much less -- the same RAM, but declared instead of a hidden
    // spike. csp_print_rule is called only from cmd_list and never recurses.
    static csp_exprbuf_t buf;

    while(i < st->ps.nn) {
	csp_instr_t ipv = csp_get_instr(st, i);   // ROM or RAM instr, by value
	if (ipv.op == OP_RULE) {
	    csp_instr_t* ip = &ipv;
	    void* savef;
	    exprbuf_init(&buf);
	    exprbuf_expr(st, &buf, i+1);   // print body
	    // Build condition in buffer, check if non-empty. A bare NORMAL+ rule
	    // carries an implicit State==INIT||State==NORMAL guard the user never
	    // wrote -- suppress it (see OP_EQI below) so the rule lists back bare.
	    st->list_implicit = ip->r.implicit;
	    exprbuf_init(&buf);
	    savef = csp_set_file_output(NULL);
	    exprbuf_expr(st, &buf, Lc); // build but don't print
	    csp_set_file_output(savef);
	    st->list_implicit = 0;
	    switch(buf.strlens[buf.reg[ip->r.cnd]]) {
	    case 0:
		break;
	    case 2:
		if ((buf.buf[buf.reg[ip->r.cnd]] == '-') &&
		    (buf.buf[buf.reg[ip->r.cnd]+1] == '1'))
		    break;		
	    default:
		csp_print_lit(" ? ");
		exprbuf_print(&buf, buf.reg[ip->r.cnd]);
	    }
	    // NO newline here: the caller closes the line, so /list can put its
	    // R/F tag there as a trailing comment instead of a leading column.
	    return i + instr(st,i,r.nxt) + 1;
	}
	i++;
    }
    return i;
}


// --- numbers and columns ---------------------------------------------------
// Numbers are formatted HERE, on top of csp_print_char, rather than once per
// platform. Two reasons.
//
// The length is exact whether or not output is enabled. A caller padding a
// column needs the width either way, and the per-platform versions returned a
// hard-coded 1 when output was off -- so /state silently lost its alignment
// whenever printing was suppressed.
//
// And the two platforms cannot drift. Serial.print(v, HEX) renders uppercase
// where the host's "0x%x" renders lowercase; that difference was invisible
// until you diffed a board against a host run.
int csp_print_uint(uvalue_t v)
{
    char b[10];                    // 2^32-1 is 10 digits
    int n = 0, i;
    do {
	b[n++] = (char)('0' + (v % 10));
	v /= 10;
    } while (v);
    for (i = n; i > 0; i--)
	csp_print_char(b[i-1]);
    return n;
}

int csp_print_int(ivalue_t v)
{
    if (v < 0) {
	csp_print_char('-');
	// via uvalue_t so the most negative value negates without overflowing
	return 1 + csp_print_uint((uvalue_t)0 - (uvalue_t)v);
    }
    return csp_print_uint((uvalue_t)v);
}

static rochar hex_digits[] RODATA = "0123456789abcdef";

static char hex_digit(uint8_t v)
{
    return (char)ro_byte((rochar*)hex_digits + (v & 0xf));
}

int csp_print_hex(uvalue_t v)
{
    char b[8];
    int n = 0, i;
    csp_print_lit("0x");
    do {
	b[n++] = hex_digit((uint8_t)v);
	v >>= 4;
    } while (v);
    for (i = n; i > 0; i--)
	csp_print_char(b[i-1]);
    return n + 2;
}

// One byte as exactly two digits, no 0x. For dumping raw bytes (a #buffer's
// frame) where the columns have to line up and a leading 0 must not vanish.
int csp_print_hex2(uint8_t v)
{
    csp_print_char(hex_digit(v >> 4));
    csp_print_char(hex_digit(v));
    return 2;
}

// Print s padded to `w` columns. Returns the number of characters written.
//
// A string LONGER than w is never truncated -- the column widens instead. That
// keeps a long name readable at the cost of one ragged row; losing characters
// silently is the worse failure for something you are reading to debug.
//
// LJUST needs no strlen: csp_print_str already returns what it wrote. RJUST and
// CJUST have to measure first, so they pay for it.
static int just_pad(int n)          // n spaces, n <= 0 prints nothing
{
    int i;
    for (i = 0; i < n; i++)
	csp_print_blank();
    return (n > 0) ? n : 0;
}

// How much padding goes before the text for a given justification.
static int just_lead(just_t j, int len, int w)
{
    int lead = (w > len) ? w - len : 0;
    switch (j) {
    case RJUST: return lead;
    case CJUST: return lead / 2;
    default:    return 0;           // LJUST, NJUST
    }
}

int csp_print_just(const char* s, just_t j, int w)
{
    int len, lead;

    if (s == NULL) s = "";
    if (j == NJUST)
	return csp_print_str(s);
    if (j == LJUST) {               // no strlen: csp_print_str reports what it wrote
	len = csp_print_str(s);
	return len + just_pad(w - len);
    }
    for (len = 0; s[len]; len++)
	;
    lead = just_lead(j, len, w);
    just_pad(lead);
    csp_print_str(s);
    return lead + len + just_pad(w - len - lead);
}

// Same, for a string in FLASH. Both the print and the length walk go through
// ro_byte -- handing one of these to csp_print_just reads the wrong address
// space on AVR, and the const char* parameter hides that from the compiler.
int csp_print_rojust(rostring_t s, just_t j, int w)
{
    int len, lead;

    if (s == NULL)
	return (j == NJUST) ? 0 : just_pad(w);
    if (j == NJUST)
	return csp_print_rostr(s);
    if (j == LJUST) {
	len = csp_print_rostr(s);
	return len + just_pad(w - len);
    }
    len = ro_strlen(s);
    lead = just_lead(j, len, w);
    just_pad(lead);
    csp_print_rostr(s);
    return lead + len + just_pad(w - len - lead);
}

#if !FVALUE_IS_FIXPOINT
// Fixed-point rendering of a non-negative float below 1e9: integer part, '.',
// six decimals, rounded half-up -- the shape "%f" produces. Split out so the
// scientific branch below can reuse it for its mantissa.
static int print_fixed6(fvalue_t v)
{
    uint32_t ip = (uint32_t)v;
    uint32_t fp = (uint32_t)((v - (fvalue_t)ip) * (fvalue_t)1000000 + (fvalue_t)0.5);
    int n;

    if (fp >= 1000000) {           // the rounding carried into the integer part
	fp -= 1000000;
	ip++;
    }
    n = csp_print_uint(ip);
    csp_print_char('.');
    return n + 1 + csp_print_uintw(fp, 100000);
}
#endif

// Floats are formatted here for the same reason integers are: the host printed
// "%f" (six decimals) while Serial.print(v) printed two, so a board and a host
// disagreed on the same value. And avr-libc's printf has no %f at all, which is
// why the board could not simply use the host's route.
//
// USE_FIXPOINT is not set by any Makefile, so this -- not csp_print_fixpoint --
// is the live path.
int csp_print_float(fvalue_t v)
{
#if FVALUE_IS_FIXPOINT
    return csp_print_fixpoint(v);
#else
    int n = 0;

    if (v != v) {                            // NaN is the only value != itself
	csp_print_lit("nan");
	return 3;
    }
    if (v < (fvalue_t)0) {
	csp_print_char('-');
	v = -v;
	n = 1;
    }
    if (v * (fvalue_t)0 != (fvalue_t)0) {    // finite*0 is 0; inf*0 is NaN
	csp_print_lit("inf");
	return n + 3;
    }
    if (v >= (fvalue_t)1e9) {
	// Too large for a uint32 integer part -- and past 2^24 a float cannot
	// represent integers exactly anyway, so six decimals would be theatre.
	// Print it as d.dddddd e+NN rather than a clamped, wrong number.
	int e = 0;
	while (v >= (fvalue_t)10) { v /= (fvalue_t)10; e++; }
	n += print_fixed6(v);
	csp_print_char('e');
	csp_print_char('+');
	return n + 2 + csp_print_uint((uvalue_t)e);
    }
    return n + print_fixed6(v);
#endif
}

// End of line. Shared, and the ONLY place the runtime should spell a newline
// out -- csp_print_char('\n') says "write this byte" where the caller means
// "end this line", and the two used to drift (Serial.println() wrote "\r\n"
// while a bare '\n' wrote LF, so a board mixed both conventions).
//
// What reaches the wire is the platform's business: csp_print_char prepends the
// CR on Arduino and passes '\n' through on the host. Returns 1 either way --
// logical characters, so column arithmetic matches across platforms.
int csp_println(void)
{
    return csp_print_char('\n');
}

int csp_print_blank(void)
{
    return csp_print_char(' ');
}
