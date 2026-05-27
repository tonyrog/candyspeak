// Dump functions for debugging and inspection
// but also generate C code for builtin eeprom code
//
#include <stdio.h>
#include <ctype.h>
#include "csp.h"
#include "csp_dump.h"
#include "csp_format.h"

extern const op_entry_t op_table[];

int csp_opcode_to_tok(opcode_t opcode)
{
    int t = 0;

    switch(opcode) {
    case OP_FADD: return PLUS;
    case OP_FSUB: return MINUS;
    case OP_FMUL: return ASTERISK;
    case OP_FDIV: return SLASH;
    case OP_FNEG: return MINUS1;
    case OP_FLT:  return LT;
    case OP_FLTE: return LTEQ;
    case OP_FGT:  return GT;
    case OP_FGTE: return GTEQ;
    case OP_FEQEQ: return EQEQ;
    case OP_FNEQ:  return NEQ;
    case OP_NEXT: return  NEXT;	
    case OP_ENTER: return ENTER;
    case OP_LEAVE: return LEAVE;
    case OP_NEW: return NEW;
    default: break;
    }
    while(op_table[t].tok != LAST) {
	if ((op_table[t].arity >= 0) &&
	    (op_table[t].code == opcode))
	    return op_table[t].tok;
	t++;
    }
    return -1;
}

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
    case V_INTEGER: fprintf(f, "%d", val.i); break;
    case V_UNSIGNED: fprintf(f, "16#%x", val.u); break;
    case V_FLOAT: fprint_fvalue(f, val.f); break;
    case V_STRING:
	csp_fprint_escaped_string(f, &st->str[val.s], st->str[val.s-1]);
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
    switch(st->decl[i].type) {
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
    switch(st->instr[i].op) {
    case OP_NOP:
	fprintf(f, "{instr,%d,'NOP'}%s\n",
		i, eot);
	break;
    case OP_NEXT:
	fprintf(f, "{instr,%d,'NEXT',[r%d]}%s\n",
		i, st->instr[i].x.x, eot);
	break;
    case OP_LD:
	fprintf(f, "{instr,%d,'LD',[r%d,",
		i,
		st->instr[i].m.x);
	csp_fprint_tag(f, st, st->instr[i].m.mem);
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_ST:
	fprintf(f, "{instr,%d,'ST',[r%d,",
		i, st->instr[i].m.x);
	csp_fprint_tag(f, st, st->instr[i].m.mem);
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_STIMP:
	fprintf(f, "{instr,%d,'STIMP',[r%d,",
		i, st->instr[i].m.x);
	csp_fprint_tag(f, st, st->instr[i].m.mem);
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_CHG:
	fprintf(f, "{instr,%d,'CHG',[r%d,",
		i, st->instr[i].m.x);
	csp_fprint_tag(f, st, st->instr[i].m.mem);
	fprintf(f, "]}%s\n", eot);
	break;
    case OP_LI:
	fprintf(f, "{instr,%d,'LI',[r%d,%d]}%s\n",
		i,
		st->instr[i].i.x,
		st->instr[i].i.imm,
		eot);
	break;
    case OP_LIU:
	fprintf(f, "{instr,%d,'LIU',[r%d,%u]}%s\n",
		i,
		st->instr[i].i.x,
		(uint16_t)st->instr[i].i.imm,
		eot);
	break;
    case OP_LIH:
	fprintf(f, "{instr,%d,'LIH',[r%d,16#%04x]}%s\n",
		i,
		st->instr[i].i.x,
		(uint16_t)st->instr[i].i.imm,
		eot);
	break;
    case OP_ARG:
	fprintf(f, "{instr,%d,'ARG',[r%d,%d]}%s\n",
		i,
		st->instr[i].i.x,
		st->instr[i].i.imm,
		eot);
	break;
    case OP_CALL:
	fprintf(f, "{instr,%d,'CALL',[r%d,%s,16#%04x]}%s\n",
		i,
		st->instr[i].f.x,
		(st->instr[i].f.usr ?
		 st->ufuncs[st->instr[i].f.idx].name :
		 csp_builtin_funcs[st->instr[i].f.idx].name),
		st->instr[i].f.avt,	
		eot);
	break;
    case OP_RULE:
	fprintf(f, "{instr,%d,'RULE',[r%d,%d]}%s\n",
		i,
		st->instr[i].r.cnd, st->instr[i].r.nxt, eot);
	break;
    case OP_ENTER: {
	index_t mx = st->instr[i].e.mx;
	int n = st->instr[i].e.num;
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
	index_t mx = st->instr[i].v.mx;
	int n = st->instr[i].v.num;
	fprintf(f, "{instr,%d,'LEAVE','%s',[{n,%d}]}%s\n",
		i, decl_name(st,mx), n, eot);
	break;
    }
    case OP_NEW: {
	index_t ent = st->instr[i].n.ent;
	index_t obj = st->instr[i].n.obj;
	index_t mx  = st->decl[INDEX(obj)].mq.mx;
	unsigned m       = st->decl[INDEX(obj)].mq.m;
	fprintf(f, "{instr,%d,'NEW',\"%s\",\"%s\",[{ent,%d},{obj,%u}]}%s\n",
		i, decl_name(st, mx), decl_name(st, obj), 
		INDEX(ent), m, eot);
	break;
    }
    default:
	switch(csp_opcode_arity(st->instr[i].op)) {
	case 1:
	    fprintf(f, "{instr,%d,'%s',[r%d,r%d]}%s\n",
		    i,
		    csp_opcode_name(st->instr[i].op),
		    st->instr[i].a.x,
		    st->instr[i].a.y,
		    eot);
	    break;	    
	case 2:
	    fprintf(f, "{instr,%d,'%s',[r%d,r%d,r%d]}%s\n",
		    i,
		    csp_opcode_name(st->instr[i].op),
		    st->instr[i].a.x,
		    st->instr[i].a.y,
		    st->instr[i].a.z,
		    eot);
	    break;
	}
    }
    return i+1;
}

