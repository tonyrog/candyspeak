%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2024, Tony Rogvall
%%% @doc
%%%    Output module for lpc_clock formula
%%% @end
%%% Created : 15 May 2024 by Tony Rogvall <tony@rogvall.se>

-module(lpc_clock_io).

-export([output/3]).

output(Fd, _Partial, Model) ->
    %% io:format("Model = ~p\n", [Model]),
    format_int(Fd, <<"Osc_clk">>, Model),
    format_int(Fd, <<"Rtc_clk">>, Model),
    format_int(Fd, <<"Irc_osc">>, Model),
    format_int(Fd, <<"CCLK">>, Model),
    format_enum(Fd, <<"CLKSRC">>, Model, {irc, osc, rtc}),

    format_hex32(Fd, <<"CLKSRCSEL_Val">>, Model),
    format_hex32(Fd, <<"CCLKCFG_Val">>, Model),
    format_hex32(Fd, <<"PLL0CFG_Val">>, Model),
    format_hex32(Fd, <<"PLL1CFG_Val">>, Model),
    format_hex32(Fd, <<"PCLKSEL0_Val">>, Model),
    format_hex32(Fd, <<"PCLKSEL1_Val">>, Model),
    format_hex32(Fd, <<"PCONP_Val">>, Model),
    io:format(Fd, "// Rates\n", []),
    format_int(Fd, <<"UART0_baudrate">>, Model),
    format_int(Fd, <<"UART0_actual_baudrate">>, Model),
    format_int(Fd, <<"SPI_bitrate">>, Model),
    format_int(Fd, <<"SPI_actual_bitrate">>, Model),
    ok.

format_hex32(Fd, Var, Model) ->
    {uint,Bits} = proplists:get_value({p,Var,[]}, Model),
    Val = list_to_integer(tuple_to_list(Bits), 2),
    io:format(Fd, "~s = 0x~8.16.0B\n", [Var, Val]).

format_int(Fd, Var, Model) ->
    {uint,Bits} = proplists:get_value({p,Var,[]}, Model),
    Val = list_to_integer(tuple_to_list(Bits), 2),
    io:format(Fd, "~s = ~w\n", [Var, Val]).

format_enum(Fd, Var, Model, Enum) ->
    {uint,Bits} = proplists:get_value({p,Var,[]}, Model),
    Val = list_to_integer(tuple_to_list(Bits), 2),
    io:format(Fd, "~s = ~s\n", [Var, element(Val+1,Enum)]).
