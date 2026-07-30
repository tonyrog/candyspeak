// Dump functions for debugging and inspection
// but also generate C code for builtin eeprom code
//
#include <stdio.h>
#include <ctype.h>
#include "csp.h"
#include "csp_dump.h"
#include "csp_print.h"

extern const op_entry_t decl_table[];
extern const op_entry_t tok_table[];
extern const op_info_t op_info[];

// Helper to print fvalue_t (works for both float and fixpoint)
static void fprint_fvalue(FILE* f, fvalue_t v)
{
#if FVALUE_IS_FIXPOINT
    // Take BOTH halves from the magnitude and put the sign back by hand.
    // FIX_TO_INT is an arithmetic shift, so for a negative value it is FLOOR,
    // not truncation -- pairing that integer part with a fraction taken from
    // the magnitude printed -2.5 as "-3.500000", a whole unit out. Negating
    // through uint32_t also keeps INT32_MIN out of undefined behaviour.
    // Same shape as csp_print_fixpoint, which had it right all along.
    int neg = (v < 0);
    uint32_t absv = neg ? -(uint32_t)v : (uint32_t)v;
    uint32_t intpart = absv >> FIX_SHIFT;
    uint32_t fracpart = absv & FIX_MASK;
    // Use 64-bit to avoid overflow: fracpart * 1000000 can exceed 32 bits
    fracpart = (uint32_t)(((uint64_t)fracpart * 1000000) >> FIX_SHIFT);
    fprintf(f, "%s%u.%06u", neg ? "-" : "", intpart, fracpart);
#else
    fprintf(f, "%f", v);
#endif
}

static const char spaces[] = "                ";

static const char* indent(int lev)
{
    int pos = (sizeof(spaces)-1) - 2*lev;
    if (pos < 0)
	return "...";
    return spaces + pos;
}

void csp_fprint_tag(FILE* f, csp_rt_t* st, index_t n)
{
    int m = OBJ(n);
    int i = INDEX(n);
    if (m == GLOBAL) // global
	fprintf(f, "{%c,%d}", csp_tag(st,n), i);
    else if (m == CURRENT) // match
	fprintf(f, "{cur,%c,%d}", csp_tag(st,n), i);
    else
	fprintf(f, "{%d,%c,%d}", m, csp_tag(st,n), i);
}

void csp_print_tag(csp_rt_t* st, index_t n)
{
    csp_fprint_tag(stdout, st, n);
}

void csp_fprint_escaped_string(FILE* f, char* ptr, int len)
{
    fputc('"', f);
    while(len--) {
	int c = *ptr++;
	if (isprint(c))
	    fputc(c, f);
	else {
	    switch(c) {
	    case '\f': fputc('\\', f); fputc('f', f); break;
	    case '\n': fputc('\\', f); fputc('n', f); break;
	    case '\r': fputc('\\', f); fputc('r', f); break;
	    case '\t': fputc('\\', f); fputc('t', f); break;
	    case '\v': fputc('\\', f); fputc('v', f); break;
	    case '\b': fputc('\\', f); fputc('b', f); break;
	    case '\a': fputc('\\', f); fputc('a', f); break;
	    default: fprintf(f, "\\%03o", c); break;
	    }
	}
    }
    fputc('"', f);
}

void csp_fprint_value(FILE* f, csp_rt_t* st, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_TIMER:
    case V_DIGITAL:
    case V_ANALOG:
    case V_INTEGER:  fprintf(f, "%d", val.i); break;
    case V_UNSIGNED: fprintf(f, "16#%x", val.u); break; // fixme lang
    case V_FLOAT:    fprint_fvalue(f, val.f); break;
    case V_STRING:
	// val.s == 0 is the "no string" sentinel: new_string() never returns 0
	// (the length byte would sit at offset -1). String constants leave their
	// runtime value slot at 0, so guard before reading the length prefix.
	if (val.s <= 0)
	    fputs("\"\"", f);
	else
	    csp_fprint_escaped_string(f, csp_str_at(st, val.s), csp_str_byte(st, val.s-1));
	break;
    default: fprintf(f, "???"); break;
    }
}

void dump_edge_list(FILE* f, csp_rt_t* st, index_t ix)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int n;
    int i = INDEX(ix);
    fprintf(f, "[");
    if ((n = st->idg[i])) {
	int j;
	int base = st->ofs[i];
	for (j = 0; j < n; j++) {
	    index_t rule = st->edg[base+j];  // parent node
	    if (j > 0) fputc(',', f);
	    fprintf(f, "%d", rule);
	}
    }
    fprintf(f, "]");
#endif
}

index_t csp_dump_rule(FILE* f, int lev, csp_rt_t* st, int i, char* eot)
{
    index_t ix = MAKE_INDEX(0,i);
    switch(decl(st,i,type)) {
    case DECL_VARIABLE:
    case DECL_DIGITAL:
    case DECL_ANALOG:
    case DECL_FIELD:
    case DECL_BUFFER:   // pack (Buf <<= ...) makes a buffer a reactive target
    case DECL_TIMER:
	fprintf(f, "%s", indent(lev));
	fprintf(f, "{rules,%d,'%s',", i, decl_name(st, ix));
	dump_edge_list(f, st, ix);
	fprintf(f, "}%s\n", eot);
	break;
    default:
	break;
    }
    return i+1;
}

