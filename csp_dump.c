
#include <stdio.h>
#include <ctype.h>
#include "csp_dump.h"
#include "csp_format.h"

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
	fprintf(f, "{%s,%d}", csp_tag(st,n), i);
    else if (m == CURRENT) // match
	fprintf(f, "{cur,%s,%d}", csp_tag(st,n), i);
    else
	fprintf(f, "{%d,%s,%d}", m, csp_tag(st,n), i);
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
    case V_FLOAT: fprintf(f, "%f", val.f); break;
    case V_STRING:
	csp_fprint_escaped_string(f, &st->str[val.s], st->str[val.s-1]);
	break;
    default: fprintf(f, "???"); break;
    }
}

void dump_edge_list(FILE* f, csp_rt_t* st, index_t ix)
{
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)
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
    const char* cond = st->instr[i].cond ? "t" : "f";

    fprintf(f, "%s", indent(lev));
    switch(st->instr[i].op) {
    case OP_NOP:
	fprintf(f, "{instr,%d,nop}%s\n",
		i, eot);
	break;
    case OP_NEXT:
	fprintf(f, "{instr,%d,next}%s\n",
		i, eot);
	break;
    case OP_LD:
	fprintf(f, "{instr,%d,ld,[r%d,",
		i,
		st->instr[i].m.x);
	csp_fprint_tag(f, st, st->instr[i].m.mem);
	fprintf(f, ",%s]}%s\n", cond, eot);
	break;
    case OP_ST:
	fprintf(f, "{instr,%d,st,[r%d,",
		i,
		st->instr[i].m.x);
	csp_fprint_tag(f, st, st->instr[i].m.mem);
	fprintf(f, ",%s]}%s\n", cond, eot);
	break;
    case OP_LI:
	fprintf(f, "{instr,%d,li,[r%d,%d,%s]}%s\n",
		i,
		st->instr[i].i.x,
		st->instr[i].i.imm,
		cond, eot);
	break;
    case OP_ARG:
	fprintf(f, "{instr,%d,arg,[r%d,%d,%s]}%s\n",
		i,
		st->instr[i].i.x,
		st->instr[i].i.imm,
		cond, eot);
	break;
    case OP_CALL:
	fprintf(f, "{instr,%d,call,[r%d,%s,%d,%s]}%s\n",
		i,
		st->instr[i].f.x,
		(st->instr[i].f.usr ?
		 st->user_funcs[st->instr[i].f.idx].name :
		 csp_builtin_funcs[st->instr[i].f.idx].name),
		st->instr[i].f.n,
		cond, eot);
	break;
    case OP_RULE:
	fprintf(f, "{instr,%d,rule,[r%d,%d]}%s\n",
		i,
		st->instr[i].r.cnd, st->instr[i].r.nxt, eot);
	break;
    case OP_ENTER: {
	index_t mx = st->instr[i].e.mx;
	int n = st->instr[i].e.num;
	int j;
	fprintf(f, "{instr,%d,enter,'%s',[{n,%d}],[\n",
		i, decl_name(st, mx), n);
	i++;
	for (j = 0; j <= n; j++) // <= include leave!
	    i = csp_dump_instr(f, lev+1, st, i, (j == n) ? "" : ",");
	fprintf(f, "]}%s\n", eot);
	break;
    }
    case OP_LEAVE: {
	index_t mx = st->instr[i].v.mx;
	int n = st->instr[i].v.num;
	fprintf(f, "{instr,%d,leave,'%s',[{n,%d}]}%s\n",
		i, decl_name(st,mx), n, eot);
	break;
    }
    case OP_NEW: {
	// FIXME: add new op arguments	
	index_t ent = st->instr[i].n.ent;
	index_t mod = st->instr[i].n.mx;
	index_t mx  = st->decl[INDEX(mod)].mq.mx;
	// int iq      = st->decl[INDEX(mod)].mq.iq;
	// int ofs = st->mofs[iq];
	fprintf(f, "{instr,%d,new,\"%s\",\"%s\",[{ent,%d}]}%s\n",
		i, decl_name(st, mx), decl_name(st, mod), 
		INDEX(ent), eot);
	break;
    }
    default:
	fprintf(f, "{instr,%d,'%s',[r%d,r%d,r%d,%s]}%s\n",
		i,
		csp_op_name(st->instr[i].op),
		st->instr[i].a.x,
		st->instr[i].a.y,
		st->instr[i].a.z,
		cond, eot);
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
#if defined(WANT_STATISTICS) && (WANT_STATISTICS==1)
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
		csp_fmt_pindir(&st->decl[i]),
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
		csp_fmt_pindir(&st->decl[i]),
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
		csp_fmt_pindir(&st->decl[i]),
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
		csp_fmt_pindir(&st->decl[i]),
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
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)
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

    fprintf(f, "{object,[");
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	index_t ix = st->object[m];
	if (i > 0) fputc(',', f);
	fprintf(f, "{'%s',%d,",
		decl_name(st, st->decl[INDEX(ix)].mq.mx),
		st->offs[m]);
	csp_fprint_tag(f, st, ix);
	fprintf(f, "}");
    }
    fprintf(f, "]}.\n");    
}

#if 0
void csp_print_expr(FILE* f, csp_rt_t* st, index_t ix)
{
    if (IS_DECL(ix)) {
	switch(st->decl[INDEX(ix)].type) {
	case DECL_VARIABLE: printf("%s", decl_name(st, ix)); break;
	case DECL_CONSTANT: printf("%d", st->decl[INDEX(ix)].cn.init.i); break;
	    // FIXME:
	default: printf("?"); break;
	}
    }
    else {
	printf("(");
	csp_print_expr(f, st, st_instr_y(st, INDEX(ix)));
	printf("%s", op_name(st->instr[INDEX(ix)].op));
	csp_print_expr(f, st, st_instr_z(st, INDEX(ix));
	printf(")");
    }
}
#endif

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


void csp_dump_tokens(FILE* f, tok_t* tok, tokval_t* val, int n)
{
    int i;
    fprintf(f, "[");
    for (i = 0; i < n; i++) {
	switch(tok[i]) {
	case INT: fprintf(f,"%d,", val[i].val.i); break;
	case FLT: fprintf(f,"%f,", val[i].val.f); break;
	case STR: fprintf(f,"\"%.*s\",", val[i].len, val[i].str); break;
	case WORD:
	    if (maybe_unquoted_atom(val[i].str, val[i].len))
		fprintf(f,"%.*s,", val[i].len, val[i].str);
	    else
		fprintf(f,"'%.*s',", val[i].len, val[i].str);
	    break;
	default:
	    if (maybe_unquoted_atom((char*)op_table[tok[i]].name, op_table[tok[i]].name_len))
		fprintf(f,"%.*s,", op_table[tok[i]].name_len, op_table[tok[i]].name);
	    else
		fprintf(f,"'%.*s',", op_table[tok[i]].name_len, op_table[tok[i]].name);
	    break;
	}
    }
    fprintf(f, "eol].\n");
}
