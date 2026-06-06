#include "csp_parse.h"
#include <string.h>

#ifdef DEBUG
#include <stdio.h>
extern int debug;
#define DBG(...) do { \
	if (debug) printf(__VA_ARGS__);		\
    } while(0)
#else
#define DBG(...)
#endif

// Callback table
static pmatch_cb_t callbacks[8];

// Stop-set storage: all tokens in one array, NONE-terminated sets
static uint8_t stop_toks[MAX_STOP_TOKENS];
static uint8_t stop_pos[MAX_STOP_SETS];
static int stop_toks_len = 0;
static int num_stop_sets = 0;

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
    start = stop_toks[sid];
    DBG("add token %d to stop_set %d\n", tok, sid);
    for (i = start; i < stop_toks_len; i++) {
        if (stop_toks[i] == tok) return;  // already present
    }
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
	add_stop_tok(sid,pat[pi + 1]);
	return;
    case P_INTEGER:
    case P_INTEGER_S:
    case P_FLOAT:
    case P_NUMBER:
    case P_STR:
    case P_EXPR:
    case P_EXPR_S:
	return;
    case P_ALT:  // we hit a choice alternative skip it
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
    case P_CALL:
	pi += 2;
	break;
    default:
	return;
    }
    goto next;
}

// Scan entire pattern, build stop-sets for all P_EXPR_S found
void scan_pattern(const uint8_t* pat)
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
	return;
    case P_TOK:
	pi += 2;
	break;
    case P_INTEGER:
    case P_FLOAT:
    case P_STR:
    case P_EXPR:
	pi += 2;
	break;
    case P_INTEGER_S:
    case P_EXPR_S:
	sid = pat[pi + 2];
	if (sid >= num_stop_sets)
	    num_stop_sets = sid+1;	    
#ifdef DEBUG
	if (stop_pos[sid] != 0)
	    printf("stop set %d not empty!!!\n", sid);
#endif
	stop_pos[sid] = stop_toks_len;
	DBG("build stop set %d = %d [num_stop_sets=%d]\n",
	    sid, stop_pos[sid], num_stop_sets);
	pi += 3;
	collect_first(sid, pat, pi);
	add_stop_tok(sid, NONE);
	break;
    case P_NUMBER:
	pi += 3;
	break;
    case P_OPTS:
	pi += 2;
	break;
    case P_OPT:
	len = pat[pi + 1];
	scan_pattern(&pat[pi + 2]);
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
	    scan_pattern(&pat[p]);
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
	scan_pattern(&pat[pi + 2]);
	pi += 2 + len;
	break;
    case P_ARRAY:
	pi += 3;
	break;
    case P_CALL:
	pi += 2;
	break;
    default:
	pi++;
	break;
    }
    goto next;
}

void init_stop_sets(void)
{
    stop_toks_len = 0;
    num_stop_sets = 0;
    
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
	fprintf(f, "%d,", stop_toks[i]); // op_table[stop_toks[i]].name);
	if (i && !(i & 0x7)) fprintf(f, "\n");
    }
    fprintf(f, "};\n");

    fprintf(f, "stop_pos[%d] = {\n", num_stop_sets);
    for (i = 0; i < num_stop_sets; i++) {
	fprintf(f, "%d,", stop_pos[i]);
	if (i && !(i & 0x7)) fprintf(f, "\n");	
    }
    fprintf(f, "};\n");    
}
#endif

typedef struct {
    csp_rt_t* st;
    void* data;
    int tb;       // base token index to get absolute index
    uint8_t ez;   // element size (set by P_ARRAY) (max element size=255!)
    int ix;       // one-level repetition arrary index
    int eo;       // element offset
} pmatch_st_t;


