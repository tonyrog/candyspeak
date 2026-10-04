%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     Parse 
%%% @end
%%% Created : 31 Aug 2026 by Tony Rogvall <tony@rogvall.se>

-module(candyspeak).

-export([start/0, start/1]).
-export([main/1]).
-export([tokens/1, parse/1, build/1, to_c/1]).

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

parse(Filename) ->
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

-define(INDENT, 2).
%% prepend N blanks
indent(0,Str) -> Str;
indent(N,Str) -> indent(N-1, [$\s|Str]).

to_c(Filename) ->
    case build(Filename) of
	{ok, Main} ->
	    Ext  = filename:extension(Filename),
	    Base = filename:basename(Filename, Ext),
	    {ok,Fd} = file:open(Base ++ ".c", [write]),
	    %% Fd = user,
	    to_c(Fd, "",Main);
	Error ->
	    Error
    end.

to_c_fields(Fd, Indent, [Decl|Decls]) ->
    case Decl of
	{variable,_Ln,{'WORD',_,Name},Array,Res,Options,_Expr} ->
	    io:put_chars(Fd,[Indent,type(Res, Options)," ",Name,
			     array_size(Array),";\n"]);
	{constant,_Ln,{'WORD',_,Name},Array,Res,Options,_Expr} ->
	    io:put_chars(Fd,
	      [Indent,"const ",type(Res, Options)," ",Name,
	       array_size(Array),";\n"]);
	{param,_Ln,{'WORD',_,Name},Res,Options,_Expr} ->
	    io:put_chars(Fd,
			 [Indent,type(Res, Options)," ",Name, ";\n"]);
	{timer,_Ln,{'WORD',_,Name},_Period,_Init} ->
	    io:put_chars(Fd,
			 [Indent,"csp_timer_t ",Name,";\n"]);
	{buffer,_Ln,{'WORD',_,Name},{'INT',_,Size},_Options,_BufType} ->
	    io:put_chars(Fd,
			 [Indent,"uint8 ",Name,"[",Size,"]", ";\n"]);
	{field,_Ln,{'WORD',_,Name},Res,Options,{'WORD',_,_BufName},_Range} ->
	    %% fixme: associate with buffer
	    io:put_chars(Fd,[Indent,type(Res, Options)," ",Name, ";\n"]);

	{digital,_Ln,{'WORD',_,Name},Array,_Res,_Options,_Expr} ->
	    io:put_chars(Fd,
			 [Indent,"csp_digital_t ", Name, array_size(Array),";\n"]);
	{analog,_Ln,{'WORD',_,Name},Array,_Res,_Options,_Expr} ->
	    io:put_chars(Fd,
			 [Indent,"csp_analog_t ", Name, array_size(Array),";\n"]);
	{object,_Ln,{'WORD',_,Module},{'WORD',_,Name},_Args} ->
	    io:put_chars(Fd,
	      [Indent,Module,"_t ",Name,";\n"]);
	_ ->
	    ok
    end,
    to_c_fields(Fd,Indent, Decls);
to_c_fields(_Fd,_Indent, []) ->
    ok.

%% FIMXE used in init
to_c_finit(Fd, Indent, Bound, [Decl|Decls]) ->
    case Decl of
	{variable,_Ln,{'WORD',_,Name},Array,Res,Options,Expr} ->
	    io:put_chars(Fd,
	      [Indent,type(Res, Options)," ",Name,array_size(Array),
	       iexpr(Expr,Bound),";\n"]);
	{constant,_Ln,{'WORD',_,Name},Array,Res,Options,Expr} ->
	    io:put_chars(Fd,
	      [Indent,"const ",type(Res, Options)," ",Name,
	       array_size(Array),iexpr(Expr,Bound),";\n"]);
	{digital,_Ln,{'WORD',_,Name},Array,_Res,_Options,Expr} ->
	    io:put_chars(Fd,
			 [Indent,"int ", Name, array_size(Array),
			  pin(Expr,Bound),";\n"]);
	{analog,_Ln,{'WORD',_,Name},Array,_Res,_Options,Expr} ->
	    io:put_chars(Fd,
			 [Indent,"int ", Name, array_size(Array), 
			  pin(Expr,Bound),";\n"]);
	{object,_Ln,{'WORD',_,Module},{'WORD',_,Name},_Args} ->
	    io:put_chars(Fd,
			 [Indent,Module,"_t ",Name,";\n"]);
	_ ->
	    ok
    end,
    to_c_finit(Fd,Indent,Bound,Decls);
