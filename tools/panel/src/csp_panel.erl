%%% The smallest complete loop: a switch, a lamp, and a trace.
%%%
%%% The panel is DERIVED, not configured. candyspeak:parse/1 gives the
%%% declarations, and the direction in them decides the widget: `digital in`
%%% is something you press, `digital out` is something that lights up. Add a
%%% declaration to the .csp file and the widget appears -- there is no second
%%% file to keep in step.
%%%
%%% Everything the panel sends is a line a human could type at the csp prompt
%%% ("> Button = 1"), which is what makes it debuggable: when the panel does
%%% something surprising, type the same line yourself and see who is wrong.

-module(csp_panel).

-export([start/0, start/2]).
-export([run/2, run/3]).
-export([widgets/1, rgb565/1, hue/1, label_of/1]).   % tests, introspection

-define(PERIOD,    100).   % ms between cycles
-define(TRACE_W,   720).   % canvas width, px
-define(ROW_H,      34).   % px per trace row
-define(STEP,        3).   % px per sample
-define(LAMP,     "display:inline-block;width:18px;height:18px;"
		  "border-radius:9px;border:1px solid #555;").
-define(PIXEL,    "display:inline-block;width:22px;height:22px;"
		  "border:1px solid #444;margin-right:2px;"
		  "vertical-align:middle;").
-define(LAMP_ON,  "background:#3f3;box-shadow:0 0 8px #3f3").
-define(LAMP_OFF, "background:#252525").

%%% --------------------------------------------------------------- starting
%%%
%%% wse_server speaks websocket on /websession and NOTHING else -- a plain GET
%%% for the page falls out of its handshake with a case_clause. So the page needs
%%% an ordinary web server: inets serves priv/ for panel.html, and ALIASES
%%% wse's own priv/ at /js for ej.js and wse.js. Copying those two in would
%%% work and is what one usually does, but then they silently diverge from the
%%% wse that is actually serving the websocket.

start() -> start(1234, 8080).

start(WsPort, HttpPort) ->
    ok = ensure_inets(),
    {ok, _} = inets:start(httpd,
			  [{port, HttpPort},
			   {server_name, "csp_panel"},
			   {server_root, priv_dir()},
			   {document_root, priv_dir()},
			   {alias, {"/js", wse_priv()}},
			   {bind_address, {127,0,0,1}},
			   {mime_types, [{"html","text/html"},
					 {"js","application/javascript"},
					 {"css","text/css"}]}]),
    {ok, _} = wse_server:start(WsPort),
    io:format("panel:  http://localhost:~w/panel.html~n", [HttpPort]),
    io:format("wse:    ws://localhost:~w/websession~n", [WsPort]),
    io:format("js from ~s~n", [wse_priv()]),
    ok.

ensure_inets() ->
    case application:ensure_all_started(inets) of
	{ok, _}                       -> ok;
	{error, {already_started, _}} -> ok;
	Err                           -> Err
    end.

%%% -------------------------------------------------------------------- entry

run(Ws, Where) ->
    run(Ws, Where, default_file()).

run(Ws, Where, File) ->
    render(Ws, Where, File).

%% Also the restart path: picking another file in the chooser comes back here,
%% so the whole panel is rebuilt from that file's declarations.
render(Ws, Where, File) ->
    Root = wse:id(Where),
    wse:set(Ws, Root, "innerHTML", ""),
    case candyspeak:parse(File) of
	{ok, Ast} ->
	    build(Ws, Where, Root, File, widgets(Ast), Ast);
	Error ->
	    chooser(Ws, Root, File),
	    text(Ws, Root, io_lib:format("~n~p cannot be parsed: ~p",
					 [File, Error]))
    end.

%%% ------------------------------------------------------------------ widgets

