%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image assembler
%%% @end
%%% Created :  5 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_asm).

-export([single/1, list/1]).

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

-define(OPCODE_MASK, ?MASK(?OPCODE_BITS)).
-define(REG_MASK, ?MASK(?REG_BITS)).
-define(PART_MASK, ?MASK(?PART_BITS)).
-define(DIR_MASK, ?MASK(?DIR_BITS)).
-define(ENDIAN_MASK, ?MASK(?ENDIAN_BITS)).
-define(DECL_TYPE_MASK, ?MASK(?DECL_TYPE_BITS)).
-define(TYPE_MASK, ?MASK(?TYPE_BITS)).

-define(MAP(Key,Val), Key => Val).

reg(Reg) ->
    maps:get(Reg, reg()).
reg() ->
    #{ ?MAP(r0,0), ?MAP(r1,1), ?MAP(r2,2), ?MAP(r3,3),
       ?MAP(r4,4), ?MAP(r5,5), ?MAP(r6,6), ?MAP(r7,7),
       ?MAP(r8,8), ?MAP(r9,9), ?MAP(r10,10), ?MAP(r11,11),
       ?MAP(r12,12), ?MAP(r13,13), ?MAP(r14,14), ?MAP(r15,15) }.

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

part() ->
#{
  ?MAP(val,?PART_VAL),
  ?MAP(pin, ?PART_PIN),
  ?MAP(port, ?PART_PORT),
  ?MAP(dir, ?PART_DIR),
  ?MAP(pwm, ?PART_PWM),
  ?MAP(endian, ?PART_ENDIAN),
  ?MAP(pullup, ?PART_PULLUP),
  ?MAP(pulldown,  ?PART_PULLDOWN),
  ?MAP(period, ?PART_PERIOD),
  ?MAP(fired, ?PART_FIRED),
  ?MAP(id,  ?PART_ID),
  ?MAP(rx, ?PART_RX),
  ?MAP(tx, ?PART_TX),
  ?MAP(dlc, ?PART_DLC),
  ?MAP(len, ?PART_LEN)
}.

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

opcodes() ->
    #{ 'NOP' => {?OP_NOP, nop},
       'NOT' => {?OP_NOT, alu, 1},
       'BNOT' => {?OP_BNOT, alu, 1},
       'NEG' => {?OP_NEG, alu, 1},
       'MOV' => {?OP_MOV, alu, 1},
       'CVTIF' => {?OP_CVTIF, alu, 1},
       'CVTFI' => {?OP_CVTFI, alu, 1},
       'ADD' => {?OP_ADD, alu, 2},
       'SUB' => {?OP_SUB, alu, 2},
       'MUL' => {?OP_MUL, alu, 2},
       'DIV' => {?OP_DIV, alu, 2},
       'REM' => {?OP_REM, alu, 2},
       'SLA' => {?OP_SLA, alu, 2},
       'SRA' => {?OP_SRA, alu, 2},
       'LT'  => {?OP_LT,   alu, 2},
       'LTE' => {?OP_LTE, alu, 2},
       'TMO' => {?OP_TMO, mem},
       'SEGMENT' => {?OP_SEGMENT, seg},
       'EQEQ' => {?OP_EQEQ, alu, 2},
       'NEQ' => {?OP_NEQ,  alu, 2},
       'BAND' => {?OP_BAND, alu, 2},
       'BOR' => {?OP_BOR,  alu, 2},
       'BXOR' => {?OP_BXOR, alu, 2},
       'AND' => {?OP_AND,  alu, 2},
       'OR' => {?OP_OR,   alu, 2},
       'FNEG' => {?OP_FNEG, alu, 1},
       'FMOV' => {?OP_FMOV, alu, 1},
       'FADD' => {?OP_FADD, alu, 2},
       'FSUB' => {?OP_FSUB, alu, 2},
       'FMUL' => {?OP_FMUL, alu, 2},
       'FDIV' => {?OP_FDIV, alu, 2},
       'FLT' => {?OP_FLT,  alu, 2},
       'FLTE' => {?OP_FLTE, alu, 2},
       'FEQEQ' => {?OP_FEQEQ, alu, 2},
       'FNEQ' => {?OP_FNEQ, alu, 2},
       'EQ' => {?OP_EQ, alu ,2},
       'RIMP' => {?OP_RIMP, alu, 2},
       'RULE' => {?OP_RULE, rule},
       'NEXT' => {?OP_NEXT, next},
       'ENTER' => {?OP_ENTER, enter},
       'LEAVE' => {?OP_LEAVE, leave},
       'NEW' => {?OP_NEW, new},
       'LD' => {?OP_LD, mem},
       'LDP' => {?OP_LDP, mem},
       'ST' => {?OP_ST, mem},
       'STP' => {?OP_STP, mem},
       'STIMP' => {?OP_STIMP, mem},
       'CHG' => {?OP_CHG, mem},
       'LI' => {?OP_LI, imm},
       'LIU' => {?OP_LIU, imm},
       'LIH' => {?OP_LIH, imm},
       'ARG' => {?OP_ARG, imm},
       'CALL' => {?OP_CALL, call},
       'STI' => {?OP_STI, memi},
       'INSTATE' => {?OP_INSTATE, instate},
       'NINSTATE' => {?OP_NINSTATE, instate},
       'SETO' => {?OP_SETO, seto},
       'SETOX' => {?OP_SETOX, setox},
       'END' => {?OP_END_MARK, 'end'}
     }.

