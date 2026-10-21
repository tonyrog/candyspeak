%%% @author Claude Opus 5
%%% @doc
%%%     candyspeak -> varp `system', for bounded model checking.
%%%
%%%     Reads the same parse tree candyspeak.erl:to_c/1 walks, and emits a varp
%%%     model of the program, so a property can be PROVED rather than tested.
%%%
%%%         candyspeak_varp:file("demo/home.csp").      %% -> demo/home.varp
%%%         candyspeak_varp:file(In, Out).
%%%
%%%     THE TRANSLATION
%%%
%%%     candyspeak rules read the COMMITTED state, which is what varp's
%%%     synchronous composition already does: a condition reads `X' (the value at
%%%     step i-1) and an assignment writes `next(X)'. Sensors become `input',
%%%     which is a free choice per step, matching a DIN committed at the top of
%%%     the cycle. One cycle is one `next'.
%%%
%%%     Several rules may write the same variable AND THE LAST ONE WINS. That
%%%     becomes a priority chain: the guard of a later rule is negated in every
%%%     earlier branch, and a final branch holds the value when nothing matched.
%%%     The chain must be TOTAL -- a variable left undetermined in some case is
%%%     not "unchanged", it is anything the solver likes.
%%%
%%%     A #local with a formula is inlined where it is read: it is computed from
%%%     the committed state in the same cycle, which is exactly what a guard
%%%     reads. An input #buffer is free bytes `B_b0'.. and a free `B_rx'; the
%%%     bytes may change in any step, more than a bus can do. A #param is set
%%%     after delivery, so it is FROZEN, not fixed: a state with a free initial
%%%     value that never changes, and a proof holds for every setting at once.
%%%     `translate(In, [{params, fixed}])' makes them the declared numbers.
%%%
%%%     STORES WRAP. varp arithmetic is exact and `==' compares values, so
%%%     `next(N) == N + 1' on an 8 bit N has NO successor at 255 -- a path the
%%%     program takes would be cut off, and a proof would skip it. Every value
%%%     stored into a variable or a local is therefore folded into the declared
%%%     width (wrap/4), from an interval of what the expression can be (rng/2).
%%%     An expression that may land more than one wrap away is not translated.
%%%     Arithmetic between the stores is exact, where the runtime is 32 bit.
%%%
%%%     WHAT CANNOT BE MODELLED, and what is done about it
%%%
%%%     Timers, part access (`T.period'), arrays, `/', shifts and the
%%%     arithmetic functions are not translated. A variable written by any such rule is emitted as `input'
%%%     instead of `state': FREE. That is deliberately conservative -- the model
%%%     then has MORE behaviour than the program, so a proof still holds, while a
%%%     counterexample may be spurious and has to be read against the source.
%%%     The alternative, dropping the rule, would make the model have LESS
%%%     behaviour, and then a proof would mean nothing.
%%% @end

-module(candyspeak_varp).

-export([file/1, file/2]).
-export([translate/1, translate/2]).
-export([collect/1, collect/2, check/2]).         % for tests

-define(FAILSAFE, 2).
-define(FIRST_USER_STATE, 3).          % INIT 0, NORMAL 1, FAILSAFE 2

%%% ------------------------------------------------------------------ entry

file(In) ->
    Out = filename:rootname(In) ++ ".varp",
    file(In, Out).

file(In, Out) ->
    case translate(In) of
	{ok, IoList} ->
	    case file:write_file(Out, IoList) of
		ok    -> {ok, Out};
		Error -> Error
	    end;
	Error -> Error
    end.

translate(In) -> translate(In, []).

%% The whole program, #import included: candyspeak:parse/2 expands the imports
%% (the same rules as csp), and `sources' marks where each file starts, so a
%% rule from an imported file is shown as file:line and not as a line number
%% that means something else in the file being translated.
translate(In, Opts) ->
    case candyspeak:parse(In, [sources]) of
	{ok, Ast} ->
	    case modules_in(Ast) of
		[] -> {ok, emit(In, collect(Ast, In, Opts), Opts)};
		Ms -> {error, {instances_not_modelled, Ms}}
	    end;
	Error -> Error
    end.

%%% OBJECT INSTANCES ARE NOT MODELLED, and the translator refuses rather than
%%% guessing.
%%%
%%% NOT because of state numbering. States are numbered GLOBALLY BY NAME --
%%% measured: with global `on off' at 3 and 4, a module declaring `on off' gets
%%% 3 and 4 too, while a module with its own names continues the sequence at 5.
%%% That is why a name may appear in several #states blocks, and it is what the
%%% global list here already does. (csp.h's csp_statedecl_t comment says each
%%% module is numbered "from 3 again, independently" -- the runtime does not do
%%% that.)
%%%
%%% The real gap is per-INSTANCE storage. `#M a' and `#M b' have their own a.V
%%% and b.V, and this walk sees one `#variable V' and models one variable. Two
%%% instances would share it, which is a model of a different program -- and the
%%% consistency check does not notice, because the transition relation is still
%%% satisfiable.
%%%
%%% Doing it properly means one varp `system' per #module and an `instance' per
%%% object, which is exactly what varp has for it.
modules_in(Ast) ->
    [Name || {module, _Ln, {'WORD', _, Name}} <- Ast]
	++ [Name || {module, _Ln, {'WORD', _, Name}, _} <- Ast].

%%% ---------------------------------------------------------------- collect
%%%
%%% The parse tree is a FLAT list in which {in,_,States} and {'end',_} bracket
%%% the blocks, so the walk carries the current gate rather than recursing.

collect(Ast) ->
    collect(Ast, undefined).

collect(Ast, Top) ->
    collect(Ast, Top, []).

%% Top is the file being translated: its own lines are plain numbers.
collect(Ast, Top, Opts) ->
    C = #{sigs => #{}, order => [], states => [], rules => [], gate => all,
	  consts => #{}, locals => #{}, lorder => [], bufs => #{},
	  top => Top, file => Top,
	  params => proplists:get_value(params, Opts, free)},
    finish(lists:foldl(fun item/2, C, Ast)).

