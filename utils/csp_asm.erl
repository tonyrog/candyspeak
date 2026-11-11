%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image assembler
%%% @end
%%% Created :  5 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_asm).

-export([file/1]).
-export([asm_decl/4, asm_instr/4, asm_list/1, asm_list/6]).
-export([reg/1, part/1, type/1, dir/1, endian/1]).
-export([decl/1, op_code/1, op_class/1]).

%%
-export([image/1]).

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
	    asm_list(List);
	Error ->
	    Error
    end.

%% test assemble from image disasm
image(Filename) ->
    case csp_disasm:file(Filename) of
	{ok,{_Header,_Decl,_Instr,Code}} ->
	    asm_list(Code);
	Error ->
	    Error
    end.

asm_list(Is) ->
    asm_list(Is, #{endian=>little}, little, 
	     #{ <<"INIT">> => 0, <<"NORMAL">> => 1, <<"FAILSAFE">> => 2}, 
	     [], []).

asm_list([I | Is], H, E, S, Ds, Cs) ->
    io:format("asm: ~p => ", [I]),
    case I of
	{header,H1} -> 
	    io:format("~p\n", [H1]),
	    asm_list(Is, H1, maps:get(endian, H1, little), S, Ds, Cs);
	{strings,S1} ->
	    io:format("~p\n", [S1]),
	    asm_list(Is, H, E, S1, Ds, Cs);
	Decl when element(1, Decl) =:= decl ->
	    {D, S1} = asm_decl(Decl, H,S,E),
	    io:format("~p\n", [D]),
	    asm_list(Is, H, E, S1, [D|Ds], Cs);
	Code when element(1, Code) =:= instr ->
	    {C, S1} = asm_instr(Code, H,S,E),
	    io:format("~p\n", [C]),
	    asm_list(Is, H, E, S1, Ds, [C|Cs])
    end;
asm_list([], H, _E, S, Ds, Cs) ->
    {H, S, lists:reverse(Ds), lists:reverse(Cs)}.

asm_decl({decl,Addr,'end'}, H,S,E) when is_integer(Addr) ->
    asm_decl({decl, 'end'}, H,S,E);
asm_decl({decl, 'end'}, _H,S,E) ->
    Bits = encode_bits([?DECL_END,0,0,0,0],
		       [{type,uint,?DECL_TYPE_BITS},
			 {cont,uint,1},
			 {local,uint,1},
			 {dir,uint,?DIR_BITS},
			 {name,uint,?NAMEID_BITS}]),
    {encode_word64(Bits,E), S};
asm_decl({decl,Addr,Type,Name,Args}, H,S,E) when is_integer(Addr) ->
    asm_decl({decl, Type, Name, Args}, H,S,E);
asm_decl({decl,Addr,Type,Mod,Obj,Args}, H,S,E) when is_integer(Addr) ->
    asm_decl({decl, Type, Mod, Obj, Args}, H,S,E);
asm_decl({decl, DeclType, Name, Args}, H,S,E) ->
    {PosWord,S1} = encode_decl_common(H, DeclType, Name, Args, S),
    {encode_word64(PosWord, E), S1};
asm_decl({decl, object, _Module, _Objekt, _Args}, _H,S,_E) ->
    {<<0:32>>, S}.

asm_instr({instr, _Addr, OpCode, Args}, H,S,E) ->
    asm_instr({instr, OpCode, Args}, H,S,E);
asm_instr(_I={instr, OpCode, Args}, _H,S,E) ->
    PosWord = encode_instr(OpCode, Args),
    {encode_word32(PosWord, E), S}.

add_string(String, Strings) ->
    case maps:get(String, Strings, undefined) of
	undefined ->
	    Index = maps:size(Strings)+1,
	    {Index, maps:put(String, Index, Strings)};
	Index ->
	    {Index, Strings}
    end.

encode_word32({{_Pos,Word},Data}, Endian) ->
    case Endian of
	little -> [<<Word:32/little>>,Data];
	big    -> [<<Word:32/big>>,Data];
	native -> [<<Word:32/native>>,Data]
    end;
encode_word32({_Pos,Word}, Endian) ->
    case Endian of
	little -> <<Word:32/little>>;
	big    -> <<Word:32/big>>;
	native -> <<Word:32/native>>
    end.

encode_word64({_Pos,Word}, Endian) ->
    case Endian of
	little -> <<Word:64/little>>;
	big    -> <<Word:64/big>>;
	native -> <<Word:64/native>>
    end.

