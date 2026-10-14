%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    List csp assember/image in candyspeak source form
%%% @end
%%% Created : 14 Oct 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_to_candyspeak).

-export([file/1]).

file(Filename) ->
    case csp_disasm:file(Filename) of
	{ok, {_Header, Strings, Decl, Code}} ->
	    source(Strings, Decl, Code);
	Error ->
	    Error
    end.

source(_Strings, Decl, Code) ->
    {ok, Ds} = decl(Decl),
    {ok, Cs} = instr(Code), %% build rule ast
    {Ds, Cs}.

decl(Decl) ->
    decl(Decl, #{}, [], [[]]).

decl([D={decl,MAddr,module,Name,MParams}|Ds], DMap, Stack, Acc) ->
    decl(Ds, DMap, [D|Stack], [[]|Acc]);
decl([D={decl,Addr,object,Module,Name,Params}|Ds], DMap, Stack, [Acc|Acc0]) ->
    decl(Ds, DMap#{ Name => D }, Stack, [[D|Acc]|Acc0]);
decl([D={decl,Addr,Type,Name,Params}|Ds], DMap, Stack, [Acc|Acc0]) ->
    decl(Ds, DMap#{ Name => D }, Stack, [[D|Acc]|Acc0]);
decl([D={decl,Addr,'end'}], DMap, [], [Acc0]) ->
    {ok, {lists:reverse(Acc0), DMap}};
decl([D={decl,Addr,'end'}|Ds], DMap,
     [S={decl,MAddr,module,Name,MParams}|Stack], [Acc|Acc0]) ->
    MDecl = lists:reverse(Acc),
    decl(Ds, DMap#{ Name => {decl,MAddr,module,Name,MParams++MDecl} },
	 Stack, Acc0).

instr(Code) ->
    instr(Code, #{}, #{}, []).

instr([I={instr,Addr,'SEGMENT',_Params}|Is], Reg, Mem, Acc) ->
    %% drop segments, we already have all strings
    instr(Is, Reg, Mem, Acc);

instr([I={instr, Addr, 'LD',  [Rx, {mem,I}]} | Is], Reg, Mem, Acc) ->
    Reg1 = maps:put(Rx, {'WORD', 0, maps:get(I, Mem)}, Reg),
    instr(Is, Reg1, Mem, Acc);
instr([I={instr, Addr, 'LI',  [Rx, {imm,V}]} | Is], Reg, Mem, Acc) ->
    Reg1 = maps:put(Rx, {'INT', 0, V}, Reg),
    instr(Is, Reg1, Mem, Acc);
instr([I={instr, Addr, Op, Params}|Is], Reg, Mem, Acc) ->
    instr(Is, Reg, Mem, [I|Acc]);
instr([], _Reg, _Mem, Acc) ->
    {ok, lists:reverse(Acc)}.

    
