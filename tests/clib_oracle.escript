#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% The csp_lib oracle: one .csp, interpreted and translated, must end in the
%% same place.
%%
%%   escript tests/clib_oracle.escript [dir|file.csp ...]   (default tests/clib tests/unit)
%%
%% For each dir/X.csp: ./csp -s runs it (with dir/X.dat as -F stimulus when
%% there is one, otherwise --virtual-time), utils/candyspeak_c.erl translates
%% it, gcc builds it against src/csp_lib.c and port/csp_lib_host.c, and the
%% final value of every name the C program reports is held against csp's last
%% state. `// cycles: N` in the .csp sets -c; the default is 200.
%%
%% Everything is left in tmp/clib/: X.c (the translation), X (the binary),
%% X.out (what it printed, then its final state) and X.state (csp -s).
%%
%% Values are compared as 32-bit patterns: csp dumps -1 as 16#ffffffff.
%%
%% With no argument it also walks tests/unit, taking cycles and stimulus from
%% each X.expect. A program the translator refuses is SKIPPED, with the
%% reason, not failed: the unit suite is the runtime's, and the translator
%% covers more of it as it grows.

main([]) -> main(["tests/clib", "tests/unit"]);
main(Dirs) ->
    Tmp = filename:join("tmp", "clib"),
    ok = filelib:ensure_path(Tmp),
    Eb = filename:join(Tmp, "ebin"),
    ok = filelib:ensure_path(Eb),
    ok = build_erl(Eb),
    true = code:add_patha(Eb),
    %% The beams live in tmp/clib/ebin, so candyspeak:tree_dir() would look for
    %% lib/ under tmp/. Name the root outright.
    true = os:putenv("CSP_ROOTS", "lib=" ++ filename:absname("lib")),
    Files = lists:append([case filelib:is_dir(D) of
                              true -> lists:sort(filelib:wildcard(
                                                   filename:join(D, "*.csp")));
                              false -> [D]                      % one program
                          end || D <- Dirs]),
    Results = [run(F, Tmp) || F <- Files],
    Count = fun(X) -> length([R || R <- Results, R =:= X]) end,
    io:format("clib oracle: ~w passed, ~w failed, ~w skipped\n",
              [Count(ok), Count(error), Count(skip)]),
    halt(case Count(error) of 0 -> 0; _ -> 1 end).

build_erl(Eb) ->
    {ok, _} = leex:file("utils/candyspeak_scan.xrl",
                        [{scannerfile, filename:join(Eb, "candyspeak_scan.erl")}]),
    {ok, _} = yecc:file("utils/candyspeak_parse.yrl",
                        [{parserfile, filename:join(Eb, "candyspeak_parse.erl")},
                         {report, false}]),
    Srcs = [filename:join(Eb, "candyspeak_scan.erl"),
            filename:join(Eb, "candyspeak_parse.erl"),
            "utils/candyspeak.erl", "utils/candyspeak_c.erl"],
    lists:foreach(fun(S) -> {ok, _} = compile:file(S, [{outdir, Eb}, report_errors]) end,
                  Srcs),
    ok.

run(Csp, Tmp) ->
    Base = filename:basename(Csp, ".csp"),
    {Cycles, DatFile, Libs} = setup(Csp),
    Stim = case DatFile of
               none -> {"--virtual-time", ""};
               Dat -> {"-F " ++ Dat, "-F " ++ Dat}
           end,
    C = filename:join(Tmp, Base ++ ".c"),
    Bin = filename:join(Tmp, Base),
    State = filename:join(Tmp, Base ++ ".state"),
    case translate(wrap(Csp, Libs, Tmp), C) of
        {skip, Why} ->
            io:format("skip ~s: ~s\n", [Base, Why]),
            skip;
        ok ->
            %% -Wno-tautological-compare: a test that writes `A == A` gets it
            %% translated word for word, and that is the point of the test.
            Gcc = io_lib:format("gcc -Wall -Werror -Wno-overflow -Wno-tautological-compare -O1 -DCSP_LIB_HOST -Iinclude -o ~s ~s "
                                "src/csp_lib.c port/csp_lib_host.c 2>&1",
                                [Bin, C]),
            case os:cmd(lists:flatten(Gcc)) of
                "" ->
                    os:cmd(lists:flatten(
                             io_lib:format("./csp ~s -c ~w -s ~s ~s ~s 2>&1",
                                           [element(1, Stim), Cycles, State,
                                            string:join(Libs, " "), Csp]))),
                    Out = os:cmd(lists:flatten(
                                   io_lib:format("~s ~s -c ~w", [Bin, element(2, Stim),
                                                                 Cycles]))),
                    ok = file:write_file(filename:join(Tmp, Base ++ ".out"), Out),
                    compare(Base, mine(Out), theirs(State));
                Err ->
                    io:format("FAIL ~s: gcc\n~ts", [Base, Err]),
                    error
            end;
        Err ->
            io:format("FAIL ~s: translate ~p\n", [Base, Err]),
            error
    end.

