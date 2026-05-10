-module(csp_test).
-export([all/0, all/1, run/1, run/2]).
-export([parse_test/1, eval_test/1, eval_test/2]).
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
    eval_test(File, []).

eval_test(File, Opts) ->
    Cycles = proplists:get_value(cycles, Opts, 20),
    TmpState = tmp_file("state"),
    TmpResult = tmp_file("result"),
    Cmd = io_lib:format("~s -c ~p -s ~s -R ~s ~s 2>&1",
                        [?CSP, Cycles, TmpState, TmpResult, File]),
    _Output = os:cmd(lists:flatten(Cmd)),
    StateResult = file:consult(TmpState),
    ResultResult = file:consult(TmpResult),
    file:delete(TmpState),
    file:delete(TmpResult),
    case {StateResult, ResultResult} of
        {{ok, States}, {ok, [Result]}} ->
            {ok, #{states => States, result => Result}};
        {{ok, []}, {ok, [Result]}} ->
            {ok, #{states => [], result => Result}};
        {{ok, States}, {ok, []}} ->
            {ok, #{states => States, result => undefined}};
        {{ok, []}, {ok, []}} ->
            {ok, #{states => [], result => undefined}};
        {{error, R1}, _} ->
            {error, {state_file, R1}};
        {_, {error, R2}} ->
            {error, {result_file, R2}}
    end.

%% Helper: get variable value from state
get_var(Name, State) when is_list(State) ->
    case lists:keyfind(Name, 2, State) of
        {var, Name, Value} -> {ok, Value};
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
        {object, ObjName, _Module, ObjVars} ->
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
state_at(Cycle, #{states := States}) ->
    case lists:keyfind(Cycle, 2, States) of
        {state, Cycle, _} = S -> {ok, S};
        false -> {error, not_found}
    end;
state_at(Cycle, States) when is_list(States) ->
    state_at(Cycle, #{states => States}).

%% Helper: get last state
last_state(#{states := []}) ->
    {state, -1, []};
last_state(#{states := States}) ->
    lists:last(States);
last_state([]) ->
    {state, -1, []};
last_state(States) when is_list(States) ->
    lists:last(States).

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
            case eval_test(File, Expects) of
                {ok, Data} ->
                    check_expectations(Name, Expects, Data);
                {error, R} ->
                    {fail, Name, {eval, R}}
            end;
        {error, R} ->
            {fail, Name, {expect_file, R}}
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

run_check({cycles, Expected}, #{result := {result, Props}}) ->
    case proplists:get_value(cycle, Props) of
        Expected -> ok;
        Got -> {fail, {expected, Expected, got, Got}}
    end;

run_check({result_value, Expected}, #{result := {result, Props}}) ->
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
