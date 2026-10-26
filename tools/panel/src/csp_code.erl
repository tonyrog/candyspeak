%%% The code view: the program's rules, lit by what holds.
%%%
%%% Two renderings of the same tree. CODE is candyspeak as written (re-printed
%%% from the parse tree, so comments and layout are gone). LADDER draws each
%%% guard the way Sternol did: an AND runs HORIZONTALLY, a chain of contacts
%%% the current passes through one after the other, and an OR stacks its
%%% branches VERTICALLY between two rails. The assignments are the coil at
%%% the end of the rung.
%%%
%%% A contact -- any sub-condition that is not && || or a ! over one -- is lit
%%% when it holds in the CURRENT state, so setting an input lights what it
%%% enables at once, before the step that acts on it. candyspeak_eval works it
%%% out from the dump; what the dump cannot say (a timer, a buffer byte) is
%%% drawn dashed, never guessed.
%%%
%%% The coil is lit when csp says the rule FIRED in the last cycle (--fired),
%%% which is the runtime's word, not ours. Outlined when its guard holds now:
%%% it will fire on the next step unless its block is not the active one.
%%%
%%% An unguarded `X = <condition>' is a rung of its own -- the classic ladder
%%% line, contacts driving an output -- so the condition is drawn as the
%%% contacts and X alone as the coil, lit when X is on. It fires every cycle,
%%% so lighting it for that would say nothing.
%%%
%%% Rules are numbered as csp numbers them -- the Nth rule in the source, the
%%% number #disable and the listing use -- and a #timer whose period is not a
%%% constant compiles to a rule of its own, `T.period = P', at its
%%% declaration.
%%%
%%% TABS. `main' is the program outside its #module definitions; every object
%%% gets a tab of its own with its module's rules, evaluated over THAT
%%% object's values (`a0.A' read as `A'), and its bindings above them. Each
%%% tab starts with the values the panel has no widget for: the variables and
%%% the #locals, worked out.
%%%
%%% csp says which rules fired as {Object, File, Line} (--fired): a module's
%%% rule is one piece of code run once per instance, so the object says whose
%%% tab lights it, and file and line say which rung. A rule that fires for one
%%% cycle -- a timeout -- would light for one tick and be missed, so a coil
%%% stays lit for ?GLOW ms after it last fired; tick/2 lets it go out.

-module(csp_code).

-export([new/3, refresh/3, fired/3, tick/2, control/3]).
-export([split/1]).
-export([rules/1, tabs/1, tabs/2]).                  % tests

-define(GLOW, 600).                     % ms a coil stays lit after firing

-define(CSS,
	".cv{font:13px monospace;color:#bbb}"
	".cv .bar button{font:12px monospace;padding:2px 8px;margin:0 4px 8px 0;"
	"  border:1px solid #555;background:#333;color:#ccc}"
	".cv .bar button.sel{background:#3af;color:#111;font-weight:bold}"
	".cv .vals{display:grid;grid-template-columns:repeat(auto-fill,"
	"  minmax(150px,1fr));gap:2px 14px;margin-bottom:10px;color:#888}"
	".cv .vals b{color:#ddd;font-weight:normal;float:right}"
	".cv .vals i{color:#8ab}"
	".cv .bind{color:#8ab;margin-bottom:8px}"
	".cv .rw{display:flex;align-items:center;margin:3px 0;flex-wrap:nowrap}"
	".cv .ln{color:#555;width:3.2em;text-align:right;margin-right:10px;"
	"  flex:none}"
	".cv .and{display:flex;align-items:center}"
	".cv .or{display:flex;flex-direction:column;border-left:2px solid #666;"
	"  border-right:2px solid #666;padding:1px 0}"
	".cv .br{display:flex;align-items:center;justify-content:space-between}"
	".cv .br>.w:last-child{flex:1}"
	".cv .w{display:inline-block;height:2px;min-width:10px;background:#666}"
	".cv .ct{border:1px solid #555;padding:1px 6px;border-radius:3px;"
	"  background:#1e1e1e;color:#999;white-space:nowrap}"
	".cv .ct.t{background:#163d16;border-color:#3f3;color:#dfd;"
	"  box-shadow:0 0 6px #3f3}"
	".cv .ct.u{border-style:dashed;color:#666}"
	".cv .ct.k{cursor:pointer}"
	".cv .co{border:1px solid #555;border-radius:10px;padding:1px 9px;"
	"  color:#999;white-space:nowrap}"
	".cv .co.nx{border-color:#fb3;color:#fd8}"
	".cv .co.on{background:#4a3708;border-color:#fb3;color:#fe9;"
	"  box-shadow:0 0 7px #fb3}"
	".cv .hd{color:#68a;margin:10px 0 2px 0}"
	".cv .rw.off{opacity:0.35}"
	".cv .hd.e{color:#456;margin:2px 0 8px 0}"
	".cv .hd.t{color:#bdf;font-weight:bold}"
	".cv.code .ct{border:none;padding:0 2px;background:none;box-shadow:none}"
	".cv.code .ct.t{color:#5f5;text-shadow:0 0 5px #3f3}"
	".cv.code .ct.u{color:#666;text-decoration:underline dotted}"
	".cv.code .co{border:none;padding:0 2px}"
	".cv.code .co.on{background:#4a3708}"
	".cv.code .op{color:#777}"
	".cv .tx{white-space:pre}").