list([I | Is]) ->
    [single(I)  | list(Is)];
list([]) ->
    [].


single({instr, _Addr, OpCode, Args}) ->
    case maps:get(OpCode, opcodes()) of
	{Op, 'end'} ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}]);
	{Op, 'nop'} ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}]);

	{Op,alu,1} ->
	    [Rx,Ry|Args1] = Args,
	    U = get_bool(lists:member(unsigned, Args1)),
	    Swap = get_bool(lists:member(swap, Args1)),
	    X = reg(Rx),
	    Y = reg(Ry),
	    encode_bits([Op,X,Y,U,Swap],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {y, uint, ?REG_BITS},
			 {'_', ?REG_BITS},
			 {u, uint, 1},
			 {swap, uint, 1}]);
	{Op,alu,2} ->
	    [Rx,Ry,Rz|Args1] = Args,
	    U = get_bool(lists:member(unsigned, Args1)),
	    Swap = get_bool(lists:member(swap, Args1)),
	    X = reg(Rx),
	    Y = reg(Ry),
	    Z = reg(Rz),
	    encode_bits([Op,X,Y,Z,U,Swap],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {y, uint, ?REG_BITS},
			 {z, uint, ?REG_BITS},
			 {u, uint, 1},
			 {swap, uint, 1}]);
	{Op, mem} ->
	    [Rx,Ry,Mem|_Args1] = Args,
	    X = reg(Rx),
	    Y = reg(Ry),
	    encode_bits([Op,X,Y,Mem],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {y, uint, ?REG_BITS},
			 {'_', 2},
			 {mem, uint, ?INDEX_BITS}])
    end.

get_bool(true) -> 1;
get_bool(false) ->  0.


-define(set_uint(Val, Start, Len, Acc),
	((Acc band (bnot (?MASK(Len) bsl Start))) bor
	     ((Val band ?MASK(Len)) bsl Start))).

-define(set_int(Val, Start, Len, Acc),
	?set_uint(Val, Start, Len, Acc)).

encode_bits(Values, Fields) ->
    encode_bits(Values, 0, Fields, 0).

encode_bits(Values, Pos, [{'_',Len}|Fields], Acc) -> %% pad
    encode_bits(Values, Pos+Len, Fields, ?set_uint(0, Pos, Len, Acc));
encode_bits([Val|Values], Pos, [{_Name,uint,Len}|Fields], Acc) ->
    encode_bits(Values, Pos+Len, Fields, ?set_uint(Val, Pos, Len, Acc));
encode_bits([Val|Values], Pos, [{_Name,int,Len}|Fields], Acc) ->
    encode_bits(Values, Pos+Len, Fields, ?set_int(Val, Pos, Len, Acc));
encode_bits([], Pos, [], Acc) ->
    {Pos, <<Acc:32/little>>}.
			 
	    

    
