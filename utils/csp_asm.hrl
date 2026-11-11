-ifndef(__CSP_ASM_HRL__).
-define(__CSP_ASM_HRL__, true).

-define(MASK(Len), ((1 bsl (Len))-1)).

-define(OPCODE_BITS,      6).
-define(DECL_TYPE_BITS,   4).
-define(BODY_BITS,        10).
-define(REG_BITS,         4).
-define(FUNC_BITS,        5).
-define(PART_BITS,        4).
-define(OBJ_BITS,         1).
-define(DECL_BITS,        15).
-define(INDEX_BITS,       (?OBJ_BITS+?DECL_BITS)).
-define(TINY_BITS,        6).
-define(TYPE_BITS,        4).
-define(PORT_BITS,        4).
-define(PIN_BITS,         7).
-define(DIR_BITS,         2).
-define(NAMEID_BITS,      8).
-define(ENDIAN_BITS,      2).
-define(RES_BITS,         5).

-define(OPCODE_MASK, ?MASK(?OPCODE_BITS)).
-define(REG_MASK, ?MASK(?REG_BITS)).
-define(PART_MASK, ?MASK(?PART_BITS)).
-define(DIR_MASK, ?MASK(?DIR_BITS)).
-define(ENDIAN_MASK, ?MASK(?ENDIAN_BITS)).
-define(DECL_TYPE_MASK, ?MASK(?DECL_TYPE_BITS)).
-define(TYPE_MASK, ?MASK(?TYPE_BITS)).

-define(REG_MAP, 
	?MAP(r0,0), ?MAP(r1,1), ?MAP(r2,2), ?MAP(r3,3),
	?MAP(r4,4), ?MAP(r5,5), ?MAP(r6,6), ?MAP(r7,7),
	?MAP(r8,8), ?MAP(r9,9), ?MAP(r10,10), ?MAP(r11,11),
	?MAP(r12,12), ?MAP(r13,13), ?MAP(r14,14), ?MAP(r15,15)).

%% Parts STP/LDP
-define(PART_VAL, 0).
-define(PART_PIN, 1).
-define(PART_PORT, 2).
-define(PART_DIR, 3).
-define(PART_PWM, 4).
-define(PART_ENDIAN, 5).
-define(PART_PULLUP, 6).
-define(PART_PULLDOWN, 7).
-define(PART_PERIOD, 8).
-define(PART_FIRED, 9).
-define(PART_ID, 10).
-define(PART_RX, 11).
-define(PART_TX, 12).
-define(PART_DLC, 13).
-define(PART_LEN, 14).

-define(PART_MAP, 
	?MAP(val,?PART_VAL), ?MAP(pin, ?PART_PIN),
	?MAP(port, ?PART_PORT), ?MAP(dir, ?PART_DIR),
	?MAP(pwm, ?PART_PWM), ?MAP(endian, ?PART_ENDIAN),
	?MAP(pullup, ?PART_PULLUP), ?MAP(pulldown,  ?PART_PULLDOWN),
	?MAP(period, ?PART_PERIOD), ?MAP(fired, ?PART_FIRED),
	?MAP(id,  ?PART_ID), ?MAP(rx, ?PART_RX),
	?MAP(tx, ?PART_TX), ?MAP(dlc, ?PART_DLC),
	?MAP(len, ?PART_LEN)).

-define(V_VOID,     0).
-define(V_INTEGER,  1).
-define(V_UNSIGNED, 2).
-define(V_FLOAT,    3).
-define(V_STRING,   4).
-define(V_INDEX,    5).
-define(V_NUMBER,   6).
-define(V_ANY,      7).
-define(V_TIMER,    8).
-define(V_DIGITAL,  9).
-define(V_ANALOG,   10).
-define(V_FIELD,   11).   

-define(TYPE_MAP, 
	?MAP(void, ?V_VOID),
	?MAP(integer, ?V_INTEGER),
	?MAP( unsigned,  ?V_UNSIGNED),
	?MAP(float,  ?V_FLOAT),
	?MAP(string,  ?V_STRING),
	?MAP(index,  ?V_INDEX),
	?MAP(number,  ?V_NUMBER),
	?MAP(any,  ?V_ANY),
	?MAP(timer,  ?V_TIMER),
	?MAP(digital,  ?V_DIGITAL),
	?MAP(analog,  ?V_ANALOG),
	?MAP(field, ?V_FIELD)).