index_t csp_dump_instr(FILE* f, int lev, csp_rt_t* st, int i, char* eot)
{
    fprintf(f, "%s", indent(lev));
    switch(instr(st,i,op)) {
    case OP_NOP:
	fprintf(f, "{instr,%d,'NOP'}%s\n",
		i, eot);
	break;
    case OP_NEXT:
	fprintf(f, "{instr,%d,'NEXT',[r%d]}%s\n",
		i, instr(st,i,x.x), eot);
	break;
    case OP_LD:
	fprintf(f, "{instr,%d,'LD',[r%d,",
		i,
		instr(st,i,m.x));
	csp_fprint_tag(f, st, instr(st,i,m.mem));
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_LDP:
	fprintf(f, "{instr,%d,'LDP',[r%d,",
		i, instr(st,i,m.x));
	csp_fprint_tag(f, st, instr(st,i,m.mem));
	fprintf(f, ",%d]}%s\n", instr(st,i,m.y), eot);
	break;
    case OP_ST:
	fprintf(f, "{instr,%d,'ST',[r%d,",
		i, instr(st,i,m.x));
	csp_fprint_tag(f, st, instr(st,i,m.mem));
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_STP:
	fprintf(f, "{instr,%d,'STP',[r%d,",
		i, instr(st,i,m.x));
	csp_fprint_tag(f, st, instr(st,i,m.mem));
	fprintf(f, ",%d]}%s\n", instr(st,i,m.y),eot);
	break;	
    case OP_STIMP:
	fprintf(f, "{instr,%d,'STIMP',[r%d,",
		i, instr(st,i,m.x));
	csp_fprint_tag(f, st, instr(st,i,m.mem));
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_CHG:
	fprintf(f, "{instr,%d,'CHG',[r%d,",
		i, instr(st,i,m.x));
	csp_fprint_tag(f, st, instr(st,i,m.mem));
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_LI:
	fprintf(f, "{instr,%d,'LI',[r%d,%d]}%s\n",
		i,
		instr(st,i,i.x),
		instr(st,i,i.imm),
		eot);
	break;
    case OP_LIU:
	fprintf(f, "{instr,%d,'LIU',[r%d,%u]}%s\n",
		i,
		instr(st,i,i.x),
		(uint16_t)instr(st,i,i.imm),
		eot);
	break;
    case OP_LIH:
	fprintf(f, "{instr,%d,'LIH',[r%d,16#%04x]}%s\n",
		i,
		instr(st,i,i.x),
		(uint16_t)instr(st,i,i.imm),
		eot);
	break;
    case OP_EQI:
	fprintf(f, "{instr,%d,'EQI',[r%d,",
		i,
		instr(st,i,mi.x));
	csp_fprint_tag(f, st, instr(st,i,mi.mem));
	fprintf(f, ",%d", instr(st,i,mi.imm));
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_STI:  // store immediate to memory (no result register)
	fprintf(f, "{instr,%d,'STI',[", i);
	csp_fprint_tag(f, st, instr(st,i,mi.mem));
	fprintf(f, ",%d", instr(st,i,mi.imm));
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_ARG:
	fprintf(f, "{instr,%d,'ARG',[r%d,%d]}%s\n",
		i,
		instr(st,i,i.x),
		instr(st,i,i.imm),
		eot);
	break;
    case OP_CALL:
	fprintf(f, "{instr,%d,'CALL',[r%d,%s,16#%04x]}%s\n",
		i,
		instr(st,i,f.x),
		(instr(st,i,f.usr) ?
		 st->ufuncs[instr(st,i,f.idx)].name :
		 csp_builtin_funcs[instr(st,i,f.idx)].name),
		instr(st,i,f.avt),	
		eot);
	break;
    case OP_RULE:
	fprintf(f, "{instr,%d,'RULE',[r%d,%d]}%s\n",
		i,
		instr(st,i,r.cnd), instr(st,i,r.nxt), eot);
	break;
    case OP_NINSTATE:
	fprintf(f, "{instr,%d,'NINSTATE',[r%d,%d,%d]}%s\n",
		i,
		instr(st,i,in.x), instr(st,i,in.imm), instr(st,i,in.nxt), eot);
	break;	
    case OP_INSTATE:
	fprintf(f, "{instr,%d,'INSTATE',[r%d,%d,%d]}%s\n",
		i,
		instr(st,i,in.x), instr(st,i,in.imm), instr(st,i,in.nxt), eot);
	break;
    case OP_ENTER: {
	index_t mx = instr(st,i,e.mx);
	int n = instr(st,i,e.num);
	int j;
	fprintf(f, "{instr,%d,'ENTER','%s',[{n,%d}],[\n",
		i, decl_name(st, mx), n);
	i++;
	for (j = 0; j <= n; j++) // <= include leave!
	    i = csp_dump_instr(f, lev+1, st, i, (j == n) ? "" : ",");
	fprintf(f, "]}%s\n", eot);
	return i; // do not update after module block
    }
    case OP_LEAVE: {
	index_t mx = instr(st,i,v.mx);
	int n = instr(st,i,v.num);
	fprintf(f, "{instr,%d,'LEAVE','%s',[{n,%d}]}%s\n",
		i, decl_name(st,mx), n, eot);
	break;
    }
    case OP_NEW: {
	index_t ent = instr(st,i,n.ent);
	index_t obj = instr(st,i,n.obj);
	index_t mx  = decl(st,INDEX(obj),mq.mx);
	unsigned m       = decl(st,INDEX(obj),mq.m);
	fprintf(f, "{instr,%d,'NEW',\"%s\",\"%s\",[{ent,%d},{obj,%u}]}%s\n",
		i, decl_name(st, mx), decl_name(st, obj), 
		INDEX(ent), m, eot);
	break;
    }
    default:
	switch(csp_opcode_arity(instr(st,i,op))) {
	case 1:
	    fprintf(f, "{instr,%d,'%s',[r%d,r%d]}%s\n",
		    i,
		    csp_opcode_name(instr(st,i,op)),
		    instr(st,i,a.x),
		    instr(st,i,a.y),
		    eot);
	    break;	    
	case 2:
	    fprintf(f, "{instr,%d,'%s',[r%d,r%d,r%d]}%s\n",
		    i,
		    csp_opcode_name(instr(st,i,op)),
		    instr(st,i,a.x),
		    instr(st,i,a.y),
		    instr(st,i,a.z),
		    eot);
	    break;
	}
    }
    return i+1;
}

void csp_dump_var_name(FILE* f, csp_rt_t* st, index_t ix)
{
    int m = OBJ(ix);
    if ((m == GLOBAL) || (m == CURRENT)) // global    
	fprintf(f, "%s", decl_name(st, ix));
    else {
	index_t obj = st->object[m];
	fprintf(f, "%s.%s", decl_name(st, obj), decl_name(st, ix));
    }
}

void csp_dump_var(FILE* f,csp_rt_t* st,
		  char* dtype, char* suffix,
		  int m, int di,
		  int fv, csp_lang_t lang)
{
    index_t ix = MAKE_INDEX(m, di);

    switch(lang) {
    case ERLANG:
	if (!fv) fprintf(f, ",");
	fprintf(f, "{%s,\"", dtype);
	csp_dump_var_name(f, st, ix);
	fprintf(f, "%s\",", suffix);
	csp_fprint_value(f, st, decl(st,di,vt), csp_value(st, ix));
	fprintf(f, "}");
	break;
    case TEXT:
	fprintf(f, " ");
	csp_dump_var_name(f, st, ix);
	fprintf(f, "%s=", suffix);
	csp_fprint_value(f, st, decl(st,di,vt),  csp_value(st, ix));
	if (m == 0) fprintf(f, "\n");
	break;
    }
}

void csp_dump_object(FILE* f,csp_rt_t* st,int m,int fo,csp_lang_t lang)
{
    int fv, j;
    index_t obj = st->object[m];
    index_t mx  = decl(st,INDEX(obj),mq.mx);
    int     n   = decl(st,INDEX(mx),md.n);    
    
    switch(lang) {
    case ERLANG:
	if (!fo) fprintf(f, ",");
	fprintf(f, "{object,\"%s.%s\",[",
		decl_name(st, mx), decl_name(st, obj));
	break;
    case TEXT:
	fprintf(f, "%s.%s\n", decl_name(st, mx), decl_name(st, obj));
	break;
    }
    fv = 1;
    j = 1;
    while(j <= n) {
	int k = INDEX(mx)+j;
	switch(decl(st,k,type)) {
	case DECL_VARIABLE:
	    csp_dump_var(f,st,"var","",m,k,fv,lang);
	    fv = 0;
	    j++;
	    break;
	case DECL_BUFFER:   // a #buffer member: per-instance storage, like a var
	    csp_dump_var(f,st,"var","",m,k,fv,lang);
	    fv = 0;
	    j++;
	    break;
	case DECL_DIGITAL:
	    csp_dump_var(f,st,"digital","",m,k,fv,lang);
	    fv = 0;
	    j++;
	    break;
	case DECL_ANALOG:
	    csp_dump_var(f,st,"analog","",m,k,fv,lang);
	    fv = 0;
	    j++;
	    break;
	case DECL_FIELD:    // a #field member: a bit-view, reads like a value
	    csp_dump_var(f,st,"var","",m,k,fv,lang);
	    fv = 0;
	    j++;
	    break;
	case DECL_TIMER:
	    csp_dump_var(f,st,"timer","",m,k,fv,lang);
	    csp_dump_var(f,st,"var","[t0]",m,k+1,fv,lang);
	    fv = 0;
	    j += 2;
	    break;
	default:
	    j++;
	    break;
	}
    }
    switch(lang) {
    case ERLANG:
	fprintf(f, "%s", "]}\n");
	break;
    case TEXT:
	fprintf(f, "\n");    
	break;
    }    
}