%%% ------------------------------------------------------------- the rules

%% [{gate, Kind, Arg} | #{no, line, file, guard, assigns, gates}] in source
%% order. `gates' is every block the rule sits in; a rule in none runs in INIT
%% and NORMAL only, as csp has it. `file' is the basename of the file the rule
%% is written in: candyspeak:parse/2 with `sources' marks where an imported
%% file starts and ends, and Top is the file the walk starts in.
rules(Ast) -> rules(Ast, "").

rules(Ast, Top) ->
    number(walk(Ast, #{cs => consts(Ast), gs => [],
		       file => filename:basename(Top)}), 1).

%% One tab per object, after `main'. #{name, rules, env, prefix, vars, binds}.
tabs(Ast) -> tabs(Ast, "").

tabs(Ast, Top) ->
    {Main, Mods} = split(Ast),
    GEnv = candyspeak_eval:env(Main),
    [#{name => "main", rules => rules(Main, Top), env => GEnv, prefix => none,
       vars => vars(Main), binds => none}] ++
	[#{name => O, rules => rules(Body, Top), prefix => O, vars => vars(Body),
	   env => merge_env(GEnv, candyspeak_eval:env(Body)),
	   binds => {M, Bs}}
	 || {object, _, {'WORD', _, M}, {'WORD', _, O}, Bs} <- Main,
	    {ok, Body} <- [maps:find(M, Mods)]].

