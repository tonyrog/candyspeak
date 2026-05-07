// Dump functions for debugging and inspection
// but also generate C code for builtin eeprom code
//
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
	fprintf(f, "{instr,%d,'NEXT'}%s\n",
		i, eot);
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
		i,
		st->instr[i].m.x);
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
	fprintf(f, "{instr,%d,'CALL',[r%d,%s,%d,16#%04x]}%s\n",
		i,
		st->instr[i].f.x,
		(st->instr[i].f.usr ?
		 st->ufuncs[st->instr[i].f.idx].name :
		 csp_builtin_funcs[st->instr[i].f.idx].name),
		st->instr[i].f.n,
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
	fprintf(f, "{instr,%d,'%s',[r%d,r%d,r%d]}%s\n",
		i,
		csp_op_name(st->instr[i].op),
		st->instr[i].a.x,
		st->instr[i].a.y,
		st->instr[i].a.z,
		eot);
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

// print tokens in erlang term format
void csp_dump_tokens(FILE* f, token_t* tv, int n)
{
    int i;
    fprintf(f, "[");
    for (i = 0; i < n; i++) {
	switch(tv[i].t) {
	case INT: fprintf(f,"%d,", tv[i].v.val.i); break;
	case FLT: fprintf(f,"%f,", tv[i].v.val.f); break;
	case STR: fprintf(f,"\"%.*s\",", tv[i].v.str.len, tv[i].v.str.ptr); break;
	case WORD:
	    if (maybe_unquoted_atom(tv[i].v.str.ptr, tv[i].v.str.len))
		fprintf(f,"%.*s,", tv[i].v.str.len, tv[i].v.str.ptr);
	    else
		fprintf(f,"'%.*s',", tv[i].v.str.len, tv[i].v.str.ptr);
	    break;
	default:
	    if (maybe_unquoted_atom((char*)op_table[tv[i].t].name, op_table[tv[i].t].name_len))
		fprintf(f,"%.*s,", op_table[tv[i].t].name_len, op_table[tv[i].t].name);
	    else
		fprintf(f,"'%.*s',", op_table[tv[i].t].name_len, op_table[tv[i].t].name);
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
	fprintf(f, "  {.type=%s,.name=%u,.vt=%s,.res=%u,.in=%u,.out=%d,",
		csp_cfmt_dtype(dp->type), dp->name, csp_cfmt_vtype(dp->vt),
		dp->res, dp->in, dp->out);
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
	fprintf(f, "  {.op=OP_%s,", csp_op_name(ip->op));
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
	case OP_LD:
	    fprintf(f, ".m={.x=%u,.mem=%u}", ip->m.x, ip->m.mem);
	    break;
	case OP_CALL:
	    fprintf(f, ".f={.x=%u,.idx=%u,.usr=%u,n=%u,avt=0x%04x}",
		    ip->f.x, ip->f.idx, ip->f.usr, ip->f.n, ip->f.avt);
	    break;
	case OP_RULE:
	    fprintf(f, ".r={.cnd=%u,.nxt=%u}", ip->r.cnd, ip->r.nxt);
	    break;
	default: // three-address-instruction
	    fprintf(f, ".a={.x=%u,.y=%u,.z=%u}",
		    ip->a.x, ip->a.y, ip->a.z);
	    break;
	}
	fprintf(f, "},\n");
    }
    fprintf(f, "};\n");    

}
