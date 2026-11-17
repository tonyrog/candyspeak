%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    image utils
%%% @end
%%% Created : 17 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_img).

-compile(export_all).

from_csp(Filename) ->
    from_csp(Filename, []).
from_csp(Filename, Options) ->
    {ok,As} = csp_cc:file(Filename),
    As1 = set_header(As, Options),
    {ok,Bin} = csp_asm:asm(As1),
    Bin.

from_img(Filename) ->
    from_img(Filename, []).
from_img(Filename, Options) ->
    {ok,{H,S,Decls,Code}} = csp_disasm:file(Filename),
    H1 = set_options(H, Options),
    [{header,H1},{strings,S}|Decls++Code].


set_header([{header, H}| As], Options) ->
    [{header, set_options(H, Options)} | As];
set_header([A|As], Options) ->
    [A|set_header(As, Options)];
set_header([], _Options) ->
    [].

set_options(H, [{Key, Value}|Options]) ->
    set_options(H#{ Key := Value}, Options);
set_options(H, []) ->
    H.

to_asm(FileName) ->
    {ok,Bin} = file:read_file(FileName),
    {ok,As}  = csp_disasm:disasm(Bin),
    As.