void csp_dump_var(FILE* f, csp_rt_t* st, int m, int i)
{
    index_t ix = MAKE_INDEX(m,i);
    int doffs = st->offs[m];
    fprintf(f, " %-s=", decl_name(st, ix));
    csp_fprint_value(f, st, st->decl[i].vt, st->dout[doffs+i]);
    // print previous value 
//    fputc('[', f);
//    csp_fprint_value(f, st, st->decl[i].vt, st->din[doffs+i]);
//    fputc(']', f);
}

void csp_dump_variables(FILE* f, csp_rt_t* st)
{
    int i;
    int m;
    fprintf(f, "%d:", st->cycle);
    i = 0;
    while(i < st->ps.nd) {
	if (st->decl[i].type == DECL_MODULE) {
	    // skip module decl (covered by objects)
	    i += (st->decl[i].md.n+1);
	}
	else {
	    if (st->decl[i].type == DECL_VARIABLE)
		csp_dump_var(f, st, 0, i);
	    i++;
	}
    }
    fprintf(f, "\n");
    for (m = 1; m <= st->ps.nq; m++) {
	int j;
	index_t obj = st->object[m];
	index_t mx  = st->decl[INDEX(obj)].mq.mx;
	int     n   = st->decl[INDEX(mx)].md.n;
	// index_t ent = st->decl[INDEX(mx)].md.ent;
	fprintf(f, "%s.%s\n", decl_name(st, mx), decl_name(st, obj));
	for (j = 0; j < n; j++) {
	    int k = INDEX(mx)+1+j;
	    if (st->decl[k].type == DECL_VARIABLE) {
		csp_dump_var(f, st, m, k);
	    }
	}
	fprintf(f, "\n");
    }
    fputc('\n', f);
}

