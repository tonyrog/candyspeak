#!/usr/bin/env escript
%%! -pa ebin
%%
%% /dump: a node's ROM slot, EEPROM store and live patch come back byte for
%% byte, and its state, read against the program those make, is what /state
%% says.
%% Usage: check.escript OUT.txt IMAGE.img STORE.db

main([Out, Img, Store]) ->
    {ok, T} = file:read_file(Out),
    Text = binary_to_list(T),
    Ds = csp_node:dumps(Text),
    {ok, ImgBin} = file:read_file(Img),
    {ok, StoreBin} = file:read_file(Store),
    [RomA, Rom] = [B || {rom, B} <- Ds],
    {ok, {_, _, Decls, _}} =
	silent(fun() ->
		       csp_node:program([{rom, Rom} |
					 [D || D = {eeprom, _} <- Ds]])
	       end),
    [St] = [S || S = {state, _, _, _, _} <- Ds],
    Named = csp_node:state(Decls, St),
    Got = lists:sort([{N, V} || {N, V} <- Named, shown(N)]),
    Want = lists:sort(state_rows(Text)),
    {ok, Listed} = csp_list:patch(Rom, StoreBin),
    Checks = [{"/dump rom A is the image /upgrade wrote", RomA =:= ImgBin},
	      {"the ROM and the store list as /list does",
	       lines(Listed) =:= lines(Text)},
	      {"/dump eeprom is the store /save wrote",
	       [B || {eeprom, B} <- Ds] =:= [StoreBin]},
	      {"/dump patch after /save is what was stored",
	       [B || {patch, B} <- Ds] =:= [StoreBin]},
	      {"/dump state reads as /state does",
	       Want =/= [] andalso Got =:= Want},
	      {"a timer comes as its parts, read on the node",
	       [N || {N, _} <- Named, lists:prefix("T", N)] =:=
		   ["T", "T.fired", "T.running", "T.period", "T.elapsed"]
	       andalso proplists:get_value("T.period", Named) =:= 100}],
    case [Name || {Name, false} <- Checks] of
	[] ->
	    io:format("dump: ok -- ~w checks~n", [length(Checks)]);
	Fails ->
	    [io:format("FAIL  dump: ~s~n", [F]) || F <- Fails],
	    io:format("  got  ~p~n  want ~p~n", [Got, Want]),
	    halt(1)
    end.

%% The `Name ... = Value' rows of the /state after the dumps. A State shows
%% as a name and a timer as its period, so those two are left out of both; a
%% timer has a check of its own.
state_rows(Text) ->
    [_, After] = string:split(Text, "\ncycle "),
    [{N, list_to_integer(V)}
     || L <- string:split(After, "\n", all),
	{match, [N, V]} <- [re:run(L, "^(\\S+)\\s.*=\\s*(-?[0-9]+)\\s*$",
				   [{capture, [1, 2], list}])],
	shown(N)].

shown(N) ->
    not (lists:suffix("State", N) orelse lists:prefix("sys.", N) orelse
	 lists:prefix("T", N)).

%% The listing lines, by their tag comment.
lines(Text) ->
    [L || L <- string:split(Text, "\n", all),
	  re:run(L, "  // ([0-9]+ )?[FREPS]!?$", [{capture, none}]) =:= match].

%% The disassembler prints as it goes; keep the test's own output to one line.
silent(F) ->
    {ok, Null} = file:open("/dev/null", [write]),
    Old = group_leader(),
    group_leader(Null, self()),
    try F() after group_leader(Old, self()) end.
