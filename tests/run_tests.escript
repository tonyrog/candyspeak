#!/usr/bin/env escript
%% -*- erlang -*-

main([]) ->
    main(["tests/unit"]);
main([Dir]) ->
    %% Compile csp_test module
    case compile:file("tests/csp_test.erl", [{outdir, "tests"}]) of
        {ok, _} ->
            code:add_path("tests"),
            case csp_test:all([{test_dir, Dir}]) of
                ok -> halt(0);
                error -> halt(1)
            end;
        {error, Errors, _} ->
            io:format("Compile error: ~p~n", [Errors]),
            halt(1)
    end;
main(_) ->
    io:format("Usage: run_tests.escript [test_dir]~n"),
    halt(1).