to_c_finit(_Fd,_Indent,_Bound,[]) ->
    ok.

%% "Globals #define's and enums
to_c_decls(Fd, Indent, Bound, [Decl|Decls]) ->
    case Decl of
	{'define',_Ln,{'WORD',_,Name}, Expr} ->
	    io:put_chars(Fd,
	      [Indent,"#define ",Name," ", expr(Expr,Bound),"\n"]);
	{{'module',_Ln,{'WORD',_,_Module}}, _Bound1, _Ds} ->
	    to_c_module(Fd, "", Decl);
	_ ->
	    ok
    end,
    to_c_decls(Fd,Indent, Bound, Decls);
to_c_decls(_Fd, _Indent, _Bound, []) ->
    ok.

to_c_module(Fd, Indent, {{'module',_,{'WORD',_,Module}}, Bound, Ds}) ->
    Module_t = [Module,"_t"],
    to_c_decls(Fd, indent(?INDENT,Indent), Bound, Ds),
    io:put_chars(Fd, [Indent, "typedef struct _",Module_t," {\n"]),
    io:put_chars(Fd, [indent(?INDENT,Indent),
		      "states_t State;\n"]),
    to_c_fields(Fd, indent(?INDENT,Indent), Ds),
    io:put_chars(Fd, [Indent, "} ", Module_t,";\n"]),
    io:put_chars(Fd, ["\n",
		  Indent, "void ",Module,"_run(",
		  Module_t,"* in, ",
		  Module_t,"* out)\n",
		  Indent, "{\n"]),
    to_c_(Fd, indent(?INDENT,Indent), Bound, Ds),
    io:put_chars(Fd, [Indent,"}\n\n"]).

to_c(Fd, Indent, Module={{module,_,{'WORD',_,"Main"}}, Bound, _Ds}) ->
    to_c_includes(Fd),
    to_c_states(Fd, Indent, Bound),
    to_c_module(Fd, Indent, Module).

to_c_(Fd, Indent, Bound, [Decl|Decls]) ->
    case Decl of
	{object,_Ln,{'WORD',_,Module},{'WORD',_,Name},_Args} ->
	    io:put_chars(Fd,
	      [Indent,Module,"_run(",
	       "&in->",Name,",",
	       "&out->",Name,");\n"]);
	{{'in',_Ln,States},Actions} ->
	    io:put_chars(Fd,[Indent,"if (", in(States), ") {\n"]),
	    to_c_(Fd,indent(?INDENT,Indent),Bound,Actions),
	    io:put_chars(Fd,[Indent,"}\n"]);
	{{'when',_Ln,Cond},Actions} ->
	    io:put_chars(Fd,[Indent,"if (", expr(Cond,Bound), ") {\n"]),
	    to_c_(Fd,indent(?INDENT,Indent),Bound,Actions),
	    io:put_chars(Fd,[Indent,"}\n"]);
	{rule,_Ln,AssignList,undefined} ->
	    io:put_chars(Fd,assignments(Indent,Bound,AssignList));
	{rule,_Ln,AssignList,Cond} ->
	    io:put_chars(Fd,
	      [Indent,"if (", expr(Cond,Bound), ") {\n",
	       assignments(indent(?INDENT,Indent),Bound,AssignList),
	       Indent,"}\n"]);
	_ ->
	    ok
    end,
    to_c_(Fd,Indent,Bound,Decls);
to_c_(_Fd,_Indent, _Bound, []) ->
    ok.

in(States) ->
    lists:join(" && ", in_(States)).

in_([{'WORD',_,W}|States]) ->
    [ ["(","in->State == ", W,")"] | in_(States)];
in_([]) ->
    [].

assignments(Indent,Bound,List) ->
    lists:join(";\n", assignments_(Indent,Bound,List)).

assignments_(Indent,Bound,[{'=',_ln,Lhs,Rhs} | List]) ->
    [ [Indent,"out->",lhs(Lhs,Bound)," = ",expr(Rhs,Bound),";\n"] |
      assignments_(Indent,Bound,List)];
