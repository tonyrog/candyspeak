%% Drives the panel against the recording wse and a real csp process.
%%
%%   make test
%%
%% Checks the three things the loop is for: that the widgets are DERIVED from
%% the declarations, that a click reaches csp and comes back as a lit lamp, and
%% that the trace keeps sampling when nothing changes.

-module(panel_test).

-export([run/0]).
-export([logger/1]).

-define(SETTLE, 600).

run() ->
    register(wse_log, spawn(?MODULE, logger, [[]])),
    File = filename:join([code:lib_dir(), "..", "demo", "gate.csp"]),
    Demo = case filelib:is_regular(File) of
	       true  -> File;
	       false -> "demo/gate.csp"
	   end,
    {ok, Ast} = candyspeak:parse(Demo),

    %% 1. derivation
    Widgets = csp_panel:widgets(Ast),
    expect("widgets derived from declarations",
	   Widgets =:= [{toggle, "Button", 1}, {lamp, "Led", 1}], Widgets),

    P = track_panel(spawn(fun() -> csp_panel:run(fake_ws, "panel", Demo) end)),
    timer:sleep(?SETTLE),
    Before = calls(),

    %% 2. a click reaches csp and comes back
    P ! {notify, 1, local, "Button"},
    timer:sleep(?SETTLE),
    After = calls(),
    %% Led has no colour in its name, so it is red -- see csp_panel:hue/1
    Lit = [X || X <- After -- Before, string:find(X, "#f44") =/= nomatch],
    expect("click lights the lamp", Lit =/= [], length(Lit)),

    %% 3. releasing it puts the lamp out again -- demo/gate.csp assigns every
    %%    cycle, so Led follows Button down. (demo/latch.csp does not, on
    %%    purpose: a guarded rule does nothing when its guard is false.)
    Mid = calls(),
    P ! {notify, 1, local, "Button"},
    timer:sleep(?SETTLE),
    Out = [X || X <- calls() -- Mid, string:find(X, "#251515") =/= nomatch],
    expect("releasing it puts the lamp out", Out =/= [], length(Out)),

    %% 4. the trace keeps moving after the program settles
    Quiet0 = length(samples(calls())),
    timer:sleep(?SETTLE),
    Quiet1 = length(samples(calls())),
    expect("trace samples while nothing changes", Quiet1 > Quiet0,
	   {Quiet0, Quiet1}),

    %% 5. picking another program tears the panel down and rebuilds it from
    %%    that file. demo/latch.csp has the same two pins, so the widgets match
    %%    but the behaviour does not: a guarded rule leaves the lamp lit.
    SelId = first_event(),
    Before5 = calls(),
    P ! {notify, SelId, local, latch_file()},
    timer:sleep(?SETTLE),
    New = calls() -- Before5,
    expect("picking another file rebuilds the panel",
	   [X || X <- New, string:find(X, "createElement select") =/= nomatch]
	   =/= [], length(New)),

    P ! stop,
    timer:sleep(200),

    %% 6. analog: the width comes from the declaration, and port 9 is the CPX
    %%    pixel strip rather than a magnitude.
    analog_checks(),

    %% 6b. the lamp colour comes from the name, and an unlabelled one is red
    Hues = [{N, element(1, csp_panel:hue(N))}
	    || N <- ["Red","Yellow","Green","Led","Blue","GronLampa"]],
    expect("lamp colour taken from the name",
	   Hues =:= [{"Red","#f44"}, {"Yellow","#fd3"}, {"Green","#3f3"},
		     {"Led","#f44"}, {"Blue","#5af"}, {"GronLampa","#3f3"}],
	   Hues),

    %% 6c. an array declaration is one widget per element, and the dump's
    %%     unnamed entries are numbered back onto it. cpx_rotate.csp declares
    %%     its strip as `#analog P[10]:16` and had no pixels at all before.
    array_check(),

    %% 6d. home.csp's invariants actually hold. Both of these failed in the
    %%     first draft: the window check sat outside any #in block, so it ran
    %%     only in INIT and NORMAL and never in Home.
    home_checks(),

    %% 6e. the __Colour suffix names a colour outright and is hidden from the
    %%     label. An unknown suffix is left alone rather than silently eaten.
    Sfx = [{csp_panel:label_of(N), element(1, csp_panel:hue(N))}
	   || N <- ["Heater__Orange", "BathFan__Blue", "Siren__Red",
		    "Motor__Bogus"]],
    expect("__Colour suffix sets the colour and hides itself",
	   Sfx =:= [{"Heater","#f92"}, {"BathFan","#5af"},
		    {"Siren","#f44"}, {"Motor__Bogus","#f44"}], Sfx),

    %% 6f. the varp generator names states, so a gate reads `St == Home' rather
    %%     than `St == 3' -- unless the name is taken, in which case the number
    %%     stays, because defining it twice would shadow the other one.
    varp_gen_check(),

    %% 6g. #annotate picks the widget where the declaration cannot. Nothing in
    %%     `#digital NightButton in 6' says momentary or latching, and it should
    %%     not -- presentation is not in the language.
    annotate_check(),

    %% 7. a program with no inputs at all still runs -- traffic.csp is driven
    %%    by its own timer, so the lamps must change with nobody touching
    %%    anything. That also proves the tick actually advances csp.
    traffic_check(),
    halt(0).

annotate_check() ->
    {ok, A} = candyspeak:parse("demo/home.csp"),
    W = csp_panel:widgets(A),
    Kind = fun(N) -> case [K || {K, N2, _} <- W, N2 =:= N] of
			 [K] -> K; _ -> none end end,
    %% BathMotion is the push in home.csp -- motion is a pulse. The buttons are
    %% switches on purpose: a held mouse button is a mouse button not clicking
    %% anything else, so an armed house could not be poked at.
    expect("#annotate kind=push overrides the derived toggle",
	   Kind("BathMotion") =:= push, Kind("BathMotion")),
    expect("#annotate kind=toggle is honoured too",
	   Kind("ArmButton") =:= toggle andalso
	   Kind("NightButton") =:= toggle,
	   {Kind("ArmButton"), Kind("NightButton")}),
    expect("an un-annotated digital in stays a toggle",
	   Kind("FrontDoor") =:= toggle, Kind("FrontDoor")),
    expect("#annotate kind=action and kind=dial",
	   Kind("Siren__Red") =:= action andalso Kind("IndoorTemp") =:= dial,
	   {Kind("Siren__Red"), Kind("IndoorTemp")}),

    %% A kind the declaration cannot be is refused, not obeyed: a panel that
    %% renders a dial for a digital pin cannot drive the pin it is wired to.
    Bad = "#digital B in pullup 2\n#annotate panel B kind=dial\n",
    Tmp = "/tmp/csp_ann_bad.csp",
    ok = file:write_file(Tmp, Bad),
    {ok, A2} = candyspeak:parse(Tmp),
    expect("an impossible kind is refused, derived kind stands",
	   [K || {K, "B", _} <- csp_panel:widgets(A2)] =:= [toggle],
	   csp_panel:widgets(A2)),

    %% and candyspeak itself catches a target that does not exist
    Typo = "#digital Led out 8\n#annotate panel Lde kind=push\n",
    Tmp2 = "/tmp/csp_ann_typo.csp",
    ok = file:write_file(Tmp2, Typo),
    expect("build rejects an annotation naming nothing",
	   case candyspeak:build(Tmp2) of
	       {error, {annotate_unknown_target, _, "Lde", _}} -> true;
	       _ -> false
	   end, candyspeak:build(Tmp2)).

varp_gen_check() ->
    F = "demo/home.csp",
    case filelib:is_regular(F) of
	false -> io:format("skip  varp generator (no home.csp)~n");
	true ->
	    {ok, Io} = candyspeak_varp:translate(F),
	    Text = lists:flatten(Io),
	    expect("varp model names its states",
		   string:find(Text, "define Home 3;") =/= nomatch andalso
		   string:find(Text, "St == Home or St == Night") =/= nomatch,
		   nomatch),
	    %% a state whose name a signal already has keeps the number
	    Clash = "#digital Led out 8\n#variable Home = 0\n"
		    "#states Home Away\n#in INIT\n    State = Home\n#end\n",
	    Tmp = "/tmp/csp_varp_clash.csp",
	    ok = file:write_file(Tmp, Clash),
	    {ok, Io2} = candyspeak_varp:translate(Tmp),
	    T2 = lists:flatten(Io2),
	    expect("a state whose name is taken keeps its number",
		   string:find(T2, "define Home") =:= nomatch andalso
		   string:find(T2, "define Away 4;") =/= nomatch,
		   nomatch),

	    %% The runtime SKIPS a reserved name in #states -- FAILSAFE resolves
	    %% to the built-in 2 and takes no slot, so A is 3 (measured). Getting
	    %% this wrong shifted every state by one and defined FAILSAFE twice:
	    %% a model of a different program, with nothing to show it.
	    Res = "#states FAILSAFE A B\n#digital L out 8\n",
	    Tmp2 = "/tmp/csp_varp_reserved.csp",
	    ok = file:write_file(Tmp2, Res),
	    {ok, Io3} = candyspeak_varp:translate(Tmp2),
	    T3 = lists:flatten(Io3),
	    expect("a reserved name in #states takes no slot",
		   string:find(T3, "define A 3;") =/= nomatch andalso
		   string:find(T3, "define B 4;") =/= nomatch andalso
		   string:find(T3, "define FAILSAFE 3;") =:= nomatch,
		   nomatch),

	    %% ...and a duplicate keeps the first, as the runtime does
	    Dup = "#states a b c d\n#states d e\n#digital L out 8\n",
	    Tmp3 = "/tmp/csp_varp_dup.csp",
	    ok = file:write_file(Tmp3, Dup),
	    {ok, Io4} = candyspeak_varp:translate(Tmp3),
	    T4 = lists:flatten(Io4),
	    expect("a duplicate state keeps the first number",
		   string:find(T4, "define d 6;") =/= nomatch andalso
		   string:find(T4, "define e 7;") =/= nomatch, nomatch)
    end.

home_checks() ->
    F = "demo/home.csp",
    case filelib:is_regular(F) of
	false -> io:format("skip  home (not found)~n");
	true ->
	    %% cold, window shut: the heater runs
	    A = settle(F, [{"IndoorTemp", 150}]),
	    expect("home: heater runs when cold",
		   maps:get("Heater__Orange", A, 0) =:= 1, A),
	    %% cold, window open: it must not
	    B = settle(F, [{"IndoorTemp", 150}, {"LivingWindow", 1}]),
	    expect("home: no heating into an open window",
		   maps:get("Heater__Orange", B, 1) =:= 0, B),
	    %% a door, alarm disarmed: silence
	    C = settle(F, [{"FrontDoor", 1}]),
	    expect("home: siren silent while disarmed",
		   maps:get("Siren__Red", C, 1) =:= 0, C),
	    %% ArmButton is a SWITCH, so it is held on -- `!ArmButton && Armed'
	    %% disarms on release, which is what a switch should do. Pressing and
	    %% releasing it (as this test first did) disarms before the door is
	    %% ever opened.
	    %%
	    %% And a switch rather than a push button for a practical reason:
	    %% holding a mouse button down means not clicking anything else, so
	    %% an armed house could not be poked at.
	    D = settle(F, [{"ArmButton", 1}], [{"FrontDoor", 1}]),
	    expect("home: siren sounds when armed and the door opens",
		   maps:get("Siren__Red", D, 0) =:= 1, D),

	    %% ...and turning the switch off disarms, siren included
	    E = settle(F, [{"ArmButton", 1}, {"FrontDoor", 1}],
		       [{"ArmButton", 0}]),
	    expect("home: the switch off disarms and silences",
		   maps:get("Armed", E, 1) =:= 0 andalso
		   maps:get("Siren__Red", E, 1) =:= 0, E),

	    %% The net must not fire in normal use, and must fire when prevention
	    %% is missing. Compared by STATE NUMBER against each other rather
	    %% than against a literal: the numbering is not the declaration
	    %% order (Home came out 3 and FAILSAFE 2), so a magic number here
	    %% would be a guess that happens to pass.
	    net_check(maps:get("State", B, -1))
    end.

