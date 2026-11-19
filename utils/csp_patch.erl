%%% @doc
%%%    A patch for a node, made on the host: the lines a person (or the chat)
%%%    would type at its prompt -- declarations, rules, #disable/#enable --
%%%    compiled by the host's own csp running the NODE'S image and patch
%%%    (--image, the store as its eeprom), so what comes out is what the node
%%%    would have made of the same lines. Then only what they added is sent,
%%%    as /patch's hex lines: for a node with no compiler of its own
%%%    (CSP_EXEC_ONLY), and for any other.
%%%
%%%    compile/4  the lines over Rom and Store: the store after them
%%%    delta/3    old store to new: the bytes /patch takes
%%%    names/2    values by name to set, and a save: the bytes /patch takes
%%%    lines/1    those bytes as /upgrade-style hex lines
%%%    send/2     the lines to a node over its console (csp_cmd)
%%%
%%%    The host's csp starts paused (-b) and never runs a cycle: a transport
%%%    opens when a cycle sends or receives, so the node's tcp ports, uarts
%%%    and buses are left alone.
%%% @end

-module(csp_patch).

-export([compile/4, delta/3, names/2, lines/1, send/2]).

-define(CHUNK, 32).                     % bytes a hex line carries
-define(TIMEOUT, 10000).               % ms the host csp may take, all of it