%% Where an imported file starts, and where its importer picks up again.
item({source, _Ln, File}, C) ->
    C#{file := File};

item({states, _Ln, Names}, C) ->
    C#{states := maps:get(states, C) ++ [N || {'WORD', _, N} <- Names]};

item({in, _Ln, Names}, C) ->
    C#{gate := [N || {'WORD', _, N} <- Names]};

item({'end', _Ln}, C) ->
    C#{gate := all};

%% A #constant or #define is a compile-time number: a varp `define'. Note the
%% arities -- #constant has an array slot, #define has neither type nor width.
item({constant, _Ln, {'WORD', _, Name}, scalar, _Res, _Opts, Expr}, C) ->
    add_const(Name, Expr, C);
item({define, _Ln, {'WORD', _, Name}, Expr}, C) ->
    add_const(Name, Expr, C);

%% A #param is a setting, frozen for the run but free across runs (see the top).
%% A float one is left out, and whatever reads it is not translated.
item({param, _Ln, {'WORD', _, Name}, Res, Opts, Expr}, C) ->
    case {maps:get(params, C), proplists:get_value(type, Opts)} of
	{_, float} -> C;
	{fixed, _} -> add_const(Name, Expr, C);
	{free, _}  -> add_sig(Name, param, Res, Opts, Expr, C)
    end;

item({Kind, _Ln, {'WORD', _, Name}, scalar, Res, Opts, Expr}, C)
  when Kind =:= digital; Kind =:= analog; Kind =:= variable ->
    case proplists:get_value(type, Opts) of
	float  -> C;
	string -> C;
	_      -> add_sig(Name, Kind, Res, Opts, Expr, C)
    end;

%% A #local with a formula is inlined where it is read. `#local X in', bound by
%% an instance, has none -- and instances are refused anyway.
item({local, _Ln, {'WORD', _, Name}, Res, Opts, Expr}, C)
  when Expr =/= undefined ->
    case proplists:get_value(type, Opts) of
	T when T =:= float; T =:= string -> C;
	_ ->
	    L = #{expr => Expr, width => width_of(local, Res),
		  signed => signed_of(local, Res, Opts)},
	    C#{locals := maps:put(Name, L, maps:get(locals, C)),
	       lorder := maps:get(lorder, C) ++ [Name]}
    end;

%% An input buffer: its bytes and .rx, all free. An output buffer is written by
%% rules this translator does not model, and reading one is not translated.
item({buffer, _Ln, {'WORD', _, Name}, {'INT', _, Size}, Opts, _Tr}, C) ->
    case proplists:get_value(dir, Opts) of
	in -> C#{bufs := maps:put(Name, list_to_integer(Size),
				  maps:get(bufs, C))};
	_  -> C
    end;

%% Anything else declared -- timers, buffers, fields, arrays, modules -- is not
%% modelled. Rules that mention them are caught by unmodellable/1 below.
item({rule, Ln, Assigns, Guard}, C) ->
    lists:foldl(fun(A, Ci) -> assign(A, Ln, Guard, Ci) end, C, Assigns);

item(_Other, C) ->
    C.

add_const(Name, Expr, C) ->
    case const_of(Expr) of
	{ok, V} -> C#{consts := maps:put(Name, V, maps:get(consts, C))};
	none    -> C
    end.

add_sig(Name, Kind, Res, Opts, Expr, C) ->
    Sig = #{kind   => Kind,
	    width  => width_of(Kind, Res),
	    signed => signed_of(Kind, Res, Opts),
	    dir    => proplists:get_value(dir, Opts, undefined),
	    init   => case const_of(Expr) of {ok, V} -> V; none -> 0 end},
    C#{sigs  := maps:put(Name, Sig, maps:get(sigs, C)),
       order := maps:get(order, C) ++ [Name]}.

assign({'=', _Ln, Target, Expr}, Ln, Guard, C) ->
    R = #{target => target_name(Target),
	  guard  => Guard,
	  expr   => Expr,
	  states => maps:get(gate, C),
	  line   => Ln,
	  file   => imported(C)},
    C#{rules := maps:get(rules, C) ++ [R]};
assign(_Other, _Ln, _Guard, C) ->
    C.

