%% A wse that records instead of rendering.
%%
%% The panel talks to the browser only through this module's API, so replacing
%% it is enough to run the whole loop -- parse, widget derivation, the csp port,
%% clicks, lamps and the trace -- with no browser and no display. It shadows the
%% real wse by coming first on the code path.

-module(wse).
-compile([export_all, nowarn_export_all]).

-define(LOG(F, A), (catch (whereis(wse_log) ! {log, io_lib:format(F, A)}))).

id(X)                    -> {id, X}.
createElement(_, Tag)    -> ?LOG("createElement ~s", [Tag]),
			    {node, Tag, erlang:unique_integer([positive])}.
createTextNode(_, T)     -> {text, T}.
appendChild(_, _, _)     -> ok.
setStyle(_, _, V)        -> ?LOG("setStyle ~s", [V]), ok.
set(_, _, A, V)          -> ?LOG("set ~p = ~p", [A, V]), ok.
%% The event ids are what a test needs to fire a click at a specific widget,
%% and they are handed out in creation order: the chooser first, then one per
%% switch. Logged as "event N" so the test can pick them up.
create_event(_)          -> Id = erlang:unique_integer([positive]),
			    ?LOG("event ~w", [Id]), {ok, Id}.
newf(_, _, Body)         -> ?LOG("newf ~s", [Body]), {func, Body}.
call(_, _, "getContext", _) -> {ok, {ctx}};
call(_, _, M, A)         -> ?LOG("call ~s ~p", [M, A]), {ok, ok}.
cast(_, _, M, A)         -> ?LOG("cast ~s ~p", [M, A]), ok.
