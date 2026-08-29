-module(csp_test).
-export([all/0, all/1, run/1, run/2]).
-export([parse_test/1, eval_test/1, eval_test/2, eval_test/3]).
-export([get_var/2, get_var/3, get_object_var/3, get_object_var/4]).
-export([state_at/2, last_state/1]).

-define(CSP, "./csp").
-define(TIMEOUT, 5000).

%% Run all tests
all() ->
    all([]).

all(Opts) ->
    TestDir = proplists:get_value(test_dir, Opts, "tests/unit"),
    {ok, Files} = file:list_dir(TestDir),
    CspFiles = [F || F <- Files, filename:extension(F) == ".csp"],
    Results = [run_test(filename:join(TestDir, F), Opts) || F <- lists:sort(CspFiles)],
    summarize(Results).

%% Run a single test file
run(File) ->
    run(File, []).

run(File, Opts) ->
    case run_test(File, Opts) of
        {pass, Name, _} ->
            io:format("PASS: ~s~n", [Name]),
            ok;
        {fail, Name, Reason} ->
            io:format("FAIL: ~s~n  Reason: ~p~n", [Name, Reason]),
            error;
        {skip, Name, Reason} ->
            io:format("SKIP: ~s (~s)~n", [Name, Reason]),
            skip
    end.

%% Parse test - check that file parses without error
parse_test(File) ->
    TmpParse = tmp_file("parse"),
    Cmd = io_lib:format("~s -n -p ~s ~s 2>&1", [?CSP, TmpParse, File]),
    case os:cmd(lists:flatten(Cmd)) of
        [] ->
            case file:consult(TmpParse) of
                {ok, Terms} ->
                    file:delete(TmpParse),
                    {ok, Terms};
                {error, Reason} ->
                    file:delete(TmpParse),
                    {error, {invalid_erlang, Reason}}
            end;
        Output ->
            file:delete(TmpParse),
            {error, {parse_error, Output}}
    end.

%% Eval test - run and get state/result
eval_test(File) ->
    eval_test(File, [], seq).

eval_test(File, Opts) ->
    eval_test(File, Opts, seq).

%% Mode: seq (default) or reactive (-r is now a no-argument flag)
eval_test(File, Opts, Mode) ->
    Cycles = proplists:get_value(cycles, Opts, 20),
    TmpState = tmp_file("state"),
    RFlag = case Mode of reactive -> "-r "; _ -> "" end,
    %% {lib, ["lib/foo.csp"]} loads modules ahead of the test file, so a test
    %% exercises the library module ITSELF rather than a copy of it that
    %% quietly drifts. csp takes several files and parses them in order.
    Libs = proplists:get_value(lib, Opts, []),
    LibStr = case Libs of
                 [] -> "";
                 _  -> string:join(Libs, " ") ++ " "
             end,
    %% {virtual_time, true} makes the clock JUMP to the next timer deadline
    %% instead of sleeping. A test whose values are driven by timeout() is
    %% otherwise at the mercy of the loop's sleep policy -- and a program that
    %% never settles (an unguarded counter) starves the clock completely, so the
    %% timer never fires at all. With it, a run is deterministic and instant.
    VFlag = case proplists:get_value(virtual_time, Opts, false) of
                true -> "--virtual-time ";
                _    -> ""
            end,
    %% {stimulus, "foo.dat"} feeds inputs the way the hardware would: rows of
    %% `<time_ms> <var>=<value> ...` against the virtual clock, applied when the
    %% clock reaches them. This is the right way to drive a sensor.
    %%
    %% The alternative -- writing `Sensor = 500 ? n >= 20` as a rule in the test
    %% program -- puts the stimulus INSIDE the thing under test: it becomes one
    %% more rule competing for evaluation order, it forces a cycle counter that
    %% the real program does not have, and every reading is then a fact about
    %% rule scheduling rather than about time. -F keeps the two apart.
    %%
    %% It implies the virtual clock, so {virtual_time, true} is not also needed.
    SFlag = case proplists:get_value(stimulus, Opts, none) of
                none -> "";
                SF   -> "-F " ++ SF ++ " "
            end,
    Cmd = io_lib:format("~s ~s~s~s-c ~p -s ~s -R ~s~s 2>&1",
                        [?CSP, RFlag, VFlag, SFlag, Cycles, TmpState,
                         LibStr, File]),
    _Output = os:cmd(lists:flatten(Cmd)),
    StateResult = file:consult(TmpState),
    file:delete(TmpState),
    case StateResult of
        {ok, States} ->
            {ok, States};
        {error, R1} ->
            {error, {state_file, R1}}
    end.

%% Helper: get variable value from state
get_var(Name, State) when is_list(State) ->
    %% accept any leaf tag: var, digital, analog, timer, ...
    case lists:keyfind(Name, 2, State) of
        {object, Name, _} -> {error, not_found};
        {_Tag, Name, Value} -> {ok, Value};
        false -> {error, not_found}
    end;
get_var(Name, {state, _Cycle, Vars}) ->
    get_var(Name, Vars).

get_var(Name, State, Default) ->
    case get_var(Name, State) of
        {ok, Value} -> Value;
        {error, _} -> Default
    end.

%% Helper: get object variable value from state
get_object_var(ObjName, VarName, State) when is_list(State) ->
    case lists:keyfind(ObjName, 2, State) of
        {object, ObjName, ObjVars} ->
            get_var(VarName, ObjVars);
        false ->
            {error, object_not_found}
    end;
get_object_var(ObjName, VarName, {state, _Cycle, Vars}) ->
    get_object_var(ObjName, VarName, Vars).

get_object_var(ObjName, VarName, State, Default) ->
    case get_object_var(ObjName, VarName, State) of
        {ok, Value} -> Value;
        {error, _} -> Default
    end.

