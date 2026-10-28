%%% @doc
%%%    What a node says about itself, read on the host: the /dump replies.
%%%
%%%        dump rom 428              dump state 21 1 1234
%%%        4A414D0A...               00002A0607...
%%%        .                         .
%%%
%%%    The node only sends bytes. The image and the store are decoded by
%%%    csp_disasm, which also lays a patch over its ROM; a state is (key,
%%%    value) pairs, read here against those declarations.
%%% @end
-module(csp_node).

-export([dumps/1, program/1, state/2]).

%% Every reply in a stretch of console text, in order, whatever else is there:
%% {rom, Bin} | {eeprom, Bin} | {patch, Bin} | {state, Cycle, Ms, N, Bin}.
%% A state's pairs are read by state/2: which of them is a timer, and so three
%% varints rather than one, takes the declarations to say.
dumps(Text) ->
    Lines = [string:trim(L) || L <- string:split(Text, "\n", all)],
    dumps(Lines, []).

dumps(["dump " ++ Head | T], Acc) ->
    {Hex, T1} = lists:splitwith(fun(L) -> L =/= "." end, T),
    Bin = << <<(list_to_integer(H, 16))>> || L <- Hex, H <- pairs(L) >>,
    D = case string:lexemes(Head, " ") of
	    ["rom", _]    -> {rom, Bin};
	    ["eeprom", _] -> {eeprom, Bin};
	    ["patch", _]  -> {patch, Bin};
	    ["state", N, Cycle, Ms] ->
		{state, list_to_integer(Cycle), list_to_integer(Ms),
		 list_to_integer(N), Bin}
	end,
    dumps(tl_or_nil(T1), [D | Acc]);
dumps([_ | T], Acc) -> dumps(T, Acc);
dumps([], Acc) -> lists:reverse(Acc).

tl_or_nil([]) -> [];
tl_or_nil([_ | T]) -> T.

pairs([A, B | T]) -> [[A, B] | pairs(T)];
pairs(_) -> [].

%% [{Decl, Obj, Value}], Obj 0 for a global, else the object's declaration.
%% A timer's Value is {Flags, Period, Elapsed}.
pairs_of(<<>>, _ByIx) -> [];
pairs_of(Bin, ByIx) ->
    {Key, B1} = varint(Bin),
    {Obj, B2} = case Key band 1 of
		    1 -> varint(B1);
		    0 -> {0, B1}
		end,
    Dj = Key bsr 1,
    case maps:get(Dj, ByIx, none) of
	{decl, _, timer, _, _} ->
	    {F, B3} = varint(B2),
	    {P, B4} = varint(B3),
	    {E, B5} = varint(B4),
	    [{Dj, Obj, {F, P, E}} | pairs_of(B5, ByIx)];
	_ ->
	    {Z, B3} = varint(B2),
	    [{Dj, Obj, unzig(Z)} | pairs_of(B3, ByIx)]
    end.

varint(<<1:1, X:7, T/binary>>) ->
    {V, T1} = varint(T),
    {X bor (V bsl 7), T1};
varint(<<0:1, X:7, T/binary>>) -> {X, T}.

%% The signed reading the node took: -1 came as 1.
unzig(Z) -> (Z bsr 1) bxor -(Z band 1).

%% The program a node runs, from its dumps: the image, and the patch on top of
%% it when there is one -- the live one (/dump patch) or the one stored. Both
%% are in the store's format.
program(Ds) ->
    case {[B || {rom, B} <- Ds], [B || {K, B} <- Ds, K =:= patch orelse
					      K =:= eeprom]} of
	{[Rom | _], []}      -> csp_disasm:image(Rom);
	{[Rom | _], [Ee | _]} -> csp_disasm:patch(Rom, Ee);
	_ -> {error, no_rom}
    end.

%% A state's pairs by name, `s1.X' for a member. An unsigned declaration
%% reads its 32 bits unsigned. A timer T is T (its value: 1 started, 0
%% stopped), T.fired, T.running, T.period and T.elapsed.
state(Decls, {state, _Cycle, _Ms, N, Bin}) ->
    ByIx = maps:from_list([{element(2, D), D} || D <- Decls]),
    Ps = pairs_of(Bin, ByIx),
    length(Ps) =:= N orelse error({short_state, N, length(Ps)}),
    lists:append([named(name(Dj, Obj, ByIx), maps:get(Dj, ByIx, none), V)
		  || {Dj, Obj, V} <- Ps]).

named(T, _D, {F, P, E}) ->
    [{T, F band 1}, {T ++ ".fired", (F bsr 1) band 1},
     {T ++ ".running", (F bsr 2) band 1}, {T ++ ".period", P},
     {T ++ ".elapsed", E}];
named(Name, D, V) ->
    [{Name, value(D, V)}].

name(Dj, 0, ByIx) -> decl_name(maps:get(Dj, ByIx, none), Dj);
name(Dj, Obj, ByIx) ->
    Inst = case maps:get(Obj, ByIx, none) of
	       {decl, _, object, _Mod, I, _} -> binary_to_list(I);
	       _ -> "$" ++ integer_to_list(Obj)
	   end,
    Inst ++ "." ++ decl_name(maps:get(Dj, ByIx, none), Dj).

decl_name({decl, _, _, Name, _}, _) -> binary_to_list(Name);
decl_name(_, Dj) -> "$" ++ integer_to_list(Dj).

value({decl, _, _, _, Opts}, V) when is_list(Opts) ->
    case proplists:get_value(type, Opts) of
	unsigned -> V band 16#FFFFFFFF;
	_ -> V
    end;
value(_, V) -> V.
