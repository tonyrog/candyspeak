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
%%% @end

-module(candyspeak_eval).

-export([env/1, eval/3, truth/3, text/1]).

-define(FIRST_USER_STATE, 3).

%%% ------------------------------------------------------------ environment

%% What the declarations say, once per program.
env(Ast) ->
    lists:foldl(fun decl/2,
		#{sigs => #{}, consts => #{}, locals => #{}, states => []},
		Ast).

decl({states, _, Names}, E) ->
    E#{states := maps:get(states, E) ++ [N || {'WORD', _, N} <- Names]};
decl({constant, _, {'WORD', _, N}, scalar, _Res, _Opts, X}, E) -> const(N, X, E);
decl({define, _, {'WORD', _, N}, X}, E)                         -> const(N, X, E);
decl({param, _, {'WORD', _, N}, _Res, _Opts, X}, E)             -> const(N, X, E);
decl({local, _, {'WORD', _, N}, Res, Opts, X}, E) when X =/= undefined ->
    E#{locals := maps:put(N, {X, sig(variable, Res, Opts)},
			  maps:get(locals, E))};
decl({Kind, _, {'WORD', _, N}, scalar, Res, Opts, _X}, E)
  when Kind =:= digital; Kind =:= analog; Kind =:= variable ->
    E#{sigs := maps:put(N, sig(Kind, Res, Opts), maps:get(sigs, E))};
decl(_, E) ->
    E.

const(N, X, E) ->
    case eval(X, #{}, E) of
	V when is_integer(V) -> E#{consts := maps:put(N, V, maps:get(consts, E))};
	unknown              -> E
    end.

%% Width and sign as the runtime has them (and candyspeak_varp): a pin is
%% unsigned, `:1' is unsigned, any other width without a type is signed, and
%% no width is 32.
sig(Kind, Res, Opts) ->
    W = case {Kind, Res} of
	    {digital, _}         -> 1;
	    {_, {'INT', _, S}}   -> list_to_integer(S);
	    _                    -> 32
	end,
    Signed = case proplists:get_value(type, Opts) of
		 unsigned -> false;
		 integer  -> true;
		 _ -> (Kind =/= digital) andalso (Kind =/= analog) andalso W > 1
	     end,
    {W, Signed}.

%%% ------------------------------------------------------------------ eval

%% The value, or `unknown'. Vals maps a name to the dumped value.
eval({'INT', _, S}, _Vals, _E) -> int(S);
eval({field, _, {'WORD', _, N}}, Vals, E) -> name(N, Vals, E);
eval({field, _, {fld, _, {'WORD', _, O}, {'WORD', _, N}}}, Vals, _E) ->
    case maps:find(O ++ "." ++ N, Vals) of          % an object's member
	{ok, V} when is_integer(V) -> V;
	_ -> unknown
    end;
eval({field, _, {part, _, {'WORD', _, B}, {'WORD', _, P}}}, Vals, _E) ->
    maps:get({part, B, P}, Vals, unknown);
eval({field, _, _}, _Vals, _E) -> unknown;          % index
eval({call, _, {'WORD', _, F}, [{field, _, {'WORD', _, T}}]}, Vals, _E)
  when F =:= "timeout"; F =:= "elapsed" ->
    maps:get({list_to_atom(F), T}, Vals, unknown);
eval({'!', _, X}, Vals, E) ->
    case truth(X, Vals, E) of
	unknown -> unknown;
	T       -> b(not T)
    end;
eval({'-', _, X}, Vals, E) -> un(fun(A) -> -A end, eval(X, Vals, E));
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
    bin(Op, eval(L, Vals, E), eval(R, Vals, E));
eval(_, _Vals, _E) ->
    unknown.                                         % call, string, float

%% true, false or unknown: a value as a condition is `!= 0'.
truth(X, Vals, E) ->
    case eval(X, Vals, E) of
	unknown -> unknown;
	0       -> false;
	_       -> true
    end.

name(N, Vals, E = #{sigs := Sigs, consts := Cs, locals := Ls}) ->
    case maps:find(N, Vals) of
	{ok, V} when is_integer(V) ->
	    case maps:find(N, Sigs) of
		{ok, {W, true}} -> sext(V, W);
		_               -> V
	    end;
	_ ->
	    case maps:find(N, Cs) of
		{ok, V} -> V;
		error ->
		    case maps:find(N, Ls) of
			{ok, {X, {W, S}}} -> store(eval(X, Vals, E), W, S);
			error             -> state(N, E)
		    end
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
un(F, A)        -> F(A).

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
bin('==', A, B) -> b(A =:= B);
bin('!=', A, B) -> b(A =/= B);
bin('<', A, B)  -> b(A < B);
bin('<=', A, B) -> b(A =< B);
bin('>', A, B)  -> b(A > B);
bin('>=', A, B) -> b(A >= B);
bin(_, _, _)    -> unknown.

b(true)  -> 1;
b(false) -> 0.

int("0x" ++ H) -> list_to_integer(H, 16);
int("0X" ++ H) -> list_to_integer(H, 16);
int("0b" ++ B) -> list_to_integer(B, 2);
int("0B" ++ B) -> list_to_integer(B, 2);
int(S)         -> list_to_integer(S).

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
