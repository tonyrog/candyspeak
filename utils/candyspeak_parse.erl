%% The parser: an LR engine for candyspeak.y, the grammar the C compiler
%% parses with too. The tables are candyspeak_parse_tab (lrgen.py, from
%% bison's automaton), the actions candyspeak_parse_act. Takes the tokens
%% candyspeak_scan gives: {ok, AST} or {error, {Line, ?MODULE, Message}}.
-module(candyspeak_parse).
-export([parse/1, parse/2, format_error/1]).

parse(Tokens) ->
    parse(Tokens, fun candyspeak_parse_act:act/2).

%% Act(Name, Values): what each reduction makes of its right hand side.
parse(Tokens, Act) ->
    loop([0], [], Tokens, Act).

loop(Ss = [S | _], Vs, Ts, Act) ->
    T = case Ts of
	    [T0 | _] -> T0;
	    [] -> {'$end', 999999}
	end,
    case candyspeak_parse_tab:action(S, element(1, T)) of
	{shift, S1} ->
	    loop([S1 | Ss], [T | Vs], tl_or_end(Ts), Act);
	{reduce, R} ->
	    {Nt, N, Name} = candyspeak_parse_tab:rule(R),
	    {Args, Vs1} = take(N, Vs, []),
	    Ss1 = lists:nthtail(N, Ss),
	    V = Act(Name, Args),
	    loop([candyspeak_parse_tab:goto(hd(Ss1), Nt) | Ss1], [V | Vs1], Ts, Act);
	accept ->
	    [_End, Program | _] = Vs,
	    {ok, Program};
	error ->
	    {error, {element(2, T), ?MODULE, ["syntax error before: ", text(T)]}}
    end.

tl_or_end([_ | Ts]) -> Ts;
tl_or_end([]) -> [].

%% the N values on top, in the order the rule has them
take(0, Vs, Acc) -> {Acc, Vs};
take(N, [V | Vs], Acc) -> take(N - 1, Vs, [V | Acc]).

%% the token as written, quoted as the Erlang parsers quote it
text({_, _, V}) -> io_lib:format("~tp", [V]);
text({'$end', _}) -> "end of file";
text({Cat, _}) -> io_lib:write_atom(Cat).

format_error(Message) ->
    case io_lib:deep_char_list(Message) of
	true -> Message;
	_ -> io_lib:write(Message)
    end.
