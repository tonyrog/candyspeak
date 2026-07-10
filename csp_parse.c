#include "csp_parse.h"
#include <string.h>

// Stop-set storage: all tokens in one array, NONE-terminated sets
static uint8_t stop_toks[MAX_STOP_TOKENS];
static uint8_t stop_pos[NUM_STOP_SETS];
static const uint8_t*   pattern[NUM_PAT];
static int stop_toks_len = 0;
static int num_stop_sets = 0;

#ifdef DEBUG

#define STRCASE(id) case id: return #id

static char* stop_set_name(uint8_t sid)
{
    switch(sid) {
    STRCASE(STOP_NONE);
    STRCASE(STOP_OPTS);
    STRCASE(STOP_RULE_BODY);
    STRCASE(STOP_RULE_COND);
    STRCASE(STOP_RES);
    STRCASE(STOP_PORT);
    STRCASE(STOP_PIN1);
    STRCASE(STOP_PIN2);
    STRCASE(STOP_VAR_INIT);
    STRCASE(STOP_CONST_INIT);
    STRCASE(STOP_TIMER_TMO);
    STRCASE(STOP_TIMER_INIT);
    STRCASE(STOP_CAN_FRAMEID);
    STRCASE(STOP_CAN_BIT0);
    STRCASE(STOP_CAN_BIT1);
    STRCASE(STOP_CAN_BIT00);
    STRCASE(STOP_OBJECT_INIT_RHS);
    STRCASE(STOP_RULE_BODY_CONT);
    STRCASE(STOP_VAR_RES_CONT);
    STRCASE(STOP_CONST_RES_CONT);
    STRCASE(STOP_DIGITAL_PP_CONT);
    STRCASE(STOP_ANALOG_RES_CONT);
    STRCASE(STOP_ANALOG_PP_CONT);
    STRCASE(STOP_CAN_RES_CONT);
    STRCASE(STOP_OBJECT_INIT_CONT);
    default: return "???";
    }
}

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
    STRCASE(D_CAN);
    STRCASE(D_UART);
    STRCASE(D_SOCKET);
    STRCASE(D_MOD);
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    STRCASE(D_STATES);
    STRCASE(D_IN);    
#endif
    default: return "???";    
    }
}


