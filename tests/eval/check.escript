#!/usr/bin/env escript
%% -*- erlang -*-
%%! -pa ebin
%%
%% candyspeak_eval:cycle/4 against csp, cycle by cycle, in each mode the
%% program's X.expect runs it in ({modes, [seq, react]}; seq when it says
%% nothing).
%%
%%   escript tests/eval/check.escript [dir|file.csp ...]   (default tests/unit)
%%
%% csp runs the program (-Lerlang -Q --fired, the stimulus and cycles from
%% X.expect as tests/clib_oracle.escript takes them) and dumps every cycle.
%% Each cycle is then run again in Erlang from csp's state before it, with
%% the inputs csp sampled: every value cycle/3 knows must be the one csp
%% dumped after it, and the rules it says fired must be the ones csp says
%% fired, an object's by object. What cycle/4 cannot know -- a timer firing,
%% a call, a buffer -- it says is unknown, and that is not compared.
%%
%% A cycle is judged on its whole state; a program on all its cycles.

main([]) -> main(["tests/unit"]);
main(Dirs) ->
    true = os:putenv("CSP_ROOTS", "lib=" ++ filename:absname("lib")),
    Files = lists:append([case filelib:is_dir(D) of
                              true -> lists:sort(filelib:wildcard(
                                                   filename:join(D, "*.csp")));
                              false -> [D]
                          end || D <- Dirs]),
    Rs = [R || F <- Files, R <- run(F)],
    N = fun(X) -> length([R || R <- Rs, R =:= X]) end,
    Cs = lists:sum([C || {ok, C} <- Rs]),
    io:format("eval oracle: ~w programs agree (~w cycles, ~w values, ~w unknown), "
              "~w differ, ~w skipped\n",
              [length([ok || {ok, _} <- Rs]), Cs, count(known), count(unknown),
               N(error), N(skip)]),
    halt(case N(error) of 0 -> 0; _ -> 1 end).

run(Csp) ->
    {_, _, _, Modes} = setup(Csp),
    [run(Csp, M) || M <- Modes].

run(Csp, Mode) ->
    Base = filename:basename(Csp, ".csp") ++ case Mode of
                                                 seq -> "";
                                                 reactive -> " (reactive)"
                                             end,
    {End, Dat, Libs, _} = setup(Csp),
    Ends = case End of
               {cycles, C} -> io_lib:format("-c ~w", [C]);
               {time, Ms} -> io_lib:format("-c 0 -T ~w", [Ms])
           end,
    Stim = case Dat of
               none -> "--virtual-time";
               _    -> "-F " ++ Dat
           end,
    Out = os:cmd(lists:flatten(
                   io_lib:format("./csp -Lerlang -Q --fired ~s ~s ~s ~s ~s 2>/dev/null",
                                 [case Mode of reactive -> "-r"; seq -> "" end,
                                  Stim, Ends, string:join(Libs, " "), Csp]))),
    Seq = os:cmd(lists:flatten(
                   io_lib:format("./csp -Lerlang -Q --fired ~s ~s ~s ~s 2>/dev/null",
                                 [Stim, Ends, string:join(Libs, " "), Csp]))),
    case parse(Csp, Libs) of
        {ok, _} when Mode =:= reactive, Out =:= Seq ->
            %% A program loaded from a file is ROM, and without a graph of its
            %% own csp_cycle runs ROM sequentially even with -r: no reactive
            %% run to hold cycle/4's against.
            io:format("skip ~s: csp -r ran it sequentially\n", [Base]),
            skip;
        {ok, Ast} ->
            P = candyspeak_eval:prog(Ast, Csp),
            judge(Base, P, ins(P, Dat), trace(Out),
                  Mode);
        _ ->
            io:format("skip ~s: does not parse\n", [Base]),
            skip
    end.

%% The libraries a unit test loads ahead of it, then the test, as one tree.
parse(Csp, Libs) ->
    Ts = [{F, candyspeak:parse(F, [sources])} || F <- Libs ++ [Csp]],
    case [[{source, 0, F} | A] || {F, {ok, A}} <- Ts] of
        As when length(As) =:= length(Ts) -> {ok, lists:append(As)};
        _ -> error
    end.

setup(Csp) ->
    case file:consult(filename:rootname(Csp) ++ ".expect") of
        {ok, Ts} ->
            {case proplists:get_value(time, Ts) of
                 undefined -> {cycles, proplists:get_value(cycles, Ts, 20)};
                 Ms -> {time, Ms}
             end,
             proplists:get_value(stimulus, Ts, none),
             proplists:get_value(lib, Ts, []),
             [M || M <- proplists:get_value(modes, Ts, [seq]),
                   M =:= seq orelse M =:= reactive]};
        _ -> {{cycles, 20}, none, [], [seq]}
    end.