void csp_dump_state(FILE* f, csp_rt_t* st, csp_lang_t lang)
{
    int i, q;
    int fo=1;

    switch(lang) {
    case ERLANG:
	fprintf(f, "{state,%d,[\n", st->cycle);
	break;
    case TEXT:
	fprintf(f, "%d:\n", st->cycle);
	break;
    }

    // global variables
    i = 0;
    while(i < st->ps.nd) {
	switch(decl(st,i,type)) {
	case DECL_MODULE:
	    // skip module decl (covered by objects)
	    i += decl(st,i,md.n) + 1;
	    break;
	case DECL_VARIABLE:
	    csp_dump_var(f,st,"var","",0,i,fo,lang);
	    fo = 0;
	    i++;
	    break;
	case DECL_BUFFER:
	    csp_dump_var(f,st,"var","",0,i,fo,lang);
	    fo = 0;
	    i++;
	    break;
	case DECL_FIELD:    // a #field: a bit-view into a buffer, reads like a value
	    csp_dump_var(f,st,"var","",0,i,fo,lang);
	    fo = 0;
	    i++;
	    break;
	case DECL_DIGITAL:
	    csp_dump_var(f,st,"digital","",0,i,fo,lang);
	    fo = 0;
	    i++;
	    break;
	case DECL_ANALOG:
	    csp_dump_var(f,st,"analog","",0,i,fo,lang);
	    fo = 0;
	    i++;
	    break;
	case DECL_TIMER:
	    csp_dump_var(f,st,"timer","",0,i,fo,lang);
	    csp_dump_var(f,st,"var","[t0]",0,i+1,fo,lang);
	    fo = 0;
	    i += 2;
	    break;
	default:
	    i++;
	    break;
	}
    }

    for (q = 1; q <= st->ps.nq; q++) {
	csp_dump_object(f,st,q,fo,lang);
	fo = 0;
    }

    switch(lang) {
    case ERLANG:
	fprintf(f, "%s", "]}.\n");
	break;
    case TEXT:
	fprintf(f, "\n");
	break;
    }
}


void csp_dump_result(FILE* f, csp_rt_t* st, index_t x, csp_lang_t lang)
{
    // x is a cycle's last instruction index, not necessarily a value leaf; only
    // read it as a value when it maps into the (now actual-sized) leaf space.
    int valid = (x != BAD_INDEX) && (st_index(st, x) < (int)st->view_cap);

    switch(lang) {
    case ERLANG:
	fprintf(f, "{result,[{cycle,%d}", st->cycle);
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
	fprintf(f, ",{num_eval0,%d}", st->num_eval0);
#endif
	if (!valid)
	    fprintf(f, ",{value,undefined}");
	else
	    fprintf(f, ",{value,%d}", csp_ivalue(st, x));
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
	fprintf(stdout, ",{num_eval_rule,%d}", st->num_eval_rule);
	fprintf(stdout, ",{num_eval0,%d}", st->num_eval0);
#endif	
	fprintf(f, "]}.\n");
	break;
    case TEXT:
	if (!valid)
	    fprintf(stdout, "result=none\n");
	else
	    fprintf(stdout, "result=%d\n", csp_ivalue(st, x));
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
	fprintf(stdout, "num_eval_rule=%d\n", st->num_eval_rule);
	fprintf(stdout, "num_eval0=%d\n", st->num_eval0);
#endif	
	break;
    }
}

index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i, char* eot)
{
    index_t ix = MAKE_INDEX(0,i);
    int vt = V_INTEGER;
    
    fprintf(f, "%s", indent(lev));
    switch(decl(st,i,type)) {
    case DECL_MODULE: {
	index_t n = decl(st,i,md.n);
	int j;
	fprintf(f, "{decl,%d,module,'%s',[\n", i, decl_name(st, ix));
	i++;
	for (j = 0; j <= n; j++) { // include 'end'
	    i = csp_dump_decl(f, lev+1, st, i, (j == n) ? "" : ",");
	}
	fprintf(f, "]}%s\n", eot);
	return i;
    }
    case DECL_END:
	fprintf(f, "{decl,%d,'end'}%s\n", i, eot);
	break;
    case DECL_OBJECT:
	fprintf(f, "{decl,%d,object,'%s','%s'}%s\n",
		i,
		decl_name(st, decl(st,i,mq.mx)),
		decl_name(st, ix), eot);
	break;
    case DECL_VARIABLE:
	vt = decl(st,i,vt);
	fprintf(f, "{decl,%d,variable,\"%s\",[{size,%d},{dir,%s},{type,%s},{init,",
		i,
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*) csp_fmt_pindir(decl(st,i,dir)),
		(char*) csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, decl(st,i,va.init));
	fprintf(f, "},{value,");
	csp_fprint_value(f, st, vt, csp_value(st, ix));
	fprintf(f, "}]}%s\n", eot);
	break;
    case DECL_CONSTANT:
	vt = decl(st,i,vt);	    
	fprintf(f, "{decl,%d,constant,\"%s\",[{size,%d},{type,%s},{init,",
		i,
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*) csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, decl(st,i,cn.init));
	fprintf(f, "},{value,");
	csp_fprint_value(f, st, vt, csp_value(st, ix));
	fprintf(f, "}]}%s\n", eot);	
	break;
    case DECL_DIGITAL:
	vt = decl(st,i,vt); // should be unsigned
	fprintf(f, "{decl,%d,digital,\"%s\",[{dir,%s},{pull,%s},{port,%d},{pin,%d}]}%s\n",
		i,
		decl_name(st, ix),
		(char*) csp_fmt_pindir(decl(st,i,dir)),
		(char*) csp_fmt_pull(st, i),
		decl(st,i,di.port),decl(st,i,di.pin),
		eot);
	break;
    case DECL_ANALOG:
	vt = decl(st,i,vt);	    
	fprintf(f,"{decl,%d,analog,\"%s\",[{size,%d},{type,%s},{dir,%s},{pwm,%s},{port,%d},{pin,%d}]}%s\n",
	       i,
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*)csp_fmt_vtype(vt),
		(char*)csp_fmt_pindir(decl(st,i,dir)),
		(char*)csp_fmt_pwm(st, i),
		decl(st,i,an.port), decl(st,i,an.pin),
		eot);
	break;
    case DECL_TIMER:
	vt = decl(st,i,vt);
	fprintf(f, "{decl,%d,timer,\"%s\",[{period,%d},{value,%d}]}%s\n",
		i,
		decl_name(st, ix),
		decl(st,i,tm.period),
		decl(st,i,tm.init),
		eot);
	break;
    case DECL_FIELD:
	vt = decl(st,i,vt);
	fprintf(f, "{decl,%d,can,\"%s\",[{size,%d},{type,%s},{endian,%s},{dir,%s},{id,16#%x},{bit,%d},{len,%d}]}%s\n",
		i,
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*)csp_fmt_vtype(vt),
		(char*)csp_fmt_endian(decl(st,i,ca.endian)),
		(char*)csp_fmt_pindir(decl(st,i,dir)),
		csp_ivalue(st, decl(st,i,ca.id)),
		decl(st,i,ca.bit),
		GET_CAN_LEN(decl(st,i,ca.len)), eot);
	break;
    case DECL_BUFFER:
	vt = decl(st,i,vt);
	// {size,N} in BYTES, matching the source syntax (bf.nbytes).
	fprintf(f, "{decl,%d,buffer,\"%s\",[{size,%d},{type,%s},{transport,%d},{id,16#%x}]}%s\n",
		i,
		decl_name(st, ix),
		decl(st,i,bf.nbytes),
		(char*)csp_fmt_vtype(vt),
		decl(st,i,bf.transport),
		(decl(st,i,bf.transport) == TR_CAN)
		    ? (unsigned)csp_ivalue(st, decl(st,i,bf.id)) : 0u,
		eot);
	break;
    default:
	break;
    }
    return i+1;
}