void csp_dump_state_erl(FILE* f, csp_rt_t* st)
{
    int i, m, j, k;
    int first = 1;
    int first_var;

    fprintf(f, "{state,%d,[", st->cycle);

    // Global variables
    i = 0;
    while(i < st->ps.nd) {
	if (st->decl[i].type == DECL_MODULE) {
	    i += (st->decl[i].md.n+1);
	} else {
	    if (st->decl[i].type == DECL_VARIABLE) {
		index_t ix = MAKE_INDEX(0,i);
		if (!first) fprintf(f, ",");
		first = 0;
		fprintf(f, "{var,\"%s\",", decl_name(st, ix));
		csp_fprint_value(f, st, st->decl[i].vt, st->dout[i]);
		fprintf(f, "}");
	    }
	    i++;
	}
    }

    // Objects
    for (m = 1; m <= st->ps.nq; m++) {
	index_t obj = st->object[m];
	index_t mx  = st->decl[INDEX(obj)].mq.mx;
	int n = st->decl[INDEX(mx)].md.n;
	int offs = st->offs[m];

	if (!first) fprintf(f, ",");
	first = 0;
	fprintf(f, "{object,\"%s\",'%s',[",
		decl_name(st, obj), decl_name(st, mx));

	first_var = 1;
	for (j = 0; j < n; j++) {
	    k = INDEX(mx)+1+j;
	    if (st->decl[k].type == DECL_VARIABLE) {
		index_t vx = MAKE_INDEX(0,k);
		if (!first_var) fprintf(f, ",");
		first_var = 0;
		fprintf(f, "{var,\"%s\",", decl_name(st, vx));
		csp_fprint_value(f, st, st->decl[k].vt, st->dout[offs+k]);
		fprintf(f, "}");
	    }
	}
	fprintf(f, "]}");
    }

    fprintf(f, "]}.\n");
}

void csp_dump_result_erl(FILE* f, csp_rt_t* st, index_t x)
{
    fprintf(f, "{result,[{cycle,%d}", st->cycle);
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    fprintf(f, ",{num_eval0,%d}", st->num_eval0);
#endif
    if (x == BAD_INDEX)
	fprintf(f, ",{value,undefined}");
    else
	fprintf(f, ",{value,%d}", csp_ivalue(st, x));
    fprintf(f, "]}.\n");
}


index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i, char* eot)
{
    index_t ix = MAKE_INDEX(0,i);
    int vt = V_INTEGER;
    
    fprintf(f, "%s", indent(lev));
    switch(st->decl[i].type) {
    case DECL_MODULE: {
	index_t n = st->decl[i].md.n;
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
		decl_name(st, st->decl[i].mq.mx),
		decl_name(st, ix), eot);
	break;
    case DECL_VARIABLE:
	vt = st->decl[i].vt;
	fprintf(f, "{decl,%d,variable,\"%s\",[{size,%d},{dir,%s},{type,%s},{init,",
		i,
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_pindir(st->decl[i].dir),
		csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, st->decl[i].va.init);
	fprintf(f, "},{value,");
	csp_fprint_value(f, st, vt, st->din[i]);
	fprintf(f, "}]}%s\n", eot);
	break;
    case DECL_CONSTANT:
	vt = st->decl[i].vt;	    
	fprintf(f, "{decl,%d,constant,\"%s\",[{size,%d},{type,%s},{init,",
		i,
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, st->decl[i].cn.init);
	fprintf(f, "},{value,");
	csp_fprint_value(f, st, vt, st->din[i]);
	fprintf(f, "}]}%s\n", eot);	
	break;
    case DECL_DIGITAL:
	vt = st->decl[i].vt; // should be unsigned
	fprintf(f, "{decl,%d,digital,\"%s\",[{dir,%s},{pull,%s},{port,%d},{pin,%d}]}%s\n",
		i,
		decl_name(st, ix),
		csp_fmt_pindir(st->decl[i].dir),
		csp_fmt_pull(st, i),
		st->decl[i].di.port,st->decl[i].di.pin,
		eot);
	break;
    case DECL_ANALOG:
	vt = st->decl[i].vt;	    
	fprintf(f,"{decl,%d,analog,\"%s\",[{size,%d},{type,%s},{dir,%s},{pwm,%s},{port,%d},{pin,%d}]}%s\n",
	       i,
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_pindir(st->decl[i].dir),
		csp_fmt_pwm(st, i),
		st->decl[i].an.port, st->decl[i].an.pin,
		eot);
	break;
    case DECL_TIMER:
	vt = st->decl[i].vt;
	fprintf(f, "{decl,%d,timer,\"%s\",[{value,%d},{signed,%d},",
		i,
		decl_name(st, ix),
		csp_ivalue(st, st->decl[i].tm.px),
		st->decl[i].tm.init);
	fprintf(f, "{t0,");
	csp_fprint_tag(f, st, st->decl[i].tm.tx);
	fprintf(f, "}]}%s\n", eot);	
	break;
    case DECL_CAN:
	vt = st->decl[i].vt;
	fprintf(f, "{decl,%d,can,\"%s\",[{size,%d},{type,%s},{endian,%s},{dir,%s},{id,16#%x},{bit,%d},{len,%d}]}%s\n",
		i,
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_endian(st->decl[i].ca.endian),
		csp_fmt_pindir(st->decl[i].dir),
		csp_ivalue(st, st->decl[i].ca.id),
		st->decl[i].ca.bit,
		GET_CAN_LEN(st->decl[i].ca.len), eot);
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
		decl_name(st, st->decl[INDEX(ix)].mq.mx),
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
	    if (maybe_unquoted_atom((char*)op_table[tv[i].t].name, op_table[tv[i].t].namelen))
		fprintf(f,"%.*s,", op_table[tv[i].t].namelen, op_table[tv[i].t].name);
	    else
		fprintf(f,"'%.*s',", op_table[tv[i].t].namelen, op_table[tv[i].t].name);
	    break;
	}
    }
    fprintf(f, "eol].\n");
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
    case DECL_UART: return "DECL_UART";
    case DECL_SOCKET: return "DECL_SOCKET";
    default: return "?";
    }
}

