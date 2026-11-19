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

-export([dumps/1, program/1, state/2, line_crc/2, merge/2, info/1]).

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
%% three varints rather than one, takes the declarations to say. A reply
%% not understood is {unknown, Head}.
dumps(Text) ->
    Lines = [string:trim(L) || L <- string:split(Text, "\n", all)],
    dumps(Lines, []).

dumps(["dump " ++ Head | T], Acc) ->
    {Ls, T1} = lists:splitwith(fun(L) -> L =/= "." end, T),
    D = try dump(string:lexemes(Head, " "), Ls)
	catch error:_ -> {unknown, Head}
	end,
    dumps(tl_or_nil(T1), [D || D =/= none] ++ Acc);
dumps([_ | T], Acc) -> dumps(T, Acc);
dumps([], Acc) -> lists:reverse(Acc).

%% A reply by its head. A node from before 2026-11-08 says `dump rom 1300'
%% and sends bare hex lines, no offsets and no CRC: taken whole when the
%% size is right, as it cannot be asked for again. A head not known is
%% {unknown, Head}, for the one asking to say.
dump(["state", N, Cycle, Ms], Ls) ->
    case whole(#{total => none, have => lists:append([good(L) || L <- Ls])}) of
	{ok, Bin} -> {state, list_to_integer(Cycle), list_to_integer(Ms),
		      list_to_integer(N), Bin};
	_ -> none
    end;
dump([Kind, Total, From, Len], Ls) ->
    K = kind(Kind),
    P = #{total => list_to_integer(Total),
	  have => lists:append([good(L) || L <- Ls]), want => []},
    merge(#{total => list_to_integer(Total), have => [],
	    want => [{list_to_integer(From), list_to_integer(Len)}]},
	  {part, K, P});
dump([Kind, Total], Ls) ->
    K = kind(Kind),
    Bin = << <<(list_to_integer(H, 16))>> || L <- Ls, H <- pairs(L) >>,
    case byte_size(Bin) =:= list_to_integer(Total) of
	true -> {K, Bin};
	false -> {unknown, Kind ++ " " ++ Total ++ " (old form, came short)"}
    end.

kind("rom") -> rom;
kind("eeprom") -> eeprom;
kind("patch") -> patch;
kind("info") -> info.

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

%% What /dump info says of a node, from its bytes (src/csp_repl.c info_emit):
%% records of a tag, a length and varints and strings. A tag not known here
%% is passed over by its length -- a newer node is still read.
%%
%%   #{fw, board, rom, float => fixpoint | ieee, endian => little | big,
%%     features, device, ram => #{total, free}, limits => #{instr, decl,
%%     string, names, funcs}, use => #{instr, decl, string (in RAM, what the
%%     limits bound), names, rom_decl, rom_instr, rom_string},
%%     flash => #{base, sectors => [Size]}, regions => [#{name, kind, first,
%%     last, slot, image}], images => [#{index, role, gen, size, booted}],
%%     eeprom => #{capacity, stored}, transports => #{can => Units, ...}}
%%
%% Units is a bit per unit the node really drives (bit b for i2c bus b, uart
%% unit b; bit 0 for a transport without units), 0 the null device.
info(Bin) -> info(Bin, #{regions => [], images => []}).

info(<<Tag, Len, Rec:Len/binary, T/binary>>, I) -> info(T, record(Tag, Rec, I));
info(<<>>, I = #{regions := Rs, images := Is}) ->
    I#{regions := lists:reverse(Rs), images := lists:reverse(Is)}.

record(1, R, I) -> {Fw, _} = vstr(R), I#{fw => Fw};
record(2, R, I) ->
    [Rom, Flags, Feat] = vints(R, 3),
    I#{rom => Rom,
       %% the node's own floats: an image's are IEEE everywhere (format 23)
       float => case Feat band 8 of 0 -> fixpoint; 8 -> ieee end,
       endian => case Flags band 2 of 0 -> little; 2 -> big end,
       features => [F || {B, F} <- [{1, compiler}, {2, flash}, {4, eeprom},
				     {8, ieee}],
			 Feat band B =/= 0]};
record(3, R, I) -> {D, _} = vstr(R), I#{device => D};
record(4, R, I) -> [Tot, Free] = vints(R, 2), I#{ram => #{total => Tot, free => Free}};
record(5, R, I) ->
    I#{limits => maps:from_list(lists:zip([instr, decl, string, names, funcs],
					  vints(R, 5)))};
record(6, R, I) ->
    I#{use => maps:from_list(lists:zip([instr, decl, string, names, rom_decl,
					rom_instr, rom_string], vints(R, 7)))};
record(7, R, I) ->
    {Base, T} = varint(R),
    I#{flash => #{base => Base, sectors => runs(T)}};
record(8, R, I = #{regions := Rs}) ->
    {[Kind, First, Last, Slot], T} = vints_t(R, 4),
    {Img, T1} = case Slot of
		    1 -> {[Role, Gen, Size], Tr} = vints_t(T, 3),
			 {#{role => role(Role), gen => Gen, size => Size}, Tr};
		    _ -> {none, T}
		end,
    {Name, _} = vstr(T1),
    Reg = #{name => Name, first => First, last => Last, image => Img,
	    kind => case Kind of 0 -> runtime; 1 -> app; 2 -> patch; 3 -> store;
			 K -> K end,
	    slot => case Slot of 0 -> erased; 1 -> image; 2 -> foreign;
			 3 -> bad_crc; 4 -> unread; 5 -> none end},
    I#{regions := [Reg | Rs]};
record(9, R, I = #{images := Is}) ->
    [Ix, Role, Gen, Size, Booted] = vints(R, 5),
    I#{images := [#{index => Ix, role => role(Role), gen => Gen, size => Size,
		    booted => Booted =:= 1} | Is]};
record(10, R, I) ->
    [Cap, Stored] = vints(R, 2),
    I#{eeprom => #{capacity => case Cap of 0 -> none; 16#ffffffff -> unbounded;
				   C -> C end,
		   stored => Stored}};
record(11, R, I) ->
    I#{transports => maps:from_list(have(R))};
record(12, R, I) -> {B, _} = vstr(R), I#{board => B};
record(_, _, I) -> I.

have(<<>>) -> [];
have(B) ->
    {[Tr, Units], T} = vints_t(B, 2),
    [{csp_disasm:transport(Tr), Units} | have(T)].

role(1) -> failsafe;
role(_) -> rom.

vints(B, N) -> element(1, vints_t(B, N)).

vints_t(B, 0) -> {[], B};
vints_t(B, N) ->
    {V, T} = varint(B),
    {Vs, T1} = vints_t(T, N - 1),
    {[V | Vs], T1}.

vstr(B) ->
    {L, T} = varint(B),
    <<S:L/binary, T1/binary>> = T,
    {binary_to_list(S), T1}.

runs(<<>>) -> [];
runs(B) ->
    {[Count, Size], T} = vints_t(B, 2),
    lists:duplicate(Count, Size) ++ runs(T).

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
%% the head whose elements reach it, P[3]. The head is P[0], as csp_list
%% writes it: an array's elements are indexed, every one.
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
	D = {decl, _, _, _, Opts} when is_list(Opts) ->
	    case proplists:get_value(len, Opts, 1) of
		1 -> decl_name(D, Dj);
		_ -> decl_name(D, Dj) ++ "[0]"
	    end;
	D -> decl_name(D, Dj)
    end.

value({decl, _, _, _, Opts}, V) when is_list(Opts) ->
    case proplists:get_value(type, Opts) of
	unsigned -> V band 16#FFFFFFFF;
	_ -> V
    end;
value(_, V) -> V.
