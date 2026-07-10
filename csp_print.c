/*
 * output function, string,  expr etc
 */

#include "csp_print.h"

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
    // body side-effects list
    uint8_t nbody;
    uint8_t body[MAX_BODY];   // strptrs indices
} csp_exprbuf_t;

static void exprbuf_init(csp_exprbuf_t* bp)
{
    bp->pos = 0;
    bp->nstrptrs = 0;
    bp->nbody = 0;
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
static void exprbuf_ro_str(csp_exprbuf_t* bp, const char *s)
{
    uint8_t c;
    while ((c = ro_byte((const uint8_t*)s)) != 0) { exprbuf_char(bp, c); s++; }
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
    int m = OBJ(ix);

    if ((m != GLOBAL) && (m != CURRENT)) {
	exprbuf_str_at(st, bp, decl_name_pos(st, st->object[m]));
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
			const char *op,
			int arity, int prio)
{
    uint8_t* start = exprbuf_ptr(bp);
    if (arity == 1) {
	exprbuf_ro_str(bp, op);
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
	exprbuf_ro_str(bp, op);
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
	    // convert argument assumed index integer
	    uint16_t imm = exprbuf_reftoi(bp, bp->arg[i]);
	    uint8_t var = exprbuf_var(st, bp, imm);
	    exprbuf_strref(bp, var);
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
    exprbuf_strref(bp, bp->reg[ip->m.x]);

    bp->reg[ip->m.x] = exprbuf_intern(bp, start, exprbuf_len(bp, start));
    bp->prio[ip->m.x] = 5;
    if (bp->nbody < MAX_BODY)
	bp->body[bp->nbody++] = bp->reg[ip->m.x];
}

static void exprbuf_ld(csp_rt_t* st,
		       csp_exprbuf_t* bp,
		       csp_instr_t* ip)
{
    uint8_t  var = exprbuf_var(st, bp, ip->m.mem);
    bp->reg[ip->m.x] = var;
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
	case OP_EQI:
	    if (ip->mi.x == reg) return 0; // redefined
	    break;
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
	    if ((int8_t)ro_byte(&tok_table[t].arity) >= 0) {
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
	case OP_RULE:
	    exprbuf_rule(st, bp, ip);
	    return i+1;
	case OP_NEXT:
	    exprbuf_body(st, bp, ip);
	    return i+1;
        case OP_ST:  // assignment in body
	    exprbuf_store(st, bp, ip, 0);
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
	case OP_LDP:  // best effort: renders as the plain variable (no .part yet)
	    exprbuf_ld(st, bp, ip);
	    break;
        case OP_LIU: {
            uint8_t *start = exprbuf_ptr(bp);
            exprbuf_xuint16(bp, (uint16_t)ip->i.imm);
	    bp->reg[ip->i.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
            bp->prio[ip->i.x] = 110;
            break;
	}
	case OP_EQI: {
	    uint8_t *start = exprbuf_ptr(bp);
	    int s;
	    exprbuf_var(st, bp, ip->mi.mem);
	    exprbuf_str(bp, "==");
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
	    for (s = 0; s < st->ps.ns; s++) {
		if (st->states[s].snum == ip->mi.imm) {
		    exprbuf_str_at(st, bp, st->states[s].name);
		    goto named;
		}
	    }
#endif
            exprbuf_int16(bp, ip->mi.imm);
	    named:
	    bp->reg[ip->a.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
	    bp->prio[ip->a.x] = 60;
	    break;
	}
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
	    if ((int8_t)ro_byte(&tok_table[t].arity) >= 0) {
		exprbuf_alu(bp, ip, (const char*)ro_ptr(&tok_table[t].name),
			    (int8_t)ro_byte(&tok_table[t].arity),
			    (int8_t)ro_byte(&tok_table[t].prec));
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
    csp_exprbuf_t buf;

    while(i < st->ps.nn) {
	csp_instr_t ipv = csp_get_instr(st, i);   // ROM or RAM instr, by value
	if (ipv.op == OP_RULE) {
	    csp_instr_t* ip = &ipv;
	    void* savef;
	    exprbuf_init(&buf);
	    exprbuf_expr(st, &buf, i+1);   // print body
	    // Build condition in buffer, check if non-empty
	    exprbuf_init(&buf);
	    savef = csp_set_file_output(NULL);
	    exprbuf_expr(st, &buf, Lc); // build but don't print
	    csp_set_file_output(savef);
	    switch(buf.strlens[buf.reg[ip->r.cnd]]) {
	    case 0:
		break;
	    case 2:
		if ((buf.buf[buf.reg[ip->r.cnd]] == '-') &&
		    (buf.buf[buf.reg[ip->r.cnd]+1] == '1'))
		    break;		
	    default:
		csp_print_str(" ? ");
		exprbuf_print(&buf, buf.reg[ip->r.cnd]);
	    }
	    csp_print_char('\n');
	    return i + instr(st,i,r.nxt) + 1;
	}
	i++;
    }
    return i;
}