const char* csp_cfmt_endian(vendian_t et)
{
    switch(et) {
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
    fprintf(f, "const char rom_str[] = {\n");
    i = 0;
    while (i < st->ps.strp) {
	uint8_t n = st->str[i]; // length of next string
	int j = 1;
	fprintf(f, "%d,", n);   // emit length
	i++;
	while(j <= n) {
	    int c = st->str[i];
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
    fprintf(f, "const csp_decl_t rom_decl[] = {\n");
    for (i = 0; i < st->ps.nd; i++) {
	// fixme: output .type as DECL_abc
	csp_decl_t* dp = &st->decl[i];
	fprintf(f, "  {.type=%s,.name=%u,.vt=%s,.res=%u,.dir=%u,",
		csp_cfmt_dtype(dp->type), dp->name, csp_cfmt_vtype(dp->vt),
		dp->res, dp->dir);
	switch(dp->type) {
	case DECL_MODULE:
	    fprintf(f, ".md={.nn=%u,.ent=%u}", dp->md.n, dp->md.ent);
	    break;
	case DECL_END:
	    break;
	case DECL_OBJECT:
	    fprintf(f, ".mq={.mx=%u,.m=%u}", dp->mq.mx, dp->mq.m);
	    break;
	case DECL_VARIABLE:
	    fprintf(f, ".va={%u}", dp->va.init.u);
	    break;
	case DECL_CONSTANT:
	    fprintf(f, ".cn={%u}", dp->va.init.u);
	    break; 
	case DECL_DIGITAL:
	    fprintf(f, ".di={.pin=%u,.port=%u,.pullup=%u,.pulldown=%u}",
		    dp->di.pin, dp->di.port, dp->di.pullup, dp->di.pulldown);
	    break;
	case DECL_ANALOG:
	    fprintf(f, ".an={.pin=%u,.port=%u,.pwm=%u}",
		    dp->an.pin, dp->an.port, dp->an.pwm);
	    break;
	case DECL_CAN:
	    fprintf(f, ".an={.id=%u,.endian=%u,.bit=%u,.len=%u}",
		    dp->ca.id, dp->ca.endian, dp->ca.bit, dp->ca.len);
	    break;
	case DECL_TIMER:
	    fprintf(f, ".tm={0,.init=%u,.px=%u,.tx=%u}",
		    // dp->tm.running (is runtime) set = 0
		    dp->tm.init, dp->tm.px, dp->tm.tx);
	    break;
	case DECL_NOP:    // not used
	case DECL_UART:   // not defined yet
	case DECL_SOCKET: // not defined yet
	}
	fprintf(f, "},\n");
    }
    fprintf(f, "};\n");

    // and then dump instructions
    fprintf(f, "const csp_instr_t rom_instr[] = {\n");
    for (i = 0; i < st->ps.nn; i++) {
	csp_instr_t* ip = &st->instr[i];
	fprintf(f, "  {.op=OP_%s,", csp_opcode_name(ip->op));
	switch(ip->op) {
	    // FIXME: OP_ENTER/OP_LEAVE could share format?
	case OP_ENTER:
	    fprintf(f, ".e={.num=%u,.mx=%u}", ip->e.num, ip->e.mx);
	    break;	    
	case OP_LEAVE:
	    fprintf(f, ".v={.num=%u,.mx=%u}", ip->v.num, ip->v.mx);
	    break;	    
	case OP_NEW:
	    fprintf(f, ".n={.ent=%u,.obj=%u}", ip->n.ent, ip->n.obj);
	    break;
	case OP_LI:
	case OP_LIU:
	case OP_LIH:
	case OP_ARG:
	    fprintf(f, ".i={.x=%u,.imm=%d}", ip->i.x, ip->i.imm);
	    break;
	case OP_ST:
	case OP_STIMP:
	case OP_CHG:
	case OP_LD:
	    fprintf(f, ".m={.x=%u,.mem=%u}", ip->m.x, ip->m.mem);
	    break;
	case OP_CALL:
	    fprintf(f, ".f={.x=%u,.idx=%u,.usr=%u,avt=0x%04x}",
		    ip->f.x, ip->f.idx, ip->f.usr, ip->f.avt);
	    break;
	case OP_NEXT:
	    fprintf(f, ".x={.x=%u}",
		    ip->a.x);
	    break;
	case OP_RULE:
	    fprintf(f, ".r={.cnd=%u,.nxt=%u}", ip->r.cnd, ip->r.nxt);
	    break;
	default: { // two/three-address-instruction
	    int t = csp_opcode_to_tok(ip->op);
	    if (op_table[t].arity == 1)
		fprintf(f, ".a={.x=%u,.y=%u}",
			ip->a.x, ip->a.y);
	    else
		fprintf(f, ".a={.x=%u,.y=%u,.z=%u}",
			ip->a.x, ip->a.y, ip->a.z);
	    break;
	}
	}
	fprintf(f, "},\n");
    }
    fprintf(f, "};\n");    
}

// list declarations

index_t csp_list_decl(FILE* f, csp_rt_t* st, int i)
{
    index_t ix = MAKE_INDEX(0,i);
    int vt = V_INTEGER;
    
    switch(st->decl[i].type) {
    case DECL_MODULE:
	fprintf(f, "#module %s\n", decl_name(st, ix));
	break;
    case DECL_END:
	fprintf(f, "#end\n");
	break;
    case DECL_OBJECT:
	fprintf(f, "#%s %s\n",
		decl_name(st, st->decl[i].mq.mx),
		decl_name(st, ix));
	break;
    case DECL_VARIABLE:
	vt = st->decl[i].vt;
	fprintf(f, "#variable %s:%d %s %s = ", // show init value
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_pindir(st->decl[i].dir),
		csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, st->decl[i].va.init);
	fprintf(f, "\n");
	break;
    case DECL_CONSTANT:
	vt = st->decl[i].vt;	    
	fprintf(f, "#constant %s:%d %s = ",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt));
	csp_fprint_value(f, st, vt, st->decl[i].cn.init);
	fprintf(f, "\n");	
	break;
    case DECL_DIGITAL:
	vt = st->decl[i].vt; // should be unsigned
	fprintf(f, "#digital %s %s %s %d:%d\n",
		decl_name(st, ix),
		csp_fmt_pindir(st->decl[i].dir),
		csp_fmt_pull(st, i),
		st->decl[i].di.port,st->decl[i].di.pin);
	break;
    case DECL_ANALOG:
	vt = st->decl[i].vt;
	fprintf(f,"#analog %s:%d %s %s %s %d:%d\n",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_pindir(st->decl[i].dir),
		csp_fmt_pwm(st, i),
		st->decl[i].an.port, st->decl[i].an.pin);
	break;
    case DECL_TIMER:
	vt = st->decl[i].vt;
	fprintf(f, "#timer %s %d = %d\n",
		decl_name(st, ix),
		csp_ivalue(st, st->decl[i].tm.px),
		st->decl[i].tm.init);
	break;
    case DECL_CAN:
	vt = st->decl[i].vt;
	fprintf(f, "#can %s:%d %s %s %s 0x%x[%d:%d]\n",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_endian(st->decl[i].ca.endian),
		csp_fmt_pindir(st->decl[i].dir),
		csp_ivalue(st, st->decl[i].ca.id),
		st->decl[i].ca.bit,
		st->decl[i].ca.bit + GET_CAN_LEN(st->decl[i].ca.len));
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

void exprbuf_init(csp_exprbuf_t* bp)
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
void exprbuf_print(FILE* f, csp_exprbuf_t* bp, unsigned idx)
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
            fputc(b, f);
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

static uint8_t exprbuf_var(csp_rt_t* st, csp_exprbuf_t* bp, uint16_t ix)
{
    uint8_t* varname =  (uint8_t*) decl_name(st, ix);
    uint8_t  varnamelen = varname[-1];
    return exprbuf_intern(bp, varname, varnamelen);
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
    if (v >= 10000) { exprbuf_char(bp, (v / 10000)+'0'); v %= 10000; }
    if (v >= 1000)  { exprbuf_char(bp, (v / 1000)+'0'); v %= 1000; }
    if (v >= 100)   { exprbuf_char(bp, (v / 100)+'0'); v %= 100; }
    if (v >= 10)    { exprbuf_char(bp, (v / 10)+'0'); v %= 10; }
    exprbuf_char(bp, v+'0');
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

char hex(uint8_t v)
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
	exprbuf_str(bp, op);
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
	exprbuf_str(bp, op);
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
    uint8_t* start = exprbuf_ptr(bp);
    const char* fname;
    int fnamelen;
    uint8_t fn;
    uint16_t argtypes;
    
    if (ip->f.usr) {
	// maybe RODATA on AVR! fixme
	fname = st->ufuncs[ip->f.idx].name;
	fnamelen = st->ufuncs[ip->f.idx].namelen;
	argtypes = st->ufuncs[ip->f.idx].argtypes;
    }
    else {
	// always RODATA on AVR!
	fname = csp_builtin_funcs[ip->f.idx].name;
	fnamelen = csp_builtin_funcs[ip->f.idx].namelen;
	argtypes = csp_builtin_funcs[ip->f.idx].argtypes;	
    }
    fn = exprbuf_intern(bp, (uint8_t*)fname, fnamelen);

    exprbuf_strref(bp, fn);
    exprbuf_char(bp, '(');
    for (i = 0; i < MAX_ARGS; i++) {
	int a = (ip->f.avt >> i*4) & 0xf;
	if (a == 0) break;
	if (i > 0) exprbuf_char(bp, ',');
	if ((argtypes >> i*4) == V_INDEX) {
	    // convert argument assumed index integer
	    uint16_t imm = exprbuf_reftoi(bp, bp->arg[i]);
	    uint8_t var = exprbuf_var(st, bp, imm);
	    exprbuf_strref(bp, var);	    
	}
	else {
	    exprbuf_strref(bp, bp->arg[i]);
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
    uint8_t* start = exprbuf_ptr(bp);
    uint8_t  var = exprbuf_var(st, bp, ip->m.mem);

    exprbuf_strref(bp, var);
    if (rimp) { exprbuf_char(bp, '<'); exprbuf_char(bp, '-'); }
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
//    printf("LD: R%d = '%s'\n", ip->m.x, bp->reg[ip->m.x]);
    bp->prio[ip->m.x] = 110;
}

// exprbuf contains rule condition
void exprbuf_rule(FILE* f, csp_rt_t* st, csp_exprbuf_t* bp, csp_instr_t* ip)
{
    if (f != NULL)
	exprbuf_print(f, bp, bp->reg[ip->r.cnd]);
}

// exprbuf contains rule body - print side-effects then final expression
void exprbuf_body(FILE* f, csp_rt_t* st, csp_exprbuf_t* bp, csp_instr_t* ip)
{
    int i;
    for (i = 0; i < bp->nbody; i++) {
	if (i > 0) fputc(',', f);
	exprbuf_print(f, bp, bp->body[i]);
    }
    // add final expression if different from last body element
    if (bp->nbody == 0 || bp->body[bp->nbody-1] != bp->reg[ip->x.x]) {
	if (bp->nbody > 0) fputc(',', f);
	exprbuf_print(f, bp, bp->reg[ip->x.x]);
    }
}

//
// trace instructions util
// 1. OP_RULE  then we have a condition expression
// 2. OP_NEXT  then we have a body expression
//
static int exprbuf_expr(FILE* f, csp_rt_t*  st,
			csp_exprbuf_t* bp,
			int i)
{
    while(i < st->ps.nn) {
	tok_t t;
	csp_instr_t* ip = &st->instr[i];
	switch(ip->op) {
	case OP_RULE:
	    exprbuf_rule(f, st, bp, ip);
	    return i+1;
	case OP_NEXT:
	    exprbuf_body(f, st, bp, ip);
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
	    // Add side-effect calls (void funcs like print) to body list
	    if (bp->nbody > 0 ||
		(i+1 < st->ps.nn &&
		 (st->instr[i+1].op == OP_RULE || st->instr[i+1].op == OP_NEXT ||
		  st->instr[i+1].op == OP_ST || st->instr[i+1].op == OP_STIMP ||
		  st->instr[i+1].op == OP_CALL))) {
		if (bp->nbody < MAX_BODY)
		    bp->body[bp->nbody++] = bp->reg[ip->f.x];
	    }
	    break;
        case OP_LD:
	    exprbuf_ld(st, bp, ip);
	    break;
        case OP_LIU: {
            uint8_t *start = exprbuf_ptr(bp);
            exprbuf_xuint16(bp, (uint16_t)ip->i.imm);
	    bp->reg[ip->i.x] = exprbuf_intern(bp,start,exprbuf_len(bp, start));
            bp->prio[ip->i.x] = 110;
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
	default:
	    t = csp_opcode_to_tok(ip->op);
	    if (op_table[t].arity >= 0) {
		exprbuf_alu(bp, ip, op_table[t].name,
			    op_table[t].arity, op_table[t].prec);
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
int csp_list_rule(FILE* f, csp_rt_t* st, int i)
{
    int Lc = i;
    csp_exprbuf_t buf;

    while(i < st->ps.nn) {
	if (st->instr[i].op == OP_RULE) {
	    csp_instr_t* ip = &st->instr[i];
	    fprintf(f, "RULE-%d\n", Lc);
	    exprbuf_init(&buf);
	    exprbuf_expr(f, st, &buf, i+1);   // print body
	    // Build condition in buffer, check if non-empty
	    exprbuf_init(&buf);
	    exprbuf_expr(NULL, st, &buf, Lc); // build but don't print
	    switch(buf.strlens[buf.reg[ip->r.cnd]]) {
	    case 0:
		break;
	    case 2:
		if ((buf.buf[buf.reg[ip->r.cnd]] == '-') &&
		    (buf.buf[buf.reg[ip->r.cnd]+1] == '1'))
		    break;		
	    default:
		fprintf(f, " ? ");
		exprbuf_print(f, &buf, buf.reg[ip->r.cnd]);
	    }
	    fprintf(f, "\n");
	    return i+st->instr[i].r.nxt+1;
	}
	i++;
    }
    return i;
}

// rules are generate as code
// here we try to reverse engineer the rules, is there a better way?
//
void csp_list_rules(FILE* f, csp_rt_t* st)
{
    int i = 0;
    while(i < st->ps.nn)
	i = csp_list_rule(f, st, i);
}
