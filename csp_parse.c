#include "csp_parse.h"
// This is the one translation unit that DEFINES the pattern bytes: pmatch owns
// them, and putting them here is what lets the offset table be RODATA too.
#define CSP_PATTERN_DEFINE
#include "csp_patterns.h"
#include <string.h>

// The pmatch engine is part of the compiler: it exists to turn a token stream
// into declarations. An exec-only build has no compiler, so this whole
// translation unit goes with it -- same reasoning and same shape as
// csp_compile.c.
#if !defined(CSP_EXEC_ONLY)

// Stop-set storage. GENERATED (csp_stop_sets.h) rather than built at boot:
// collect_first() and scan_pattern_() computed something that cannot change,
// every time the firmware came up, into 230 bytes of RAM. Now RODATA, and those
// two functions plus add_stop_tok and init_stop_sets are gone with them.
//
// The budget check went with them too -- and had to go somewhere: the generator
// fails the build if the sets outgrow MAX_STOP_TOKENS, which is a better place
// to find out than a boot counter nobody reads.
static const uint8_t stop_toks[] RODATA = { CSP_STOP_TOKS };
static const uint8_t stop_pos[NUM_STOP_SETS] RODATA = { CSP_STOP_POS };

int csp_stop_tokens_used(void) { return (int) sizeof(stop_toks); }

#ifdef DEBUG

#define STRCASE(id) case id: return #id

// The names come from csp_pattern_ids.h, generated with the enum itself. As a
// hand-written switch this only compiled under DEBUG, so it kept the old set
// names through a rename and said nothing until somebody built with -DDEBUG.
static const char* const stop_set_names[] = { CSP_STOP_SET_NAMES };

static char* stop_set_name(uint8_t sid)
{
    if (sid >= NUM_STOP_SETS)
	return "???";
    return (char*) stop_set_names[sid];
}

#if 0
static char* dtok_name(uint8_t dtok)
{
    switch(dtok) {
    STRCASE(D_NONE);
    STRCASE(D_MODULE);
    STRCASE(D_END);
    STRCASE(D_CONSTANT);
    STRCASE(D_VARIABLE);
    STRCASE(D_DIGITAL);
    STRCASE(D_ANALOG);
    STRCASE(D_TIMER);
    // STRCASE(D_CAN);
    STRCASE(D_UART);
    STRCASE(D_SOCKET);
    STRCASE(D_MOD);
    STRCASE(D_STATES);
    STRCASE(D_IN);    
    default: return "???";    
    }
}
#endif

// Indexed by tok_t, from csp_tokens.h -- generated with the enum, so a new
// token cannot go unnamed here the way RBRACE did (it printed as "???").
static const char* const tok_names[] = { CSP_TOKEN_NAMES };

static char* tok_name(uint8_t tok)
{
    if (tok > T_LAST)
	return "???";
    return (char*) tok_names[tok];
}

static void print_stop(FILE* f, uint8_t stop)
{
    if (stop & TOK_SET) {
	fprintf(f, "{%s},", stop_set_name(stop & TOK_SET_MASK));
    }
    else {
	fprintf(f, "%s,", tok_name(stop));
    }
}

#endif

// Check if token is in stop-set
int stop_set_has(int set_idx, uint8_t tok)
{
    int i;
    if ((set_idx < 0) || (set_idx >= NUM_STOP_SETS))
	return 0;
    i = ro_byte(&stop_pos[set_idx]);
    while (i < (int)sizeof(stop_toks)) {
	uint8_t t = ro_byte(&stop_toks[i]);
	if (t == NONE)
	    break;
	if (t & TOK_SET) { // t is a token set
	    if (stop_set_has((t & TOK_SET_MASK), tok))
		return 1;
	}
	else if (t == tok)
	    return 1;
        i++;
    }
    return 0;
}

// Add token to current set (avoid duplicates)
#ifdef DEBUG
void dump_stop_sets(void)
{
    int i;
    FILE* f = stdout;
    fprintf(f, "stop_toks[%d] = {\n", (int)sizeof(stop_toks));
    for (i = 0; i < (int)sizeof(stop_toks); i++) {
	uint8_t stop = stop_toks[i];
	print_stop(f, stop);
	if (i && !(i & 0x7)) fprintf(f, "\n");
    }
    fprintf(f, "};\n");

    fprintf(f, "stop_pos[%d] = {\n", NUM_STOP_SETS);
    for (i = 0; i < NUM_STOP_SETS; i++) {
	fprintf(f, "%d,", stop_pos[i]);
	if (i && !(i & 0x7)) fprintf(f, "\n");	
    }
    fprintf(f, "};\n");

    for (i = 0; i < NUM_STOP_SETS; i++) {
	int pos = stop_pos[i];
	int j = 0;
	fprintf(f, "%s = {", stop_set_name(i));
	while(stop_toks[pos] != NONE) {
	    print_stop(f, stop_toks[pos]);
	    if (j && !(j & 0x7)) fprintf(f, "\n");
	    pos++;
	}
	fprintf(f, "}\n");
    }
}
#endif

