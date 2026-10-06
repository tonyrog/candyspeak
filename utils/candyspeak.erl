%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     Parse 
%%% @end
%%% Created : 31 Aug 2026 by Tony Rogvall <tony@rogvall.se>

-module(candyspeak).

-export([start/0, start/1]).
-export([main/1]).
-export([tokens/1, parse/1, parse/2, parse_one/1, build/1]).

-export([check_unit/0, check_examples/0]).
-export([build_unit/0, build_examples/0]).

start() ->
    io:format("candyspeak <file>\n", []),
    halt(1).

start([Filename|Fs]) when is_atom(Filename) ->
    main([atom_to_list(F) || F <- [Filename|Fs]]).

main(Args) ->
    files(Args).

files([Filename|Fs]) ->
    case parse(Filename) of
	{error, _} ->
	    halt(1);
	ok ->
	    files(Fs)
    end;
files([]) ->
    halt(0).

check_unit() ->
    lists:foreach(
      fun(Filename) ->
	      io:format("Check: ~s ", [Filename]),
	      case parse(Filename) of
		  {ok,_} -> io:format(" ok\n");
		  {error,Error} ->  io:format(" error ~p\n", [Error])
	      end
      end, filelib:wildcard("../tests/unit/*.csp")).

check_examples() ->
    lists:foreach(
      fun(Filename) ->
	      io:format("Check: ~s ", [Filename]),
	      case parse(Filename) of
		  {ok,_} -> io:format(" ok\n");
		  {error,Error} ->  io:format(" error ~p\n", [Error])
	      end
      end, filelib:wildcard("../examples/*.csp")).

build_unit() ->
    lists:foreach(
      fun(Filename) ->
	      io:format("Build: ~s ", [Filename]),
	      case build(Filename) of
		  {ok,_} -> io:format(" ok\n");
		  {error,Error} ->  io:format(" error ~p\n", [Error])
	      end
      end, filelib:wildcard("../tests/unit/*.csp")).

build_examples() ->
    lists:foreach(
      fun(Filename) ->
	      io:format("Build: ~s ", [Filename]),
	      case build(Filename) of
		  {ok,_} -> io:format(" ok\n");
		  {error,Error} ->  io:format(" error ~p\n", [Error])
	      end
      end, filelib:wildcard("../examples/*.csp")).

