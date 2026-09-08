%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     Parse 
%%% @end
%%% Created : 31 Aug 2026 by Tony Rogvall <tony@rogvall.se>

-module(candyspeak).

-export([start/0, start/1]).
-export([main/1]).
-export([tokens/1, parse/1]).

start() ->
    io:format("candyspeak <file>\n", []),
    halt(1).

start([Filename|Fs]) when is_atom(Filename) ->
    main([atom_to_list(F) || F <- [Filename|Fs]]).

main(Args) ->
    files(Args).

files([Filename|Fs]) ->
    parse(Filename),
    files(Fs);
files([]) ->
    ok.

parse(Filename) ->
    case tokens(Filename) of
	{ok,Ts} ->
	    io:format("Ts=~p\n", [Ts]),
	    case candyspeak_parse:parse(Ts) of
		{error,{Ln,Mod,Message}} ->
		    io:format("~s:~w: ~s\n", 
			      [Filename,Ln,
			       apply(Mod,format_error,[Message])]),
		    halt(1);
		{ok,M0} ->
		    io:format("~p\n", [M0]),
		    halt(0)
	    end;
	Error ->
	    io:format("error: ~p\n", [Error]),
	    halt(1)
    end.		

tokens(Filename) ->
    case file:read_file(Filename) of
	{ok,Bin} ->
	    case candyspeak_scan:string(binary_to_list(Bin)) of
		{ok,Ts,_EndLine} ->
		    {ok, Ts};
		Error -> 
		    Error
	    end;
	Error ->
	    Error
    end.
