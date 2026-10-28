%%% @doc
%%%    An image listed as CandySpeak source: what /list prints, off the
%%%    bytes. The ROM a node runs (/dump rom), with its EEPROM patch on top
%%%    (/dump eeprom), comes back as the program -- so a host can show a
%%%    node's code without the source file it was built from.
%%%
%%%    The C side is src/csp_repl.c (cmd_list, list_rules) and src/csp_expr.c
%%%    (the rule renderer); this follows them line for line, and the test in
%%%    tests/list/ holds the two to the same text.
%%% @end
-module(csp_list).

-export([file/1, image/1, patch/2, program/1, program/2]).

%% A file csp_disasm reads (.img, .hex).
file(Filename) ->
    case quiet(fun() -> csp_disasm:file(Filename) end) of
	{ok, P}  -> {ok, program(P)};
	Error -> Error
    end.

%% The bytes of /dump rom.
image(Bin) ->
    case quiet(fun() -> csp_disasm:image(Bin) end) of
	{ok, P}  -> {ok, program(P)};
	Error -> Error
    end.

%% The bytes of /dump rom and /dump eeprom: what the node runs. The patch's
%% lines are tagged E, the image's F.
patch(Rom, Eeprom) ->
    case quiet(fun() -> csp_disasm:patch(Rom, Eeprom) end) of
	{ok, P = {H, _, Decls, _}} ->
	    {ok, program(P, #{rom_nd => rom_nd(H, Decls),
			      rom_nn => maps:get(n_instr, H)})};
	Error -> Error
    end.