encode_instr(OpCode, Args) ->
    Op = op_code(OpCode),
    OpClass = op_class(OpCode),
    case OpClass of
	'end' ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}]);

	seg ->
	    [{slots,Num}, {used,Used}, {data, Data}] = Args,
	    Seg = encode_bits([Op,Num,Used],
			      [{opcode,uint,?OPCODE_BITS},
			       {'_',2},
			       {slots,uint,?BODY_BITS},
			       {'_', 6},
			       {used,uint,8}]),
	    {Seg, Data};
	instate ->
	    [Rx,Imm,{nxt,Nxt}|Args1] = Args,
	    X = reg(Rx),
	    Implicit = lists:member(implicit, Args1),
	    encode_bits([Op, X, Imm, Nxt, Implicit],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {imm, int, 8},
			 {nxt, int, 13},
			 {implicit, uint, 1}]);
	rule ->	
	    [Cnd,Nxt|Args1] = Args,
	    C = reg(Cnd),
	    Implicit = lists:member(implicit, Args1),
	    encode_bits([Op, C, Nxt, Implicit],
			[{opcode,uint,?OPCODE_BITS},
			 {'_', 6},
			 {cnd, uint, ?REG_BITS},
			 {nxt, int, 15},
			 {implicit, uint, 1}]);
	enter ->
	    [{mem,Mx},{n,Num}] = Args,
	    encode_bits([Op, Num, Mx],
			[{opcode,uint,?OPCODE_BITS},
			 {num, uint, ?BODY_BITS},
			 {mx,  uint, ?INDEX_BITS}]);
	leave ->
	    [{mem,Mx},{n,Num}] = Args,
	    encode_bits([Op, Num, Mx],
			[{opcode,uint,?OPCODE_BITS},
			 {num, uint, ?BODY_BITS},
			 {mx,  uint, ?INDEX_BITS}]);

	next ->
	    [Rx] = Args,
	    X = reg(Rx),
	    encode_bits([Op, X],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS}]);
	new ->
	    [{obj,Obj}] = Args,
	    encode_bits([Op,Obj],
			[{opcode,uint,?OPCODE_BITS},
			 {'_', 2},
			 {obj, uint, ?INDEX_BITS}]);

	call ->
	    [Rx,{idx,Idx},{usr,Usr},{avt,Ts}] = Args,
	    X = reg(Rx),
	    Avt = avt(Ts),
	    encode_bits([Op,X, Idx, Usr, Avt],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {idx, uint, ?FUNC_BITS},
			 {usr, uint, 1},
			 {avt, uint, 16}]);
	alu1 ->
	    [Rx,Ry|Args1] = Args,
	    U = lists:member(unsigned, Args1),
	    Swap = lists:member(swap, Args1),
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
	    U = lists:member(unsigned, Args1),
	    Swap = lists:member(swap, Args1),
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
	    case Args of
		[Rx,{mem,Mem}] ->
		    X = reg(Rx),
		    Y = 0,
		    encode_bits([Op,X,Y,Mem],
				[{opcode,uint,?OPCODE_BITS},
				 {x, uint, ?REG_BITS},
				 {y, uint, ?REG_BITS},
				 {'_', 2},
				 {mem, uint, ?INDEX_BITS}]);
		[Rx,{mem,Mem},Part] when is_atom(Part) ->
		    X = reg(Rx),
		    P = part(Part),
		    encode_bits([Op,X,P,Mem],
				[{opcode,uint,?OPCODE_BITS},
				 {x, uint, ?REG_BITS},
				 {y, uint, ?REG_BITS},
				 {'_', 2},
				 {mem, uint, ?INDEX_BITS}])
	    end;
		    
	imm ->
	    [Rx,{imm,Imm}] = Args,
	    X = reg(Rx),
	    encode_bits([Op,X,Imm],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {'_',6},
			 {imm, int, 16}]);
	memi ->
	    [Rx,{imm,Imm},{mem,Mem}] = Args,
	    X = reg(Rx),	    
	    encode_bits([Op,X,Imm,Mem],
			[{opcode,uint,?OPCODE_BITS},
			 {x, uint, ?REG_BITS},
			 {imm, int, ?TINY_BITS},
			 {mem, uint, ?INDEX_BITS}]);
	seto ->
	    [{obj,Obj}] = Args,
	    encode_bits([Op, Obj],
			[{opcode,uint,?OPCODE_BITS},
			 {'_', 2},
			 {obj, uint, 16}]);
	setox ->
	    [{bits,Bits},{len,Len},Rx,{stride,Stride}] = Args,
	    X = reg(Rx),
	    encode_bits([Op, Bits, Len, X, Stride],
			[{opcode,uint,?OPCODE_BITS},
			 {'_', 1},
			 {bits, uint, 1},
			 {len, uint, 14},
			 {x, uint, ?REG_BITS},
			 {stride, uint, 6}]);
	'nop' ->
	    encode_bits([Op],
			[{opcode,uint,?OPCODE_BITS}])
    end.

