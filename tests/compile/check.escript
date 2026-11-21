#!/usr/bin/env escript
%%! -pa ebin
%% csp_cc's terms against csp_disasm's of the C compiler's image, per program:
%% same, differs (the first term that does, with -v), or not compiled yet (what
%% stopped it). Fails only on a difference: an unsupported program is progress
%% still to make, a different one is a bug.
main(["-v" | Pairs]) -> run(Pairs, true);
main(Pairs) -> run(Pairs, false).

run(Pairs, Verbose) ->
    Rs = [one(string:split(P, "="), Verbose) || P <- Pairs],
    Count = fun(K) -> length([R || R <- Rs, element(1, R) =:= K]) end,
    Why = lists:sort([W || {unsupported, W} <- Rs]),
    Top = lists:sublist(lists:reverse(lists:keysort(2, count(Why))), 8),
    io:format("compile: ~w same, ~w differ, ~w not compiled yet, of ~w~n",
	      [Count(same), Count(differ), Count(unsupported), length(Rs)]),
    [io:format("  not yet: ~3w x ~p~n", [N, W]) || {W, N} <- Top],
    Hole = hole(),
    Route = route(),
    halt(case {Count(differ), Hole, Route} of {0, ok, ok} -> 0; _ -> 1 end).

%% `#route Src Dst' is a declaration of its own to the parser, not an object
%% line; `route' stays a name anywhere else, as in C.
route() ->
    F = "tmp/compile/route.csp",
    ok = file:write_file(F, "#buffer A:8 inout tcp 5000\n#buffer B:8 inout repl\n"
                         "#variable route\n#route A B\nroute = 1\n"),
    {ok, Ast} = candyspeak:parse(F),
    case [R || R = {route, _, _, _} <- Ast] of
        [{route, 4, {'WORD', _, "A"}, {'WORD', _, "B"}}] ->
            io:format("route: ok~n");
        Got ->
            io:format("route: FAIL ~p~n", [Got]),
            fail
    end.

%% A hole (`_', a part not written yet) parses and compiles, reads as itself
%% and as unknown, and is refused where the image is made: by csp_asm, with
%% its line. A name begins with a letter, as in C: `_x' is no token.
hole() ->
    F = "tmp/compile/hole.csp",
    ok = file:write_file(F, "#variable X\n#variable Y\nY = X > 1 && _\n"),
    {ok, Ast} = candyspeak:parse(F),
    [{'=', _, _, G}] = hd([As || {rule, _, As, _} <- Ast]),
    {ok, Terms} = csp_cc:file(F),
    Got = {candyspeak_eval:text(G),
	   candyspeak_eval:eval(G, #{"X" => 2}, candyspeak_eval:env(Ast)),
	   candyspeak_eval:eval(G, #{"X" => 0}, candyspeak_eval:env(Ast)),
	   csp_asm:asm(Terms),
	   candyspeak_scan:string("_x\n")},
    case Got of
	{"X > 1 && _", unknown, 0, {error, {hole, 3}},
	 {error, {1, candyspeak_scan, {user, "no token starts with '_'"}}, _}} ->
	    io:format("hole: ok~n");
	_ ->
	    io:format("hole: FAIL ~p~n", [Got]),
	    fail
    end.

one([Src, Img], Verbose) ->
    {ok, {_H, Strings, Decls, Code}} = csp_disasm:file(Img),
    C = [{strings, Strings} | Decls ++ Code],
    case csp_cc:file(Src) of
	{ok, [{header, _} | E]} when E =:= C -> {same, Src};
	{ok, [{header, _} | E]} ->
	    io:format("DIFFER ~s~n", [Src]),
	    Verbose andalso first_diff(E, C),
	    {differ, Src};
	{unsupported, W} -> {unsupported, kind(W)};
	{error, _} = Err -> {unsupported, {parse, Err}}
    end.

%% The kind of thing, not every instance of it: {fold, Expr} as fold.
kind(W) when is_tuple(W) -> element(1, W);
kind(W) -> W.

count(Ws) -> maps:to_list(lists:foldl(fun(W, M) -> maps:update_with(W, fun(N) -> N + 1 end, 1, M) end, #{}, Ws)).

first_diff([X | Xs], [X | Ys]) -> first_diff(Xs, Ys);
first_diff([X | _], [Y | _]) -> io:format("  erlang: ~P~n  C:      ~P~n", [X, 30, Y, 30]);
first_diff([], [Y | _]) -> io:format("  erlang: (end)~n  C:      ~P~n", [Y, 30]);
first_diff([X | _], []) -> io:format("  erlang: ~P~n  C:      (end)~n", [X, 30]);
first_diff([], []) -> ok.