// Dump all parsed structures in erlang term format, to
// simplify parser validation.

void csp_dump(FILE* f, csp_rt_t* st)
{
    int i;
    int n;

    i = 0;
    while(i < st->ps.nd) 
	i = csp_dump_decl(f, 0, st, i, ".");
    i = 0;
    while(i < st->ps.nn)
	i = csp_dump_instr(f, 0, st, i, ".");
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive) {
	i = 0;
	while(i < st->ps.nd) 
	    i = csp_dump_rule(f, 0, st, i, ".");
    }
#endif
    fprintf(f, "{timer,[");
    for (i = 0; i < st->nt; i++) {
	if (i > 0) fputc(',', f);	
	csp_fprint_tag(f, st, st->timer[i]);
    }
    fprintf(f, "]}.\n");
    
    
    // One device list, printed as the two roles it is currently serving --
    // read off the DECLARED direction, which is what a listing should show. The
    // live direction can differ (a rule may have written .dir); /state answers
    // that question, this one answers what the program said.
    fprintf(f, "{input,[");
    for (i = 0, n = 0; i < st->nio; i++) {
	if (!(decl(st, INDEX(st->io[i]), dir) & DIR_IN)) continue;
	if (n++ > 0) fputc(',', f);
	csp_fprint_tag(f, st, st->io[i]);
    }
    fprintf(f, "]}.\n");

    fprintf(f, "{output,[");
    for (i = 0, n = 0; i < st->nio; i++) {
	if (!(decl(st, INDEX(st->io[i]), dir) & DIR_OUT)) continue;
	if (n++ > 0) fputc(',', f);
	csp_fprint_tag(f, st, st->io[i]);
    }
    fprintf(f, "]}.\n");

    fprintf(f, "{module,[");    
    for (i = 0; i < st->nm; i++) {
	if (i > 0) fputc(',', f);
	csp_fprint_tag(f, st, st->module[i]);
    }
    fprintf(f, "]}.\n");    

    fprintf(f, "{object,[global");
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	index_t ix = st->object[m];
	fputc(',', f);
	fprintf(f, "{'%s',%d,",
		decl_name(st, decl(st,INDEX(ix),mq.mx)),
		st->offs[m]);
	csp_fprint_tag(f, st, ix);
	fprintf(f, "}");
    }
    fprintf(f, "]}.\n");
}

extern const op_entry_t op_table[];

// 
static int maybe_unquoted_atom(char* ptr, int len)
{
    int i;
    if (len <= 0)
	return 0;
    if (!islower(ptr[0])) return 0;
    i = 1;
    while((i < len) && isalnum(ptr[i]))
	i++;
    if (i == len)
	return 1;
    return 0;
}

// print tokens in erlang term format
void csp_dump_tokens(FILE* f, token_t* tv, int n)
{
    int i;
    fprintf(f, "[");
    for (i = 0; i < n; i++) {
	switch(tv[i].t) {
	case INT: fprintf(f,"%d,", tv[i].v.val.i); break;
	case FLT: fprint_fvalue(f, tv[i].v.val.f); fprintf(f,","); break;
	case STR: fprintf(f,"\"%.*s\",", tv[i].v.str.len, tv[i].v.str.ptr); break;
	case WORD:
	    if (maybe_unquoted_atom(tv[i].v.str.ptr, tv[i].v.str.len))
		fprintf(f,"%.*s,", tv[i].v.str.len, tv[i].v.str.ptr);
	    else
		fprintf(f,"'%.*s',", tv[i].v.str.len, tv[i].v.str.ptr);
	    break;
	default:
	    if (maybe_unquoted_atom((char*)tok_table[tv[i].t].name, tok_table[tv[i].t].namelen))
		fprintf(f,"%.*s,", tok_table[tv[i].t].namelen, (char*) tok_table[tv[i].t].name);
	    else
		fprintf(f,"'%.*s',", tok_table[tv[i].t].namelen, (char*) tok_table[tv[i].t].name);
	    break;
	}
    }
    fprintf(f, "eol].\n");
    fflush(f);
}

// Dump str, decl and inst tables

const char* csp_cfmt_vtype(vtype_t vt)
{
    switch(vt) {
    case V_VOID: return "V_VOID";	
    case V_INTEGER: return "V_INTEGER";
    case V_UNSIGNED: return "V_UNSIGNED";	
    case V_FLOAT: return "V_FLOAT";
    case V_STRING: return "V_STRING";
    case V_INDEX: return "V_INDEX";
    case V_NUMBER: return "V_NUMBER";
    case V_ANY: return "V_ANY";	
    case V_TIMER: return "V_TIMER";	
    case V_DIGITAL: return "V_DIGITAL";
    case V_ANALOG: return "V_ANALOG";
    case V_FIELD: return "V_FIELD";
    default: return "UNDEFINED";
    }
}

const char* csp_cfmt_dtype(decl_t dt)
{
    switch(dt) {
    case DECL_MODULE: return "DECL_MODULE";
    case DECL_END: return "DECL_END";	
    case DECL_OBJECT: return "DECL_OBJECT";
    case DECL_CONSTANT: return "DECL_CONSTANT";
    case DECL_VARIABLE: return "DECL_VARIABLE";
    case DECL_DIGITAL: return "DECL_DIGITAL";
    case DECL_ANALOG: return "DECL_ANALOG";
    case DECL_TIMER: return "DECL_TIMER";
    case DECL_FIELD: return "DECL_FIELD";
    case DECL_BUFFER: return "DECL_BUFFER";
    case DECL_VIEW: return "DECL_VIEW";
    default: return "?";
    }
}

const char* csp_cfmt_endian(vendian_t et)
{
    switch(et) {
    case E_NATIVE: return "E_NATIVE";
    case E_LITTLE: return "E_LITTLE";
    case E_BIG: return "E_BIG";
    default: return "E_UNDEFINED";
    }
}

