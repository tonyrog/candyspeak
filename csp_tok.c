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

const op_entry_t tok_table[] RODATA = {
    INSTR_ENT(NONE,OP_NOP,s_NOP,-1,0,NO),
    INSTR_ENT(EXCLAMATION,OP_NOT,s_EXCLAMATION,1,105,RIGHT),
    INSTR_ENT(TILDE,OP_BNOT,s_TILDE,1,105,RIGHT),
    INSTR_ENT(MINUS1,OP_NEG,s_MINUS,1,105,RIGHT),
    INSTR_ENT(PLUS1,OP_MOV,s_MOV,1,105,RIGHT),
    // node - binary
    INSTR_ENT(PLUS,OP_ADD,s_PLUS,2,90,LEFT),
    INSTR_ENT(MINUS,OP_SUB,s_MINUS,2,90,LEFT),
    INSTR_ENT(ASTERISK,OP_MUL,s_ASTERISK,2,100,LEFT),
    INSTR_ENT(SLASH,OP_DIV,s_SLASH,2,100,LEFT),
    INSTR_ENT(PERCENT,OP_REM,s_PERCENT,2,100,LEFT),
    INSTR_ENT(LTLT,OP_SLA,s_LTLT,2,80,LEFT),
    INSTR_ENT(GTGT,OP_SRA,s_GTGT,2,80,LEFT),
    INSTR_ENT(LT,OP_LT,s_LT,2,70,LEFT),
    INSTR_ENT(LTEQ,OP_LTE,s_LTEQ,2,70,LEFT),
    INSTR_ENT(GT,OP_GT,s_GT,2,70,LEFT),
    INSTR_ENT(GTEQ,OP_GTE,s_GTEQ,2,70,LEFT),
    INSTR_ENT(EQEQ,OP_EQEQ,s_EQEQ,2,60,LEFT),
    INSTR_ENT(NEQ,OP_NEQ,s_NEQ,2,60,LEFT),
    INSTR_ENT(AMP,OP_BAND,s_AMP,2,50,LEFT),
    INSTR_ENT(CIRC,OP_BXOR,s_CIRC,2,40,LEFT),
    INSTR_ENT(BAR,OP_BOR,s_BAR,2,30,LEFT),
    INSTR_ENT(AMPAMP,OP_AND,s_AMPAMP,2,20,LEFT),
    INSTR_ENT(BARBAR,OP_OR,s_BARBAR,2,10,LEFT),
    // EQ/RIMP are shunted as low-precedence right-assoc operators so that the
    // expression parser handles `var = expr` -- needed for immediate `> T1=1`
    // (one-shot assignment / timer start). Rules use asm_rule, not this path.
    INSTR_ENT(EQ,OP_EQ,s_EQ,2,5,RIGHT),       // assign_expr
    INSTR_ENT(RIMP,OP_RIMP,s_RIMP,2,4,RIGHT), // assign_expr
    // INSTR_ENT(COMMA,OP_COMMA,s_COMMA,2,2,RIGHT),
    INSTR_ENT(COMMA,OP_NOP,s_COMMA,2,2,RIGHT),    
    INSTR_ENT(QUEST,OP_RULE,s_QUEST,-1,-1,NO),

    TOK_ENT(T_PULLUP,OP_NOP,s_pullup),
    TOK_ENT(T_PULLDOWN,OP_NOP,s_pulldown),
    TOK_ENT(T_RESOLUTION,OP_NOP,s_resolution),
    TOK_ENT(T_IN,OP_NOP,s_in),
    TOK_ENT(T_OUT,OP_NOP,s_out),
    TOK_ENT(T_INOUT,OP_NOP,s_inout),
    TOK_ENT(T_PWM,OP_NOP,s_pwm),
    TOK_ENT(T_FLOAT,OP_NOP,s_float),
    TOK_ENT(T_INTEGER,OP_NOP,s_integer),
    TOK_ENT(T_UNSIGNED,OP_NOP,s_unsigned),
    TOK_ENT(T_STRING,OP_NOP,s_string),
    TOK_ENT(T_NATIVE,OP_NOP,s_native),    
    TOK_ENT(T_LITTLE,OP_NOP,s_little),
    TOK_ENT(T_BIG,OP_NOP,s_big),
    TOK_ENT(T_CAN,OP_NOP,s_can),   // the #buffer TRANSPORT, not a declaration
    TOK_ENT(T_DISABLE,OP_NOP,s_disable),
    TOK_ENT(T_ENABLE,OP_NOP,s_enable),

    TOK_ENT(LP,OP_NOP,s_LP),
    TOK_ENT(RP,OP_NOP,s_RP),
    TOK_ENT(HASH,OP_NOP,s_HASH),
    TOK_ENT(DOT,OP_NOP,s_DOT),
    TOK_ENT(COLON,OP_NOP,s_COLON),
    TOK_ENT(LB,OP_NOP,s_LB),
    TOK_ENT(RB,OP_NOP,s_RB),
    TOK_ENT(INT,OP_NOP,s_null),
    TOK_ENT(FLT,OP_NOP,s_null),
    TOK_ENT(WORD,OP_NOP,s_null),
    TOK_ENT(NEWLINE,OP_NOP,s_null),
    // eot
    TOK_ENT(T_LAST,OP_NOP,s_null)
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
