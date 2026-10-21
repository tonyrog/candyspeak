#!/usr/bin/env escript
%%! -pa ebin
%%
%% csp_disasm on an image with every kind of array: each array is one entry, the
%% way the source declares it, and the continuations it is stored as do not
%% show. Usage: check.escript IMAGE.img

main([Img]) ->
    {ok, {_Header, _Strings, Ds, _Code}} =
	silent(fun() -> csp_disasm:file(Img) end),
    Fails = [Name || {Name, false} <- checks(Ds)],
    case Fails of
	[] ->
	    io:format("disasm: ok -- ~w checks~n", [length(checks(Ds))]);
	_ ->
	    [io:format("FAIL  disasm: ~s~n", [F]) || F <- Fails],
	    [io:format("  ~p~n", [D]) || D <- Ds],
	    halt(1)
    end.

checks(Ds) ->
    [{"digital array: one entry, a pin per element",
      opt(Ds, "D", len) =:= 3 andalso
      opt(Ds, "D", elems) =:= [[{pin,4}],[{pin,5}],[{pin,6}]]},
     {"analog array: one entry, a pin per element",
      opt(Ds, "A", len) =:= 2 andalso
      opt(Ds, "A", elems) =:= [[{pin,1}],[{pin,2}]]},
     {"variable array: identical elements, no elems",
      opt(Ds, "V", len) =:= 4 andalso opt(Ds, "V", init) =:= 7 andalso
      opt(Ds, "V", elems) =:= none},
     {"param array",
      kind(Ds, "Q") =:= param andalso opt(Ds, "Q", len) =:= 2},
     {"constant array: a value per element",
      opt(Ds, "K", len) =:= 3 andalso
      opt(Ds, "K", elems) =:= [[{init,1}],[{init,2}],[{init,3}]]},
     {"a scalar after an array is its own entry",
      opt(Ds, "S", len) =:= none andalso opt(Ds, "S", init) =:= 9},
     {"array inside a module",
      opt(Ds, "W", len) =:= 2 andalso opt(Ds, "X", len) =:= none},
     {"the entry after an array is numbered past its elements",
      index(Ds, "A") =:= index(Ds, "D") + 3},
     {"no nameless entry and no cont left",
      [D || D <- Ds, name(D) =:= []] =:= [] andalso
      [D || D <- Ds, lists:keymember(cont, 1, opts(D))] =:= []}].

find(Ds, Name) ->
    B = list_to_binary(Name),
    hd([D || D <- Ds, tuple_size(D) >= 5, element(4, D) =:= B] ++ [none]).

opt(Ds, Name, K) ->
    case lists:keyfind(K, 1, opts(find(Ds, Name))) of
	{K, V} -> V;
	false  -> none
    end.

kind(Ds, Name)  -> element(3, find(Ds, Name)).
index(Ds, Name) -> element(2, find(Ds, Name)).

name(D) when tuple_size(D) >= 5 -> element(4, D);
name(_)                         -> undefined.

opts(none) -> [];
opts(D) ->
    case element(tuple_size(D), D) of
	L when is_list(L) -> L;
	_                 -> []
    end.

%% The disassembler prints as it goes; keep the test's own output to one line.
silent(F) ->
    {ok, Null} = file:open("/dev/null", [write]),
    Old = group_leader(),
    group_leader(Null, self()),
    try F() after group_leader(Old, self()) end.