%% The program outside its #module definitions, and each definition's body.
%% A module ends at the #end that closes it, not at one closing an #in in it.
%% A body starts with the {source, 0, File} it was written in, when it was
%% not the top file, so the rules in it know where they are.
split(Ast) -> split(Ast, [], #{}, undefined).

split([{module, _, {'WORD', _, M}} | T], Acc, Mods, F) ->
    {Body, Rest} = body(T, 0, []),
    split(Rest, Acc, Mods#{M => src(F) ++ Body}, F);
split([{module, _, {'WORD', _, M}, _} | T], Acc, Mods, F) ->
    {Body, Rest} = body(T, 0, []),
    split(Rest, Acc, Mods#{M => src(F) ++ Body}, F);
split([X = {source, _, F} | T], Acc, Mods, _F) -> split(T, [X | Acc], Mods, F);
split([X | T], Acc, Mods, F) -> split(T, [X | Acc], Mods, F);
split([], Acc, Mods, _F) -> {lists:reverse(Acc), Mods}.

src(undefined) -> [];
src(F)         -> [{source, 0, F}].

body([{'end', _} | T], 0, Acc) -> {lists:reverse(Acc), T};
body([X = {'end', _} | T], D, Acc) -> body(T, D - 1, [X | Acc]);
body([X = {in, _, _} | T], D, Acc) -> body(T, D + 1, [X | Acc]);
body([X = {'when', _, _} | T], D, Acc) -> body(T, D + 1, [X | Acc]);
body([X | T], D, Acc) -> body(T, D, [X | Acc]);
body([], _D, Acc) -> {lists:reverse(Acc), []}.

merge_env(G, M) ->
    maps:map(fun(states, V) -> maps:get(states, G) ++ V;
		(K, V)      -> maps:merge(maps:get(K, G), V)
	     end, M).

%% The values a tab lists: variables, and #locals marked as worked out.
vars(Ast) ->
    [{var, N} || {variable, _, {'WORD', _, N}, scalar, _, _, _} <- Ast] ++
	[{local, N} || {local, _, {'WORD', _, N}, _, _, X} <- Ast,
		       X =/= undefined].

walk([{source, _, F} | T], C) ->
    walk(T, C#{file := filename:basename(F)});
walk([{in, _, Names} | T], C = #{gs := Gs}) ->
    G = {in, [N || {'WORD', _, N} <- Names]},
    [{gate, in, element(2, G)} | walk(T, C#{gs := [G | Gs]})];
walk([{'when', _, X} | T], C = #{gs := Gs}) ->
    [{gate, 'when', X} | walk(T, C#{gs := [{'when', X} | Gs]})];
walk([{'end', _} | T], C = #{gs := Gs}) ->
    [{gate, 'end', none} | walk(T, C#{gs := tl_or_nil(Gs)})];
walk([{rule, _, _, _} = R | T], C = #{gs := Gs, file := F}) ->
    [M#{file => F,
	gates => case Gs of
		     [] -> [{in, ["INIT", "NORMAL"]}];
		     _  -> Gs
		 end} || M <- rule(R)] ++ walk(T, C);
walk([{timer, Ln, {'WORD', _, N}, P, _} | T], C = #{cs := Cs, file := F}) ->
    case period_rule(P, Cs) of
	true ->
	    Tgt = {field, Ln, {part, Ln, {'WORD', Ln, N}, {'WORD', Ln, "period"}}},
	    [#{line => Ln, file => F, guard => undefined, gates => [],
	       assigns => [{'=', Ln, Tgt, P}]} | walk(T, C)];
	false -> walk(T, C)
    end;
walk([_ | T], C) -> walk(T, C);
walk([], _C) -> [].

tl_or_nil([_ | T]) -> T;
tl_or_nil([])      -> [].

rule({rule, Ln, As = [{'=', _, Tgt, X}], undefined}) ->
    case is_cond(X) of
	true  -> [#{line => Ln, guard => X, assigns => As, coil => Tgt}];
	false -> [#{line => Ln, guard => undefined, assigns => As}]
    end;
rule({rule, Ln, As, G}) ->
    [#{line => Ln, guard => guard(G), assigns => As}].


is_cond({Op, _, _, _}) ->
    lists:member(Op, ['&&', '||', '==', '!=', '<', '<=', '>', '>=']);
is_cond({'!', _, _}) -> true;
is_cond(_) -> false.

%% `true' is what the parser gives a pack/unpack rule; it is no guard.
guard(true) -> undefined;
guard(G)    -> G.

%% A literal or a #constant/#define folds; anything else is computed at run
%% time, by a rule.
period_rule({'INT', _, _}, _Cs) -> false;
period_rule({field, _, {'WORD', _, N}}, Cs) -> not lists:member(N, Cs);
period_rule(_, _Cs) -> true.

consts(Ast) ->
    [N || {constant, _, {'WORD', _, N}, _, _, _, _} <- Ast] ++
	[N || {define, _, {'WORD', _, N}, _} <- Ast].

number([R = #{} | T], N) -> [R#{no => N} | number(T, N + 1)];
number([G | T], N)       -> [G | number(T, N)];
number([], _N)           -> [].

%%% ----------------------------------------------------------------- view

%% Hidden until control/3 says ladder or code. Ast is candyspeak:parse/2 of
%% File with `sources'.
new(Ws, Parent, {Ast, File}) ->
    Style = wse:createElement(Ws, "style"),
    wse:set(Ws, Style, "textContent", ?CSS),
    wse:appendChild(Ws, Parent, Style),
    {ok, Ev} = wse:create_event(Ws),
    Box = wse:createElement(Ws, "div"),
    wse:setStyle(Ws, Box, "display:none"),
    Tabs = tabs(Ast, File),
    Bar = wse:createElement(Ws, "div"),
    wse:set(Ws, Bar, "className", "bar"),
    Btns = case Tabs of
	       [_] -> [];
	       _ ->
		   [begin
			B = wse:createElement(Ws, "button"),
			wse:appendChild(Ws, B, wse:createTextNode(Ws, Name)),
			F = wse:newf(Ws, "", "{ Wse.notify(" ++
					 integer_to_list(Ev) ++ ", 'tab=" ++
					 integer_to_list(I) ++ "'); }"),
			wse:set(Ws, B, "onclick", F),
			wse:appendChild(Ws, Bar, B),
			B
		    end || {I, #{name := Name}} <- enum(Tabs)]
	   end,
    Body = wse:createElement(Ws, "div"),
    wse:appendChild(Ws, Box, Bar),
    wse:appendChild(Ws, Box, Body),
    wse:appendChild(Ws, Parent, Box),
    {ok, Click} = wse:create_event(Ws),
    Paint = wse:newf(Ws, "a,b",
		     "{ for (var i = 0; i < a.length; i += 2) {"
		     "    var e = document.getElementById(a[i]);"
		     "    if (e) e.className = a[i+1]; }"
		     "  for (var i = 0; i < b.length; i += 2) {"
		     "    var e = document.getElementById(b[i]);"
		     "    if (e) e.textContent = b[i+1]; } }"),
    #{box => Box, body => Body, tab_buttons => Btns, event => Ev,
      click => Click, paint => Paint, tabs => Tabs, tab => 0,
      mode => hide, shown => #{}, fired => [], glow => #{}, last => #{}}.

enum(L) -> lists:zip(lists:seq(0, length(L) - 1), L).

%% hide | ladder | code from the panel's bar, tab=N from the view's own.
control(_Ws, undefined, _Tag) -> undefined;
control(Ws, V = #{box := Box}, "hide") ->
    wse:setStyle(Ws, Box, "display:none"),
    V#{mode := hide};
control(Ws, V = #{box := Box}, Tag) when Tag =:= "ladder"; Tag =:= "code" ->
    wse:setStyle(Ws, Box, "display:block"),
    render(Ws, V#{mode := list_to_atom(Tag)});
control(Ws, V = #{mode := Mode}, "tab=" ++ I) ->
    V1 = V#{tab := list_to_integer(I)},
    case Mode of
	hide -> V1;
	_    -> render(Ws, V1)
    end;
control(_Ws, V, _Tag) -> V.

refresh(_Ws, undefined, _Last) -> undefined;
refresh(Ws, V, Last)           -> paint(Ws, V#{last := Last}).

fired(_Ws, undefined, _Rules) -> undefined;
fired(Ws, V = #{glow := G}, Rules) ->
    Now = erlang:monotonic_time(millisecond),
    G1 = maps:filter(fun(_, T) -> Now - T < ?GLOW end, G),
    paint(Ws, V#{fired := Rules,
		 glow := maps:merge(G1, maps:from_list([{R, Now} || R <- Rules]))}).

%% On the panel's tick: let what has stopped firing go out.
tick(_Ws, undefined) -> undefined;
tick(Ws, V)          -> paint(Ws, V).

render(Ws, V = #{box := Box, body := Body, mode := Mode, tab := I,
		 tab_buttons := Btns}) ->
    Tab = lists:nth(I + 1, maps:get(tabs, V)),
    wse:set(Ws, Box, "className", case Mode of
				      ladder -> "cv";
				      code   -> "cv code"
				  end),
    [wse:set(Ws, B, "className", case J of I -> "sel"; _ -> "" end)
     || {J, B} <- enum(Btns)],
    Html = lists:flatten([head(Tab), html(maps:get(rules, Tab), Mode,
					   V#{env => maps:get(env, Tab),
					      prefix => maps:get(prefix, Tab)})]),
    wse:set(Ws, Body, "innerHTML", Html),
    paint(Ws, V#{shown := #{}}).

%% The bindings of an object, and the values with no widget of their own.
head(Tab = #{binds := Binds, vars := Vars}) ->
    [case Binds of
	 none -> "";
	 {M, Bs} -> ["<div class='bind'>#", M, " ", maps:get(name, Tab), " ",
		     esc(lists:join("  ", [bind(B) || B <- Bs])), "</div>"]
     end,
     case Vars of
	 [] -> "";
	 _ -> ["<div class='vals'>",
	       [["<span>", case K of local -> ["<i>", N, "</i>"]; var -> N end,
		 "<b id='v_", N, "'></b></span>"] || {K, N} <- Vars],
	       "</div>"]
     end].

%%% ----------------------------------------------------------------- html
%%%
%%% Ids: a rule's coil is k<N>, its contacts k<N>_<I> in walk order, a gate
%%% header g<I>. paint/2 sets classes by id, so the html is written once.

html(Rules, Mode, V) ->
    {Out, _} = lists:mapfoldl(fun(R, G) -> row(R, G, Mode, V) end, 0, Rules),
    Out.

row({gate, 'end', _}, G, _Mode, _V) ->
    {"<div class='hd e'>#end</div>", G};
row({gate, Kind, Arg}, G, _Mode, _V) ->
    T = case Kind of
	    in      -> ["#in ", lists:join(" ", Arg)];
	    'when'  -> ["#when ", candyspeak_eval:text(Arg)]
	end,
    {["<div class='hd' id='g", integer_to_list(G), "'>", esc(T), "</div>"],
     G + 1};
row(R = #{no := N, line := Ln, guard := Gd, assigns := As}, G, Mode, V) ->
    K = "k" ++ integer_to_list(N),
    Label = case {Mode, R} of
		{ladder, #{coil := Tgt}} -> candyspeak_eval:text(Tgt);
		{code, #{coil := Tgt}}   -> [candyspeak_eval:text(Tgt), " ="];
		_ -> lists:join(", ", [assign(A) || A <- As])
	    end,
    Coil = ["<span class='co' id='", K, "'>", esc(Label), "</span>"],
    Ln1 = ["<span class='ln'>", integer_to_list(Ln), "</span>"],
    Body = case {Mode, Gd} of
	       {ladder, undefined} ->
		   [Ln1, "<span class='w' style='width:40px'></span>", Coil];
	       {ladder, _} ->
		   {L, _} = ladder(Gd, K, 0, V),
		   [Ln1, "<span class='w'></span>", L,
		    "<span class='w'></span>", Coil];
	       {code, undefined} ->
		   [Ln1, "<span class='tx'>", Coil, "</span>"];
	       {code, _} ->
		   {T, _} = code(Gd, K, 0, 0, V),
		   Sep = case R of
			     #{coil := _} -> "&nbsp;";
			     _ -> "<span class='op'>&nbsp;?&nbsp;</span>"
			 end,
		   %% one inline run: as flex items the operators would
		   %% lose the spaces around them
		   [Ln1, "<span class='tx'>", Coil, Sep, T, "</span>"]
	   end,
    {["<div class='rw' id='r", integer_to_list(N), "'>", Body, "</div>"], G}.

assign({'=', _, T, X}) -> [candyspeak_eval:text(T), " = ", candyspeak_eval:text(X)];
assign({'<-', _, T, X}) -> [candyspeak_eval:text(T), " <- ", candyspeak_eval:text(X)];
assign({pack, T, _})   -> [candyspeak_eval:text(T), " <<= ..."];
assign({unpack, T, _}) -> [candyspeak_eval:text(T), " >>= ..."];
assign(X)              -> candyspeak_eval:text(X).   % println("ON")

%% As an object line writes it: `Cin=a0.Cout'.
bind({'=', _, T, X}) -> [candyspeak_eval:text(T), "=", candyspeak_eval:text(X)];
bind(B)              -> assign(B).

%% AND horizontal, OR vertical. Returns {Html, NextContact}.
ladder(X = {'&&', _, _, _}, K, I, V) ->
    {Hs, I1} = lists:mapfoldl(fun(C, J) -> ladder(C, K, J, V) end, I,
			      chain('&&', X)),
    {["<span class='and'>", lists:join("<span class='w'></span>", Hs),
      "</span>"], I1};
ladder(X = {'||', _, _, _}, K, I, V) ->
    {Hs, I1} = lists:mapfoldl(fun(C, J) -> ladder(C, K, J, V) end, I,
			      chain('||', X)),
    {["<span class='or'>",
      [["<span class='br'>", H, "<span class='w'></span></span>"] || H <- Hs],
      "</span>"], I1};
ladder(X, K, I, V) ->
    {contact(X, K, I, V), I + 1}.

%% The same walk as text: the contacts in order, the operators between.
code(X = {Op, _, _, _}, K, I, P, V) when Op =:= '&&'; Op =:= '||' ->
    Q = case Op of '||' -> 1; '&&' -> 2 end,
    {Ts, I1} = lists:mapfoldl(fun(C, J) -> code(C, K, J, Q + 1, V) end, I,
			      chain(Op, X)),
    T = lists:join(["<span class='op'> ", atom_to_list(Op), " </span>"], Ts),
    {case Q < P of
	 true  -> ["<span class='op'>(</span>", T, "<span class='op'>)</span>"];
	 false -> T
     end, I1};
code(X, K, I, _P, V) ->
    {contact(X, K, I, V), I + 1}.

chain(Op, {Op, _, L, R}) -> chain(Op, L) ++ chain(Op, R);
chain(_Op, X)            -> [X].

%% A contact. One naming a one-bit signal is a switch: click it to flip the
%% signal, the same `> X = ...' a panel switch sends.
contact(X, K, I, #{click := Click, env := Env, prefix := P}) ->
    Id = [K, "_", integer_to_list(I)],
    case switchable(X, Env) of
	{ok, Name} ->
	    Full = case P of none -> Name; _ -> P ++ "." ++ Name end,
	    ["<span class='ct' id='", Id, "' onclick=\"Wse.notify(",
	     integer_to_list(Click), ",'", Full, "')\">",
	     esc(candyspeak_eval:text(X)), "</span>"];
	none ->
	    ["<span class='ct' id='", Id, "'>",
	     esc(candyspeak_eval:text(X)), "</span>"]
    end.

switchable({'!', _, X}, Env) -> switchable(X, Env);
switchable({field, _, {'WORD', _, N}}, #{sigs := Sigs}) ->
    case maps:find(N, Sigs) of
	{ok, {1, _}} -> {ok, N};
	_            -> none
    end;
switchable(_, _Env) -> none.

esc(T) ->
    lists:flatmap(fun($<) -> "&lt;";
		     ($>) -> "&gt;";
		     ($&) -> "&amp;";
		     (C)  -> [C]
		  end, lists:flatten(T)).

%%% ---------------------------------------------------------------- paint

%% Every id's class from the current state; only what changed is sent, in one
%% cast -- a setStyle per contact would be a round trip each.
paint(_Ws, V = #{mode := hide}) ->
    V;
paint(Ws, V = #{tabs := Tabs, tab := I, last := Last0, fired := Now0,
		glow := Glow, shown := Shown, paint := F}) ->
    Ms = erlang:monotonic_time(millisecond),
    Fired0 = Now0 ++ [R || {R, T} <- maps:to_list(Glow), Ms - T < ?GLOW],
    #{rules := Rules, env := Env, prefix := P, vars := Vars} =
	lists:nth(I + 1, Tabs),
    Last = values(Last0, P),
    Obj = case P of none -> ""; _ -> P end,
    Fired = [{filename:basename(Fl), L} || {O, Fl, L} <- Fired0, O =:= Obj],
    State = maps:get("State", Last, 0),
    {Cls, _} = lists:mapfoldl(
		 fun(R, G) -> classes(R, G, Env, Last, Fired, State) end,
		 0, Rules),
    Txt = [{"v_" ++ N, show(candyspeak_eval:eval({field, 0, {'WORD', 0, N}},
						 Last, Env))}
	   || {_, N} <- Vars],
    New = maps:from_list(lists:append(Cls) ++ [{{t, Id}, T} || {Id, T} <- Txt]),
    Changed = [{Id, C} || {Id, C} <- maps:to_list(New),
			  maps:get(Id, Shown, undefined) =/= C],
    Cs = lists:append([[Id, C] || {Id, C} <- Changed, is_list(Id)]),
    Ts = lists:append([[Id, T] || {{t, Id}, T} <- Changed]),
    case Cs ++ Ts of
	[] -> ok;
	_  -> wse:cast(Ws, F, "call", [null, wse:array(Cs), wse:array(Ts)])
    end,
    V#{shown := New}.

%% An object's tab reads `a0.A' as `A'; its own names win over the globals.
values(Last, none) -> Last;
values(Last, P) ->
    Pre = P ++ ".",
    maps:merge(Last, maps:from_list([{lists:nthtail(length(Pre), K), V}
				     || {K, V} <- maps:to_list(Last),
					lists:prefix(Pre, K)])).

show(unknown) -> "?";
show(V)       -> integer_to_list(V).

classes({gate, 'end', _}, G, _Env, _Last, _Fired, _State) ->
    {[], G};
classes({gate, Kind, Arg}, G, Env, Last, _Fired, State) ->
    On = gate_on({Kind, Arg}, Env, Last, State),
    {[{"g" ++ integer_to_list(G), hd_class(On)}], G + 1};
classes(R = #{no := N, guard := Gd, gates := Gs, line := Ln, file := F},
	G, Env, Last, Fired, State) ->
    K = "k" ++ integer_to_list(N),
    Active = lists:all(fun(Gt) -> gate_on(Gt, Env, Last, State) end, Gs),
    Holds = Active andalso
	((Gd =:= undefined) orelse
	 (candyspeak_eval:truth(Gd, Last, Env) =:= true)),
    On = case R of
	     #{coil := Tgt} -> candyspeak_eval:truth(Tgt, Last, Env) =:= true;
	     _              -> lists:member({F, Ln}, Fired)
	 end,
    Coil = string:join(["co"] ++ [ "nx" || Holds] ++ [ "on" || On], " "),
    Cs = case Gd of
	     undefined -> [];
	     _ -> leaves(Gd, K, Env, Last)
	 end,
    {[{K, Coil}, {"r" ++ integer_to_list(N), case Active of
						   true  -> "rw";
						   false -> "rw off"
					       end} | Cs], G}.

gate_on({in, Names}, Env, Last, State) ->
    lists:any(fun(N) ->
		      candyspeak_eval:eval({field, 0, {'WORD', 0, N}},
					   Last, Env) =:= State
	      end, Names);
gate_on({'when', X}, Env, Last, _State) ->
    candyspeak_eval:truth(X, Last, Env) =:= true.

hd_class(true)  -> "hd t";
hd_class(false) -> "hd".

%% The contacts in the order ladder/4 and code/5 numbered them.
leaves(Gd, K, Env, Last) ->
    Xs = contacts(Gd),
    [{K ++ "_" ++ integer_to_list(I), ct_class(X, Env, Last)}
     || {I, X} <- lists:zip(lists:seq(0, length(Xs) - 1), Xs)].

contacts(X = {Op, _, _, _}) when Op =:= '&&'; Op =:= '||' ->
    lists:append([contacts(C) || C <- chain(Op, X)]);
contacts(X) -> [X].

ct_class(X, Env, Last) ->
    Base = case switchable(X, Env) of
	       {ok, _} -> "ct k";
	       none    -> "ct"
	   end,
    case candyspeak_eval:truth(X, Last, Env) of
	true    -> Base ++ " t";
	false   -> Base;
	unknown -> Base ++ " u"
    end.