// how to do this be made with pattern?
NOINLINE decl_opts_t parse_opts(csp_rt_t* st, token_t* tv,
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

void pmatch_set_cb(int id, pmatch_cb_t cb)
{
    if (id >= 0 && id < 8) callbacks[id] = cb;
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

static inline void store_str(void* data, int off, tstr_t str)
{
    *((tstr_t*)((uint8_t*)data + off)) = str;
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

NOINLINE static int pmatch_expr(pmatch_st_t* pst, token_t* tv, int ti,
				size_t n, uint8_t off)
{
    int k = ti;
    size_t num;
    pexpr_t range;

    // Find expression boundary (stop at NEWLINE or QUEST)
    while ((k < (int)n) &&
	   (tv[k].t != NEWLINE) &&
	   (tv[k].t != QUEST))
	k++;
    num = (k > ti) ? k - ti : 1;
    DBG("expr: %d:%d, num=%ld\n", ti, ti+pst->tb, num);
    // Just record boundaries - actual parsing happens in csp_parse_rule
    range.pos = pst->tb+ti;
    range.len = num;
    store_expr(pst->data, pst->eo+off, range);
    return ti + num;
}

// Expression with stop-set
NOINLINE static int pmatch_expr_s(pmatch_st_t* pst, token_t* tv, int ti,
				  size_t n, uint8_t off, int sid)
{
    int k = ti;
    size_t num;
    pexpr_t range;

    // Find expression boundary using stop-set
    while ((k < (int)n) && !stop_set_has(sid, tv[k].t))
	k++;
    num = (k > ti) ? k - ti : 1;
    DBG("expr_s: %d:%d, num=%ld, set=%d\n", ti, ti+pst->tb, num, sid);
    range.pos = pst->tb+ti;
    range.len = num;
    store_expr(pst->data, pst->eo+off, range);
    return ti + num;
}

// match type vt constant
static int pmatch_const_s(pmatch_st_t* pst, token_t* tv, int ti, size_t n,
			  uint8_t vt, uint8_t off, int sid)
{
    int k = ti;
    size_t num;
    rentry_t result;

    // Find expression boundary using stop-set
    while ((k < (int)n) && !stop_set_has(sid, tv[k].t))
	k++;    
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


// match type vt constant
static int pmatch_const(pmatch_st_t* pst, token_t* tv, int ti, size_t n,
			uint8_t vt, uint8_t off)
{
    int k = ti;
    size_t num;
    rentry_t result;

    while ((k < (int)n) &&
	   (tv[k].t != NEWLINE) &&
	   (tv[k].t != COLON) &&
	   (tv[k].t != DOT) &&
	   (tv[k].t != QUEST) &&
	   (tv[k].t != LB) &&
	   (tv[k].t != RB) &&
	   (tv[k].t != EQ))
	k++;
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
int pmatch_(pmatch_st_t* pst, token_t* tv, size_t n, int l, const uint8_t* pat)
{
    int ti = 0;  // token index
    int pi = 0;  // pattern index
    uint8_t cmd;
next:
    cmd = pat[pi++];
    switch (cmd) {
    case P_END:
	DBG("P_END: %d:%d\n", ti, ti+pst->tb);
	return ti;  // tokens consumed
    case P_OPT_END:
	DBG("P_OPT_END: %d:%d\n", ti, ti+pst->tb);
	return ti;  // tokens consumed
    case P_ALT_END:
	DBG("P_ALT_END: %d:%d\n", ti, ti+pst->tb);
	return ti;  // tokens consumed
    case P_CHOICE_END:
	DBG("P_CHOICE_END: %d:%d\n", ti, ti+pst->tb);
	return ti;  // tokens consumed
    case P_TOK: {
	// Match specific token
	uint8_t tok = pat[pi++];
	DBG("%sP_TOK: %d:%d tok='%s'\n", indent(l),
	    ti, ti+pst->tb,
	    op_table[tok].name);
	if ((ti >= (int)n) || (tv[ti].t != tok))
	    return -1;
	ti++;
	break;
    }
    case P_NUMBER: {
	uint8_t opts_off = pat[pi++];
	uint8_t val_off = pat[pi++];
	decl_opts_t opts = fetch_opts(pst->data, pst->eo+opts_off);
	
	DBG("%sP_NUMBER: %d:%d opts_off=%d, val_off=%d, vt=%d\n", indent(l),
	    ti, ti+pst->tb, opts_off, val_off, opts.vt);
	
	if ((ti = pmatch_const(pst,tv,ti,n,opts.vt,val_off)) < 0)
	    return -1;
	break;
    }
    case P_INTEGER: {
	uint8_t val_off = pat[pi++];
	DBG("%sP_INTEGER: %d:%d val_off=%d\n", indent(l),
	    ti, ti+pst->tb, val_off);		
	if ((ti = pmatch_const(pst,tv,ti,n,V_INTEGER,val_off)) < 0)
	    return -1;
	break;
    }
    case P_INTEGER_S: {
	uint8_t val_off = pat[pi++];
	uint8_t sid = pat[pi++];
	DBG("%sP_INTEGER_S: %d:%d val_off=%d,sid=%d\n", indent(l),
	    ti, ti+pst->tb, val_off, sid);		
	if ((ti = pmatch_const_s(pst,tv,ti,n,V_INTEGER,val_off,sid)) < 0)
	    return -1;
	break;
    }	    
    case P_FLOAT: {
	uint8_t val_off = pat[pi++];
	DBG("%sP_FLOAT: %d:%d val_off=%d\n", indent(l),
	    ti, ti+pst->tb, val_off);
	if ((ti = pmatch_const(pst,tv,ti,n,V_FLOAT,val_off)) < 0)
	    return -1;
	break;
    }
    case P_STR: {
	// Capture WORD as string
	uint8_t val_off = pat[pi++];
	int off = pst->eo + val_off;  // eo already adjusted by P_REP
	DBG("%sP_STR: %d:%d \"%.*s\" val_off=%d, off=%d\n", indent(l),
	    ti, ti+pst->tb,
	    tv[ti].v.str.len, tv[ti].v.str.ptr, val_off, off);
	if ((ti >= (int)n) || (tv[ti].t != WORD))
	    return -1;
	store_str(pst->data, off, *(tstr_t*)&tv[ti].v.str);
	ti++;
	break;
    }
    case P_EXPR: {
	// Capture EXPR as start/stop index
	uint8_t val_off = pat[pi++];
	DBG("%sP_EXPR: %d:%d val_off=%d\n", indent(l),
	    ti, ti+pst->tb, val_off);
	if ((ti = pmatch_expr(pst, tv, ti, n, val_off)) < 0)
	    return -1;
	break;
    }
    case P_EXPR_S: {
	// Capture EXPR with stop-set
	uint8_t val_off = pat[pi++];
	uint8_t sid = pat[pi++];
	DBG("%sP_EXPR_S: %d:%d val_off=%d set=%d\n", indent(l),
	    ti, ti+pst->tb, val_off, sid);
	if ((ti = pmatch_expr_s(pst, tv, ti, n, val_off, sid)) < 0)
	    return -1;
	break;
    }
    case P_OPTS: {
	// Parse options, store at offset
	uint8_t val_off = pat[pi++];
	decl_opts_t opts = fetch_opts(pst->data, pst->eo+val_off);
	DBG("%sP_OPTS: %d:%d val_off=%d\n", indent(l),
	    ti, ti+pst->tb, val_off);
	opts = parse_opts(pst->st, tv, &ti, n, opts);
	store_opts(pst->data, pst->eo+val_off, opts);
	break;
    }
    case P_OPT: {
	// Optional: try to match, ok if fails
	uint8_t len = pat[pi++];
	int tb0 = pst->tb;  // save current tb
	int r;
	DBG("%sP_OPT: %d:%d len=%d\n", indent(l), ti, ti+pst->tb, len);
	pst->tb += ti;
	if ((r = pmatch_(pst, &tv[ti], n-ti, l+1, &pat[pi])) >= 0) {
	    ti += r;
	}
	// else: no match, that's ok for optional
	pst->tb = tb0;	    
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
	
	DBG("%sP_ALT: %d:%d num_alts=%d\n", indent(l),
	    ti, ti+pst->tb, num_alts);
	
	for (int a = 0; a < num_alts && !matched; a++) {
	    uint8_t len;
	    int tb0 = pst->tb;  // save tb
	    int r;
	    
	    if (pat[pi++] != P_ALT) {
		DBG("%sALT[%d]: missing\n", indent(l), a);
		return -1;
	    }
	    len = pat[pi++];
	    DBG("%sALT[%d]: %d:%d len=%d\n", indent(l),
		a, ti, ti+pst->tb, num_alts);
		
	    memcpy(saved, pst->data, sizeof(saved));
	    pst->tb += ti;
	    if ((r = pmatch_(pst, &tv[ti], n-ti, l+1, &pat[pi])) >= 0) {
		ti += r;
		matched = 1;
		// Skip remaining alts
		pi += len;
		for (int b = a + 1; b < num_alts; b++) {
		    uint8_t skip;
		    if (pat[pi++] != P_ALT) {
			DBG("%sALT[%d]: missing\n", indent(l), b);
			return -1;
		    }
		    skip = pat[pi++];
		    pi += skip;
		}
		if (pat[pi++] != P_CHOICE_END) {
		    DBG("%sCHOICE: missing CHOICE_END\n", indent(l));
		    return -1;		    
		}
	    } else {
		memcpy(pst->data, saved, sizeof(saved));  // restore
		pi += len;
	    }
	    pst->tb = tb0;
	}
	if (!matched)
	    return -1;
	break;
    }
    case P_ARRAY: {  // normally used inside P_REP
	pst->eo = pat[pi++];  // base offset
	pst->ez = pat[pi++];  // element size
	DBG("%sP_ARRAY: %d:%d, eo=%d, ez=%d\n", indent(l),
	    ti, ti+pst->tb, pst->eo, pst->ez);
	break;
    }
    case P_REP: {
	// Repeat: match zero or more times
	uint8_t len = pat[pi++];
	int ix = 0;
	int tb0 = pst->tb;
	pst->eo = 0;
	pst->ez = 0;
	if (pat[pi] == P_ARRAY) {
	    pi++;
	    pst->eo = pat[pi++];  // base offset
	    pst->ez = pat[pi++];  // element size
	}
	DBG("%sP_REP: %d:%d len=%d, ez=%d\n", indent(l),
	    ti, ti+pst->tb, len, pst->ez);
	while (ti < (int)n) {
	    DBG("ITER %d:\n", ix);
	    // fixme pass ix to pmatch to allow data to store
	    // array elements
	    pst->tb += ti;
	    pst->ix = ix;
	    int r = pmatch_(pst, &tv[ti], n-ti, l+1, &pat[pi]);
	    if (r <= 0) break;  // no match or empty match
	    ti += r;
	    ix++;
	    pst->eo += pst->ez;
	}
	pi += len;
	pst->tb = tb0;
	pst->ez = 0; // reset array
	break;
    }
    case P_CALL: {
	// Call registered callback - returns tokens consumed or 0 on error
	uint8_t id = pat[pi++];
	DBG("%sP_CALL: %d:%d id=%d\n",
	    indent(l), ti, ti+pst->tb, id);
	if (id < 8 && callbacks[id]) {
	    int r = callbacks[id](pst->st, tv, ti, pst->data);
	    if (r <= 0)
		return -1;
	    ti += r;
	}
	break;
    }
    default:
	return -1;  // unknown command
    }
    goto next;
}

int pmatch(csp_rt_t* st, token_t* tv, size_t n,
	   const uint8_t* pat, void* data)
{
    pmatch_st_t pst;

    pst.st = st;
    pst.data = data;
    pst.ez   = 0;     // element size (P_ARRAY)
    pst.tb   = 0;
    pst.ix   = 0;
    pst.eo   = 0;     // current element offset (P_REP)
    return pmatch_(&pst, tv, n, 0, pat);
}