#define MAX_CONT_DEPTH 8

// One pattern byte. The patterns are RODATA, which on AVR is PROGMEM -- reading
// pat[pi] directly there reads DATA space at the table's flash address, which
// on a mega lands inside CandySpeak's own arena. Same discipline as tok_table;
// see the note at the top of csp_tok.c. `&pat[pi]` stays a plain address: it is
// passed on to a recursive match, never dereferenced here.
#define PB(i) ro_byte(&pat[(i)])

typedef struct {
    csp_rt_t* st;
    void* data;
    const uint8_t* dend;  // one past the end of the data struct (bounds backtracking)
    uint8_t ez;   // element size (set by P_ARRAY) (max element size=255!)
    int ix;       // one-level repetition arrary index
    int eo;       // element offset
    uint8_t cont_stack[MAX_CONT_DEPTH];  // continuation stop-set stack
    int cont_sp;  // continuation stack pointer
} pmatch_st_t;

// Check if token matches stop-set or any continuation sets
static int stop_match(pmatch_st_t* pst, int sid, uint8_t tok)
{
    int i;

    if (stop_set_has(sid, tok))
        return 1;
    for (i = 0; i < pst->cont_sp; i++) {
        if (stop_set_has(pst->cont_stack[i], tok))
            return 1;
    }
    return 0;
}


// how to do this be made with pattern?
NOINLINE decl_opts_t parse_opts(csp_rt_t* st, const token_t* tv,
				int* ip, size_t n,
				decl_opts_t opts)
{
    int i = *ip;

    while(i < (int)n) {
	switch(tv[i].t) {
	case T_UNSIGNED: opts.vt=V_UNSIGNED; DBG("UNSIGNED,"); break;
	case T_INTEGER:  opts.vt=V_INTEGER; DBG("INTEGER,"); break;
	case T_FLOAT:    opts.vt=V_FLOAT; DBG("FLOAT,"); break;
	case T_STRING:   opts.vt=V_STRING; DBG("STRING,"); break;	    
	case T_PWM:      opts.pwm = 1; DBG("T_PWM,"); break;
	case T_IN:       opts.dir |= DIR_IN; DBG("IN,"); break;
	case T_OUT:      opts.dir |= DIR_OUT; DBG("OUT,"); break;
	case T_INOUT:    opts.dir |= DIR_INOUT; DBG("INOUT,"); break;
	case T_NATIVE:   opts.endian=E_NATIVE; DBG("NATIVE,"); break;	    
	case T_LITTLE:   opts.endian=E_LITTLE; DBG("LITTLE,"); break;
	case T_BIG:      opts.endian=E_BIG; DBG("BIG,"); break;	    
	case T_PULLUP:   opts.pullup=1; DBG("PULLUP,"); break;
	case T_PULLDOWN: opts.pulldown=1; DBG("PULLDOWN,"); break;
	default: goto done;
	}
	i++;
    }
done:
    if (i != *ip) {
	*ip= i;	
	DBG("\n");
    }
    return opts;
}

// Store value at offset in data struct
static inline void store_val(void* data, int off, value_t val)
{
    *((value_t*)((uint8_t*)data + off)) = val;
}

// Store integer value at offset in data struct
static inline void store_int(void* data, int off, ivalue_t val)
{
    *((ivalue_t*)((uint8_t*)data + off)) = val;
}

static inline void store_str(void* data, int off, const tstr_t* str)
{
    *((tstr_t*)((uint8_t*)data + off)) = *str;
}

static inline void store_expr(void* data, int off, pexpr_t expr)
{
    *((pexpr_t*)((uint8_t*)data + off)) = expr;
}

static inline void store_opts(void* data, int off, decl_opts_t opts)
{
    *((decl_opts_t*)((uint8_t*)data + off)) = opts;
}

// fetch integer value at offset in data struct
static inline ivalue_t fetch_int(void* data, int off)
{
    return *((ivalue_t*)((uint8_t*)data + off));
}

static inline decl_opts_t fetch_opts(void* data, int off)
{
    return *((decl_opts_t*)((uint8_t*)data + off));
}

