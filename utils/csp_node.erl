%%% @doc
%%%    What a node says about itself, read on the host: the /dump replies.
%%%
%%%        dump rom 428 0 428           dump state 21 1 1234
%%%        0 4a414d0a... 3F1C           0 00002a0607... 81A2
%%%        32 ... 77B0                  .
%%%        .
%%%
%%%    A head says what, its whole size, the offset and the bytes sent; a line
%%%    its offset, up to 32 bytes hex and a CRC-16 over the offset (4 bytes,
%%%    little endian) and the bytes. The one asking sets the pace: `/dump rom
%%%    [slot] [offs [len]]' reads a piece, and a line that came back wrong is
%%%    asked for again by its offset.
%%%
%%%    The node only sends bytes. The image and the store are decoded by
%%%    csp_disasm, which also lays a patch over its ROM; a state is (key,
%%%    value) pairs, read here against those declarations.
%%% @end
-module(csp_node).

-export([dumps/1, program/1, state/2, line_crc/2, merge/2]).

%% Every reply in a stretch of console text, in order, whatever else is there:
%% {rom, Bin} | {eeprom, Bin} | {patch, Bin} | {state, Cycle, Ms, N, Bin} when
%% the whole of it came and every line is right, else
%%
%%     {part, Kind, #{total, have => [{Offs, Bin}], want => [{Offs, Len}]}}
%%
%% what came right and what to ask for again -- a window asked for, or a line
%% whose CRC was wrong, or that never came; merge/2 puts pieces together. A
%% state is a moment and is not asked for again: one that came wrong is left
%% out. A state's pairs are read by state/2: which of them is a timer, and so
%% three varints rather than one, takes the declarations to say.
dumps(Text) ->
    Lines = [string:trim(L) || L <- string:split(Text, "\n", all)],
    dumps(Lines, []).

dumps(["dump " ++ Head | T], Acc) ->
    {Ls, T1} = lists:splitwith(fun(L) -> L =/= "." end, T),
    Good = lists:append([good(L) || L <- Ls]),
    D = case string:lexemes(Head, " ") of
	    ["state", N, Cycle, Ms] ->
		case whole(#{total => none, have => Good}) of
		    {ok, Bin} -> {state, list_to_integer(Cycle),
				  list_to_integer(Ms), list_to_integer(N), Bin};
		    _ -> none
		end;
	    [Kind, Total, From, Len] ->
		K = list_to_atom(Kind),
		F = list_to_integer(From),
		P = #{total => list_to_integer(Total), have => Good,
		      want => []},
		merge(#{total => list_to_integer(Total), have => [],
			want => [{F, list_to_integer(Len)}]}, {part, K, P})
	end,
    dumps(tl_or_nil(T1), [D || D =/= none] ++ Acc);
dumps([_ | T], Acc) -> dumps(T, Acc);
dumps([], Acc) -> lists:reverse(Acc).

%% A line, if it is one and its CRC says it came as sent: [{Offs, Bin}].
good(L) ->
    try string:lexemes(L, " ") of
	[O, Hex, C] ->
	    Offs = list_to_integer(O),
	    Bin = << <<(list_to_integer(H, 16))>> || H <- pairs(Hex) >>,
	    case line_crc(Offs, Bin) =:= list_to_integer(C, 16) of
		true -> [{Offs, Bin}];
		false -> []
	    end;
	_ -> []
    catch error:badarg -> []
    end.

%% The CRC a line carries: CRC-16/CCITT from 0xFFFF over the offset, 4 bytes
%% little endian, and the bytes.
line_crc(Offs, Bin) ->
    csp_disasm:crc16(<<Offs:32/little, Bin/binary>>).

%% What a dump came with, put together with what is known of it: Part is
%% #{total, have, want} -- want the windows still to be read -- and the new
%% reply a {part, ...} (or a whole one). The windows it was asked for are
%% taken off want, and what of them did not come right goes back on. Back:
%% the whole, {Kind, Bin}, or {part, Kind, Part} with what to ask for next.
merge(Part = #{total := Total, have := Have0, want := Want0},
      {part, Kind, #{have := New}}) ->
    Have = lists:ukeysort(1, New ++ Have0),
    Missing = gaps(Want0, Have),
    case {Missing, whole(#{total => Total, have => Have})} of
	{[], {ok, Bin}} -> {Kind, Bin};
	_ -> {part, Kind, Part#{have := Have, want := Missing}}
    end.

%% The parts of the windows Want that Have does not cover.
gaps(Want, Have) ->
    lists:append([gap(F, F + L, Have) || {F, L} <- Want, L > 0]).

gap(F, E, _Have) when F >= E -> [];
gap(F, E, Have) ->
    case [{O, B} || {O, B} <- Have, O =< F, O + byte_size(B) > F] of
	[{O, B} | _] -> gap(O + byte_size(B), E, Have);
	[] ->
	    Next = lists:min([E | [O || {O, _} <- Have, O > F, O < E]]),
	    [{F, Next - F} | gap(Next, E, Have)]
    end.

%% The bytes from 0 to Total, if Have covers them: a state's total is what
%% came, from 0 without a hole.
whole(#{total := none, have := Have}) ->
    whole(#{total => lists:sum([byte_size(B) || {_, B} <- Have]),
	    have => Have});
whole(#{total := Total, have := Have}) ->
    case gap(0, Total, Have) of
	[] -> {ok, binary:part(iolist_to_binary(
				 join(lists:ukeysort(1, Have), 0)), 0, Total)};
	_ -> missing
    end.

%% The pieces in order from 0, overlaps taken once.
join([], _At) -> [];
join([{O, B} | T], At) when O + byte_size(B) =< At -> join(T, At);
join([{O, B} | T], At) ->
    Skip = At - O,
    [binary:part(B, Skip, byte_size(B) - Skip) | join(T, O + byte_size(B))].

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

name(Dj, 0, ByIx) -> elem_name(Dj, ByIx);
name(Dj, Obj, ByIx) ->
    Inst = case maps:get(Obj, ByIx, none) of
	       {decl, _, object, _Mod, I, _} -> binary_to_list(I);
	       _ -> "$" ++ integer_to_list(Obj)
	   end,
    Inst ++ "." ++ elem_name(Dj, ByIx).

decl_name({decl, _, _, Name, _}, _) -> binary_to_list(Name);
decl_name(_, Dj) -> "$" ++ integer_to_list(Dj).

%% An array element after the first is no declaration of its own here --
%% csp_disasm folds an array into its head, {len, N} -- so it is named after
%% the head whose elements reach it, P[3], as csp's own dump calls it.
elem_name(Dj, ByIx) ->
    case maps:get(Dj, ByIx, none) of
	none ->
	    case [{H, D} || {H, D = {decl, _, _, _, Opts}} <- maps:to_list(ByIx),
			    is_list(Opts), H < Dj,
			    Dj < H + proplists:get_value(len, Opts, 1)] of
		[{H, D} | _] ->
		    decl_name(D, H) ++ "[" ++ integer_to_list(Dj - H) ++ "]";
		[] -> decl_name(none, Dj)
	    end;
	D -> decl_name(D, Dj)
    end.

value({decl, _, _, _, Opts}, V) when is_list(Opts) ->
    case proplists:get_value(type, Opts) of
	unsigned -> V band 16#FFFFFFFF;
	_ -> V
    end;
value(_, V) -> V.
