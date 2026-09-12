// The generated accessors against the struct they will replace.
//
// utils/layout.terms now states the bit layout of a declaration, and
// gen/csp_layout.h is the accessors computed from it. Until the tree stops
// reading csp_decl_t's bit-fields there are two descriptions of one layout,
// which is the hazard this whole exercise is meant to remove -- so while both
// exist, every field is checked BOTH ways: set through the struct and read
// through the accessor, then set through the accessor and read through the
// struct.
//
// When the conversion is done and the struct's bit-fields are gone, this file
// goes with them. Until then it is what makes the cutover incremental instead
// of a flag day.
//
// Built by `make layout_check`. Prints "ok" and nothing else on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"
#include "csp_layout_raw.h"
#include "csp_layout.h"
#include "csp_layout_oracle.h"

static int errors = 0;

static void fail(const char* what, long got, long want)
{
    printf("FAIL %s: got %ld, want %ld\n", what, got, want);
    errors++;
}

int main(void)
{
    csp_decl_raw_t d;

    CSP_ORACLE_DECL_COMMON(d, fail);
    CSP_ORACLE_DECL_COMMON_MD(d, fail);
    CSP_ORACLE_DECL_COMMON_MQ(d, fail);
    CSP_ORACLE_DECL_COMMON_VA(d, fail);
    CSP_ORACLE_DECL_COMMON_CN(d, fail);
    CSP_ORACLE_DECL_COMMON_DI(d, fail);
    CSP_ORACLE_DECL_COMMON_AN(d, fail);
    CSP_ORACLE_DECL_COMMON_CA(d, fail);
    CSP_ORACLE_DECL_COMMON_BF(d, fail);
    CSP_ORACLE_DECL_COMMON_RT(d, fail);
    CSP_ORACLE_DECL_COMMON_TM(d, fail);
    CSP_ORACLE_DECL_COMMON_EM(d, fail);
    CSP_ORACLE_DECL_COMMON_S6(d, fail);

    // The record must still be the size the terms file says, or a field that
    // fits the description does not fit the storage.

    {
	csp_instr_raw_t n;

	CSP_ORACLE_INSTR_COMMON(n, fail);
	CSP_ORACLE_INSTR_COMMON_A(n, fail);
	CSP_ORACLE_INSTR_COMMON_E(n, fail);
	CSP_ORACLE_INSTR_COMMON_EM(n, fail);
	CSP_ORACLE_INSTR_COMMON_F(n, fail);
	CSP_ORACLE_INSTR_COMMON_I(n, fail);
	CSP_ORACLE_INSTR_COMMON_IN(n, fail);
	CSP_ORACLE_INSTR_COMMON_M(n, fail);
	CSP_ORACLE_INSTR_COMMON_MI(n, fail);
	CSP_ORACLE_INSTR_COMMON_N(n, fail);
	CSP_ORACLE_INSTR_COMMON_O(n, fail);
	CSP_ORACLE_INSTR_COMMON_OX(n, fail);
	CSP_ORACLE_INSTR_COMMON_R(n, fail);
	CSP_ORACLE_INSTR_COMMON_SG(n, fail);
	CSP_ORACLE_INSTR_COMMON_V(n, fail);
	CSP_ORACLE_INSTR_COMMON_X(n, fail);
	if (sizeof(csp_instr_raw_t) != 4)
	    fail("csp_instr_t is 4 bytes", (long)sizeof(csp_instr_t), 4);
    }

    if (sizeof(csp_decl_raw_t) != 8)
	fail("csp_decl_t is 8 bytes", (long)sizeof(csp_decl_t), 8);

    if (errors == 0)
	printf("ok\n");
    return (errors == 0) ? 0 : 1;
}