%%% ------------------------------------------------------------------ #import
%%%
%%% parse/1 is the whole PROGRAM: the file and everything it imports, as one
%%% flat list in which each imported file's lines stand where its #import was.
%%% That is what every caller -- the varp translator, the panel, build/1 --
%%% wants, and the reason the expansion is here and not in each of them.
%%% parse_one/1 is a single file, with its #import lines left in.
%%%
%%% The same rules as csp's (port/csp_linux.c, csp_import_run), so a program
%%% means the same thing to both:
%%%
%%%   #import analog            analog.csp, the first root that has it
%%%   #import lib "analog.csp"  that file in the root named lib
%%%   #import "pins.csp"        beside the importing file, no search
%%%
%%% Roots in order: {roots, [{Name, Dir}]} in Opts (csp's --root), CSP_ROOTS
%%% ("name=dir:name=dir"), board -- the nearest directory with a pins.csp,
%%% from the first file upwards -- and lib, the tree's own lib/. The first by a
%%% name wins. Each file is loaded ONCE, by its normalised absolute path.
%%%
%%% Line numbers stay those of the file each line is in. {sources, true} adds
%%% a {source, 0, File} marker where an imported file starts and where the
%%% importer resumes, for a caller that has to say WHICH file a line is in;
%%% without it the list holds nothing a caller of parse/1 did not see before.

parse(Filename) ->
    parse(Filename, []).

parse(Filename, Opts) ->
    Roots = roots(Filename, Opts),
    Mark = proplists:get_bool(sources, Opts),
    case expand(Filename, {Roots, Mark}, []) of
	{ok, Ast, _Seen} -> {ok, Ast};
	Error            -> Error
    end.

expand(File, Ctx, Seen) ->
    case parse_one(File) of
	{ok, Ast} -> expand(Ast, File, Ctx, [real(File) | Seen], []);
	Error     -> Error
    end.

expand([{import, Ln, How, What} | T], File, Ctx = {Roots, Mark}, Seen, Acc) ->
    case resolve(How, What, File, Roots) of
	{ok, Path} ->
	    case lists:member(real(Path), Seen) of
		true ->
		    expand(T, File, Ctx, Seen, Acc);
		false ->
		    case expand(Path, Ctx, Seen) of
			{ok, Sub, Seen1} when Mark ->
			    Sub1 = [{source, 0, Path}] ++ Sub ++ [{source, 0, File}],
			    expand(T, File, Ctx, Seen1, lists:reverse(Sub1, Acc));
			{ok, Sub, Seen1} ->
			    expand(T, File, Ctx, Seen1, lists:reverse(Sub, Acc));
			Error ->
			    Error
		    end
	    end;
	error ->
	    Asked = asked(How, What),
	    io:format("~s:~w: cannot import ~s: no such file\n",
		      [File, Ln, Asked]),
	    {error, {import_missing, Asked}}
    end;
expand([D | T], File, Ctx, Seen, Acc) ->
    expand(T, File, Ctx, Seen, [D | Acc]);
expand([], _File, _Ctx, Seen, Acc) ->
    {ok, lists:reverse(Acc), Seen}.

asked(name, N)      -> N;
asked({root, R}, P) -> R ++ " \"" ++ P ++ "\"";
asked(quoted, P)    -> "\"" ++ P ++ "\"".

resolve(quoted, P, File, _Roots) ->
    exists(filename:join(filename:dirname(File), P));
resolve({root, R}, P, _File, Roots) ->
    case lists:keyfind(R, 1, Roots) of
	{R, Dir} -> exists(filename:join(Dir, P));
	false    -> error
    end;
resolve(name, N, _File, Roots) ->
    first([filename:join(Dir, N ++ ".csp") || {_, Dir} <- Roots]).

first([F | Fs]) ->
    case exists(F) of
	{ok, _} = Ok -> Ok;
	error        -> first(Fs)
    end;
first([]) ->
    error.

exists(F) ->
    case filelib:is_regular(F) of
	true  -> {ok, F};
	false -> error
    end.

roots(First, Opts) ->
    Env = case os:getenv("CSP_ROOTS") of
	      false -> [];
	      Path  -> [{N, D} || Item <- string:split(Path, ":", all),
				  [N, D] <- [string:split(Item, "=")],
				  N =/= "", D =/= ""]
	  end,
    Board = [{"board", board_dir(filename:dirname(real(First)))}],
    Lib = [{"lib", filename:join(tree_dir(), "lib")}],
    uniq(proplists:get_value(roots, Opts, []) ++ Env ++ Board ++ Lib, []).

uniq([{N, _} = R | T], Acc) ->
    case lists:keymember(N, 1, Acc) of
	true  -> uniq(T, Acc);
	false -> uniq(T, Acc ++ [R])
    end;
uniq([], Acc) ->
    Acc.

%% The nearest directory with a pins.csp, from Dir upwards; Dir itself when
%% there is none. A test in boards/x/tests/ is boards/x's, not its own board.
board_dir(Dir) -> board_dir(Dir, Dir).

board_dir(Dir, Start) ->
    case filelib:is_regular(filename:join(Dir, "pins.csp")) of
	true -> Dir;
	false ->
	    case filename:dirname(Dir) of
		Dir    -> Start;                 % reached /
		Parent -> board_dir(Parent, Start)
	    end
    end.

%% The top of the tree: this module is utils/ebin/candyspeak.beam, or
%% utils/candyspeak.beam when compiled in place.
tree_dir() ->
    Beam = filename:absname(code:which(?MODULE)),
    Dir = filename:dirname(Beam),
    case filename:basename(Dir) of
	"ebin" -> filename:dirname(filename:dirname(Dir));
	_      -> filename:dirname(Dir)
    end.

%% An absolute path with . and .. taken out, so one file reached by two
%% spellings is seen once.
real(F) ->
    norm(filename:split(filename:absname(F)), []).

norm([".." | T], [_ | Acc]) -> norm(T, Acc);
norm([".." | T], [])        -> norm(T, []);
norm(["." | T], Acc)        -> norm(T, Acc);
norm([P | T], Acc)          -> norm(T, [P | Acc]);
norm([], Acc)               -> filename:join(lists:reverse(Acc)).

parse_one(Filename) ->
    case tokens(Filename) of
	{ok,Ts} ->
	    case candyspeak_parse:parse(Ts) of
		{error,{Ln,Mod,Message}} ->
		    io:format("~s:~w: ~s\n", 
			      [Filename,Ln,
			       apply(Mod,format_error,[Message])]),
		    {error, syntax};
		{ok,M0} ->
		    {ok, M0}
	    end;
	{error,{Ln,Mod,Message},_Ln} ->
	    io:format("~s:~w: ~s\n", 
		      [Filename,Ln,
		       apply(Mod,format_error,[Message])]),
	    {error, token};
	Error ->
	    io:format("error: ~p\n", [Error]),
	    Error
    end.

tokens(Filename) ->
    case file:read_file(Filename) of
	{ok,Bin} ->
	    case candyspeak_scan:string(binary_to_list(Bin)) of
		{ok,Ts,_EndLine} ->
		    {ok, Ts};
		{error,{Ln,Mod,Message},_Ln} ->
		    io:format("~s:~w: ~s\n", 
			      [Filename,Ln,
			       apply(Mod,format_error,[Message])]),
		    {error, token};
		Error -> 
		    Error
	    end;
	Error ->
	    Error
    end.

%% Build syntax structure from parsed lines
%% check variables names etc wrap in/when/objects ...
build(Filename) ->
    case parse(Filename) of
	{ok, Lines} ->
	    StateVar = {variable,0,{'WORD',0,"State"},scalar,default,[],
			{'INT',0,"0"}},
	    Bound = #{ "State" => StateVar,
		       states => [{'WORD',0,"INIT"},
				  {'WORD',0,"NORMAL"},
				  {'WORD',0,"FAILSAFE"}],
		       "Sys" => {{'module',0,{'WORD',0,"Sys"},
				  #{ "Serial" => fix,
				     "Id" => fix,
				     "Name" => fix,
				     "Image" => fix,
				     "Boot" => fix },
				  [
				  {variable,0,{'WORD',0,"Serial"},scalar,default,[{type,unsigned}],undefined},
				  {param,0,{'WORD',0,"Id"},scalar,default,[{type,unsigned}],undefined},
				  {param,0,{'WORD',0,"Name"},scalar,default,[{type,string}],undefined},
				  {variable,0,{'WORD',0,"Image"},scalar,default,[{type,unsigned}],undefined},
				  {param,0,{'WORD',0,"Boot"},scalar,default,[{type,unsigned}],undefined}
				 ]}},
		       "sys" => {object,0,{'WORD',0,"Sys"},{'WORD',0,"sys"},[]}
		     },
	    build(Lines, [], [[]], [Bound]);
	Error ->
	    Error
    end.

