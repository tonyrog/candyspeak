
#include "csp_format.h"

const char* csp_tag(csp_rt_t* st, index_t n)
{
    if (IS_INSTR(n))
	return "i";
    else {
	switch(st->decl[INDEX(n)].type) {
	case DECL_MOD: return "q";
	case DECL_MODULE: return "m";
	case DECL_CONSTANT: return "c";
	case DECL_VARIABLE: return "v";
	case DECL_DIGITAL: return "d";
	case DECL_ANALOG: return "a";
	case DECL_TIMER: return "t";
	case DECL_CAN: return "k";
	case DECL_UART: return "u";
	case DECL_SOCKET: return "s";
	default: return "?";
	}
    }
}

const char* csp_fmt_pindir(csp_decl_t* lp)
{
    if (lp->in && lp->out)
	return " inout";
    else if (lp->in)
	return " in";
    else if (lp->out)
	return " out";
    else
	return "undefined";
}

const char* csp_fmt_pull(csp_rt_t* st, int ix)
{
    if (st->decl[ix].di.pullup)
	return " pullup";
    else if (st->decl[ix].di.pulldown)
	return " pulldown";
    else
	return "undefined";
}

const char* csp_fmt_pwm(csp_rt_t* st, int ix)
{
    if (st->decl[ix].an.pwm)
	return " pwm";
    else
	return "undefined";
}

const char* csp_fmt_vtype(vtype_t vt)
{
    switch(vt) {
    case V_FLOAT: return "float";
    case V_UNSIGNED: return "unsigned";
    case V_INTEGER: return "integer";
    case V_STRING: return "string";	
    default: return "undefined";
    }
}

const char* csp_fmt_endian(vendian_t et)
{
    switch(et) {
    case E_LITTLE: return "little";
    case E_BIG: return "big";
    default: return "undefined";
    }
}
