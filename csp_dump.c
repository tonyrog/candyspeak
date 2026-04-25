
#include "csp_dump.h"
#include "csp_format.h"

void csp_print_tag(csp_rt_t* st, index_t n)
{
    int m = MOD(n);
    int ix = INDEX(n);
    if (m == 0) // global
	printf("%s:%d", csp_tag(st,n), ix);
    else if (m == ANY_MOD) // match
	printf("*:%s:%d", csp_tag(st,n), ix);
    else
	printf("%s:%d.%d", csp_tag(st,n), m, ix);
}

void dump_edge_list(FILE* f, csp_rt_t* st, int i)
{
#ifdef WANT_REACTIVE
    if (st->idg[i]) {
	int j;
	printf(",e=");
	for (j = 0; j < st->idg[i]; j++) {
	    index_t p = st->edg[st->ofs[i]+j];  // parent node
	    csp_print_tag(st, p);
	    printf(",");
	}
    }
#endif
}

index_t csp_dump_instr(FILE* f, int lev, csp_rt_t* st, int i)
{
    int cond = st->instr[i].cond;
    int vt = st->instr[i].vt;

    printf("%-*s %s%-4d: ", 2*lev, " ", (cond ? "?" : " "), i);

    switch(st->instr[i].op) {
    case OP_ENTER: {
	index_t mx = st->instr[i].z;
	printf(" enter %s, n=%d", decl_name(st, mx), st->instr[i].y);
	break;
    }
    case OP_LEAVE: {
	index_t mx = st->instr[i].z;	
	printf(" leave %s, n=%d", decl_name(st,mx), st->instr[i].y);
	break;
    }
    case OP_NEW: {
	index_t ent = st->instr[i].y;
	index_t mod = st->instr[i].z;
	index_t mx  = st->decl[INDEX(mod)].mq.mx;
	int iq = st->decl[INDEX(mod)].mq.iq;
	int ofs = st->mofs[iq];
	printf(" %s = new(%s) ent=%d, ofs=%d",
	       decl_name(st, mod), decl_name(st, mx), INDEX(ent), ofs);
	break;
    }
    default:
	csp_print_tag(st, st->instr[i].y);
	printf(" '%s' ", csp_op_name(st->instr[i].op));
	csp_print_tag(st, st->instr[i].z);
    }
    if (st) {
	printf(" [");
	csp_print_value(st, vt, st->xval[i]);
	printf("]");
    }
    dump_edge_list(f, st, i);	
    printf("\n");
    return i+1;
}

index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i)
{
    index_t ix = MAKE_INDEX(0,i,TAG_DECL);
    int vt = V_INTEGER;
    printf("%-*s %-4d: ", 2*lev, " ", i);
    switch(st->decl[i].type) {
    case DECL_MODULE: {
	index_t n = st->decl[i].md.n;
	printf("#module %s, n=%d\n", decl_name(st, ix), n);
	i++;
	while(n--) {
	    i = csp_dump_decl(f, lev+1, st, i);
	}
	return i;
    }
    case DECL_END:
	printf("#end");
	break;
    case DECL_MOD:
	printf("#%s %s",
		decl_name(st, st->decl[i].mq.mx),
		decl_name(st, ix));
	break;
    case DECL_VARIABLE:
	vt = st->decl[i].vt;
	printf("#variable %s:%d%s %s=",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_pindir(&st->decl[i]),
		csp_fmt_vtype(vt));
	csp_print_value(st, vt, st->decl[i].va.init);
	break;
    case DECL_CONSTANT:
	vt = st->decl[i].vt;	    
	printf("#constant %s:%d %s=",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt));
	csp_print_value(st, vt, st->decl[i].cn.init);
	break;
    case DECL_DIGITAL:
	vt = st->decl[i].vt; // should be unsigned
	printf("#digital %s%s%s %d:%d",
		decl_name(st, ix),
		csp_fmt_pindir(&st->decl[i]),
		csp_fmt_pull(st, i),
		st->decl[i].di.port, st->decl[i].di.pin);
	break;
    case DECL_ANALOG:
	vt = st->decl[i].vt;	    
	printf("#analog %s:%d %s%s%s %d:%d",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_pindir(&st->decl[i]),
		csp_fmt_pwm(st, i),
		st->decl[i].an.port, st->decl[i].an.pin);
	break;
    case DECL_TIMER:
	vt = st->decl[i].vt;
	printf("#timer %s %d signed=%d",
		decl_name(st, ix),
		csp_ivalue(st, st->decl[i].tm.px),
		st->decl[i].tm.init);
	printf(" t0=");
	csp_print_tag(st, st->decl[i].tm.tx);
	break;
    case DECL_CAN:
	vt = st->decl[i].vt;
	printf("#can %s:%d %s%s%s 0x%x[%d:%d]",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		csp_fmt_vtype(vt),
		csp_fmt_endian(st->decl[i].ca.endian),
		csp_fmt_pindir(&st->decl[i]),
		csp_ivalue(st, st->decl[i].ca.id),
		st->decl[i].ca.bit, GET_CAN_LEN(st->decl[i].ca.len));
	break;
    default:
	break;
    }
    if (st) {
	printf(" [");
	csp_print_value(st, vt, st->dval[i]);
	printf("]");	    
    }
    printf("\n");
    return i+1;
}

    
void csp_dump(FILE* f, csp_rt_t* st)
{
    int i;

    // decls
    printf("DECL %d\n", st->nd);
    i = 0;
    while(i < st->nd) {
	i = csp_dump_decl(f, 1, st, i);
    }
    // instructions
    printf("INSTR %d\n", st->nn);
    i = 0;
    while(i < st->nn) {
	i = csp_dump_instr(f, 1, st, i);
    }

    printf("INPUTS %d\n", st->ni);
    for (i = 0; i < st->ni; i++) {
	csp_print_tag(st, st->input[i]);
	printf("\n");
    }

    printf("OUTPUTS %d\n", st->no);
    for (i = 0; i < st->no; i++) {
	csp_print_tag(st, st->output[i]);
	printf("\n");
    }

    printf("MODULES %d\n", st->nm);
    for (i = 0; i < st->nm; i++) {
	csp_print_tag(st, st->module[i]);
	printf("\n");
    }

    printf("MODS %d\n", st->nq);
    for (i = 0; i < st->nq; i++) {
	index_t ix = st->mod[i];
	csp_print_tag(st, ix);
	printf(" mod=%s", decl_name(st, st->decl[INDEX(ix)].mq.mx));
	printf(" offs=%d", st->mofs[i]);
	printf("\n");
    }

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