build([D={'in', _Ln, _States} | Lines], Stack, Acc, Bound) ->
    build(Lines, [D|Stack], [[]|Acc], Bound);
build([D={'when', _Ln, _Expr} | Lines], Stack, Acc, Bound) ->
    build(Lines, [D|Stack], [[]|Acc], Bound);
build([D={'module', _Ln, _Name} | Lines], Stack, Acc, Bound=[B|_]) ->
    StateVar = {variable,0,{'WORD',0,"State"},scalar,default,[],
		{'INT',0,"0"}},
    build(Lines, [D|Stack], [[]|Acc], [#{ 
					 "State" => StateVar,
					 states => maps:get(states,B)
					}|Bound]);
build([{'end',_L}| Lines], [D|Stack], [Ds,Ds0|Acc], Bound) ->
    if element(1,D) =:= 'module' ->
	    [B0,B1|Bs] = Bound,
	    States = maps:get(states, B0, []),
	    B11 = B1#{ states => States },
	    build(Lines, Stack, [[{D,B0,lists:reverse(Ds)}|Ds0]|Acc],[B11|Bs]);
       true ->
	    build(Lines, Stack, [[{D,lists:reverse(Ds)}|Ds0]|Acc], Bound)
    end;
build([D={field,_Ln,{'WORD',_,Name},_Res,_Options,_BufId,_Range}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    build(Lines, Stack, [[D|Ds]|Acc], 
	  [B#{ Name => D }|Bound]);
build([D={What,_Ln,{'WORD',_,Name},_Array,_Res,_Options,_Expr}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    case add_identifier(Name, D, B) of
	{ok, B1} ->
	    case What of
		variable -> build(Lines, Stack, [[D|Ds]|Acc],[B1|Bound]);
		constant -> build(Lines, Stack, [[D|Ds]|Acc],[B1|Bound]);
		digital -> build(Lines, Stack, [[D|Ds]|Acc],[B1|Bound]);
		analog -> build(Lines, Stack, [[D|Ds]|Acc],[B1|Bound])
	    end;
	Error ->
	    Error
    end;
build([D={local,_Ln,{'WORD',_,Name},_Res,_Options,_Expr}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    build(Lines, Stack, [[D|Ds]|Acc], 
	  [B#{ Name => D }|Bound]);
build([D={param,_Ln,{'WORD',_,Name},_Res,_Options,_Expr}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    build(Lines, Stack, [[D|Ds]|Acc], 
	  [B#{ Name => D }|Bound]);
build([D={timer,_Ln,{'WORD',_,Name},_Period,_Expr}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    build(Lines, Stack, [[D|Ds]|Acc], 
	  [B#{ Name => D }|Bound]);
build([D={buffer,_Ln,{'WORD',_,Name},_Period,_Expr}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    build(Lines, Stack, [[D|Ds]|Acc], 
	  [B#{ Name => D }|Bound]);
build([D={define,_Ln,{'WORD',_,Name},_Expr}|Lines],
      Stack, [Ds|Acc], [B|Bound]) ->
    build(Lines, Stack, [[D|Ds]|Acc], 
	  [B#{ Name => D }|Bound]);
build([{states,_Ln,States}|Lines], Stack, Acc, [B|Bound]) ->
    case add_states(States, B) of
	{ok, B1} ->
	    build(Lines, Stack, Acc, [B1|Bound]);
	Error ->
	    Error
    end;
build([{annotate,Ln, {'WORD',_,Tool},{'WORD',_,Target}, Items}|Lines],
      Stack, Acc, [B|Bound]) ->
    Kv = maps:from_list([{K, ann_value(V)}
			 || {{'WORD', _, K}, V} <- Items]),
    Tools = maps:get(tools, B, #{}),
    Targets = maps:get(Tool, Tools, #{}),
    TargetMap = maps:get(Target, Targets, #{}),
    Targets1 = maps:put(Target, maps:merge(TargetMap, Kv), Targets),
    Tools1 = maps:put(Tool, Targets1, Tools),
    %% The node does not go into Acc -- the tools map is what a tool reads --
    %% so where it CAME FROM has to be kept for check_annotations: it runs at
    %% the end (an annotation may precede its declaration) and by then the node
    %% is gone. Target and line, nothing else; the keys are the tool's business.
    At = [{Tool, Target, Ln} | maps:get(annotate_at, B, [])],
    B1 = maps:put(annotate_at, At, maps:put(tools, Tools1, B)),
    build(Lines, Stack, Acc, [B1|Bound]);

build([D|Lines], Stack, [Ds|Acc], Bound) ->
    build(Lines, Stack, [[D|Ds]|Acc], Bound);
build([], [], [Ds], [B]) ->
    Decls = lists:reverse(Ds),
    case check_annotations(Decls, B) of
	ok ->
	    Main = {{'module',0,{'WORD',0,"Main"}},B,Decls},
	    {ok, Main};
	Error ->
	    Error
    end.

%%
ann_value(true)              -> true;
ann_value({'WORD', _, V})    -> V;
ann_value({'INT', _, V})     -> V;
ann_value({'FLT', _, V})     -> V;
ann_value({'STR', _, V})     -> V;
ann_value(V)                 -> V.


%% An #annotate names a target, and a target that does not exist is the same
%% kind of mistake as a rule naming one -- a tool reading the annotation would
%% just never find it, and the typo would live forever.
%%
%% Checked HERE rather than during the walk so an annotation may precede its
%% declaration: the map is complete by now.
%%
%% The KEYS are not checked, and must not be: the tool named in the annotation
%% owns that space, and each tool warns about what it does not recognise.
check_annotations(_Decls, Bound) ->
    %% Oldest first, so a file with two mistakes reports the first one.
    check_targets(lists:reverse(maps:get(annotate_at, Bound, [])), Bound).

check_targets([{Tool, Target, Ln} | T], Bound) ->
    case maps:is_key(Target, Bound) orelse
	 is_state(Target, maps:get(states, Bound, [])) orelse
	 Target =:= "State" orelse
	 Target =:= "*" of                      % the tool itself
	true  -> check_targets(T, Bound);
	false -> {error, {annotate_unknown_target, Tool, Target, Ln}}
    end;
check_targets([], _Bound) ->
    ok.

add_identifier(Name, Decl, Bound) ->
    case maps:find(Name, Bound) of
	{ok, Decl0} ->
	    {error, {already_defined, Name, element(2, Decl0)}};
	error ->
	    States = maps:get(states, Bound),
	    case lists:keyfind(Name, 3, States) of
		false ->
		    {ok, Bound#{ Name => Decl }};
		{_, Ln, _} ->
		    {error, {already_defined, Name, Ln}}
	    end
    end.

add_states(States, Bound) ->
    add_states_(States, Bound).

add_states_([S={'WORD',_,Name}|List], Bound) ->
    case maps:find(Name, Bound) of
	{ok, Decl0} ->
	    {error, {already_defined, Name, element(2, Decl0)}};
	error ->
	    States = maps:get(states, Bound, []),
	    case is_state(Name, States) of
		true ->
		    {ok, Bound};
		false ->
		    Bound1 = Bound#{ states => States++[S] },
		    add_states_(List, Bound1)
	    end
    end;
add_states_([], Bound) ->
    {ok, Bound}.

is_state(Name, States) ->
    case lists:keyfind(Name, 3, States) of
	false ->
	    false;
	_ ->
	    true
    end.