// dump C code 
// FIXME: struct version to check match when compile
void csp_dump_code(FILE* f, csp_rt_t* st, const csp_rom_meta_t* meta)
{
    int i;
    uint16_t crc_str_data = 0xFFFF, crc_state_data = 0xFFFF;
    uint16_t crc_decl_data = 0xFFFF, crc_instr_data = 0xFFFF;
    uint16_t decl_mark_crc = 0, instr_mark_crc = 0;
    // Every symbol this file emits is <px>_something. One image format, told
    // apart from another only by the name it answers to -- see csp_rom_meta_t.
    const char* px = (meta && meta->prefix) ? meta->prefix : "rom";
    // Section lengths as emitted, and the offsets derived from them.
    unsigned n_str_b, n_decl_b, n_instr_b, n_state_b;
    unsigned n_idg = 1, n_ofs = 1, n_edg = 1;    // stubs unless -r bakes a graph
    uint32_t SP, o_str, o_decl, o_instr, o_idg, o_ofs, o_edg, o_states, img_size;

    // Provenance banner: what this ROM is, where it came from, how big. The
    // counts are what actually goes into flash, so a glance at the top of rom.c
    // tells you the program AND its size without reading the tables.
    fprintf(f, "// Generated CandySpeak image (%s_*) -- do not edit.\n", px);
    if (meta) {
	if (meta->src)     fprintf(f, "//   source:  %s\n", meta->src);
	if (meta->version) fprintf(f, "//   version: %s\n", meta->version);
	if (meta->date)    fprintf(f, "//   built:   %s\n", meta->date);
    }
    fprintf(f, "//   size:    %d instr, %d decl, %d str, %d states\n",
	    st->ps.nn, st->ps.nd, st->ps.strp, st->ps.ns);
    fprintf(f, "\n");

    fprintf(f, "#include \"csp.h\"\n");
    // Compile-time reject: if this rom.c was generated for an older ROM format
    // than the csp.h it is now built against, fail the BUILD with a clear message
    // instead of flashing a firmware that rejects its own ROM at boot. The
    // runtime check (csp_load_rom) is the backstop for a corrupt or wrongly
    // matched flash image; this catches the far more common "forgot to
    // regenerate" at the earliest possible point.
    fprintf(f, "#if ROM_FORMAT_VERSION != %u\n", (unsigned)ROM_FORMAT_VERSION);
    fprintf(f, "#error \"%s.c is stale: generated for ROM format %u, "
	       "csp.h is newer -- regenerate with 'csp -C'\"\n",
	    px, (unsigned)ROM_FORMAT_VERSION);
    fprintf(f, "#endif\n");

    // Section byte lengths as EMITTED (trailers included), then the offsets.
    // The generator must compute these itself: crc_hdr covers them, and a CRC
    // cannot be taken over values only the C compiler knows. CSP_IMAGE_CHECK
    // below makes the compiler confirm every one of them.
    n_str_b   = st->ps.strp + 3;              // data + 0xFF sentinel + crc16
    n_decl_b  = st->ps.nd + 1;                // + DECL_END_MARK
    n_instr_b = st->ps.nn + 1;                // + OP_END_MARK
    n_state_b = st->ps.ns + 2;                // + sentinel + crc
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive) {
	n_idg = st->ps.nd;
	n_ofs = st->ps.nd + 1;
	n_edg = st->ofs[st->ps.nd] ? st->ofs[st->ps.nd] : 1;
    }
