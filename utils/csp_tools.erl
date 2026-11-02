%%% Where the csp_tools application's own files are: the host's csp, the
%%% library #import reads (lib/), the examples, the manual.
%%%
%%% priv/ mirrors the tree with symlinks -- priv/csp, priv/lib, priv/examples,
%%% priv/doc -- so in a checkout they ARE the tree, and in a packaged system
%%% (servator copies an application's priv/, following the links) they are
%%% the copies that came along. Everything that needs one of them joins its
%%% name onto dir/0.

-module(csp_tools).

-export([dir/0]).

%% priv/ beside this application's ebin/. Found from where the beam is, not
%% code:priv_dir: a checkout's directory is named candyspeak, not csp_tools,
%% and code:lib_dir finds an application by its directory's name.
dir() ->
    filename:join(filename:dirname(filename:dirname(
				     filename:absname(code:which(?MODULE)))),
		  "priv").
