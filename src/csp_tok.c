// The operator/token table, and the only code that reads it.
//
// Its own file because it has TWO users that are not each other's dependency:
// the compiler (turning text into instructions) and the disassembler (turning
// instructions back into text). Neither owns it, and neither should have to
// depend on the other to reach it.
//
// It is deliberately NOT in csp_rt.c: the evaluator never touches this table.
// `op_info` does stay there -- csp_eval_rule reads its arity to dispatch the ALU
// -- but tok_table is text-side data, and a clean exec-only build has no text.
// (--gc-sections already drops it there; this makes that a property of the
// structure rather than a happy accident.)
//
// Every read goes through ro_byte/ro_ptr. On AVR RODATA is PROGMEM, so indexing
// the table directly reads DATA space at the table's flash address -- which on a
// mega lands inside CandySpeak's own arena. That bug was real; see the comment
// on decl_table_code in csp_compile.c.
#include <string.h>

#include "csp.h"
#include "csp_strings.h"   // the operator spellings
#include "csp_tok.h"

// The rows come from csp_tokens.h, generated together with the tok_t enum this
// table is indexed by. A macro rather than a linked table so it keeps living
// HERE, in the file whose whole reason is that neither the compiler nor the
// disassembler owns it.
const op_entry_t tok_table[] RODATA = {
    CSP_TOK_TABLE
};

int find_op_entry(const op_entry_t* tab, int size,
			 const char* name, int namelen)
{
    int i;
    for (i = 1; i < size; i++) { // assume none entry in slot 0
	uint8_t ronamelen = ro_byte(&tab[i].namelen);
	if (ronamelen == namelen) {
	    const char* roname = ro_ptr(&tab[i].name);
	    if (ro_memcmp(name, roname, ronamelen) == 0)
		return i;
	}
    }
    return -1;
}

int find_tok_entry(const char* name, int namelen)
{
    return find_op_entry(tok_table, sizeof(tok_table)/sizeof(tok_table[0]),
			 name, namelen);
}

inline int8_t op_table_tok(int i)
{
    return ro_byte(&tok_table[i].tok);
}

inline int8_t op_table_arity(int i)
{
    return ro_byte(&tok_table[i].arity);
}

inline int8_t op_table_code(int i)
{
    return ro_byte(&tok_table[i].code);
}

inline int8_t op_table_prec(int i)
{
    return ro_byte(&tok_table[i].prec);
}

inline int8_t op_table_assoc(int i)
{
    return ro_byte(&tok_table[i].assoc);
}

// The operator's spelling, for the disassembler. Same ro_ptr discipline as the
// rest -- csp_print.c used to index the table raw.
rostring_t op_table_name(int i)
{
    return (rostring_t) ro_ptr(&tok_table[i].name);
}
