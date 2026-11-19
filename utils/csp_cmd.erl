%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Talking to a node's console: where it is, a line out, a line back, and
%%%    the commands built on that -- for ./cs on the command line and for
%%%    candid's csp_link, which runs the same connection in a loop of its own.
%%%
%%% THE CONNECTION. target/1 reads where a node is, as candid's target field
%%% says it ("HOST:PORT", "/dev/ttyACM0[@BAUD]", "can0 to=ID from=ID", with
%%% flags after); open/1 connects and gives a connection C, a map. Lines go
%%% out with send/2 and come back with recv/2, synchronously; a process with
%%% a receive loop of its own hands each message to msg/2 instead, which says
%%% what lines it held. Either way a line is text without its end, flow
%%% control (XON/XOFF, which a node sends around every command) and prompts.
%%%
%%% THE COMMANDS. A dump (/dump rom, patch, eeprom, state, info) is its head,
%%% hex lines with an offset and a CRC each, and a `.'; dump/2 reads one,
%%% asking again for the windows that came wrong, and gives what
%%% csp_node:dumps/1 makes of it. info/1, program/1, state/2 and the rest are
%%% that and a line or two more. cmd/3 sends a line and gives back what the
%%% node said until it was quiet.
%%% @end
%%% Created : 18 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_cmd).

-export([target/1, where/1]).
-export([open/1, close/1, send/2, recv/2, msg/2, lines/1]).
-export([dump/2, dump/3, cmd/3]).
-export([info/1, program/1, program/2, state/2, pause/1, resume/1, step/2,
	 set/3, reset/1, live/1, latch/2, settings/1, save/1, load/1]).
-export([info_text/1]).

-define(XON,  16#11).
-define(XOFF, 16#13).
-define(TIMEOUT, 2000).      % ms a reply may take to start, and between lines
-define(TRIES, 5).           % a dump's windows asked for again, at most

%%% ------------------------------------------------------------ where

%% Where a node is, and how it is read:
%%   "" | "local"           a csp of our own, on vcan0 (csp_link runs it)
%%   "can0"                 a csp of our own, on that bus
%%   "/dev/ttyUSB0[@BAUD]"  a node's console on a serial line, 115200 if unsaid
%%   "HOST:PORT"            a node's console on tcp
%%   "can0 to=ID from=ID"   a node's console on CAN: we send on `to', it
%%                          answers on `from', 1-8 bytes a frame, no ack
%% Flags, after it:
%%   rom | patch | eeprom   the program without a patch, with the one RUNNING,
%%                          or with the one STORED (a node: patch)
%%   image=SLOT             a node: the program in that flash slot
%%   gap=MS                 CAN: between our frames (default 20)
%%   frame=N                CAN: bytes in a frame we send, 1-8 (default 4)
%% -> {ok, {Conn, #{patch => none | live | stored, image => none | string(),
%%                  gap, frame}}} | {error, Text}
target(S) ->
    case string:lexemes(S, " \t") of
	[] -> target("local", []);
	[Where | Flags] -> target(Where, Flags)
    end.

target(Where, Flags) ->
    case conn(Where) of
	{ok, C} ->
	    Base = #{patch => case C of
				  {local, _} -> none;
				  _ -> live
			      end,
		     image => none, gap => 20, frame => 4},
	    case flags(Flags, Base) of
		{ok, O = #{to := To, from := From}} ->
		    case C of
			{local, If} ->
			    {ok, {{can, If, To, From},
				  maps:remove(patch_set,
					      O#{patch := maps:get(patch_set, O,
								   live)})}};
			_ -> {error, "to= and from= are for a CAN interface"}
		    end;
		{ok, #{to := _}} -> {error, "to= wants a from= too"};
		{ok, #{from := _}} -> {error, "from= wants a to= too"};
		{ok, #{image := I}} when I =/= none, element(1, C) =:= local ->
		    {error, "image= is for a node; our own csp runs its file"};
		{ok, O} -> {ok, {C, maps:remove(patch_set, O)}};
		E -> E
	    end;
	E -> E
    end.

flags([], O)                 -> {ok, O};
flags(["rom" | T], O)        -> flags(T, O#{patch := none, patch_set => none});
flags(["patch" | T], O)      -> flags(T, O#{patch := live, patch_set => live});
flags(["eeprom" | T], O)     -> flags(T, O#{patch := stored, patch_set => stored});
flags(["image=" ++ I | T], O) when I =/= "" -> flags(T, O#{image := I});
flags(["to=" ++ N | T], O)   -> number(N, to, T, O);
flags(["from=" ++ N | T], O) -> number(N, from, T, O);
flags(["gap=" ++ N | T], O)  -> number(N, gap, T, O);
flags(["frame=" ++ N | T], O) ->
    case read_int(N) of
	{ok, V} when V >= 1, V =< 8 -> flags(T, O#{frame := V});
	_ -> {error, "frame= is 1 to 8: " ++ N}
    end;
flags([F | _], _)            -> {error, "unknown flag: " ++ F}.

number(N, Key, T, O) ->
    case read_int(N) of
	{ok, V} -> flags(T, O#{Key => V});
	error -> {error, atom_to_list(Key) ++ "= is not a number: " ++ N}
    end.

read_int("0x" ++ H) -> read_int(H, 16);
read_int(D)         -> read_int(D, 10).

read_int(S, Base) ->
    try {ok, list_to_integer(S, Base)} catch _:_ -> error end.

conn("local") -> {ok, {local, "vcan0"}};
conn(S = "/" ++ _) ->
    case string:split(S, "@") of
	[Dev] -> {ok, {uart, Dev, 115200}};
	[Dev, B] ->
	    case string:to_integer(B) of
		{N, ""} when N > 0 -> {ok, {uart, Dev, N}};
		_ -> {error, "baud is not a number: " ++ B}
	    end
    end;
conn(S) ->
    case string:split(S, ":", trailing) of
	[H, P] ->
	    case string:to_integer(P) of
		{N, ""} when N > 0, N < 65536 -> {ok, {tcp, H, N}};
		_ -> {error, "port is not a number: " ++ P}
	    end;
	[If] ->
	    case re:run(If, "^[a-z][a-z0-9_]*$") of
		{match, _} -> {ok, {local, If}};
		nomatch -> {error, "not a target: " ++ S}
	    end
    end.

%% A target, or a connection's, as it would be written.
where(#{target := T}) -> where(T);
where({tcp, H, P})    -> H ++ ":" ++ integer_to_list(P);
where({can, If, To, From}) ->
    lists:flatten(io_lib:format("~s ~.16#/~.16#", [If, To, From]));
where({uart, Dev, B}) -> Dev ++ "@" ++ integer_to_list(B);
where({local, If})    -> "local " ++ If.

%%% ------------------------------------------------------- the connection

%% A node's console, connected. Its messages come to the process that
%% opened it: recv/2 takes them, or a loop of its own gives them to msg/2.
%% Target: what target/1 gives, or a string it reads.
open(S) when is_list(S) ->
    case target(S) of
	{ok, T} -> open(T);
	E -> E
    end;
open({{local, _}, _}) ->
    {error, "a csp of our own is run by csp_link, not opened"};
open({Target, Opts}) ->
    case connect(Target, Opts) of
	{ok, Io} -> {ok, #{io => Io, target => Target, opts => Opts, part => <<>>,
			   lines => []}};
	E -> E
    end.

connect({tcp, H, P}, _Opts) ->
    case gen_tcp:connect(H, P, [binary, {packet, 0}, {active, true}], 3000) of
	{ok, S} -> {ok, {tcp, S}};
	E -> E
    end;
%% The can application runs already (candid.config, or whoever opens this).
connect({can, If, To, From}, Opts) ->
    try
	%% #can_if{}: the router's id is element 4, its parameters element 7.
	case [element(4, I) || I <- can_router:interfaces(),
			       maps:get(device, element(7, I), none) =:= If] of
	    [Id | _] ->
		ok = can_router:attach(),
		{ok, {can, Id, To, From, maps:with([gap, frame], Opts)}};
	    [] -> {error, "the can application has no " ++ If}
	end
    catch
	error:undef -> {error, "no can application on the code path"}
    end;
%% Raw bytes, split into lines here: in {packet, line} the driver hands over
%% one line per read and keeps the rest of a burst until more bytes come, so
%% a dump stalled half way.
connect({uart, Dev, Baud}, _Opts) ->
    try uart:open1(Dev, [{baud, Baud}, {mode, binary}, {packet, 0},
			 {active, true}]) of
	{ok, U} -> {ok, {uart, U}};
	E -> E
    catch
	error:undef -> {error, "no uart application on the code path"}
    end.

close(#{io := {tcp, S}})  -> gen_tcp:close(S);
close(#{io := {uart, U}}) -> uart:close(U);
close(#{io := {can, _, _, _, _}}) -> can_router:detach().

%% A line to the node; its end is added.
send(C, Line) -> out(C, [Line, "\n"]).

out(#{io := {tcp, S}}, Data)  -> gen_tcp:send(S, Data);
out(#{io := {uart, U}}, Data) -> uart:send(U, Data);
out(#{io := {can, Id, To, _From, #{gap := Gap, frame := N}}}, Data) ->
    lists:foreach(fun(B) ->
			  can:sync_send(can:create(To, Id, B)),
			  timer:sleep(Gap)
		  end, chunks(iolist_to_binary(Data), N)).

chunks(Bin, N) when byte_size(Bin) > N ->
    <<B:N/binary, T/binary>> = Bin,
    [B | chunks(T, N)];
chunks(B, _) -> [B].

%% What a message from the node holds: {lines, Lines, C} -- maybe none, the
%% rest of a line kept for the next -- {closed, Why}, or `no': not ours.
msg(C = #{io := {tcp, S}}, {tcp, S, Data}) -> bytes(C, Data);
msg(C = #{io := {uart, U}}, {uart, U, Data}) -> bytes(C, Data);
%% #can_frame{id, len, data, intf, ts}: ours only, on the interface we named.
msg(C = #{io := {can, Id, _, From, _}}, {can_frame, From, _, Data, Id, _}) ->
    bytes(C, Data);
msg(#{io := {tcp, S}}, {tcp_closed, S}) -> {closed, closed};
msg(#{io := {tcp, S}}, {tcp_error, S, Why}) -> {closed, Why};
msg(#{io := {uart, U}}, {uart_closed, U}) -> {closed, closed};
msg(#{io := {uart, U}}, {uart_error, U, Why}) -> {closed, Why};
msg(_C, _Msg) -> no.

%% XOFF/XON are flow control, never text: a node holds the line around every
%% command it parses (serial_hold), on USB too, and "\^Sdump rom 1300" is not a
%% dump head.
bytes(C = #{part := P}, Data0) ->
    Data = binary:replace(iolist_to_binary(Data0), [<<?XOFF>>, <<?XON>>], <<>>,
			  [global]),
    case binary:split(<<P/binary, Data/binary>>, <<"\n">>, [global]) of
	[Rest] -> {lines, [], C#{part := Rest}};
	Ls ->
	    {Lines, [Rest]} = lists:split(length(Ls) - 1, Ls),
	    {lines, [line(L) || L <- Lines], C#{part := Rest}}
    end.

%% A line as text: no CR, no prompts in front ("> > dump ...").
line(B) -> prompts(string:trim(binary_to_list(B))).

prompts("> " ++ L) -> prompts(string:trim(L, leading));
prompts(">") -> "";
prompts(L) -> L.

%% The next line, Timeout ms at most: {ok, Line, C} | {timeout, C} |
%% {closed, Why}. Lines a message brought beyond the first wait in C; what is
%% not from the node stays in the queue.
recv(C = #{lines := [L | Ls]}, _Timeout) -> {ok, L, C#{lines := Ls}};
recv(C = #{io := Io}, Timeout) ->
    case wait(Io, Timeout) of
	timeout -> {timeout, C};
	Msg ->
	    case msg(C, Msg) of
		{lines, Ls, C1} -> recv(C1#{lines := Ls}, Timeout);
		{closed, Why} -> {closed, Why}
	    end
    end.

wait({tcp, S}, T) ->
    receive
	M = {tcp, S, _} -> M;
	M = {tcp_closed, S} -> M;
	M = {tcp_error, S, _} -> M
    after T -> timeout
    end;
wait({uart, U}, T) ->
    receive
	M = {uart, U, _} -> M;
	M = {uart_closed, U} -> M;
	M = {uart_error, U, _} -> M
    after T -> timeout
    end;
wait({can, Id, _, From, _}, T) ->
    receive
	M = {can_frame, From, _, _, Id, _} -> M
    after T -> timeout
    end.

%% Every line that comes until the node is quiet for Quiet ms.
lines(C) -> lines(C, 300, []).

lines(C, Quiet, Acc) ->
    case recv(C, Quiet) of
	{ok, "", C1} -> lines(C1, Quiet, Acc);
	{ok, L, C1} -> lines(C1, Quiet, [L | Acc]);
	{timeout, C1} -> {lists:reverse(Acc), C1};
	{closed, _} -> {lists:reverse(Acc), C}
    end.

%%% ---------------------------------------------------------- the commands

%% A line, and what the node said back until it was quiet: {Lines, C}. Its
%% own echo is not among them.
cmd(C, Line, Quiet) ->
    send(C, Line),
    {Ls, C1} = lines(C, Quiet, []),
    Echo = prompts(lists:flatten(Line)),
    {[L || L <- Ls, L =/= Echo], C1}.

%% /dump Kind [Args]: {ok, Dump, C} -- what csp_node:dumps/1 makes of it,
%% {rom, Bin}, {state, ...}, {info, Bin} -- with the windows that came wrong
%% asked for again; {error, Why, C}: refused (`ERR ...'), not understood, or
%% no answer.
dump(C, Kind) -> dump(C, Kind, []).

dump(C, Kind, Args) ->
    Cmd = lists:join(" ", ["/dump", Kind | Args]),
    send(C, Cmd),
    case read(C, Kind) of
	{ok, Text, C1} -> whole(C1, Cmd, csp_node:dumps(Text), ?TRIES);
	E -> E
    end.

%% Up to the dump's head: what comes before it is the echo and the prompt.
head(C, Kind) ->
    case recv(C, ?TIMEOUT) of
	{ok, L = "dump " ++ K, C1} ->
	    case string:lexemes(K, " ") of
		[Kind | _] -> {ok, L, C1};
		_ -> head(C1, Kind)
	    end;
	{ok, L = "ERR " ++ _, C1} -> {error, L, C1};
	{ok, _, C1} -> head(C1, Kind);
	{timeout, C1} -> {error, timeout, C1};
	{closed, Why} -> {error, {closed, Why}, C}
    end.

%% A dump cut short is what came of it: a part, its windows asked for again.
body(C, Acc) ->
    case recv(C, ?TIMEOUT) of
	{ok, ".", C1} -> {ok, text(Acc), C1};
	{ok, L, C1} -> body(C1, [L | Acc]);
	{timeout, C1} -> {ok, text(Acc), C1};
	{closed, Why} -> {error, {closed, Why}, C}
    end.

text(Acc) -> lists:flatten(lists:join("\n", lists:reverse(Acc, ["."]))).

%% A part asked for again, window by window, until it is whole.
whole(C, Cmd, [N = {part, K, P}], _Tries) ->
    again(C, Cmd, K, P#{have := []}, N, ?TRIES);
whole(C, _Cmd, [{unknown, Head}], _Tries) -> {error, {unknown, Head}, C};
whole(C, _Cmd, [D], _Tries) -> {ok, D, C};
whole(C, _Cmd, _, _Tries) -> {error, nothing, C}.

again(C, Cmd, K, Piece, New, Tries) ->
    case csp_node:merge(Piece, New) of
	{K, Bin} -> {ok, {K, Bin}, C};
	{part, K, P1 = #{want := [{O, L} | _]}} when Tries > 0 ->
	    send(C, [Cmd, " ", integer_to_list(O), " ", integer_to_list(L)]),
	    case read(C, atom_to_list(K)) of
		{ok, Text, C1} ->
		    case csp_node:dumps(Text) of
			[N = {part, K, _}] -> again(C1, Cmd, K, P1, N, Tries - 1);
			Other -> {error, {window, Other}, C1}
		    end;
		E -> E
	    end;
	_ -> {error, {incomplete, K}, C}
    end.

%% A dump's text, head to `.'.
read(C, Kind) ->
    case head(C, Kind) of
	{ok, Head, C1} -> body(C1, [Head]);
	E -> E
    end.

%% What the node is: csp_node:info/1's map. {error, Why, C} from a node from
%% before /dump info.
info(C) ->
    case dump(C, "info") of
	{ok, {info, Bin}, C1} -> {ok, csp_node:info(Bin), C1};
	{ok, Other, C1} -> {error, {not_info, Other}, C1};
	E -> E
    end.

%% The program a node runs: the image (the running one, or Opts' image=SLOT)
%% and the patch Opts says -- the running one unless told -- as
%% csp_node:program/1 reads them: {ok, Dumps, C}, Dumps [{rom, Bin} | {patch |
%% eeprom, Bin}]. Nothing stored is an image with no patch.
program(C = #{opts := Opts}) -> program(C, Opts).

program(C, Opts) ->
    Rom = case maps:get(image, Opts, none) of
	      none -> [];
	      I -> [I]
	  end,
    case dump(C, "rom", Rom) of
	{ok, {rom, Bin}, C1} ->
	    case maps:get(patch, Opts, live) of
		none -> {ok, [{rom, Bin}], C1};
		P ->
		    K = case P of live -> "patch"; stored -> "eeprom" end,
		    case dump(C1, K) of
			{ok, D = {_, _}, C2} -> {ok, [{rom, Bin}, D], C2};
			{error, "ERR " ++ _, C2} -> {ok, [{rom, Bin}], C2};
			E -> E
		    end
	    end;
	{ok, Other, C1} -> {error, {not_rom, Other}, C1};
	E -> E
    end.

%% The values, by name, against the program's declarations (from program/1
%% and csp_node:program/1): {ok, Cycle, Ms, [{Name, Value}], C}.
state(C, Decls) ->
    case dump(C, "state") of
	{ok, D = {state, Cycle, Ms, _, _}, C1} ->
	    {ok, Cycle, Ms, csp_node:state(Decls, D), C1};
	{ok, Other, C1} -> {error, {not_state, Other}, C1};
	E -> E
    end.

pause(C)     -> cmd(C, "/pause", 300).
resume(C)    -> cmd(C, "/resume", 300).
reset(C)     -> cmd(C, "/reset", 300).
%% A node steps in its own loop after the line is read: N cycles, then still.
%% What was set while paused is committed first, or the step reads the old.
step(C, N) ->
    {L1, C1} = cmd(C, "/commit", 100),
    {L2, C2} = cmd(C1, "/step " ++ integer_to_list(N), 300),
    {[L || L <- L1, L =/= "Committed"] ++ L2, C2}.
set(C, Name, V) when is_integer(V) -> set(C, Name, integer_to_list(V));
set(C, Name, V) -> cmd(C, ["> ", Name, " = ", V], 300).
%% Rules frozen, I/O running: a value set is on the pin at once. /resume
%% (resume/1) runs the rules again.
live(C)      -> cmd(C, "/live", 300).
%% Device output on or off; the node says nothing back when it took.
latch(C, true)  -> cmd(C, "/latch on", 300);
latch(C, false) -> cmd(C, "/latch off", 300).
%% What the unit is configured to: a #param or a pin's part set by hand
%% (set/3), kept by name across a reflash once saved. The node's lines.
settings(C)  -> cmd(C, "/settings", 300).
%% The patch and the settings to storage, and back. A write to EEPROM or
%% flash: once, when it is meant, never in a loop.
save(C)      -> cmd(C, "/save", 1000).
load(C)      -> cmd(C, "/load", 1000).

%%% ------------------------------------------------------------ in words

%% The node in a line: its part, firmware, number format, room, slots.
info_text(I) ->
    Float = case maps:get(float, I, fixpoint) of
		fixpoint -> "Q16.16";
		ieee -> "IEEE float"
	    end,
    Ram = case I of
	      #{ram := #{total := T, free := F}} ->
		  io_lib:format(", RAM ~w free of ~w", [F, T]);
	      _ -> ""
	  end,
    Slots = [[", ", N, ": ", slot_text(R)]
	     || R = #{kind := app, name := N} <- maps:get(regions, I, [])],
    Who = [X || X <- [maps:get(board, I, ""), maps:get(device, I, "")], X =/= ""],
    lists:flatten([lists:join(" ", Who ++ ["?" || Who =:= []]),
		   " fw ", maps:get(fw, I, "?"),
		   ", ", Float, " ", atom_to_list(maps:get(endian, I, little)),
		   "-endian", Ram, Slots]).

slot_text(#{slot := image, image := #{role := R, gen := G, size := Sz}}) ->
    io_lib:format("~s gen ~w (~w B)", [R, G, Sz]);
slot_text(#{slot := S}) -> atom_to_list(S).
