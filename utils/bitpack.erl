%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Generate bit access patterns 
%%% @end
%%% Created :  6 Aug 2026 by Tony Rogvall <tony@rogvall.se>

-module(bitpack).

-export([unpack_bits/3]).


uunpack(I, N, W) ->
    P1    = (I+N) rem W,
    RShift = (W-P1) rem W,
    Mask  = (1 bsl N)-1,
    LShift=0,
    {LShift,RShift,Mask}.

iunpack(I, N, W) ->
    P1    = (I+N) rem W,
    RShift = (W-P1) rem W,
    Mask  = (1 bsl N)-1,
    LShift = P1,
    {LShift,RShift, Mask}.


unpack_bits(uint32, I, N) when ((I div 32) =:= ((I+N-1) div 32)) ->
    uunpack(I, N, 32);
unpack_bits(uint16, I, N) when ((I div 16) =:= ((I+N-1) div 16)) ->
    uunpack(I, N, 16);
unpack_bits(uint8, I, N) when ((I div 8) =:= ((I+N-1) div 8)) ->
    uunpack(I, N, 8);
unpack_bits(int32, I, N) when ((I div 32) =:= ((I+N-1) div 32)) ->
    iunpack(I, N, 32);
unpack_bits(int16, I, N) when ((I div 16) =:= ((I+N-1) div 16)) ->
    iunpack(I, N, 16);
unpack_bits(int8, I, N) when ((I div 8) =:= ((I+N-1) div 8)) ->
    iunpack(I, N, 8).





    


    
    
     
    
    
    
    
