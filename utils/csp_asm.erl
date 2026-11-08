%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image assembler
%%% @end
%%% Created :  5 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_asm).

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

list([I | Is]) ->
    [single(I)  | list(Is)];
list([]) ->
    [].


single({instr, _Addr, OpCode, Args}) ->
    Op = op_code(OpCode),
    OpClass = op_class(OpCode),
    case OpClass of
	'end' ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}]);
	'nop' ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}]);

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
			 {swap, uint, 1}]);
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
			 {swap, uint, 1}]);
	mem ->
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
			 
	    

    