// Find expression boundary: first stop token at paren depth 0
static int scan_expr_end(pmatch_st_t* pst, const token_t* tv, int ti,
			 size_t n, int sid)
{
    int k = ti;
    int depth = 0;

    while (k < (int)n) {
	uint8_t t = tv[k].t;
	if ((depth == 0) && stop_match(pst, sid, t))
	    break;
	if (t == LP) depth++;
	else if ((t == RP) && (depth > 0)) depth--;
	k++;
    }
    return k;
}

// Expression with stop-set
NOINLINE static int pmatch_expr_s(pmatch_st_t* pst, const token_t* tv, int ti,
				  size_t n, uint8_t off, int sid)
{
    int k;
    size_t num;
    pexpr_t range;
    rentry_t result;

    // Find expression boundary using stop-set + continuation sets
    k = scan_expr_end(pst, tv, ti, n, sid);
    num = (k > ti) ? k - ti : 1;
    if (!csp_parse_const_expr(pst->st, &tv[ti], &num, &result))
	return -1;
    DBG("expr_s: (%d), num=%ld, set=%d\n", ti, num, sid);
    range.pos = ti;
    range.len = num;
    store_expr(pst->data, pst->eo+off, range);
    return ti + num;
}

// match type vt constant
static int pmatch_const_s(pmatch_st_t* pst, const token_t* tv, int ti,
			  size_t n, uint8_t vt, uint8_t off, int sid)
{
    int k;
    size_t num;
    rentry_t result;

    // Find expression boundary using stop-set + continuation sets
    k = scan_expr_end(pst, tv, ti, n, sid);
    num = (k > ti) ? k - ti : 1;

    if (!csp_parse_const_expr(pst->st, &tv[ti], &num, &result))
	return -1;
    if (!result.I)
	return -1;  // not constant
    switch(vt) {
    case V_UNSIGNED:
    case V_INTEGER:
	if (result.vt == V_INTEGER) break;
	if (result.vt == V_UNSIGNED) break;
	if (result.vt == V_FLOAT) {
	    result.val.i = op_CVTFI(result.val.f);
	    break;
	}
	return -1;
    case V_FLOAT:
	if (result.vt == V_FLOAT) break;
	if (result.vt == V_INTEGER) {
	    result.val.f = op_CVTIF(result.val.i);
	    break;
	}
	return -1;
    case V_STRING:
	if (result.vt == V_STRING) break;
	return -1;
    default:
	return -1;
    }
    store_val(pst->data, pst->eo+off, result.val);
    return ti + num;
}

#ifdef DEBUG
#define SP ' '
#define NUL '\0'
#define NSPACES 10
static char spaces[NSPACES+1] = {SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,NUL};

static char* indent(int l)
{
    return &spaces[10-2*l];
}
#endif