%% The whole derivation. A declaration's direction gives the widget, and an
%% `#annotate panel <target> kind=...' overrides it -- presentation is not in the
%% language and must not be, so it is said next to the declaration instead.
widgets(Ast) ->
    Ann = annotations(Ast),
    [apply_ann(W, Ann) || W <- lists:foldr(fun collect/2, [], Ast)].

%% #annotate panel <target> key=value ... -> #{Target => #{Key => Value}}
%% Only rows naming THIS tool. The keys are ours to validate: an unknown one is
%% warned about rather than ignored, because a layout that silently drops a typo
%% is how it stops matching the program.
annotations(Ast) ->
    lists:foldl(
      fun({annotate, Ln, {'WORD', _, "panel"}, {'WORD', _, Target}, Items}, M) ->
	      Kv = maps:from_list([{K, ann_value(V)}
				   || {{'WORD', _, K}, V} <- Items]),
	      warn_unknown(Target, Kv, Ln),
	      maps:put(Target, maps:merge(maps:get(Target, M, #{}), Kv), M);
	 (_, M) -> M
      end, #{}, Ast).

ann_value(true)              -> true;
ann_value({'WORD', _, V})    -> V;
ann_value({'INT', _, V})     -> V;
ann_value({'FLT', _, V})     -> V;
ann_value({'STR', _, V})     -> V;
ann_value(V)                 -> V.

-define(ANN_KEYS, ["kind", "colour", "color", "unit", "min", "max", "hidden",
		   "label"]).

warn_unknown(Target, Kv, Ln) ->
    case [K || K <- maps:keys(Kv), not lists:member(K, ?ANN_KEYS)] of
	[] -> ok;
	Ks -> io:format("panel: line ~w: ~s: unknown annotation key~s ~s~n"
			"       known: ~s~n",
			[Ln, Target, case Ks of [_] -> ""; _ -> "s" end,
			 string:join(Ks, ", "), string:join(?ANN_KEYS, ", ")])
    end.

%% `kind' may only move a widget WITHIN what the declaration allows: a digital in
%% can be a push or a toggle, not a dial. Asking for the impossible is a warning
%% and the derived kind stands -- the alternative is a panel that cannot drive
%% the pin it is wired to.
apply_ann({Kind, Name, W}, Ann) ->
    Kv = maps:get(Name, Ann, #{}),
    case maps:get("kind", Kv, undefined) of
	undefined -> {Kind, Name, W};
	Want ->
	    case allowed(Kind, Want) of
		{ok, K2} -> {K2, Name, W};
		no ->
		    io:format("panel: ~s: kind=~s is not one a ~w can be "
			      "(~s)~n", [Name, Want, Kind,
					 string:join(alternatives(Kind), ", ")]),
		    {Kind, Name, W}
	    end
    end.

allowed(toggle, "push")   -> {ok, push};
allowed(toggle, "toggle") -> {ok, toggle};
allowed(push, "toggle")   -> {ok, toggle};
allowed(push, "push")     -> {ok, push};
allowed(lamp, "action")   -> {ok, action};
allowed(lamp, "lamp")     -> {ok, lamp};
allowed(slider, "dial")   -> {ok, dial};
allowed(slider, "slider") -> {ok, slider};
allowed(slider, "value")  -> {ok, invalue};
allowed(meter, "value")   -> {ok, value};
allowed(meter, "meter")   -> {ok, meter};
allowed(pixel, "pixel")   -> {ok, pixel};
allowed(_Kind, _Want)     -> no.

alternatives(toggle) -> ["push", "toggle"];
alternatives(push)   -> ["push", "toggle"];
alternatives(lamp)   -> ["lamp", "action"];
alternatives(slider) -> ["slider", "dial", "value"];
alternatives(meter)  -> ["meter", "value"];
alternatives(pixel)  -> ["pixel"];
alternatives(_)      -> [].

collect(D, Acc) ->
    case widget(D) of
	skip               -> Acc;
	L when is_list(L)  -> L ++ Acc;      % an array: one widget per element
	W                  -> [W | Acc]
    end.

widget({digital, _Ln, {'WORD', _, Name}, scalar, _Res, Opts, _Expr}) ->
    case proplists:get_value(dir, Opts) of
	in -> {toggle, Name, 1};
	_  -> {lamp, Name, 1}       % out, inout: show it
    end;
%% Analog carries its WIDTH in the resolution slot, so the control's range is
%% derived too -- a :10 in is a 0..1023 slider, a :16 out a full-scale meter.
%% PORT 9 is special: it is the CPX pixel strip, where the 16 bits are an
%% RGB565 colour rather than a magnitude. That mapping is an assumption about
%% the board, not something the declaration says, which is why it lives here
%% and not in widget/1's shape.
widget({analog, _Ln, {'WORD', _, Name}, scalar, Res, Opts, Expr}) ->
    analog_widget(Name, width(Res), port_of(Expr),
		  proplists:get_value(dir, Opts));
%% An array: `#analog P[10]:16 out unsigned 9:0..9` is ten pixels, not one.
%% Returned as a LIST, which collect/2 splices -- cpx_rotate.csp declares its
%% strip this way and had no widgets at all until this clause existed.
widget({analog, _Ln, {'WORD', _, Name}, {array_size, _, Size}, Res, Opts,
	Expr}) ->
    N = width(Size),
    W = width(Res),
    Port = port_of(Expr),
    Dir = proplists:get_value(dir, Opts),
    [analog_widget(Name ++ "[" ++ integer_to_list(I) ++ "]", W, Port, Dir)
     || I <- lists:seq(0, N - 1)];
widget(_) ->
    skip.                           % variables, fields, rules: not yet

analog_widget(Name, W, 9, _Dir)   -> {pixel, Name, W};
analog_widget(Name, W, _P, in)    -> {slider, Name, W};
analog_widget(Name, W, _P, _Dir)  -> {meter, Name, W}.

width({'INT', _, S}) -> list_to_integer(S);
width(_)             -> 16.         % `default` -- treat as full word

%% [{port_pin,_,Port,Pin}] or [{pin,_,Pin}]; only the port decides anything here
port_of([{port_pin, _, {'INT', _, P}, _Pin} | _]) -> list_to_integer(P);
port_of(_)                                        -> 0.

full(W) when W > 0, W < 32 -> (1 bsl W) - 1;
full(_)                    -> 65535.

%% RGB565 -> css. The strip is written as one 16 bit word per pixel.
rgb565(V) ->
    R = (V bsr 11) band 16#1f,
    G = (V bsr 5)  band 16#3f,
    B = V band 16#1f,
    lists:flatten(io_lib:format("rgb(~w,~w,~w)",
				[R * 255 div 31, G * 255 div 63,
				 B * 255 div 31])).

%%% ---------------------------------------------------------------- rendering

build(Ws, Where, Root, File, [], Ast) ->
    %% Parsed fine, but nothing in it is a widget yet. Say which declarations
    %% it does have -- an empty panel otherwise looks like a failure, and
    %% most of examples/ lands here until analog and fields are wired up.
    {_SelId, _Sel, _Run} = chooser(Ws, Root, File),
    text(Ws, Root, io_lib:format(
		     "no digital pins in ~s.~n~n"
		     "it declares: ~s~n~n"
		     "only `digital in` and `digital out` are wired up so far.",
		     [shorten(File), kinds(Ast)])),
    wait_for_pick(Ws, Where);
build(Ws, Where, Root, File, Widgets, _Ast) ->
    {SelId, _Sel, Run} = chooser(Ws, Root, File),
    %% Pixels are one strip, not one row each -- see strip/3.
    {Pixels, Rest} = lists:partition(fun({K, _, _}) -> K =:= pixel end,
				     Widgets),
    RestNodes = [{Name, control(Ws, Root, W)} || W = {_, Name, _} <- Rest],
    PixNodes = strip(Ws, Root, Pixels),
    Ordered = Rest ++ Pixels,
    {_Canvas, Ctx} = canvas(Ws, Root, length(Ordered)),
    {ok, Link} = csp_link:open(csp_exe(), [File]),
    erlang:send_after(?PERIOD, self(), tick),
    loop(Ws, Link, #{nodes  => maps:from_list(RestNodes ++ PixNodes),
		     traces => [{N, K, W, hue(N)} || {K, N, W} <- Ordered],
		     ctx    => Ctx,
		     x      => 0,
		     last   => #{},
		     sel_id => SelId,
		     run    => Run,
		     mode   => run,
		     where  => Where}).