%% Where the patch's declarations start: the image's trailing `end' is dropped
%% on load and the patch's first declaration takes its index.
rom_nd(H, Decls) ->
    N = maps:get(n_decl, H),
    case lists:keyfind(N - 1, 2, Decls) of
	{decl, _, 'end'} -> N;
	_ -> N - 1
    end.

program(P) ->
    program(P, #{}).

%% The listing, as one string.
program({Header, Strings, Decls, Code}, Opts) ->
    Env = env(Header, Strings, Decls, Code, Opts),
    lists:flatten([[Text, eol(Tag), $\n] || {Text, Tag} <- listing(Env)]).

%% csp_disasm prints as it goes; a library caller does not want that.
quiet(Fun) ->
    Self = self(),
    Null = spawn(fun null_io/0),
    Old = group_leader(),
    group_leader(Null, Self),
    try Fun()
    after
	group_leader(Old, Self),
	exit(Null, kill)
    end.

null_io() ->
    receive
	{io_request, From, Ref, _} -> From ! {io_reply, Ref, ok}, null_io();
	_ -> null_io()
    end.

%% The trailing tag comment: rule number and segment, so the line pastes
%% back as source. See list_eol.
eol(none)          -> "";
eol({0, Seg})      -> ["  // ", Seg];
eol({No, Seg})     -> ["  // ", integer_to_list(No), " ", Seg].

%% ------------------------------------------------------------- environment

env(Header, Strings, Decls, Code, Opts) ->
    D = decls(Decls),
    C = code(Code),
    Nd = case maps:size(D) of 0 -> 0; _ -> lists:max(maps:keys(D)) + 1 end,
    Nn = lists:foldl(fun({instr, A, 'SEGMENT', Ps}, M) ->
			     max(M, A + 1 + proplists:get_value(slots, Ps));
			({instr, A, _, _}, M) -> max(M, A + 1);
			(_, M) -> M
		     end, 0, Code),
    #{decl => D, code => C, strings => Strings, nd => Nd, nn => Nn,
      states => states(Decls),
      objects => maps:from_list([{proplists:get_value(m, Ps), Name}
				 || {decl, _, object, _, Name, Ps} <- Decls]),
      float => maps:get(float, Header, fixpoint),
      rom_nd => maps:get(rom_nd, Opts, maps:get(n_decl, Header)),
      rom_nn => maps:get(rom_nn, Opts, maps:get(n_instr, Header))}.

%% One entry per declaration INDEX. csp_disasm folds an array into its head;
%% the code addresses elements, so they are unfolded again here, each with
%% the head it belongs to.
decls(Decls) ->
    lists:foldl(fun decl_entries/2, #{}, Decls).

decl_entries({decl, I, 'end'}, M) ->
    M#{I => #{type => 'end', name => <<>>, local => 0, head => I, elem => 0,
	      props => []}};
decl_entries({decl, I, object, Mod, Name, Ps}, M) ->
    M#{I => #{type => object, name => iolist_to_binary(Name), local => 0,
	      head => I, elem => 0, module => iolist_to_binary(Mod),
	      props => Ps}};
decl_entries({decl, I, Type0, Name0, Ps}, M) ->
    Name = iolist_to_binary(Name0),
    {Type, Local} = case Type0 of
			local -> {variable, 1};
			param -> {constant, 1};
			_ -> {Type0, 0}
		    end,
    Len = proplists:get_value(len, Ps, 1),
    Common = [P || P <- Ps, element(1, P) =/= elems],
    Elems = proplists:get_value(elems, Ps, lists:duplicate(Len, [])),
    M1 = lists:foldl(
	   fun({K, Own}, Mi) ->
		   Mi#{I + K => #{type => Type, local => Local,
				  name => case K of 0 -> Name; _ -> <<>> end,
				  head => I, elem => K, props => Own ++ Common}}
	   end, M, lists:zip(lists:seq(0, Len - 1), Elems)),
    %% A timer's second slot is its start time: nameless, and never listed.
    case proplists:get_value(slots, Ps) of
	2 when Type =:= timer ->
	    M1#{I + 1 => #{type => variable, local => 0, name => <<>>,
			   head => I + 1, elem => 0, props => []}};
	_ -> M1
    end.

%% Instructions by address. A SEGMENT's payload slots hold text, not code.
code(Code) ->
    maps:from_list([{element(2, I), I} || I <- Code, is_tuple(I),
					   tuple_size(I) =:= 4]).

%% Every state, numbered densely across the #states blocks.
states(Decls) ->
    lists:append([[N || N <- [Name | [proplists:get_value(K, Ps)
				      || K <- [name2, name3, name4, name5,
					       name6]]],
			N =/= [], N =/= <<>>, N =/= ""]
		  || {decl, _, states, Name, Ps} <- Decls]).

d(Env, I) -> maps:get(I band 16#7fff, maps:get(decl, Env), undefined).

dtype(Env, I) ->
    case d(Env, I) of
	undefined -> none;
	#{type := T} -> T
    end.

dprop(Env, I, K) -> dprop(Env, I, K, undefined).
dprop(Env, I, K, Def) ->
    case d(Env, I) of
	undefined -> Def;
	#{props := Ps} -> proplists:get_value(K, Ps, Def)
    end.

name(Env, I) ->
    case d(Env, I) of
	undefined -> "";
	#{name := N} -> str(N)
    end.

str(B) when is_binary(B) -> binary_to_list(B);
str(L) when is_list(L) -> L.

string_at(Env, Pos) -> str(maps:get(Pos, maps:get(strings, Env), "")).

is_local(Env, I) ->
    dtype(Env, I) =:= variable andalso maps:get(local, d(Env, I)) =:= 1.

%% $N: locals counted from the start of the enclosing scope.
local_number(Env, Ix) ->
    I = Ix band 16#7fff,
    Start = scope_start(Env, I),
    1 + length([K || K <- lists:seq(Start, I - 1), is_local(Env, K)]).

scope_start(_Env, 0) -> 0;
scope_start(Env, K) ->
    case dtype(Env, K - 1) of
	T when T =:= module; T =:= 'end' -> K;
	_ -> scope_start(Env, K - 1)
    end.

state_var(Env, I) ->
    dtype(Env, I) =:= variable andalso name(Env, I) =:= "State".

state_name(Env, N) ->
    States = maps:get(states, Env),
    case N >= 0 andalso N < length(States) of
	true -> str(lists:nth(N + 1, States));
	false -> ""
    end.

state_number(Env, Name) ->
    index_of(Name, maps:get(states, Env), 0).

index_of(_, [], _) -> -1;
index_of(X, [X | _], N) -> N;
index_of(X, [_ | T], N) -> index_of(X, T, N + 1).

instr(Env, I) -> maps:get(I, maps:get(code, Env), undefined).

iop(Env, I) ->
    case instr(Env, I) of
	{instr, _, Op, _} -> Op;
	_ -> data
    end.

args(Env, I) -> element(4, instr(Env, I)).

dseg(Env, I) ->
    case I < maps:get(rom_nd, Env) of true -> "F"; false -> "E" end.

iseg(Env, I) ->
    case I < maps:get(rom_nn, Env) of true -> "F"; false -> "E" end.

%% ------------------------------------------------------------ declarations

listing(Env) ->
    decl_lines(Env, 0, #{mod => none, skip => 0}) ++
	element(1, rules(Env, 0, maps:get(nn, Env), 0, true)).

decl_lines(Env, I, S) ->
    case I >= maps:get(nd, Env) of
	true -> [];
	false ->
	    {Ls, S1} = decl_line(Env, I, d(Env, I), S),
	    Ls ++ decl_lines(Env, I + 1, S1)
    end.

%% The built-in Sys namespace and its instance are runtime machinery: the
%% members and the #end are skipped with the module.
decl_line(_Env, _I, _D, S = #{skip := N}) when N > 0 ->
    {[], S#{skip := N - 1}};
decl_line(_Env, _I, #{type := module, name := <<"Sys">>, props := Ps}, S) ->
    {[], S#{skip := proplists:get_value(n, Ps) + 1}};
decl_line(_Env, _I, #{type := object, module := <<"Sys">>}, S) ->
    {[], S};
decl_line(Env, I, #{type := module, name := Name}, S) ->
    {[{["#module ", str(Name)], {0, dseg(Env, I)}}], S#{mod := I}};
decl_line(Env, I, #{type := 'end'}, S = #{mod := M}) when M =/= none ->
    Ent = dprop(Env, M, ent),
    N = case iop(Env, Ent) of
	    'ENTER' -> proplists:get_value(n, args(Env, Ent));
	    _ -> 0
	end,
    {Rs, _} = rules(Env, Ent + 1, Ent + 1 + N, 1, false),
    {Rs ++ [{"#end ", {0, dseg(Env, I)}}], S#{mod := none}};
decl_line(_Env, _I, #{type := 'end'}, S) ->
    {[], S};
decl_line(Env, I, D = #{type := Type, name := Name}, S) ->
    Skip = state_var(Env, I) orelse maps:get(elem, D) > 0 orelse
	(Type =/= route andalso not is_local(Env, I) andalso Name =:= <<>>),
    case Skip of
	true -> {[], S};
	false ->
	    Pre = case maps:get(mod, S) of none -> ""; _ -> "  " end,
	    case decl_text(Env, I, D) of
		none -> {[], S};
		Text -> {[{[Pre, Text], {0, dseg(Env, I)}}], S}
	    end
    end.

decl_text(Env, _I, #{type := states, name := Name, props := Ps}) ->
    Names = [str(N) || N <- [Name | [proplists:get_value(K, Ps)
				   || K <- [name2, name3, name4, name5,
					    name6]]],
		       N =/= [], N =/= <<>>],
    case [N || N <- Names, state_number(Env, list_to_binary(N)) > 2] of
	[] -> none;
	Shown -> ["#states ", lists:join(" ", Shown)]
    end;
decl_text(Env, I, #{type := variable, name := Name, props := Ps}) ->
    Local = is_local(Env, I),
    Dir = proplists:get_value(dir, Ps),
    Head = case Local of
	       true when Name =/= <<>> -> ["#local ", str(Name)];
	       true -> ["#local $", integer_to_list(local_number(Env, I))];
	       false -> ["#variable ", str(Name)]
	   end,
    Type = proplists:get_value(type, Ps),
    Tail = case {Local, proplists:get_value(init, Ps)} of
	       {true, Init} when Name =/= <<>>, (Dir =:= in orelse
						 Dir =:= inout) ->
		   [" in" | case Dir of
			       inout -> [" = ", value(Env, Type, Init)];
			       in -> []
			   end];
	       {true, _} when Name =/= <<>>, Dir =:= out ->
		   [" out = ", formula(Env, I)];
	       {true, _} -> [];
	       {false, {bind, Ca}} ->
		   [case proplists:get_value(endian, Ca) of
			native -> "";
			E -> [" ", atom_to_list(E)]
		    end,
		    " bind ", window(Env, Ca)];
	       {false, Init} ->
		   [case Dir of
			none -> "";
			_ -> [" ", atom_to_list(Dir)]
		    end, " = ", value(Env, Type, Init)]
	   end,
    [Head, alen(Env, I), ":", integer_to_list(proplists:get_value(size, Ps)),
     " ", atom_to_list(Type), Tail];
decl_text(Env, I, #{type := constant, name := Name, props := Ps}) ->
    Kw = case maps:get(local, d(Env, I)) of 1 -> "#param "; 0 -> "#constant " end,
    Type = proplists:get_value(type, Ps),
    Len = dprop(Env, I, len, 1),
    Val = case Len of
	      1 -> value(Env, Type, proplists:get_value(init, Ps));
	      _ -> ["{ ", lists:join(", ", [value(Env, Type, dprop(Env, I + E, init))
					    || E <- lists:seq(0, Len - 1)]),
		    " }"]
	  end,
    [Kw, str(Name), alen(Env, I), ":",
     integer_to_list(proplists:get_value(size, Ps)), " ", atom_to_list(Type),
     " = ", Val];
decl_text(Env, _I, #{type := object, name := Name, module := Mod, props := Ps}) ->
    ["#", str(Mod), " ", str(Name), bindings(Env, proplists:get_value(m, Ps))];
decl_text(_Env, _I, #{type := timer, name := Name, props := Ps}) ->
    ["#timer ", str(Name), " ", integer_to_list(proplists:get_value(period, Ps)),
     case proplists:get_value(init, Ps) of 0 -> ""; _ -> " = 1" end];
decl_text(Env, I, #{type := digital, name := Name, props := Ps}) ->
    ["#digital ", str(Name), alen(Env, I), " ",
     atom_to_list(proplists:get_value(dir, Ps)),
     case {proplists:get_value(pullup, Ps), proplists:get_value(pulldown, Ps)} of
	 {1, _} -> " pullup";
	 {_, 1} -> " pulldown";
	 _ -> ""
     end,
     trigger(Ps), " ", pin_spec(Env, I)];
decl_text(Env, I, #{type := analog, name := Name, props := Ps}) ->
    ["#analog ", str(Name), alen(Env, I), ":",
     integer_to_list(proplists:get_value(size, Ps)), " ",
     atom_to_list(proplists:get_value(dir, Ps)),
     case proplists:get_value(type, Ps) of
	 unsigned -> "";
	 T -> [" ", atom_to_list(T)]
     end,
     case proplists:get_value(pwm, Ps) of 1 -> " pwm"; _ -> "" end,
     trigger(Ps), " ", pin_spec(Env, I)];
decl_text(Env, _I, #{type := buffer, name := Name, props := Ps}) ->
    ["#buffer ", str(Name), ":", integer_to_list(proplists:get_value(nbytes, Ps)),
     case proplists:get_value(dir, Ps) of
	 none -> "";
	 Dir -> [" ", atom_to_list(Dir)]
     end,
     transport(Env, proplists:get_value(transport, Ps), proplists:get_value(id, Ps))];
decl_text(Env, _I, #{type := route, props := Ps}) ->
    ["#route ", name(Env, proplists:get_value(src, Ps)), " ",
     name(Env, proplists:get_value(dst, Ps))];
decl_text(Env, _I, #{type := field, name := Name, props := Ps}) ->
    ["#field ", str(Name), ":", integer_to_list(proplists:get_value(len, Ps)),
     case proplists:get_value(dir, Ps) of
	 none -> "";
	 Dir -> [" ", atom_to_list(Dir)]
     end,
     " ", atom_to_list(proplists:get_value(type, Ps)),
     case proplists:get_value(endian, Ps) of
	 native -> "";
	 E -> [" ", atom_to_list(E)]
     end,
     " ", window(Env, Ps)];
decl_text(_Env, _I, _D) ->
    "".

%% `Buf[lo..hi]', hi inclusive.
window(Env, Ca) ->
    Bit = proplists:get_value(bit, Ca),
    [name(Env, proplists:get_value(id, Ca)), "[", integer_to_list(Bit), "..",
     integer_to_list(Bit + proplists:get_value(len, Ca) - 1), "]"].