-define(DECL_NONE, 0).
-define(DECL_VARIABLE, 1).
-define(DECL_CONSTANT, 2).
-define(DECL_MODULE,   3).
-define(DECL_END,      4).
-define(DECL_OBJECT,   5).
-define(DECL_STATES,   6).
-define(DECL_IN,       7).
-define(DECL_TIMER, ?V_TIMER).
-define(DECL_DIGITAL, ?V_DIGITAL).
-define(DECL_ANALOG, ?V_ANALOG).
-define(DECL_FIELD, ?V_FIELD).
-define(DECL_BUFFER, 12).
-define(DECL_VIEW, 13).
-define(DECL_ROUTE, 14).
-define(DECL_END_MARK, 15).

-define(DECL_MAP, 
	?MAP(none, ?DECL_NONE),
	?MAP(variable,?DECL_VARIABLE),
	?MAP(constant,?DECL_CONSTANT),
	?MAP(module, ?DECL_MODULE),
	?MAP('end', ?DECL_END),
	?MAP(object,?DECL_OBJECT),
	?MAP(states,?DECL_STATES),
	?MAP('in', ?DECL_IN),
	?MAP(timer, ?DECL_TIMER),
	?MAP(digital, ?DECL_DIGITAL),
	?MAP(analog, ?DECL_ANALOG),
	?MAP(field, ?DECL_FIELD),
	?MAP(buffer,?DECL_BUFFER),
	?MAP(view,?DECL_VIEW),
	?MAP(route,?DECL_ROUTE),
	?MAP(end_mark, ?DECL_END_MARK)).