encode_decl_common(H, DeclType, Name, Args, Strings) ->
    {IName, Strings1} = add_string(Name, Strings),
    {IType, ILocal} = encode_decl_type(DeclType),
    IDir = case lists:keyfind(dir, 1, Args) of
	       {_, in} -> ?DIR_IN;
	       {_, out} -> ?DIR_OUT;
	       {_,inout} -> ?DIR_INOUT;
	       {_,none} -> ?DIR_NONE;
	       false -> ?DIR_NONE
	   end,
    ICont = lists:keymember(len, 1, Args),
    Bits = 
	encode_bits([IType,ICont,ILocal,IDir,IName],
		     [{type,uint,?DECL_TYPE_BITS},
		      {cont,uint,1},
		      {local,uint,1},
		      {dir,uint,?DIR_BITS},
		      {name,uint,?NAMEID_BITS}]),
    encode_type_header(H, DeclType, Bits, Args, Strings1).


encode_type_header(H,local, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);
encode_type_header(H,constant, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);    
encode_type_header(H,variable, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);
encode_type_header(H,param, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);
encode_type_header(H, digital, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);
encode_type_header(H, analog, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);
encode_type_header(H, timer, Bits, Args, Strings) ->
    Type = proplists:get_value(type, Args),
    {Bits1,Strings1} = encode_type_header_(Args, Bits, Strings),
    encode_value(H, Args, Bits1, Type, Strings1);
encode_type_header(_, _, Bits, _Args, Strings) ->
    {Bits, Strings}.

encode_type_header_(Args, Bits, Strings) ->
    Res = proplists:get_value(res, Args, 32),
    Type = proplists:get_value(type, Args),
    Vt = type(Type),
    Bits1 = encode_bits([Res-1,0,0,0,Vt,0],
			[{res,uint,?RES_BITS},
			 {is_mapped, uint,1},
			 {bound,uint,1},
			 {nx,uint,1},
			 {vt,uint,?TYPE_BITS},
			 {reg,uint,?REG_BITS}], Bits),
    {Bits1, Strings}.

encode_decl_type(Decl) ->
    case Decl of
	variable -> {?DECL_VARIABLE, 0};
	local    -> {?DECL_VARIABLE, 1};
	constant -> {?DECL_CONSTANT, 0};
	param    -> {?DECL_CONSTANT, 1};
	module   -> {?DECL_MODULE, 0};
	'end'    -> {?DECL_END, 0};
	object   -> {?DECL_OBJECT, 0};
	states   -> {?DECL_STATES, 0};
	'in'     -> {?DECL_IN, 0};
	timer    -> {?DECL_TIMER, 0};
	digital  -> {?DECL_DIGITAL, 0};
	analog   -> {?DECL_ANALOG, 0};
	field    -> {?DECL_FIELD, 0};
	view     -> {?DECL_VIEW, 0};
	route    -> {?DECL_ROUTE, 0};
	end_mark -> {?DECL_END_MARK, 0}
    end.

encode_value(_H, Args, Bits, integer, Strings) ->
    Value = proplists:get_value(value, Args, 0),
    Bits1 = encode_bits([Value], [{ivalue,int,32}], Bits),
    {Bits1, Strings};
encode_value(_H, Args, Bits, unsigned, Strings) ->
    Value = proplists:get_value(value, Args, 0),
    Bits1 = encode_bits([Value], [{uvalue,uint,32}], Bits),
    {Bits1, Strings};
encode_value(H, Args, Bits, float, Strings) ->
    Value = float(proplists:get_value(value, Args, 0)),
    %% Q16.16 or IEEE binary32, as the header's flags say.
    Bits1 = case maps:get(float, H) of
		ieee -> encode_bits([Value], [{fvalue,float,32}], Bits);
		fixpoint -> encode_bits([Value], [{fvalue,fixpoint,32}], Bits)
	    end,
    {Bits1, Strings};
