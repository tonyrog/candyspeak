
#include <stdio.h>
#include <ctype.h>
#include "csp_dump.h"
#include "csp_format.h"


void csp_fprint_tag(FILE* f, csp_rt_t* st, index_t n)
{
    int m = MOD(n);
    int i = INDEX(n);
    if (m == 0) // global
	fprintf(f, "{%s,%d}", csp_tag(st,n), i);
    else if (m == ANY_MOD) // match
	fprintf(f, "{any,%s,%d}", csp_tag(st,n), i);
    else
	fprintf(f, "{%s,%d,%d}", csp_tag(st,n), m, i);
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

void dump_edge_list(FILE* f, csp_rt_t* st, int i)
{
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)    
    if (st->idg[i]) {
	int j;
	fprintf(f,",{edge_list,[");
	for (j = 0; j < st->idg[i]; j++) {
	    index_t p = st->edg[st->ofs[i]+j];  // parent node
	    if (j > 0) fputc(',', f);
	    csp_fprint_tag(f, st, p);
	}
	fprintf(f, "]}");
    }
#endif
}

index_t csp_dump_instr(FILE* f, int lev, csp_rt_t* st, int i)
{
    int cond = st->instr[i].cond;
    int vt = st->instr[i].vt;

    switch(st->instr[i].op) {
    case OP_ENTER: {
	index_t mx = st->instr[i].z;
	fprintf(f, "{instr,%d,enter,'%s',[{n,%d}]}.\n",
		i,
		decl_name(st, mx), st->instr[i].y);
	break;
    }
    case OP_LEAVE: {
	index_t mx = st->instr[i].z;	
	fprintf(f, "{instr,%d,leave,'%s',[{n,%d}]}.\n",
		i,
		decl_name(st,mx),
		st->instr[i].y);
	break;
    }
    case OP_NEW: {
	index_t ent = st->instr[i].y;
	index_t mod = st->instr[i].z;
	index_t mx  = st->decl[INDEX(mod)].mq.mx;
	int iq = st->decl[INDEX(mod)].mq.iq;
	int ofs = st->mofs[iq];
	fprintf(f, "{instr,%d,new,\"%s\".\"%s\",[{ent,%d},{ofs,%d}]}.\n",
		i,
		decl_name(st, mod),
		decl_name(st, mx),
		INDEX(ent), ofs);
	break;
    }
    default:
	fprintf(f, "{instr,%d,'%s',[{cond,%s},",
		i,
		csp_op_name(st->instr[i].op),
		cond ? "true" : "false");
	fprintf(f, "{x,");
	csp_fprint_value(f, st, vt, st->xval[i]);
	fprintf(f, "},{y,");
	csp_fprint_tag(f, st, st->instr[i].y);
	fprintf(f, "},{z,");
	csp_fprint_tag(f, st, st->instr[i].z);
	fprintf(f, "}");
	dump_edge_list(f, st, i);
	fprintf(f, "]}.\n");
    }
    return i+1;
}

void csp_dump_variables(FILE* f, csp_rt_t* st)
{
    int i;
    int n = 0;
    fprintf(f, "%d:", st->cycle);
    for (i = 0; i < st->ps.nd; i++) {
	if (st->decl[i].type == DECL_VARIABLE) {
	    index_t ix = MAKE_INDEX(0,i,TAG_DECL);
	    if (n > 0) fputc(',', f);	    
	    fprintf(f, "%s=", decl_name(st, ix));
	    csp_fprint_value(f, st, st->decl[i].vt, st->dval[i]);
	    n++;
	}
    }
    fputc('\n', f);
}


index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i)
{
    index_t ix = MAKE_INDEX(0,i,TAG_DECL);
    int vt = V_INTEGER;
    
    switch(st->decl[i].type) {
    case DECL_MODULE: {
	index_t n = st->decl[i].md.n;
	fprintf(f, "{decl,%d,module,%s,[\n", i, decl_name(st, ix));
	i++;
	while(n--) {
	    i = csp_dump_decl(f, lev+1, st, i);
	}
	fprintf(f, "]}.\n");
	return i;
    }
    case DECL_END:
	fprintf(f, "{decl,%d,'end'}.\n", i);
	break;
    case DECL_MOD:
	fprintf(f, "{decl,%d,mod,'%s','%s'}.\n",
		i,
		decl_name(st, st->decl[i].mq.mx),
		decl_name(st, ix));
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
	csp_fprint_value(f, st, vt, st->dval[i]);
	fprintf(f, "}]}.\n");
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
	csp_fprint_value(f, st, vt, st->dval[i]);
	fprintf(f, "}]}.\n");	
	break;
    case DECL_DIGITAL:
	vt = st->decl[i].vt; // should be unsigned
	fprintf(f, "{decl,%d,digital,\"%s\",[{dir,%s},{pull,%s},{port,%d},{pin,%d}]}.\n",
		i,
		decl_name(st, ix),		
		csp_fmt_pindir(&st->decl[i]),
		csp_fmt_pull(st, i),
		st->decl[i].di.port,
		st->decl[i].di.pin);
	break;
    case DECL_ANALOG:
	vt = st->decl[i].vt;	    
	fprintf(f,"{decl,%d,analog,\"%s\",[{size,%d},{type,%s},{dir,%s},{pwm,%s},{port,%d},{pin,%d}]}.\n",
	       i,
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_pindir(&st->decl[i]),
		csp_fmt_pwm(st, i),
		st->decl[i].an.port,
	       st->decl[i].an.pin);
	break;
    case DECL_TIMER:
	vt = st->decl[i].vt;
	fprintf(f, "{decl,%d,timer,\"%s\",[{value,%d},{signed,%d},",
		i,
		decl_name(st, ix),
		csp_ivalue(st, st->decl[i].tm.px),
		st->decl[i].tm.init);
	printf("{t0,");
	csp_fprint_tag(f, st, st->decl[i].tm.tx);
	fprintf(f, "}]}.\n");	
	break;
    case DECL_CAN:
	vt = st->decl[i].vt;
	fprintf(f, "{decl,%d,can,\"%s\",[{size,%d},{type,%s},{endian,%s},{dir,%s}], 0x%x[%d:%d]}.\n",
		i,
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_endian(st->decl[i].ca.endian),
		csp_fmt_pindir(&st->decl[i]),
		csp_ivalue(st, st->decl[i].ca.id),
		st->decl[i].ca.bit,
		GET_CAN_LEN(st->decl[i].ca.len));
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
	i = csp_dump_decl(f, 1, st, i);
    i = 0;
    while(i < st->ps.nn)
	i = csp_dump_instr(f, 1, st, i);

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

    fprintf(f, "{mod,[");        
    for (i = 0; i < st->nq; i++) {
	index_t ix = st->mod[i];
	if (i > 0) fputc(',', f);
	csp_fprint_tag(f, st, ix);
	fprintf(f, " mod=%s", decl_name(st, st->decl[INDEX(ix)].mq.mx));
	fprintf(f, " offs=%d", st->mofs[i]);
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
	csp_print_expr(f, st, st->instr[INDEX(ix)].y);
	printf("%s", op_name(st->instr[INDEX(ix)].op));
	csp_print_expr(f, st, st->instr[INDEX(ix)].z);
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
