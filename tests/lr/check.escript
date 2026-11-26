#!/usr/bin/env escript
%%! -pa ebin
%%
%% The C engine (tests/lr/lrtest, src/csp_lr.inc) against the Erlang one
%% (candyspeak_parse), on the same tokens: each program whole and with
%% lines cut short, as a tree (action child ...) of the reductions, or the
%% line of the error. Usage: check.escript LRTEST FILE...

main([Exe | Files0]) ->
    Files = [F || F <- Files0, filelib:is_regular(F)],
    Codes = maps:from_list(lists:zip(candyspeak_parse_tab:tokens(),
				     lists:seq(1, length(candyspeak_parse_tab:tokens())))),
    R = [file_c(Exe, F, Codes) || F <- Files],
    Bad = lists:append([B || {_, _, B} <- R]),
    [io:format("FAIL  ~s~n  erlang ~ts~n  c      ~ts~n", [F, W, G])
     || {F, W, G} <- lists:sublist(Bad, 5)],
    io:format("lr: ~w files, ~w cases, ~w over LR_DEPTH, ~w differ~n",
	      [length(Files), lists:sum([N || {N, _, _} <- R]),
	       lists:sum([D || {_, D, _} <- R]), length(Bad)]),
    halt(case Bad of [] -> 0; _ -> 1 end).

%% line I cut in half
cut(Lines, I) ->
    {A, [L | B]} = lists:split(I - 1, Lines),
    A ++ [lists:sublist(L, length(L) div 2) | B].

%% One file's cases through the C engine at once: {Cases, Deep, [Bad]}.
%% Deeper than the C stack (LR_DEPTH) is the C parser's limit, as the
%% compiler has one: counted, not a difference.
file_c(Exe, F, Codes) ->
    Cases = [Ts || C <- cases(F), {ok, Ts, _} <- [candyspeak_scan:string(C)]],
    In = "tmp/lr_check.in",
    ok = file:write_file(In, [stream(Ts, Codes) || Ts <- Cases]),
    Out = os:cmd(Exe ++ " < " ++ In ++ " 2>/dev/null"),
    Got = string:split(string:trim(Out, trailing), "\n", all),
    Want = [expect(Ts, Codes) || Ts <- Cases],
    Pairs = lists:zip(Want, lines(Got, Want)),
    {length(Cases), length([x || {_, "deep " ++ _} <- Pairs]),
     [{F, W, G} || {W, G} <- Pairs, not lists:prefix("deep ", G),
		   unicode:characters_to_binary(W) =/= unicode:characters_to_binary(G)]}.

lines(Got, Want) when length(Got) >= length(Want) -> lists:sublist(Got, length(Want));
lines(Got, Want) -> Got ++ lists:duplicate(length(Want) - length(Got), "missing").

%% the file, and at most 200 of its lines cut, spread over it
cases(F) ->
    {ok, Bin} = file:read_file(F),
    Text = binary_to_list(Bin),
    Lines = string:split(Text, "\n", all),
    N = length(Lines),
    Step = max(1, N div 200),
    [Text | [lists:flatten(lists:join("\n", cut(Lines, I)))
	     || I <- lists:seq(1, N, Step)]].

stream(Ts, Codes) ->
    [[case T of
	  {Cat, L, V} ->
	      B = unicode:characters_to_binary(V),
	      [integer_to_list(maps:get(Cat, Codes)), " ", integer_to_list(L), " ",
	       integer_to_list(byte_size(B)), "\n", B, "\n"];
	  {Cat, L} ->
	      [integer_to_list(maps:get(Cat, Codes)), " ", integer_to_list(L), " -1\n"]
      end || T <- Ts], "end\n"].

expect(Ts, Codes) ->
    case candyspeak_parse:parse(Ts, fun(Name, Vs) -> {Name, Vs} end) of
	{ok, Tree} -> lists:flatten(tree(Tree, Codes));
	{error, {L, _, _}} -> "error " ++ integer_to_list(L)
    end.

tree({Name, Vs}, Codes) when is_list(Vs) ->
    ["(", atom_to_list(Name), [[" ", tree(V, Codes)] || V <- Vs], ")"];
tree({Cat, _L}, Codes) ->
    ["T", integer_to_list(maps:get(Cat, Codes))];
tree({Cat, _L, V}, Codes) ->
    ["T", integer_to_list(maps:get(Cat, Codes)), $", esc(V), $"].

esc([$" | T]) -> [$\\, $" | esc(T)];
esc([$\\ | T]) -> [$\\, $\\ | esc(T)];
esc([$\n | T]) -> [$\\, $n | esc(T)];
esc([C | T]) -> [C | esc(T)];
esc([]) -> [].
