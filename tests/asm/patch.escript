#!/usr/bin/env escript
%%! -pa ebin
%% See patch.sh. Dir holds out.txt (the node's /dump rom, /dump eeprom and
%% /list) and ee.db (the store /save wrote).
main([Dir]) ->
    {ok, Out} = file:read_file(filename:join(Dir, "out.txt")),
    Ds = csp_node:dumps(binary_to_list(Out)),
    [Rom | _] = [B || {rom, B} <- Ds],
    {ok, Store} = file:read_file(filename:join(Dir, "ee.db")),
    Rom1 = filename:join(Dir, "run.img"),
    ok = file:write_file(Rom1, Rom),
    {ok, Terms} = csp_asm:patch_terms(Rom, Store),
    %% the change: rule 1 (the module's) off instead of 2
    Terms1 = [case T of {disabled, _} -> {disabled, [1]}; _ -> T end
	      || T <- Terms],
    {ok, Store2} = csp_asm:patch(Rom, Terms1),
    ok = file:write_file(filename:join(Dir, "ee2.db"), Store2),
    Listed = os:cmd("printf '/list\\n/quit\\n' | " ++ Dir ++ "/node -i -e " ++
		    Dir ++ "/ee2.db 2>&1"),
    Off = [L || L <- string:split(Listed, "\n", all),
		{match, _} <- [re:run(L, "  // [0-9]+ [A-Z]!$")]],
    Checks =
	[{"the store /save wrote comes back byte for byte",
	  csp_asm:patch_roundtrip(Rom1, filename:join(Dir, "ee.db")) =:= ok},
	 {"it carries the setting, the #disable and the patch",
	  proplists:get_value(disabled, Terms) =:= [2] andalso
	      proplists:get_value(settings, Terms) =/= <<>> andalso
	      length([D || D <- Terms, element(1, D) =:= decl]) >= 3},
	 {"a store csp_asm builds loads, and says what it was built to",
	  length(Off) =:= 1 andalso
	      string:find(hd(Off ++ [""]), "N=N+Step  // 1 F!") =/= nomatch}],
    case [N || {N, false} <- Checks] of
	[] -> io:format("asm patch: ok -- ~w checks~n", [length(Checks)]);
	Bad ->
	    [io:format("FAIL  asm patch: ~s~n", [B]) || B <- Bad],
	    io:format("~s~n", [Listed]),
	    halt(1)
    end.
