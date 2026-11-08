%%% @doc
%%%     Evaluate a candyspeak expression from the parse tree against a set of
%%%     values, and print one back as candyspeak.
%%%
%%%     For a tool that watches a running program -- the panel's code view
%%%     lights the conditions that hold -- and later for replaying a model
%%%     trace. The values are what csp dumps: committed variables and pins by
%%%     name. What the dump does not carry is worked out here:
%%%
%%%       a #local is its formula, evaluated over the same values;
%%%       a #param, #constant or #define is its declared number (a #param
%%%       patched at run time is not seen);
%%%       a state name is its number, numbered as the runtime does.
%%%
%%%     What cannot be known from the dump -- a timer's elapsed(), a buffer
%%%     byte, a call -- is `unknown' unless Vals says it, as a model trace
%%%     does ({timeout, T}, {elapsed, T} and {part, B, P} are keys there too),
%%%     and the logic is three-valued around it:
%%%     `false && unknown' is false, `true || unknown' is true. Unknown is
%%%     never guessed.
%%%
%%%     A dumped value is the stored bits. A signed declaration reads them
%%%     sign-extended from its width, as the runtime compares them.
%%%
%%%     cycle/3 runs one cycle of the program, the runtime's sequential mode
%%%     written out (doc/manual_en.md, Execution Model):
%%%
%%%       DOUT starts as DIN, the committed values; the inputs are sampled
%%%       into DOUT; every rule reads DIN and writes DOUT, in source order, the
%%%       last write winning; changed(X) is "X was written to a new value so
%%%       far this cycle", rising/falling(X) DIN against DOUT; a `<-' runs when
%%%       something on its right changed so far, or on the seed cycle; a rule
%%%       outside every block runs in INIT and NORMAL only, an #in block in
%%%       its states, a #when while its condition holds; FAILSAFE is never
%%%       left; INIT steps to NORMAL unless a rule wrote State. DOUT is then
%%%       the next DIN.
%%%
%%%     An object runs where its line stands: the line's bindings, then the
%%%     module's body with the object's members (`a0.A') and its own State.
%%%     cycle/4 with mode => reactive runs, after a sequential seed cycle,
%%%     only the rules something they watch woke (Execution Modes).
%%%
%%%     NOT YET: parts, buffers, unpack, calls, #disable with modules. What a
%%%     timer does over time is the caller's: {timeout, T} in In says whether
%%%     T fired.
%%% @end

-module(candyspeak_eval).

-export([env/1, eval/3, truth/3, text/1]).
-export([prog/1, prog/2, prog/3, cycle/3, cycle/4, sampled/2]).

-define(FIRST_USER_STATE, 3).

%%% ------------------------------------------------------------ environment

%% What the declarations say, once per program.
env(Ast) -> env(Ast, fixpoint).

env(Ast, Float) ->
    lists:foldl(fun decl/2,
		#{float => Float,
		  sigs => #{}, consts => #{}, locals => #{}, states => [],
		  tables => #{}, arrays => #{}, bound => #{}, slots => #{}},
		Ast).

decl({states, _, Names}, E) ->
    E#{states := maps:get(states, E) ++ [N || {'WORD', _, N} <- Names]};
decl({constant, _, {'WORD', _, N}, scalar, _Res, _Opts, X}, E) -> const(N, X, E);
decl({constant, _, {'WORD', _, N}, {array_size, _, _}, _Res, _Opts,
      {array, _, Xs}}, E) ->
    E#{tables := maps:put(N, [eval(X, #{}, E) || X <- Xs], maps:get(tables, E))};
decl({Kind, _, {'WORD', _, N}, {array_size, _, {'INT', _, Sz}}, Res, Opts, _X}, E)
  when Kind =:= digital; Kind =:= analog; Kind =:= variable ->
    K = list_to_integer(Sz),
    Sig = sig(Kind, Res, Opts),
    E#{sigs := maps:merge(maps:get(sigs, E),
			  maps:from_list([{elem(N, I), Sig}
					  || I <- lists:seq(0, K - 1)])),
       arrays := maps:put(N, K, maps:get(arrays, E))};
decl({define, _, {'WORD', _, N}, X}, E)                         -> const(N, X, E);
decl({param, _, {'WORD', _, N}, _Res, _Opts, X}, E)             -> const(N, X, E);
%% A #local has one copy, readable in the cycle it is written. Its formula,
%% when it has one, is a rule where it is declared (see rules/4); the formula
%% is kept too, for a value worked out outside a cycle.
decl({local, _, {'WORD', _, N}, Res, Opts, X}, E) ->
    Sig = sig(variable, Res, Opts),
    E1 = E#{sigs := maps:put(N, Sig, maps:get(sigs, E)),
	    slots := maps:put(N, true, maps:get(slots, E))},
    case X =:= undefined orelse proplists:get_value(dir, Opts) =:= in of
	true  -> E1;
	false -> E1#{locals := maps:put(N, {X, Sig}, maps:get(locals, E))}
    end;
decl({Kind, _, {'WORD', _, N}, scalar, Res, Opts, _X}, E)
  when Kind =:= digital; Kind =:= analog; Kind =:= variable ->
    B = maps:from_list([{N, true} || proplists:is_defined(bind, Opts)]),
    E#{sigs := maps:put(N, sig(Kind, Res, Opts), maps:get(sigs, E)),
       bound := maps:merge(maps:get(bound, E), B)};    % a buffer's bits
decl(_, E) ->
    E.

const(N, X, E) ->
    case eval(X, #{}, E) of
	V when is_number(V) -> E#{consts := maps:put(N, V, maps:get(consts, E))};
	unknown              -> E
    end.

%% Width and sign as the runtime has them (and candyspeak_varp): a pin, a
%% field and a declared width without a type are unsigned; no width is a
%% signed 32.
sig(Kind, Res, Opts) ->
    W = case {Kind, Res} of
	    {digital, _}         -> 1;
	    {_, {'INT', _, S}}   -> list_to_integer(S);
	    _                    -> 32
	end,
    Signed = case proplists:get_value(type, Opts) of
		 float    -> float;                  % {32, float}: a float32
		 unsigned -> false;
		 integer  -> true;
		 _ -> (Kind =/= digital) andalso (Kind =/= analog) andalso
			  (Kind =/= field) andalso
			  not (is_tuple(Res) andalso element(1, Res) =:= 'INT')
	     end,
    case Signed of
	float -> {32, float};
	_     -> {W, Signed}
    end.

%%% ------------------------------------------------------------------ eval

%% The value, or `unknown'. Vals maps a name to the dumped value.
eval({'INT', _, S}, _Vals, _E) -> int(S);
eval({'FLT', _, S}, _Vals, E) -> snap(flt(S), E);
eval({field, _, {'WORD', _, N}}, Vals, E) -> name(N, Vals, E);
eval({field, _, {fld, _, {'WORD', _, O}, {'WORD', _, N}}}, Vals, E) ->
    name(O ++ "." ++ N, Vals, E);                   % an object's member
eval({field, _, {part, _, {'WORD', _, B}, {'WORD', _, P}}}, Vals, _E) ->
    maps:get({part, B, P}, Vals, unknown);
eval({field, _, {index, _, {'WORD', _, A}, Ix}}, Vals, E) ->
    case eval(Ix, Vals, E) of
	K when is_integer(K), K >= 0 ->
	    case maps:find(A, maps:get(tables, E, #{})) of
		{ok, Xs} when K < length(Xs) -> lists:nth(K + 1, Xs);
		{ok, _}  -> unknown;
		error ->
		    case maps:find(A, maps:get(arrays, E, #{})) of
			{ok, Sz} when K < Sz -> name(elem(A, K), Vals, E);
			_ -> unknown                 % a buffer's byte, NOT YET
		    end
	    end;
	_ -> unknown
    end;
eval({field, _, _}, _Vals, _E) -> unknown;          % range, part of a part
eval({call, _, {'WORD', _, F}, [{field, _, {'WORD', _, N}}]},
     Vals = #{'$' := C}, E)
  when F =:= "changed"; F =:= "rising"; F =:= "falling" ->
    edge(F, N, Vals, C, E);
eval({call, _, {'WORD', _, F}, [{field, _, {'WORD', _, T}}]}, Vals, _E)
  when F =:= "timeout"; F =:= "elapsed" ->
    maps:get({list_to_atom(F), T}, Vals, unknown);
eval({call, _, {'WORD', _, F}, []}, Vals, _E) when F =:= "cycle"; F =:= "tick" ->
    maps:get(list_to_atom(F), Vals, unknown);       % the caller's: cycle, tick
eval({call, _, {'WORD', _, F}, Args}, Vals, E) ->
    Vs = [eval(A, Vals, E) || A <- Args],
    case lists:all(fun is_number/1, Vs) of
	true  -> fn(F, Vs, E);
	false -> unknown
    end;
eval({'!', _, X}, Vals, E) ->
    case truth(X, Vals, E) of
	unknown -> unknown;
	T       -> b(not T)
    end;
eval({'-', _, X}, Vals, E) ->
    case eval(X, Vals, E) of
	A when is_float(A) -> snap(-A, E);
	A -> un(fun(Y) -> -Y end, A)
    end;
eval({'~', _, X}, Vals, E) -> un(fun(A) -> bnot A end, eval(X, Vals, E));
eval({'&&', _, L, R}, Vals, E) ->
    case {truth(L, Vals, E), truth(R, Vals, E)} of
	{false, _} -> 0;
	{_, false} -> 0;
	{true, true} -> 1;
	_ -> unknown
    end;
eval({'||', _, L, R}, Vals, E) ->
    case {truth(L, Vals, E), truth(R, Vals, E)} of
	{true, _} -> 1;
	{_, true} -> 1;
	{false, false} -> 0;
	_ -> unknown
    end;
eval({Op, _, L, R}, Vals, E) ->
    case {eval(L, Vals, E), eval(R, Vals, E)} of
	{A, B} when is_float(A), is_number(B); is_number(A), is_float(B) ->
	    fbin(Op, float(A), float(B), fmt(E));
	{A, B} -> bin(Op, A, B)
    end;
eval(_, _Vals, _E) ->
    unknown.                                         % a string

%% The runtime's pure built-ins on integers (src/csp_rt.c fn_*); a float, a
%% call with a side effect (print, latch) or a board's own function is
%% unknown.
fn(F, Vs, E) when F =:= "min"; F =:= "max"; F =:= "abs"; F =:= "clip" ->
    %% float when any argument is: an integer is promoted, as V_NUMBER does
    R = num(F, Vs),
    case lists:any(fun is_float/1, Vs) of
	true  -> snap(float(R), E);
	false -> R
    end;
fn("sign", [A], _E) when A < 0 -> -1;
fn("sign", [A], _E)       -> b(A /= 0);
fn("trunc", [A], _E)      -> trunc(A);             % toward zero
fn("round", [A], _E)      -> round(A);             % halves away from zero
fn(_, _, _E)              -> unknown.

num("min", [A, B])         -> min(A, B);
num("max", [A, B])         -> max(A, B);
num("abs", [A])            -> abs(A);
num("clip", [X, Lo, _]) when X < Lo -> Lo;
num("clip", [X, _, Hi]) when X > Hi -> Hi;
num("clip", [X, _, _])     -> X;
num(_, _)                  -> unknown.

%% Within a cycle (Vals carries it as '$'): changed() reads what was written
%% so far, an edge DIN against DOUT.
edge("changed", N, _Vals, #{dirty := D}, _E) ->
    case maps:get(N, D, false) of
	true    -> 1;
	unsure   -> unknown;
	false   -> 0
    end;
edge(F, N, Vals, #{out := Out}, E) ->
    case {name(N, Vals, E), maps:get(N, Out, unknown)} of
	{I, O} when is_number(I), is_number(O) ->
	    case F of
		"rising"  -> b(I =:= 0 andalso O =/= 0);
		"falling" -> b(I =/= 0 andalso O =:= 0)
	    end;
	_ -> unknown
    end.

%% true, false or unknown: a value as a condition is `!= 0'.
truth(X, Vals, E) ->
    case eval(X, Vals, E) of
	unknown -> unknown;
	V when V == 0 -> false;                     % 0.0 too
	_       -> true
    end.

%% Within a cycle (Vals carries it as '$') a #local is what DOUT has, as its
%% rule or the binding left it. Outside one, what Vals says, else its
%% formula over Vals.
name(N, Vals, E = #{sigs := Sigs, consts := Cs, locals := Ls}) ->
    Slot = maps:is_key(N, maps:get(slots, E, #{})),
    V0 = case {Slot, Vals} of
	     {true, #{'$' := #{out := Out}}} -> maps:find(N, Out);
	     _ -> maps:find(N, Vals)
	 end,
    case V0 of
	{ok, V} when is_integer(V) ->
	    case maps:find(N, Sigs) of
		{ok, {W, true}}  -> sext(V, W);
		{ok, {_, float}} -> from_bits(V, fmt(E));  % a node's dump: the bits
		_                -> V
	    end;
	{ok, V} when is_float(V) -> V;
	{ok, _} -> unknown;
	error ->
	    case {maps:find(N, Cs), maps:find(N, Ls), Vals} of
		{{ok, V}, _, _} -> V;
		{_, {ok, {X, _}}, _} when not is_map_key('$', Vals) ->
		    stored(N, eval(X, Vals, E#{locals := maps:remove(N, Ls)}), E);
		{_, {ok, _}, _} -> unknown;
		_ -> state(N, E)
	    end
    end.

state("INIT", _E)     -> 0;
state("NORMAL", _E)   -> 1;
state("FAILSAFE", _E) -> 2;
state(N, #{states := Ss}) ->
    User = lists:foldl(fun(S, Acc) ->
			       case lists:member(S, ["INIT", "NORMAL", "FAILSAFE"])
				   orelse lists:member(S, Acc) of
				   true  -> Acc;
				   false -> Acc ++ [S]
			       end
		       end, [], Ss),
    case string:str(User, [N]) of
	0 -> unknown;
	I -> ?FIRST_USER_STATE + I - 1
    end.

%% A local holds what its declared width can: the formula is computed and
%% then stored, as the runtime does.
store(unknown, _W, _S) -> unknown;
store(V, W, S) when is_float(V) -> store(trunc(V), W, S);   % toward zero
store(V, W, S) ->
    M = V band ((1 bsl W) - 1),
    case S of
	true  -> sext(M, W);
	false -> M
    end.

sext(V, W) ->
    M = V band ((1 bsl W) - 1),
    case M >= (1 bsl (W - 1)) of
	true  -> M - (1 bsl W);
	false -> M
    end.

un(_F, unknown) -> unknown;
un(_F, A) when is_float(A) -> unknown;            % ~ of a float
un(F, A)        -> F(A).

%% A float in the runtime is what the build makes it: Q16.16 (USE_FIXPOINT,
%% the host's and AVR's, SAMD's and the Due's) or an IEEE float32. Kept as an
%% Erlang float, always one the format can hold: every result snapped to it.
%% fixpoint is the default; prog/3 with {float, ieee} says otherwise.
fmt(E) -> maps:get(float, E, fixpoint).

snap(X, E) when is_map(E) -> snap(X, fmt(E));
snap(X, fixpoint) -> from_raw(wrap32(round(X * 65536)));
snap(X, ieee) ->
    try <<X:32/float>> of
	<<Y:32/float>> -> Y
    catch
	error:badarg -> unknown                     % out of float32's range
    end.

raw(X) -> round(X * 65536).

from_raw(R) -> R / 65536.

wrap32(R) -> sext(R, 32).

from_bits(V, fixpoint) -> from_raw(sext(V, 32));
from_bits(V, ieee) ->
    case <<V:32>> of
	<<F:32/float>> -> F;
	_ -> unknown                                % a NaN or an infinity
    end.

%% A float stored: what the format holds.
fstore(unknown, _E) -> unknown;
fstore(V, E) -> snap(float(V), E).

flt(S) ->
    case string:to_float(S) of
	{F, _} when is_float(F) -> F;
	_ -> float(list_to_integer(S))
    end.

bin(_Op, unknown, _) -> unknown;
bin(_Op, _, unknown) -> unknown;
bin('+', A, B)  -> A + B;
bin('-', A, B)  -> A - B;
bin('*', A, B)  -> A * B;
bin('/', _, 0)  -> 0;                       % the runtime's division by zero
bin('/', A, B)  -> A div B;
bin('%', _, 0)  -> 0;
bin('%', A, B)  -> A rem B;
bin('&', A, B)  -> A band B;
bin('|', A, B)  -> A bor B;
bin('^', A, B)  -> A bxor B;
bin('<<', A, B) -> A bsl B;
bin('>>', A, B) -> A bsr B;
bin('==', A, B) -> b(A == B);
bin('!=', A, B) -> b(A /= B);
bin('<', A, B)  -> b(A < B);
bin('<=', A, B) -> b(A =< B);
bin('>', A, B)  -> b(A > B);
bin('>=', A, B) -> b(A >= B);
bin(_, _, _)    -> unknown.

%% Float arithmetic, as src/csp_fixpoint.c or a float32 does it. What has no
%% float form (% and the bit operators) is unknown, and so is x/0.0 in IEEE.
fbin(Op, A, B, fixpoint) when Op =:= '+'; Op =:= '-'; Op =:= '*'; Op =:= '/' ->
    from_raw(wrap32(fix(Op, raw(A), raw(B))));
fbin(Op, A, B, ieee) when Op =:= '+'; Op =:= '-'; Op =:= '*' ->
    snap(case Op of '+' -> A + B; '-' -> A - B; '*' -> A * B end, ieee);
fbin('/', _, B, ieee) when B == 0 -> unknown;
fbin('/', A, B, ieee) -> snap(A / B, ieee);
fbin('==', A, B, _) -> b(A == B);
fbin('!=', A, B, _) -> b(A /= B);
fbin('<', A, B, _)  -> b(A < B);
fbin('<=', A, B, _) -> b(A =< B);
fbin('>', A, B, _)  -> b(A > B);
fbin('>=', A, B, _) -> b(A >= B);
fbin(_, _, _, _)    -> unknown.

fix('+', A, B) -> A + B;
fix('-', A, B) -> A - B;
fix('*', A, B) -> (A * B) bsr 16;
fix('/', _, 0) -> 0;
fix('/', A, B) -> (A bsl 16) div B.

b(true)  -> 1;
b(false) -> 0.

int("0x" ++ H) -> list_to_integer(H, 16);
int("0X" ++ H) -> list_to_integer(H, 16);
int("0b" ++ B) -> list_to_integer(B, 2);
int("0B" ++ B) -> list_to_integer(B, 2);
int(S)         -> list_to_integer(S).

elem(A, 0) -> A;
elem(A, K) -> A ++ "[" ++ integer_to_list(K) ++ "]".

%%% ----------------------------------------------------------------- cycle

%% The program, once, as the runtime lays it out: the rules in instruction
%% order. An object's line is where its rules run -- its bindings, then the
%% module's body for it -- and an object's members are names like any other,
%% `a0.A'. Top is the file the parse started in; candyspeak:parse/2 with
%% `sources' says where an import is.
prog(Ast) -> prog(Ast, "").

prog(Ast, Top) -> prog(Ast, Top, []).

%% Opts: {float, fixpoint | ieee}, the build's floats (fixpoint the default).
prog(Ast, Top, Options) ->
    Float = proplists:get_value(float, Options, fixpoint),
    {Main, Mods} = top(Ast, Top, [], #{}),
    Objs = [{O, M, Ln, Bs} || {object, Ln, {'WORD', _, M}, {'WORD', _, O}, Bs} <- Main,
			      maps:is_key(M, Mods)],
    Bodies = maps:from_list([{O, rename(maps:get(M, Mods), O, members(maps:get(M, Mods)))}
			     || {O, M, _, _} <- Objs]),
    E0 = env(Main ++ lists:append(maps:values(Bodies)), Float),
    E1 = E0#{states := maps:get(states, env(Ast)),
	     svars => ["State" | [O ++ ".State" || {O, _, _, _} <- Objs]]},
    E = lists:foldl(fun patch/2, E1, [X || {immediate, X} <- Main]),
    Ctx = #{obj => "", sv => "State", mods => Mods, bodies => Bodies,
	    consts => [N || B <- [Ast | maps:values(Bodies)],
			    D <- B, element(1, D) =:= constant orelse
				element(1, D) =:= define,
			    {'WORD', _, N} <- [element(3, D)]]},
    Rs0 = rules(Main, [], Top, Ctx),
    Rs = case Objs of
	     [] -> disable(Rs0, disabled(Ast, length([R || R <- Rs0, not gen(R)])));
	     _  -> Rs0                     % #disable counts a module's body once: NOT YET
	 end,
    #{env => E, rules => [R#{wake => wake(R)} || R <- Rs],
      inputs => [N || {K, _, {'WORD', _, N}, _, _, Opts, _} <- Main,
		      lists:member(K, [digital, analog, variable]),
		      proplists:get_value(dir, Opts) =:= in] ++
	  [N || B <- maps:values(Bodies),
		{K, _, {'WORD', _, N}, _, _, Opts, _} <- B,
		lists:member(K, [digital, analog]),
		proplists:get_value(dir, Opts) =:= in],
      timers => [T || B <- [Main | maps:values(Bodies)],
		      {timer, _, {'WORD', _, T}, _, _} <- B]}.

%% `> P = 5' in the program: a #param patched as it is loaded, before the
%% first cycle -- what the rules then read. (One onto a variable is in the
%% values already.)
patch({'=', _, {field, _, F}, X}, E = #{consts := Cs}) ->
    N = case F of
	    {'WORD', _, W} -> W;
	    {fld, _, {'WORD', _, O}, {'WORD', _, W}} -> O ++ "." ++ W;
	    _ -> none
	end,
    case {maps:is_key(N, Cs), eval(X, #{}, E)} of
	{true, V} when is_number(V) -> E#{consts := Cs#{N => V}};
	_ -> E
    end;
patch(_, E) -> E.

gen(#{gen := true}) -> true;
gen(_) -> false.

%% #disable and #enable, by rule number, in the order they stand.
disabled(Ast, N) ->
    lists:foldl(fun({disable, _, Rs}, Off) -> lists:usort(Off ++ nums(Rs, N));
		   ({enable, _, Rs}, Off)  -> Off -- nums(Rs, N);
		   (_, Off) -> Off
		end, [], Ast).

nums(Rs, N) ->
    lists:append([case R of
		      {{'INT', _, A}, {'INT', _, Z}} ->
			  lists:seq(int(A), min(int(Z), N));
		      {'INT', _, A} -> [int(A)];
		      _ -> []
		  end || R <- Rs]).

disable(Rs, Off) -> disable(Rs, Off, 1).

disable([R | T], Off, I) ->
    case gen(R) of
	true  -> [R | disable(T, Off, I)];
	false -> [R || not lists:member(I, Off)] ++ disable(T, Off, I + 1)
    end;
disable([], _Off, _I) -> [].

%% The program outside its modules, and each module's body after the file it
%% is in.
top([{module, _, {'WORD', _, M}} | T], F, Acc, Ms)    -> body(T, 0, F, M, [{source, 0, F}], Acc, Ms);
top([{module, _, {'WORD', _, M}, _} | T], F, Acc, Ms) -> body(T, 0, F, M, [{source, 0, F}], Acc, Ms);
top([X = {source, _, F} | T], _F, Acc, Ms) -> top(T, F, [X | Acc], Ms);
top([X | T], F, Acc, Ms) -> top(T, F, [X | Acc], Ms);
top([], _F, Acc, Ms) -> {lists:reverse(Acc), Ms}.

body([{'end', _} | T], 0, F, M, B, Acc, Ms) -> top(T, F, Acc, Ms#{M => lists:reverse(B)});
body([X = {'end', _} | T], D, F, M, B, Acc, Ms) -> body(T, D - 1, F, M, [X | B], Acc, Ms);
body([X = {in, _, _} | T], D, F, M, B, Acc, Ms) -> body(T, D + 1, F, M, [X | B], Acc, Ms);
body([X = {'when', _, _} | T], D, F, M, B, Acc, Ms) -> body(T, D + 1, F, M, [X | B], Acc, Ms);
body([X = {source, _, G} | T], D, _F, M, B, Acc, Ms) -> body(T, D, G, M, [X | B], Acc, Ms);
body([X | T], D, F, M, B, Acc, Ms) -> body(T, D, F, M, [X | B], Acc, Ms);
body([], _D, F, M, B, Acc, Ms) -> top([], F, Acc, Ms#{M => lists:reverse(B)}).

%% What a module declares: in an object, each is the object's own.
members(Body) ->
    ["State" | [N || D <- Body, is_tuple(D), tuple_size(D) >= 3,
		     not lists:member(element(1, D), [object, module, source]),
		     {'WORD', _, N} <- [element(3, D)]]].

%% A module's body as object O runs it: its members named O.N. A state name
%% is the program's, and an #in in the body is the object's State.
rename(Body, O, Ms) ->
    [rn_decl(D, O, Ms) || D <- Body, not is_states(D)].

is_states({states, _, _}) -> true;
is_states(_) -> false.

rn_decl({'when', L, X}, O, Ms) -> {'when', L, rn(X, O, Ms)};
rn_decl(D = {K, _, _}, _O, _Ms) when K =:= in; K =:= source -> D;
rn_decl(D, O, Ms) when is_tuple(D), tuple_size(D) >= 3 ->
    R = rn(D, O, Ms),
    case element(3, D) of
	{'WORD', L, N} when element(1, D) =/= rule -> setelement(3, R, {'WORD', L, O ++ "." ++ N});
	_ -> R
    end;
rn_decl(D, _O, _Ms) -> D.

rn({field, L, F}, O, Ms) -> {field, L, rnf(F, O, Ms)};
rn(T, O, Ms) when is_tuple(T) -> list_to_tuple([rn(X, O, Ms) || X <- tuple_to_list(T)]);
rn(L, O, Ms) when is_list(L) -> [rn(X, O, Ms) || X <- L];
rn(X, _O, _Ms) -> X.

rnf({'WORD', L, N}, O, Ms) ->
    case lists:member(N, Ms) of
	true  -> {'WORD', L, O ++ "." ++ N};
	false -> {'WORD', L, N}
    end;
rnf({index, L, B, Ix}, O, Ms) -> {index, L, rnf(B, O, Ms), rn(Ix, O, Ms)};
rnf({part, L, B, P}, O, Ms)   -> {part, L, rnf(B, O, Ms), P};
rnf({fld, L, A, B}, O, Ms)    -> {fld, L, rnf(A, O, Ms), B};
rnf(F, _O, _Ms) -> F.

%% Gs: the blocks around, innermost first. A rule outside every block runs
%% in INIT and NORMAL only, at the top; in a module's body it always runs. A
%% #local's formula is a rule where it stands, run when its block is -- one
%% csp does not count as a rule that fired.
rules([{source, _, F} | T], Gs, _F, C) -> rules(T, Gs, F, C);
rules([{in, _, Ns} | T], Gs, F, C = #{sv := SV}) ->
    rules(T, [{in, SV, [N || {'WORD', _, N} <- Ns]} | Gs], F, C);
rules([{'when', _, X} | T], Gs, F, C) -> rules(T, [{'when', X} | Gs], F, C);
rules([{'end', _} | T], Gs, F, C) -> rules(T, tl_or_nil(Gs), F, C);
rules([{local, Ln, {'WORD', _, N}, _Res, Opts, X} | T], Gs, F, C = #{obj := O})
  when X =/= undefined ->
    case proplists:get_value(dir, Opts) of
	in -> rules(T, Gs, F, C);                  % the object's line binds it
	_ ->
	    [#{line => {O, F, Ln}, gates => Gs, guard => undefined, local => true,
	       assigns => [{'=', Ln, {field, Ln, {'WORD', Ln, N}}, X}]}
	     | rules(T, Gs, F, C)]
    end;
rules([{timer, Ln, {'WORD', _, N}, P, _} | T], Gs, F, C = #{obj := O, sv := SV}) ->
    %% a period that does not fold (a #param) is set in INIT, by a rule
    case folds(P, C) of
	true -> rules(T, Gs, F, C);
	false ->
	    Tgt = {field, Ln, {part, Ln, {'WORD', Ln, N}, {'WORD', Ln, "period"}}},
	    [#{line => {O, F, Ln}, gates => [{in, SV, ["INIT"]}], guard => undefined,
	       assigns => [{'=', Ln, Tgt, P}]} | rules(T, Gs, F, C)]
    end;
rules([{rule, Ln, As, G} | T], Gs, F, C = #{obj := O}) ->
    Gates = case {Gs, O} of
		{[], ""} -> [{in, "State", ["INIT", "NORMAL"]}];
		_ -> Gs
	    end,
    [#{line => {O, F, Ln}, gates => Gates, guard => G, assigns => As}
     | rules(T, Gs, F, C)];
rules([{object, Ln, {'WORD', _, M}, {'WORD', _, O}, Bs} | T], Gs, F,
      C = #{obj := "", mods := Mods, bodies := Bodies}) when is_map_key(M, Mods) ->
    Body = maps:get(M, Mods),
    object(O, Ln, F, Bs, Body) ++
	rules(maps:get(O, Bodies), [], F, C#{obj := O, sv := O ++ ".State"}) ++
	rules(T, Gs, F, C);
rules([_ | T], Gs, F, C) -> rules(T, Gs, F, C);
rules([], _Gs, _F, _C) -> [].

%% An object's line, as csp_parse_object compiles it: a #local ... in's
%% formula every cycle (a formula: not a rule that fires), a `<-' a standing rule, the default of a #local ... in
%% left unbound, then the static ones (`=', `.part =') once, in the object's
%% INIT, and State = NORMAL after them.
object(O, Ln, F, Bs, Body) ->
    Ms = members(Body),
    Ins = [{N, X} || {local, _, {'WORD', _, N}, _, Opts, X} <- Body,
		     proplists:get_value(dir, Opts) =:= in],
    In = fun({_, _, {field, _, {'WORD', _, N}}, _}) -> lists:keymember(N, 1, Ins);
	    (_) -> false
	 end,
    Tgt = fun(T) -> rn(T, O, Ms) end,
    R = fun(As, Gates) -> #{line => {"", F, Ln}, gates => Gates, guard => undefined,
			    assigns => As, gen => true}
	end,
    Bound = [N || B = {_, _, {field, _, {'WORD', _, N}}, _} <- Bs, In(B)],
    Statics = [{'=', L, Tgt(T), X} || B = {'=', L, T, X} <- Bs, not In(B)],
    Init = [{in, O ++ ".State", ["INIT"]}],
    Formula = fun(A) -> (R([A], []))#{local => true} end,
    [Formula({'=', L, Tgt(T), X}) || B = {_, L, T, X} <- Bs, In(B)] ++
	[R([{'<-', L, Tgt(T), X}], []) || B = {'<-', L, T, X} <- Bs, not In(B)] ++
	[Formula({'=', Ln, {field, Ln, {'WORD', Ln, O ++ "." ++ N}}, X})
	 || {N, X} <- Ins, X =/= undefined, not lists:member(N, Bound)] ++
	case Statics of
	    [] -> [];
	    _  -> [R(Statics, Init),
		   R([{'=', Ln, {field, Ln, {'WORD', Ln, O ++ ".State"}},
		       {'INT', Ln, "1"}}], Init)]
	end.

tl_or_nil([_ | T]) -> T;
tl_or_nil([])      -> [].

folds({'INT', _, _}, _C) -> true;
folds({field, _, {'WORD', _, N}}, #{consts := Cs}) -> lists:member(N, Cs);
folds(_, _C) -> false.

%% What wakes a rule in reactive mode: the names behind its `?', both sides
%% of a `<-', and the blocks' -- a State test is folded into every rule an
%% #in (or the INIT and NORMAL of a loose rule) gates. One with none of them
%% runs on the seed cycle only.
wake(#{guard := G, assigns := As, gates := Gs}) ->
    lists:usort(names(G) ++ lists:append([names(T) ++ names(X) || {'<-', _, T, X} <- As]) ++
		    lists:append([case Gt of
				      {in, SV, _} -> [SV];
				      {'when', X} -> names(X)
				  end || Gt <- Gs])).

%% One cycle from the committed values Din. In: what was sampled this cycle
%% (see sampled/2) by name, {timeout, T} => 0 | 1 for a timer that fired,
%% {elapsed, T} => Ms, cycle => N and tick => Ms for cycle() and tick(), and
%% `seed => true' on the first cycle. Opts: mode => seq (the default) or
%% reactive, and for reactive woken => what the cycle before changed (its
%% `changed').
%%
%% #{values => the next Din, fired => [{Object, File, Line}], unsure => [...],
%%   changed => [Name]}: unsure is a rule whose condition could not be known,
%% and what it would have written is `unknown'. Object is "" for the program
%% and an object's line, else the object whose body the rule is in -- as
%% csp --fired has it.
cycle(P, Din, In) -> cycle(P, Din, In, #{}).

cycle(#{env := E, rules := Rs}, Din, In, Opts) ->
    Seed = maps:get(seed, In, false),
    Vals = maps:merge(Din, maps:filter(fun(K, _) -> is_tuple(K) orelse K =:= cycle
							orelse K =:= tick end, In)),
    C0 = #{din => Din, out => Din, dirty => #{}, fired => [], unsure => [],
	   woke => [], phase => input},
    C1 = maps:fold(fun(N, V, C) when is_list(N) -> write(N, V, C, E);
		      (_, _, C) -> C
		   end, C0, In),
    Run = case {maps:get(mode, Opts, seq), Seed} of
	      {reactive, false} ->
		  maps:from_list([{N, true} || N <- maps:get(woken, Opts, []) ++
						   maps:keys(maps:get(dirty, C1)) ++
						   [T || {{timeout, T}, 1} <- maps:to_list(In)]]);
	      _ -> all
	  end,
    C2 = lists:foldl(fun(R, C) -> rule(R, Run, Vals, Seed, C, E) end,
		     C1#{phase := rules}, Rs),
    #{out := Out, fired := F, unsure := M, woke := W} = advance(C2, E),
    #{values => maps:map(fun(N, V) ->
				 case known(N, E) of
				     true  -> V;
				     false -> unknown            % NOT YET
				 end
			 end, Out),
      fired => lists:reverse(F),
      unsure => lists:reverse(M),
      changed => lists:usort(W)}.

%% What a cycle sampled, out of the dump taken after it: its inputs, and
%% whether each timer fired (a node's dump has T.fired: timeout(T) held in
%% that cycle). Left out what the dump does not have -- unknown, not 0.
sampled(#{inputs := Ins, timers := Ts}, Dump) ->
    maps:merge(maps:with(Ins, Dump),
	       maps:from_list([{{timeout, T}, F} || T <- Ts,
						   {ok, F} <- [maps:find(T ++ ".fired", Dump)]])).

rule(R = #{line := Ln, gates := Gs, guard := G, assigns := As},
     Run, Vals0, Seed, C, E) ->
    case Run =:= all orelse lists:any(fun(N) -> maps:is_key(N, Run) end,
				      maps:get(wake, R)) of
	false -> C;
	true ->
	    Vals = Vals0#{'$' => C},
	    case and3([gates(Gs, Vals, E), guard(G, Vals, E), trigger(As, Seed, C)]) of
		false -> C;
		T ->
		    C1 = lists:foldl(fun(A, Ci) -> assign(A, T, Vals, Ci, E) end, C, As),
		    case {T, maps:get(local, R, false)} of
			{_, true}       -> C1;
			{true, _}       -> C1#{fired := [Ln | maps:get(fired, C1)]};
			{unknown, _}    -> C1#{unsure := [Ln | maps:get(unsure, C1)]}
		    end
	    end
    end.

gates(Gs, Vals, E) -> and3([gate(G, Vals, E) || G <- Gs]).

gate({in, SV, Ns}, Vals, E) ->
    case name(SV, Vals, E) of
	S when is_integer(S) -> lists:member(S, [state(N, E) || N <- Ns]);
	_ -> unknown
    end;
gate({'when', X}, Vals, E) -> truth(X, Vals, E).

guard(undefined, _Vals, _E) -> true;
guard(true, _Vals, _E)      -> true;                % a pack/unpack rule
guard(G, Vals, E)           -> truth(G, Vals, E).

%% A rule with a `<-' runs when a name on the right of one has changed so far
%% this cycle; on the seed cycle every one runs once.
trigger(As, Seed, #{dirty := D}) ->
    case [X || {'<-', _, _, X} <- As] of
	[]  -> true;
	_ when Seed -> true;
	Xs ->
	    Ch = [maps:get(N, D, false) || X <- Xs, N <- names(X)],
	    case {lists:member(true, Ch), lists:member(unsure, Ch)} of
		{true, _}      -> true;
		{false, true}  -> unknown;
		{false, false} -> false
	    end
    end.

names({field, _, {'WORD', _, N}}) -> [N];
names({field, _, {fld, _, {'WORD', _, O}, {'WORD', _, N}}}) -> [O ++ "." ++ N];
names({field, _, {index, _, {'WORD', _, A}, Ix}}) -> [A | names(Ix)];
names(T) when is_tuple(T) -> lists:append([names(X) || X <- tuple_to_list(T)]);
names(L) when is_list(L)  -> lists:append([names(X) || X <- L]);
names(_) -> [].

assign({Op, _, Tgt, X}, T, Vals, C, E) when Op =:= '='; Op =:= '<-' ->
    V = eval(X, Vals, E),
    case {target(Tgt, Vals, E), T, V} of
	{none, _, _}             -> C;
	{{one, N}, true, I} when is_number(I) -> write(N, I, C, E);
	{{one, N}, _, _}         -> blur(N, V, C, E);
	{{all, A}, _, _}         ->
	    K = maps:get(A, maps:get(arrays, E), 1),
	    lists:foldl(fun(I, Ci) -> blur(elem(A, I), unknown, Ci, E) end,
			C, lists:seq(0, K - 1))
    end;
assign({unpack, _, Ps}, _T, _Vals, C, E) ->        % NOT YET
    lists:foldl(fun(N, Ci) -> blur(N, unknown, Ci, E) end, C,
		[N || {T, _} <- Ps, {one, N} <- [target(T, #{}, E)]]);
assign(_, _T, _Vals, C, _E) -> C.                   % pack: into a buffer

target({field, _, {'WORD', _, N}}, _Vals, _E) -> {one, N};
target({field, _, {fld, _, {'WORD', _, O}, {'WORD', _, N}}}, _Vals, _E) ->
    {one, O ++ "." ++ N};
target({field, _, {index, _, {'WORD', _, A}, Ix}}, Vals, E) ->
    case eval(Ix, Vals, E) of
	K when is_integer(K), K >= 0 -> {one, elem(A, K)};
	_ -> {all, A}
    end;
target(_, _Vals, _E) -> none.                   % a part

%% A write that may or may not have happened: the value stays only if it is
%% the one that would have been written.
blur(N, V, C = #{out := Out, dirty := D}, E) ->
    case is_number(V) andalso stored(N, V, E) =:= now(N, Out, E) of
	true  -> C;
	false -> woke(N, C#{out := Out#{N => unknown},
			    dirty := D#{N => case maps:get(N, D, false) of
						 true -> true;
						 _    -> unsure
					     end}})
    end.

%% csp_set_value: stored as the declaration holds it, dirty if that differs
%% from what DOUT has. A FAILSAFE State is not left.
write(N, V, C = #{din := Din}, E = #{svars := SVs}) when V =/= 2 ->
    case lists:member(N, SVs) andalso now(N, Din, E) =:= 2 of
	true  -> C;
	false -> put_out(N, V, C, E)
    end;
write(N, V, C, E) -> put_out(N, V, C, E).

put_out(N, V, C = #{out := Out, dirty := D}, E) ->
    S = stored(N, V, E),
    case now(N, Out, E) of
	S -> C;
	unknown ->            % what it was is not known: changed, perhaps
	    woke(N, C#{out := Out#{N => S},
			dirty := D#{N => case maps:get(N, D, false) of
					     true -> true;
					     _    -> unsure
					 end}});
	_ -> woke(N, C#{out := Out#{N => S}, dirty := D#{N => true}})
    end.

%% A change a rule made wakes, in reactive mode, what reads it next cycle.
woke(N, C = #{phase := rules, woke := W}) -> C#{woke := [N | W]};
woke(_N, C) -> C.

now(N, M, E) ->
    case maps:find(N, M) of
	{ok, V} when is_number(V) -> stored(N, V, E);
	_ -> unknown
    end.

stored(N, V, E = #{sigs := Sigs}) ->
    case maps:get(N, Sigs, {32, true}) of
	{_, float} -> fstore(V, E);
	{W, S}     -> store(V, W, S)
    end.

%% What a cycle works out: a State, the variables and pins and their
%% elements -- not a buffer or what is bound into one, a field, a timer.
known(N, #{sigs := Sigs, bound := B, svars := SVs}) ->
    lists:member(N, SVs) orelse
	(maps:is_key(N, Sigs) andalso not maps:is_key(N, B)).

%% The end of a cycle in INIT steps to NORMAL, unless a rule wrote State --
%% the program's and each object's.
advance(C0, E = #{svars := SVs}) ->
    lists:foldl(fun(SV, C = #{din := Din, dirty := D}) ->
			case {now(SV, Din, E), maps:get(SV, D, false)} of
			    {0, false} -> put_out(SV, 1, C, E);
			    _ -> C
			end
		end, C0, SVs).

and3(Ts) ->
    case lists:member(false, Ts) of
	true  -> false;
	false ->
	    case lists:all(fun(T) -> T =:= true end, Ts) of
		true  -> true;
		false -> unknown
	    end
    end.

%%% ------------------------------------------------------------------ text

%% Back to candyspeak, with only the parentheses precedence needs.
text(X) -> lists:flatten(text(X, 0)).

text({'INT', _, S}, _P) -> S;
text({'FLT', _, S}, _P) -> S;
text({'STR', _, S}, _P) -> [$", S, $"];
text({field, _, F}, _P) -> fld(F);
text({'!', _, X}, _P)   -> ["!", text(X, 11)];
text({'-', _, X}, _P)   -> ["-", text(X, 11)];
text({'~', _, X}, _P)   -> ["~", text(X, 11)];
text({call, _, {'WORD', _, F}, Args}, _P) ->
    [F, "(", lists:join(", ", [text(A, 0) || A <- Args]), ")"];
text({Op, _, L, R}, P) ->
    Q = prec(Op),
    T = [text(L, Q), " ", atom_to_list(Op), " ", text(R, Q + 1)],
    case Q < P of
	true  -> ["(", T, ")"];
	false -> T
    end;
text(_, _P) -> "?".

fld({'WORD', _, N})                    -> N;
fld({part, _, B, {'WORD', _, P}})      -> [fld(B), ".", P];
fld({fld, _, B, {'WORD', _, P}})       -> [fld(B), ".", P];
fld({index, _, B, {range, _, {'INT', _, A}, {'INT', _, Z}}}) ->
    [fld(B), "[", A, "..", Z, "]"];
fld({index, _, B, Ix})                 -> [fld(B), "[", text(Ix, 0), "]"];
fld(_)                                 -> "?".

prec('||') -> 1;
prec('&&') -> 2;
prec('|')  -> 3;
prec('^')  -> 4;
prec('&')  -> 5;
prec('==') -> 6;
prec('!=') -> 6;
prec('<')  -> 7;
prec('<=') -> 7;
prec('>')  -> 7;
prec('>=') -> 7;
prec('<<') -> 8;
prec('>>') -> 8;
prec('+')  -> 9;
prec('-')  -> 9;
prec(_)    -> 10.                            % * / %
