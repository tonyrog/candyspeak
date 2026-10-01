%%% Drives one csp process as an Erlang port.
%%%
%%% The owner receives {csp_state, Cycle, [{Name,Value}]} for every state dump
%%% csp prints. Note that csp dumps ON CHANGE, not every cycle, so a trace must
%%% hold its last value between dumps rather than expect one per tick.
%%%
%%% Ticking is ours to drive: /commit runs exactly one cycle and prints nothing
%%% but the dump. /state would also step, but adds a page of human text to parse
%%% past, and an empty line steps nothing at all.

-module(csp_link).

-export([open/2, close/1, set/3, tick/1]).
-export([pause/1, resume/1, step/1, step/2]).
-export([init/3]).

open(Exe, Files) ->
    Owner = self(),
    {ok, spawn_link(?MODULE, init, [Owner, Exe, Files])}.

close(L)          -> L ! close, ok.
set(L, Name, Val) -> L ! {set, Name, Val}, ok.
tick(L)           -> L ! tick, ok.

%% Stepping, which is not the same as ticking.
%%
%% `tick' is /commit: it runs a cycle in a RUNNING program, and a `> X = 1' sent
%% just before it runs a cycle of its OWN -- so three values for the same instant
%% become three cycles. Under /pause they do not: values are set without running
%% anything, and /step N runs exactly N cycles and stops again. That is the
%% sequence that behaves like hardware, where every DIN is committed at the top
%% of one cycle, and it is what a replayed model trace needs to match its source.
pause(L)          -> L ! pause, ok.
resume(L)         -> L ! resume, ok.
step(L)           -> step(L, 1).
step(L, N)        -> L ! {step, N}, ok.