assignments_(Indent,Bound,[{'<-',_ln,Lhs,Rhs} | List]) ->
    [ [Indent,"out->",lhs(Lhs,Bound)," = ",expr(Rhs,Bound),";\n"] |
      assignments_(Indent,Bound,List)];
assignments_(Indent,Bound,[Expr|List]) ->
    [ [Indent,expr(Expr,Bound),";\n"] | assignments_(Indent,Bound,List)];
assignments_(_Indent,_Bound,[]) ->
    [].

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

has_val_part(Name, Bound) ->
    case maps:get(Name, Bound) of
	{digital,_Ln,_Name,_Array,_Res,_Options,_Expr} ->
	    true;
	{analog,_Ln,_Name,Array,_Res,_Options,_Expr} ->
	    true;
	{timer,_Ln,_Name,_Period,_Init} ->
	    true;
	_ ->
	    false
    end.
%% 
to_c_includes(Fd) ->
    io:put_chars(Fd,["#include <stdint.h>","\n",
		     "typedef struct {\n",
		     "  int period;\n"
		     "  int fired;\n"
		     "  int running;\n"
		     "  int val;\n"
		     "} csp_timer_t;\n\n",
		     "typedef struct {\n",
		     "  uint8_t dir:2;\n"
		     "  uint8_t soft:1;\n",
		     "  uint8_t pin:7;\n",
		     "  uint8_t port:4;\n",
		     "  uint8_t irq:3;\n",
		     "  uint8_t pullup:1;\n",
		     "  uint8_t pulldown:1;\n"
		     "  uint8_t val;\n"
		     "} csp_digital_t;\n\n",
		     "typedef struct {\n",
		     "  uint8_t dir:2;\n"
		     "  uint8_t soft:1;\n",
		     "  uint8_t pin:7;\n",
		     "  uint8_t port:4;\n",
		     "  uint8_t irq:3;\n",
		     "  uint8_t pwm:1;\n",
		     "  uint8_t endian:2;\n"
		     "  uint16_t val:2;\n"
		     "} csp_analog_t;\n\n"]).

%% declare states
to_c_states(Fd,Indent,Bound) ->
    case maps:get(states,Bound,[]) of
	[] ->
	    ok;
	States ->
	    io:put_chars(Fd,[Indent,"typedef enum {\n"]),
	    lists:foreach(
	      fun({{'WORD',_Ln,StateName},Num}) ->
		      io:put_chars(Fd,[indent(?INDENT,Indent),
				       StateName," = ",integer_to_list(Num),
				       ",\n"])
	      end, lists:zip(States, lists:seq(0, length(States)-1))),
	    io:put_chars(Fd,[Indent, "} ","states_t;\n\n"])
    end.

