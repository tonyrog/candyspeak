%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image assembler
%%% @end
%%% Created :  5 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_asm).

-export([file/1]).
-export([single/1, list/1]).
-export([reg/1, part/1, type/1, dir/1, endian/1]).
-export([decl/1, op_code/1, op_class/1]).

-include("csp_asm.hrl").

-define(MAP(Key,Val), Key => Val).

reg(Reg) -> maps:get(Reg, reg()).
reg() -> #{ ?REG_MAP }.

part(Part) -> maps:get(Part, part()).
part() -> #{ ?PART_MAP }.

type(Type) -> maps:get(Type, type()).
type() -> #{ ?TYPE_MAP }.

dir(Dir) -> maps:get(Dir, dir()).
dir() -> #{ ?DIR_MAP }.

endian(Endian) -> maps:get(Endian, endian()).
endian() -> #{ ?ENDIAN_MAP }.

decl(Type) -> maps:get(Type, decl()).
decl() -> #{ ?DECL_MAP }.

op_class(OpCode) -> maps:get(OpCode, ?OPCLASS_MAP).

op_code() -> #{ ?OPCODE_MAP }.
op_code(OpCode) -> maps:get(OpCode, op_code()).

file(Filename) ->
    case file:consult(Filename) of
	{ok, List} ->
	    list(List);
	Error ->
	    Error
    end.

list([I | Is]) ->
    [single(I)  | list(Is)];
list([]) ->
    [].

%% instruktions from consult or similar do not have address yet
single(I) -> single(I, little).
single({instr, OpCode, Args}, Endian) ->
    Op = op_code(OpCode),
    OpClass = op_class(OpCode),
    case OpClass of
	'end' ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}], Endian);
	'nop' ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}], Endian);
	seg ->
	    error(fixme);

	instate ->
	    [{x,Rx},{imm,Imm},{nxt,Nxt}|Args1] = Args,
	    X = reg(Rx),
	    Implicit = get_bool(lists:member(implicit, Args1)),
	    encode_bits([Op, X, Imm, Nxt, Implicit],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {imm, int, 8},
			 {nxt, int, 13},
			 {implicit, uint, 1}], Endian);
	rule ->	
	    [{cnd,Cnd},{nxt,Nxt}|Args1] = Args,
	    C = reg(Cnd),
	    Implicit = get_bool(lists:member(implicit, Args1)),
	    encode_bits([Op, C, Nxt, Implicit],
			[{opcode,uint,?OPCODE_BITS},
			 {'_', 6},
			 {cnd, uint, ?REG_BITS},
			 {imm, int, 8},
			 {nxt, int, 13},
			 {implicit, uint, 1}], Endian);
	enter ->
	    [{num,Num},{mx,Mx}] = Args,
	    encode_bits([Op, Num, Mx],
			[{opcode,uint,?OPCODE_BITS},
			 {num, uint, ?BODY_BITS},
			  {mx,  uint, ?INDEX_BITS}], Endian);
	leave ->
	    [{num,Num},{mx,Mx}] = Args,
	    encode_bits([Op, Num, Mx],
			[{opcode,uint,?OPCODE_BITS},
			 {num, uint, ?BODY_BITS},
			 {mx,  uint, ?INDEX_BITS}], Endian);

	next ->
	    [{x,Rx}] = Args,
	    X = reg(Rx),
	    encode_bits([Op, X],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS}], Endian);
	new ->
	    [{obj,Obj}] = Args,
	    encode_bits([Op,Obj],
			[{opcode,uint,?OPCODE_BITS},
			 {'_', 2},
			 {obj, uint, ?INDEX_BITS}], Endian);

	call ->
	    [{x,Rx},{idx,Idx},{usr,Usr},{avt,Ts}] = Args,
	    X = reg(Rx),
	    Avt = avt(Ts),
	    encode_bits([Op,X, Idx, Usr, Avt],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {idx, uint, ?FUNC_BITS},
			 {usr, uint, 1},
			 {avt, uint, 16}], Endian);
	alu1 ->
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
			 {swap, uint, 1}], Endian);
	alu2 ->
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
			 {swap, uint, 1}], Endian);
	mem ->
	    [Rx,Ry,Mem|_Args1] = Args,
	    X = reg(Rx),
	    Y = reg(Ry),
	    encode_bits([Op,X,Y,Mem],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {y, uint, ?REG_BITS},
			 {'_', 2},
			 {mem, uint, ?INDEX_BITS}], Endian)
    end.

get_bool(true) -> 1;
get_bool(false) ->  0.

avt([]) -> 0;
avt([T|Ts]) -> type(T) bor (avt(Ts) bsr 4).

-define(set_uint(Val, Start, Len, Acc),
	((Acc band (bnot (?MASK(Len) bsl Start))) bor
	     ((Val band ?MASK(Len)) bsl Start))).

-define(set_int(Val, Start, Len, Acc),
	?set_uint(Val, Start, Len, Acc)).

encode_bits(Values, Fields, Endian) ->
    encode_bits(Values, 0, Fields, 0, Endian).

encode_bits(Values, Pos, Fields, Acc, Endian) ->
    {Pos, Word} = encode_bits_(Values, Pos, Fields, Acc),
    case Endian of
	little -> {Pos, <<Word:32/little>>};
	big    -> {Pos, <<Word:32/big>>};
	native -> {Pos, <<Word:32/native>>}
    end.

encode_bits_(Values, Pos, [{'_',Len}|Fields], Acc) -> %% pad
    encode_bits_(Values, Pos+Len, Fields, ?set_uint(0, Pos, Len, Acc));
encode_bits_([Val|Values], Pos, [{_Name,uint,Len}|Fields], Acc) ->
    encode_bits_(Values, Pos+Len, Fields, ?set_uint(Val, Pos, Len, Acc));
encode_bits_([Val|Values], Pos, [{_Name,int,Len}|Fields], Acc) ->
    encode_bits_(Values, Pos+Len, Fields, ?set_int(Val, Pos, Len, Acc));
encode_bits_([], Pos, [], Acc) ->
    {Pos, Acc}.

			 
	    

    
