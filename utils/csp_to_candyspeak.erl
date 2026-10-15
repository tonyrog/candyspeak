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
    {ok, Ds={As,Dmap}} = decl(Decl),
    {ok, Cs} = instr(Code, Ds), %% build rule ast
    {As, Dmap, Cs}.

decl(Decl) ->
    decl(Decl, #{}, [], [[]]).

decl([D={decl,_Addr,module,_Name,_MParams}|Ds], DMap, Stack, Acc) ->
    decl(Ds, DMap, [D|Stack], [[]|Acc]);
decl([D={decl,Addr,object,_Module,Name,_Params}|Ds], DMap, Stack, [Acc|Acc0]) ->
    decl(Ds, DMap#{ Name => Addr, Addr => D }, Stack, [[D|Acc]|Acc0]);
decl([D={decl,Addr,_Type,Name,_Params}|Ds], DMap, Stack, [Acc|Acc0]) ->
    decl(Ds, DMap#{ Name => Addr, Addr => D }, Stack, [[D|Acc]|Acc0]);
decl([{decl,_Addr,'end'}], Dmap, [], [Acc0]) ->
    As = [element(2, Di) || Di <- lists:reverse(Acc0)],
    {ok, {As, Dmap}};
decl([{decl,_Addr,'end'}|Ds], DMap,
     [{decl,Addr,module,Name,_MParams}|Stack], [Acc|Acc0]) ->
    As = [element(2, Di) || Di <- lists:reverse(Acc)],
    %% do we need MParams?
    Mod = {decl,Addr,module,Name,As},
    decl(Ds, DMap#{ Name => Addr, Addr => Mod },
	 Stack, Acc0).

%% Is = list of declararaion indices and Mem contain 
%%  simple mem index to declaration and name to declaraiont
instr(Code, {_Ds, Dmap}) ->
    instr(Code, #{}, Dmap, []).

instr([{instr,_Addr,'SEGMENT',_Params}|Is], Reg, Dmap, Acc) ->
    %% drop segments, we already have all strings
    instr(Is, Reg, Dmap, Acc);

instr([{instr, _Addr, 'LD',  [Rx, {mem,I}]} | Is], Reg, Dmap, Acc) ->
    Name = element(4, maps:get(I, Dmap)),
    Reg1 = maps:put(Rx, {'WORD', 0, Name}, Reg),
    instr(Is, Reg1, Dmap, Acc);
instr([{instr, _Addr, 'LI',  [Rx, {imm,V}]} | Is], Reg, Dmap, Acc) ->
    Reg1 = maps:put(Rx, {'INT', 0, V}, Reg),
    instr(Is, Reg1, Dmap, Acc);
instr([I={instr, _Addr, _Op, _Params}|Is], Reg, Dmap, Acc) ->
    instr(Is, Reg, Dmap, [I|Acc]);
instr([], _Reg, _Dmap, Acc) ->
    {ok, lists:reverse(Acc)}.
