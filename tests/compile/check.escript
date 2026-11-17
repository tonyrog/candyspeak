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
    halt(case Count(differ) of 0 -> 0; _ -> 1 end).

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