%% Lists every .csp under demo/ and the tree's examples/, so another program is
%% a pick rather than an edit of the page.
chooser(Ws, Root, Current) ->
    Bar = wse:createElement(Ws, "div"),
    wse:setStyle(Ws, Bar, "font:14px monospace;color:#888;margin-bottom:12px"),
    Sel = wse:createElement(Ws, "select"),
    wse:setStyle(Ws, Sel, "font:14px monospace;background:#222;color:#eee;"
		 "border:1px solid #555;padding:3px"),
    lists:foreach(
      fun(F) ->
	      O = wse:createElement(Ws, "option"),
	      wse:set(Ws, O, "value", F),
	      wse:appendChild(Ws, O, wse:createTextNode(Ws, shorten(F))),
	      case F =:= Current of
		  true  -> wse:set(Ws, O, "selected", true);
		  false -> ok
	      end,
	      wse:appendChild(Ws, Sel, O)
      end, available(Current)),
    {ok, Id} = wse:create_event(Ws),
    %% `this` in an onchange assigned this way is the select, so the new
    %% filename rides along and the loop needs no reverse map.
    Func = wse:newf(Ws, "", "{ Wse.notify(" ++ integer_to_list(Id) ++
			", this.value); }"),
    wse:set(Ws, Sel, "onchange", Func),
    wse:appendChild(Ws, Bar, Sel),
    Run = runstep(Ws, Bar),
    wse:appendChild(Ws, Root, Bar),
    {Id, Sel, Run}.

