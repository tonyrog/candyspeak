#!/usr/bin/env escript
%%! -pa ebin
%%
%% Each IMAGE listed by csp_list has to be its WANT, line for line -- the
%% listing lines of /list, filtered the same way on both sides (a string
%% with a newline in it breaks a line in either).
%% Usage: check.escript IMAGE.img WANT ...

main(Args) ->
    Fails = [Img || {Img, Want} <- pairs(Args), not same(Img, Want)],
    N = length(Args) div 2,
    case Fails of
	[] ->
	    io:format("list: ok -- ~w programs~n", [N]);
	_ ->
	    [io:format("FAIL  list: ~s~n", [F]) || F <- Fails],
	    halt(1)
    end.

pairs([Img, Want | Rest]) -> [{Img, Want} | pairs(Rest)];
pairs([]) -> [].

same(Img, Want) ->
    {ok, W} = file:read_file(Want),
    case csp_list:file(Img) of
	{ok, Text} ->
	    Got = [L || L <- string:split(Text, "\n", all), tagged(L)],
	    Exp = [L || L <- string:split(binary_to_list(W), "\n", all), L =/= ""],
	    Got =:= Exp orelse show(Img, Got, Exp);
	Error ->
	    io:format("  ~s: ~p~n", [Img, Error]),
	    false
    end.

tagged(L) ->
    re:run(L, "  // ([0-9]+ )?[FREPS]!?$", [{capture, none}]) =:= match.

show(Img, Got, Exp) ->
    case [{G, E} || {G, E} <- lists:zip(pad(Got, Exp), pad(Exp, Got)), G =/= E] of
	[{G, E} | _] -> io:format("  ~s~n    got  ~s~n    want ~s~n", [Img, G, E]);
	[] -> ok
    end,
    false.

pad(L, Other) -> L ++ lists:duplicate(max(0, length(Other) - length(L)), "").
