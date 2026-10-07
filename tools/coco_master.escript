#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% CoCo master over serial -- CoCoMaster.ino and pdb_coco.c on a laptop.
%%
%% Talks CoCo's byte protocol (boards/coco/link.c, CoCo.ino's command()) to a
%% CoCo on a serial port, or to `tmp/clib_host/main -P` on a pty:
%%
%%   coco_master.escript PORT sync
%%   coco_master.escript PORT status          the record, and acknowledges it
%%   coco_master.escript PORT capture
%%   coco_master.escript PORT get NAME|INDEX SUB
%%   coco_master.escript PORT set NAME|INDEX SUB VALUE
%%   coco_master.escript PORT dict            every entry, read back
%%   coco_master.escript PORT watch [MS]      status each period, what woke it
%%
%% NAME is an index without its INDEX_ prefix, any case: adc_delta,
%% polarity_input8. Numbers take 0x. -b BAUD before PORT sets the speed (9600,
%% as CoCo.ino's USE_SERIAL).
%%
%% Every byte sent gets one byte back, and that byte is the answer to the byte
%% BEFORE -- an SPI shift register, kept over the UART. So a command is a burst
%% of transfers, as in CoCoMaster.ino, and each reply is read one transfer late.
%%
%% Needs the uart application: ~/erlang/uart, or wherever UART_EBIN points.

-define(NONE,    16#00).
-define(SYN,     16#01).
-define(STATUS,  16#02).
-define(CAPTURE, 16#03).
-define(SET,     16#04).
-define(GET,     16#05).
-define(ACK,     16#81).

%% pds_index.h, the part CoCo has.
index() ->
    [{"READ_INPUT8",                      16#6000},
     {"POLARITY_INPUT8",                  16#6002},
     {"FILTER_CONSTANT_INPUT8",           16#6003},
     {"GLOBAL_INTERRUPT_ENABLED_DIGITAL", 16#6005},
     {"INTERRUPT_MASK_ANY_CHANGE8",       16#6006},
     {"INTERRUPT_MASK_LOW_TO_HIGH8",      16#6007},
     {"INTERRUPT_MASK_HIGH_TO_LOW8",      16#6008},
     {"ADC_READ16",                       16#6401},
     {"ADC_FLAGS",                        16#6421},
     {"GLOBAL_INTERRUPT_ENABLED_ANALOG",  16#6423},
     {"ADC_UPPER",                        16#6424},
     {"ADC_LOWER",                        16#6425},
     {"ADC_DELTA",                        16#6426},
     {"ADC_NDELTA",                       16#6427},
     {"ADC_PDELTA",                       16#6428},
     {"ADC_OFFSET",                       16#6431},
     {"ADC_SCALE",                        16#6432},
     {"ADC_MIN",                          16#2768},
     {"ADC_MAX",                          16#2769},
     {"ADC_INHIBIT",                      16#276A},
     {"ADC_DELAY",                        16#276B}].

%% Which subindexes an entry has: 0 for a global, 1 for the digital byte,
%% 1..4 for the four analog channels.
subindexes("GLOBAL_" ++ _) -> [0];
subindexes("ADC_" ++ _) -> [1, 2, 3, 4];
subindexes(_) -> [1].

main(Args) ->
    ok = uart_path(),
    {Baud, Rest} = case Args of
                       ["-b", B | T] -> {list_to_integer(B), T};
                       T -> {9600, T}
                   end,
    case Rest of
        [Port, Cmd | CmdArgs] -> run(Port, Baud, Cmd, CmdArgs);
        _ -> usage()
    end.

usage() ->
    io:format(standard_error,
              "usage: coco_master.escript [-b BAUD] PORT "
              "sync|status|capture|get|set|dict|watch [ARGS]\n", []),
    halt(1).

uart_path() ->
    case code:which(uart) of
        non_existing ->
            Ebin = case os:getenv("UART_EBIN") of
                       false -> filename:join([os:getenv("HOME"), "erlang", "uart", "ebin"]);
                       E -> E
                   end,
            true = code:add_patha(Ebin),
            ok;
        _ -> ok
    end.

run(Port, Baud, Cmd, Args) ->
    {ok, U} = uart:open(Port, [{baud, Baud}, {mode, binary}, {active, false}]),
    %% An Arduino resets when the port opens: give it time to come up, and drop
    %% whatever it said meanwhile.
    case lists:prefix("/dev/pts/", Port) of
        true -> ok;
        false -> timer:sleep(2000)
    end,
    uart:flush(U, input),
    sync(U) orelse fail("no SYN answer from CoCo on " ++ Port),
    cmd(U, Cmd, Args),
    uart:close(U).

fail(Msg) ->
    io:format(standard_error, "~s\n", [Msg]),
    halt(1).

cmd(_U, "sync", []) -> io:format("ok\n");
cmd(U, "status", []) -> show(status(U, ?STATUS));
cmd(U, "capture", []) -> show(status(U, ?CAPTURE));
cmd(U, "get", [Ix, Sub]) ->
    case get(U, index_of(Ix), num(Sub)) of
        undefined -> io:format("no such entry\n");
        V -> io:format("~w (0x~.16B)\n", [V, V])
    end;
cmd(U, "set", [Ix, Sub, Val]) ->
    V = num(Val),
    case set(U, index_of(Ix), num(Sub), V band 16#FFFFFFFF) of
        undefined -> io:format("no such entry\n");
        Old -> io:format("was ~w, now ~w\n", [Old, V])
    end;
cmd(U, "dict", []) ->
    [io:format("~-34s ~4.16.0B  ~s\n",
               [Name, Ix, lists:join(" ", [case get(U, Ix, S) of
                                               undefined -> "-";
                                               V -> integer_to_list(V)
                                           end || S <- subindexes(Name)])])
     || {Name, Ix} <- lists:sort(index())],
    ok;
cmd(U, "watch", []) -> cmd(U, "watch", ["1000"]);
cmd(U, "watch", [Ms]) -> watch(U, num(Ms));
cmd(_, _, _) -> usage().

watch(U, Ms) ->
    {_, DIntf, _, AIntf, _} = Rec = status(U, ?STATUS),
    case (DIntf bor AIntf) =/= 0 of
        true ->
            {_, {H, M, S}} = calendar:local_time(),
            io:format("~2..0w:~2..0w:~2..0w ", [H, M, S]),
            show(Rec);
        false -> ok
    end,
    timer:sleep(Ms),
    watch(U, Ms).

%% ------------------------------------------------------------------
%% The protocol
%% ------------------------------------------------------------------

transfer(U, B) ->
    ok = uart:send(U, <<(B band 16#FF)>>),
    case uart:recv(U, 1, 1000) of
        {ok, <<R>>} -> R;
        _ -> fail("no answer from CoCo")
    end.

sync(U) -> sync(U, 10).

sync(_U, 0) -> false;
sync(U, N) ->
    case transfer(U, ?SYN) of
        ?ACK -> true;
        _ -> sync(U, N - 1)
    end.

%% d_intf d_value a_intf a1..a4 low bytes, then the top two bits of each,
%% a1's highest.
status(U, Cmd) ->
    transfer(U, Cmd),
    R = transfer(U, 0),
    DIntf = transfer(U, 0),
    DValue = transfer(U, 0),
    AIntf = transfer(U, 0),
    Lo = [transfer(U, 0) || _ <- [1, 2, 3, 4]],
    Hi = transfer(U, ?NONE),
    A = [L bor (((Hi bsr (6 - 2 * I)) band 3) bsl 8)
         || {I, L} <- lists:zip([0, 1, 2, 3], Lo)],
    {R =:= 16#80 bor Cmd, DIntf, DValue, AIntf, A}.

get(U, Ix, Sub) -> access(U, ?GET, Ix, Sub, 0).

%% The OLD value comes back.
set(U, Ix, Sub, V) -> access(U, ?SET, Ix, Sub, V).

access(U, Cmd, Ix, Sub, V) ->
    transfer(U, Cmd),
    R = transfer(U, Ix bsr 8),
    transfer(U, Ix band 16#FF),
    Si = transfer(U, Sub),
    transfer(U, V bsr 24),
    B2 = transfer(U, V bsr 16),
    B1 = transfer(U, V bsr 8),
    B0 = transfer(U, V),
    B = transfer(U, ?NONE),
    case (R =:= 16#80 bor Cmd) andalso (Si =/= 0) of
        true -> (B2 bsl 24) bor (B1 bsl 16) bor (B0 bsl 8) bor B;
        false -> undefined
    end.

%% ------------------------------------------------------------------

show({false, _, _, _, _}) ->
    io:format("status: bad reply\n");
show({true, DIntf, DValue, AIntf, A}) ->
    io:format("D:~2.16.0B A:~2.16.0B  din=~s  ain=~s\n",
              [DIntf, AIntf,
               [$0 + ((DValue bsr I) band 1) || I <- [0, 1, 2, 3]],
               lists:join(" ", [io_lib:format("~4w", [X]) || X <- A])]),
    [io:format("  D[~w]=~w\n", [I + 1, (DValue bsr I) band 1])
     || I <- [0, 1, 2, 3], (DIntf bsr I) band 1 =:= 1],
    [io:format("  A[~w]=~w\n", [I + 1, lists:nth(I + 1, A)])
     || I <- [0, 1, 2, 3], (AIntf bsr I) band 1 =:= 1],
    ok.

index_of(S) ->
    try num(S)
    catch error:badarg ->
            K = case string:uppercase(S) of
                    "INDEX_" ++ T -> T;
                    T -> T
                end,
            case lists:keyfind(K, 1, index()) of
                {_, Ix} -> Ix;
                false -> fail("unknown index " ++ S)
            end
    end.

num("0x" ++ H) -> list_to_integer(H, 16);
num("0X" ++ H) -> list_to_integer(H, 16);
num(D) -> list_to_integer(D).
