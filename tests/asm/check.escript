#!/usr/bin/env escript
%%! -pa ebin
%% csp_asm:roundtrip/1 on every image given: ok, or where it parts.
main(Imgs) ->
    Rs = [{I, csp_asm:roundtrip(I)} || I <- Imgs],
    Bad = [{I, R} || {I, R} <- Rs, R =/= ok],
    [io:format("FAIL  asm: ~s: ~P~n", [I, R, 20]) || {I, R} <- Bad],
    case Bad of
	[] -> io:format("asm: ok -- ~w images round trip~n", [length(Rs)]);
	_ -> halt(1)
    end.