alen(Env, I) ->
    case dprop(Env, I, len, 1) of
	1 -> "";
	N -> ["[", integer_to_list(N), "]"]
    end.

trigger(Ps) ->
    case proplists:get_value(irq, Ps, 0) of
	0 -> "";
	Irq ->
	    [case proplists:get_value(soft, Ps) of 1 -> " soft"; _ -> "" end,
	     " ", element(Irq, {"rising", "falling", "both", "high", "low",
				"ready", "none"})]
    end.

%% `9:0..2,1:5' -- a run of pins on one port collapses to a range.
pin_spec(Env, I) ->
    Len = dprop(Env, I, len, 1),
    Pins = [{dprop(Env, I + K, port), dprop(Env, I + K, pin)}
	    || K <- lists:seq(0, Len - 1)],
    [{Port0, Pin0} | Rest] = Pins,
    pin_runs(Rest, Port0, Pin0, Pin0,
	     [integer_to_list(Port0), ":", integer_to_list(Pin0)]).

pin_runs([{Q, P} | Rest], Port, Start, Last, Acc) when Q =:= Port, P =:= Last + 1 ->
    pin_runs(Rest, Port, Start, P, Acc);
pin_runs([{Q, P} | Rest], Port, Start, Last, Acc) ->
    Acc1 = [Acc, range(Start, Last), ",",
	    case Q =:= Port of
		true -> "";
		false -> [integer_to_list(Q), ":"]
	    end, integer_to_list(P)],
    pin_runs(Rest, Q, P, P, Acc1);
pin_runs([], _Port, Start, Last, Acc) ->
    [Acc, range(Start, Last)].

range(Start, Start) -> "";
range(_Start, Last) -> ["..", integer_to_list(Last)].

%% The endpoint lives in a constant (two, for UDP and TCP).
transport(Env, Tr, Id) ->
    Ep = case dprop(Env, Id, init, 0) of
	     V when is_integer(V) -> V band 16#ffffffff;
	     _ -> 0
	 end,
    case Tr of
	can -> [" can ", hex(Ep)];
	i2c -> [" i2c ", integer_to_list((Ep bsr 16) band 16#ff), " ",
		hex((Ep bsr 8) band 16#ff), " ", hex(Ep band 16#ff)];
	spi -> [" spi ", integer_to_list((Ep bsr 24) band 16#f), " ",
		integer_to_list((Ep bsr 20) band 16#f), ":",
		integer_to_list((Ep bsr 16) band 16#1f), " ", hex(Ep band 16#ffff)];
	T when T =:= tcp; T =:= udp ->
	    [" ", atom_to_list(T), " ", integer_to_list(dprop(Env, Id + 1, init, 0)),
	     case Ep of
		 0 -> "";
		 _ -> [" ", lists:join(".", [integer_to_list((Ep bsr S) band 255)
					     || S <- [24, 16, 8, 0]])]
	     end];
	uart -> [" uart ", integer_to_list((Ep bsr 28) band 16#f),
		 case Ep band 16#fffffff of
		     0 -> "";
		     B -> [" ", integer_to_list(B)]
		 end];
	console -> " console";
	repl -> " repl";
	sms -> [" sms ", name(Env, Ep band 16#ffff),
		case Ep bsr 16 of
		    0 -> "";
		    P -> [" ", name(Env, P - 1)]
		end];
	onewire -> [" onewire ", integer_to_list((Ep bsr 8) band 16#f), ":",
		    integer_to_list(Ep band 16#ff), " ",
		    name(Env, dprop(Env, Id + 1, init, 0))];
	_ -> ""
    end.

hex(V) -> ["0x", string:lowercase(integer_to_list(V, 16))].

%% A value as it would be WRITTEN in source: a string in quotes.
value(_Env, string, S) -> [$", str(S), $"];
value(_Env, integer, V) -> integer_to_list(V);
value(_Env, unsigned, V) -> integer_to_list(V);
value(Env, float, V) -> float_text(maps:get(float, Env), V);
value(_Env, timer, V) -> integer_to_list(V);
value(_Env, _, _) -> "???".

%% csp_print_fixpoint: Q16.16, the fraction truncated to six decimals.
float_text(fixpoint, V) ->
    Fix = round(V * 65536),
    Abs = abs(Fix),
    Frac = ((Abs band 16#ffff) * 1000000) bsr 16,
    [case Fix < 0 of true -> "-"; false -> "" end,
     integer_to_list(Abs bsr 16), ".",
     string:right(integer_to_list(Frac), 6, $0)];
%% csp_print_float on a float build: six decimals, rounded; past 1e9 as
%% d.dddddd e+NN.
float_text(ieee, V) when V < 0 -> ["-", float_text(ieee, -V)];
float_text(ieee, V) when V >= 1.0e9 -> exp_text(V, 0);
float_text(ieee, V) -> fixed6(V).

exp_text(V, E) when V >= 10 -> exp_text(V / 10, E + 1);
exp_text(V, E) -> [fixed6(V), "e+", integer_to_list(E)].

fixed6(V) ->
    Ip = trunc(V),
    Fp = trunc((V - Ip) * 1000000 + 0.5),
    {Ip1, Fp1} = case Fp >= 1000000 of
		     true -> {Ip + 1, Fp - 1000000};
		     false -> {Ip, Fp}
		 end,
    [integer_to_list(Ip1), ".", string:right(integer_to_list(Fp1), 6, $0)].

%% ------------------------------------------------------------------- rules

%% The rules in [From, To), as lines, and the number the next rule gets.
%% Top: a module's body is only counted there -- it was listed inside its
%% own block.
rules(Env, From, To, Indent, Top) ->
    No0 = count_rules(Env, 0, From, 0),
    S = #{rule => From, rule_pos => -1, no => No0, blk => none, stk => []},
    walk(Env, From, To, Indent, Top, S, []).

count_rules(Env, I, To, N) when I < To ->
    case iop(Env, I) of
	'SEGMENT' -> count_rules(Env, I + 1 + slots(Env, I), To, N);
	'RULE' ->
	    count_rules(Env, I + 1, To, N + case is_rule(Env, I) of
						true -> 1;
						false -> 0
					    end);
	_ -> count_rules(Env, I + 1, To, N)
    end;
count_rules(_Env, _I, _To, N) -> N.

slots(Env, I) -> proplists:get_value(slots, args(Env, I)).

depth(#{stk := Stk}) -> length(Stk).

indent(N) -> lists:duplicate(2 * N, $\s).

walk(Env, I, To, Ind, Top, S, Acc) when I < To ->
    case iop(Env, I) of
	'SEGMENT' ->
	    walk(Env, I + 1 + slots(Env, I), To, Ind, Top, S, Acc);
	_ ->
	    {S1, Ends} = close_blocks(Env, I, Ind, S, []),
	    step(Env, I, To, Ind, Top, S1, lists:reverse(Ends) ++ Acc)
    end;
walk(Env, _I, To, Ind, _Top, S, Acc) ->
    Ends = final_ends(Env, To, Ind, S, []),
    {lists:reverse(Acc) ++ Ends, maps:get(no, S)}.

%% `#end' for every block that ends here -- they nest, so a while.
close_blocks(Env, I, Ind, S = #{blk := {End, _Shown}}, Acc) when I >= End ->
    Line = {[indent(Ind + depth(S)), "#end "], {0, iseg(Env, I)}},
    close_blocks(Env, I, Ind, pop(S), [Line | Acc]);
close_blocks(_Env, _I, _Ind, S, Acc) ->
    {S, lists:reverse(Acc)}.

final_ends(Env, To, Ind, S = #{blk := {_, _}}, Acc) ->
    Line = {[indent(Ind + depth(S)), "#end "], {0, iseg(Env, To - 1)}},
    final_ends(Env, To, Ind, pop(S), Acc ++ [Line]);
final_ends(_Env, _To, _Ind, _S, Acc) ->
    Acc.

push(S = #{blk := none}, Blk) -> S#{blk := Blk};
push(S = #{blk := Cur, stk := Stk}, Blk) -> S#{blk := Blk, stk := [Cur | Stk]}.

pop(S = #{stk := [Prev | Stk]}) -> S#{blk := Prev, stk := Stk};
pop(S = #{stk := []}) -> S#{blk := none}.

in_block(#{blk := none}) -> 0;
in_block(_) -> 1.

step(Env, I, To, Ind, Top, S, Acc) ->
    Op = iop(Env, I),
    case Op of
	%% A #when gate: one NINSTATE against zero, the condition before it.
	'NINSTATE' ->
	    case args(Env, I) of
		[_, 0, {nxt, Nxt} | _] ->
		    S1 = push(S, {I + Nxt, true}),
		    Line = {[indent(Ind + depth(S1)), "#when ",
			     when_text(Env, maps:get(rule, S), I)],
			    {0, iseg(Env, I)}},
		    walk(Env, I + 1, To, Ind, Top, S1#{rule := I + 1},
			 [Line | Acc]);
		_ ->
		    walk(Env, I + 1, To, Ind, Top, S, Acc)
	    end;
	'LD' ->
	    case in_gate(Env, I, To) of
		{true, J, States} ->
		    [_, _, {nxt, Nxt} | _] = args(Env, J),
		    S1 = push(S, {J + Nxt, true}),
		    Line = {[indent(Ind + depth(S1)), "#in",
			     [[" ", state_name(Env, N)] || N <- States]],
			    {0, iseg(Env, I)}},
		    walk(Env, J + 1, To, Ind, Top, S1#{rule := J + 1},
			 [Line | Acc]);
		false ->
		    walk(Env, I + 1, To, Ind, Top, S, Acc)
	    end;
	'ENTER' when Top ->
	    N = proplists:get_value(n, args(Env, I)),
	    No = count_rules(Env, I + 1, I + 1 + N, maps:get(no, S)),
	    walk(Env, I + N + 2, To, Ind, Top, S#{no := No, rule := I + N + 2},
		 Acc);
	_ when Op =:= 'ENTER'; Op =:= 'LEAVE' ->
	    walk(Env, I + 1, To, Ind, Top, S#{rule := I + 1}, Acc);
	'RULE' ->
	    No = maps:get(no, S) + case is_rule(Env, I) of
					true -> 1;
					false -> 0
				    end,
	    walk(Env, I + 1, To, Ind, Top, S#{rule_pos := I, no := No}, Acc);
	'NEXT' ->
	    Rp = maps:get(rule_pos, S),
	    Show = Rp >= 0 andalso binding(Env, Rp) =:= 0 andalso
		defines_out(Env, Rp) < 0,
	    Acc1 = case Show of
		       false -> Acc;
		       true ->
			   No = case rule_binds(Env, Rp) of
				    true -> 0;
				    false -> maps:get(no, S)
				end,
			   [{[indent(Ind + depth(S) + in_block(S)),
			      print_rule(Env, maps:get(rule, S), plain)],
			     {No, iseg(Env, Rp)}} | Acc]
		   end,
	    walk(Env, I + 1, To, Ind, Top, S#{rule := I + 1, rule_pos := -1},
		 Acc1);
	_ ->
	    walk(Env, I + 1, To, Ind, Top, S, Acc)
    end.

%% An #in gate: `LD State ; NINSTATE* ; INSTATE'. An #in chain always ends
%% in an INSTATE, which is what tells it from a #when on a plain variable.
in_gate(Env, I, To) ->
    case I + 1 < To andalso lists:member(iop(Env, I + 1), ['NINSTATE', 'INSTATE']) of
	false -> false;
	true -> in_chain(Env, I + 1, To, [])
    end.

in_chain(Env, J, To, Acc) when J < To ->
    case iop(Env, J) of
	'NINSTATE' -> in_chain(Env, J + 1, To, [state_imm(Env, J) | Acc]);
	'INSTATE' -> {true, J, lists:reverse([state_imm(Env, J) | Acc])};
	_ -> false
    end;
in_chain(_Env, _J, _To, _Acc) -> false.

state_imm(Env, J) -> lists:nth(2, args(Env, J)).

is_rule(Env, I) ->
    iop(Env, I) =:= 'RULE' andalso not rule_binds(Env, I).

%% The first store decides: a rule writing a local binds it.
rule_binds(Env, Ri) -> rule_binds_(Env, Ri + 1).

rule_binds_(Env, I) ->
    case iop(Env, I) of
	'NEXT' -> false;
	data -> false;
	'STI' -> local_target(Env, mem(args(Env, I)));
	Op when Op =:= 'ST'; Op =:= 'STIMP' -> local_target(Env, mem(args(Env, I)));
	_ -> rule_binds_(Env, I + 1)
    end.

local_target(Env, M) -> is_local(Env, M).

mem(Args) -> hd([M || {mem, M} <- Args]).

%% The object whose `#local ... in' this rule binds, or 0.
binding(Env, Ri) -> binding_(Env, Ri + 1, 0, 0).

binding_(Env, I, Seto, Obj) ->
    case iop(Env, I) of
	'NEXT' -> Obj;
	data -> Obj;
	'SETO' -> binding_(Env, I + 1, obj(args(Env, I)), Obj);
	Op when Op =:= 'ST'; Op =:= 'STIMP'; Op =:= 'STI'; Op =:= 'STP' ->
	    M = mem(args(Env, I)),
	    Dir = dprop(Env, M, dir),
	    case (Op =:= 'ST' orelse Op =:= 'STI') andalso Seto =/= 0 andalso
		is_local(Env, M) andalso (Dir =:= in orelse Dir =:= inout)
		andalso name(Env, M) =/= "" of
		false -> 0;
		true -> binding_(Env, I + 1, 0, Seto)
	    end;
	_ -> binding_(Env, I + 1, 0, Obj)
    end.

obj(Args) -> hd([O || {obj, O} <- Args]).

%% The `#local ... out' whose formula this rule is, or -1.
defines_out(Env, Ri) -> defines_out_(Env, Ri + 1, 0, -1).

defines_out_(Env, I, Seto, Found) ->
    case iop(Env, I) of
	'NEXT' -> Found;
	data -> Found;
	'SETO' -> defines_out_(Env, I + 1, 1, Found);
	Op when Op =:= 'ST'; Op =:= 'STI' ->
	    M = mem(args(Env, I)) band 16#7fff,
	    case Seto =:= 0 andalso is_local(Env, M) andalso
		dprop(Env, M, dir) =:= out andalso name(Env, M) =/= "" of
		true -> defines_out_(Env, I + 1, 0, M);
		false -> -1
	    end;
	Op when Op =:= 'STIMP'; Op =:= 'STP' -> -1;
	_ -> defines_out_(Env, I + 1, 0, Found)
    end.

%% ` X<-expr' for every binding of object M, on its `#Mod m' line. A
%% default -- a constant equal to the declaration's own -- says nothing.
bindings(Env, M) ->
    [[" ", print_rule(Env, start_of(Env, Ri), bind)]
     || Ri <- lists:sort(maps:keys(maps:get(code, Env))),
	iop(Env, Ri) =:= 'RULE', binding(Env, Ri) =:= M,
	not default_binding(Env, Ri)].

start_of(Env, Ri) when Ri > 0 ->
    case iop(Env, Ri - 1) of
	'LI' -> Ri - 1;
	_ -> Ri
    end;
start_of(_Env, Ri) -> Ri.

default_binding(Env, Ri) ->
    case const_store(Env, Ri + 1, 0, none) of
	{M, V} ->
	    dprop(Env, M, dir) =:= inout andalso
		bits(dprop(Env, M, init)) =:= V band 16#ffffffff;
	none -> false
    end.

const_store(Env, I, R, Got) ->
    case {iop(Env, I), Got} of
	{'NEXT', _} -> Got;
	{'SETO', _} -> const_store(Env, I + 1, R, Got);
	{'LI', _} -> const_store(Env, I + 1, imm(args(Env, I)), Got);
	{'LIU', _} -> const_store(Env, I + 1, imm(args(Env, I)), Got);
	{'LIH', _} -> const_store(Env, I + 1,
				  (R band 16#ffff) bor (imm(args(Env, I)) bsl 16),
				  Got);
	{'ST', none} -> const_store(Env, I + 1, R, {mem(args(Env, I)) band 16#7fff, R});
	{'STI', none} -> const_store(Env, I + 1, R, {mem(args(Env, I)) band 16#7fff,
						     imm(args(Env, I))});
	_ -> none
    end.

imm(Args) -> hd([V || {imm, V} <- Args]).

bits(V) when is_integer(V) -> V band 16#ffffffff;
bits(V) when is_float(V) -> round(V * 65536) band 16#ffffffff;
bits(_) -> 0.

formula(Env, Di) ->
    case [Ri || Ri <- lists:sort(maps:keys(maps:get(code, Env))),
		iop(Env, Ri) =:= 'RULE', defines_out(Env, Ri) =:= Di] of
	[Ri | _] -> print_rule(Env, start_of(Env, Ri), rhs);
	[] -> ""
    end.

%% ----------------------------------------------------------- the renderer

%% A rule from its first instruction: the body, then ` ? cond' unless the
%% condition is the implicit -1. The body is walked from just past the
%% OP_RULE, the condition from where the rule starts.
print_rule(Env, Start, Mode) ->
    Ri = find_rule(Env, Start),
    {body, Body} = expr(Env, Ri + 1, buf(Mode)),
    case expr(Env, Start, buf(Mode)) of
	{cnd, Cond} when Cond =/= "", Cond =/= "-1" -> [Body, " ? ", Cond];
	_ -> Body
    end.

find_rule(Env, I) ->
    case iop(Env, I) of
	'SEGMENT' -> find_rule(Env, I + 1 + slots(Env, I));
	'RULE' -> I;
	_ -> find_rule(Env, I + 1)
    end.

when_text(Env, From, Gate) ->
    [X | _] = args(Env, Gate),
    {stop, B} = expr(Env, From, buf(plain)),
    text(B, X).

buf(Mode) ->
    #{reg => #{}, arg => #{}, body => [], seto => 0, setox => none,
      mode => Mode, id => 0}.

%% A rendered value: {Id, Text, Prio, Imm}. Id is its identity -- the body
%% compares by it, the way the C compares interned references.
new(B = #{id := N}, Text, Prio, Imm) ->
    {{N, lists:flatten(Text), Prio, Imm}, B#{id := N + 1}}.

reg(B, R) -> maps:get(R, maps:get(reg, B), {-1, "", 110, 0}).
text(B, R) -> element(2, reg(B, R)).
prio(B, R) -> element(3, reg(B, R)).
regi(B, R) -> element(4, reg(B, R)).
setreg(B, R, V) -> B#{reg := (maps:get(reg, B))#{R => V}}.

wrap(B, R, Outer) ->
    case prio(B, R) < Outer of
	true -> ["(", text(B, R), ")"];
	false -> text(B, R)
    end.

expr(Env, I, B) ->
    case instr(Env, I) of
	undefined -> {cnd, ""};
	{instr, _, Op, Args} -> op(Env, I, Op, Args, B)
    end.

next(Env, I, B) -> expr(Env, I + 1, B).

op(Env, I, 'SEGMENT', Args, B) ->
    expr(Env, I + 1 + proplists:get_value(slots, Args), B);
op(Env, I, 'SETO', [{obj, O}], B) -> next(Env, I, B#{seto := O});
op(Env, I, 'SETOX', [X | _], B) -> next(Env, I, B#{setox := text(B, X)});
op(_Env, _I, Op, _Args, B) when Op =:= 'INSTATE'; Op =:= 'NINSTATE' ->
    {stop, B};
op(_Env, _I, 'RULE', [Cnd | _], B) -> {cnd, text(B, Cnd)};
op(_Env, _I, 'NEXT', [X], B = #{body := Body}) ->
    Items = lists:reverse(Body),
    Final = reg(B, X),
    Tail = case Items =/= [] andalso element(1, lists:last(Items)) =:= element(1, Final) of
	       true -> [];
	       false -> [Final]
	   end,
    {body, lists:join(",", [element(2, V) || V <- Items ++ Tail])};
op(Env, I, Op, [X, {mem, M}], B) when Op =:= 'ST'; Op =:= 'STIMP' ->
    Mode = maps:get(mode, B),
    B0 = case Mode of bind -> B#{seto := 0}; _ -> B end,
    {Var, B1} = var(Env, B0, M),
    Lhs = case Mode of
	      rhs -> "";
	      bind -> [Var, "<-"];
	      _ when Op =:= 'STIMP' -> [Var, "<-"];
	      _ -> [Var, "="]
	  end,
    Rhs = case dprop(Env, M, type) =:= string andalso prio(B1, X) =:= 110 of
	      true -> [$", string_at(Env, regi(B1, X)), $"];
	      false -> text(B1, X)
	  end,
    store(Env, I, B1, X, [Lhs, Rhs]);
op(Env, I, 'STP', [X, {mem, M}, Part], B) ->
    {Var, B1} = var(Env, B, M),
    Rhs = case Part =:= dir andalso prio(B1, X) =:= 110 of
	      true -> pindir(regi(B1, X));
	      false -> text(B1, X)
	  end,
    store(Env, I, B1, X, [Var, ".", atom_to_list(Part), "=", Rhs]);
op(Env, I, 'STI', [X, {imm, V}, {mem, M}], B) ->
    Mode = maps:get(mode, B),
    B0 = case Mode of bind -> B#{seto := 0}; _ -> B end,
    {Var, B1} = var(Env, B0, M),
    Lhs = case Mode of
	      rhs -> "";
	      bind -> [Var, "<-"];
	      _ -> [Var, "="]
	  end,
    Rhs = case state_var(Env, M) andalso state_name(Env, V) =/= "" of
	      true -> state_name(Env, V);
	      false ->
		  case dprop(Env, M, type) of
		      string -> [$", string_at(Env, V), $"];
		      _ -> integer_to_list(V)
		  end
	  end,
    store(Env, I, B1, X, [Lhs, Rhs]);
op(Env, I, 'TMO', [X, {mem, M}], B) ->
    {Var, B1} = var(Env, B, M),
    {V, B2} = new(B1, ["timeout(", Var, ")"], 110, 0),
    next(Env, I, setreg(B2, X, V));
op(Env, I, 'CHG', [X | _], B) ->
    {V, B1} = new(B, "", 110, 0),
    next(Env, I, setreg(B1, X, V));
op(Env, I, 'ARG', [X, {imm, N}], B) ->
    next(Env, I, B#{arg := (maps:get(arg, B))#{N => {text(B, X), regi(B, X)}}});
op(Env, I, 'CALL', Args = [X | _], B) ->
    {V, B1} = fcall(Env, B, Args),
    B2 = setreg(B1, X, V),
    B3 = case consumed(Env, I + 1, X) of
	     true -> B2;
	     false -> B2#{body := [V | maps:get(body, B2)]}
	 end,
    next(Env, I, B3);
op(Env, I, 'LD', [X, {mem, M}], B) ->
    {Var, B1} = var(Env, B, M),
    {V, B2} = new(B1, Var, 110, 0),
    next(Env, I, setreg(B2, X, V));
op(Env, I, 'LDP', [X, {mem, M}, Part], B) ->
    {Var, B1} = var(Env, B, M),
    {V, B2} = new(B1, [Var, ".", atom_to_list(Part)], 110, 0),
    next(Env, I, setreg(B2, X, V));
op(Env, I, 'LIU', [X, {imm, V0}], B) ->
    {V, B1} = new(B, ["0x", hex4(V0)], 110, V0),
    next(Env, I, setreg(B1, X, V));
op(Env, I, 'LIH', [X, {imm, V0}], B) ->
    S = signed16(V0),
    Hi = case S < 0 of
	     true -> ["-0x", hex4(-S)];
	     false -> ["0x", hex4(S)]
	 end,
    {V, B1} = new(B, [Hi, ".", text(B, X)], 110, 0),
    next(Env, I, setreg(B1, X, V));
op(Env, I, 'LI', [X, {imm, V0}], B) ->
    {V, B1} = new(B, integer_to_list(V0), 110, V0 band 16#ffff),
    next(Env, I, setreg(B1, X, V));
op(Env, I, Op, [X, Y | _], B) when Op =:= 'MOV'; Op =:= 'CVTIF'; Op =:= 'CVTFI' ->
    next(Env, I, setreg(B, X, reg(B, Y)));
op(Env, I, Op, Args, B) ->
    case alu(Op) of
	none -> next(Env, I, B);
	{Name, Arity, Prec} ->
	    Swap = lists:member(swap, Args),
	    [X, Y | Rest] = [R || R <- Args, is_reg(R)],
	    {Name1, Y1, Z1} = case {Swap, Rest} of
				  {true, [Z | _]} -> {mirror(Name), Z, Y};
				  {false, [Z | _]} -> {Name, Y, Z};
				  {_, []} -> {Name, Y, none}
			      end,
	    next(Env, I, alu(B, X, Y1, Z1, Name1, Arity, Prec))
    end.

is_reg(R) -> lists:member(R, [r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10,
			      r11, r12, r13, r14, r15]).

store(Env, I, B, X, Text) ->
    {V, B1} = new(B, Text, 5, 0),
    B2 = setreg(B1, X, V),
    next(Env, I, B2#{body := [V | maps:get(body, B2)]}).

alu(B, X, Y, _Z, Name, 1, Prec) ->
    {V, B1} = new(B, [Name, wrap(B, Y, Prec)], Prec, 0),
    setreg(B1, X, V);
alu(B, X, Y, Z, Name, 2, Prec) ->
    case {text(B, Y), text(B, Z)} of
	{"", ""} -> setreg(B, X, setelement(3, reg(B, Y), Prec));
	{"", _} -> setreg(B, X, reg(B, Z));
	{_, ""} -> setreg(B, X, reg(B, Y));
	_ ->
	    {V, B1} = new(B, [wrap(B, Y, Prec), Name, wrap(B, Z, Prec)], Prec, 0),
	    setreg(B1, X, V)
    end.

%% opcode -> {spelling, arity, precedence}: the tok_table rows.
alu('NOT')   -> {"!", 1, 105};
alu('BNOT')  -> {"~", 1, 105};
alu('NEG')   -> {"-", 1, 105};
alu('FNEG')  -> {"-", 1, 105};
alu('FMOV')  -> {"mov", 1, 105};
alu('ADD')   -> {"+", 2, 90};
alu('FADD')  -> {"+", 2, 90};
alu('SUB')   -> {"-", 2, 90};
alu('FSUB')  -> {"-", 2, 90};
alu('MUL')   -> {"*", 2, 100};
alu('FMUL')  -> {"*", 2, 100};
alu('DIV')   -> {"/", 2, 100};
alu('FDIV')  -> {"/", 2, 100};
alu('REM')   -> {"%", 2, 100};
alu('SLA')   -> {"<<", 2, 80};
alu('SRA')   -> {">>", 2, 80};
alu('LT')    -> {"<", 2, 70};
alu('FLT')   -> {"<", 2, 70};
alu('LTE')   -> {"<=", 2, 70};
alu('FLTE')  -> {"<=", 2, 70};
alu('EQEQ')  -> {"==", 2, 60};
alu('FEQEQ') -> {"==", 2, 60};
alu('NEQ')   -> {"!=", 2, 60};
alu('FNEQ')  -> {"!=", 2, 60};
alu('BAND')  -> {"&", 2, 50};
alu('BXOR')  -> {"^", 2, 40};
alu('BOR')   -> {"|", 2, 30};
alu('AND')   -> {"&&", 2, 20};
alu('OR')    -> {"||", 2, 10};
alu('EQ')    -> {"=", 2, 5};
alu('RIMP')  -> {"<-", 2, 4};
alu(_)       -> none.

mirror("<")  -> ">";
mirror("<=") -> ">=";
mirror(Op)   -> Op.

hex4(V) -> string:right(string:lowercase(integer_to_list(V band 16#ffff, 16)), 4, $0).

signed16(V) when V >= 16#8000 -> V - 16#10000;
signed16(V) -> V.

pindir(D) -> element((D band 3) + 1, {"none", "in", "out", "inout"}).

%% A variable as the source names it: object prefix, $N for a local, the
%% subscript put back.
var(Env, B = #{seto := Seto, setox := Setox}, Ix) ->
    Obj = case Seto of
	      0 -> "";
	      M -> [str(maps:get(M, maps:get(objects, Env), <<>>)), "."]
	  end,
    D = d(Env, Ix),
    Head = case D of undefined -> Ix band 16#7fff; #{head := H} -> H end,
    Elem = case D of undefined -> 0; #{elem := E} -> E end,
    Base = case dtype(Env, Head) of
	       view ->
		   Bit = dprop(Env, Head, bit),
		   Len = dprop(Env, Head, len),
		   [name(Env, dprop(Env, Head, id)),
		    case Setox =:= none andalso Bit band 7 =:= 0 andalso
			Len band 7 =:= 0 of
			true ->
			    Lo = Bit bsr 3,
			    Hi = Lo + (Len bsr 3) - 1,
			    ["[", integer_to_list(Lo),
			     case Hi =:= Lo of
				 true -> "";
				 false -> ["..", integer_to_list(Hi)]
			     end, "]"];
			false -> ""
		    end];
	       _ ->
		   case is_local(Env, Head) andalso name(Env, Head) =:= "" of
		       true -> ["$", integer_to_list(local_number(Env, Head))];
		       false -> name(Env, Head)
		   end
	   end,
    Sub = case {Setox, Elem} of
	      {none, 0} -> "";
	      {none, K} -> ["[", integer_to_list(K), "]"];
	      {T, _} -> ["[", T, "]"]
	  end,
    {lists:flatten([Obj, Base, Sub]), B#{seto := 0, setox := none}}.

%% The built-in function table, csp_builtin_funcs in csp_rt.c: name and the
%% DECLARED argument types, which say when an argument is a variable.
builtin(I) ->
    element(I + 1,
	    {{"min", []}, {"max", []}, {"abs", []}, {"sign", []}, {"clip", []},
	     {"trunc", []}, {"round", []},
	     {"elapsed", [timer]}, {"progress", [timer]},
	     {"changed", [index]}, {"rising", [digital]}, {"falling", [digital]},
	     {"print", []}, {"print", []}, {"print", []}, {"print", []},
	     {"println", []}, {"println", []}, {"println", []}, {"println", []},
	     {"println", []},
	     {"tick", []}, {"cycle", []}, {"latch", []}}).

fcall(Env, B, [_X, {idx, Idx}, {usr, Usr}, {avt, Avt}]) ->
    {Name, Decl} = case Usr of
		       0 when Idx < 24 -> builtin(Idx);
		       _ -> {"f" ++ integer_to_list(Idx), []}
		   end,
    Args = maps:get(arg, B),
    {Texts, B1} =
	lists:mapfoldl(
	  fun({K, A}, Bi) ->
		  {T, Ti} = maps:get(K, Args, {"", 0}),
		  Dt = case K < length(Decl) of
			   true -> lists:nth(K + 1, Decl);
			   false -> any
		       end,
		  case A =:= string andalso T =/= "" andalso
		      lists:all(fun(C) -> C >= $0 andalso C =< $9 end, T) of
		      true -> {[$", string_at(Env, list_to_integer(T)), $"], Bi};
		      false when Dt =:= index; Dt =:= timer; Dt =:= analog;
				 Dt =:= digital ->
			  var(Env, Bi, Ti);
		      false -> {T, Bi}
		  end
	  end, B, lists:zip(lists:seq(0, length(Avt) - 1), Avt)),
    new(B1, [Name, "(", lists:join(",", Texts), ")"], 110, 0).

%% Is register R read later in the rule before it is written again? A call
%% whose result nobody reads is a statement (print) and goes in the body.
consumed(Env, J, R) ->
    case instr(Env, J) of
	undefined -> false;
	{instr, _, Op, Args} ->
	    Regs = [A || A <- Args, is_reg(A)],
	    case Op of
		'SEGMENT' -> consumed(Env, J + 1 + proplists:get_value(slots, Args), R);
		_ when Op =:= 'NEXT'; Op =:= 'RULE' -> false;
		_ when Op =:= 'ST'; Op =:= 'STP'; Op =:= 'STIMP'; Op =:= 'CHG' ->
		    hd(Regs) =:= R orelse consumed(Env, J + 1, R);
		'ARG' -> hd(Regs) =:= R orelse consumed(Env, J + 1, R);
		_ when Op =:= 'LD'; Op =:= 'LDP'; Op =:= 'LI'; Op =:= 'LIU';
		       Op =:= 'LIH'; Op =:= 'CALL' ->
		    hd(Regs) =/= R andalso consumed(Env, J + 1, R);
		_ when Op =:= 'MOV'; Op =:= 'CVTIF'; Op =:= 'CVTFI' ->
		    [X, Y | _] = Regs,
		    Y =:= R orelse (X =/= R andalso consumed(Env, J + 1, R));
		_ ->
		    case alu(Op) of
			none -> consumed(Env, J + 1, R);
			{_, Arity, _} ->
			    %% A unary op's z field is zero: r0, as the C reads it.
			    [X, Y | Zs] = Regs,
			    Z = case {Arity, Zs} of
				    {1, _} -> r0;
				    {_, [Z0 | _]} -> Z0
				end,
			    Y =:= R orelse Z =:= R orelse
				(X =/= R andalso consumed(Env, J + 1, R))
		    end
	    end
    end.