%% Same file with the window guard deleted: the violation now persists, and the
%% net has to catch it and land softly.
net_check(NormalState) ->
    {ok, Src} = file:read_file("demo/home.csp"),
    Lines = string:split(binary_to_list(Src), "\n", all),
    Broken = [L || L <- Lines,
		   string:find(L, "Heater__Orange = 0 ? LivingWindow") =:= nomatch],
    Tmp = "/tmp/home_no_guard.csp",
    ok = file:write_file(Tmp, string:join(Broken, "\n")),
    V = settle(Tmp, [{"IndoorTemp", 150}], [{"LivingWindow", 1}]),
    Failsafe = maps:get("State", V, -1),
    expect("home: the net does not fire when prevention works",
	   NormalState =/= -1, NormalState),
    expect("home: the net catches a real violation",
	   Failsafe =/= NormalState, {normal, NormalState, broken, Failsafe}),
    %% and the landing is what makes FAILSAFE safe rather than merely stopped
    expect("home: FAILSAFE lands softly (lights on, heat off)",
	   maps:get("HallLight", V, 0) =:= 1 andalso
	   maps:get("OutdoorLamp", V, 0) =:= 1 andalso
	   maps:get("Heater__Orange", V, 1) =:= 0, V).

settle(F, Sets) -> settle(F, Sets, []).