%% Run or step, and they are not the same clock.
%%
%% RUNNING: the panel sends /commit on a timer, and a `> X = 1' from a widget
%% runs a cycle of its own -- which is what you want while poking at a thing.
%%
%% STEPPING: /pause, so nothing moves until asked. Values set from widgets go in
%% WITHOUT running a cycle, and one /step runs exactly one with all of them
%% committed together -- the way a board commits every DIN at the top of a cycle.
%% That is also what a replayed model trace needs, since a trace step carries
%% several inputs belonging to the same instant.
runstep(Ws, Bar) ->
    Wrap = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Wrap, "margin-left:16px"),
    {ok, Id} = wse:create_event(Ws),
    Mk = fun(Label, Tag) ->
		 B = wse:createElement(Ws, "button"),
		 wse:setStyle(Ws, B, lists:flatten(btn_style(false))),
		 wse:appendChild(Ws, B, wse:createTextNode(Ws, Label)),
		 F = wse:newf(Ws, "", "{ Wse.notify(" ++ integer_to_list(Id) ++
				  ", '" ++ Tag ++ "'); }"),
		 wse:set(Ws, B, "onclick", F),
		 wse:appendChild(Ws, Wrap, B),
		 B
	 end,
    RunB  = Mk("run",   "mode=run"),
    PauseB = Mk("pause", "mode=step"),
    OneB  = Mk("step",  "step=1"),
    wse:appendChild(Ws, Bar, Wrap),
    #{event => Id, run => RunB, pause => PauseB, one => OneB}.

btn_style(Active) ->
    ["font:13px monospace;padding:3px 9px;margin-right:4px;"
     "border:1px solid #555;",
     case Active of
	 true  -> "background:#3af;color:#111;font-weight:bold";
	 false -> "background:#333;color:#ccc"
     end].

%% A row per widget: the name, then the control. The lamp node is what we
%% restyle when the value changes, so it is what we keep.
control(Ws, Root, {Kind, Name, W}) ->
    Row = wse:createElement(Ws, "div"),
    wse:setStyle(Ws, Row, "margin:6px 0;font:14px monospace"),
    Label = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Label, "display:inline-block;width:110px;color:#ccc"),
    wse:appendChild(Ws, Label, wse:createTextNode(Ws, label_of(Name))),
    wse:appendChild(Ws, Row, Label),
    Node = case Kind of
	       toggle  -> switch(Ws, Row, Name);
	       push    -> push(Ws, Row, Name);
	       lamp    -> lamp(Ws, Row, Name);
	       action  -> action(Ws, Row, Name);
	       slider  -> slider(Ws, Row, Name, W);
	       dial    -> dial(Ws, Row, Name, W);
	       invalue -> invalue(Ws, Row, Name);
	       meter   -> meter(Ws, Row, W);
	       value   -> value_out(Ws, Row)
	   end,
    wse:appendChild(Ws, Root, Row),
    Node.

%% An input's value has to travel with the name, so the notify carries
%% "Name=Value" and the loop splits on the first '='. A switch sends the bare
%% name, which is how the two are told apart.
slider(Ws, Row, Name, W) ->
    S = wse:createElement(Ws, "input"),
    wse:set(Ws, S, "type", "range"),
    wse:set(Ws, S, "min", 0),
    wse:set(Ws, S, "max", full(W)),
    wse:set(Ws, S, "value", 0),
    wse:setStyle(Ws, S, "width:200px;vertical-align:middle"),
    {ok, Id} = wse:create_event(Ws),
    Func = wse:newf(Ws, "", "{ Wse.notify(" ++ integer_to_list(Id) ++
			", '" ++ Name ++ "=' + this.value); }"),
    wse:set(Ws, S, "oninput", Func),
    wse:appendChild(Ws, Row, S),
    Out = readout(Ws, Row),
    {slider, S, Out}.

%% Held down rather than latched: mousedown sends 1, mouseup sends 0. Reuses the
%% slider wire format (Name=Value) rather than inventing a second one.
push(Ws, Row, Name) ->
    B = wse:createElement(Ws, "button"),
    wse:setStyle(Ws, B, "width:60px;padding:4px;font:14px monospace;"
		 "background:#333;color:#eee;border:1px solid #555"),
    wse:appendChild(Ws, B, wse:createTextNode(Ws, "0")),
    {ok, Id} = wse:create_event(Ws),
    Down = wse:newf(Ws, "", "{ Wse.notify(" ++ integer_to_list(Id) ++
			", '" ++ Name ++ "=1'); }"),
    Up = wse:newf(Ws, "", "{ Wse.notify(" ++ integer_to_list(Id) ++
		      ", '" ++ Name ++ "=0'); }"),
    wse:set(Ws, B, "onmousedown", Down),
    wse:set(Ws, B, "onmouseup", Up),
    wse:set(Ws, B, "onmouseleave", Up),     % dragging off must not leave it held
    wse:appendChild(Ws, Row, B),
    {switch, B}.

%% Something that DOES rather than indicates: a siren, a motor, a valve. Shown as
%% text that lights up, because a 18px dot does not say "the pump is running".
action(Ws, Row, Name) ->
    A = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, A, action_style(0, hue(Name))),
    wse:appendChild(Ws, A, wse:createTextNode(Ws, "off")),
    wse:appendChild(Ws, Row, A),
    {action, A, hue(Name)}.

action_style(V, {On, Off}) ->
    ["display:inline-block;min-width:54px;text-align:center;padding:3px 8px;"
     "border-radius:3px;font:12px monospace;border:1px solid #555;",
     case V of
	 0 -> ["background:", Off, ";color:#777"];
	 _ -> ["background:", On, ";color:#111;font-weight:bold"]
     end].

%% A dial is a slider for the hand and a needle for the eye: the same range
%% input, plus a rotating pointer that reads at a glance.
dial(Ws, Row, Name, W) ->
    Face = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Face, "display:inline-block;width:38px;height:38px;"
		 "border-radius:19px;border:2px solid #555;background:#252525;"
		 "position:relative;vertical-align:middle;margin-right:8px"),
    Needle = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Needle, needle_style(0, W)),
    wse:appendChild(Ws, Face, Needle),
    wse:appendChild(Ws, Row, Face),
    {slider, S, Out} = slider(Ws, Row, Name, W),
    {dial, Needle, S, Out, W}.

%% -135..+135 degrees, the usual instrument sweep.
needle_style(V, W) ->
    Deg = -135 + (270 * min(V, full(W)) div max(full(W), 1)),
    ["position:absolute;left:50%;top:50%;width:2px;height:15px;"
     "background:#3af;transform-origin:50% 100%;"
     "transform:translate(-50%,-100%) rotate(", integer_to_list(Deg), "deg)"].

%% An editable field, for an input whose value is a number rather than a level.
invalue(Ws, Row, Name) ->
    I = wse:createElement(Ws, "input"),
    wse:set(Ws, I, "type", "text"),
    wse:set(Ws, I, "size", 8),
    wse:setStyle(Ws, I, "font:13px monospace;background:#222;color:#eee;"
		 "border:1px solid #555;padding:2px 4px"),
    {ok, Id} = wse:create_event(Ws),
    %% on Enter, not on every keystroke: half a number is not a value
    Func = wse:newf(Ws, "e", "{ if (e && e.key == 'Enter') Wse.notify(" ++
			integer_to_list(Id) ++ ", '" ++ Name ++
			"=' + this.value); }"),
    wse:set(Ws, I, "onkeydown", Func),
    wse:appendChild(Ws, Row, I),
    {invalue, I}.

%% Just the number, for an output nobody wants a bar for.
value_out(Ws, Row) ->
    T = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, T, "font:14px monospace;color:#eee;min-width:60px;"
		 "display:inline-block"),
    wse:appendChild(Ws, T, wse:createTextNode(Ws, "0")),
    wse:appendChild(Ws, Row, T),
    {value, T}.

meter(Ws, Row, W) ->
    Outer = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Outer, "display:inline-block;width:200px;height:12px;"
		 "background:#252525;border:1px solid #555;vertical-align:middle"),
    Bar = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Bar, "display:block;height:100%;width:0;background:#3af"),
    wse:appendChild(Ws, Outer, Bar),
    wse:appendChild(Ws, Row, Outer),
    Out = readout(Ws, Row),
    {meter, Bar, Out, W}.

%% Every port 9 pixel side by side on one line. That is what the strip is on
%% the board, and one swatch per row neither looks like the hardware nor lets
%% you see a pattern travel along it -- which is the whole of what cpx_ball
%% does.
strip(_Ws, _Root, []) -> [];
strip(Ws, Root, Pixels) ->
    Row = wse:createElement(Ws, "div"),
    wse:setStyle(Ws, Row, "margin:12px 0;font:14px monospace"),
    Label = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, Label, "display:inline-block;width:110px;color:#ccc"),
    wse:appendChild(Ws, Label, wse:createTextNode(Ws, "port 9")),
    wse:appendChild(Ws, Row, Label),
    Nodes = [{Name, cell(Ws, Row, Name)} || {_K, Name, _W} <- Pixels],
    wse:appendChild(Ws, Root, Row),
    Nodes.

cell(Ws, Row, Name) ->
    C = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, C, ?PIXEL "background:#000"),
    wse:set(Ws, C, "title", Name),      % hover says which pixel
    wse:appendChild(Ws, Row, C),
    {pixel, C}.

readout(Ws, Row) ->
    T = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, T, "margin-left:10px;color:#888;font:12px monospace"),
    wse:appendChild(Ws, T, wse:createTextNode(Ws, "")),
    wse:appendChild(Ws, Row, T),
    T.

switch(Ws, Row, Name) ->
    B = wse:createElement(Ws, "button"),
    wse:setStyle(Ws, B, "width:60px;padding:4px;font:14px monospace;"
		 "background:#333;color:#eee;border:1px solid #555"),
    wse:appendChild(Ws, B, wse:createTextNode(Ws, "0")),
    {ok, Id} = wse:create_event(Ws),
    Func = wse:newf(Ws, "", "{ Wse.notify(" ++ integer_to_list(Id) ++
			",'" ++ Name ++ "'); }"),
    wse:set(Ws, B, "onclick", Func),
    wse:appendChild(Ws, Row, B),
    {switch, B}.

lamp(Ws, Row, Name) ->
    L = wse:createElement(Ws, "span"),
    wse:setStyle(Ws, L, ?LAMP ?LAMP_OFF),
    wse:appendChild(Ws, Row, L),
    {lamp, L, hue(Name)}.

%% Take the hint from the name. A traffic light whose lamps all glow green is
%% harder to read than one that looks like a traffic light, and the name is the
%% only thing that knows -- the declaration says `out 5`, nothing about colour.
%% A plain `Led` stays red, which is what a bare indicator LED usually is.
%%
%% A `__Colour` SUFFIX says it outright, for the cases no word in the name
%% implies: `Heater__Orange`, `BathFan__Blue`. The suffix is stripped from the
%% label, so the panel shows `Heater` -- see label_of/1.
%%
%% This is a stopgap for demos and development, not a design. It puts
%% presentation into the identifier, which then has to be spelled out in every
%% rule that touches it. A layout file that names colours per signal is the
%% right answer and does not need the language to change.
hue(Name) ->
    case suffix_colour(Name) of
	{ok, Hue} -> Hue;
	none      -> hue_from_words(Name)
    end.

%% Everything after the last "__", matched against the palette.
suffix_colour(Name) ->
    case string:split(Name, "__", trailing) of
	[_, Tail] when Tail =/= [] -> palette(string:lowercase(Tail));
	_                          -> none
    end.

palette("red")     -> {ok, {"#f44", "#251515"}};
palette("orange")  -> {ok, {"#f92", "#251a10"}};
palette("yellow")  -> {ok, {"#fd3", "#252210"}};
palette("green")   -> {ok, {"#3f3", "#152515"}};
palette("cyan")    -> {ok, {"#3dd", "#102525"}};
palette("blue")    -> {ok, {"#5af", "#101825"}};
palette("purple")  -> {ok, {"#b7f", "#1d1526"}};
palette("pink")    -> {ok, {"#f7b", "#261520"}};
palette("white")   -> {ok, {"#eee", "#222"}};
palette(_)         -> none.

hue_from_words(Name) ->
    Lower = string:lowercase(Name),
    Hit = fun(S) -> string:find(Lower, S) =/= nomatch end,
    case Hit("green") orelse Hit("gron") orelse Hit("grn") of
	true -> {"#3f3", "#152515"};
	false ->
	    case Hit("yellow") orelse Hit("amber") orelse Hit("gul") of
		true -> {"#fd3", "#252210"};
		false ->
		    case Hit("blue") orelse Hit("bla") of
			true -> {"#5af", "#101825"};
			false ->
			    case Hit("white") orelse Hit("vit") of
				true  -> {"#eee", "#222"};
				%% red: Red, Rod, and everything unlabelled
				false -> {"#f44", "#251515"}
			    end
		    end
	    end
    end.

%% What the panel shows. The csp name keeps its suffix -- that is what the
%% dump and every rule call it -- but nobody wants to read it.
label_of(Name) ->
    case string:split(Name, "__", trailing) of
	[Head, Tail] when Head =/= [], Tail =/= [] ->
	    case palette(string:lowercase(Tail)) of
		{ok, _} -> Head;         % a colour: hide it
		none    -> Name          % something else: leave it alone
	    end;
	_ -> Name
    end.

canvas(Ws, Root, NTraces) ->
    C = wse:createElement(Ws, "canvas"),
    wse:set(Ws, C, "width", ?TRACE_W),
    wse:set(Ws, C, "height", NTraces * ?ROW_H + 8),
    wse:setStyle(Ws, C, "margin-top:14px;background:#1a1a1a;"
		 "border:1px solid #333"),
    wse:appendChild(Ws, Root, C),
    {ok, Ctx} = wse:call(Ws, C, "getContext", ["2d"]),
    {C, Ctx}.

%% Nothing to drive, so there is no csp and no tick -- just the chooser.
wait_for_pick(Ws, Where) ->
    receive
	{notify, _Id, _Local, File} -> render(Ws, Where, File);
	{closed, _}                 -> ok;
	stop                        -> ok
    end.

%% Which declaration types a file actually has, counted, for the message above.
kinds(Ast) ->
    Counts = lists:foldl(fun(D, M) when is_tuple(D) ->
				 K = element(1, D),
				 maps:update_with(K, fun(N) -> N + 1 end, 1, M);
			    (_, M) -> M
			 end, #{}, Ast),
    string:join([io_lib:format("~w x~w", [K, N])
		 || {K, N} <- lists:sort(maps:to_list(Counts))], ", ").

%%% --------------------------------------------------------------------- loop

loop(Ws, Link, S) ->
    SelId = maps:get(sel_id, S),
    %% Bound before the receive so the clause can match on it: a guard may not
    %% call maps:get, and matching a bound variable is what tells the bar's
    %% events apart from a widget's without reading the payload.
    RunId = maps:get(event, maps:get(run, S, #{event => undefined}),
		     undefined),
    receive
	%% Another program was picked: drop this csp and rebuild from that file.
	{notify, SelId, _Local, File} ->
	    csp_link:close(Link),
	    flush(),
	    render(Ws, maps:get(where, S), File);

	%% run / pause / step. Sent by the bar, and told apart from a widget by
	%% the event id rather than by the payload, so a signal called "step"
	%% could not be mistaken for the button.
	{notify, RunId, _Local, Tag} ->
	    loop(Ws, Link, control(Ws, Link, S, Tag));

	%% A switch was clicked. We do not track its state ourselves -- we
	%% toggle against what csp last told us, so the button can never
	%% disagree with the program.
	{notify, _Id, _Local, Data} ->
	    case string:split(Data, "=") of
		[Name, Val] ->
		    %% a slider: send the value it was dragged to
		    csp_link:set(Link, Name, Val);
		[Name] ->
		    %% a switch: toggle against what csp last told us, so the
		    %% button can never disagree with the program
		    Cur = maps:get(Name, maps:get(last, S), 0),
		    csp_link:set(Link, Name, case Cur of 0 -> 1; _ -> 0 end)
	    end,
	    loop(Ws, Link, S);

	{csp_state, _N, Vals} ->
	    loop(Ws, Link, update(Ws, S, Vals));

	%% Draw on the TICK, not on the dump. csp only dumps what changed, so a
	%% trace driven by dumps stops moving as soon as the program settles --
	%% which is exactly when you are staring at it wondering why. Drawing
	%% here also means the x axis is wall time, not cycle number.
	%% The timer keeps arriving while stepping -- it just does not drive the
	%% program. Drawing still happens, so a held value stays visible as a
	%% flat line rather than the trace freezing mid-air.
	tick ->
	    S1 = draw(Ws, S),
	    case maps:get(mode, S, run) of
		run  -> csp_link:tick(Link);
		step -> ok
	    end,
	    erlang:send_after(?PERIOD, self(), tick),
	    loop(Ws, Link, S1);

	{csp_exit, _Status} -> ok;
	{closed, _}         -> csp_link:close(Link);
	stop                -> csp_link:close(Link)
    end.

%% run: let the program go. step: freeze it, so values set from widgets wait for
%% a /step and are committed together.
control(Ws, Link, S, "mode=run") ->
    csp_link:resume(Link),
    S1 = S#{mode := run},
    light_mode(Ws, S1),
    S1;
control(Ws, Link, S, "mode=step") ->
    csp_link:pause(Link),
    S1 = S#{mode := step},
    light_mode(Ws, S1),
    S1;
control(_Ws, Link, S, "step=1") ->
    %% Only meaningful while stopped; in run mode the program is already going
    %% and a step would be indistinguishable from the tick.
    case maps:get(mode, S, run) of
	step -> csp_link:step(Link, 1);
	run  -> ok
    end,
    S;
control(_Ws, _Link, S, _Other) ->
    S.

%% Which of the three is in force, so the bar says where you are.
light_mode(Ws, S) ->
    #{run := RunB, pause := PauseB, one := OneB} = maps:get(run, S),
    Step = maps:get(mode, S, run) =:= step,
    style(Ws, RunB,   btn_style(not Step)),
    style(Ws, PauseB, btn_style(Step)),
    %% the step button is only live while stopped
    style(Ws, OneB, [btn_style(false),
		     case Step of true -> ";opacity:1"; false -> ";opacity:0.4" end]).

%% Dumps and ticks already in flight belong to the csp we just dropped.
flush() ->
    receive
	{csp_state, _, _} -> flush();
	{csp_exit, _}     -> flush();
	tick              -> flush()
    after 0 -> ok
    end.

%% csp only dumps what changed, so merge into `last` rather than replace it.
%% That is also what makes a trace hold its level between dumps.
update(Ws, S, Vals) ->
    paint(Ws, S, Vals),
    S#{last := maps:merge(maps:get(last, S), maps:from_list(Vals))}.

paint(Ws, S, Vals) ->
    Nodes = maps:get(nodes, S),
    lists:foreach(fun({Name, V}) -> paint1(Ws, maps:find(Name, Nodes), V) end,
		  Vals).

paint1(Ws, {ok, {lamp, Node, {On, Off}}}, V) ->
    style(Ws, Node,
	  case V of
	      0 -> [?LAMP, "background:", Off];
	      _ -> [?LAMP, "background:", On, ";box-shadow:0 0 9px ", On]
	  end);
paint1(Ws, {ok, {switch, Node}}, V) ->
    wse:set(Ws, Node, "textContent", label(V));
paint1(Ws, {ok, {meter, Bar, Out, W}}, V) ->
    Pct = 100 * min(V, full(W)) div max(full(W), 1),
    style(Ws, Bar, io_lib:format("display:block;height:100%;"
				 "background:#3af;width:~w%", [Pct])),
    wse:set(Ws, Out, "textContent", integer_to_list(V));
paint1(Ws, {ok, {action, Node, Hue}}, V) ->
    style(Ws, Node, action_style(V, Hue)),
    wse:set(Ws, Node, "textContent", case V of 0 -> "off"; _ -> "ON" end);
paint1(Ws, {ok, {dial, Needle, _S, Out, W}}, V) ->
    style(Ws, Needle, needle_style(V, W)),
    wse:set(Ws, Out, "textContent", integer_to_list(V));
paint1(Ws, {ok, {value, Node}}, V) ->
    wse:set(Ws, Node, "textContent", integer_to_list(V));
%% An input's field is left alone while it is being typed in, same reasoning as
%% the slider: csp owning it would fight the hand.
paint1(_Ws, {ok, {invalue, _Node}}, _V) ->
    ok;
paint1(Ws, {ok, {pixel, Node}}, V) ->
    style(Ws, Node, [?PIXEL, "background:", rgb565(V)]);
%% A slider is an INPUT: csp owning the value would fight the drag, so only the
%% readout follows. The control itself is left where the hand put it.
paint1(Ws, {ok, {slider, _S, Out}}, V) ->
    wse:set(Ws, Out, "textContent", integer_to_list(V));
paint1(_Ws, _, _V) ->
    ok.

label(0) -> "0";
label(_) -> "1".

%% wse encodes a plain Erlang list as a JSON ARRAY, and only a flat string as a
%% string -- so an iolist handed to setStyle arrives as [["..."],["..."]] and the
%% style is silently dropped. Everything built up from pieces goes through here.
style(Ws, Node, IoList) ->
    wse:setStyle(Ws, Node, lists:flatten(IoList)).

%% One column per tick, drawn incrementally: two fillRects per trace beats
%% redrawing the canvas, and cast/4 does not wait for the browser to answer.
draw(Ws, S) ->
    X = maps:get(x, S),
    Ctx = maps:get(ctx, S),
    case X >= ?TRACE_W of
	true ->
	    wse:cast(Ws, Ctx, "clearRect", [0, 0, ?TRACE_W, 4096]),
	    draw(Ws, S#{x := 0});
	false ->
	    Last = maps:get(last, S),
	    lists:foldl(
	      fun({Name, Kind, W, Hue}, Row) ->
		      V = maps:get(Name, Last, 0),
		      sample(Ws, Ctx, X, Row * ?ROW_H + 4, Kind, W, V, Hue),
		      Row + 1
	      end, 0, maps:get(traces, S)),
	    S#{x := X + ?STEP}
    end.

%% Digital is two levels; analog is a height in the row, so the trace reads as a
%% waveform rather than a bit. A pixel is drawn as the colour itself -- a strip
%% of them over time is what a running animation actually looks like.
sample(Ws, Ctx, X, Top, pixel, _W, V, _Hue) ->
    wse:set(Ws, Ctx, "fillStyle", rgb565(V)),
    wse:cast(Ws, Ctx, "fillRect", [X, Top, ?STEP, ?ROW_H - 10]);
sample(Ws, Ctx, X, Top, Kind, W, V, _Hue) when Kind =:= meter;
					       Kind =:= slider;
					       Kind =:= dial;
					       Kind =:= value;
					       Kind =:= invalue ->
    H = ?ROW_H - 12,
    Y = Top + H - (H * min(V, full(W)) div max(full(W), 1)),
    wse:set(Ws, Ctx, "fillStyle", "#3af"),
    wse:cast(Ws, Ctx, "fillRect", [X, Y, ?STEP, 3]);
%% The lamp's own colour, so a traffic light's three traces are red, yellow and
%% green rather than three identical stripes.
sample(Ws, Ctx, X, Top, _Kind, _W, V, {On, _Off}) ->
    Y = case V of 0 -> Top + ?ROW_H - 14; _ -> Top end,
    wse:set(Ws, Ctx, "fillStyle", case V of 0 -> "#3a3a4a"; _ -> On end),
    wse:cast(Ws, Ctx, "fillRect", [X, Y, ?STEP, 3]).

%%% -------------------------------------------------------------------- files

available(Current) ->
    Found = lists:append([csp_in(demo_dir()), csp_in(examples_dir())]),
    lists:usort([Current | Found]).

csp_in(Dir) ->
    case file:list_dir(Dir) of
	{ok, Fs} -> [filename:join(Dir, F) || F <- Fs,
					      filename:extension(F) =:= ".csp"];
	_        -> []
    end.

%% The last two components are enough to tell demo/gate.csp from
%% examples/gate.csp, and a full path fills the whole control.
shorten(Path) ->
    filename:join(
      lists:reverse(lists:sublist(lists:reverse(filename:split(Path)), 2))).

default_file() ->
    F = filename:join(demo_dir(), "gate.csp"),
    case filelib:is_regular(F) of
	true  -> F;
	false -> hd(available(F))
    end.

text(Ws, Root, Str) ->
    P = wse:createElement(Ws, "pre"),
    wse:setStyle(Ws, P, "color:#e77;font:13px monospace"),
    wse:appendChild(Ws, P, wse:createTextNode(Ws, lists:flatten(Str))),
    wse:appendChild(Ws, Root, P).

%%% -------------------------------------------------------------------- paths

%% Derived from the module's own location, which holds while the beam sits in
%% tools/panel/ebin -- but overridable, because when it does not hold the
%% failure is an enoent on a path nobody typed.
demo_dir() ->
    case os:getenv("CSP_PANEL_DEMO") of
	false -> filename:join(base(), "demo");
	Dir   -> Dir
    end.

examples_dir() -> filename:join(root(), "examples").
priv_dir()     -> filename:join(base(), "priv").

csp_exe() ->
    Exe = case os:getenv("CSP_EXE") of
	      false -> filename:join(root(), "csp");
	      Path  -> Path
	  end,
    case filelib:is_regular(Exe) of
	true  -> Exe;
	false -> error({csp_not_found, Exe, "set CSP_EXE to the csp binary"})
    end.

%% wse's priv/, taken from where its beam actually is, so the aliased scripts
%% are always the ones belonging to the wse serving the websocket.
wse_priv() ->
    filename:join(filename:dirname(
		    filename:dirname(filename:absname(code:which(wse)))),
		  "priv").

%% absname first: with a relative -pa, code:which/1 answers a relative path
%% and every dirname above would climb out of the wrong tree.
base() ->
    filename:dirname(filename:dirname(filename:absname(code:which(?MODULE)))).

root() ->
    filename:dirname(filename:dirname(base())).
