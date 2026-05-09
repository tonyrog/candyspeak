
#include "csp_format.h"

const char* csp_format_error(csp_err_t err)
{
    switch(err) {
    case ERR_OK:
	return "ok";
    case ERR_SYNTAX:
	return "syntax error";
    case ERR_STRING_SPACE_EXHUSTED:
	return "string space exhuasted";
    case ERR_TOO_MANY_DECLARATIONS:
	return "too many declarations";
    case ERR_TOO_MANY_INSTRUCTIONS:
	return "too many instructions";
    case ERR_TOO_MANY_OBJECTS:
	return "too many objects";
    case ERR_MODULE_NOT_DECLARED:
	return "module %s not declared";
    case ERR_NOT_A_MODULE:
	return "word %s not a module";
    case ERR_OBJECT_ALREADY_DEFINED:
	return "object %s is already defined";
    case ERR_OBJECT_NOT_DEFINED:
	return "object %s is not defined";
    case ERR_VARIABLE_NOT_DECLARED:
	return "variable %s is not declared";
    default:
	return "unknown error";
    }
}
    

const char* csp_tag(csp_rt_t* st, index_t n)
{
    switch(st->decl[INDEX(n)].type) {
    case DECL_OBJECT: return "q";
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

const char* csp_fmt_pindir(csp_decl_t* lp)
{
    switch(lp->dir) {
    case DIR_INOUT: return " inout";
    case DIR_IN:    return " in";
    case DIR_OUT:   return " out";
    default:        return "undefined";
    }
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
    case V_VOID: return "void";	
    case V_INTEGER: return "integer";
    case V_UNSIGNED: return "unsigned";	
    case V_FLOAT: return "float";
    case V_STRING: return "string";
    case V_INDEX: return "index";
    case V_NUMBER: return "number";
    case V_ANY: return "any";	
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