init(Owner, Exe, Files) ->
    %% Trap exits so the owner dying becomes a MESSAGE rather than killing us
    %% outright. Without this the panel's death takes this process with it
    %% before it can send /quit, the port closes, and csp -- whose own help
    %% says EOF ends the prompt and not the program -- runs on forever.
    process_flag(trap_exit, true),
    link(Owner),
    %% NO --virtual-time. It jumps the clock to the next timer deadline, so a
    %% 500 ms phase fires on every tick and traffic.csp runs four times too
    %% fast (measured: 202 ms between phases against 1161 in real time). The
    %% panel wants wall time -- watching it is the point -- and real time is
    %% what csp does when the flag is absent.
    %%
    %% --exit-on-eof: when our end of the pipe goes, so should csp.
    %%
    %% NO -b, though it would be the right thing. Starting paused would give the
    %% trace a known zero point -- without it csp runs INIT and whatever cycles
    %% fit before the first dump, so a step starts from wherever the scheduler
    %% was. But `-b -Q' produces NO DUMPS AT ALL (measured: 0 against 3), and
    %% the dump stream is the panel's only way of seeing anything. Put the -b
    %% back when that combination works.
    Port = open_port({spawn_executable, Exe},
		     [{args, ["-i", "--no-eeprom", "-Q", "-Lerlang",
			      "--exit-on-eof" | Files]},
		      exit_status, use_stdio, stderr_to_stdout,
		      {line, 4096}]),
    loop(Port, Owner, []).

loop(Port, Owner, Buf) ->
    receive
	{Port, {data, {eol, Line}}} ->
	    loop(Port, Owner, feed(Owner, Buf, Line));
	{Port, {data, {noeol, Line}}} ->
	    loop(Port, Owner, feed(Owner, Buf, Line));
	{Port, {exit_status, S}} ->
	    Owner ! {csp_exit, S};
	{set, Name, Val} ->
	    port_command(Port, ["> ", Name, " = ", fmt(Val), "\n"]),
	    loop(Port, Owner, Buf);
	tick ->
	    port_command(Port, "/commit\n"),
	    loop(Port, Owner, Buf);
	pause ->
	    port_command(Port, "/pause\n"),
	    loop(Port, Owner, Buf);
	resume ->
	    port_command(Port, "/resume\n"),
	    loop(Port, Owner, Buf);
	{step, N} ->
	    port_command(Port, ["/step ", integer_to_list(N), "\n"]),
	    loop(Port, Owner, Buf);
	close ->
	    shutdown(Port);
	%% The panel went away: browser closed, or it crashed. Same handling --
	%% we are the only one holding this csp, so it is ours to end.
	{'EXIT', Owner, _Reason} ->
	    shutdown(Port);
	{'EXIT', _Other, _Reason} ->
	    loop(Port, Owner, Buf)
    end.

%% /quit makes csp exit at once (measured: 0 ms), so wait for that rather than
%% just closing the port. Closing alone leaves it running, and walking away
%% without sending anything is what left processes behind.
shutdown(Port) ->
    port_command(Port, "/quit\n"),
    receive
	{Port, {exit_status, _}} -> ok
    after 500 ->
	    %% /quit did not take. Close the pipe and make sure of the process;
	    %% by here it is either wedged or ignoring us.
	    OsPid = case erlang:port_info(Port, os_pid) of
			{os_pid, Pid} -> Pid;
			undefined     -> undefined
		    end,
	    try port_close(Port) catch _:_ -> ok end,
	    kill(OsPid)
    end.

kill(undefined) -> ok;
kill(Pid)       -> os:cmd("kill -TERM " ++ integer_to_list(Pid)), ok.

fmt(V) when is_integer(V) -> integer_to_list(V);
fmt(V) when is_list(V)    -> V.

%%% A dump spans three lines -- "{state,N,[", the values, then "]}." -- and the
%%% prompt "> " may be prepended to any of them, sometimes twice.

feed(Owner, [], Line) ->
    case string:find(Line, "{state,") of
	nomatch -> [];
	Rest    -> hold(Owner, [Rest])
    end;
feed(Owner, Buf, Line) ->
    hold(Owner, [strip(Line) | Buf]).

hold(Owner, Buf = [Last | _]) ->
    case lists:suffix("]}.", Last) of
	true  -> emit(Owner, lists:flatten(lists:reverse(Buf))), [];
	false -> Buf
    end.

strip("> " ++ Rest) -> strip(Rest);
strip(Line)         -> Line.

emit(Owner, Text) ->
    case erl_scan:string(Text) of
	{ok, Toks, _} ->
	    case erl_parse:parse_term(Toks) of
		{ok, {state, N, Vals}} -> Owner ! {csp_state, N, flat(Vals)};
		_                      -> ok
	    end;
	_ -> ok
    end.

%% {var,Name,V} | {digital,Name,V} | {analog,Name,V} | {object,Name,[...]}
%%
%% An ARRAY arrives as the name once and then one entry per further element with
%% an EMPTY name: `#analog P[10]:16` is {analog,"P",V0} followed by nine
%% {analog,"",Vn}. Name them P[0]..P[9] here so everything above deals in plain
%% names, and a panel widget can be addressed per element.
flat(Vals) -> number(lists:flatten([fv(V) || V <- Vals]), undefined, 0).

fv({object, _Name, Inner}) -> [fv(V) || V <- Inner];
fv({_Kind, Name, Value})   -> [{Name, Value}];
fv(_)                      -> [].

number([], _Base, _N) ->
    [];
number([{"", V} | T], Base, N) when Base =/= undefined ->
    [{elem(Base, N), V} | number(T, Base, N + 1)];
number([{"", _V} | T], undefined, N) ->
    %% an unnamed entry with nothing before it: nothing to attach it to
    number(T, undefined, N);
number([{Name, V} | T], _Base, _N) ->
    %% A named entry may be element 0 of an array or a plain scalar -- we
    %% cannot tell from the dump, so keep the bare name AND offer it as [0].
    %% The widget side asks for whichever it declared.
    [{Name, V}, {elem(Name, 0), V} | number(T, Name, 1)].

elem(Base, N) -> Base ++ "[" ++ integer_to_list(N) ++ "]".
