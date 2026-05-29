#include "csp_parse.h"
#include <string.h>
#ifdef DEBUG

#endif

#ifdef DEBUG
#include <stdio.h>
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...)
#endif

// Callback table
static pmatch_cb_t callbacks[8];

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
    DBG("\n");
    *ip= i;
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

#ifdef __not_used__
// find next P_TOK in pattern (after i) return NEWLINE if not found?
NOINLINE static int next_ptok(const uint8_t* pat, int pi)
{
    while(pat[pi] != P_END) {
	switch(pat[pi]) {
	case P_TOK: return pat[pi+1];
	case P_INTEGER: pi += 2; break;
	case P_FLOAT:   pi += 2; break;	    
	case P_NUMBER:  pi += 3; break;
	case P_STR: pi += 2; break;
	case P_EXPR: pi += 2; break;
	case P_OPTS: pi += 2; break;
	case P_CALL: pi += 2; break;
	case P_ARRAY: pi += 2; break;	    
	case P_OPT: // how to handle ?
	case P_ALT: // how to handle ?
	    break;
	}
    }
    return NEWLINE;
}
#endif


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
    DBG("expr: ti=%d, num=%ld\n", ti, num);
    // Just record boundaries - actual parsing happens in csp_parse_rule
    range.pos = pst->tb+ti;
    range.len = num;
    store_expr(pst->data, pst->eo+off, range);
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


// Match pattern, return tokens consumed or -1
int pmatch_(pmatch_st_t* pst, token_t* tv, size_t n, const uint8_t* pat)
{
    int ti = 0;  // token index
    int pi = 0;  // pattern index

    while (pat[pi] != P_END) {
        uint8_t cmd = pat[pi++];
        switch (cmd) {
        case P_TOK: {
            // Match specific token
            uint8_t tok = pat[pi++];
	    DBG("P_TOK: tok='%s'\n", op_table[tok].name);
            if ((ti >= (int)n) || (tv[ti].t != tok))
                return -1;
            ti++;
            break;
        }
        case P_NUMBER: {
	    uint8_t opts_off = pat[pi++];
	    uint8_t val_off = pat[pi++];
	    decl_opts_t opts = fetch_opts(pst->data, pst->eo+opts_off);

	    DBG("P_NUMBER: opts_off=%d, val_off=%d, vt=%d\n",
		opts_off, val_off, opts.vt);
		   
	    if ((ti = pmatch_const(pst,tv,ti,n,opts.vt,val_off)) < 0)
		return -1;
	    break;
	}
        case P_INTEGER: {
	    uint8_t val_off = pat[pi++];
	    
	    DBG("P_INTEGER: val_off=%d\n", val_off);
	    
	    if ((ti = pmatch_const(pst,tv,ti,n,V_INTEGER,val_off)) < 0)
		return -1;
	    break;
	}
        case P_FLOAT: {
	    uint8_t val_off = pat[pi++];

	    DBG("P_FLOAT: val_off=%d\n", val_off);	    
	    
	    if ((ti = pmatch_const(pst,tv,ti,n,V_FLOAT,val_off)) < 0)
		return -1;
	    break;
	}
        case P_STR: {
            // Capture WORD as string
            uint8_t val_off = pat[pi++];
	    int off = pst->eo + val_off;  // eo already adjusted by P_REP
	    DBG("P_STR: val_off=%d, off=%d\n", val_off, off);
            if ((ti >= (int)n) || (tv[ti].t != WORD))
                return -1;
            store_str(pst->data, off, *(tstr_t*)&tv[ti].v.str);
            ti++;
            break;
        }
        case P_EXPR: {
            // Capture EXPR as start/stop index
            uint8_t val_off = pat[pi++];
	    DBG("P_EXPR: val_off=%d\n", val_off);
	    if ((ti = pmatch_expr(pst, tv, ti, n, val_off)) < 0)
		return -1;
            break;
        }	    
        case P_OPTS: {
            // Parse options, store at offset
            uint8_t val_off = pat[pi++];
	    decl_opts_t opts = fetch_opts(pst->data, pst->eo+val_off);
	    DBG("P_OPTS: val_off=%d\n", val_off);
	    opts = parse_opts(pst->st, tv, &ti, n, opts);
	    store_opts(pst->data, pst->eo+val_off, opts);
            break;
        }
        case P_OPT: {
            // Optional: try to match, ok if fails
            uint8_t len = pat[pi++];
            int r;
	    DBG("P_OPT: len=%d\n", len);
	    pst->tb = ti;
	    if ((r = pmatch_(pst, &tv[ti], n-ti, &pat[pi])) >= 0)
                ti += r;
            // else: no match, that's ok for optional
            pi += len;
            break;
        }
        case P_ALT: {
            // Alternatives: try each until one matches
            // Save data before each alt, restore if it fails
            uint8_t num_alts = pat[pi++];
            int matched = 0;
	    // is this really needed (mode compact way)
	    // temp save of data (assumes data < 64 bytes)	    
            uint8_t saved[64]; 

	    DBG("P_ALT: num_alts=%d\n", num_alts);

            for (int a = 0; a < num_alts && !matched; a++) {
                uint8_t len = pat[pi++];
		int r;
		
		DBG("ALT %d: len=%d\n", a, num_alts);
		
                memcpy(saved, pst->data, sizeof(saved));
		pst->tb = ti;
                if ((r = pmatch_(pst, &tv[ti], n-ti, &pat[pi])) >= 0) {
                    ti += r;
                    matched = 1;
                    // Skip remaining alts
                    pi += len;
                    for (int b = a + 1; b < num_alts; b++) {
                        uint8_t skip = pat[pi++];
                        pi += skip;
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
	    break;
	}
        case P_REP: {
            // Repeat: match zero or more times
            uint8_t len = pat[pi++];
	    int ix = 0;
	    pst->eo = 0;
	    pst->ez = 0;
	    if (pat[pi] == P_ARRAY) {
		pi++;
		pst->eo = pat[pi++];  // base offset
		pst->ez = pat[pi++];  // element size
	    }
	    DBG("P_REP: len=%d, ez=%d\n", len, pst->ez);
            while (ti < (int)n) {
		DBG("ITER %d:\n", ix);
		// fixme pass ix to pmatch to allow data to store
		// array elements
		pst->tb = ti;
		pst->ix = ix;
                int r = pmatch_(pst, &tv[ti], n-ti, &pat[pi]);
                if (r <= 0) break;  // no match or empty match
                ti += r;
		ix++;
		pst->eo += pst->ez;
            }
            pi += len;
	    pst->ez = 0; // reset array
            break;
        }
        case P_CALL: {
            // Call registered callback - returns tokens consumed or 0 on error
            uint8_t id = pat[pi++];
	    DBG("P_CALL: id=%d\n", id);
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
    }

    return ti;  // tokens consumed
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
    return pmatch_(&pst, tv, n, pat);
}