#endif
    SP       = (uint32_t)sizeof(csp_sect_t);
    o_str    = (uint32_t)sizeof(csp_image_header_t) + SP;
    o_decl   = o_str   + n_str_b   + CSP_PAD4(n_str_b)   + SP;
    o_instr  = o_decl  + n_decl_b  * 8                   + SP;
    o_idg    = o_instr + n_instr_b * 4                   + SP;
    o_ofs    = o_idg   + n_idg*2   + CSP_PAD4(2*n_idg)   + SP;
    o_edg    = o_ofs   + n_ofs*2   + CSP_PAD4(2*n_ofs)   + SP;
    o_states = o_edg   + n_edg*2   + CSP_PAD4(2*n_edg)   + SP;
    img_size = o_states+ n_state_b*2 + CSP_PAD4(2*n_state_b);

    fprintf(f, "\nCSP_IMAGE_TYPE(%s_image_t, %u,%u,%u,%u,%u,%u,%u);\n",
	    px, n_str_b, n_decl_b, n_instr_b, n_idg, n_ofs, n_edg, n_state_b);
    fprintf(f, "CSP_IMAGE_CHECK(%s_image_t, %u,%u,%u,%u,%u,%u,%u,%u);\n\n",
	    px, o_str, o_decl, o_instr, o_idg, o_ofs, o_edg, o_states, img_size);
    fprintf(f, "static const %s_image_t %s_image_data RODATA = {\n", px, px);

    // first dump string table
    // int, not char: matches the `extern const int rom_str_len` in csp_rt.c so
    // the read is 4-byte aligned (a char at an odd address read as int HardFaults
    // on Cortex-M0), and avoids signed-char overflow for tables > 127 bytes.
    // CRC over the string DATA (used for the header AND the self-CRC trailer).
    crc_str_data = csp_crc16(0xFFFF, st->ram_str, st->ps.strp, 0);
    // 3 extra bytes: a 0xFF sentinel (never a valid length or ASCII char, so it
    // marks the section end for header-free recovery) + the 2-byte CRC. See
    // rom_scan_str.
    fprintf(f, "  .s_str = { { CSP_SECT_STR }, %u },\n  .str = {\n",
	    n_str_b + CSP_PAD4(n_str_b));
    i = 0;
    while (i < st->ps.strp) {
	uint8_t n = st->ram_str[i]; // length of next string
	int j = 1;
	fprintf(f, "%d,", n);   // emit length
	i++;
	while(j <= n) {
	    int c = st->ram_str[i];
	    if (isprint(c))
		fprintf(f, "'%c',", c);
	    else if (c == 0)
		fprintf(f, "0,");
	    else
		fprintf(f, "0x%02x,", (uint8_t) c);
	    j++;
	    i++;
	    if ((i & 0xf) == 0)
		fprintf(f, "\n");
	}
    }
    fprintf(f, "\n(char)0xff,%u,%u,", crc_str_data & 0xff, (crc_str_data >> 8) & 0xff);
    fprintf(f, "},\n");

    // Canonical CRC over the decl DATA (normalization mirrors the header note
    // below: zero the runtime-scratch fields the emitter does not write). Used
    // BOTH for the header's crc_decl and to seed the DECL_END_MARK self-CRC, so
    // the fold lives in one place.
    crc_decl_data = 0xFFFF;
    {
	index_t di;
	for (di = 0; di < st->ps.nd; di++) {
	    csp_decl_t d = csp_get_decl(st, di);
	    d.is_mapped = 0; d.bound = 0; d.reg = 0;
	    if (d.type == DECL_TIMER) { d.tm.fired=0; d.tm.running=0; d.tm._res=0; }
	    crc_decl_data = csp_crc16(crc_decl_data, &d, sizeof(d), 0);
	}
    }
    // DECL_END_MARK self-CRC = over [data + this marker with crc zeroed], so the
    // section verifies without the header (rom_scan_end).
    {
	csp_decl_t dm = {0};
	dm.type = DECL_END_MARK;
	decl_mark_crc = csp_crc16(crc_decl_data, &dm, sizeof(dm), 0);
    }

    // now dump declarations -- one extra entry: the DECL_END_MARK terminator.
    fprintf(f, "  .s_decl = { { CSP_SECT_DECL }, %u },\n  .decl = {\n",
	    n_decl_b * 8);
    for (i = 0; i < st->ps.nd; i++) {
	// csp_decl_t is a UNION whose every arm begins with DECL_COMMON, so the
	// common fields MUST be written inside the same arm designator as the
	// type-specific fields -- a trailing ".va={..}" would otherwise clobber
	// (zero) the common fields set before it (last union initializer wins).
	csp_decl_t dv = csp_get_decl(st, i);  // RAM decls grow down: use the accessor
	csp_decl_t* dp = &dv;
	char cmn[128];
	snprintf(cmn, sizeof(cmn), ".type=%s,.dir=%u,.name=%u,.vt=%s,.res=%u",
		 csp_cfmt_dtype(dp->type), dp->dir, dp->name,
		 csp_cfmt_vtype(dp->vt), dp->res);
	switch(dp->type) {
	case DECL_MODULE:
	    fprintf(f, "  {.md={%s,.n=%u,.ent=%u}},\n",
		    cmn, dp->md.n, dp->md.ent);
	    break;
	case DECL_OBJECT:
	    fprintf(f, "  {.mq={%s,.mx=%u,.m=%u}},\n",
		    cmn, dp->mq.mx, dp->mq.m);
	    break;
	case DECL_VARIABLE:
	    fprintf(f, "  {.va={%s,.init={.u=%u}}},\n", cmn, dp->va.init.u);
	    break;
	case DECL_CONSTANT:
	    fprintf(f, "  {.cn={%s,.init={.u=%u}}},\n", cmn, dp->cn.init.u);
	    break;
	case DECL_DIGITAL:
	    fprintf(f, "  {.di={%s,.pin=%u,.port=%u,.pullup=%u,.pulldown=%u}},\n",
		    cmn, dp->di.pin, dp->di.port, dp->di.pullup, dp->di.pulldown);
	    break;
	case DECL_ANALOG:
	    fprintf(f, "  {.an={%s,.pin=%u,.port=%u,.pwm=%u,.endian=%u}},\n",
		    cmn, dp->an.pin, dp->an.port, dp->an.pwm, dp->an.endian);
	    break;
	case DECL_FIELD:
	    fprintf(f, "  {.ca={%s,.id=%u,.endian=%u,.bit=%u,.len=%u}},\n",
		    cmn, dp->ca.id, dp->ca.endian, dp->ca.bit, dp->ca.len);
	    break;
	case DECL_TIMER:
	    fprintf(f, "  {.tm={%s,.period=%u,.init=%u}},\n",
		    cmn, (unsigned)dp->tm.period, dp->tm.init);
	    break;
	case DECL_BUFFER:
	    // The buffer's SIZE (nbytes) lives here, not in cmn.res -- omitting it
	    // baked a zero-length buffer AND made crc_decl mismatch (the fold sees
	    // the real nbytes, the emit wrote 0) -> "CRC mismatch in decl section".
	    fprintf(f, "  {.bf={%s,.nbytes=%u,.transport=%u,.id=%u}},\n",
		    cmn, dp->bf.nbytes, dp->bf.transport, dp->bf.id);
	    break;
	case DECL_END:    // common fields only (anonymous union arm)
	case DECL_VIEW:   // synthetic; emitted as common only
	case DECL_STATES:
	case DECL_IN:
	case DECL_NONE:
	default:
	    fprintf(f, "  {%s},\n", cmn);
	    break;
	}
    }
    fprintf(f, "  {.em={.type=DECL_END_MARK,.crc=%u,._res=0}},\n", decl_mark_crc);
    fprintf(f, "  },\n");

    // Instruction section CRC + OP_END_MARK self-CRC (no canonicalization -- the
    // emitted instrs are byte-for-byte st->ram_instr).
    crc_instr_data = csp_crc16(0xFFFF, st->ram_instr,
			       (size_t)st->ps.nn * sizeof(csp_instr_t), 0);
    {
	csp_instr_t im = {0};
	im.op = OP_END_MARK;
	instr_mark_crc = csp_crc16(crc_instr_data, &im, sizeof(im), 0);
    }

    // and then dump instructions -- one extra entry: the OP_END_MARK terminator.
    fprintf(f, "  .s_instr = { { CSP_SECT_INSTR }, %u },\n  .instr = {\n",
	    n_instr_b * 4);
    for (i = 0; i < st->ps.nn; i++) {
	// csp_instr_t is a UNION whose every arm begins with INSTR_COMMON (op),
	// so .op MUST be written inside the same arm designator -- a leading
	// ".op=..,.m={..}" would let the .m arm clobber (zero) op to OP_NOP.
	csp_instr_t* ip = &st->ram_instr[i];
	char op[24];
	snprintf(op, sizeof(op), ".op=OP_%s", csp_opcode_name(ip->op));
	switch(ip->op) {
	    // FIXME: OP_ENTER/OP_LEAVE could share format?
	case OP_ENTER:
	    fprintf(f, "  {.e={%s,.num=%u,.mx=%u}},\n", op, ip->e.num, ip->e.mx);
	    break;
	case OP_LEAVE:
	    fprintf(f, "  {.v={%s,.num=%u,.mx=%u}},\n", op, ip->v.num, ip->v.mx);
	    break;
	case OP_NEW:
	    fprintf(f, "  {.n={%s,.ent=%u,.obj=%u}},\n", op, ip->n.ent, ip->n.obj);
	    break;
	case OP_LI:
	case OP_LIU:
	case OP_LIH:
	case OP_ARG:
	    fprintf(f, "  {.i={%s,.x=%u,.imm=%d}},\n", op, ip->i.x, ip->i.imm);
	    break;
	case OP_ST:
	case OP_STIMP:
	case OP_CHG:
	case OP_LD:
	    fprintf(f, "  {.m={%s,.x=%u,.mem=%u}},\n", op, ip->m.x, ip->m.mem);
	    break;
	case OP_STP:
	case OP_LDP:
	    fprintf(f, "  {.m={%s,.x=%u,.mem=%u,.y=%u}},\n",
		    op, ip->m.x, ip->m.mem, ip->m.y);
	    break;
	case OP_STI:
	case OP_EQI:
	    fprintf(f, "  {.mi={%s,.x=%u,.mem=%u,.imm=%d}},\n",
		    op, ip->mi.x, ip->mi.mem, ip->mi.imm);
	    break;
	case OP_CALL:
	    fprintf(f, "  {.f={%s,.x=%u,.idx=%u,.usr=%u,.avt=0x%04x}},\n",
		    op, ip->f.x, ip->f.idx, ip->f.usr, ip->f.avt);
	    break;
	case OP_NEXT:
	    fprintf(f, "  {.x={%s,.x=%u}},\n", op, ip->x.x);
	    break;
	case OP_NINSTATE:	    
	case OP_INSTATE:
	    fprintf(f, "  {.in={%s,.x=%u,.imm=%d,.nxt=%d}},\n",
		    op, ip->in.x, ip->in.imm, ip->in.nxt);
	    break;
	case OP_RULE:
	    fprintf(f, "  {.r={%s,.cnd=%u,.nxt=%d}},\n", op, ip->r.cnd, ip->r.nxt);
	    break;
	default: // two/three-address-instruction
	    if (op_info[ip->op].arity == 1)
		fprintf(f, "  {.a={%s,.x=%u,.y=%u}},\n", op, ip->a.x, ip->a.y);
	    else if (op_info[ip->op].arity == 2)
		fprintf(f, "  {.a={%s,.x=%u,.y=%u,.z=%u}},\n",
			op, ip->a.x, ip->a.y, ip->a.z);
	    else
		fprintf(f, "  {%s},\n", op);  // no operands (NOP etc)
	    break;
	}
    }
    fprintf(f, "  {.em={.op=OP_END_MARK,.crc=%u,._res=0}},\n", instr_mark_crc);
    fprintf(f, "  },\n");

    // Reactive dependency graph: maps each ROM decl -> the ROM rules that read
    // it, so firmware runs reactively without rebuilding its graph in RAM
    // (compiled with -r). Indices match the ROM segment 1:1 (compiled at base
    // 0). The three sections are ALWAYS present -- the image type has a slot for
    // each -- with n_edg=0 and stub arrays when there is no graph.
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive) {
	int nd = st->ps.nd;
	int nedg = st->ofs[nd];
	fprintf(f, "  .s_idg = { { CSP_SECT_IDG }, %u },\n  .idg = {",
		n_idg*2 + CSP_PAD4(2*n_idg));
	for (i = 0; i < nd; i++) fprintf(f, "%u,", st->idg[i]);
	fprintf(f, "},\n");
	fprintf(f, "  .s_ofs = { { CSP_SECT_OFS }, %u },\n  .ofs = {",
		n_ofs*2 + CSP_PAD4(2*n_ofs));
	for (i = 0; i <= nd; i++) fprintf(f, "%u,", st->ofs[i]);
	fprintf(f, "},\n");
	fprintf(f, "  .s_edg = { { CSP_SECT_EDG }, %u },\n  .edg = {",
		n_edg*2 + CSP_PAD4(2*n_edg));
	for (i = 0; i < nedg; i++) fprintf(f, "%u,", st->edg[i]);
	if (!nedg) fprintf(f, "0");   // avoid a zero-length array
	fprintf(f, "},\n");
    }
    else