encode_value(_H, Args, Bits, string, Strings) ->
    String = proplists:get_value(value, Args, ""),
    {Si, Strings1} = add_string(String, Strings),
    {encode_bits([Si],[{sindex,int,32}],Bits), Strings1};
encode_value(_H, Args, Bits, timer, Strings) ->
    Period = proplists:get_value(period, Args, 0),
    Value  = proplists:get_value(value, Args, 0),
    Bits1 = encode_bits([Period,0,0,Value],
			[{period,uint,28},
			 {'_', 1},  %% not used
			 {fired,uint,1},
			 {running,uint,1},
			 {value,uint,1}], Bits),
    {Bits1, Strings};
encode_value(_H, Args, Bits, digital, Strings) ->
    Soft = proplists:get_value(soft, Args, 0),
    Pin  = proplists:get_value(pin, Args, 0),
    Port  = proplists:get_value(port, Args, 0),
    Irq  = proplists:get_value(irq, Args, 0),
    Pullup = proplists:get_value(pullup, Args, 0),
    Pulldown = proplists:get_value(pulldown, Args, 0),
    Bits1 = encode_bits([Soft,Port,Pin,Irq,Pullup,Pulldown],
			[{soft,uint,1},
			 {pin,uint,?PIN_BITS},
			 {port,uint,?PORT_BITS},
			 {irq,uint,3},
			 {pullup,uint,1},
			 {pulldown,uint,1}], Bits),
    {Bits1, Strings};
encode_value(_H, Args, Bits, analog, Strings) ->
    Soft = proplists:get_value(soft, Args, 0),
    Pin  = proplists:get_value(pin, Args, 0),
    Port  = proplists:get_value(port, Args, 0),
    Irq  = proplists:get_value(irq, Args, 0),
    Pwm  = proplists:get_value(pwm, Args, false),
    E = proplists:get_value(endian, Args, little),
    Endian = endian(E),     
    Bits1 = encode_bits([Soft,Pin,Port,Irq,Pwm,Endian],
			[{soft,uint,1},
			 {pin,uint,?PIN_BITS},
			 {port,uint,?PORT_BITS},
			 {irq,uint,3},
			 {pwm,uint,1},
			 {endian,uint,2}
			], Bits),
    {Bits1, Strings}.


avt([]) -> 0;
avt([T|Ts]) -> type(T) bor (avt(Ts) bsr 4).

-define(set_uint(Val, Start, Len, Acc),
	((Acc band (bnot (?MASK(Len) bsl Start))) bor
	     ((Val band ?MASK(Len)) bsl Start))).

-define(set_int(Val, Start, Len, Acc),
	?set_uint(Val, Start, Len, Acc)).

encode_bits(Values, Fields) ->
    encode_bits(Values, Fields, {0,0}).

encode_bits(Values, [{'_',Len}|Fields], _Bits={Pos,Word}) -> %% pad
    encode_bits(Values, Fields,
		{Pos+Len,?set_uint(0, Pos, Len, Word)});
%% allow true/false for {name, uint, 1}
encode_bits([true|Values], [{_Name,uint,1}|Fields], {Pos,Word}) ->
    encode_bits(Values, Fields, {Pos+1,?set_uint(1, Pos, 1, Word)});
encode_bits([false|Values], [{_Name,uint,1}|Fields], {Pos,Word}) ->
    encode_bits(Values, Fields, {Pos+1, ?set_uint(0, Pos, 1, Word)});
encode_bits([Val|Values], [{_Name,uint,Len}|Fields], {Pos,Word}) ->
    encode_bits(Values, Fields, {Pos+Len,?set_uint(Val, Pos, Len, Word)});
encode_bits([Val|Values], [{_Name,int,Len}|Fields], {Pos,Word}) ->
    encode_bits(Values, Fields, {Pos+Len, ?set_int(Val, Pos, Len, Word)});
encode_bits([Val|Values], [{_Name,float,32}|Fields], {Pos,Word}) ->
    <<X:32>> = <<Val:32/float>>,
    encode_bits(Values, Fields, {Pos+32,?set_int(X, Pos, 32, Word)});
encode_bits([Val|Values], [{_Name,fixpoint,32}|Fields], {Pos,Word}) ->
    X = trunc(Val * 65536),
    encode_bits(Values, Fields, {Pos+32,?set_int(X, Pos, 32, Word)});
encode_bits([], [], Bits) ->
    Bits.
			 
	    

    