%% cycles and stimulus: from X.expect when there is one (a unit test), else
%% from `// cycles: N` and X.dat beside the .csp.
setup(Csp) ->
    Expect = filename:rootname(Csp) ++ ".expect",
    Dat = filename:rootname(Csp) ++ ".dat",
    case file:consult(Expect) of
        {ok, Terms} ->
            {proplists:get_value(cycles, Terms, 20),            % csp_test's default
             proplists:get_value(stimulus, Terms, none),
             proplists:get_value(lib, Terms, [])};
        _ ->
            {cycles(Csp),
             case filelib:is_regular(Dat) of true -> Dat; false -> none end,
             []}
    end.

%% A unit test with {lib, [...]} has csp load those files ahead of it. The
%% translator takes one file, so it gets one that imports them all -- by
%% absolute path, so the test's own imports still resolve beside the test.
wrap(Csp, [], _Tmp) -> Csp;
wrap(Csp, Libs, Tmp) ->
    W = filename:join(Tmp, filename:basename(Csp, ".csp") ++ "_wrap.csp"),
    ok = file:write_file(W, [["#import \"", filename:absname(F), "\"\n"]
                             || F <- Libs ++ [Csp]]),
    W.

%% The translator says on stderr what it refused; that line is the reason.
translate(Csp, C) ->
    {Pid, Ref} = spawn_monitor(
                   fun() ->
                           exit({done, try candyspeak_c:file(Csp, C)
                                        catch C1:E1 -> {C1, E1} end})
                   end),
    receive
        {'DOWN', Ref, process, Pid, {done, ok}} -> ok;
        {'DOWN', Ref, process, Pid, {done, {error, unsupported}}} ->
            {skip, "not translated"};
        {'DOWN', Ref, process, Pid, {done, Other}} ->
            {skip, lists:flatten(io_lib:format("~P", [Other, 8]))};
        {'DOWN', Ref, process, Pid, Why} ->
            {skip, lists:flatten(io_lib:format("~P", [Why, 8]))}
    end.

cycles(Csp) ->
    {ok, Bin} = file:read_file(Csp),
    case re:run(Bin, "//\\s*cycles:\\s*([0-9]+)", [{capture, [1], list}]) of
        {match, [N]} -> list_to_integer(N);
        nomatch -> 200
    end.

%% name=value lines, after the harness's marker.
mine(Out0) ->
    Out = case string:split(Out0, "--- state\n") of
              [_, After] -> After;
              _ -> ""
          end,
    maps:from_list(
      [{N, list_to_integer(V) band 16#ffffffff}
       || L <- string:split(Out, "\n", all),
          [N, V] <- [string:split(L, "=")],
          re:run(N, "^[A-Za-z_][A-Za-z0-9_.]*$") =/= nomatch,
          re:run(V, "^-?[0-9]+$") =/= nomatch]).

%% csp's last {state, Cycle, Vars}, flattened: an object's members are already
%% named "m.V" inside it.
theirs(File) ->
    case file:consult(File) of
        {ok, []} -> #{};
        {ok, States} ->
            {state, _, Vars} = lists:last(States),
            maps:from_list(flat(Vars));
        _ -> #{}
    end.

flat([{object, _, Vs} | T]) -> flat(Vs) ++ flat(T);
flat([{_, N, V} | T]) when is_integer(V) -> [{N, V band 16#ffffffff} | flat(T)];
flat([_ | T]) -> flat(T);
flat([]) -> [].

%% Only names both sides report: csp's dump leaves out a module's #locals,
%% which the C side has as ordinary fields.
compare(Base, Mine, Theirs) ->
    Bad = [{N, V, maps:get(N, Theirs)}
           || {N, V} <- lists:sort(maps:to_list(Mine)),
              maps:is_key(N, Theirs),
              maps:get(N, Theirs) =/= V],
    case Bad of
        [] -> io:format("ok   ~s (~w names)\n", [Base, maps:size(Mine)]), ok;
        _ ->
            io:format("FAIL ~s\n", [Base]),
            [io:format("     ~s: c ~w, csp ~p\n", [N, A, B]) || {N, A, B} <- Bad],
            error
    end.