// Match pattern, return tokens consumed or -1
int pmatch_(pmatch_st_t* pst, const token_t* tv, int ti, size_t n, int l,
	    const uint8_t* pat)
{
    int pi = 0;  // pattern index
next:
    switch (PB(pi++)) {
    case P_END:
	DBG("%sP_END: (%d)\n", indent(l), ti);
	return ti;  // tokens consumed
    case P_OPT_END:
	DBG("%sP_OPT_END: (%d)\n", indent(l), ti);
	return ti;  // tokens consumed
    case P_ALT_END:
	DBG("%sP_ALT_END: (%d)\n", indent(l), ti);
	return ti;  // tokens consumed
    case P_CHOICE_END:
	DBG("%sP_CHOICE_END: (%d)\n", indent(l), ti);
	return ti;  // tokens consumed
    case P_REP_END:
	DBG("%sP_REP_END: (%d)\n", indent(l), ti);
	return ti;  // tokens consumed	
    case P_TOK: {
	// Match specific token
	uint8_t tok = PB(pi++);
	DBG("%sP_TOK: (%d) tok='%s'\n",indent(l),ti,(char*)tok_table[tok].name);
	if ((ti >= (int)n) || (tv[ti].t != tok))
	    return -1;
	ti++;
	break;
    }
    case P_TOK_W: {
	// Match specific token and write it
	uint8_t tok = PB(pi++);
	uint8_t val_off = PB(pi++);
	int off = pst->eo + val_off;

	DBG("%sP_TOK_W: (%d) tok='%s' val_off=%d off=%d\n",
	    indent(l), ti, (char*)tok_table[tok].name, val_off, off);
	if ((ti >= (int)n) || (tv[ti].t != tok))
	    return -1;
	store_int(pst->data, off, tok);
	ti++;
	break;
    }
    case P_INTEGER_S: {
	uint8_t val_off = PB(pi++);
	uint8_t sid = PB(pi++);
	DBG("%sP_INTEGER_S: (%d) val_off=%d,sid=%d\n", indent(l), ti,
	    val_off, sid);		
	if ((ti = pmatch_const_s(pst,tv,ti,n,V_INTEGER,val_off,sid)) < 0)
	    return -1;
	break;
    }	    
    case P_FLOAT_S: {
	uint8_t val_off = PB(pi++);
	uint8_t sid = PB(pi++);	
	DBG("%sP_FLOAT_S: (%d) val_off=%d\n", indent(l), ti, val_off);
	if ((ti = pmatch_const_s(pst,tv,ti,n,V_FLOAT,val_off,sid)) < 0)
	    return -1;
	break;
    }
    case P_CONST_S: {
	uint8_t opts_off = PB(pi++);
	uint8_t val_off  = PB(pi++);
	uint8_t sid      = PB(pi++);
	decl_opts_t opts = fetch_opts(pst->data, pst->eo+opts_off);
	DBG("%sP_CONST_S: (%d) opts_off=%d, val_off=%d, vt=%d\n",indent(l),ti,
	    opts_off, val_off, opts.vt);
	if ((ti = pmatch_const_s(pst,tv,ti,n,opts.vt,val_off,sid)) < 0)
	    return -1;
	break;
    }
    case P_STR: {
	// Capture WORD as string
	uint8_t val_off = PB(pi++);
	int off = pst->eo + val_off;  // eo already adjusted by P_REP
	DBG("%sP_STR: (%d) \"%.*s\" val_off=%d, off=%d\n", indent(l),ti,
	    tv[ti].v.str.len, tv[ti].v.str.ptr, val_off, off);
	if ((ti >= (int)n) || (tv[ti].t != WORD))
	    return -1;
	store_str(pst->data, off, &tv[ti].v.str);
	ti++;
	break;
    }
    case P_EXPR_S: {
	// Capture EXPR with stop-set
	uint8_t val_off = PB(pi++);
	uint8_t sid = PB(pi++);
	DBG("%sP_EXPR_S: (%d) val_off=%d set=%d\n", indent(l), ti,
	    val_off, sid);
	if ((ti = pmatch_expr_s(pst, tv, ti, n, val_off, sid)) < 0)
	    return -1;
	break;
    }
    case P_OPTS: {
	// Parse options, store at offset
	uint8_t val_off = PB(pi++);
	decl_opts_t opts = fetch_opts(pst->data, pst->eo+val_off);
	DBG("%sP_OPTS: (%d) val_off=%d\n", indent(l), ti, val_off);
	opts = parse_opts(pst->st, tv, &ti, n, opts);
	store_opts(pst->data, pst->eo+val_off, opts);
	break;
    }
    case P_OPT: {
	// Optional: try to match, ok if fails
	uint8_t len = PB(pi++);
	int r;
	DBG("%sP_OPT: (%d) len=%d\n", indent(l), ti, len);
	if ((r = pmatch_(pst, tv, ti, n, l+1, &pat[pi])) >= 0)
	    ti = r;
	// else: no match, that's ok for optional
	pi += len;
	break;
    }
    case P_CHOICE: {
	// Alternatives: try each until one matches
	// Save data before each alt, restore if it fails
	uint8_t num_alts = PB(pi++);
	int matched = 0;
	// is this really needed (mode compact way)
	// temp save of data for backtracking. Copy only what actually remains of
	// the data struct from the current cursor (clamped to the buffer), so a
	// struct smaller than 64 bytes is never over-read/written.
	uint8_t saved[64];
	size_t  avail = (const uint8_t*)pst->dend - (const uint8_t*)pst->data;
	size_t  slen  = avail < sizeof(saved) ? avail : sizeof(saved);
	
	DBG("%sP_CHOICE: (%d) num_alts=%d\n", indent(l),
	    ti, num_alts);
	
	for (int a = 0; a < num_alts && !matched; a++) {
	    uint8_t len;
	    int r;
	    
	    if (PB(pi++) != P_ALT) {
		DBG("%sALT[%d]: (%d) missing\n", indent(l+1), a, ti);
		return -1;
	    }
	    len = PB(pi++);
	    DBG("%sALT[%d]: (%d) len=%d\n", indent(l+1), ti, a, num_alts);
		
	    memcpy(saved, pst->data, slen);
	    if ((r = pmatch_(pst, tv, ti, n, l+2, &pat[pi])) >= 0) {
		ti = r;
		matched = 1;
		DBG("%sMATCHED\n", indent(l+1));
		// Skip remaining alts
		pi += len;
		for (int b = a + 1; b < num_alts; b++) {
		    uint8_t skip;
		    if (PB(pi++) != P_ALT) {
			DBG("%sALT[%d]: (%d) missing\n", indent(l+1),
			    b, ti);
			return -1;
		    }
		    skip = PB(pi++);
		    pi += skip;
		}
		if (PB(pi++) != P_CHOICE_END) {
		    DBG("%sP_CHOICE: (%d) missing CHOICE_END\n",indent(l+1),ti);
		    return -1;		    
		}
	    } else {
		memcpy(pst->data, saved, slen);  // restore
		pi += len;
	    }
	}
	if (!matched)
	    return -1;
	break;
    }
    case P_ARRAY: {  // normally used inside P_REP
	pst->eo = PB(pi++);  // base offset
	pst->ez = PB(pi++);  // element size
	DBG("%sP_ARRAY: (%d), eo=%d, ez=%d\n", indent(l), ti,
	    pst->eo, pst->ez);
	break;
    }
    case P_REP: {
	// Repeat: match zero or more times
	uint8_t len = PB(pi++);
	int pi0 = pi;  // len counts from here (including P_REP_END)
	int ix = 0;
	pst->eo = 0;
	pst->ez = 0;
	if (PB(pi) == P_ARRAY) {
	    pi++;
	    pst->eo = PB(pi++);  // base offset
	    pst->ez = PB(pi++);  // element size
	}
	DBG("%sP_REP: (%d) n=%ld, len=%d, ez=%d\n", indent(l),
	    ti, n, len, pst->ez);
	while (ti < (int)n) {
	    int r;
	    DBG("%sITER (%d) i=%d:\n", indent(l), ti, ix);
	    // fixme pass ix to pmatch to allow data to store
	    // array elements
	    pst->ix = ix;
	    r = pmatch_(pst, tv, ti, n, l+1, &pat[pi]);
	    if (r <= 0) break;  // no match or empty match
	    ti = r;
	    ix++;
	    DBG("%sAFTER_ITER (%d) i=%d:\n",indent(l+1),ti,ix);
	    pst->eo += pst->ez;
	}
	pi = pi0 + len;
	pst->eo = 0; // reset array
	pst->ez = 0;
	break;
    }
    case P_PAT: {
	// The target's offset into csp_pattern_data, TWO bytes, high first. One
	// would only hold while every P_PAT target happened to sit in the first
	// 256 of them -- an ordering constraint nothing enforces and a silent
	// wrong answer when it stops being true.
	uint16_t poff = ((uint16_t)PB(pi) << 8) | PB(pi + 1);
	uint8_t off;
	uint8_t cont_sid;
	void* saved_data = pst->data;
	int saved_eo = pst->eo;
	int r;

	pi += 2;
	off = PB(pi++);
	cont_sid = PB(pi++);
	DBG("%sP_PAT: (%d) poff=%d off=%d cont=%d\n", indent(l), ti,
	    poff, off, cont_sid);
	// eo selects current array element when used inside P_REP
	pst->data = (void*)(((uint8_t*)pst->data) + pst->eo + off);
	pst->eo = 0;
	if (cont_sid != STOP_NONE)
	    pst->cont_stack[pst->cont_sp++] = cont_sid;
	// The sub-pattern's bytes. P_PAT carries the OFFSET itself, so there is
	// no id and no table between the two -- see the note in csp_patterns.h.
	r = pmatch_(pst, tv, ti, n, l+1, csp_pattern_data + poff);
	if (cont_sid != STOP_NONE)
	    pst->cont_sp--;
	pst->data = saved_data;
	pst->eo = saved_eo;
	if (r < 0)
	    return -1;
	ti = r;
	break;
    }
    default:
	return -1;  // unknown command
    }
    goto next;
}

int pmatch(csp_rt_t* st, const token_t* tv, int ti, size_t n,
	   const uint8_t* pat, void* data, size_t data_size)
{
    pmatch_st_t pst;

    csp_stack_mark();   // recursive over the pattern -- sample every level
    pst.st = st;
    pst.data = data;
    pst.dend = (const uint8_t*)data + data_size;  // never save/restore past this
    pst.ez   = 0;     // element size (P_ARRAY)
    pst.ix   = 0;
    pst.eo   = 0;     // current element offset (P_REP)
    pst.cont_sp = 0;  // continuation stack empty
    return pmatch_(&pst, tv, ti, n, 0, pat);
}

#endif /* !CSP_EXEC_ONLY */
