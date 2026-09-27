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
%%%     WHAT CANNOT BE MODELLED, and what is done about it
%%%
%%%     Timers, part access (`T.period'), arrays and the arithmetic functions are
%%%     not translated. A variable written by any such rule is emitted as `input'
%%%     instead of `state': FREE. That is deliberately conservative -- the model
%%%     then has MORE behaviour than the program, so a proof still holds, while a
%%%     counterexample may be spurious and has to be read against the source.
%%%     The alternative, dropping the rule, would make the model have LESS
%%%     behaviour, and then a proof would mean nothing.
%%% @end

-module(candyspeak_varp).

-export([file/1, file/2]).
-export([translate/1, translate/2]).
-export([collect/1, check/2]).         % for tests

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

translate(In, Opts) ->
    case candyspeak:parse(In) of
	{ok, Ast} ->
	    case modules_in(Ast) of
		[] -> {ok, emit(In, collect(Ast), Opts)};
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
    C = #{sigs => #{}, order => [], states => [], rules => [], gate => all,
	  consts => #{}},
    finish(lists:foldl(fun item/2, C, Ast)).

item({states, _Ln, Names}, C) ->
    C#{states := maps:get(states, C) ++ [N || {'WORD', _, N} <- Names]};

item({in, _Ln, Names}, C) ->
    C#{gate := [N || {'WORD', _, N} <- Names]};

item({'end', _Ln}, C) ->
    C#{gate := all};

%% A #param or #constant is a compile-time number: a varp `define'. Note the
%% arity -- param/constant carry no `scalar' slot, unlike the signals below.
item({Kind, _Ln, {'WORD', _, Name}, _Res, _Opts, Expr}, C)
  when Kind =:= param; Kind =:= constant ->
    case const_of(Expr) of
	{ok, V} -> C#{consts := maps:put(Name, V, maps:get(consts, C))};
	none    -> C
    end;

item({Kind, _Ln, {'WORD', _, Name}, scalar, Res, Opts, Expr}, C)
  when Kind =:= digital; Kind =:= analog; Kind =:= variable ->
    Sig = #{kind  => Kind,
	    width => width_of(Kind, Res),
	    dir   => proplists:get_value(dir, Opts, undefined),
	    init  => case const_of(Expr) of {ok, V} -> V; none -> 0 end},
    C#{sigs  := maps:put(Name, Sig, maps:get(sigs, C)),
       order := maps:get(order, C) ++ [Name]};

%% Anything else declared -- timers, buffers, fields, arrays, modules -- is not
%% modelled. Rules that mention them are caught by unmodellable/1 below.
item({rule, Ln, Assigns, Guard}, C) ->
    lists:foldl(fun(A, Ci) -> assign(A, Ln, Guard, Ci) end, C, Assigns);

item(_Other, C) ->
    C.

assign({'=', _Ln, Target, Expr}, Ln, Guard, C) ->
    R = #{target => target_name(Target),
	  guard  => Guard,
	  expr   => Expr,
	  states => maps:get(gate, C),
	  line   => Ln},
    C#{rules := maps:get(rules, C) ++ [R]};
assign(_Other, _Ln, _Guard, C) ->
    C.

%% `X' or `X.part' -- the latter is not modellable, and is kept as a tagged name
%% so the rule can be recognised and its target set free.
target_name({field, _, {'WORD', _, Name}})               -> Name;
target_name({field, _, {part, _, {'WORD', _, N}, _P}})    -> {part, N};
target_name(Other)                                        -> {other, Other}.

const_of({'INT', _, S}) -> num_of(S);
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

%% #digital is one bit; #analog and #variable carry a declared width, and a
%% variable without one gets 16 -- wide enough for what these programs hold and
%% narrow enough to stay cheap in CNF.
width_of(digital, _)              -> 1;
width_of(_, {'INT', _, S})        -> list_to_integer(S);
width_of(_, _)                    -> 16.

%%% After the walk: work out which signals are WRITTEN, and which of those can
%%% be modelled at all.
finish(C0) ->
    C = abstract_guards(C0),
    Rules = maps:get(rules, C),
    Written = lists:usort([T || #{target := T} <- Rules, is_list(T)]),
    %% Only an untranslatable VALUE forces a target free -- we cannot say what
    %% it would be assigned. An untranslatable GUARD has been abstracted to a
    %% free boolean above, so the rule may or may not fire: more behaviour than
    %% the program has, which is the safe direction.
    Bad = lists:usort([T || #{target := T, expr := E} <- Rules, is_list(T),
			    not translatable(E, C)]),
    C#{written => Written, free => Bad,
       skipped => [R || R = #{target := T} <- Rules,
			(not is_list(T)) orelse
			    (not translatable(maps:get(expr, R), C))]}.

%%% Replace every guard we cannot translate -- timeout(), a part, a call -- with
%%% a fresh free input. The transition then becomes POSSIBLE rather than
%%% determined, which is the standard abstraction and keeps a proof sound: the
%%% model can do everything the program can, and more.
abstract_guards(C) ->
    {Rules, Abs} =
	lists:mapfoldl(
	  fun(R = #{guard := G, line := Ln}, Acc) ->
		  case (G =:= undefined) orelse translatable(G, C) of
		      true  -> {R, Acc};
		      false ->
			  Name = "u_free" ++ integer_to_list(length(Acc)) ++
			      "_L" ++ integer_to_list(Ln),
			  {R#{guard := {abstracted, Name, G}},
			   Acc ++ [{Name, Ln}]}
		  end
	  end, [], maps:get(rules, C)),
    C#{rules := Rules, abstract => Abs}.


translatable(undefined, _C) -> true;
translatable({abstracted, _N, _G}, _C) -> true;   % by construction
translatable({'INT', _, _}, _C) -> true;
translatable({field, _, {'WORD', _, N}}, C) ->
    maps:is_key(N, maps:get(sigs, C)) orelse
	maps:is_key(N, maps:get(consts, C)) orelse
	lists:member(N, maps:get(states, C)) orelse
	is_builtin_state(N) orelse N =:= "State";
translatable({Op, _, M}, C) when is_atom(Op) ->
    lists:member(Op, ['!', '-']) andalso translatable(M, C);
translatable({Op, _, L, R}, C) when is_atom(Op) ->
    lists:member(Op, ['&&', '||', '==', '!=', '<', '<=', '>', '>=',
		      '+', '-', '*']) andalso
	translatable(L, C) andalso translatable(R, C);
translatable(_Other, _C) ->
    false.                            % calls, parts, arrays, ranges, floats

is_builtin_state(N) -> lists:member(N, ["INIT", "NORMAL", "FAILSAFE"]).

%%% ------------------------------------------------------------------- emit

emit(In, C, _Opts) ->
    Name = varp_name(filename:basename(filename:rootname(In))),
    [header(In, C),
     consts(C),
     "\nsystem ", Name, " {\n",
     decls(C),
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
    States = [N || N <- maps:get(order, C),
		   lists:member(N, Written), not lists:member(N, Free)],
    Inputs = [N || N <- maps:get(order, C),
		   (not lists:member(N, Written)) orelse lists:member(N, Free)],
    Abs = [N || {N, _Ln} <- maps:get(abstract, C, [])],
    ["    state St:", integer_to_list(state_bits(C)), ";\n",
     group("state", States, Sigs),
     group("input", Inputs, Sigs),
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
     lists:join(", ", [[varp_name(N), ":",
		       integer_to_list(maps:get(width, maps:get(N, Sigs)))]
		       || N <- Names]),
     ";\n"].

state_bits(C) ->
    bits(?FIRST_USER_STATE + length(maps:get(states, C))).

bits(N) when N < 2 -> 1;
bits(N) -> trunc(math:ceil(math:log2(N + 1))).

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
    [state_chain(C), [chain(V, C, Sigs) || V <- Vs]].

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
    W = maps:get(width, maps:get(V, Sigs)),
    Exprs = [["next(", varp_name(V), ") == ",
	      vexpr(maps:get(expr, R), C, num)] || R <- Rules],
    ["    // ---- ", V, ":", integer_to_list(W), " ",
     lists:duplicate(max(1, 46 - length(V)), $-), "\n",
     [["    //   ", src_of(R, C), "\n"] || R <- Rules],
     priority(V, lists:reverse(Guards), lists:reverse(Exprs),
	      [{none, ["next(", varp_name(V), ") == ", varp_name(V)]}], none),
     "\n"].

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

%%% `num' yields a value, `bool' a condition. A bare signal used as a condition
%%% becomes `X == 1', because varp has no implicit truthiness on a vector.
%% An integer used as a CONDITION has to be compared -- varp has no implicit
%% truthiness, so a bare `1' from `Led = 1 ? 1' is a value where a formula was
%% wanted and the file does not parse.
vexpr({'INT', _, S}, _C, bool) -> [S, " == 1"];
vexpr({'INT', _, S}, _C, num)  -> S;

vexpr({field, _, {'WORD', _, N}}, C, M) ->
    Text = case is_builtin_state(N) orelse
	       lists:keymember(N, 1, number_states(C)) of
	       true  -> state_text(N, C);
	       false -> varp_name(N)         % a signal or a define
	   end,
    case M of
	bool -> [Text, " == 1"];
	num  -> Text
    end;

vexpr({abstracted, Name, _Orig}, _C, _M) -> [Name, " == 1"];
vexpr({'!', _, M0}, C, _M) -> ["not ", paren(vexpr(M0, C, bool))];
vexpr({'-', _, M0}, C, _M) -> ["-", paren(vexpr(M0, C, num))];

vexpr({Op, _, L, R}, C, _M) when Op =:= '&&'; Op =:= '||' ->
    [paren(vexpr(L, C, bool)), op(Op), paren(vexpr(R, C, bool))];

vexpr({Op, _, L, R}, C, _M) ->
    [paren(vexpr(L, C, num)), op(Op), paren(vexpr(R, C, num))].

op('&&') -> " and ";
op('||') -> " or ";
op(Op)   -> [" ", atom_to_list(Op), " "].

%%% The source line, as a comment above each chain, so the model can be read
%%% against the program without holding both files open.
src_of(#{target := T, expr := E, guard := G, line := Ln}, C) ->
    [integer_to_list(Ln), ": ", T, " = ",
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
	       [["//   line ", integer_to_list(maps:get(line, R)), "\n"]
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
