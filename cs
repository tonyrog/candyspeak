#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% cs -- one way in to building, loading and testing what is in this tree.
%%
%%     ./cs                              what there is, and what is plugged in
%%     ./cs info  <app|board> [board]    the board, and how it is built and flashed
%%     ./cs build <app> [board]          build the application for a board
%%     ./cs upload [<app> [board]]       load it, built first if it is not up to
%%                                       date; no app: the one last built
%%     ./cs test  <app>                  its tests, on the host
%%     ./cs ports                        the serial ports and what they are
%%
%%     -D <device>    the port (else the one plugged in with the board's USB id,
%%                    else the one in its terms)
%%     -b <baud>      the upload speed, where the loader has one
%%     -n             say what would run, run nothing
%%
%% A board may be named short: `./cs wintersbone s3` is wintersbone_s3. NAME=VALUE
%% anywhere on the line goes on to Makefile.board as it is (WATCH=1, MAP=...).
%%
%% THE TERMS ARE THE TRUTH and make does the building. An application is
%% apps/<app>/app.terms, a board boards/<board>/<board>.terms; where make put
%% things it says itself (`cs-vars`), in the run that builds. The upload is
%% the loader run directly, with the files make left: a build that is newer
%% than everything it was made from is not built again. Every command is
%% printed before it runs, so what this did can always be done by hand.
-mode(compile).

%% What a build leaves behind that an upload and `info` need.
-define(VARS, ["ACHIP", "CLIB", "SKETCH", "BOSSAC19", "FQBN", "B", "UPFQBN",
	       "UPPORT", "DEV", "BOARDDIR"]).