%% Lines over the program Rom + Store (none: no patch) runs, by Exe (the
%% host csp of the node's number format). {ok, NewStore} | {error, Why},
%% Why {line, N, Line, Said} for a line refused.
compile(Rom, Store, Lines, Exe) ->
    Dir = scratch(),
    Img = filename:join(Dir, "node.img"),
    Db = filename:join(Dir, "node.db"),
    ok = file:write_file(Img, Rom),
    _ = file:delete(Db),
    Ee = case Store of
	     none -> ["--no-eeprom"];
	     _ -> ok = file:write_file(Db, Store), []
	 end,
    Args = ["-b", "--image=" ++ Img, "-e", Db | Ee],
    Port = open_port({spawn_executable, Exe},
		     [{args, Args}, {line, 4096}, exit_status, use_stdio,
		      stderr_to_stdout]),
    Ls = [L || {_, L} <- Lines],
    [port_command(Port, [L, "\n"]) || L <- Ls ++ ["/dump patch", "/quit"]],
    case collect(Port, []) of
	{ok, Out} -> answers(Lines, Out);
	E -> E
    end.

collect(Port, Acc) ->
    receive
	{Port, {data, {eol, L}}} -> collect(Port, [L | Acc]);
	{Port, {data, {noeol, L}}} -> collect(Port, [L | Acc]);
	{Port, {exit_status, _}} -> {ok, lists:reverse(Acc)}
    after ?TIMEOUT ->
	    try port_close(Port) catch _:_ -> ok end,
	    {error, "the host's csp did not answer"}
    end.

%% What csp said after each line it echoed; the first Error is the line's.
answers(Lines, Out) ->
    Said = said(Out, none, #{}),
    case [{N, L, E} || {N, L} <- Lines,
		       E <- [[X || X <- maps:get(L, Said, []),
				   lists:prefix("Error", X)]],
		       E =/= []] of
	[{N, L, E} | _] -> {error, {line, N, L, string:join(E, " ")}};
	[] ->
	    Text = string:join(maps:get("/dump patch", Said, []), "\n"),
	    case [B || {patch, B} <- csp_node:dumps(Text)] of
		[Bin] -> {ok, Bin};
		_ -> {error, "the host's csp gave no patch"}
	    end
    end.

said(["> " ++ L | Out], _At, M) -> said(Out, L, M#{L => maps:get(L, M, [])});
said([_ | Out], none, M) -> said(Out, none, M);
said([X | Out], At, M) -> said(Out, At, M#{At => maps:get(At, M) ++ [X]});
said([], _At, M) -> M.

%% Old store to New: the head, what New adds and the records of Old it
%% changes (a name into a segment with room, a variable marked read), and
%% the #disable set whole. Old's records only: the image is never touched.
delta(Rom, Old, New) ->
    {ok, N} = csp_disasm:store(Rom, New),
    O = case Old of
	    none -> #{n_decl => 0, n_instr => 0, decls => <<>>, instrs => <<>>};
	    _ -> {ok, O0} = csp_disasm:store(Rom, Old), O0
	end,
    #{decls := Od, instrs := Oi} = O,
    #{decls := Nd, instrs := Ni} = N,
    case byte_size(Nd) >= byte_size(Od) andalso byte_size(Ni) >= byte_size(Oi) of
	false -> {error, "the lines take away from what the node runs"};
	true ->
	    {ChD, AddD} = split(Od, Nd, 8),
	    {ChI, AddI} = split(Oi, Ni, 4),
	    Head = <<"CSPD", 1, (flags(Rom)),
		     (maps:get(rom_fp, N)):16/little,
		     (maps:get(n_decl, O)):16/little,
		     (maps:get(n_instr, O)):16/little,
		     (byte_size(AddD) div 8):16/little,
		     (byte_size(AddI) div 4):16/little,
		     (maps:get(nq, N)):16/little,
		     (maps:get(n_dis, N)):16/little,
		     (length(ChD)):16/little,
		     (length(ChI)):16/little>>,
	    {ok, iolist_to_binary([Head, AddD,
				   [<<I:16/little, R/binary>> || {I, R} <- ChD],
				   [<<I:16/little, R/binary>> || {I, R} <- ChI],
				   AddI, maps:get(dis, N)])}
    end.

%% Values by name, and what to do after: [save]. NVs [{Name, Value}], a
%% value {int, I} | {float, F} | {string, S}; the node finds each name in
%% what it runs, checks them all and sets them as `> Name = V' would.
names(NVs, Ops) ->
    Body = iolist_to_binary([[length(N), N, nv(V)] || {N, V} <- NVs]),
    <<"CSPN", 1, (case lists:member(save, Ops) of true -> 1; false -> 0 end),
      (byte_size(Body)):16/little, Body/binary>>.

%% The kinds: include/csp_reply.h, CSP_NV_*.
nv({int, I}) -> <<0, (I band 16#FFFFFFFF):32/little>>;
nv({float, F}) -> <<2, F:32/float-little>>;
nv({string, S}) -> [3, length(S), S].

%% Old's records that New has otherwise, {Index, NewRecord}, and what New
%% has after them.
split(Old, New, Sz) ->
    Ch = [{I, binary:part(New, I * Sz, Sz)}
	  || I <- lists:seq(0, byte_size(Old) div Sz - 1),
	     binary:part(Old, I * Sz, Sz) =/= binary:part(New, I * Sz, Sz)],
    {Ch, binary:part(New, byte_size(Old), byte_size(New) - byte_size(Old))}.

%% CSP_IMG_FLAGS of the node: its image's flags byte (format 23).
flags(<<_:7/binary, F, _/binary>>) -> F.

%% Bin as /patch's lines: offs hex crc, then `. size crc'.
lines(Bin) ->
    [line(Off, binary:part(Bin, Off, min(?CHUNK, byte_size(Bin) - Off)))
     || Off <- lists:seq(0, byte_size(Bin) - 1, ?CHUNK)]
	++ [lists:flatten(io_lib:format(". ~w ~4.16.0B",
					[byte_size(Bin), crc16(16#ffff, Bin)]))].

line(Off, Data) ->
    Crc = crc16(crc16(16#ffff, <<Off:32/little>>), Data),
    lists:flatten(io_lib:format("~w ~s ~4.16.0B",
				[Off, [io_lib:format("~2.16.0B", [B])
				       || <<B>> <= Data], Crc])).

%% CRC-16/CCITT, seeded by the caller, as csp_crc16.
crc16(Crc, <<B, Rest/binary>>) -> crc16(crc8(8, Crc bxor (B bsl 8)), Rest);
crc16(Crc, <<>>) -> Crc band 16#ffff.

crc8(0, C) -> C;
crc8(I, C) when C band 16#8000 =/= 0 -> crc8(I - 1, ((C bsl 1) bxor 16#1021) band 16#ffff);
crc8(I, C) -> crc8(I - 1, (C bsl 1) band 16#ffff).

%% The lines to the node, one answer each. {ok, Said, C} | {error, Why, C}.
send(C, Bin) ->
    case csp_cmd:cmd(C, "/patch", 1000) of
	{["OK" ++ _ | _], C1} -> send_lines(C1, lines(Bin));
	{Said, C1} -> {error, "the node takes no /patch: " ++ string:join(Said, " "), C1}
    end.

send_lines(C, [L]) ->                          % `. size crc'
    {Said, C1} = csp_cmd:cmd(C, L, 3000),
    case [S || S = "OK" ++ _ <- Said] of
	[Ok | _] -> {ok, ok_text(string:lexemes(Ok, " ")), C1};
	[] -> {error, string:join(Said, " "), C1}
    end;
send_lines(C, [L | Ls]) ->
    case csp_cmd:cmd(C, L, 1000) of
	{["OK" ++ _ | _], C1} -> send_lines(C1, Ls);
	{Said, C1} ->
	    {_, C2} = csp_cmd:cmd(C1, "!", 1000),
	    {error, string:join(Said, " "), C2}
    end.

%% The node's `OK n m' (a program: declarations and instructions added) or
%% `OK n' (values set), in words.
ok_text(["OK", D, I]) -> "OK " ++ D ++ " decls, " ++ I ++ " instrs added -- not saved";
ok_text(["OK", N]) -> "OK " ++ N ++ " set";
ok_text(Ws) -> string:join(Ws, " ").

scratch() ->
    D = filename:join(case os:getenv("TMPDIR") of
			  false -> "/tmp";
			  T -> T
		      end, "csp_patch_" ++ os:getpid()),
    ok = filelib:ensure_dir(filename:join(D, "x")),
    D.