-define(DIR_NONE,  16#00).
-define(DIR_IN,    16#01).
-define(DIR_OUT,   16#02).
-define(DIR_INOUT, 16#03).

-define(DIR_MAP, 
	?MAP(none,?DIR_NONE),
	?MAP(in, ?DIR_IN),
	?MAP(out, ?DIR_OUT),
	?MAP(inout, ?DIR_INOUT)).

-define(E_NATIVE,    16#00). 
-define(E_LITTLE,    16#01).
-define(E_BIG,       16#02).
-define(E_UNDEFINED, 16#03).

-define(ENDIAN_MAP,
	?MAP(native, ?E_NATIVE),
	?MAP(little, ?E_LITTLE),
	?MAP(big, ?E_BIG),
	?MAP(undefined, ?E_UNDEFINED)).

%% Opcodes
-define(OP_NOP, 0).  %% nothing
-define(OP_NOT, 1).     %% "!"  x=-y == x=0-y
-define(OP_BNOT, 2).    %% "~"  x=~y =  x=1^y        
-define(OP_NEG , 3).     %% "-"  x=-y == x=0-y
-define(OP_MOV , 4).     %% "mov" x=y == x=y
-define(OP_CVTIF, 5).   %% trunc float => integer
-define(OP_CVTFI, 6).   %% cast int to float
    %% node - binary operator
-define(OP_ADD  , 7).     %% "+"
-define(OP_SUB  , 8).     %% "-"
-define(OP_MUL  , 9).     %% "*"
-define(OP_DIV  , 10).     %% "/"
-define(OP_REM  , 11).     %% "%"
-define(OP_SLA  , 12).     %% "<<"
-define(OP_SRA  , 13).     %% ">>"    
-define(OP_LT  , 14).      %% "<"
-define(OP_LTE  , 15).     %% "<="
    %% `>` and `>=` used to be 16 and 17; the compiler mirrors them into
    %% OP_LT/OP_LTE with the operands swapped (see mirror_op / asm_alu), so the
    %% numbers came free. 16 is spent again below; 17 is still free.
-define(OP_TMO  , 16).     %% timeout(T): x = the timer's `fired` bit
    %% 17 -- FREE
-define(OP_EQEQ, 18).    %% "=="
-define(OP_NEQ, 19).     %% "!="
-define(OP_BAND, 20).    %% "&"
-define(OP_BOR, 21).     %% "|"
-define(OP_BXOR, 22).    %% "^"
-define(OP_AND, 23).     %% "&&"
-define(OP_OR, 24).      %% "||"

-define(OP_FNEG, 25).     %% "-"  x=-y == x=0-y
-define(OP_FMOV, 26).     %% "mov"  x=y
-define(OP_FADD, 27).     %% "+"
-define(OP_FSUB, 28).     %% "-"
-define(OP_FMUL, 29).     %% "*"
-define(OP_FDIV, 30).     %% "/"

-define(OP_FLT, 31).      %% "<"
-define(OP_FLTE, 32).      %% "<="
    %% 33, 34 -- FREE, for the same reason as 16 and 17 above.
-define(OP_FEQEQ, 35).     %% "=="
-define(OP_FNEQ, 36).     %% "!="    
    
-define(OP_EQ, 37).     %% "="
-define(OP_RIMP, 38).     %% "<-"    

-define(OP_RULE, 39).    %% "?"
-define(OP_NEXT, 40).    %% "next"

-define(OP_ENTER, 41).   %% enter object
-define(OP_LEAVE, 42).   %% leave object
-define(OP_NEW, 43).     %% #<module> <instance-name>
-define(OP_LD, 44).      %% load register from memory
-define(OP_LDP, 45).     %% load register from memory part
-define(OP_ST, 46).      %% store register to memory
-define(OP_STP, 47).     %% store register to memory part
-define(OP_STIMP, 48).  %% store for <- (reactive assign), same as ST but marks rimp
-define(OP_CHG, 49).     %% r |= dset[ix], check if variable changed
-define(OP_LI, 50).      %% load signed 16-bit constant
-define(OP_LIU, 51).     %% load unsigned 16-bit constant (zero extend)
-define(OP_LIH, 52).    %% load high 16-bit (OR into high bits)
-define(OP_ARG, 53).    %% load argument from register
-define(OP_CALL, 54).   %% function call:
-define(OP_STI, 55).    %% store immediate value to memory (mirror of EQI)
    %% A run of identifier text living in the INSTRUCTION pool: this header, then
    %% num slots of characters. Executing it JUMPS the payload, which is what
    %% makes a segment transparent -- it lands mid-stream because a name is
    %% created while code is being generated, and there is no moving it
    %% afterwards without renumbering every jump.
    %%
    %% 17 is a hole (where `>` used to be, before it was mirrored to LT+swap),
    %% taken ahead of 60..62 as the note at OP_AVAIL asks.
-define(OP_SEGMENT,17).

-define(OP_INSTATE,56). %% #in <state> block gate: if reg != state, skip block(nxt)
-define(OP_NINSTATE,57). %% #in A B C OR-chain gate: if reg == state, jump INTO block (nxt)
-define(OP_SETO, 58).    %% point CURRENT at a NAMED object for the next memory access
-define(OP_SETOX, 59).   %% same, but the object number comes from a register (arrays)
-define(OP_END_MARK, 16#3f).

-define(OPCODE_MAP, 
       ?MAP('NOP', ?OP_NOP),
       ?MAP('NOT', ?OP_NOT),
       ?MAP('BNOT', ?OP_BNOT),
       ?MAP('NEG', ?OP_NEG),
       ?MAP('MOV', ?OP_MOV),
       ?MAP('CVTIF', ?OP_CVTIF),
       ?MAP('CVTFI', ?OP_CVTFI),
       ?MAP('ADD', ?OP_ADD  ),
       ?MAP('SUB', ?OP_SUB  ),
       ?MAP('MUL', ?OP_MUL  ),
       ?MAP('DIV', ?OP_DIV  ),
       ?MAP('REM', ?OP_REM  ),
       ?MAP('SLA', ?OP_SLA  ),
       ?MAP('SRA', ?OP_SRA  ),
       ?MAP('LT', ?OP_LT   ),
       ?MAP('LTE', ?OP_LTE  ),
       ?MAP('TMO', ?OP_TMO  ),
       ?MAP('SEGMENT', ?OP_SEGMENT),
       ?MAP('EQEQ', ?OP_EQEQ ),
       ?MAP('NEQ', ?OP_NEQ  ),
       ?MAP('BAND', ?OP_BAND ),
       ?MAP('BOR', ?OP_BOR  ),
       ?MAP('BXOR', ?OP_BXOR ),
       ?MAP('AND', ?OP_AND  ),
       ?MAP('OR', ?OP_OR   ),
       ?MAP('FNEG', ?OP_FNEG ),
       ?MAP('FMOV', ?OP_FMOV ),
       ?MAP('FADD', ?OP_FADD ),
       ?MAP('FSUB', ?OP_FSUB ),
       ?MAP('FMUL', ?OP_FMUL ),
       ?MAP('FDIV', ?OP_FDIV ),
       ?MAP('FLT', ?OP_FLT  ),
       ?MAP('FLTE', ?OP_FLTE ),
       ?MAP('FEQEQ', ?OP_FEQEQ),
       ?MAP('FNEQ', ?OP_FNEQ ),
       ?MAP('EQ', ?OP_EQ   ),
       ?MAP('RIMP', ?OP_RIMP ),
       ?MAP('RULE', ?OP_RULE ),
       ?MAP('NEXT', ?OP_NEXT ),
       ?MAP('ENTER', ?OP_ENTER),
       ?MAP('LEAVE', ?OP_LEAVE),
       ?MAP('NEW', ?OP_NEW  ),
       ?MAP('LD', ?OP_LD   ),
       ?MAP('LDP', ?OP_LDP  ),
       ?MAP('ST', ?OP_ST   ),
       ?MAP('STP', ?OP_STP  ),
       ?MAP('STIMP', ?OP_STIMP),
       ?MAP('CHG', ?OP_CHG  ),
       ?MAP('LI', ?OP_LI   ),
       ?MAP('LIU', ?OP_LIU  ),
       ?MAP('LIH', ?OP_LIH  ),
       ?MAP('ARG', ?OP_ARG  ),
       ?MAP('CALL', ?OP_CALL ),
       ?MAP('STI', ?OP_STI  ),
       ?MAP('INSTATE', ?OP_INSTATE),
       ?MAP('NINSTATE', ?OP_NINSTATE),
       ?MAP('SETO', ?OP_SETO),
       ?MAP('SETOX', ?OP_SETOX),
       ?MAP('END', ?OP_END_MARK)
       ).

-define(OPCLASS_MAP,
    #{ 'NOP' => nop,
       'NOT' => alu1,
       'BNOT' =>  alu1,
       'NEG' =>  alu1,
       'MOV' =>  alu1,
       'CVTIF' =>  alu1,
       'CVTFI' =>  alu1,
       'ADD' =>  alu2,
       'SUB' =>  alu2,
       'MUL' =>  alu2,
       'DIV' =>  alu2,
       'REM' =>  alu2,
       'SLA' =>  alu2,
       'SRA' =>  alu2,
       'LT' =>    alu2,
       'LTE' =>  alu2,
       'TMO' =>  mem,
       'SEGMENT' =>  seg,
       'EQEQ' =>  alu2,
       'NEQ' =>   alu2,
       'BAND' =>  alu2,
       'BOR' =>   alu2,
       'BXOR' =>  alu2,
       'AND' =>   alu2,
       'OR' =>    alu2,
       'FNEG' =>  alu1,
       'FMOV' =>  alu1,
       'FADD' =>  alu2,
       'FSUB' =>  alu2,
       'FMUL' =>  alu2,
       'FDIV' =>  alu2,
       'FLT' =>   alu2,
       'FLTE' =>  alu2,
       'FEQEQ' =>  alu2,
       'FNEQ' =>  alu2,
       'EQ' =>  alu2,
       'RIMP' =>  alu2,
       'RULE' =>  rule,
       'NEXT' =>  next,
       'ENTER' =>  enter,
       'LEAVE' =>  leave,
       'NEW' =>  new,
       'LD' =>  mem,
       'LDP' =>  mem,
       'ST' =>  mem,
       'STP' =>  mem,
       'STIMP' =>  mem,
       'CHG' =>  mem,
       'LI' =>  imm,
       'LIU' =>  imm,
       'LIH' =>  imm,
       'ARG' =>  imm,
       'CALL' =>  call,
       'STI' =>  memi,
       'INSTATE' =>  instate,
       'NINSTATE' =>  instate,
       'SETO' =>  seto,
       'SETOX' =>  setox,
       'END' =>  'end'}).

-endif.