array_size(scalar) -> "";
array_size({array_size,_Ln,Size}) -> ["[",expr(Size,#{}),"]"].

pin(undefined,_Bound) -> "";
pin({pin,_,Pin},Bound) ->  [" ",expr(Pin,Bound)];
pin({port_pin,_Ln,Port,{range,_,Pin0,Pin1}},Bound) -> 
    [" ",expr(Port,Bound),":",expr(Pin0,Bound),"..",expr(Pin1,Bound)];
pin({port_pin,_Ln,Port,Pin},Bound) ->
    [" ",expr(Port,Bound),":",expr(Pin,Bound)];
pin(Pins,Bound) when is_list(Pins) ->
    lists:join(" ", pin_list(Pins,Bound)).

pin_list([P|Ps],Bound) ->
    [pin(P,Bound) | pin_list(Ps,Bound)];
pin_list([],_Bound) ->
    [].

type(default, Options) -> 
    case proplists:get_value(type, Options, integer) of
	integer -> "int32_t";
	unsigned -> "uint32_t";
	float -> "float";
	string -> "char*"
    end;
type({'INT',_,Res}, Options) -> 
    N = list_to_integer(Res),
    case proplists:get_value(type, Options, integer) of
	integer ->
	    if N =< 8  -> "int8_t";
	       N =< 16 -> "int16_t";
	       N =< 32 -> "int32_t"
	    end;
	unsigned ->
	    if N =< 8  -> "uint8_t";
	       N =< 16 -> "uint16_t";
	       N =< 32 -> "uint32_t"
	    end;
	float ->
	    "float";
	string ->
	    "char*"
    end.

iexpr(undefined,_Bound) -> "";
iexpr(Expr,Bound) -> [" = ", expr(Expr,Bound)].

fld({'WORD',_Ln,Name},_Bound) -> Name;
fld({index,_Ln,Field,Index},Bound) -> 
    [fld(Field,Bound),"[",expr(Index,Bound),"]"];
fld({fld,_Ln,Field,Name},Bound) ->
    [fld(Field,Bound),".",expr(Name,Bound)].

field({'WORD',_Ln,Name},Bound) -> 
    case has_val_part(Name, Bound) of
	true -> [Name,".val"];
	false -> [Name]
    end;
field({index,_Ln,Field,Index},Bound) -> 
    [field(Field,Bound),"[",expr(Index,Bound),"]"];
field({part,_Ln,Field,{'WORD',_,Part}},Bound) ->
    [fld(Field,Bound),".",Part];
field({fld,_Ln,Field,Name},Bound) ->
    [fld(Field,Bound),".",expr(Name,Bound)].

lhs({field,_Ln,Field},Bound) -> field(Field,Bound).
    

expr(Expr,Bound) ->
    expr(Expr,Bound,0).

expr({'INT',_Ln,Int},_Bound,_) -> Int;
expr({'FLT',_Ln,Float},_Bound,_) -> Float;
expr(V={'WORD',_Ln,Var}, Bound,_) -> 
    io:format("VAR=~p\n", [V]),
    Var;
expr({range,_Ln,From,To},Bound,_) ->
    [expr(From,Bound),"..",expr(To,Bound)];
expr({field,_Ln,W={'WORD',_,Name}},Bound,_) ->
    case is_state(Name, maps:get(states, Bound, [])) of
	true -> Name;
	false -> ["in->", Name]
    end;
expr({field,_Ln,Field},Bound,_) ->
    ["in->", field(Field,Bound)];
expr({array,_Ln,Values},Bound,_) ->
    ["{",lists:join(",", expr_list(Values,Bound)), "}"];
expr({call,_Ln,{'WORD',_Ln1,Name},Args},Bound,_) ->
    [Name, "(",lists:join(",", expr_list(Args,Bound)), ")"];
expr({Op,_Ln,M},Bound,P0) ->
    P1 = prec(1, Op),
    {LP,RP} = if P0 > P1 -> {"(",")"}; true -> {"", ""} end,
    [LP,atom_to_list(Op),expr(M,Bound,P1),RP];
expr({Op,_Ln,L,R},Bound,P0) ->
    P1 = prec(2, Op),
    {LP,RP} = if P0 > P1 -> {"(",")"}; true -> {"", ""} end,
    [LP,expr(L,Bound,P1), atom_to_list(Op), expr(R,Bound,P1),RP].

expr_list([],_Bound) -> [];
expr_list([E],Bound) -> [expr(E,Bound)];
expr_list([E|Es],Bound) -> [expr(E,Bound)|expr_list(Es,Bound)].


prec(1, '!') -> {105,right};
prec(1, '~') -> {105,right};
prec(1, '-') -> {105,right};
prec(1, '+') -> {105,right};
prec(2, '+') -> {90, left};
prec(2, '-') -> {90, left};
prec(2, '*') -> {100, left};
prec(2, '/') -> {100, left};
prec(2, '%') -> {100, left};
prec(2, '<<') -> {80, left};
prec(2, '>>') -> {80, left};
prec(2, '<') ->  {70, left};
prec(2, '<=') -> {70, left};
prec(2, '>') ->  {70, left};
prec(2, '>=') -> {70, left};
prec(2, '==') -> {60, left};
prec(2, '!=') -> {60, left};
prec(2, '&') ->  {50, left};
prec(2, '|') ->  {30, left};
prec(2, '^') ->  {40, left};
prec(2, '&&') -> {20, left};
prec(2, '||') -> {10, left};
prec(2, '=')  -> {5, right};
prec(2, '<-') -> {4, right}.
