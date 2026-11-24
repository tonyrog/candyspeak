%%% What is plugged in: the serial devices, the USB device behind each and
%%% the boards of this tree it could be, by the USB ids their .terms list
%%% ({usb, [...]}, and {usb_boot, [...]} for a loader). ./cs lists them, and
%%% candid offers them when it connects.

-module(csp_ports).

-export([ports/0, ports/1, boards/1, board_of_usb/2, root/0]).

%% [{Device, UsbId, Product, Boards}], the boards of this tree.
ports() -> ports(root()).

ports(Root) ->
    Bs = boards(Root),
    [{D, Id, Prod, board_of_usb(Id, Bs)} || {D, Id, Prod} <- devices()].

%% This tree: where the ebin these modules were loaded from sits.
root() ->
    filename:dirname(filename:dirname(code:which(?MODULE))).

%% [{Board, Props}] of boards/*/*.terms under Root.
boards(Root) ->
    lists:sort([{B, P} || F <- filelib:wildcard(filename:join(Root, "boards/*/*.terms")),
                          {ok, Ts} <- [file:consult(F)],
                          {board, B, P} <- Ts]).

%% The boards a USB id could be, a loader's said so.
board_of_usb(Id, Boards) ->
    [atom_to_list(B) ++ [" (loader)" || lists:member(Id, proplists:get_value(usb_boot, P, []))]
     || {B, P} <- Boards,
        lists:member(Id, proplists:get_value(usb, P, []) ++
                         proplists:get_value(usb_boot, P, []))].

devices() ->
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