%% Drive csp to a settled state with the given inputs, in two rounds so a
%% button can be pressed and released.
settle(F, Sets1, Sets2) ->
    %% Dumps from the PREVIOUS test's csp may still be in the mailbox, and they
    %% would be merged in as though they came from this program. That is not
    %% hypothetical: it made the first run of this check read cpx_rotate's
    %% pixels and report that home.csp had no heater.
    flush(),
    {ok, L} = csp_link:open(csp_exe(), [F]), track(L),
    Last = steps(L, 3, #{}),                       % reach Home first
    Last1 = apply_sets(L, Sets1, Last),
    Last2 = apply_sets(L, Sets2, Last1),
    csp_link:close(L),
    timer:sleep(250),
    Last2.

apply_sets(_L, [], Acc) -> Acc;
apply_sets(L, Sets, Acc) ->
    [csp_link:set(L, N, V) || {N, V} <- Sets],
    steps(L, 4, Acc).

steps(_L, 0, Acc) -> Acc;
steps(L, N, Acc) ->
    csp_link:tick(L),
    timer:sleep(60),
    Acc1 = receive {csp_state, _, V} -> maps:merge(Acc, maps:from_list(V))
	   after 0 -> Acc
	   end,
    steps(L, N - 1, Acc1).

array_check() ->
    F = "../../examples/cpx_rotate.csp",
    case filelib:is_regular(F) of
	false -> io:format("skip  array (no cpx_rotate.csp)~n");
	true ->
	    {ok, A} = candyspeak:parse(F),
	    Ws = csp_panel:widgets(A),
	    Px = [N || {pixel, N, _} <- Ws],
	    expect("an analog array becomes one widget per element",
		   Px =:= ["P[" ++ integer_to_list(I) ++ "]"
			   || I <- lists:seq(0, 9)], Px),
	    %% and the values reach those names
	    flush(),
	    {ok, L} = csp_link:open(csp_exe(), [F]), track(L),
	    Vals = collect_px(L, 25, #{}),
	    csp_link:close(L),
	    timer:sleep(300),
	    expect("array elements get values from the dump",
		   length([1 || N <- Px, maps:is_key(N, Vals)]) =:= 10,
		   maps:keys(Vals))
    end.

flush() ->
    receive
	{csp_state, _, _} -> flush();
	{csp_exit, _}     -> flush()
    after 0 -> ok
    end.

collect_px(_L, 0, Acc) -> Acc;
collect_px(L, N, Acc) ->
    csp_link:tick(L),
    timer:sleep(60),
    Acc1 = receive {csp_state, _, V} -> maps:merge(Acc, maps:from_list(V))
	   after 0 -> Acc
	   end,
    collect_px(L, N - 1, Acc1).

analog_checks() ->
    Cpx = "../../examples/cpx_ball.csp",
    case filelib:is_regular(Cpx) of
	false -> io:format("skip  analog (no cpx_ball.csp)~n");
	true ->
	    {ok, A} = candyspeak:parse(Cpx),
	    Ws = csp_panel:widgets(A),
	    expect("analog in becomes a slider with the declared width",
		   lists:member({slider, "Light", 10}, Ws), Ws),
	    expect("port 9 becomes a pixel",
		   lists:member({pixel, "P0", 16}, Ws), Ws)
    end,
    expect("rgb565 decodes the strip",
	   lists:flatten(csp_panel:rgb565(16#F800)) =:= "rgb(255,0,0)" andalso
	   lists:flatten(csp_panel:rgb565(16#001F)) =:= "rgb(0,0,255)",
	   {csp_panel:rgb565(16#F800), csp_panel:rgb565(16#001F)}).

%% No widgets to poke: open the link, tick it, and watch the lamps move.
traffic_check() ->
    F = "../../examples/traffic.csp",
    case filelib:is_regular(F) of
	false -> io:format("skip  traffic (not found)~n");
	true ->
	    {ok, L} = csp_link:open(csp_exe(), [F]), track(L),
	    Seen = tick_collect(L, 40, []),
	    csp_link:close(L),
	    timer:sleep(300),
	    expect("timer-driven program changes with no input",
		   length(lists:usort(Seen)) > 1, lists:usort(Seen))
    end.

tick_collect(_L, 0, Acc) -> Acc;
tick_collect(L, N, Acc) ->
    csp_link:tick(L),
    Acc1 = receive
	       {csp_state, _, Vals} ->
		   [[{K, V} || {K, V} <- Vals,
			       lists:member(K, ["Red","Yellow","Green"])] | Acc]
	   after 300 -> Acc
	   end,
    tick_collect(L, N - 1, Acc1).

csp_exe() ->
    case os:getenv("CSP_EXE") of
	false -> "../../csp";
	Path  -> Path
    end.

latch_file() ->
    F = "demo/latch.csp",
    case filelib:is_regular(F) of true -> F; false -> "demo/gate.csp" end.

%% The chooser's event is created first, before any switch.
first_event() ->
    [Id | _] = [list_to_integer(string:trim(string:prefix(X, "event ")))
		|| X <- calls(), lists:prefix("event ", X)],
    Id.

%% One batched call per tick, not one fillRect per trace: the column is handed
%% to a painter function created once in the browser. See csp_panel:painter/1 --
%% wse:set/4 is rsync, so the old shape cost a round trip per trace per tick.
samples(L) -> [X || X <- L, lists:prefix("ASYNC cast call", X)].

calls() ->
    wse_log ! {calls, self()},
    receive {calls, L} -> L after 1000 -> [] end.

expect(What, true, _)  -> io:format("ok    ~s~n", [What]);
expect(What, false, D) ->
    io:format("FAIL  ~s: ~p~n", [What, D]),
    %% halt/1 runs no `after' clause and no linked process's cleanup, so a
    %% csp started by this test would be left holding a pipe nobody closes --
    %% and --exit-on-eof does not help, because the VM never closes it. Every
    %% link opened here is registered; close them before going.
    close_links(),
    halt(1).

%% Links opened by the test, so a failure can still take them down. A process
%% dictionary rather than an argument: expect/3 is called from a dozen places
%% and none of them should have to carry the bookkeeping.
track(L) ->
    put(links, [L | get_or([])]),
    L.

%% The panel process owns a link of its own, so it has to come down too -- it
%% is sent `stop', which is what makes its csp_link send /quit.
track_panel(P) ->
    put(panels, [P | case get(panels) of undefined -> []; V -> V end]),
    P.

get_or(D) -> case get(links) of undefined -> D; V -> V end.

close_links() ->
    [catch (P ! stop) || P <- case get(panels) of undefined -> []; V -> V end],
    [catch csp_link:close(L) || L <- get_or([])],
    timer:sleep(400),          % give each one time to send /quit
    put(links, []),
    put(panels, []).

logger(Acc) ->
    receive
	{log, S}        -> logger([lists:flatten(S) | Acc]);
	{calls, From}   -> From ! {calls, lists:reverse(Acc)}, logger(Acc)
    end.