#endif
    {
	// No graph: stub sections (all reads gated by n_edg == 0).
	fprintf(f, "  .s_idg = { { CSP_SECT_IDG }, %u },\n  .idg = {0},\n",
		n_idg*2 + CSP_PAD4(2*n_idg));
	fprintf(f, "  .s_ofs = { { CSP_SECT_OFS }, %u },\n  .ofs = {0},\n",
		n_ofs*2 + CSP_PAD4(2*n_ofs));
	fprintf(f, "  .s_edg = { { CSP_SECT_EDG }, %u },\n  .edg = {0},\n",
		n_edg*2 + CSP_PAD4(2*n_edg));
    }

    // State table (name offset -> state number). csp_load_image copies this back
    // into st->states so baked user states resolve. Name offsets index str.
    crc_state_data = csp_crc16(0xFFFF, st->states,
			       (size_t)st->ps.ns * sizeof(state_t), 0);
    // state self-CRC trailer: a sentinel (snum 0x7f -- never a real state, snums
    // are 0..MAX_STATES-1) marks the section end for header-free recovery, and the
    // next state_t packs the 16-bit CRC across its name(9)+snum(7). rom_scan_state.
    fprintf(f, "  .s_states = { { CSP_SECT_STATES }, %u },\n  .states = {",
	    n_state_b*2 + CSP_PAD4(2*n_state_b));
    for (i = 0; i < st->ps.ns; i++)
	fprintf(f, "{.name=%u,.snum=%u},",
		st->states[i].name, st->states[i].snum);
    fprintf(f, "{.name=0,.snum=0x7f},");                       // sentinel
    fprintf(f, "{.name=%u,.snum=%u},",                         // crc: name|snum<<9
	    crc_state_data & 0x1ff, (crc_state_data >> 9) & 0x7f);
    fprintf(f, "},\n");

    // The image header LAST: counts + per-section CRCs, so csp_load_rom can
    // reject a stale or corrupt generate and name the bad section. Each section
    // CRC folds the SAME bytes this file emits, byte-for-byte what a
    // little-endian target reads back (see rom_verify). crc_hdr covers the whole
    // header up to itself.
    {
	csp_image_header_t h;
	int nedg = 0;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive) nedg = st->ofs[st->ps.nd];
#endif
	h.version = ROM_FORMAT_VERSION;
	h.n_str = st->ps.strp; h.n_decl = st->ps.nd; h.n_instr = st->ps.nn;
	h.n_edg = nedg;        h.n_state = st->ps.ns;

	// crc over the str/decl/instr DATA -- precomputed above (same folds that
	// seed the section self-CRCs), so header and section never disagree.
	h.crc_str = crc_str_data;
	h.crc_decl = crc_decl_data;
	h.crc_instr = crc_instr_data;
	h.crc_state = crc_state_data;   // precomputed above (seeds the trailer too)
	// Graph CRC folds the same three arrays rom_verify reads back, in the same
	// order and with the same sizes the emission above used: idg[nd], ofs[nd+1],
	// edg[nedg]. Only when a graph exists (nedg > 0); else baked 0 and skipped.
	h.crc_graph = 0;
	if (nedg > 0) {
	    h.crc_graph = csp_crc16(0xFFFF, st->idg,
				    (size_t)st->ps.nd * sizeof(index_t), 0);
	    h.crc_graph = csp_crc16(h.crc_graph, st->ofs,
				    (size_t)(st->ps.nd + 1) * sizeof(index_t), 0);
	    h.crc_graph = csp_crc16(h.crc_graph, st->edg,
				    (size_t)nedg * sizeof(index_t), 0);
	}
	h.crc_hdr = csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0);

	h.magic[0] = CSP_IMAGE_MAGIC0; h.magic[1] = CSP_IMAGE_MAGIC1;
	h.magic[2] = CSP_IMAGE_MAGIC2; h.magic[3] = CSP_IMAGE_MAGIC3;
	h.size = img_size;
	h.role = (meta && meta->role) ? meta->role : CSP_ROLE_ROM;
	h.generation = (meta) ? meta->generation : 0;
	h.ofs_str = o_str;   h.ofs_decl = o_decl; h.ofs_instr = o_instr;
	h.ofs_idg = o_idg;   h.ofs_ofs = o_ofs;   h.ofs_edg = o_edg;
	h.ofs_states = o_states;
	// LAST, and over every byte above it -- magic, size, role, generation,
	// counts, section CRCs AND the offsets. Computed after all of them.
	h.crc_hdr = csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0);

	// Designated initializers may appear in any order, so the header is
	// emitted last (it needs every section CRC) but lands first in the object.
	fprintf(f, "  .hdr = {\n"
		   "    .magic = { CSP_IMAGE_MAGIC0, CSP_IMAGE_MAGIC1,"
		   " CSP_IMAGE_MAGIC2, CSP_IMAGE_MAGIC3 },\n"
		   "    .size=%u, .version=%u, .role=%u, .generation=%u,\n"
		   "    .n_str=%u, .n_decl=%u, .n_instr=%u, .n_edg=%u,"
		   " .n_state=%u,\n"
		   "    .crc_str=%u, .crc_decl=%u, .crc_instr=%u,"
		   " .crc_state=%u, .crc_graph=%u,\n"
		   "    .ofs_str=%u, .ofs_decl=%u, .ofs_instr=%u,"
		   " .ofs_idg=%u,\n"
		   "    .ofs_ofs=%u, .ofs_edg=%u, .ofs_states=%u,\n"
		   "    .crc_hdr=%u }\n};\n",
		h.size, h.version, h.role, h.generation,
		h.n_str, h.n_decl, h.n_instr, h.n_edg, h.n_state,
		h.crc_str, h.crc_decl, h.crc_instr, h.crc_state, h.crc_graph,
		h.ofs_str, h.ofs_decl, h.ofs_instr, h.ofs_idg,
		h.ofs_ofs, h.ofs_edg, h.ofs_states, h.crc_hdr);

	// The handle the runtime takes: just the base. It never names the image's
	// struct type -- that type is generated per program -- it works in offsets
	// from here.
	fprintf(f, "const csp_image_ref_t %s_image RODATA = "
		   "{ (const uint8_t*)&%s_image_data };\n", px, px);
	// Announce it to the linker, so a firmware carrying several images can
	// enumerate them without knowing any of their names.
	fprintf(f, "CSP_REGISTER_IMAGE(%s_image_data);\n", px);
    }
}