%% What csp samples: the program's inputs, and whatever the stimulus file
%% writes.
ins(#{inputs := Pins}, Dat) ->
    Stim = case Dat of
               none -> [];
               _ ->
                   {ok, B} = file:read_file(Dat),
                   [N || L <- string:split(binary_to_list(B), "\n", all),
                         {match, [N]} <- [re:run(L, "^\\s*[0-9]+\\s+([A-Za-z_][A-Za-z0-9_.\\[\\]]*)\\s*=",
                                                 [{capture, [1], list}])]]
           end,
    lists:usort(Pins ++ Stim).

%% [{Cycle, State, Fired}] from csp's terms. A state or a fired set is
%% printed only when it changes, so each carries on until the next.
trace(Out) ->
    Ts = [T || L <- string:split(Out, "\n", all), {ok, T} <- [term(L)]],
    walk(Ts, #{}, [], []).

walk([{state, C, Vs} | T], _S, F, Acc) -> walk(T, flat(Vs, #{}), F, at(C, Acc, flat(Vs, #{}), F));
walk([{fired, C, Rs} | T], S, _F, Acc) ->
    F = [{O, filename:basename(Fl), Ln} || {O, Fl, Ln} <- Rs],
    walk(T, S, F, at(C, Acc, S, F));
walk([{clock, C, Ms} | T], S, F, Acc) ->
    put({tick, C}, Ms),                             % what tick() read in C
    walk(T, S, F, at(C, Acc, S, F));
walk([_ | T], S, F, Acc) -> walk(T, S, F, Acc);
walk([], _S, _F, Acc) -> lists:reverse(Acc).

%% The latest word on cycle C.
at(C, [{C, _, _} | Acc], S, F) -> [{C, S, F} | Acc];
at(C, Acc, S, F) -> [{C, S, F} | Acc].

flat([{object, _, Vs} | T], M) -> flat(T, flat(Vs, M));
flat([{_, N, V} | T], M) when is_number(V) -> flat(T, M#{N => V});
flat([_ | T], M) -> flat(T, M);
flat([], M) -> M.

term(L) ->
    case erl_scan:string(L) of
        {ok, Toks, _} when Toks =/= [] -> erl_parse:parse_term(Toks);
        _ -> error
    end.

judge(Base, _P, _Ins, [], _Mode) ->
    io:format("skip ~s: no trace\n", [Base]),
    skip;
judge(Base, P, Ins, Tr, Mode) ->
    Steps = lists:zip(lists:droplast(Tr), tl(Tr)),
    %% reactive: what the cycle before changed is what wakes this one; the
    %% Erlang cycle's own word on it, carried along
    %% A #local is not in csp's dump: what the Erlang cycle had for one is
    %% carried on to the next, under csp's state.
    {Bad0, _, _} = lists:foldl(
                  fun({{Ca, Din, Fa}, B = {Cb, _, _}}, {Acc, Woken, Ls}) when Cb =:= Ca + 1 ->
                          {Bs, W, Vs} = step(P, Ins, {Ca, maps:merge(Ls, Din), Fa}, B,
                                             #{mode => Mode, woken => Woken}),
                          {Acc ++ Bs, W, maps:with(slots(P), Vs)};
                     (_, {Acc, _, _}) -> {Acc, [], #{}}
                  end, {[], [], #{}}, Steps),
    Bad = Bad0,
    case Bad of
        [] -> {ok, length(Steps)};
        [{C, Why} | _] ->
            io:format("FAIL ~s: cycle ~w: ~ts~s\n",
                      [Base, C, Why, case length(Bad) of
                                         1 -> "";
                                         K -> io_lib:format(" (and ~w more)", [K - 1])
                                     end]),
            error
    end.

step(P = #{env := #{sigs := Sigs}}, Ins, {C0, Din, _}, {C, Dout, Fired}, Opts) ->
    In0 = maps:merge(#{seed => C0 =:= 0, cycle => C},
                     maps:with(Ins, Dout)),
    In = case get({tick, C}) of
             undefined -> In0;
             Ms -> In0#{tick => Ms}
         end,
    #{values := Vs, fired := F, unsure := U, changed := Woke} =
        candyspeak_eval:cycle(P, Din, In, Opts),
    Bits = fun(_, V) when is_float(V) -> V;
              (N, V) ->
                   {W, _} = maps:get(N, Sigs, {32, true}),
                   V band ((1 bsl W) - 1)
           end,
    Diff = [io_lib:format("~s is ~w, csp has ~w", [N, Bits(N, V), Bits(N, X)])
            || {N, V} <- lists:sort(maps:to_list(Vs)), is_number(V),
               {ok, X} <- [maps:find(N, Dout)], not same(Bits(N, V), Bits(N, X))],
    Base = fun(L) -> [{O, filename:basename(Fl), Ln} || {O, Fl, Ln} <- L] end,
    Mine = lists:sort(Base(F)),
    Sure = lists:sort(Fired -- Base(U)),
    Fd = case Mine =:= Sure of
             true -> [];
             false -> [io_lib:format("fired ~tp, csp fired ~tp", [Mine -- Sure, Sure -- Mine])]
         end,
    tally(known, length([V || {N, V} <- maps:to_list(Vs), is_number(V),
                              maps:is_key(N, Dout)])),
    tally(unknown, length([V || {N, V} <- maps:to_list(Vs), V =:= unknown,
                                maps:is_key(N, Dout), maps:is_key(N, Sigs)])),
    {[{C, lists:flatten(lists:join("; ", Diff ++ Fd))} || Diff ++ Fd =/= []], Woke, Vs}.

slots(#{env := #{slots := S}}) -> maps:keys(S).

%% csp prints a float with six decimals: equal to that.
same(A, B) when is_float(A); is_float(B) -> abs(A - B) < 0.0000005 + abs(B) * 1.0e-6;
same(A, B) -> A =:= B.

%% Values held against csp, and variables and pins cycle/4 said it could not
%% know (a buffer or a timer is not counted).
tally(K, N) -> put(K, count(K) + N).

count(K) -> case get(K) of undefined -> 0; N -> N end.
