#!/usr/bin/env escript
%%! -pa ebin
%% csp_asm:roundtrip/1 on every image given: ok, or where it parts. And
%% each written BIG-endian too: it has to say so of itself and read back as
%% the same program.
main(Imgs) ->
    Rs = [{I, csp_asm:roundtrip(I)} || I <- Imgs] ++
	[{I ++ " (big)", big(I)} || I <- Imgs],
    Bad = [{I, R} || {I, R} <- Rs, R =/= ok],
    [io:format("FAIL  asm: ~s: ~P~n", [I, R, 20]) || {I, R} <- Bad],
    case Bad of
	[] -> io:format("asm: ok -- ~w images round trip~n", [length(Rs)]);
	_ -> halt(1)
    end.

big(Img) ->
    {ok, {H, Str, D, C}} = csp_disasm:file(Img),
    {ok, Big} = csp_asm:asm([{header, H#{endian => big}}, {strings, Str} | D ++ C]),
    case csp_disasm:image(Big) of
	{ok, {#{endian := big}, Str, D, C}} -> ok;
	Other -> {big, Other}
    end.
