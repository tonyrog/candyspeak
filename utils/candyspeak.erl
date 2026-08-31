%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     Parse 
%%% @end
%%% Created : 31 Aug 2026 by Tony Rogvall <tony@rogvall.se>

-module(candyspeak).

-export([file/1]).

file(Filename) ->
    case file:read_file(Filename) of
	{ok,Bin} ->
	    case candyspeak_scan:string(binary_to_list(Bin)) of
		{ok,Ts,_EndLine} ->
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
	    end;
	Error ->
	    io:format("error: ~p\n", [Error]),
	    halt(1)
    end.		