%% The file a rule came from when it is not the one being translated, else
%% undefined.
imported(#{file := F, top := T}) when F =:= T -> undefined;
imported(#{file := F})                        -> F.

%% A line, as a reader finds it: a plain number in the translated file, and
%% file:number in one it imports.
loc(Ln, undefined) -> integer_to_list(Ln);
loc(Ln, File)      -> [filename:basename(File), ":", integer_to_list(Ln)].

%% The same, as part of a varp identifier: decls.csp line 28 is _decls_L28.
loc_id(Ln, undefined) -> "_L" ++ integer_to_list(Ln);
loc_id(Ln, File) ->
    Base = filename:basename(filename:rootname(File)),
    "_" ++ [case (C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z)
		 orelse (C >= $0 andalso C =< $9) of
		true -> C; false -> $_ end || C <- Base]
	++ "_L" ++ integer_to_list(Ln).

%% `X' or `X.part' -- the latter is not modellable, and is kept as a tagged name
%% so the rule can be recognised and its target set free.
target_name({field, _, {'WORD', _, Name}})               -> Name;
target_name({field, _, {part, _, {'WORD', _, N}, _P}})    -> {part, N};
target_name(Other)                                        -> {other, Other}.

const_of({'INT', _, S}) -> num_of(S);
const_of({'-', _, {'INT', _, S}}) ->
    case num_of(S) of
	{ok, V} -> {ok, -V};
	none    -> none
    end;
const_of(undefined)     -> none;
const_of(_)             -> none.

%% candyspeak writes hex as 0xFFFF (not 16#FFFF). varp takes 0x, 0b and octal
%% literally, so vexpr passes the text through -- but a #param has to become an
%% integer here.
num_of("0x" ++ H) -> safe_int(H, 16);
num_of("0X" ++ H) -> safe_int(H, 16);
num_of("0b" ++ B) -> safe_int(B, 2);
num_of("0B" ++ B) -> safe_int(B, 2);
num_of(S)         -> safe_int(S, 10).

safe_int(S, Base) ->
    try {ok, list_to_integer(S, Base)}
    catch _:_ -> none
    end.

%% #digital is one bit; the rest carry a declared width, and one without gets
%% 32, as the runtime does (`#variable X = 0' lists as `X:32 integer'). It used
%% to be 16 here, which left out every value the program can reach above that.
width_of(digital, _)              -> 1;
width_of(_, {'INT', _, S})        -> list_to_integer(S);
width_of(_, _)                    -> 32.

%% Signed as the runtime has it: `unsigned' or `integer' when written, else a
%% one bit leaf and the pins are unsigned and every other width is an integer.
signed_of(_Kind, _Res, [{type, unsigned} | _]) -> false;
signed_of(_Kind, _Res, [{type, integer} | _])  -> true;
signed_of(Kind, Res, [_ | Opts])               -> signed_of(Kind, Res, Opts);
signed_of(digital, _Res, [])                   -> false;
signed_of(analog, _Res, [])                    -> false;
signed_of(Kind, Res, [])                       -> width_of(Kind, Res) > 1.

%%% After the walk: work out which signals are WRITTEN, and which of those can
%%% be modelled at all.
finish(C0) ->
    Written = lists:usort([T || #{target := T} <- maps:get(rules, C0),
				is_list(T)]),
    %% A local a rule assigns is not a formula any more -- its value depends on
    %% where in the cycle it is read -- so it is not inlined, and reading it is
    %% not translated.
    C1 = C0#{locals := maps:without(Written, maps:get(locals, C0))},
    C = abstract_guards(C1),
    Rules = maps:get(rules, C),
    %% Only an untranslatable VALUE forces a target free -- we cannot say what
    %% it would be assigned. An untranslatable GUARD has been abstracted to a
    %% free boolean above, so the rule may or may not fire: more behaviour than
    %% the program has, which is the safe direction.
    Sigs = maps:get(sigs, C),
    Bad = lists:usort([T || #{target := T, expr := E} <- Rules, is_list(T),
			    maps:is_key(T, Sigs),
			    not storable(E, maps:get(T, Sigs), C)]),
    C#{written => Written, free => Bad,
       skipped => [R || R = #{target := T} <- Rules, T =/= "State",
			(not is_list(T)) orelse
			    (not maps:is_key(T, Sigs)) orelse
			    (not storable(maps:get(expr, R),
					  maps:get(T, Sigs), C))]}.

%% A value can be stored when it translates and wraps into the target.
storable(E, Sig, C) ->
    case rng(E, C) of
	{ok, R} -> wrappable(R, Sig);
	error   -> false
    end.

%%% Replace every guard we cannot translate -- timeout(), a part, a call -- with
%%% a fresh free input. The transition then becomes POSSIBLE rather than
%%% determined, which is the standard abstraction and keeps a proof sound: the
%%% model can do everything the program can, and more.
abstract_guards(C) ->
    {Rules, Abs} =
	lists:mapfoldl(
	  fun(R = #{guard := G, line := Ln, file := F}, Acc) ->
		  case (G =:= undefined) orelse translatable(G, C) of
		      true  -> {R, Acc};
		      false ->
			  Name = "u_free" ++ integer_to_list(length(Acc)) ++
			      loc_id(Ln, F),
			  {R#{guard := {abstracted, Name, G}},
			   Acc ++ [{Name, Ln}]}
		  end
	  end, [], maps:get(rules, C)),
    C#{rules := Rules, abstract => Abs}.


translatable(undefined, _C) -> true;
translatable(E, C) -> rng(E, C) =/= error.

%%% ------------------------------------------------------- value intervals
%%%
%%% {ok, {Lo, Hi}}: every value the expression can have, from the declared
%%% widths -- or `error' when it cannot be translated at all. One walk answers
%%% both, so "translatable" and "has a known range" cannot drift apart.

-define(IS_CMP(Op), (Op =:= '==' orelse Op =:= '!=' orelse Op =:= '<' orelse
		     Op =:= '<=' orelse Op =:= '>' orelse Op =:= '>=')).
-define(IS_LOGIC(Op), (Op =:= '&&' orelse Op =:= '||')).
-define(IS_BITS(Op), (Op =:= '&' orelse Op =:= '|' orelse Op =:= '^')).
-define(U32, 4294967296).

rng({abstracted, _N, _G}, _C) -> {ok, {0, 1}};
rng({'INT', _, S}, _C) ->
    case num_of(S) of
	{ok, V} -> {ok, {V, V}};
	none    -> error
    end;
rng({field, _, {'WORD', _, N}}, C) ->
    Sigs = maps:get(sigs, C),
    Consts = maps:get(consts, C),
    Locals = maps:get(locals, C),
    if
	is_map_key(N, Sigs)   -> {ok, sig_range(maps:get(N, Sigs))};
	is_map_key(N, Consts) -> V = maps:get(N, Consts), {ok, {V, V}};
	is_map_key(N, Locals) -> local_rng(N, C);
	true ->
	    case lists:member(N, maps:get(states, C)) orelse
		is_builtin_state(N) orelse N =:= "State" of
		true  -> {ok, {0, (1 bsl state_bits(C)) - 1}};
		false -> error
	    end
    end;
rng({field, _, {index, _, {'WORD', _, B}, Ix}}, C) ->
    case is_map_key(B, maps:get(bufs, C)) andalso rng(Ix, C) of
	{ok, _} -> {ok, {0, 255}};
	_       -> error
    end;
rng({field, _, {part, _, {'WORD', _, B}, {'WORD', _, "rx"}}}, C) ->
    case is_map_key(B, maps:get(bufs, C)) of
	true  -> {ok, {0, 1}};
	false -> error
    end;
rng({'!', _, M}, C) ->
    both(rng(M, C), {ok, {0, 0}}, fun(_, _) -> {0, 1} end);
rng({'-', _, M}, C) ->
    both(rng(M, C), {ok, {0, 0}}, fun({Lo, Hi}, _) -> {-Hi, -Lo} end);
rng({Op, _, L, R}, C) when ?IS_CMP(Op); ?IS_LOGIC(Op) ->
    both(rng(L, C), rng(R, C), fun(_, _) -> {0, 1} end);
rng({'+', _, L, R}, C) ->
    both(rng(L, C), rng(R, C), fun({A, B}, {X, Y}) -> {A + X, B + Y} end);
rng({'-', _, L, R}, C) ->
    both(rng(L, C), rng(R, C), fun({A, B}, {X, Y}) -> {A - Y, B - X} end);
rng({'*', _, L, R}, C) ->
    both(rng(L, C), rng(R, C),
	 fun({A, B}, {X, Y}) ->
		 Ps = [A * X, A * Y, B * X, B * Y],
		 {lists:min(Ps), lists:max(Ps)}
	 end);
%% varp's `%' takes unsigned operands only (a signed one is a function_clause
%% in varp_arith:reminder), and C's sign rules would have to be spelled out --
%% so both sides must be non-negative, and the divisor never zero.
rng({'%', _, L, R}, C) ->
    case {rng(L, C), rng(R, C)} of
	{{ok, {A, B}}, {ok, {X, Y}}} when A >= 0, X > 0 ->
	    case vsigned(L, C) orelse vsigned(R, C) of
		true  -> error;
		false -> {ok, {0, min(B, Y - 1)}}
	    end;
	_ -> error
    end;
%% Bitwise on 32 bit two's complement, as the runtime computes it. vbits/5
%% makes a possibly negative operand unsigned first, which needs it in range.
rng({Op, _, L, R}, C) when ?IS_BITS(Op) ->
    case {rng(L, C), rng(R, C)} of
	{{ok, {A, B} = RL}, {ok, {X, Y} = RR}}
	  when A >= -(?U32 div 2), X >= -(?U32 div 2), B < ?U32, Y < ?U32 ->
	    {ok, bits_range(Op, RL, RR)};
	_ -> error
    end;
rng(_Other, _C) ->
    error.                            % calls, parts, arrays, ranges, floats

both({ok, A}, {ok, B}, F) -> {ok, F(A, B)};
both(_, _, _)             -> error.

bits_range(Op, {A, B}, {X, Y}) when A >= 0, X >= 0 ->
    case Op of
	'&' -> {0, min(B, Y)};
	_   -> {0, (1 bsl bit_len(max(B, Y))) - 1}
    end;
bits_range('&', {A, B}, _) when A >= 0 -> {0, B};
bits_range('&', _, {X, Y}) when X >= 0 -> {0, Y};
bits_range(_Op, _, _) -> {-(?U32 div 2), ?U32 div 2 - 1}.

bit_len(0) -> 0;
bit_len(N) -> 1 + bit_len(N bsr 1).

sig_range(#{width := W, signed := true})  -> {-(1 bsl (W - 1)), (1 bsl (W - 1)) - 1};
sig_range(#{width := W, signed := false}) -> {0, (1 bsl W) - 1}.

%% A local reads as what was STORED, so its range is its declared one unless
%% the formula fits inside that. A local that reads itself (it gets its own
%% slot, so it means last cycle's value) is not a formula and not translated.
local_rng(N, C) ->
    Seen = maps:get(seen, C, []),
    case lists:member(N, Seen) of
	true -> error;
	false ->
	    L = maps:get(N, maps:get(locals, C)),
	    case rng(maps:get(expr, L), C#{seen => [N | Seen]}) of
		{ok, R} ->
		    case {fits(R, L), wrappable(R, L)} of
			{true, _}  -> {ok, R};
			{_, true}  -> {ok, sig_range(L)};
			{_, false} -> error
		    end;
		error -> error
	    end
    end.

fits({Lo, Hi}, Sig) ->
    {TLo, THi} = sig_range(Sig),
    Lo >= TLo andalso Hi =< THi.

%% One wrap at most in either direction: that is every `N + 1', `A - B' of the
%% target's width and the like, and anything further is not translated.
wrappable({Lo, Hi}, Sig = #{width := W}) ->
    {TLo, THi} = sig_range(Sig),
    Lo >= TLo - (1 bsl W) andalso Hi =< THi + (1 bsl W).

%% Is the expression SIGNED in varp's typing? A subtraction or a signed vector
%% anywhere in it makes it so, whatever its value -- and varp's `%' refuses
%% that.
vsigned({'INT', _, _}, _C) -> false;
vsigned({field, _, {'WORD', _, N}}, C) ->
    case maps:find(N, maps:get(sigs, C)) of
	{ok, #{signed := S}} -> S;
	error ->
	    case maps:find(N, maps:get(locals, C)) of
		{ok, L} -> maps:get(signed, L) orelse
			       vsigned(maps:get(expr, L), C);
		error -> false
	    end
    end;
vsigned({field, _, _}, _C) -> false;
vsigned({'-', _, _}, _C) -> true;
vsigned({'-', _, _, _}, _C) -> true;
vsigned({Op, _, _, _}, _C) when ?IS_CMP(Op); ?IS_LOGIC(Op) -> false;
vsigned({_Op, _, L, R}, C) -> vsigned(L, C) orelse vsigned(R, C);
vsigned(_, _C) -> false.

is_builtin_state(N) -> lists:member(N, ["INIT", "NORMAL", "FAILSAFE"]).

%%% ------------------------------------------------------------------- emit

emit(In, C, _Opts) ->
    Name = varp_name(filename:basename(filename:rootname(In))),
    [header(In, C),
     consts(C),
     "\nsystem ", Name, " {\n",
     decls(C),
     local_defs(C),
     "\n", init_item(C),
     "\n", transitions(C),
     "\n    // FAILSAFE is absorbing in the runtime (csp.h: no rule may leave\n"
     "    // it, only a reset), so reaching it once is reaching it for ever --\n"
     "    // this is a safety property in the strict sense.\n"
     "    invariant St != FAILSAFE;\n",
     "}\n",
     footer(C)].

%% varp keywords are not available as a system name, and examples/state.csp is
%% exactly that case -- `system state {' does not parse.
varp_name(S) ->
    Clean = [case C of $- -> $_; $. -> $_; _ -> C end || C <- S],
    case lists:member(Clean, keywords()) of
	true  -> "m_" ++ Clean;
	false -> Clean
    end.

keywords() ->
    ["system", "state", "input", "init", "next", "invariant", "reach",
     "eventually", "assume", "send", "recv", "channel", "instance",
     "declare", "define", "circuit", "import", "in", "out", "return",
     "not", "and", "or", "xor", "implies", "imp", "equ", "min", "max",
     "St"].                            % ours: the state variable

header(In, C) ->
    ["// Generated from ", filename:basename(In), " by candyspeak_varp.\n"
     "// DO NOT EDIT -- regenerate.\n"
     "//\n"
     "//     varp bmc --induction <this file>     // prove it\n"
     "//     varp bmc <this file>                 // or get a trace\n"
     "//\n"
     "// FIRST check the model has transitions at all. An inconsistent `next'\n"
     "// has none, so everything is unreachable and a proof says nothing:\n"
     "//\n"
     "//     // drop the `invariant' line, append this, run `varp sat bj'\n"
     "//     ", varp_name(filename:basename(filename:rootname(In))),
     "_init(0) and ", varp_name(filename:basename(filename:rootname(In))),
     "_next(1)\n"
     "//\n"
     "// must answer `% 1'.\n//\n",
     free_note(C),
     "// States: ", state_note(C), "\n"].

free_note(C) ->
    case maps:get(free, C) of
	[] -> "";
	Fs -> ["// FREE (input, not state): ", lists:join(", ", Fs), "\n"
	       "// Each is written by a rule this translator cannot model --\n"
	       "// a timer, a part, a call. Left free the model has MORE\n"
	       "// behaviour than the program, so a PROOF still holds while a\n"
	       "// counterexample may be spurious. Read one against the source.\n"]
    end.

state_note(C) ->
    lists:join(", ",
	       ["INIT=0", "NORMAL=1", "FAILSAFE=2" |
		[[N, "=", integer_to_list(I)]
		 || {N, I} <- number_states(C)]]).

%%% The runtime numbers user states from 3, and it SKIPS a reserved name in the
%%% list: `#states FAILSAFE A B' gives A 3 and B 4, because FAILSAFE resolves to
%%% the built-in 2 and takes no slot. Numbering them here as written shifted
%%% every state by one and emitted `define FAILSAFE' twice -- a model of a
%%% different program than the one that runs, with nothing to show it.
%%% A duplicate is dropped for the same reason: the runtime keeps the first.
number_states(C) ->
    Names = user_states(C),
    lists:zip(Names,
	      lists:seq(?FIRST_USER_STATE,
			?FIRST_USER_STATE + length(Names) - 1)).

user_states(C) ->
    lists:foldl(fun(N, Acc) ->
			case is_builtin_state(N) orelse lists:member(N, Acc) of
			    true  -> Acc;
			    false -> Acc ++ [N]
			end
		end, [], maps:get(states, C)).

%%% The state numbers get names, so a gate reads `St == Home or St == Night'
%%% instead of `St == 3 or St == 4'. A state whose name is already taken -- by a
%%% signal, a #param or a varp keyword -- keeps its number, because defining it
%%% twice would either fail to parse or silently shadow the other one.
consts(C) ->
    ["\ndefine INIT ", integer_to_list(0), ";\n",
     "define NORMAL ", integer_to_list(1), ";\n",
     "define FAILSAFE ", integer_to_list(?FAILSAFE), ";\n",
     [["define ", N, " ", integer_to_list(V), ";\n"]
      || {N, V} <- number_states(C), state_has_name(N, C)],
     [["define ", N, " ", integer_to_list(V), ";\n"]
      || {N, V} <- lists:sort(maps:to_list(maps:get(consts, C)))]].

%% A state may be named only when nothing else claims the name.
state_has_name(N, C) ->
    (not maps:is_key(N, maps:get(sigs, C))) andalso
	(not maps:is_key(N, maps:get(consts, C))) andalso
	(not lists:member(N, keywords())) andalso
	varp_name(N) =:= N.

%% How to write a state in a formula: its name when it has one, else its number.
state_text(N, C) ->
    case is_builtin_state(N) of
	true -> N;                      % INIT, NORMAL, FAILSAFE are defined above
	false ->
	    case lists:keyfind(N, 1, number_states(C)) of
		{N, V} ->
		    case state_has_name(N, C) of
			true  -> N;
			false -> integer_to_list(V)
		    end;
		false -> "0"
	    end
    end.

%%% A signal is `state' when a rule writes it and every such rule could be
%%% translated; `input' when it is a sensor, or when it is written by a rule we
%%% had to give up on.
decls(C) ->
    Sigs = maps:get(sigs, C),
    Written = maps:get(written, C),
    Free = maps:get(free, C),
    Frozen = frozen(C),
    States = [N || N <- maps:get(order, C),
		   lists:member(N, Written), not lists:member(N, Free)],
    Inputs = [N || N <- maps:get(order, C), not lists:member(N, Frozen),
		   (not lists:member(N, Written)) orelse lists:member(N, Free)],
    Abs = [N || {N, _Ln} <- maps:get(abstract, C, [])],
    ["    state St:", integer_to_list(state_bits(C)), ";\n",
     group("state", States, Sigs),
     case Frozen of
	 [] -> "";
	 _  -> ["    // #param: free in the first step, then never changes\n",
		group("state", Frozen, Sigs)]
     end,
     group("input", Inputs, Sigs),
     [["    // #buffer ", B, ": any bytes, and a frame or not, in every step\n"
       "    input ",
       lists:join(", ", [[byte_name(B, K), ":8"]
			 || K <- lists:seq(0, Size - 1)]),
       ", ", varp_name(B ++ "_rx"), ":1;\n"]
      || {B, Size} <- lists:sort(maps:to_list(maps:get(bufs, C)))],
     case Abs of
	 [] -> "";
	 _  -> ["    // one per guard that could not be translated: free, so\n"
		"    // the rule MAY fire. See the note at the top.\n"
		"    input ",
		lists:join(", ", [[N, ":1"] || N <- Abs]), ";\n"]
     end].

group(_Kw, [], _Sigs) -> "";
group(Kw, Names, Sigs) ->
    ["    ", Kw, " ",
      lists:join(", ", [[varp_name(N), ":", decl_type(maps:get(N, Sigs))]
		       || N <- Names]),
     ";\n"].

decl_type(#{width := W, signed := true})  -> [integer_to_list(W), "/signed"];
decl_type(#{width := W, signed := false}) -> integer_to_list(W).

%% The #params no rule writes. One a rule does write is an ordinary variable.
frozen(C) ->
    Sigs = maps:get(sigs, C),
    [N || N <- maps:get(order, C), maps:get(kind, maps:get(N, Sigs)) =:= param,
	  not lists:member(N, maps:get(written, C))].

state_bits(C) ->
    bits(?FIRST_USER_STATE + length(maps:get(states, C))).

bits(N) when N < 2 -> 1;
bits(N) -> trunc(math:ceil(math:log2(N + 1))).

%%% A #local is a macro of the system, so a formula read by six rules is written
%%% once -- and `HighBeam()' in a rule reads as the source does. A macro that
%%% calls another must come after it, so they are emitted callee first.
local_defs(C) ->
    Locals = maps:get(locals, C),
    Ok = [N || N <- maps:get(lorder, C), rng(field(N), C) =/= error],
    Order = lists:foldl(fun(N, Acc) -> local_dfs(N, Ok, C, Acc) end, [], Ok),
    case Order of
	[] -> "";
	_ ->
	    ["\n    // #local: inlined where it is read, in the same cycle\n",
	     [begin
		  L = maps:get(N, Locals),
		  E = maps:get(expr, L),
		  Body = case is_bool(E, C) of
			     true  -> vbool(E, C);
			     false -> stored(E, L, C)
			 end,
		  ["    define ", varp_name(N), "() ", paren(Body), ";\n"]
	      end || N <- Order]]
    end.

local_dfs(N, Ok, C, Acc) ->
    case lists:member(N, Acc) of
	true -> Acc;
	false ->
	    E = maps:get(expr, maps:get(N, maps:get(locals, C))),
	    Deps = [D || D <- names_in(E), lists:member(D, Ok), D =/= N],
	    lists:foldl(fun(D, A) -> local_dfs(D, Ok, C, A) end, Acc, Deps)
		++ [N]
    end.

field(N) -> {field, 0, {'WORD', 0, N}}.

names_in({field, _, {'WORD', _, N}}) -> [N];
names_in({field, _, {index, _, _, Ix}}) -> names_in(Ix);
names_in(T) when is_tuple(T) ->
    lists:append([names_in(X) || X <- tl(tuple_to_list(T))]);
names_in(L) when is_list(L) -> lists:append([names_in(X) || X <- L]);
names_in(_) -> [].

%%% The house starts in INIT with every signal at its declared value.
init_item(C) ->
    Sigs = maps:get(sigs, C),
    Written = maps:get(written, C),
    Free = maps:get(free, C),
    Vs = [N || N <- maps:get(order, C),
	       lists:member(N, Written), not lists:member(N, Free)],
    ["    init  St == INIT",
     [[" and ", varp_name(N), " == ",
       integer_to_list(maps:get(init, maps:get(N, Sigs)))]
      || N <- Vs],
     ";\n"].

%%% ------------------------------------------------------- the next relation

transitions(C) ->
    Sigs = maps:get(sigs, C),
    Written = maps:get(written, C),
    Free = maps:get(free, C),
    Vs = [N || N <- maps:get(order, C),
	       lists:member(N, Written), not lists:member(N, Free)],
    [state_chain(C), [chain(V, C, Sigs) || V <- Vs],
     case frozen(C) of
	 [] -> "";
	 Ps -> ["    // ---- #param ", lists:duplicate(43, $-), "\n",
		[["    next  next(", varp_name(P), ") == ", varp_name(P), ";\n"]
		 || P <- Ps]]
     end].

%%% St has two implicit rules around the explicit ones:
%%%
%%%   FAILSAFE is sticky, at the TOP -- that is a runtime guarantee, not a rule
%%%   in the program, and nothing may override it.
%%%
%%%   INIT falls through to NORMAL at the BOTTOM, so an explicit `State = Home'
%%%   in #in INIT wins over it.
state_chain(C) ->
    Rules = [R || R = #{target := "State"} <- maps:get(rules, C)],
    Guards = [full_guard(R, C) || R <- Rules],
    Exprs  = [["next(St) == ", state_value(R, C)] || R <- Rules],
    ["    // ---- State ",
     lists:duplicate(50, $-), "\n",
     "    next  (St == FAILSAFE) implies (next(St) == FAILSAFE);\n",
     priority("St", lists:reverse(Guards), lists:reverse(Exprs),
	      [{"St == INIT", "next(St) == NORMAL"},  % INIT falls through
	       {none,         "next(St) == St"}],      % otherwise stay put
	      "(St != FAILSAFE)"),
     "\n"].

%% `State = Home' assigns a state NAME, not a number.
state_value(#{expr := {field, _, {'WORD', _, N}}}, C) ->
    state_text(N, C);
state_value(#{expr := E}, C) ->
    vexpr(E, C, num).

%%% One variable's chain. Rules are reversed because the LAST rule in the file
%%% wins, and every earlier branch negates the guards of the later ones. The
%%% final branch holds the value, which is what makes the chain total.
chain(V, C, Sigs) ->
    Rules = [R || R = #{target := T} <- maps:get(rules, C), T =:= V],
    Guards = [full_guard(R, C) || R <- Rules],
    Sig = maps:get(V, Sigs),
    W = maps:get(width, Sig),
    Exprs = [["next(", varp_name(V), ") == ", stored(maps:get(expr, R), Sig, C)]
	     || R <- Rules],
    ["    // ---- ", V, ":", integer_to_list(W), " ",
     lists:duplicate(max(1, 46 - length(V)), $-), "\n",
     [["    //   ", src_of(R, C), "\n"] || R <- Rules],
     priority(V, lists:reverse(Guards), lists:reverse(Exprs),
	      [{none, ["next(", varp_name(V), ") == ", varp_name(V)]}], none),
     "\n"].

stored(E, Sig, C) ->
    {ok, R} = rng(E, C),
    wrap(vexpr(E, C, num), R, Sig).

%% Emit the chain. `Fallbacks' are lowest-priority {guard, expr} branches, tried
%% in order after every rule has missed, and they must between them cover
%% everything -- the last one has no guard. `Pre' is an extra conjunct on every
%% branch (used to keep St out of FAILSAFE, which is handled above the chain).
priority(_V, Guards, Exprs, Fallbacks, Pre) ->
    Items = zip_branches(Guards, Exprs, [], Pre),
    Missed = negate_all(Guards, Pre),
    [Items, fallbacks(Fallbacks, Missed, [])].

fallbacks([], _Missed, _Seen) -> [];
fallbacks([{G, E} | T], Missed, Seen) ->
    Cond = conj([Missed] ++ [["not ", paren(S)] || S <- Seen] ++
		    case G of none -> []; _ -> [paren(G)] end),
    [["    next  ", paren(Cond), " implies (", E, ");\n"]
     | fallbacks(T, Missed, Seen ++ case G of none -> []; _ -> [G] end)].

zip_branches([], [], _Seen, _Pre) -> [];
zip_branches([G | Gs], [E | Es], Seen, Pre) ->
    Cond = conj([pre(Pre)] ++ [["not ", paren(S)] || S <- Seen] ++ [paren(G)]),
    [["    next  ", paren(Cond), " implies (", E, ");\n"]
     | zip_branches(Gs, Es, Seen ++ [G], Pre)].

negate_all(Guards, Pre) ->
    conj([pre(Pre)] ++ [["not ", paren(G)] || G <- Guards]).

pre(none) -> [];
pre(S)    -> S.

conj(Parts) ->
    case [P || P <- Parts, P =/= []] of
	[]  -> "1 == 1";
	Ps  -> lists:join(" and ", Ps)
    end.

paren(X) -> ["(", X, ")"].

%%% The gate and the guard together. A rule inside `#in A B' runs only in those
%%% states; a LOOSE rule -- no #in at all -- runs only in INIT and NORMAL, which
%%% is the trap that bit home.csp three times and is why this is explicit.
full_guard(#{states := Gate, guard := G}, C) ->
    %% paren/1 on the gate is NOT cosmetic. `and' binds tighter than `or', so
    %% an unbracketed "St == 3 or St == 4 or St == 5" conjoined with a guard
    %% parses as "St == 3 or St == 4 or (St == 5 and guard)" -- true whenever
    %% the house is in the first state, whatever the guard says. That generated
    %% a counterexample out of thin air.
    conj([paren(gate_expr(Gate, C))] ++ guard_part(G, C)).

guard_part(undefined, _C) -> [];
guard_part(G, C)          -> [paren(vexpr(G, C, bool))].

gate_expr(all, _C) ->
    "St == INIT or St == NORMAL";           % loose: those two states only
gate_expr(Names, C) ->
    lists:join(" or ", [["St == ", state_num(N, C)] || N <- Names]).

state_num(N, C) -> state_text(N, C).

%%% ------------------------------------------------------------ expressions

%%% `num' yields a value, `bool' a condition, and varp keeps the two apart: a
%%% vector used as a condition is `X != 0' (C truthiness -- `X == 1' was wrong
%%% for anything wider than a bit), and a condition used as a value is
%%% `(c ? 1 : 0)'.
vexpr(E, C, bool) ->
    case is_bool(E, C) of
	true  -> vbool(E, C);
	false -> [paren(vnum(E, C)), " != 0"]
    end;
vexpr(E, C, num) ->
    case is_bool(E, C) of
	true  -> ["(", paren(vbool(E, C)), " ? 1 : 0)"];
	false -> vnum(E, C)
    end.

is_bool({abstracted, _, _}, _C) -> true;
is_bool({'!', _, _}, _C) -> true;
is_bool({Op, _, _, _}, _C) when ?IS_CMP(Op); ?IS_LOGIC(Op) -> true;
is_bool({field, _, {'WORD', _, N}}, C) ->
    case maps:find(N, maps:get(locals, C)) of
	{ok, L} -> is_bool(maps:get(expr, L), C);
	error   -> false
    end;
is_bool(_, _C) -> false.

vbool({abstracted, Name, _Orig}, _C) -> [Name, " == 1"];
vbool({'!', _, M}, C) -> ["not ", paren(vexpr(M, C, bool))];
vbool({Op, _, L, R}, C) when ?IS_LOGIC(Op) ->
    [paren(vexpr(L, C, bool)), op(Op), paren(vexpr(R, C, bool))];
vbool({Op, _, L, R}, C) when ?IS_CMP(Op) ->
    [paren(vexpr(L, C, num)), op(Op), paren(vexpr(R, C, num))];
vbool({field, _, {'WORD', _, N}}, _C) ->         % a boolean local
    [varp_name(N), "()"].

vnum({'INT', _, S}, _C) -> S;
vnum({field, _, {'WORD', _, N}}, C) ->
    case is_map_key(N, maps:get(locals, C)) of
	true -> [varp_name(N), "()"];           % a macro, see local_defs/1
	false ->
	    case is_builtin_state(N) orelse
		lists:keymember(N, 1, number_states(C)) of
		true  -> state_text(N, C);
		false -> varp_name(N)         % a signal or a define
	    end
    end;
%% A byte at a constant index is a name; at a variable one, a choice among the
%% bytes it can reach. Out of range reads byte 0, as the runtime does.
vnum({field, _, {index, _, {'WORD', _, B}, Ix}}, C) ->
    Size = maps:get(B, maps:get(bufs, C)),
    {ok, {Lo, Hi}} = rng(Ix, C),
    case Lo =:= Hi of
	true when Lo >= 0, Lo < Size -> byte_name(B, Lo);
	true -> byte_name(B, 0);
	false ->
	    I = paren(vnum(Ix, C)),
	    lists:foldr(fun(K, Else) ->
				["(", I, " == ", integer_to_list(K), " ? ",
				 byte_name(B, K), " : ", Else, ")"]
			end, byte_name(B, 0),
			lists:seq(max(1, Lo), min(Size - 1, Hi)))
    end;
vnum({field, _, {part, _, {'WORD', _, B}, {'WORD', _, "rx"}}}, _C) ->
    varp_name(B ++ "_rx");
vnum({'-', _, M}, C) -> ["-", paren(vexpr(M, C, num))];
vnum({Op, _, L, R}, C) when ?IS_BITS(Op) ->
    vbits(Op, L, R, C);
vnum({Op, _, L, R}, C) ->
    [paren(vexpr(L, C, num)), op(Op), paren(vexpr(R, C, num))].

byte_name(B, K) -> varp_name(B ++ "_b" ++ integer_to_list(K)).

%% varp's bitwise operators are right on non-negative VALUES only (-1 & 0xFFFF
%% is -1 there), so an operand that can be negative is first made its 32 bit
%% unsigned self, and the result read back as a signed 32 bit word.
vbits(Op, L, R, C) ->
    {ok, RL} = rng(L, C),
    {ok, RR} = rng(R, C),
    T = [paren(u32(vexpr(L, C, num), RL)), op(Op),
	 paren(u32(vexpr(R, C, num), RR))],
    case bits_range(Op, RL, RR) of
	{Lo, _} when Lo >= 0 -> T;
	_ -> ["((", T, ") >= 2147483648 ? (", T, ") - 4294967296 : (", T, "))"]
    end.

u32(T, {Lo, _}) when Lo >= 0 -> T;
u32(T, _) -> ["((", T, ") < 0 ? (", T, ") + 4294967296 : (", T, "))"].

%% Fold a value into a stored width: at most one wrap each way (wrappable/2
%% has said so), so two comparisons cover it.
wrap(T, {Lo, Hi} = R, Sig = #{width := W}) ->
    {TLo, THi} = sig_range(Sig),
    case fits(R, Sig) of
	true -> T;
	false ->
	    M = integer_to_list(1 bsl W),
	    Inner = case Lo < TLo of
			true  -> ["((", T, ") < ", integer_to_list(TLo),
				  " ? (", T, ") + ", M, " : (", T, "))"];
			false -> T
		    end,
	    case Hi > THi of
		true  -> ["((", T, ") > ", integer_to_list(THi),
			  " ? (", T, ") - ", M, " : ", Inner, ")"];
		false -> Inner
	    end
    end.

op('&&') -> " and ";
op('||') -> " or ";
op(Op)   -> [" ", atom_to_list(Op), " "].

%%% The source line, as a comment above each chain, so the model can be read
%%% against the program without holding both files open.
src_of(#{target := T, expr := E, guard := G, line := Ln, file := F}, C) ->
    [loc(Ln, F), ": ", T, " = ",
     vexpr(E, C, num),
     case G of
	 undefined -> "";
	 {abstracted, N, _} -> [" ? <untranslatable, free as ", N, ">"];
	 _ -> [" ? ", vexpr(G, C, bool)]
     end].

footer(C) ->
    case maps:get(skipped, C) of
	[] -> "";
	Rs -> ["\n// NOT TRANSLATED (", integer_to_list(length(Rs)),
	       " rules), because of a timer, a part, a call or an array:\n",
	       [["//   line ", loc(maps:get(line, R), maps:get(file, R)), "\n"]
		|| R <- Rs]]
    end.

%%% -------------------------------------------------------------- self check
%%%
%%% Generate for every example and ask varp two questions about each: does it
%%% PARSE, and does the transition relation have any transitions at all. The
%%% second is the one that matters -- a generator that emits an inconsistent
%%% `next' makes every property provable and every question answerable with
%%% "impossible", which looks authoritative and is worthless.
%%%
%%%     candyspeak_varp:check("../examples/*.csp", "/path/to/varp.sh").

check(Wildcard, VarpSh) ->
    Files = filelib:wildcard(Wildcard),
    R = [{F, check_one(F, VarpSh)} || F <- Files],
    Ok = [F || {F, ok} <- R],
    Bad = [{F, W} || {F, W} <- R, W =/= ok],
    io:format("~w files: ~w consistent, ~w not~n",
	      [length(R), length(Ok), length(Bad)]),
    [io:format("  ~-28s ~p~n", [filename:basename(F), W]) || {F, W} <- Bad],
    {length(Ok), Bad}.

check_one(File, VarpSh) ->
    Tmp = "/tmp/csp_varp_check.varp",
    case translate(File) of
	{ok, Io} ->
	    ok = file:write_file(Tmp, Io),
	    %% `bmc --runs' unrolls the transition relation and ignores the
	    %% invariant, so the file is asked the question as generated. This
	    %% used to strip the property with string surgery and write a second
	    %% file, which broke whenever the line's formatting changed.
	    Out = os:cmd(VarpSh ++ " bmc --runs --k-min 2 --k-max 2"
			 " --trace false " ++ Tmp ++ " 2>&1 | tail -1"),
	    case string:find(Out, "% 1") of
		nomatch -> {no_transitions, string:trim(Out)};
		_       -> ok
	    end;
	Error -> {translate_failed, Error}
    end.