%% Helper: get state at specific cycle
state_at(Cycle, Data) ->
    case lists:keyfind(Cycle, 2, Data) of
        {state, Cycle, _} = S -> {ok, S};
        false -> {error, not_found}
    end.

%% Helper: get last state
last_state([]) ->
    {state, -1, []};
last_state([S]) when element(1,S) =:= state ->
    S;
last_state([S,R]) when element(1,S) =:= state, element(1,R) =:= result ->
    S;
last_state([_|State]) ->
    last_state(State).

%% Internal: run a test and check expectations
run_test(File, Opts) ->
    Name = filename:basename(File, ".csp"),
    ExpectFile = filename:rootname(File) ++ ".expect",
    case filelib:is_file(ExpectFile) of
        true ->
            run_with_expect(File, ExpectFile, Name, Opts);
        false ->
            %% No expect file - just check it parses and runs
            run_basic(File, Name, Opts)
    end.

run_basic(File, Name, _Opts) ->
    case parse_test(File) of
        {ok, _} ->
            case eval_test(File) of
                {ok, _} -> {pass, Name, basic};
                {error, R} -> {fail, Name, {eval, R}}
            end;
        {error, R} ->
            {fail, Name, {parse, R}}
    end.

run_with_expect(File, ExpectFile, Name, _Opts) ->
    case file:consult(ExpectFile) of
        {ok, Expects} ->
            %% Run each requested mode; default is sequential only. A test opts
            %% into reactive with {modes, [seq, reactive]}. All modes must pass.
            Modes = proplists:get_value(modes, Expects, [seq]),
            run_modes(Modes, File, Expects, Name);
        {error, R} ->
            {fail, Name, {expect_file, R}}
    end.

run_modes(Modes, File, Expects, Name) ->
    Results = [run_mode(M, File, Expects, Name) || M <- Modes],
    case [F || {fail, _, _} = F <- Results] of
        []          -> {pass, Name, Modes};
        [First | _] -> First
    end.

run_mode(Mode, File, Expects, Name) ->
    MName = lists:flatten(io_lib:format("~s[~s]", [Name, Mode])),
    case eval_test(File, Expects, Mode) of
        {ok, Data} ->
            check_expectations(MName, Expects, Data);
        {error, R} ->
            {fail, MName, {eval, R}}
    end.

check_expectations(Name, Expects, Data) ->
    Checks = proplists:get_value(checks, Expects, []),
    case run_checks(Checks, Data, []) of
        [] -> {pass, Name, Checks};
        Failures -> {fail, Name, Failures}
    end.

run_checks([], _Data, Failures) ->
    lists:reverse(Failures);
run_checks([Check | Rest], Data, Failures) ->
    case run_check(Check, Data) of
        ok ->
            run_checks(Rest, Data, Failures);
        {fail, Reason} ->
            run_checks(Rest, Data, [{Check, Reason} | Failures])
    end.

run_check({cycles, Expected}, Data) ->
    Props = proplists:get_value(result, Data, []),
    case proplists:get_value(cycle, Props) of
        Expected -> ok;
        Got -> {fail, {expected, Expected, got, Got}}
    end;

run_check({result_value, Expected}, Data) ->
    Props = proplists:get_value(result, Data, []),
    case proplists:get_value(value, Props) of
        Expected -> ok;
        Got -> {fail, {expected, Expected, got, Got}}
    end;

run_check({var, Cycle, Name, Expected}, Data) ->
    case state_at(Cycle, Data) of
        {ok, State} ->
            case get_var(Name, State) of
                {ok, Expected} -> ok;
                {ok, Got} -> {fail, {expected, Expected, got, Got}};
                {error, R} -> {fail, R}
            end;
        {error, R} ->
            {fail, R}
    end;

run_check({object_var, Cycle, Obj, Var, Expected}, Data) ->
    case state_at(Cycle, Data) of
        {ok, State} ->
            case get_object_var(Obj, Var, State) of
                {ok, Expected} -> ok;
                {ok, Got} -> {fail, {expected, Expected, got, Got}};
                {error, R} -> {fail, R}
            end;
        {error, R} ->
            {fail, R}
    end;

run_check({final_var, Name, Expected}, Data) ->
    State = last_state(Data),
    case get_var(Name, State) of
        {ok, Expected} -> ok;
        {ok, Got} -> {fail, {expected, Expected, got, Got}};
        {error, R} -> {fail, R}
    end;

run_check({final_object_var, Obj, Var, Expected}, Data) ->
    State = last_state(Data),
    case get_object_var(Obj, Var, State) of
        {ok, Expected} -> ok;
        {ok, Got} -> {fail, {expected, Expected, got, Got}};
        {error, R} -> {fail, R}
    end;

run_check(Unknown, _Data) ->
    {fail, {unknown_check, Unknown}}.

%% Summarize results
summarize(Results) ->
    Pass = length([R || {pass, _, _} = R <- Results]),
    Fail = length([R || {fail, _, _} = R <- Results]),
    Skip = length([R || {skip, _, _} = R <- Results]),
    io:format("~n========================================~n"),
    io:format("Pass: ~p, Fail: ~p, Skip: ~p~n", [Pass, Fail, Skip]),
    lists:foreach(fun({fail, Name, Reason}) ->
                          io:format("FAIL: ~s~n  ~p~n", [Name, Reason]);
                     (_) -> ok
                  end, Results),
    case Fail of
        0 -> ok;
        _ -> error
    end.

%% Temp file helper
tmp_file(Prefix) ->
    N = erlang:unique_integer([positive]),
    lists:flatten(io_lib:format("/tmp/csp_~s_~p.erl", [Prefix, N])).