static char* tok_name(uint8_t tok)
{
    switch(tok) {
    STRCASE(NONE);
    STRCASE(EXCLAMATION);
    STRCASE(TILDE);
    STRCASE(MINUS1);
    STRCASE(PLUS1);
    STRCASE(PLUS);
    STRCASE(MINUS);
    STRCASE(ASTERISK);
    STRCASE(SLASH);
    STRCASE(PERCENT);
    STRCASE(LTLT);
    STRCASE(GTGT);
    STRCASE(LT);
    STRCASE(LTEQ);
    STRCASE(GT);
    STRCASE(GTEQ);
    STRCASE(EQEQ);
    STRCASE(NEQ);
    STRCASE(AMP);
    STRCASE(BAR);
    STRCASE(CIRC);
    STRCASE(AMPAMP);
    STRCASE(BARBAR);
    STRCASE(EQ);
    STRCASE(RIMP);
    STRCASE(COMMA);
    STRCASE(QUEST);
    STRCASE(PULLUP);
    STRCASE(PULLDOWN);
    STRCASE(RESOLUTION);
    STRCASE(IN);
    STRCASE(OUT);
    STRCASE(INOUT);
    STRCASE(PWM);
    STRCASE(FLOAT);
    STRCASE(INTEGER);
    STRCASE(UNSIGNED);
    STRCASE(STRING);
    STRCASE(NATIVE);
    STRCASE(LITTLE);
    STRCASE(BIG);
    STRCASE(LP);
    STRCASE(RP);
    STRCASE(COLON);
    STRCASE(HASH);
    STRCASE(DOT);
    STRCASE(LB);
    STRCASE(RB);
    STRCASE(INT);
    STRCASE(FLT);
    STRCASE(STR);
    STRCASE(WORD);
    STRCASE(NEWLINE);
    default: return "???";
    }
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
    if ((set_idx < 0) || (set_idx >= num_stop_sets))
	return 0;
    i = stop_pos[set_idx];
    while ((i < stop_toks_len) && (stop_toks[i] != NONE)) {
	uint8_t t = stop_toks[i];
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
static void add_stop_tok(uint8_t sid, uint8_t tok)
{
    int start;
    int i;
    start = stop_pos[sid];
    for (i = start; i < stop_toks_len; i++) {
        if (stop_toks[i] == tok) return;  // already present
    }
    if (tok & TOK_SET)
	DBG("add set {%s} to stop_set %s\n",
	    stop_set_name(tok & TOK_SET_MASK), stop_set_name(sid));
    else
	DBG("add token %s to stop_set %s\n", tok_name(tok), stop_set_name(sid));
    if (stop_toks_len < MAX_STOP_TOKENS)
        stop_toks[stop_toks_len++] = tok;
}

// Collect FIRST tokens from pattern position pi
static void collect_first(uint8_t sid, const uint8_t* pat, int pi)
{
    uint8_t cmd;
    uint8_t len;

next:
    cmd = pat[pi];
    switch (cmd) {
    case P_END:
	add_stop_tok(sid, NEWLINE);
	return;
    case P_ALT_END:    pi++; break;
    case P_CHOICE_END: pi++; break;
    case P_OPT_END:    pi++; break;
    case P_REP_END:    pi++; break;
    case P_TOK:
    case P_TOK_W:
	add_stop_tok(sid,pat[pi + 1]);
	return;
    case P_STR:
	add_stop_tok(sid,WORD);
	return;
    case P_INTEGER_S:
    case P_FLOAT_S:	
    case P_NUMBER_S:
    case P_EXPR_S:
	return;
    case P_ALT:  // BUG? choice should skip!
	len = pat[pi+1];
	pi += (len+1);
	break;
    case P_OPTS: // add the whole set
	add_stop_tok(sid, STOP_SET(STOP_OPTS));
	pi += 2;
	break;
    case P_OPT:
	len = pat[pi + 1];
	collect_first(sid, pat, pi + 2);
	collect_first(sid, pat, pi + 2 + len);
	return;
    case P_CHOICE: {
	uint8_t n = pat[pi + 1];
	int p = pi + 2;
	int a;
	for (a = 0; a < n; a++) {
	    if (pat[p++] != P_ALT) {
		DBG("P_CHOICE %d P_ALT internal error\n", a);
		return;
	    }
	    len = pat[p++];
	    collect_first(sid, pat, p);
	    p += len;
	}
	return;
    }
    case P_REP:
	len = pat[pi + 1];
	collect_first(sid, pat, pi + 2);
	collect_first(sid, pat, pi + 2 + len);
	return;
    case P_ARRAY:
	pi += 3;
	break;
    case P_PAT: {
	int p = pat[pi + 1];
	collect_first(sid, pattern[p], 0);
	return;
    }
    default:
	return;
    }
    goto next;
}

// Scan entire pattern,
// build stop-sets for all P_NUMBER_S, P_INTEGER_S, P_FLOAT_S, P_EXPR_S
static void scan_pattern_(const uint8_t* pat)
{
    int pi = 0;
    int sid;
    uint8_t len;
    uint8_t n;
    int p, a;
next:
    switch(pat[pi]) {
    case P_END:
    case P_OPT_END:
    case P_ALT_END:
    case P_REP_END:
    case P_CHOICE_END:
	return;
    case P_TOK:
    case P_STR:	
	pi += 2;
	break;
    case P_TOK_W:	// cmd, tok, offset
	pi += 3;
	break;
    case P_NUMBER_S:
	n = 3;
	goto p_scan_s;
    case P_INTEGER_S:
    case P_FLOAT_S:
    case P_EXPR_S:
	n = 2;
	goto p_scan_s;	
    case P_OPTS:
	pi += 2;
	break;
    case P_OPT:
	len = pat[pi + 1];
	scan_pattern_(&pat[pi + 2]);
	pi += 2 + len;
	break;
    case P_CHOICE:
	n = pat[pi + 1];
	p = pi + 2;
	for (a = 0; a < n; a++) {
	    if (pat[p++] != P_ALT) {
		DBG("P_ALT %d internal error\n", a);
		return;
	    }	    
	    len = pat[p++];
	    scan_pattern_(&pat[p]);
	    p += len;
	}
	if (pat[p] != P_CHOICE_END) {
	    DBG("P_CHOICE_END  missing internal error\n");
	    return;	    
	}
	pi = p+1;
	break;
    case P_REP:
	len = pat[pi + 1];
	scan_pattern_(&pat[pi + 2]);
	pi += 2 + len;
	break;
    case P_ARRAY:
	pi += 3;
	break;
    case P_PAT: {
	uint8_t cont_sid = pat[pi+3];

	// Build continuation stop-set from followers.
	// A set shared by several P_PAT sites becomes the union
	// of the followers from all sites.
	if (cont_sid != STOP_NONE) {
	    int old = stop_pos[cont_sid];
	    if (cont_sid >= num_stop_sets)
		num_stop_sets = cont_sid + 1;
	    stop_pos[cont_sid] = stop_toks_len;
	    DBG("build cont set %d from followers at %d\n", cont_sid, pi+4);
	    if (old) {  // already built: merge old tokens
		while (stop_toks[old] != NONE)
		    add_stop_tok(cont_sid, stop_toks[old++]);
	    }
	    collect_first(cont_sid, pat, pi + 4);
	    add_stop_tok(cont_sid, NONE);
	}
	// sub-pattern is scanned by its own scan_pattern call (enum order)
	pi += 4;
	break;
    }
    default:
	pi++;
	break;
    }
    goto next;
    
p_scan_s:
    sid = pat[pi + n];
    if (sid >= num_stop_sets)
	num_stop_sets = sid+1;
#ifdef DEBUG
    if (stop_pos[sid] != 0)
	printf("stop set %s not empty!!!\n", stop_set_name(sid));
#endif
    stop_pos[sid] = stop_toks_len;
    DBG("build stop set %s = %d\n",
	stop_set_name(sid), stop_pos[sid]);
    pi += (n+1);
    collect_first(sid, pat, pi);
    add_stop_tok(sid, NONE);
    goto next;
}

void scan_pattern(int pat_id, const uint8_t* pat)
{
    pattern[pat_id] = pat;
    scan_pattern_(pat);
}

void init_stop_sets(void)
{
    stop_toks_len = 0;
    num_stop_sets = 0;
    memset(stop_pos, 0, sizeof(stop_pos));
    
    stop_pos[STOP_NONE] = stop_toks_len; // empty placeholder
    stop_toks[stop_toks_len++] = NONE;
    // Add OPTS tokens as a special set
    stop_pos[STOP_OPTS] = stop_toks_len; // options token set
    stop_toks[stop_toks_len++] = UNSIGNED;
    stop_toks[stop_toks_len++] = INTEGER;
    stop_toks[stop_toks_len++] = FLOAT;
    stop_toks[stop_toks_len++] = PWM;
    stop_toks[stop_toks_len++] = IN;
    stop_toks[stop_toks_len++] = OUT;
    stop_toks[stop_toks_len++] = INOUT;
    stop_toks[stop_toks_len++] = NATIVE;    
    stop_toks[stop_toks_len++] = LITTLE;
    stop_toks[stop_toks_len++] = BIG;
    stop_toks[stop_toks_len++] = PULLUP;
    stop_toks[stop_toks_len++] = PULLDOWN;
    stop_toks[stop_toks_len++] = NONE;
    num_stop_sets = 2;
}

#ifdef DEBUG
void dump_stop_sets(void)
{
    int i;
    FILE* f = stdout;
    fprintf(f, "stop_toks[%d] = {\n", stop_toks_len);
    for (i = 0; i < stop_toks_len; i++) {
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

typedef struct {
    csp_rt_t* st;
    void* data;
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
	case UNSIGNED: opts.vt=V_UNSIGNED; DBG("UNSIGNED,"); break;
	case INTEGER:  opts.vt=V_INTEGER; DBG("INTEGER,"); break;
	case FLOAT:    opts.vt=V_FLOAT; DBG("FLOAT,"); break;
	case PWM:      opts.pwm = 1; DBG("PWM,"); break;
	case IN:       opts.dir |= DIR_IN; DBG("IN,"); break;
	case OUT:      opts.dir |= DIR_OUT; DBG("OUT,"); break;
	case INOUT:    opts.dir |= DIR_INOUT; DBG("INOUT,"); break;
	case NATIVE:   opts.endian=E_NATIVE; DBG("NATIVE,"); break;	    
	case LITTLE:   opts.endian=E_LITTLE; DBG("LITTLE,"); break;
	case BIG:      opts.endian=E_BIG; DBG("BIG,"); break;	    
	case PULLUP:   opts.pullup=1; DBG("PULLUP,"); break;
	case PULLDOWN: opts.pulldown=1; DBG("PULLDOWN,"); break;
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
    switch (pat[pi++]) {
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
	uint8_t tok = pat[pi++];
	DBG("%sP_TOK: (%d) tok='%s'\n", indent(l), ti, tok_table[tok].name);
	if ((ti >= (int)n) || (tv[ti].t != tok))
	    return -1;
	ti++;
	break;
    }
    case P_TOK_W: {
	// Match specific token and write it
	uint8_t tok = pat[pi++];
	uint8_t val_off = pat[pi++];
	int off = pst->eo + val_off;

	DBG("%sP_TOK_W: (%d) tok='%s' val_off=%d off=%d\n", indent(l), ti,
	    tok_table[tok].name, val_off, off);
	if ((ti >= (int)n) || (tv[ti].t != tok))
	    return -1;
	store_int(pst->data, off, tok);
	ti++;
	break;
    }
    case P_INTEGER_S: {
	uint8_t val_off = pat[pi++];
	uint8_t sid = pat[pi++];
	DBG("%sP_INTEGER_S: (%d) val_off=%d,sid=%d\n", indent(l), ti,
	    val_off, sid);		
	if ((ti = pmatch_const_s(pst,tv,ti,n,V_INTEGER,val_off,sid)) < 0)
	    return -1;
	break;
    }	    
    case P_FLOAT_S: {
	uint8_t val_off = pat[pi++];
	uint8_t sid = pat[pi++];	
	DBG("%sP_FLOAT_S: (%d) val_off=%d\n", indent(l), ti, val_off);
	if ((ti = pmatch_const_s(pst,tv,ti,n,V_FLOAT,val_off,sid)) < 0)
	    return -1;
	break;
    }
    case P_NUMBER_S: {
	uint8_t opts_off = pat[pi++];
	uint8_t val_off  = pat[pi++];
	uint8_t sid      = pat[pi++];
	decl_opts_t opts = fetch_opts(pst->data, pst->eo+opts_off);
	DBG("%sP_NUMBER: (%d) opts_off=%d, val_off=%d, vt=%d\n", indent(l), ti,
	    opts_off, val_off, opts.vt);
	if ((ti = pmatch_const_s(pst,tv,ti,n,opts.vt,val_off,sid)) < 0)
	    return -1;
	break;
    }
    case P_STR: {
	// Capture WORD as string
	uint8_t val_off = pat[pi++];
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
	uint8_t val_off = pat[pi++];
	uint8_t sid = pat[pi++];
	DBG("%sP_EXPR_S: (%d) val_off=%d set=%d\n", indent(l), ti,
	    val_off, sid);
	if ((ti = pmatch_expr_s(pst, tv, ti, n, val_off, sid)) < 0)
	    return -1;
	break;
    }
    case P_OPTS: {
	// Parse options, store at offset
	uint8_t val_off = pat[pi++];
	decl_opts_t opts = fetch_opts(pst->data, pst->eo+val_off);
	DBG("%sP_OPTS: (%d) val_off=%d\n", indent(l), ti, val_off);
	opts = parse_opts(pst->st, tv, &ti, n, opts);
	store_opts(pst->data, pst->eo+val_off, opts);
	break;
    }
    case P_OPT: {
	// Optional: try to match, ok if fails
	uint8_t len = pat[pi++];
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
	uint8_t num_alts = pat[pi++];
	int matched = 0;
	// is this really needed (mode compact way)
	// temp save of data (assumes data < 64 bytes)
	uint8_t saved[64]; 
	
	DBG("%sP_CHOICE: (%d) num_alts=%d\n", indent(l),
	    ti, num_alts);
	
	for (int a = 0; a < num_alts && !matched; a++) {
	    uint8_t len;
	    int r;
	    
	    if (pat[pi++] != P_ALT) {
		DBG("%sALT[%d]: (%d) missing\n", indent(l+1), a, ti);
		return -1;
	    }
	    len = pat[pi++];
	    DBG("%sALT[%d]: (%d) len=%d\n", indent(l+1), ti, a, num_alts);
		
	    memcpy(saved, pst->data, sizeof(saved));
	    if ((r = pmatch_(pst, tv, ti, n, l+2, &pat[pi])) >= 0) {
		ti = r;
		matched = 1;
		DBG("%sMATCHED\n", indent(l+1));
		// Skip remaining alts
		pi += len;
		for (int b = a + 1; b < num_alts; b++) {
		    uint8_t skip;
		    if (pat[pi++] != P_ALT) {
			DBG("%sALT[%d]: (%d) missing\n", indent(l+1),
			    b, ti);
			return -1;
		    }
		    skip = pat[pi++];
		    pi += skip;
		}
		if (pat[pi++] != P_CHOICE_END) {
		    DBG("%sP_CHOICE: (%d) missing CHOICE_END\n",indent(l+1),ti);
		    return -1;		    
		}
	    } else {
		memcpy(pst->data, saved, sizeof(saved));  // restore
		pi += len;
	    }
	}
	if (!matched)
	    return -1;
	break;
    }
    case P_ARRAY: {  // normally used inside P_REP
	pst->eo = pat[pi++];  // base offset
	pst->ez = pat[pi++];  // element size
	DBG("%sP_ARRAY: (%d), eo=%d, ez=%d\n", indent(l), ti,
	    pst->eo, pst->ez);
	break;
    }
    case P_REP: {
	// Repeat: match zero or more times
	uint8_t len = pat[pi++];
	int pi0 = pi;  // len counts from here (including P_REP_END)
	int ix = 0;
	pst->eo = 0;
	pst->ez = 0;
	if (pat[pi] == P_ARRAY) {
	    pi++;
	    pst->eo = pat[pi++];  // base offset
	    pst->ez = pat[pi++];  // element size
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
	uint8_t pid = pat[pi++];
	uint8_t off = pat[pi++];
	uint8_t cont_sid = pat[pi++];
	void* saved_data = pst->data;
	int saved_eo = pst->eo;
	int r;

	DBG("%sP_PAT: (%d) pid=%d off=%d cont=%d\n", indent(l), ti,
	    pid, off, cont_sid);
	// eo selects current array element when used inside P_REP
	pst->data = (void*)(((uint8_t*)pst->data) + pst->eo + off);
	pst->eo = 0;
	if (cont_sid != STOP_NONE)
	    pst->cont_stack[pst->cont_sp++] = cont_sid;
	r = pmatch_(pst, tv, ti, n, l+1, pattern[pid]);
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
	   const uint8_t* pat, void* data)
{
    pmatch_st_t pst;

    pst.st = st;
    pst.data = data;
    pst.ez   = 0;     // element size (P_ARRAY)
    pst.ix   = 0;
    pst.eo   = 0;     // current element offset (P_REP)
    pst.cont_sp = 0;  // continuation stack empty
    return pmatch_(&pst, tv, ti, n, 0, pat);
}