main(Args0) ->
    {Opts, Args1} = options(Args0, #{}, []),
    {Vars, Args} = lists:partition(fun is_var/1, Args1),
    put(dry, maps:get(dry, Opts, false)),
    try cmd(Args, Vars, Opts)
    catch throw:{error, Fmt, As} ->
	    io:format(standard_error, "cs: " ++ Fmt ++ "~n", As),
	    halt(1)
    end.

options(["-n" | T], O, Acc) -> options(T, O#{dry => true}, Acc);
options(["-D", D | T], O, Acc) -> options(T, O#{dev => D}, Acc);
options(["-b", B | T], O, Acc) -> options(T, O#{baud => B}, Acc);
options([F], _O, _Acc) when F =:= "-D"; F =:= "-b" ->
    err("~s wants a value", [F]);
options([A | T], O, Acc) -> options(T, O, [A | Acc]);
options([], O, Acc) -> {O, lists:reverse(Acc)}.

is_var(A) ->
    case string:split(A, "=") of
	[K, _] -> K =/= "" andalso
		      lists:all(fun(C) -> (C >= $A andalso C =< $Z) orelse
					      C =:= $_ orelse
					      (C >= $0 andalso C =< $9)
				end, K);
	_ -> false
    end.

err(Fmt, As) -> throw({error, Fmt, As}).

%% ---------------------------------------------------------------------------
%% Commands
%% ---------------------------------------------------------------------------

cmd([], _Vars, _O) -> overview();
cmd(["help" | _], _Vars, _O) -> usage();
cmd(["ports" | _], _Vars, _O) -> show_ports(ports());
cmd(["info", Name | Rest], Vars, O) -> info(Name, Rest, Vars, O);
cmd(["build" | Rest], Vars, _O) -> build(target(Rest), Vars);
cmd(["upload"], Vars, O) -> upload(last(), Vars, O);
cmd(["upload" | Rest], Vars, O) -> upload(target(Rest), Vars, O);
cmd(["test", App | _], _Vars, _O) -> test(app(App));
%% `./cs wintersbone s3' -- an application on its own asks for a build
cmd([Name | Rest], Vars, _O) ->
    case app_file(Name) of
	false -> err("no command or application '~s' (./cs help)", [Name]);
	_ -> build(target([Name | Rest]), Vars)
    end.

usage() ->
    {ok, Bin} = file:read_file(escript:script_name()),
    Lines = tl(tl(tl(string:split(Bin, "\n", all)))),
    Help = lists:takewhile(fun(L) -> L =/= <<>> andalso binary:first(L) =:= $%
			   end, Lines),
    [io:format("~s~n", [strip_comment(L)]) || L <- Help],
    ok.

strip_comment(<<"%% ", R/binary>>) -> R;
strip_comment(<<"%%", R/binary>>) -> R;
strip_comment(L) -> L.

overview() ->
    io:format("applications~n"),
    [io:format("  ~-14s ~s  (~s)~n",
	       [A, prop(board, P), join(boards_of(A, P), ", ")])
     || {A, P} <- apps()],
    io:format("~nboards~n  ~s~n", [wrap(join([B || {B, _} <- boards()], " "), 72)]),
    io:format("~nplugged in~n"),
    case ports() of
	[] -> io:format("  nothing~n");
	Ps -> show_ports(Ps)
    end,
    case state(last) of
	{A, B} -> io:format("~nlast built  ~s on ~s  (./cs upload)~n", [A, B]);
	_ -> ok
    end.

show_ports(Ps) ->
    [io:format("  ~-14s ~s  ~-28s ~s~n",
	       [Dev, Id, Prod, case board_of_usb(Id) of
				   [] -> "";
				   Bs -> "= " ++ string:join(Bs, " | ")
			       end])
     || {Dev, Id, Prod} <- Ps],
    ok.

join(Xs, Sep) -> string:join([to_list(X) || X <- Xs], Sep).

to_list(A) when is_atom(A) -> atom_to_list(A);
to_list(S) -> S.

wrap(S, W) ->
    {Ls, L} = lists:foldl(fun(Wd, {Acc, ""}) -> {Acc, Wd};
			     (Wd, {Acc, Cur}) when length(Cur) + length(Wd) >= W ->
				  {[Cur | Acc], Wd};
			     (Wd, {Acc, Cur}) -> {Acc, Cur ++ " " ++ Wd}
			  end, {[], ""}, string:lexemes(S, " ")),
    string:join(lists:reverse([L | Ls]), "\n  ").

%% ---------------------------------------------------------------------------
%% build, upload, test
%% ---------------------------------------------------------------------------

%% make, and what it says of where things went, kept for the upload.
build({App, Board, P}, Vars) ->
    Make = make_line(App, Board, P, Vars),
    MV = run_make(Make ++ ["all", "cs-vars", "CSVARS=" ++ string:join(?VARS, " ")]),
    get(dry) orelse put_state({built, App, Board}, #{vars => Vars, make => MV}),
    get(dry) orelse put_state(last, {App, Board}),
    MV.

%% The loader on what is built -- built first unless it is up to date.
upload({App, Board, P}, Vars, O) ->
    MV = case fresh(App, Board, Vars) of
	     {yes, MV0} ->
		 io:format("cs: ~s for ~s is up to date~n", [App, Board]),
		 MV0;
	     {no, Why} ->
		 io:format("cs: building (~s)~n", [Why]),
		 build({App, Board, P}, Vars)
	 end,
    put_state(last, {App, Board}),
    flash(Board, MV, O).

test({App, P}) ->
    run(["make", "-s", "csp"]),
    run(["escript", "tests/run_tests.escript",
	 prop(tests, P, "apps/" ++ atom_to_list(App) ++ "/tests")]).

%% What Makefile.board is told: the board and which program.
make_line(App, Board, P, Vars) ->
    How = case prop(pins, P) of
	      app -> ["APP=" ++ atom_to_list(App)];
	      board -> ["PROG=" ++ prop(main, P)]
	  end,
    ["make", "--no-print-directory", "-f", "Makefile.board",
     "BOARD=" ++ atom_to_list(Board)] ++ How ++ Vars.

%% ---------------------------------------------------------------------------
%% Up to date?
%%
%% The firmware is newer than everything it was made from: the program's files
%% (the image compiler's own deps, csp_rom.cspd), the runtime's sources, the
%% generators' terms, the board's directory, Makefile.board, the csp that wrote
%% the image -- and it was built with the same NAME=VALUEs. Anything else is
%% for make to decide.
%% ---------------------------------------------------------------------------

fresh(App, Board, Vars) ->
    case state({built, App, Board}) of
	#{vars := Vars, make := MV} ->
	    Out = output(MV),
	    case mtime(Out) of
		0 -> {no, "nothing built"};
		T ->
		    case [F || F <- inputs(MV), mtime(F) > T] of
			[] -> {yes, MV};
			[F | _] -> {no, F ++ " is newer"}
		    end
	    end;
	#{} -> {no, "other NAME=VALUEs than last time"};
	undefined -> {no, "not built by ./cs yet"}
    end.

%% The file the loader takes.
output(MV) ->
    case v("CLIB", MV) of
	"" -> filename:join(v("B", MV), "firmware.hex");
	Dir -> filename:join(Dir, v("SKETCH", MV) ++ ".ino.bin")
    end.

inputs(MV) ->
    Deps = case file:read_file(filename:join(v("B", MV), "csp_rom.cspd")) of
	       {ok, D} ->
		   [F || L <- string:split(binary_to_list(D), "\n", all),
			 [_, Fs] <- [string:split(L, ": ")],
			 F <- string:lexemes(Fs, " ")];
	       _ -> []
	   end,
    Srcs = lists:append([filelib:wildcard(W) ||
			    W <- ["src/*.[ch]", "include/*.h", "port/*",
				  "gen/*.[ch]", "utils/*.terms",
				  "CandySpeak/*.ino", "Makefile.board", "csp",
				  "csp-ieee"]]),
    Board = case v("BOARDDIR", MV) of
		"" -> [];
		BD -> filelib:wildcard(filename:join(BD, "*"))
	    end,
    [F || F <- Deps ++ Srcs ++ Board, filelib:is_regular(F)].

mtime(F) ->
    case file:read_file_info(F, [{time, posix}]) of
	{ok, I} -> element(6, I);                    % #file_info.mtime
	_ -> 0
    end.

v(K, MV) -> proplists:get_value(K, MV, "").

%% ---------------------------------------------------------------------------
%% The loaders
%% ---------------------------------------------------------------------------

flash(Board, MV, O) ->
    case recipe(Board, MV, O) of
	{ok, Steps} -> [step(S) || S <- Steps], ok;
	{error, Why} -> err("~s: ~s", [Board, Why])
    end.

step({run, Argv}) -> run(Argv);
step({touch, Dev, Boot}) -> touch(Dev, Boot);
step({note, S}) -> io:format("cs: ~s~n", [S]).

%% [{touch, ...} | {run, Argv} | {note, Text}], the loader's way in and the
%% loader, for this board and the files make left.
recipe(Board, MV, O) ->
    Dev = device(Board, MV, O),
    Baud = maps:get(baud, O, ""),
    case {v("CLIB", MV), v("UPFQBN", MV)} of
	{"", ""} ->
	    {error, "no loader in its terms yet -- see ./cs info"};
	{"", Fq} ->                          % bare metal over an Arduino bootloader
	    {ok, [{run, ["arduino-cli", "upload", "-p", Dev, "-b", Fq, "-i",
			 output(MV)] ++
		       [["--upload-property=upload.speed=", Baud] || Baud =/= ""]}]};
	{Dir, _} ->
	    Props = core_props(v("FQBN", MV)),
	    Touch = case maps:get("upload.use_1200bps_touch", Props, "false") of
			"true" -> [{touch, Dev, usb_boot(Board)}];
			_ -> []
		    end,
	    case lists:prefix("samd21", v("ACHIP", MV)) of
		true -> {ok, Touch ++ [bossac(MV, Dir, Baud)]};
		false ->
		    case core_line(Props, Dir, v("SKETCH", MV), Dev, Baud) of
			{ok, _Tool, Line} -> {ok, Touch ++ [{run, {sh, Line}}]};
			{error, _} = E -> E
		    end
	    end
    end.

%% NO -e: the settings are the last 2K of the flash, outside the image, and
%% -e erases them with the rest. --offset=0x2000 is the bootloader's 8K.
%% bossac 1.9: 1.7 does not erase what it writes. The port: what the touch
%% left, found again then (`{dev}').
bossac(MV, Dir, Baud) ->
    B = case v("BOSSAC19", MV) of "" -> "bossac"; X -> X end,
    Bin = filename:join(Dir, v("SKETCH", MV) ++ ".ino.bin"),
    [io:format("cs: -b ~s: bossac has no speed (it is USB), ignored~n", [Baud])
     || Baud =/= ""],
    {run, [B, "-p", {dev_file}, "-U", "-i", "--offset=0x2000", "-w", "-v", Bin, "-R"]}.

%% The port: -D, or the one plugged in that is this board by its USB id (in
%% its sketch or in its loader), or the one in the terms.
device(Board, MV, O) ->
    case maps:find(dev, O) of
	{ok, D} -> D;
	error ->
	    Ids = usb_ids(Board) ++ usb_boot(Board),
	    case [D || {D, Id, _} <- ports(), lists:member(Id, Ids)] of
		[D] -> D;
		[_, _ | _] = Ds ->
		    err("~s: ~s are all this board -- say which with -D",
			[Board, string:join(Ds, ", ")]);
		[] ->
		    case v("DEV", MV) of
			"" -> err("~s: no port -- say which with -D", [Board]);
			D -> D
		    end
	    end
    end.

%% Into the bootloader: 1200 baud, then let go of DTR. A -hupcl port keeps
%% DTR up on close, so the touch is made with hupcl -- twice, the first only
%% sets it. Already in the loader: nothing to do. Then wait for its port,
%% which may come back under another name.
touch(Dev, Boot) ->
    case lists:member(id_of(Dev), Boot) of
	true -> put(dev, Dev);
	false ->
	    run({sh, "stty -F " ++ Dev ++ " 1200 hupcl; stty -F " ++ Dev ++
		     " 1200 hupcl"}),
	    get(dry) =:= true orelse put(dev, wait_boot(Dev, Boot, 40))
    end,
    get(dry) =:= true andalso put(dev, Dev),
    ok.

wait_boot(Dev, _Boot, 0) ->
    err("~s: no bootloader came -- double-tap reset and try again", [Dev]);
wait_boot(Dev, Boot, N) ->
    timer:sleep(250),
    case [D || {D, Id, _} <- ports(), lists:member(Id, Boot)] of
	[D | _] -> io:format("cs: bootloader on ~s~n", [D]), D;
	[] when Boot =:= [], N < 32 -> Dev;          % no id to know it by: wait 2 s
	[] -> wait_boot(Dev, Boot, N - 1)
    end.

id_of(Dev) ->
    case [Id || {D, Id, _} <- ports(), D =:= Dev] of
	[Id] -> Id;
	_ -> "?"
    end.

%% What arduino-cli knows of the fqbn, expanded.
core_props(Fqbn) ->
    Out = os:cmd("arduino-cli board details -b '" ++ Fqbn ++
		     "' --show-properties=expanded 2>/dev/null"),
    maps:from_list([{K, V} || L <- string:split(Out, "\n", all),
			      [K, V] <- [string:split(L, "=")]]).

%% The upload command the core would run, expanded: the tool arduino-cli
%% picks for the fqbn, its pattern, its properties.
core_line(Props, Dir, Sketch, Dev, Baud) ->
    case maps:find("upload.tool", Props) of
	error -> {error, "arduino-cli names no upload.tool"};
	{ok, Tool0} ->
	    Tool = lists:last(string:split(Tool0, ":")),   % "esp32:esptool_py"
	    T = "tools." ++ Tool ++ ".",
	    Own = maps:from_list([{lists:nthtail(length(T), K), V}
				  || {K, V} <- maps:to_list(Props),
				     lists:prefix(T, K)]),
	    Speed = [{"upload.speed", Baud} || Baud =/= ""],
	    Env = maps:merge(maps:merge(Props, Own),
			     maps:from_list(
			       [{"serial.port", Dev},
				{"serial.port.file", filename:basename(Dev)},
				{"build.path", Dir},
				{"build.project_name", Sketch ++ ".ino"},
				{"upload.verbose", ""},
				{"upload.flash_prefix", ""} | Speed])),
	    %% A pattern with args of its own wraps the tool in something of
	    %% arduino-cli's (esp32: `{upload.flash_prefix} --esptool ...');
	    %% by hand it is the tool and the args.
	    case {maps:find("upload.pattern_args", Own),
		  maps:find("upload.pattern", Own)} of
		{{ok, _}, _} ->
		    {ok, Tool, expand("\"{path}/{cmd}\" {upload.pattern_args}",
				      speed(Env, Baud), 8)};
		{error, {ok, Pat}} -> {ok, Tool, expand(Pat, Env, 8)};
		{error, error} -> {error, "no " ++ T ++ "upload.pattern"}
	    end
    end.

%% esptool's args name the speed as a number, not as {upload.speed}.
speed(Env, "") -> Env;
speed(Env, Baud) ->
    Args = maps:get("upload.pattern_args", Env, ""),
    Env#{"upload.pattern_args" =>
	     re:replace(Args, "--baud [0-9]+", "--baud " ++ Baud,
			[{return, list}])}.

expand(S, _Env, 0) -> S;
expand(S, Env, N) ->
    S1 = re:replace(S, "\\{([a-zA-Z0-9_.\\-]+)\\}",
		    fun(_, [K]) -> maps:get(binary_to_list(K), Env,
					    "{" ++ binary_to_list(K) ++ "}")
		    end, [global, {return, list}]),
    case S1 =:= S of
	true -> string:trim(re:replace(S1, "  +", " ", [global, {return, list}]));
	false -> expand(S1, Env, N - 1)
    end.

%% ---------------------------------------------------------------------------
%% info
%% ---------------------------------------------------------------------------

info(Name, Rest, Vars, O) ->
    case app_file(Name) of
	false ->
	    Board = board_name(Name, [B || {B, _} <- boards()], none),
	    info_board(Board, none, Vars, O);
	_ ->
	    {App, Board, P} = target([Name | Rest]),
	    info_board(Board, {App, P}, Vars, O)
    end.

info_board(Board, AppP, Vars, O) ->
    B = atom_to_list(Board),
    io:format("~s", [os:cmd("escript utils/gen_chips.erl --info-of " ++ B
			    ++ " 2>/dev/null")]),
    [io:format("  usb        ~s~n", [string:join(Ids, " ")])
     || Ids <- [usb_ids(Board)], Ids =/= []],
    [io:format("  usb boot   ~s~n", [string:join(Ids, " ")])
     || Ids <- [usb_boot(Board)], Ids =/= []],
    {Make, MV} =
	case AppP of
	    none ->
		%% No program: what make says of the board alone -- a board is
		%% flashed the same whatever it carries.
		M = ["make", "--no-print-directory", "-f", "Makefile.board",
		     "BOARD=" ++ B],
		{M, make_vars(M)};
	    {App, P} ->
		M = make_line(App, Board, P, Vars),
		io:format("~nbuild~n  ~s~n", [show(M ++ ["all"])]),
		case state({built, App, Board}) of
		    #{make := MV0} -> {M, MV0};
		    _ -> {M, make_vars(M)}
		end
	end,
    _ = Make,
    io:format("~nby hand~n  output     ~s~n", [output(MV)]),
    [io:format("  load at    ~s~n", [L])
     || v("CLIB", MV) =:= "",
	L <- [string:trim(os:cmd("escript utils/gen_chips.erl --load-addr " ++ B
				 ++ " 2>/dev/null"))], L =/= ""],
    put(dry, true),                          % show the steps, run none
    O1 = case maps:is_key(dev, O) of
	     true -> O;
	     false -> O#{dev => case v("DEV", MV) of "" -> "/dev/ttyACM0"; D -> D end}
	 end,
    put(dev, maps:get(dev, O1)),             % the port the touch would find
    try recipe(Board, MV, O1) of
	{ok, Steps} -> [info_step(S) || S <- Steps];
	{error, Why} -> io:format("  flash      ~s~n", [Why])
    catch throw:{error, F, As} -> io:format("  flash      " ++ F ++ "~n", As)
    end,
    loader_note(v("ACHIP", MV), v("FQBN", MV), MV),
    case AppP of
	none -> io:format("~nan application builds for it: ./cs info <app> ~s~n", [B]);
	_ -> ok
    end.

info_step({touch, Dev, _}) ->
    io:format("  loader     stty -F ~s 1200 hupcl; stty -F ~s 1200 hupcl\n"
	      "             (the 1200-baud touch, twice for a -hupcl port), or\n"
	      "             double-tap reset; the loader comes up on its own id\n",
	      [Dev, Dev]);
info_step({run, Argv}) -> io:format("  flash      ~s~n", [show(Argv)]);
info_step({note, S}) -> io:format("             ~s~n", [S]).

loader_note("samd21" ++ _, _, MV) ->
    B = case v("BOSSAC19", MV) of "" -> "bossac"; X -> X end,
    io:format("             NO -e: the settings are the last 2K of flash, outside\n"
	      "             the image; 0x2000 is the bootloader's 8K. bossac >= 1.9.\n"
	      "  backup     ~s -p ~s -U --offset=0x2000 -r before.bin\n",
	      [B, filename:basename(get(dev))]);
loader_note(_, "esp32:" ++ _, _) ->
    io:format("  loader     esptool resets into it itself (--before default-reset);\n"
	      "             by hand: hold BOOT, press RST, let go of BOOT.\n"
	      "  backup     the same esptool, read-flash 0 ALL before.bin\n");
loader_note(_, "arduino:avr:" ++ _, _) ->
    io:format("  loader     DTR on the USB serial resets it; avrdude does it\n");
loader_note(_, _, _) -> ok.

%% What make made of the variables when nothing was built by ./cs: one run of
%% it, which costs what reading Makefile.board costs.
make_vars(Make) ->
    Out = os:cmd(show(Make ++ ["cs-vars", "CSVARS=" ++ string:join(?VARS, " ")])
		 ++ " 2>/dev/null"),
    cs_vars(Out).

cs_vars(Out) ->
    [{K, V} || L <- string:split(Out, "\n", all),
	       "cs-var " ++ KV <- [L],
	       [K, V] <- [string:split(KV, "=")]].

%% ---------------------------------------------------------------------------
%% Applications and boards
%% ---------------------------------------------------------------------------

app_file(Name) ->
    F = filename:join(["apps", Name, "app.terms"]),
    filelib:is_regular(F) andalso F.

apps() ->
    [{A, P} || F <- lists:sort(filelib:wildcard("apps/*/app.terms")),
	       {app, A, P} <- consult(F)].

app(Name) ->
    case app_file(Name) of
	false -> err("no application '~s' -- there are: ~s",
		     [Name, join([A || {A, _} <- apps()], " ")]);
	F -> [{app, A, P}] = consult(F), {A, P}
    end.

%% Which boards an application is for: its list, or with its own pins per
%% board, the board directories it has.
boards_of(App, P) ->
    case prop(pins, P) of
	app -> [list_to_atom(filename:basename(filename:dirname(F)))
		|| F <- lists:sort(filelib:wildcard(
				     "apps/" ++ atom_to_list(App) ++ "/*/pins.csp"))];
	board -> prop(boards, P, [prop(board, P)])
    end.

%% [App, Board?] -> {App, Board, Props}
target([]) -> err("which application? (./cs for the list)", []);
target([Name | Rest]) ->
    {App, P} = app(Name),
    Boards = boards_of(App, P),
    Board = case Rest of
		[] -> prop(board, P);
		[B | _] -> board_name(B, Boards, App)
	    end,
    case lists:member(Board, Boards) of
	true -> {App, Board, P};
	false -> err("~s is not built for ~s (only ~s)",
		     [App, Board, join(Boards, ", ")])
    end.

%% The board as written, or <app>_<it>, or the one board whose name has it.
board_name(S, Boards, App) ->
    Names = [atom_to_list(B) || B <- Boards],
    Pref = case App of none -> []; _ -> [atom_to_list(App) ++ "_" ++ S] end,
    case [N || N <- [S | Pref], lists:member(N, Names)] of
	[N | _] -> list_to_atom(N);
	[] ->
	    case [N || N <- Names, string:find(N, S) =/= nomatch] of
		[N] -> list_to_atom(N);
		[] -> err("no board '~s' -- there are: ~s", [S, string:join(Names, " ")]);
		Ns -> err("'~s' could be ~s", [S, string:join(Ns, ", ")])
	    end
    end.

boards() ->
    lists:sort([{B, P} || F <- filelib:wildcard("boards/*/*.terms"),
			  {board, B, P} <- consult(F)]).

board_props(Board) ->
    case lists:keyfind(Board, 1, boards()) of
	{_, P} -> P;
	false -> []
    end.

usb_ids(Board) -> prop(usb, board_props(Board), []).
usb_boot(Board) -> prop(usb_boot, board_props(Board), []).

board_of_usb(Id) ->
    [atom_to_list(B) ++ [" (loader)" || lists:member(Id, prop(usb_boot, P, []))]
     || {B, P} <- boards(),
	lists:member(Id, prop(usb, P, []) ++ prop(usb_boot, P, []))].

consult(F) ->
    case file:consult(F) of
	{ok, Ts} -> Ts;
	{error, E} -> err("~s: ~p", [F, E])
    end.

prop(K, P) -> prop(K, P, undefined).
prop(K, P, D) -> proplists:get_value(K, P, D).

%% ---------------------------------------------------------------------------
%% The serial ports, by what is behind them: [{Dev, "vid:pid", Product}]
%% ---------------------------------------------------------------------------

ports() ->
    Devs = lists:sort(filelib:wildcard("/dev/ttyACM*") ++
			  filelib:wildcard("/dev/ttyUSB*")),
    [{D, Id, Prod} || D <- Devs, {Id, Prod} <- [usb_of(D)]].

%% Up from the interface until a directory has an idVendor. `..', not the
%% link read and joined: the kernel takes .. after the link, where it points.
usb_of(Dev) ->
    usb_up("/sys/class/tty/" ++ filename:basename(Dev) ++ "/device", 4).

usb_up(_Dir, 0) -> {"?", ""};
usb_up(Dir, N) ->
    case file:read_file(Dir ++ "/idVendor") of
	{ok, V} ->
	    {string:trim(binary_to_list(V)) ++ ":" ++ read(Dir ++ "/idProduct"),
	     read(Dir ++ "/product")};
	_ -> usb_up(Dir ++ "/..", N - 1)
    end.

read(F) ->
    case file:read_file(F) of
	{ok, B} -> string:trim(binary_to_list(B));
	_ -> ""
    end.

%% ---------------------------------------------------------------------------
%% What ./cs remembers: tmp/cs.state, a map
%% ---------------------------------------------------------------------------

state_file() -> "tmp/cs.state".

states() ->
    case file:consult(state_file()) of
	{ok, [M]} when is_map(M) -> M;
	_ -> #{}
    end.

state(K) -> maps:get(K, states(), undefined).

put_state(K, V) ->
    get(dry) =:= true orelse
	begin
	    ok = filelib:ensure_dir(state_file()),
	    ok = file:write_file(state_file(),
				 io_lib:format("~p.~n", [(states())#{K => V}]))
	end.

last() ->
    case state(last) of
	{App, Board} ->
	    {_, P} = app(atom_to_list(App)),
	    {App, Board, P};
	_ -> err("nothing built yet -- ./cs upload <app> [board]", [])
    end.

%% ---------------------------------------------------------------------------
%% Running
%% ---------------------------------------------------------------------------

%% An argv, or {sh, Line} for one that is already a shell line. {dev_file} is
%% the port the touch found, as bossac wants it: the name in /dev.
show({sh, Line}) -> Line;
show(Argv) ->
    string:join([quote(arg(A)) || A <- Argv], " ").

arg({dev_file}) ->
    case get(dev) of
	undefined -> "<port>";
	D -> filename:basename(D)
    end;
arg(A) -> lists:flatten(A).

quote(A) ->
    case lists:any(fun(C) -> lists:member(C, " '\"$*?;&|<>()") end, A) of
	true -> "'" ++ A ++ "'";
	false -> A
    end.

run(Argv) ->
    Line = show(Argv),
    io:format("+ ~s~n", [Line]),
    case get(dry) of
	true -> ok;
	_ ->
	    case shell(Line, fun io:put_chars/1) of
		0 -> ok;
		S -> halt(S)
	    end
    end.

%% make, with its cs-var lines taken out of what it prints and handed back.
run_make(Argv) ->
    Line = show(Argv),
    io:format("+ ~s~n", [Line]),
    case get(dry) of
	true -> [];
	_ ->
	    put(cs_out, []),
	    S = shell(Line, fun(D) ->
				    put(cs_out, [D | get(cs_out)]),
				    [io:put_chars([L, $\n])
				     || L <- string:split(D, "\n", all),
					L =/= <<>>,
					not is_cs_var(L)]
			    end),
	    S =:= 0 orelse halt(S),
	    cs_vars(binary_to_list(iolist_to_binary(lists:reverse(get(cs_out)))))
    end.

is_cs_var(<<"cs-var ", _/binary>>) -> true;
is_cs_var(_) -> false.

%% Line by line, so that what make prints comes through as it is printed.
shell(Line, Out) ->
    Port = open_port({spawn, Line ++ " 2>&1"},
		     [{line, 4096}, binary, exit_status, use_stdio]),
    pump(Port, Out, <<>>).

pump(Port, Out, Part) ->
    receive
	{Port, {data, {noeol, D}}} -> pump(Port, Out, <<Part/binary, D/binary>>);
	{Port, {data, {eol, D}}} ->
	    Out(<<Part/binary, D/binary, "\n">>),
	    pump(Port, Out, <<>>);
	{Port, {exit_status, S}} -> S
    end.