// list declarations

index_t csp_list_decl(FILE* f, csp_rt_t* st, int i)
{
    index_t ix = MAKE_INDEX(0,i);
    int vt = V_INTEGER;
    
    switch(decl(st,i,type)) {
    case DECL_MODULE:
	fprintf(f, "#module %s\n", decl_name(st, ix));
	break;
    case DECL_END:
	fprintf(f, "#end\n");
	break;
    case DECL_OBJECT:
	fprintf(f, "#%s %s\n",
		decl_name(st, decl(st,i,mq.mx)),
		decl_name(st, ix));
	break;
    case DECL_VARIABLE:
	vt = decl(st,i,vt);
	fprintf(f, "#variable %s:%d %s %s = ", // show init value
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*)csp_fmt_pindir(decl(st,i,dir)),
		(char*)csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, decl(st,i,va.init));
	fprintf(f, "\n");
	break;
    case DECL_CONSTANT:
	vt = decl(st,i,vt);	    
	fprintf(f, "#constant %s:%d %s = ",
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*)csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, decl(st,i,cn.init));
	fprintf(f, "\n");	
	break;
    case DECL_DIGITAL:
	vt = decl(st,i,vt); // should be unsigned
	fprintf(f, "#digital %s %s %s %d:%d\n",
		decl_name(st, ix),
		(char*)csp_fmt_pindir(decl(st,i,dir)),
		(char*)csp_fmt_pull(st, i),
		decl(st,i,di.port),decl(st,i,di.pin));
	break;
    case DECL_ANALOG:
	vt = decl(st,i,vt);
	fprintf(f,"#analog %s:%d %s %s %s %d:%d\n",
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*)csp_fmt_vtype(vt),
		(char*)csp_fmt_pindir(decl(st,i,dir)),
		(char*)csp_fmt_pwm(st, i),
		decl(st,i,an.port), decl(st,i,an.pin));
	break;
    case DECL_TIMER:
	vt = decl(st,i,vt);
	fprintf(f, "#timer %s %d = %d\n",
		decl_name(st, ix),
		decl(st,i,tm.period),
		decl(st,i,tm.init));
	break;
    case DECL_FIELD:
	vt = decl(st,i,vt);
	fprintf(f, "#field %s:%d %s %s %s 0x%x[%d:%d]\n",
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		(char*)csp_fmt_vtype(vt),
		(char*)csp_fmt_endian(decl(st,i,ca.endian)),
		(char*)csp_fmt_pindir(decl(st,i,dir)),
		csp_ivalue(st, decl(st,i,ca.id)),
		decl(st,i,ca.bit),
		decl(st,i,ca.bit) + GET_CAN_LEN(decl(st,i,ca.len)));
	break;
    case DECL_BUFFER:
	// #buffer <name>:<size> [dir] [can 0x<id>]. Size is BYTES (bf.nbytes)
	// -- matching the board lister and the parser.
	fprintf(f, "#buffer %s:%d",
		decl_name(st, ix),
		decl(st,i,bf.nbytes));
	if (decl(st,i,dir))
	    fprintf(f, " %s", (char*)csp_fmt_pindir(decl(st,i,dir)));
	if (decl(st,i,bf.transport) == TR_CAN)
	    fprintf(f, " can 0x%x", (unsigned)csp_ivalue(st, decl(st,i,bf.id)));
	fprintf(f, "\n");
	break;
    default:
	break;
    }
    return i+1;
}

void csp_list_declarations(FILE* f, csp_rt_t* st)
{
    int i = 0;
    while(i < st->ps.nd)
	i = csp_list_decl(f, st, i);
}



// rules are generate as code
// here we try to reverse engineer the rules, is there a better way?
//

int csp_list_rule(csp_rt_t* st, int i)
{
    { int r = csp_print_rule(st, i); csp_println(); return r; }
}

// Name string position of the state numbered snum (0 if none).
static sindex_t list_state_name_pos(csp_rt_t* st, int snum)
{
    int s;
    for (s = 0; s < st->ps.ns; s++)
	if (st->states[s].snum == snum)
	    return st->states[s].name;
    return 0;
}

// Reconstruct source-shaped output: #module/#in blocks are recovered from the
// OP_ENTER/OP_LEAVE and OP_INSTATE markers, rules are indented within them, and
// the per-rule State==S gate is suppressed (implied by the #in header).
void csp_list_rules(FILE* f, csp_rt_t* st)
{
    int i = 0;
    int lev = 0;
    int block_end = -1;
    void* savef = csp_set_file_output(f);

    while (i < st->ps.nn) {
	opcode_t op;
	if ((block_end >= 0) && (i >= block_end)) {   // close finished #in block
	    if (lev > 0) lev--;
	    fprintf(f, "%s#end\n", indent(lev));
	    block_end = -1;
	    st->list_state = -1;
	    st->list_nstate = 0;
	}
	op = instr(st, i, op);
	if (op == OP_ENTER) {
	    fprintf(f, "%s#module %s\n", indent(lev), decl_name(st, instr(st,i,e.mx)));
	    lev++;
	    i++;
	    continue;
	}
	if (op == OP_LEAVE) {
	    if (lev > 0) lev--;
	    fprintf(f, "%s#end\n", indent(lev));
	    i++;
	    continue;
	}
	// A block gate: LD State ; NINSTATE* ; INSTATE (open_in_block). Emit
	// `#in <states>` and set list_states so each rule's State guard drops
	// under the header. The whole gate is consumed, never listed as a rule.
	if ((op == OP_LD) && (i+1 < st->ps.nn) &&
	    ((instr(st, i+1, op) == OP_NINSTATE) ||
	     (instr(st, i+1, op) == OP_INSTATE))) {
	    int j = i + 1, ns = 0, k;
	    fprintf(f, "%s#in", indent(lev));
	    while ((j < st->ps.nn) && (instr(st, j, op) == OP_NINSTATE)) {
		if (ns < MAX_IN_STATES) st->list_states[ns++] = instr(st,j,in.imm);
		j++;
	    }
	    if (ns < MAX_IN_STATES) st->list_states[ns++] = instr(st,j,in.imm);
	    st->list_nstate = ns;
	    for (k = 0; k < ns; k++) {
		sindex_t np = list_state_name_pos(st, st->list_states[k]);
		fprintf(f, " %s", np ? csp_str_at(st, np) : "?");
	    }
	    fprintf(f, "\n");
	    block_end = j + instr(st, j, in.nxt);
	    lev++;
	    i = j + 1;
	    continue;
	}
	if ((op == OP_NEW) || (op == OP_NOP)) {
	    i++;
	    continue;
	}
	fprintf(f, "%s", indent(lev));   // a rule starts here
	i = csp_print_rule(st, i);
	csp_println();
    }
    if (block_end >= 0) {   // block runs to the very end
	if (lev > 0) lev--;
	fprintf(f, "%s#end\n", indent(lev));
    }
    st->list_state = -1;
    csp_set_file_output(savef);
}
