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
    int32_t intpart = FIX_TO_INT(v);
    uint32_t fracpart = (v >= 0 ? v : -v) & FIX_MASK;
    // Use 64-bit to avoid overflow: fracpart * 1000000 can exceed 32 bits
    fracpart = (uint32_t)(((uint64_t)fracpart * 1000000) >> FIX_SHIFT);
    if (v < 0 && intpart == 0)
	fprintf(f, "-0.%06u", fracpart);
    else
	fprintf(f, "%d.%06u", intpart, fracpart);
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
    case DECL_CAN:
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
    switch(lang) {
    case ERLANG:
	fprintf(f, "{result,[{cycle,%d}", st->cycle);
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
	fprintf(f, ",{num_eval0,%d}", st->num_eval0);
#endif
	if (x == BAD_INDEX)
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
	if (x == BAD_INDEX)
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
		csp_fmt_pindir(decl(st,i,dir)),
		csp_fmt_vtype(vt));
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
		csp_fmt_vtype(vt));
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
		csp_fmt_pindir(decl(st,i,dir)),
		csp_fmt_pull(st, i),
		decl(st,i,di.port),decl(st,i,di.pin),
		eot);
	break;
    case DECL_ANALOG:
	vt = decl(st,i,vt);	    
	fprintf(f,"{decl,%d,analog,\"%s\",[{size,%d},{type,%s},{dir,%s},{pwm,%s},{port,%d},{pin,%d}]}%s\n",
	       i,
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		csp_fmt_vtype(vt),
		csp_fmt_pindir(decl(st,i,dir)),
		csp_fmt_pwm(st, i),
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
    case DECL_CAN:
	vt = decl(st,i,vt);
	fprintf(f, "{decl,%d,can,\"%s\",[{size,%d},{type,%s},{endian,%s},{dir,%s},{id,16#%x},{bit,%d},{len,%d}]}%s\n",
		i,
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		csp_fmt_vtype(vt),
		csp_fmt_endian(decl(st,i,ca.endian)),
		csp_fmt_pindir(decl(st,i,dir)),
		csp_ivalue(st, decl(st,i,ca.id)),
		decl(st,i,ca.bit),
		GET_CAN_LEN(decl(st,i,ca.len)), eot);
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
    
    
    fprintf(f, "{input,[");
    for (i = 0; i < st->ni; i++) {
	if (i > 0) fputc(',', f);	
	csp_fprint_tag(f, st, st->input[i]);
    }
    fprintf(f, "]}.\n");

    fprintf(f, "{output,[");
    for (i = 0; i < st->no; i++) {
	if (i > 0) fputc(',', f);
	csp_fprint_tag(f, st, st->output[i]);
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
		fprintf(f,"%.*s,", tok_table[tv[i].t].namelen, tok_table[tv[i].t].name);
	    else
		fprintf(f,"'%.*s',", tok_table[tv[i].t].namelen, tok_table[tv[i].t].name);
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
    case V_CAN: return "V_CAN";
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
    case DECL_CAN: return "DECL_CAN";
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
void csp_dump_code(FILE* f, csp_rt_t* st)
{
    int i;

    fprintf(f, "#include \"csp.h\"\n");
    // first dump string table
    fprintf(f, "const char rom_str_len RODATA = %d;\n", st->ps.strp);
    fprintf(f, "const char rom_str[%d] RODATA = {\n", st->ps.strp);
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
    fprintf(f, "};\n");

    // now dump declatrations
    fprintf(f, "const int rom_n_decl RODATA = %d;\n", st->ps.nd);
    fprintf(f, "const csp_decl_t rom_decl[%d] RODATA = {\n", st->ps.nd);
    for (i = 0; i < st->ps.nd; i++) {
	// csp_decl_t is a UNION whose every arm begins with DECL_COMMON, so the
	// common fields MUST be written inside the same arm designator as the
	// type-specific fields -- a trailing ".va={..}" would otherwise clobber
	// (zero) the common fields set before it (last union initializer wins).
	csp_decl_t* dp = &st->ram_decl[i];
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
	case DECL_CAN:
	    fprintf(f, "  {.ca={%s,.id=%u,.endian=%u,.bit=%u,.len=%u}},\n",
		    cmn, dp->ca.id, dp->ca.endian, dp->ca.bit, dp->ca.len);
	    break;
	case DECL_TIMER:
	    fprintf(f, "  {.tm={%s,.period=%u,.init=%u}},\n",
		    cmn, (unsigned)dp->tm.period, dp->tm.init);
	    break;
	case DECL_END:    // common fields only (anonymous union arm)
	case DECL_BUFFER: // no extra union fields (res/vt/dir already in cmn)
	case DECL_VIEW:   // synthetic; emitted as common only
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
	case DECL_STATES:
	case DECL_IN:
#endif
	case DECL_NONE:
	default:
	    fprintf(f, "  {%s},\n", cmn);
	    break;
	}
    }
    fprintf(f, "};\n");

    // and then dump instructions
    fprintf(f, "const int rom_n_instr RODATA = %d;\n", st->ps.nn);
    fprintf(f, "const csp_instr_t rom_instr[%d] RODATA = {\n", st->ps.nn);
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
    fprintf(f, "};\n");

    // Reactive dependency graph (only when built -- compiled with -r). Maps each
    // ROM decl -> the ROM rules that read it. Consumed at runtime by
    // csp_enq_elist so firmware runs reactively without rebuilding its graph in
    // RAM. Indices match the ROM segment 1:1 (compiled at base 0).
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive) {
	int nd = st->ps.nd;
	int nedg = st->ofs[nd];
	fprintf(f, "const int rom_n_edg RODATA = %d;\n", nedg);
	fprintf(f, "const index_t rom_idg[%d] RODATA = {", nd);
	for (i = 0; i < nd; i++) fprintf(f, "%u,", st->idg[i]);
	fprintf(f, "};\n");
	fprintf(f, "const index_t rom_ofs[%d] RODATA = {", nd+1);
	for (i = 0; i <= nd; i++) fprintf(f, "%u,", st->ofs[i]);
	fprintf(f, "};\n");
	fprintf(f, "const index_t rom_edg[%d] RODATA = {", nedg ? nedg : 1);
	for (i = 0; i < nedg; i++) fprintf(f, "%u,", st->edg[i]);
	if (!nedg) fprintf(f, "0");   // avoid a zero-length array
	fprintf(f, "};\n");
    }
#endif
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
		csp_fmt_pindir(decl(st,i,dir)),
		csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, decl(st,i,va.init));
	fprintf(f, "\n");
	break;
    case DECL_CONSTANT:
	vt = decl(st,i,vt);	    
	fprintf(f, "#constant %s:%d %s = ",
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, decl(st,i,cn.init));
	fprintf(f, "\n");	
	break;
    case DECL_DIGITAL:
	vt = decl(st,i,vt); // should be unsigned
	fprintf(f, "#digital %s %s %s %d:%d\n",
		decl_name(st, ix),
		csp_fmt_pindir(decl(st,i,dir)),
		csp_fmt_pull(st, i),
		decl(st,i,di.port),decl(st,i,di.pin));
	break;
    case DECL_ANALOG:
	vt = decl(st,i,vt);
	fprintf(f,"#analog %s:%d %s %s %s %d:%d\n",
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		csp_fmt_vtype(vt),
		csp_fmt_pindir(decl(st,i,dir)),
		csp_fmt_pwm(st, i),
		decl(st,i,an.port), decl(st,i,an.pin));
	break;
    case DECL_TIMER:
	vt = decl(st,i,vt);
	fprintf(f, "#timer %s %d = %d\n",
		decl_name(st, ix),
		decl(st,i,tm.period),
		decl(st,i,tm.init));
	break;
    case DECL_CAN:
	vt = decl(st,i,vt);
	fprintf(f, "#can %s:%d %s %s %s 0x%x[%d:%d]\n",
		decl_name(st, ix),
		GET_RES(decl(st,i,res)),
		csp_fmt_vtype(vt),
		csp_fmt_endian(decl(st,i,ca.endian)),
		csp_fmt_pindir(decl(st,i,dir)),
		csp_ivalue(st, decl(st,i,ca.id)),
		decl(st,i,ca.bit),
		decl(st,i,ca.bit) + GET_CAN_LEN(decl(st,i,ca.len)));
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
    return csp_print_rule(st, i);
}

void csp_list_rules(FILE* f, csp_rt_t* st)
{
    int i = 0;
    void* savef = csp_set_file_output(f);
    while(i < st->ps.nn) {
	fprintf(f, "RULE-%d\n", i);
	i = csp_list_rule(st, i);
    }
    csp_set_file_output(savef);
}
