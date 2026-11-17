%%% @doc
%%%    The candyspeak compiler in Erlang: a program's AST (candyspeak:parse/1)
%%%    to the terms csp_disasm writes and csp_asm reads -- {header, H},
%%%    {strings, S}, the declarations, the instructions -- so a host builds an
%%%    image without the C compiler.
%%%
%%%    The C compiler (src/csp_compile.c) is the oracle: `make compile_check'
%%%    compiles every program both ways and wants the same terms. What is not
%%%    compiled yet throws {unsupported, What}, and the check counts it.
%%%
%%%    NAMES travel in OP_SEGMENT runs in the code: 32 slots (128 bytes) each,
%%%    a name a length byte and its text, appended in declaration order; a
%%%    name that does not fit opens a new segment where the code stands. The
%%%    strings table numbers them in that order, 0 the empty one.
%%%
%%%    The program starts with what the runtime declares before it: State,
%%%    the built-in states, and the Sys module with its object sys.
%%%
%%%    EXPRESSIONS are compiled as the C compiler's shunting yard compiles
%%%    them, which comes down to: post order, with a LEAF LAZY -- a variable
%%%    or a literal is loaded only when the operator over it is reduced, left
%%%    then right, so `N + M * 2' computes M * 2 before it loads N. Registers
%%%    come off a LIFO stack, fresh each statement; the result's is taken
%%%    before the operands' go back. A variable loaded into a register that is
%%%    still live is not loaded again (map_reg). Two literals fold, with the
%%%    runtime's 32-bit arithmetic; a #constant is a declaration and does not.
%%%    `a > b' is `b < a' with the swap flag.
%%%
%%%    A RULE: the guard into a register and MOVed into a fresh one (or LI -1:
%%%    none), RULE, the body -- a small literal is one STI -- and NEXT on the
%%%    last value. RULE's nxt is the distance to its NEXT.
%%% @end
-module(csp_cc).

-export([file/1, compile/1]).
-export([t/2]).                         % the tests: a conversion by name

t(F, A) when F =:= fltbits; F =:= fix_from_ieee; F =:= fix_ieee_exact;
	     F =:= fix_to_ieee -> apply(fun_of(F), A).

fun_of(fltbits) -> fun fltbits/1;
fun_of(fix_from_ieee) -> fun fix_from_ieee/1;
fun_of(fix_ieee_exact) -> fun fix_ieee_exact/1;
fun_of(fix_to_ieee) -> fun fix_to_ieee/1.

-define(SEG_SLOTS, 32).
-define(SEG_BYTES, (?SEG_SLOTS * 4)).
-define(VERSION, 23).
-define(CURRENT, 16#8000).                % an index relative to the object

file(F) ->
    case candyspeak:parse(F, [sources]) of
	{ok, Ast} -> compile(Ast, F);
	Error -> Error
    end.

%% {ok, Terms} | {unsupported, What}.
compile(Ast) -> compile(Ast, none).

compile(Ast, Top) ->
    try
	S0 = (prelude(new()))#{files => [Top]},
	S1 = close_tinit(lists:foldl(fun item_/2, S0, resugar(Ast))),
	[] =:= maps:get(blocks, S1) orelse unsupported(open_block),
	{ok, terms(decl('end', [], none, S1))}
    catch
	throw:{unsupported, _} = U -> U
    end.

new() ->
    #{decls => [], nd => 0, code => [], pc => 0, seg => none, segs => [],
      names => [], consts => #{}, env => #{}, states => [], free => [],
      rmap => #{}, defs => #{}, blocks => [], menv => none, mods => #{},
      nobj => 1, tgt => none, arrays => #{}, tinit => none}.

%% candyspeak:parse/2 hands #else on as `#end' and `#when !C' on its line --
%% the C compiler does not, it jumps (see else/1). A block never spans a file
%% and the files are marked, so an #end and a #when on the same line are an
%% #else.
resugar([{'end', L}, {'when', L, _} | T]) -> [{'else', L} | resugar(T)];
resugar([X | T]) -> [X | resugar(T)];
resugar([]) -> [].

%%% ------------------------------------------------------------- the image

terms(S = #{decls := Ds, names := Ns}) ->
    #{code := Code} = close_seg(S),
    Strings = maps:from_list([{0, []} |
			      lists:zip(lists:seq(1, length(Ns)),
					lists:reverse(Ns))]),
    [{header, #{version => ?VERSION, generation => 0, role => 0,
		endian => little, float => fixpoint}},
     {strings, Strings} | lists:reverse(Ds) ++ lists:sort(Code)].

%%% ------------------------------------------------------------ the names

%% Name N into the current segment, a new one when it does not fit.
name(N, S0 = #{names := Ns}) ->
    B = list_to_binary(N),
    Need = 1 + byte_size(B),
    S = case S0 of
	    #{seg := {_, Used, _}} when Used + Need =< ?SEG_BYTES -> S0;
	    _ -> open_seg(close_seg(S0))
	end,
    #{seg := {At, Used1, Data}} = S,
    S#{seg := {At, Used1 + Need, <<Data/binary, (byte_size(B)), B/binary>>},
       names := [B | Ns]}.

%% The string literals of a statement, in the order it is written: the C
%% compiler takes them in while it matches the line, before any of its code.
literals({'STR', _, T}, S) -> element(2, intern(T, S));
literals(X, S) when is_tuple(X) -> literals(tuple_to_list(X), S);
literals(L, S) when is_list(L) -> lists:foldl(fun literals/2, S, L);
literals(_, S) -> S.

%% A string VALUE: the text already in the table (a name too), else a new one.
%% Its value is its number there. Escapes as the C scanner reads them.
intern(T0, S = #{names := Ns}) ->
    T = unescape(T0),
    B = list_to_binary(T),
    case string:str(lists:reverse(Ns), [B]) of
	0 -> S1 = #{names := Ns1} = name(T, S), {length(Ns1), S1};
	I -> {I, S}
    end.

unescape([$\\, C | T]) ->
    [case C of
	 $a -> 7; $b -> 8; $e -> 27; $f -> 12; $n -> 10; $r -> 13;
	 $t -> 9; $v -> 11; _ -> C
     end | unescape(T)];
unescape([C | T]) -> [C | unescape(T)];
unescape([]) -> [].

open_seg(S = #{pc := Pc}) ->
    S#{seg := {Pc, 0, <<>>}, pc := Pc + 1 + ?SEG_SLOTS}.

close_seg(S = #{seg := none}) -> S;
close_seg(S = #{seg := {At, Used, Data}, code := C}) ->
    Pad = ?SEG_BYTES - byte_size(Data),
    I = {instr, At, 'SEGMENT', [{slots, ?SEG_SLOTS}, {used, Used},
				  {data, <<Data/binary, 0:Pad/unit:8>>}]},
    S#{seg := none, code := [I | C]}.

%%% ----------------------------------------------------- the declarations

%% A name declared in a module is the module's own: CURRENT-relative, the
%% object's when the module runs for it.
decl(Kind, Name, Opts, S0 = #{decls := Ds, nd := N, env := Env, menv := Me}) ->
    S = case {Name, Me} of
	    {[], _} -> S0;
	    {_, none} -> (name(Name, S0))#{env := Env#{Name => {N, Kind, vt(Kind, Opts)}}};
	    _ -> (name(Name, S0))#{menv := Me#{Name => {?CURRENT bor N, Kind,
							 vt(Kind, Opts)}}}
	end,
    D = case {Name, Opts} of
	    {[], none} -> {decl, N, Kind};
	    _ -> {decl, N, Kind, list_to_binary(Name), Opts}
	end,
    S#{decls := [D | Ds], nd := N + 1}.

prelude(S0) ->
    S1 = decl(variable, "State", var_opts(32, none, integer, 0), S0),
    S2 = states(["INIT", "NORMAL", "FAILSAFE"], S1),
    S3 = decl(module, "Sys", [{n, 5}, {ent, 0}], S2),
    S4 = decl(variable, "Serial", var_opts(32, none, unsigned, 0), S3),
    S5 = decl(param, "Id", [{size, 32}, {type, unsigned}, {init, 0}], S4),
    S6 = decl(param, "Name", [{size, 32}, {type, string}, {init, []}], S5),
    S7 = decl(variable, "Image", var_opts(32, none, unsigned, 0), S6),
    S8 = decl(param, "Boot", [{size, 32}, {type, unsigned}, {init, 255}], S7),
    S9 = decl('end', [], none, S8),
    decl_obj("Sys", "sys", [{mx, 2}, {m, 1}], S9).

%% An object's declaration names its module and itself.
decl_obj(M, O, Opts, S0 = #{decls := Ds, nd := N, env := Env}) ->
    S = (name(O, S0))#{env := Env#{O => {N, object, {M, proplists:get_value(m, Opts)}}}},
    S#{decls := [{decl, N, object, list_to_binary(M), list_to_binary(O), Opts} | Ds],
       nd := N + 1}.

%% The value type a declaration has in an expression.
vt(digital, _) -> integer;
vt(timer, _) -> timer;
vt(buffer, _) -> unsigned;
vt(_, Opts) when is_list(Opts) -> proplists:get_value(type, Opts, integer);
vt(_, _) -> none.

var_opts(Size, Dir, Type, Init) ->
    [{size, Size}, {dir, Dir}, {type, Type}, {init, Init}].

%% Six names a declaration; more take another. A name that is a state
%% already is not declared again.
states(Ns0, S0) ->
    New = lists:foldl(fun(N, Acc) ->
			      case state_no(N, S0) =:= none andalso
				  not lists:member(N, Acc) of
				  true -> Acc ++ [N];
				  false -> Acc
			      end
		      end, [], Ns0),
    states_(New, S0).

states_([], S) -> S;
states_(Ns, S) ->
    {Six, Rest} = lists:split(min(6, length(Ns)), Ns),
    [First | More] = Six,
    Pad = More ++ lists:duplicate(5 - length(More), []),
    %% a state name already in the table is that string, as a literal is
    #{decls := Ds, nd := N} = S1 =
	lists:foldl(fun(X, Si) -> element(2, intern(X, Si)) end, S, Six),
    S1a = S1#{states := maps:get(states, S1) ++ [X || X <- Six]},
    Opts = [{list_to_atom("name" ++ integer_to_list(K)), bin(X)}
	    || {K, X} <- lists:zip(lists:seq(2, 6), Pad)],
    states_(Rest, S1a#{decls := [{decl, N, states, bin(First), Opts} | Ds],
		       nd := N + 1}).

bin([]) -> [];
bin(X)  -> list_to_binary(X).

%%% ---------------------------------------------------------------- items

%% The INIT block of timers whose period is a #param stays open over the
%% next such timer, and closes where the next code goes.
item_(I, S) when element(1, I) =:= timer; element(1, I) =:= variable;
		 element(1, I) =:= constant; element(1, I) =:= param;
		 element(1, I) =:= digital; element(1, I) =:= analog;
		 element(1, I) =:= buffer; element(1, I) =:= field;
		 element(1, I) =:= define; element(1, I) =:= states;
		 element(1, I) =:= source; element(1, I) =:= annotate ->
    item(I, S);
item_(I, S) -> item(I, close_tinit(S)).

close_tinit(S = #{tinit := none}) -> S;
close_tinit(S = #{tinit := J}) -> patch_nxt(J, S#{tinit := none}).

%% An ARRAY is its elements, each declared as a scalar would be, put in one
%% declaration: what they share once, what differs per element under
%% `elems'. Its name is the first element's; the rest have none.
item(D, S0) when element(4, D) =/= scalar, element(1, D) =/= param,
		 element(1, D) =/= timer, tuple_size(D) >= 5,
		 is_tuple(element(4, D)), element(1, element(4, D)) =:= array_size ->
    {array_size, _, Sz} = element(4, D),
    Len = fold(Sz, S0),
    Elems = [element_decl(D, K, S0) || K <- lists:seq(0, Len - 1)],
    {decl, Ix, Kind, Name, _} = hd(Elems),
    Optss = [O || {decl, _, _, _, O} <- Elems],
    Keys = [K || {K, _} <- hd(Optss)],
    Same = [K || K <- Keys, length(lists:usort([proplists:get_value(K, O) || O <- Optss])) =:= 1],
    Diff = Keys -- Same,
    Opts = [{len, Len}, {slots, Len}] ++ [KV || KV = {K, _} <- hd(Optss), lists:member(K, Same)]
	++ [{elems, [[KV || KV = {K, _} <- O, lists:member(K, Diff)] || O <- Optss]}
	    || Diff =/= []],
    N = binary_to_list(Name),
    S1 = decl(Kind, N, Opts, S0),
    S2 = case Kind of
	     constant ->
		 Vs = [case element(2, lists:keyfind(init, 1, O)) of
			   F when is_float(F) -> {f, float_bits(F)};
			   V -> V
		       end || O <- Optss],
		 S1#{consts := (maps:get(consts, S1))#{N => Vs}};
	     _ -> S1
	 end,
    Ix = maps:get(nd, S0),
    S2#{nd := Ix + Len, arrays := (maps:get(arrays, S2))#{N => Len}};
item({variable, _, {'WORD', _, N}, scalar, Res, Opts, X}, S0) ->
    {Size, Type} = size_type(Res, Opts),
    Dir = proplists:get_value(dir, Opts, none),
    {V, S} = case proplists:get_value(bind, Opts) of
		 undefined -> init_value(X, Type, S0);
		 %% bound to bits of a buffer: that is where it lives
		 {{'WORD', _, B}, {range, _, A, Z}} ->
		     {ok, {Bx0, buffer, _}} = find(B, S0),
		     Bx = raw(Bx0),
		     {{bind, [{id, Bx}, {endian, proplists:get_value(endian, Opts, native)},
			      {bit, fold(A, S0)},
			      {len, fold(Z, S0) - fold(A, S0) + 1}]}, S0};
		 Bd -> unsupported({bind, Bd})
	     end,
    decl(variable, N, var_opts(Size, Dir, Type, V), S);
item({param, _, {'WORD', _, N}, Res, Opts, X}, S0) ->
    {Size, Type} = size_type(Res, Opts),
    {V, S} = init_value(X, Type, S0),
    decl(param, N, [{size, Size}, {type, Type}, {init, V}], S);
item({annotate, _, _, _, _}, S) -> S;
%% `> X = 5' is run when the program is loaded, not put in it; what it does
%% to the image is the strings it names.
item({immediate, X}, S) -> literals(X, S);
item({local, Ln, {'WORD', _, N}, Res, Opts, X}, S0) ->
    case proplists:get_value(dir, Opts) of
	undefined -> local(Ln, N, Res, Opts, X, S0);
	Dir -> member_local(Ln, N, Res, Opts, Dir, X, S0)
    end;
item({buffer, _, {'WORD', _, N}, Res, Opts, Trs}, S0) ->
    Nb = case Res of
	     default -> 8;
	     {'INT', _, B} -> int(B)
	 end,
    Dir = proplists:get_value(dir, Opts, none),
    [unsupported({buffer, O}) || O <- Opts, element(1, O) =/= dir],
    {T, Consts, Fresh} = transport(Trs, S0),
    Bx = maps:get(nd, S0),
    S1 = decl(buffer, N, [{dir, Dir}, {nbytes, Nb}, {transport, T}, {id, 0}], S0),
    {Id, S2} = case {Consts, Fresh} of
		   {[], _} -> {0, S1};
		   {[V], false} ->
		       case lookup_const(V, S1) of
			   none -> anon_const(V, S1);
			   Cx -> {Cx, S1}
		       end;
		   {Vs, true} ->
		       {Cxs, Si} = lists:mapfoldl(fun anon_const/2, S1, Vs),
		       {hd(Cxs), Si}
	       end,
    set_decl(Bx, [{dir, Dir}, {nbytes, Nb}, {transport, T}, {id, Id}], S2);
item({field, _, {'WORD', _, N}, Res, Opts, {'WORD', _, B}, Pos}, S) ->
    {ok, {Bx0, buffer, _}} = find(B, S),
    Bx = raw(Bx0),
    {Bit, Len0} = case Pos of
		      {range, _, A, Z} -> {fold(A, S), fold(Z, S) - fold(A, S) + 1};
		      X -> {fold(X, S), 1}
		  end,
    {Size, Len} = case Res of
		      default -> {1, Len0};
		      {'INT', _, W} when Len0 =:= 1 -> {int(W), int(W)};
		      {'INT', _, W} -> {int(W), Len0}
		  end,
    [unsupported({field, O}) || O <- Opts, element(1, O) =/= type,
				 element(1, O) =/= endian],
    decl(field, N, [{size, Size}, {dir, buf_dir(Bx, S)},
		    {type, proplists:get_value(type, Opts, unsigned)}, {id, Bx},
		    {endian, proplists:get_value(endian, Opts, native)},
		    {bit, Bit}, {len, Len}], S);
item({constant, _, {'WORD', _, N}, scalar, Res, Opts, X}, S0) ->
    {Size, Type} = size_type(Res, Opts),
    {V, S = #{consts := Cs}} = init_value(X, Type, S0),
    decl(constant, N, [{size, Size}, {type, Type}, {init, V}],
	 S#{consts := Cs#{N => case Type of
				   float -> {f, float_bits(V)};
				   string -> element(1, intern(binary_to_list(V), S));
				   _ -> V
			       end}});
item({digital, _, {'WORD', _, N}, scalar, default, Opts, [Pin]}, S) ->
    {Port, P} = pin(Pin, S),
    Has = fun(O) -> b2i(lists:member(O, Opts)) end,
    decl(digital, N, [{size, 1}, {dir, proplists:get_value(dir, Opts, in)},
		      {pin, P}, {port, Port},
		      {soft, b2i(proplists:get_value(trig, Opts) =:= soft)},
		      {irq, irq(proplists:get_value(trig, Opts))},
		      {pullup, b2i(proplists:get_value(pull, Opts) =:= pullup)},
		      {pulldown, b2i(proplists:get_value(pull, Opts) =:= pulldown)},
		      {invert, Has(invert)}], S);
item({analog, _, {'WORD', _, N}, scalar, Res, Opts, [Pin]}, S) ->
    {Port, P} = pin(Pin, S),
    Size = case Res of
	       default -> 10;
	       {'INT', _, W} -> int(W)
	   end,
    [unsupported({analog, O}) || O <- Opts, not is_tuple(O) orelse
				     not lists:member(element(1, O), [type, dir]),
				 O =/= pwm],
    decl(analog, N, [{size, Size},
		     {type, proplists:get_value(type, Opts, unsigned)},
		     {dir, proplists:get_value(dir, Opts, in)}, {pin, P},
		     {port, Port}, {soft, 0}, {irq, 0},
		     {pwm, b2i(lists:member(pwm, Opts))},
		     {endian, native}], S);
item({states, _, Ws}, S) ->
    states([N || {'WORD', _, N} <- Ws], S);
%% An imported file ends in an `end' declaration: a marker naming a file the
%% walk is already in is the importer picking up again.
item({source, _, F}, S = #{files := Fs}) ->
    case lists:member(F, Fs) of
	true ->
	    {Done, Rest} = lists:splitwith(fun(X) -> X =/= F end, Fs),
	    S1 = lists:foldl(fun(_, Si) -> decl('end', [], none, Si) end, S, Done),
	    S1#{files := Rest};
	false -> S#{files := [F | Fs]}
    end;
item({define, _, {'WORD', _, N}, X}, S = #{defs := Ds}) ->
    S#{defs := Ds#{N => fold(X, S)}};
%% A period that is a #param: the declaration takes its value now, and an
%% INIT rule `T.period = P' the value it has when the program starts.
item({timer, L, {'WORD', _, N}, P = {field, _, {'WORD', _, Pn}}, X}, S0) ->
    case find(Pn, S0) of
	{ok, {Px, param, _}} ->
	    {decl, _, param, _, Po} = lists:keyfind(raw(Px), 2, maps:get(decls, S0)),
	    S1 = timer_decl(N, proplists:get_value(init, Po), X, S0),
	    S3 = case S1 of
		     #{tinit := none} ->
			 {R, S2} = load(leaf("State", S1), line(S1)),
			 {J, S2a} = emit('INSTATE', [R, 0, {nxt, 0}], S2),
			 (free(R, S2a))#{tinit := J};
		     _ -> line(S1)
		 end,
	    Set = {'=', L, {field, L, {part, L, {'WORD', L, N}, {'WORD', L, "period"}}}, P},
	    rule([Set], undefined, [], S3);
	_ -> timer_decl(N, fold(P, S0), X, S0)
    end;
item({timer, _, {'WORD', _, N}, P, X}, S) ->
    timer_decl(N, fold(P, S), X, S);
item({disable, _, _}, S) -> S;          % the image does not keep it
item({enable, _, _}, S) -> S;
item({rule, _, As0, G}, S0) ->
    S1 = literals([As0, G], S0),
    {As, S2} = lists:mapfoldl(fun packing/2, S1, As0),
    Imp = case lists:append(As) =:= As0 of
	      true -> implicit(S2);
	      false -> []                     % a pack is never implicit
	  end,
    rule(lists:append(As), G, Imp, line(S2));
item({module, _, {'WORD', _, M}}, S0 = #{nd := Mx, menv := none}) ->
    S1 = decl(module, M, [{n, 0}, {ent, 0}], line(S0)),
    S2 = decl(variable, "State", var_opts(32, none, integer, 0),
	      S1#{menv := #{}}),
    {J, S3} = emit('ENTER', [{mem, Mx}, {n, 0}], S2),
    push_block({module, J, Mx, M}, S3);
%% `#route Src Dst' reads as an object line to the parser; it is a
%% declaration, with an empty name of its own.
item({object, _, {'WORD', _, "route"}, {'WORD', _, Src}, [{field, _, {'WORD', _, Dst}}]},
     S0 = #{nd := N}) ->
    {ok, {Sx, buffer, _}} = find(Src, S0),
    {ok, {Dx, buffer, _}} = find(Dst, S0),
    S1 = name("", S0),
    S1#{decls := [{decl, N, route, <<>>, [{src, raw(Sx)}, {dst, raw(Dx)}]}
		  | maps:get(decls, S1)], nd := N + 1};
item({object, _, {'WORD', _, M}, {'WORD', _, O}, Bs}, S0 = #{nobj := No0}) ->
    Mem = case maps:find(M, maps:get(mods, S0)) of
	      {ok, Mm} -> Mm;
	      error -> unsupported({module_not_compiled, M})
	  end,
    No = No0 + 1,
    Ox = maps:get(nd, S0),
    #{mx := Mx} = Mem,
    S1 = decl_obj(M, O, [{mx, Mx}, {m, No}], line(S0#{nobj := No})),
    Tgt = {No, maps:get(env, Mem)},
    Local = fun({_, _, {field, _, {'WORD', _, F}}, _}) ->
		    case maps:find(F, maps:get(env, Mem)) of
			{ok, {_, local, _}} -> true;
			_ -> false
		    end;
	       (_) -> false
	    end,
    %% a #local's binding is its formula: a plain store every cycle, `=' or
    %% `<-' alike, with the `<-' rules
    In = [case Local(B) of
	      true -> setelement(1, B, '=');
	      false -> B
	  end || B <- Bs, element(1, B) =:= '<-' orelse Local(B)],
    Statics = [B || B = {'=', _, _, _} <- Bs, not Local(B)],
    length(In) + length(Statics) =:= length(Bs) orelse unsupported(binding),
    S2 = lists:foldl(fun(B, Si) -> rule([B], undefined, [], Si#{tgt := Tgt}) end,
		     S1, In),
    S2a = lists:foldl(fun(L, Si) -> local_default(L, No, Si) end, S2,
		      unbound_locals(Mem, Bs, S2)),
    S3 = case Statics of
	     [] -> S2a;
	     _ -> obj_init(Statics, Tgt, S2a)
	 end,
    {_, S4} = emit('NEW', [{obj, Ox}], S3#{tgt := none}),
    S4;
item({in, _, Ws}, S0) ->
    {R, S1} = load(leaf("State", S0), line(S0)),
    Nos = [case state_no(N, S1) of
	       none -> unsupported({state, N});
	       No -> No
	   end || {'WORD', _, N} <- Ws],
    {Ors, Last} = lists:split(length(Nos) - 1, Nos),
    {Js, S2} = lists:mapfoldl(fun(No, Si) ->
				      emit('NINSTATE', [R, No, {nxt, 0}], Si)
			      end, S1, Ors),
    {J, S3} = emit('INSTATE', [R, hd(Last), {nxt, 0}], S2),
    S4 = lists:foldl(fun(Jo, Si) -> patch_nxt(Jo, Si) end, S3, Js),
    push_block({in, J}, free(R, S4));
item({'when', _, X}, S0) ->
    {E, S1} = expr(X, line(S0)),
    {R, S2} = load(E, S1),
    {J, S3} = emit('NINSTATE', [R, 0, {nxt, 0}], S2),
    push_block({'when', J}, free(R, S3));
item({'else', _}, S0 = #{blocks := [{'when', Jw} | Bs]}) ->
    {T, S1} = alloc(line(S0)),
    {_, S2} = emit('LI', [T, {imm, 1}], S1),
    {J, S3} = emit('INSTATE', [T, 0, {nxt, 0}], S2),
    S4 = patch_nxt(Jw, free(T, S3)),
    S4#{blocks := [{'else', J} | Bs]};
item({'end', _}, S0 = #{blocks := [{module, J, Mx, M} | Bs], menv := Me}) ->
    S = S0#{blocks := Bs},
    N = maps:get(pc, S) - J - 1,
    {_, S1} = emit('LEAVE', [{mem, Mx}, {n, N}], S),
    S2 = set_instr(J, 'ENTER', [{mem, Mx}, {n, N}], S1),
    S3 = set_decl(Mx, [{n, maps:get(nd, S2) - Mx - 1}, {ent, J}], S2),
    S4 = decl('end', [], none, S3),
    S4#{menv := none, mods := (maps:get(mods, S4))#{M => #{mx => Mx, env => Me}}};
item({'end', _}, S = #{blocks := [{_, J} | Bs]}) ->
    patch_nxt(J, S#{blocks := Bs});
item(X, _S) ->
    unsupported(element(1, X)).

%% A width makes a value unsigned unless it says integer; none is signed 32.
size_type(default, Opts) ->
    case proplists:get_value(type, Opts) of
	float -> {32, float};
	undefined -> {32, integer};
	T -> {32, T}
    end;
size_type({'INT', _, W}, Opts) ->
    {int(W), proplists:get_value(type, Opts, unsigned)};
size_type(Res, _Opts) ->
    unsupported({width, Res}).

%% A string's text goes in the table before the name it is the value of.
init_value({'STR', _, T}, string, S) ->
    {_, S1} = intern(T, S),
    {list_to_binary(unescape(T)), S1};
init_value(undefined, string, S) -> {[], S};
init_value(X, Type, S) -> {init(X, Type, S), S}.

init(undefined, float, _S) -> 0.0;
init(undefined, _T, _S) -> 0;
init(X, float, S) ->
    case fold(X, S) of
	{f, B} -> bits_float(B);
	I when is_integer(I) -> bits_float(fix_to_ieee(s32(I bsl 16)))
    end;
init(X, _T, S) ->
    case fold(X, S) of
	V when is_integer(V) -> V;
	{f, B} -> fix_trunc(fix_from_ieee(B));
	V -> unsupported({init, V})
    end.

pin({pin, _, X}, S) -> {0, fold(X, S)};
pin({port_pin, _, Po, Pi}, S) -> {fold(Po, S), fold(Pi, S)};
pin(P, _S) -> unsupported({pin, P}).

%%% ---------------------------------------------------------------- rules

%% Element K of an array declaration, as the scalar it is: its decl term.
element_decl(D, K, S) ->
    E0 = setelement(4, D, scalar),
    E1 = case E0 of
	     {variable, L, W, scalar, R, O, {array, _, Xs}} ->
		 {variable, L, W, scalar, R, O, lists:nth(K + 1, Xs)};
	     {constant, L, W, scalar, R, O, {array, _, Xs}} ->
		 {constant, L, W, scalar, R, O, lists:nth(K + 1, Xs)};
	     {Io, L, W, scalar, R, O, [Pin]} when Io =:= analog; Io =:= digital ->
		 {Io, L, W, scalar, R, O, [pin_k(Pin, K)]};
	     _ -> E0
	 end,
    #{decls := [Dk | _]} = item(E1, S),
    Dk.

pin_k({port_pin, L, Po, {range, _, {'INT', _, A}, _}}, K) ->
    {port_pin, L, Po, {'INT', L, integer_to_list(int(A) + K)}};
pin_k({pin, L, {range, _, {'INT', _, A}, _}}, K) ->
    {pin, L, {'INT', L, integer_to_list(int(A) + K)}};
pin_k(P, _) -> unsupported({array_pin, P}).

%% `Buf <<= A B:2 ...' is the fields stored to views of Buf, bit after bit;
%% `Buf >>= A:3 ...' the views read back. A field's width is its own unless
%% it says. The views are made as the line is read, before its code.
packing({Dir, {field, _, {'WORD', _, B}}, Fs}, S0) when Dir =:= pack; Dir =:= unpack ->
    {ok, {Bx0, buffer, _}} = find(B, S0),
    Bx = raw(Bx0),
    {Parts, {_, S}} =
	lists:mapfoldl(
	  fun({X, W0}, {Bit, Si}) ->
		  W = case W0 of
			  default -> width_of(X, Si);
			  _ -> fold(W0, Si)
		      end,
		  {Vx, Sj} = view(Bx, Bit, W, Si),
		  P = case Dir of
			  pack -> {'=', 0, {view, Vx}, X};
			  unpack -> {'=', 0, X, {view, Vx}}
		      end,
		  {P, {Bit + W, Sj}}
	  end, {0, S0}, Fs),
    {Parts, S};
packing(A, S) -> {[A], S}.

width_of({field, _, {'WORD', _, N}}, S = #{decls := Ds}) ->
    {ok, {Ix, _, _}} = find(N, S),
    {decl, _, _, _, Opts} = lists:keyfind(raw(Ix), 2, Ds),
    proplists:get_value(size, Opts);
width_of(X, _S) -> unsupported({pack_width, element(1, X)}).

%% A #local: a declaration with no name -- the name lives in the compiler
%% only -- and the rule that works it out, there and then.
local(Ln, N, Res, Opts, X, S0 = #{decls := Ds, nd := Ix, env := Env, menv := Me}) ->
    [unsupported({local, O}) || O <- Opts, element(1, O) =/= type],
    {Size, Type} = size_type(Res, Opts),
    E = {Ix, local, Type},
    S1 = case Me of
	     none -> S0#{env := Env#{N => E}};
	     _ -> S0#{menv := Me#{N => setelement(1, E, ?CURRENT bor Ix)}}
	 end,
    Zero = case Type of float -> 0.0; _ -> 0 end,
    S2 = S1#{decls := [{decl, Ix, local, [], var_opts(Size, none, Type, Zero)} | Ds],
	     nd := Ix + 1},
    item({rule, Ln, [{'=', Ln, {field, Ln, {'WORD', Ln, N}}, X}], undefined}, S2).

%% `#local X in' (out, inout): a member an object line binds -- named, and
%% its formula, when it has one, a rule as any local's.
%% `in = 1' is no formula: it is the value an object that binds nothing
%% gets, and the member is inout.
member_local(_Ln, N, Res, Opts, in, X, S0) when X =/= undefined ->
    {Size, Type} = size_type(Res, Opts),
    decl(local, N, var_opts(Size, inout, Type, init(X, Type, S0)), S0);
member_local(Ln, N, Res, Opts, Dir, X, S0) ->
    {Size, Type} = size_type(Res, Opts),
    S1 = decl(local, N, var_opts(Size, Dir, Type, 0), S0),
    case X of
	undefined -> S1;
	_ -> item({rule, Ln, [{'=', Ln, {field, Ln, {'WORD', Ln, N}}, X}], undefined}, S1)
    end.

timer_decl(N, Period, X, S) ->
    is_integer(Period) orelse unsupported(timer_period),
    S1 = decl(timer, N, [{size, 32}, {type, timer}, {period, Period},
			 {init, init(X, integer, S)}, {slots, 2}], S),
    S1#{nd := maps:get(nd, S1) + 1}.          % the second slot: no name

%% The module's `#local X in = V' this line does not bind: {Index, V}, in
%% declaration order.
unbound_locals(#{env := Env}, Bs, #{decls := Ds}) ->
    Bound = [F || {_, _, {field, _, {'WORD', _, F}}, _} <- Bs],
    lists:sort([{raw(Ix), proplists:get_value(init, O)}
		|| {F, {Ix, local, _}} <- maps:to_list(Env), not lists:member(F, Bound),
		   {decl, _, local, _, O} <- [lists:keyfind(raw(Ix), 2, Ds)],
		   proplists:get_value(dir, O) =:= inout]).

%% Written where a binding would be, so the body reads it the same way.
local_default({Ix, V}, No, S0) ->
    {C, S1} = alloc(S0),
    {_, S2} = emit('LI', [C, {imm, -1}], S1),
    {J, S3} = emit('RULE', [C, 0], S2),
    {R, S4} = alloc(free(C, S3)),
    {_, S5} = load_int(R, V, S4),
    {_, S6} = emit('ST', [R, {mem, ?CURRENT bor Ix}], seto(No, S5)),
    S7 = patch_nxt(J, S6),
    {_, S8} = emit('NEXT', [R], S7),
    free(R, S8).

%% An object's starting values: one rule and then `State = NORMAL', both in
%% its INIT.
obj_init(Statics, Tgt = {No, Env}, S0) ->
    {Six, _, _} = maps:get("State", Env),
    {R, S1} = load({ovar, No, Six, integer}, S0),
    {J, S2} = emit('INSTATE', [R, 0, {nxt, 0}], S1),
    S3 = rule(Statics, undefined, [], (free(R, S2))#{tgt := Tgt}),
    Normal = {'=', 0, {field, 0, {'WORD', 0, "State"}}, {'INT', 0, "1"}},
    S4 = rule([Normal], undefined, [], S3),
    patch_nxt(J, S4).

%% A transport and what its constants hold: {Transport, Values, Fresh} --
%% fresh when they come as a pair, which never reuses one already there.
transport([], _S) -> {none, [], false};
transport([{can, X}], S) -> {can, [fold(X, S)], false};
transport([{repl}], _S) -> {repl, [0], false};
transport([{console}], _S) -> {console, [0], false};
transport([{uart, U, 9600}], S) -> {uart, [fold(U, S) bsl 28], false};
transport([{uart, U, B}], S) -> {uart, [(fold(U, S) bsl 28) bor fold(B, S)], false};
transport([{sms, {'WORD', _, O}}], S) ->
    {ok, {Ox, _, _}} = find(O, S),
    {sms, [Ox], false};
transport([{i2c, B, A, R}], S) ->
    {i2c, [(fold(B, S) bsl 16) bor (fold(A, S) bsl 8) bor fold(R, S)], false};
transport([{spi, B, {Po, Pi}, C}], S) ->
    {spi, [(fold(B, S) bsl 24) bor (fold(Po, S) bsl 20) bor (fold(Pi, S) bsl 16)
	   bor fold(C, S)], false};
transport([{sms, {'WORD', _, O}, {'WORD', _, P}}], S) ->
    {ok, {Ox, _, _}} = find(O, S),
    {ok, {Px, _, _}} = find(P, S),
    {sms, [Ox bor ((Px + 1) bsl 16)], false};
transport([{udp, P, udefined}], S) -> {udp, [0, fold(P, S)], true};
transport([{tcp, P, udefined}], S) -> {tcp, [0, fold(P, S)], true};
transport([{onewire, {Po, Pi}, {'WORD', _, R}}], S) ->
    {ok, {Rx, _, _}} = find(R, S),
    {onewire, [(fold(Po, S) bsl 8) bor fold(Pi, S), Rx], true};
transport(T, _S) -> unsupported({transport, element(1, hd(T))}).

%% An integer constant with this value, named or not: its index, or none.
lookup_const(V, #{decls := Ds}) ->
    case [I || {decl, I, K, _, Opts} <- lists:reverse(Ds),
	       K =:= constant orelse K =:= param,
	       proplists:get_value(type, Opts) =:= integer,
	       proplists:get_value(init, Opts) =:= V] of
	[I | _] -> I;
	[] -> none
    end.

%% A constant with no name: a transport's address.
anon_const(V, S = #{decls := Ds, nd := N}) ->
    {N, S#{decls := [{decl, N, constant, [], [{size, 32}, {type, integer},
					       {init, V}]} | Ds], nd := N + 1}}.

buf_dir(Bx, #{decls := Ds}) ->
    {decl, Bx, buffer, _, Opts} = lists:keyfind(Bx, 2, Ds),
    proplists:get_value(dir, Opts).

%% The view of bits Bit..Bit+Len-1 of buffer Bx: the one there is, or a new
%% one, nameless but for an empty name of its own.
view(Bx, Bit, Len, S = #{decls := Ds}) ->
    Opts = [{size, Len}, {dir, buf_dir(Bx, S)}, {type, unsigned}, {id, Bx},
	    {endian, native}, {bit, Bit}, {len, Len}],
    case [I || {decl, I, view, _, O} <- Ds, O =:= Opts] of
	[I | _] -> {I, S};
	[] ->
	    N = maps:get(nd, S),
	    S1 = name("", S),
	    {N, S1#{decls := [{decl, N, view, <<>>, Opts} | maps:get(decls, S1)],
		    nd := N + 1}}
    end.

%% Buf[k] or Buf[a..b], k and a..b constant: the view's index.
buf_view(B, Ix, S) ->
    {ok, {Bx0, buffer, _}} = find(B, S),
    Bx = raw(Bx0),
    {A, Z} = case Ix of
		 {range, _, A0, Z0} -> {fold(A0, S), fold(Z0, S)};
		 X -> K = fold(X, S), {K, K}
	     end,
    (is_integer(A) andalso is_integer(Z)) orelse unsupported(runtime_index),
    view(Bx, A * 8, (Z - A + 1) * 8, S).

set_instr(J, Op, Args, S = #{code := C}) ->
    S#{code := lists:keyreplace(J, 2, C, {instr, J, Op, Args})}.

set_decl(Ix, Opts, S = #{decls := Ds}) ->
    {decl, Ix, K, T, _} = lists:keyfind(Ix, 2, Ds),
    S#{decls := lists:keyreplace(Ix, 2, Ds, {decl, Ix, K, T, Opts})}.

%% A new statement: every register free, nothing cached.
line(S) ->
    S#{free := [list_to_atom("r" ++ integer_to_list(R)) || R <- lists:seq(0, 15)],
       rmap := #{}}.

push_block(B, S = #{blocks := Bs}) -> S#{blocks := [B | Bs]}.

%% A rule outside every #in (and module) runs in INIT and NORMAL only, and
%% says so: implicit.
implicit(#{blocks := Bs}) ->
    case lists:keymember(in, 1, Bs) orelse lists:keymember(module, 1, Bs) of
	true -> [];
	false -> [implicit]
    end.

rule(As, G, Imp, S0) ->
    {Cnd, S1} = case rimp_vars(As, S0) of
		    [] -> guard(G, S0);
		    Vs -> changed(Vs, G, S0)
		end,
    {J, S2} = emit('RULE', [Cnd, 0 | Imp], S1),
    S3 = free(Cnd, S2),
    {Dst, S4} = lists:foldl(fun part/2, {none, S3}, As),
    {Dst1, S5} = case Dst of
		     none -> R = alloc(S4), load_int(R, 0, S4);
		     _ -> {Dst, S4}
		 end,
    S6 = patch_nxt(J, S5),
    {_, S7} = emit('NEXT', [Dst1], S6),
    free(Dst1, S7).

%% A `<-' rule runs when what it reads changed: CHG on each variable and
%% #param its right side names, in the order they first appear, and the guard
%% ANDed in.
changed(Vs, G, S0) ->
    {C, S1} = alloc(S0),
    {_, S2} = emit('LI', [C, {imm, 0}], S1),
    S3 = lists:foldl(fun({Obj, Ix}, Si) ->
			     element(2, emit('CHG', [C, {mem, Ix}], seto(Obj, Si)))
		     end, S2, Vs),
    case G of
	undefined -> {C, S3};
	_ ->
	    {E, S4} = expr(G, S3),
	    {Rc, S5} = load(E, S4),
	    {_, S6} = emit('AND', [C, Rc, C], S5),
	    {C, free(Rc, S6)}
    end.

rimp_vars(As, S) ->
    Ns = lists:append([names(X) || {'<-', _, _, X} <- As]),
    uniq([X || N <- Ns, X <- read_by(N, S)]).

%% A variable or #param the right side reads, a name or an object's member.
read_by({O, F}, S) ->
    [{No, Ix} || {No, {Ix, K, _}} <- [member(O, F, S)],
		 K =:= variable orelse K =:= param];
read_by(N, S) ->
    [{none, Ix} || {ok, {Ix, K, _}} <- [find(N, S)],
		   K =:= variable orelse K =:= param].

names({field, _, {'WORD', _, N}}) -> [N];
names({field, _, {fld, _, {'WORD', _, O}, {'WORD', _, F}}}) -> [{O, F}];
names({field, _, _}) -> unsupported(rimp_member);
names(T) when is_tuple(T) -> lists:append([names(E) || E <- tuple_to_list(T)]);
names(L) when is_list(L) -> lists:append([names(E) || E <- L]);
names(_) -> [].

uniq([X | T]) -> [X | uniq([Y || Y <- T, Y =/= X])];
uniq([]) -> [].

guard(G, S) when G =:= undefined; G =:= true ->
    {R, S1} = alloc(S),
    {_, S2} = emit('LI', [R, {imm, -1}], S1),
    {R, S2};
guard(G, S) ->
    {E, S1} = expr(G, S),
    {Rc, S2} = load(E, S1),
    {Cnd, S3} = alloc(S2),
    {_, S4} = emit('MOV', [Cnd, Rc], S3),
    {Cnd, free(Rc, S4)}.

%% One assignment of the body; only the last value is kept, for NEXT.
part({Op, _, T, X}, {Dst0, S0}) when Op =:= '='; Op =:= '<-' ->
    S1 = case Dst0 of
	     none -> S0;
	     _ -> free(Dst0, S0)
	 end,
    {E0, S1a} = expr(X, S1),
    {{Obj, Ix, Kind, Lt}, S2a} = target(T, S1a),
    {E, S2} = coerce(Lt, E0, S2a),
    case {Op, E} of
	{_, _} when element(1, Kind) =:= part ->
	    {R, S3} = load(E, S2),
	    {_, S4} = emit('STP', [R, {mem, Ix}, element(2, Kind)], seto(Obj, S3)),
	    {R, S4};
	{'=', {imm, Vt, V}} when Vt =/= float, V >= -32, V =< 31 -> sti(V, Ix, Obj, S2);
	{'=', {const, _, Vt, V}} when Vt =/= float, V >= -32, V =< 31 -> sti(V, Ix, Obj, S2);
	_ ->
	    {R, S3} = load(E, S2),
	    St = case Op of '=' -> 'ST'; '<-' -> 'STIMP' end,
	    S4 = seto(Obj, S3),
	    {_, S5} = emit(St, [R, {mem, Ix}], S4),
	    {R, unseto(Obj, S5)}
    end;
%% A call for what it does: println("ON").
part(X = {call, _, _, _}, {Dst0, S0}) ->
    S1 = case Dst0 of
	     none -> S0;
	     _ -> free(Dst0, S0)
	 end,
    {E, S2} = expr(X, S1),
    load(E, S2);
part(A, _) ->
    unsupported({assign, element(1, A)}).

sti(V, Ix, Obj, S) ->
    {R, S1} = alloc(S),
    {_, S2} = emit('STI', [R, {imm, V}, {mem, Ix}], seto(Obj, S1)),
    {R, unseto(Obj, S2)}.

%% The SETOX consumed the index: its register goes back.
unseto({setox, R, _}, S) -> free(R, S);
unseto(_, S) -> S.

%% A named object's member: the object is set for the one instruction after.
%% An array element at a run-time index: the index, likewise.
seto(none, S) -> S;
seto({setox, R, Sx}, S) ->
    {_, S1} = emit('SETOX', [R | Sx], S), S1;
seto(No, S) -> {_, S1} = emit('SETO', [{obj, No}], S), S1.

%% Where a value goes: {Object | none, Index, Kind, Vt}. On an object's line
%% a bare name is the object's member.
target({view, Vx}, S) -> {{none, Vx, view, unsigned}, S};
target({field, _, {'WORD', _, T}}, S = #{tgt := {No, Env}}) ->
    case maps:find(T, Env) of
	{ok, {Ix, K, Vt}} when K =/= constant -> {{No, Ix, K, Vt}, S};
	_ ->
	    {X, S1} = target({field, 0, {'WORD', 0, T}}, S#{tgt := none}),
	    {X, S1#{tgt := {No, Env}}}
    end;
target({field, _, {'WORD', _, T}}, S) ->
    case find(T, S) of
	{ok, {Ix, K, Vt}} when K =/= constant, K =/= object, K =/= module ->
	    {{none, Ix, K, Vt}, S};
	_ -> unsupported({target, T})
    end;
%% `F300.dlc': a part the parser did not know for one, when F300 is no object.
target({field, L, {fld, _, {'WORD', _, O}, {'WORD', _, F}}}, S) ->
    case is_object(O, S) of
	false -> target({field, L, {part, L, {'WORD', L, O}, {'WORD', L, F}}}, S);
	true ->
	    {No, {Ix, K, Vt}} = member(O, F, S),
	    K =/= constant orelse unsupported({target, F}),
	    {{No, Ix, K, Vt}, S}
    end;
target({field, _, {part, _, Of, {'WORD', _, P}}}, S) ->
    {Obj, Ix} = part_of(Of, S),
    {{Obj, Ix, {part, part(P)}, integer}, S};
target({field, _, {index, _, {'WORD', _, B}, Ix}}, S) ->
    case find(B, S) of
	{ok, {_, constant, _}} -> unsupported({target, B});
	{ok, {_, K, _}} ->
	    E = case K of
		    buffer -> buf_ref(B, Ix, S);
		    _ -> array_elem(B, Ix, S)
		end,
	    case E of
		{{var, El, Vt}, S1} -> {{none, El, variable, Vt}, S1};
		{{elem, Mem, Vt, R, Sx}, S1} -> {{{setox, R, Sx}, Mem, variable, Vt}, S1}
	    end
    end;
target(T, _S) -> unsupported({target, element(1, element(3, T))}).

%% Whose part: a name of this object's (on its line), a named object's
%% member, or a name. {Object | none, Index}.
part_of({'WORD', _, N}, S = #{tgt := {No, Env}}) ->
    case maps:find(N, Env) of
	{ok, {Ix, _, _}} -> {No, Ix};
	error -> part_of({'WORD', 0, N}, S#{tgt := none})
    end;
part_of({'WORD', _, N}, S) ->
    case find(N, S) of
	{ok, {Ix, _, _}} -> {none, Ix};
	error -> unsupported({name, N})
    end;
part_of({fld, _, {'WORD', _, O}, {'WORD', _, F}}, S) ->
    {No, {Ix, _, _}} = member(O, F, S),
    {No, Ix}.

is_object(O, S) ->
    case find(O, S) of
	{ok, {_, object, _}} -> true;
	_ -> false
    end.

find(N, #{menv := Me, env := Env}) ->
    case Me =/= none andalso maps:find(N, Me) of
	{ok, _} = F -> F;
	_ -> maps:find(N, Env)
    end.

member(O, F, S = #{mods := Mods}) ->
    case find(O, S) of
	{ok, {_, object, {M, No}}} ->
	    case maps:find(F, maps:get(env, maps:get(M, Mods))) of
		{ok, X} -> {No, X};
		error -> unsupported({member, F})
	    end;
	_ -> unsupported({object, O})
    end.

%% The value as the target's type: unsigned is integer by now, and a float
%% and an integer are converted into each other.
coerce(Lt, E0, S) ->
    E = case vt_of(E0) of
	    unsigned -> set_vt(integer, E0);
	    _ -> E0
	end,
    case {Lt, vt_of(E)} of
	{float, integer} -> to_float(E, S);
	{Int, float} when Int =:= integer; Int =:= unsigned -> to_int(E, S);
	_ -> {E, S}
    end.

%% The jump at J lands here: RULE's nxt, or an (N)INSTATE's {nxt, N}.
patch_nxt(J, S = #{code := C, pc := Pc}) ->
    {value, {instr, J, Op, Args}, C1} = lists:keytake(J, 2, C),
    Args1 = case {Op, Args} of
		{'RULE', [Cnd, _ | F]} -> [Cnd, Pc - J | F];
		{_, [R, No, {nxt, _} | F]} -> [R, No, {nxt, Pc - J} | F]
	    end,
    S#{code := [{instr, J, Op, Args1} | C1]}.

%%% ---------------------------------------------------------- expressions
%%%
%%% An entry: {imm, Vt, V} a literal or a fold; {var, Ix, Vt} a declaration
%%% not loaded yet; {const, Ix, Vt, V} a #constant, loaded as a literal but a
%%% declaration all the same; {reg, R, Vt} a value in R.

expr({'INT', _, T}, S) -> {{imm, integer, s32(int(T))}, S};
expr({view, Vx}, S) -> {{var, Vx, unsigned}, S};
expr({'FLT', _, T}, S) -> {{imm, float, fltbits(T)}, S};   % its IEEE bits
expr({field, _, {'WORD', _, N}}, S) -> {leaf(N, S), S};
expr({field, _, {index, _, {'WORD', _, B}, Ix}}, S) ->
    case find(B, S) of
	{ok, {_, buffer, _}} -> buf_ref(B, Ix, S);
	_ -> array_elem(B, Ix, S)
    end;
%% A part is read when it is needed, like any leaf, and never cached.
expr({field, _, {part, _, Of, {'WORD', _, P}}}, S) ->
    {Obj, Ix} = part_of(Of, S),
    {{part, Ix, part(P), Obj}, S};
expr({field, L, {fld, L2, {'WORD', L3, O}, {'WORD', L4, F}}}, S) ->
    case is_object(O, S) of
	false -> expr({field, L, {part, L2, {'WORD', L3, O}, {'WORD', L4, F}}}, S);
	true -> member_expr(O, F, S)
    end;
expr({'-', _, X}, S) -> unop('NEG', X, S);
expr({'!', _, X}, S) -> unop('NOT', X, S);
expr({'~', _, X}, S) -> unop('BNOT', X, S);
%% timeout(T) is an instruction, not a call.
expr({call, _, {'WORD', _, "timeout"}, [{field, _, {'WORD', _, T}}]}, S) ->
    {Ix, timer, _} = lookup(T, S),
    {R, S1} = alloc(S),
    {_, S2} = emit('TMO', [R, {mem, Ix}], S1),
    {{reg, R, integer}, S2};
expr({call, _, {'WORD', _, F}, Args}, S0) ->
    {Es, S1} = lists:mapfoldl(fun expr/2, S0, Args),
    {Idx, Pure, Rt0, Ts} = func(F, length(Es)),
    %% one float `number' argument makes every `number' argument float
    Flt = lists:any(fun({number, E}) -> vt_of(E) =:= float; (_) -> false end,
		    lists:zip(Ts, Es)),
    {Args0, S2} = lists:foldl(fun(A, Acc) -> arg(A, Flt, Acc) end, {[], S1},
			      lists:zip3(lists:seq(0, length(Es) - 1), Es, Ts)),
    {Avt, Vals} = lists:unzip(lists:reverse(Args0)),
    %% all literals and nothing done: worked out here -- the CALL stays, its
    %% register holding what is now also a literal
    Folded = Pure andalso lists:all(fun is_imm/1, Es) andalso
	lists:all(fun({float, V}) -> fix_ieee_exact(V); (_) -> true end,
		  lists:zip(Avt, Vals)),
    Rt = case Rt0 of
	     number -> case lists:member(float, Avt) of
			   true -> float;
			   false -> integer
		       end;
	     _ -> Rt0
	 end,
    {R, S3} = alloc(S2),
    {_, S4} = emit('CALL', [R, {idx, Idx}, {usr, 0}, {avt, Avt}], S3),
    case Folded of
	true -> {{regi, R, Rt, call_fold(F, hd(Avt), Vals)}, S4};
	false -> {{reg, R, Rt}, S4}
    end;
expr({'STR', _, T}, S) ->
    {I, S1} = intern(T, S),
    {{imm, string, I}, S1};
expr({Op, _, A, B}, S) when is_atom(Op) ->
    {Ea, S1} = expr(A, S),
    {Eb, S2} = expr(B, S1),
    binop(alu(Op), Ea, Eb, S2);
expr(X, _S) -> unsupported({expr, element(1, X)}).

%% A[k] with k constant is the element's own declaration (a constant's its
%% value); A[i] loads i there and then, and the access waits with it.
array_elem(A, Ix, S) ->
    {ok, {Base, K, Vt}} = find(A, S),
    Len = maps:get(A, maps:get(arrays, S), 1),
    Folded = try fold(Ix, S) catch throw:{unsupported, _} -> runtime end,
    case Folded of
	I when is_integer(I), I >= 0, I < Len, K =:= constant ->
	    {{const, Base + I, Vt, cval(lists:nth(I + 1, maps:get(A, maps:get(consts, S))))}, S};
	I when is_integer(I), I >= 0, I < Len -> {{var, Base + I, Vt}, S};
	I when is_integer(I) -> unsupported(index_range);
	_ ->
	    {E, S1} = expr(Ix, S),
	    {R, S2} = load(E, S1),
	    {{elem, ?CURRENT bor raw(Base), Vt, R, [{len, Len}, {stride, 1}]}, S2}
    end.

%% Buf[i], i known only when it runs: byte 0's view, moved by i bytes.
buf_elem(B, Ix, S) ->
    {ok, {Bx0, buffer, _}} = find(B, S),
    Bx = raw(Bx0),
    {decl, _, buffer, _, Bo} = lists:keyfind(Bx, 2, maps:get(decls, S)),
    {E, S1} = expr(Ix, S),
    {R, S2} = load(E, S1),
    {Vx, S3} = view(Bx, 0, 8, S2),
    {{elem, Vx, unsigned, R, [{len, proplists:get_value(nbytes, Bo)}, {stride, 8}, bits]},
     S3}.

%% Buf[...]: a view, or byte 0's moved at run time.
buf_ref(B, Ix, S) ->
    try buf_view(B, Ix, S) of
	{Vx, S1} -> {{var, Vx, unsigned}, S1}
    catch
	throw:{unsupported, _} -> buf_elem(B, Ix, S)
    end.

member_expr(O, F, S) ->
    case member(O, F, S) of
	{No, {Ix, K, Vt}} when K =:= variable; K =:= param; K =:= digital;
			       K =:= analog; K =:= local -> {{ovar, No, Ix, Vt}, S};
	{_, {_, K, _}} -> unsupported({member_kind, K})
    end.

%% An argument as the function takes it, into its argument register.
%% Its type code is what it was handed as, a `number' made float said so.
arg({J, E0, T}, Flt, {Avt, S}) ->
    {E, Vt, S0} =
	case {T, E0, vt_of(E0)} of
	    {any, _, V} -> {E0, V, S};
	    {number, _, float} -> {E0, float, S};
	    {number, _, integer} when Flt ->
		{Ef, Sf} = to_float(E0, S), {Ef, float, Sf};
	    {number, _, integer} -> {E0, integer, S};
	    {integer, _, integer} -> {E0, integer, S};
	    {integer, _, float} -> {Ei, Si} = to_int(E0, S), {Ei, float, Si};
	    {string, {imm, string, _}, _} -> {E0, string, S};
	    {Kind, {var, Ix, Vt0}, _} when Kind =:= timer; Kind =:= digital;
					   Kind =:= index ->
		%% by INDEX: the declaration's number, a literal
		{{imm, load_vt(Vt0), Ix}, Vt0, S};
	    {_, _, V} -> unsupported({call_arg, T, V})
	end,
    {R, S1} = load(E, S0),
    {_, S2} = emit('ARG', [R, {imm, J}], S1),
    {[{Vt, imm_value(E)} | Avt], free(R, S2)}.

is_imm({imm, _, _}) -> true;
is_imm({regi, _, _, _}) -> true;
is_imm(_) -> false.

imm_value({imm, _, V}) -> V;
imm_value({regi, _, _, V}) -> V;
imm_value(_) -> none.

%% The pure built-ins on literals, as src/csp_rt.c computes them; a float is
%% its Q16.16 bits, which order as integers do.
%% Floats are exact here (only those fold) and worked as Q16.16, which orders
%% as the values do; the answer is IEEE bits again.
call_fold(F, float, Vs) when F =/= "trunc", F =/= "round", F =/= "sign" ->
    fix_to_ieee(call_fold(F, integer, [fix_from_ieee(V) || V <- Vs]));
call_fold("sign", float, [A]) -> call_fold("sign", integer, [fix_from_ieee(A)]);
call_fold("min", _, [A, B]) -> min(A, B);
call_fold("max", _, [A, B]) -> max(A, B);
call_fold("abs", _, [A]) -> s32(abs(A));
call_fold("sign", _, [A]) -> if A > 0 -> 1; A < 0 -> -1; true -> 0 end;
call_fold("clip", _, [X, Lo, Hi]) -> if X < Lo -> Lo; X > Hi -> Hi; true -> X end;
call_fold("trunc", float, [A]) -> fix_trunc(fix_from_ieee(A));
call_fold("round", float, [A]) ->
    case fix_from_ieee(A) of
	Q when Q < 0 -> -(((-Q) + 32768) bsr 16);
	Q -> (Q + 32768) bsr 16
    end;
call_fold(F, _, [A]) when F =:= "trunc"; F =:= "round" -> A;
call_fold(F, _, _) -> unsupported({fold_call, F}).

%% How an index loads: as the declaration's own value type would.
load_vt(integer) -> integer;
load_vt(_) -> unsigned.

%% The built-in functions by name and arity: {Index, Pure, Returns, Args}.
%% src/csp_rt.c csp_builtin_funcs, in its order; `number' returns what its
%% arguments are.
func(F, A) ->
    Tab = [{"min", 2, true, number, [number, number]},
	   {"max", 2, true, number, [number, number]},
	   {"abs", 1, true, number, [number]},
	   {"sign", 1, true, integer, [number]},
	   {"clip", 3, true, number, [number, number, number]},
	   {"trunc", 1, true, integer, [number]},
	   {"round", 1, true, integer, [number]},
	   {"elapsed", 1, false, integer, [timer]},
	   {"progress", 1, false, float, [timer]},
	   {"changed", 1, false, integer, [index]},
	   {"rising", 1, false, integer, [digital]},
	   {"falling", 1, false, integer, [digital]},
	   {"print", 1, false, integer, [any]},
	   {"print", 2, false, integer, [any, any]},
	   {"print", 3, false, integer, [any, any, any]},
	   {"print", 4, false, integer, [any, any, any, any]},
	   {"println", 0, false, integer, []},
	   {"println", 1, false, integer, [any]},
	   {"println", 2, false, integer, [any, any]},
	   {"println", 3, false, integer, [any, any, any]},
	   {"println", 4, false, integer, [any, any, any, any]},
	   {"tick", 0, false, integer, []},
	   {"cycle", 0, false, integer, []},
	   {"latch", 1, false, integer, [integer]}],
    case [{I, P, R, Ts} || {I, {N, Ar, P, R, Ts}} <- lists:zip(lists:seq(0, length(Tab) - 1), Tab),
			   N =:= F, Ar =:= A] of
	[X] -> X;
	[] -> unsupported({call, F})
    end.

part(P) ->
    lists:member(P, ["val", "pin", "port", "dir", "pwm", "endian", "pullup",
		     "pulldown", "period", "fired", "id", "rx", "tx", "dlc",
		     "len"]) orelse unsupported({part, P}),
    list_to_atom(P).

leaf(N, S) ->
    case find(N, S) of
	{ok, {Ix, constant, Vt}} -> {const, Ix, Vt, cval(maps:get(N, maps:get(consts, S)))};
	{ok, {Ix, K, Vt}} when K =:= variable; K =:= param; K =:= digital;
			       K =:= analog; K =:= timer; K =:= field; K =:= local;
			       K =:= buffer ->
	    {var, Ix, Vt};
	{ok, {_, K, _}} -> unsupported({leaf, K});
	error ->
	    case {state_no(N, S), maps:find(N, maps:get(defs, S))} of
		{none, {ok, V}} when is_integer(V) -> {imm, integer, V};
		{none, {ok, {f, B}}} -> {imm, float, B};
		{none, error} -> {imm, integer, keyword(N)};
		{No, _} -> {imm, integer, No}
	    end
    end.

%% A direction or a byte order, as the integer a part holds (Motor.dir = in).
keyword("in") -> 1;     keyword("out") -> 2;     keyword("inout") -> 3;
keyword("native") -> 0; keyword("little") -> 1;  keyword("big") -> 2;
keyword(N) -> unsupported({name, N}).

lookup(N, S) ->
    case find(N, S) of
	{ok, {Ix, K, Vt}} when K =/= constant -> {Ix, K, Vt};
	_ -> unsupported({target, N})
    end.

%% The runtime numbers user states from 3, a repeat or a built-in taking none.
state_no(N, #{states := Ss}) ->
    User = lists:foldl(fun(X, Acc) ->
			       case lists:member(X, Acc) of
				   true -> Acc;
				   false -> Acc ++ [X]
			       end
		       end, [], Ss),
    case string:str(User, [N]) of
	0 -> none;
	I -> I - 1
    end.

alu('+') -> 'ADD';   alu('-') -> 'SUB';   alu('*') -> 'MUL';
alu('/') -> 'DIV';   alu('%') -> 'REM';   alu('<<') -> 'SLA';
alu('>>') -> 'SRA';  alu('&') -> 'BAND';  alu('|') -> 'BOR';
alu('^') -> 'BXOR';  alu('&&') -> 'AND';  alu('||') -> 'OR';
alu('<') -> 'LT';    alu('<=') -> 'LTE';  alu('>') -> 'GT';
alu('>=') -> 'GTE';  alu('==') -> 'EQEQ'; alu('!=') -> 'NEQ';
alu(Op) -> unsupported({op, Op}).

binop(Op0, A0, B0, S) ->
    {Op1, Swap, A1, B1} = case Op0 of
			      'GT'  -> {'LT', true, B0, A0};
			      'GTE' -> {'LTE', true, B0, A0};
			      _     -> {Op0, false, A0, B0}
			  end,
    {Op, A, B, Uns, S0} = case vt_of(A1) =:= float orelse vt_of(B1) =:= float of
			      true ->
				  %% one float makes both: the left first
				  {Af, Sa} = to_float(A1, S),
				  {Bf, Sb} = to_float(B1, Sa),
				  {float_op(Op1), Af, Bf, false, Sb};
			      false ->
				  {Op1, A1, B1, vt_of(A1) =:= unsigned orelse
				   vt_of(B1) =:= unsigned, S}
			  end,
    Rt = rtype(Op, Uns),
    binop_(Op, A, B, Uns, Swap, Rt, S0).

binop_(Op, A, B, Uns, Swap, Rt, S) ->
    Fold = case is_imm(A) andalso is_imm(B) of
	       false -> no;
	       true ->
		   case vt_of(A) =:= float orelse vt_of(B) =:= float of
		       true -> fold_float(Op, imm_value(A), imm_value(B));
		       false -> {ok, eval2(Op, imm_value(A), imm_value(B), Uns)}
		   end
	   end,
    case Fold of
	{ok, V} ->
	    %% a call's register goes back, its value folded in
	    S1 = free_regi(A, S),
	    S2 = case {A, B} of
		     {{regi, R, _, _}, {regi, R, _, _}} -> S1;
		     _ -> free_regi(B, S1)
		 end,
	    {{imm, Rt, V}, S2};
	no -> binop_r(Op, A, B, Uns, Swap, Rt, S)
    end.

%% A float operation on two literals (IEEE bits), folded only when the answer
%% is the same on every node -- both exact in Q16.16 and the result too, which
%% is then exact in IEEE: src/csp_compile.c fold_float. `no': run time.
fold_float(Op, A, B) ->
    case fix_ieee_exact(A) andalso fix_ieee_exact(B) of
	false -> no;
	true ->
	    Qa = fix_from_ieee(A),
	    Qb = fix_from_ieee(B),
	    T = fun(true) -> 1; (false) -> 0 end,
	    case Op of
		'FLT' -> {ok, T(Qa < Qb)};
		'FLTE' -> {ok, T(Qa =< Qb)};
		'FEQEQ' -> {ok, T(Qa =:= Qb)};
		'FNEQ' -> {ok, T(Qa =/= Qb)};
		'AND' -> {ok, T(Qa =/= 0 andalso Qb =/= 0)};
		'OR' -> {ok, T(Qa =/= 0 orelse Qb =/= 0)};
		'FADD' -> exact_q(Qa + Qb);
		'FSUB' -> exact_q(Qa - Qb);
		'FMUL' when (Qa * Qb) band 16#ffff =:= 0 -> exact_q((Qa * Qb) div 65536);
		'FDIV' when Qb =/= 0, (Qa * 65536) rem Qb =:= 0 -> exact_q((Qa * 65536) div Qb);
		_ -> no
	    end
    end.

exact_q(Q) when Q >= -16#80000000, Q =< 16#7fffffff ->
    B = fix_to_ieee(Q),
    case fix_ieee_exact(B) andalso fix_from_ieee(B) =:= Q of
	true -> {ok, B};
	false -> no
    end;
exact_q(_) -> no.

free_regi({regi, R, _, _}, S) -> free(R, S);
free_regi(_, S) -> S.

binop_r(Op, A, B, Uns, Swap, Rt, S) ->
    case {A, B} of
	_ ->
	    {Ra, S1} = load(A, S),
	    {Rb, S2} = load(B, S1),
	    {Dst, S3} = alloc(S2),
	    Fl = [unsigned || Uns] ++ [swap || Swap],
	    {_, S4} = emit(Op, [Dst, Ra, Rb | Fl], S3),
	    S5 = free(Ra, S4),
	    S6 = case Rb of
		     Ra -> S5;
		     _ -> free(Rb, S5)
		 end,
	    {{reg, Dst, Rt}, S6}
    end.

unop(Op0, X, S0) ->
    {A, S} = expr(X, S0),
    Op = case vt_of(A) of
	     float -> float_op(Op0);
	     _ -> Op0
	 end,
    Rt = rtype(Op, vt_of(A) =:= unsigned),
    Fold = case {is_imm(A), vt_of(A), Op} of
	       {false, _, _} -> no;
	       {true, float, 'FNEG'} -> {ok, imm_value(A) bxor 16#80000000};
	       {true, float, 'FMOV'} -> {ok, imm_value(A)};
	       {true, float, 'NOT'} -> {ok, case imm_value(A) band 16#7fffffff of
						0 -> 1; _ -> 0 end};
	       {true, float, _} -> no;
	       {true, _, _} -> {ok, eval1(Op, imm_value(A))}
	   end,
    case {Fold, A} of
	{{ok, V}, {regi, R0, _, _}} -> {{imm, Rt, V}, free(R0, S)};
	{{ok, V}, _} -> {{imm, Rt, V}, S};
	_ ->
	    {Ra, S1} = load(A, S),
	    {Dst, S2} = alloc(S1),
	    {_, S3} = emit(Op, [Dst, Ra], S2),
	    {{reg, Dst, Rt}, free(Ra, S3)}
    end.

%% An operation on an unsigned operand is unsigned; a truth value is not.
rtype(Op, _) when Op =:= 'FADD'; Op =:= 'FSUB'; Op =:= 'FMUL'; Op =:= 'FDIV';
		  Op =:= 'FNEG'; Op =:= 'FMOV' -> float;
rtype(Op, true) ->
    case lists:member(Op, ['ADD', 'SUB', 'MUL', 'DIV', 'REM', 'SLA', 'SRA',
			   'BAND', 'BOR', 'BXOR', 'NEG', 'BNOT', 'MOV']) of
	true -> unsigned;
	false -> integer
    end;
rtype(_Op, false) -> integer.

float_op('ADD') -> 'FADD';  float_op('SUB') -> 'FSUB';
float_op('MUL') -> 'FMUL';  float_op('DIV') -> 'FDIV';
float_op('NEG') -> 'FNEG';  float_op('MOV') -> 'FMOV';
float_op('LT') -> 'FLT';    float_op('LTE') -> 'FLTE';
float_op('EQEQ') -> 'FEQEQ'; float_op('NEQ') -> 'FNEQ';
float_op(Op) -> Op.

%% An integer as a float: a literal converted here, anything else by CVTIF.
to_float(E, S) ->
    case vt_of(E) of
	float -> {E, S};
	Vt when Vt =:= integer; Vt =:= unsigned -> convert('CVTIF', float, E, S);
	Vt -> unsupported({to_float, Vt})
    end.

to_int(E, S) -> convert('CVTFI', integer, E, S).

%% A literal converted here when every node converts it the same -- an
%% integer Q16.16 holds, a float exact in it -- and at run time when not. A
%% call's folded register is left as it is, as C leaves it.
convert('CVTIF', Vt, E, S) when element(1, E) =:= imm; element(1, E) =:= regi ->
    case imm_value(E) of
	I when I >= -32768, I =< 32767 -> {{imm, Vt, fix_to_ieee(I bsl 16)}, S};
	_ -> convert_rt('CVTIF', Vt, E, S)
    end;
convert('CVTFI', Vt, E, S) when element(1, E) =:= imm; element(1, E) =:= regi ->
    B = imm_value(E),
    case fix_ieee_exact(B) of
	true -> {{imm, Vt, fix_trunc(fix_from_ieee(B))}, S};
	false -> convert_rt('CVTFI', Vt, E, S)
    end;
convert(Op, Vt, E, S) -> convert_rt(Op, Vt, E, S).

convert_rt(Op, Vt, E, S) ->
    {R, S1} = load(E, S),
    {D, S2} = alloc(S1),
    {_, S3} = emit(Op, [D, R], S2),
    {{reg, D, Vt}, free(R, S3)}.

vt_of({imm, Vt, _}) -> Vt;
vt_of({var, _, Vt}) -> Vt;
vt_of({const, _, Vt, _}) -> Vt;
vt_of({reg, _, Vt}) -> Vt;
vt_of({ovar, _, _, Vt}) -> Vt;
vt_of({part, _, _, _}) -> integer;
vt_of({elem, _, Vt, _, _}) -> Vt;
vt_of({regi, _, Vt, _}) -> Vt.

set_vt(Vt, {imm, _, V}) -> {imm, Vt, V};
set_vt(Vt, {var, Ix, _}) -> {var, Ix, Vt};
set_vt(Vt, {const, Ix, _, V}) -> {const, Ix, Vt, V};
set_vt(Vt, {reg, R, _}) -> {reg, R, Vt};
set_vt(Vt, {ovar, No, Ix, _}) -> {ovar, No, Ix, Vt};
set_vt(_Vt, P = {part, _, _, _}) -> P;
set_vt(Vt, {elem, B, _, R, L}) -> {elem, B, Vt, R, L};
set_vt(Vt, {regi, R, _, V}) -> {regi, R, Vt, V}.

%% Into a register: a declaration through the cache, a literal by its type.
load({reg, R, _}, S) -> {R, S};
load({regi, R, _, _}, S) -> {R, S};
load({imm, Vt, V}, S) ->
    {R, S1} = alloc(S),
    load_value(R, Vt, V, S1);
%% A named object's member is never cached: the cache keys on the module's
%% declaration, which every object shares.
load({elem, Mem, _, Ar, Sx}, S) ->
    {R, S1} = alloc(S),
    {_, S2} = emit('SETOX', [Ar | Sx], S1),
    {_, S3} = emit('LD', [R, {mem, Mem}], S2),
    {R, free(Ar, S3)};
load({part, Ix, P, Obj}, S) ->
    {R, S1} = alloc(S),
    {_, S2} = emit('LDP', [R, {mem, Ix}, P], seto(Obj, S1)),
    {R, S2};
load({ovar, No, Ix, _}, S) ->
    {R, S1} = alloc(S),
    {_, S2} = emit('LD', [R, {mem, Ix}], seto(No, S1)),
    {R, S2};
load({var, Ix, _}, S) ->
    map_reg(Ix, S, fun(R, Si) -> {_, Sj} = emit('LD', [R, {mem, Ix}], Si),
				 {R, Sj} end);
load({const, Ix, Vt, V}, S) ->
    map_reg(Ix, S, fun(R, Si) -> load_value(R, Vt, V, Si) end).

map_reg(Ix, S = #{rmap := Rm}, Load) ->
    case [R || {R, X} <- maps:to_list(Rm), X =:= Ix] of
	[R] -> {R, S};
	[] ->
	    {R, S1} = alloc(S),
	    Load(R, S1#{rmap := (maps:get(rmap, S1))#{R => Ix}})
    end.

%% A float is its IEEE bits, the same on every node: LIFH makes the node's
%% own of them. 0.0 is the same word in both formats.
load_value(R, float, 0, S) -> load_int(R, 0, S);
load_value(R, float, B, S) when B band 16#ffff =:= 0 ->
    {_, S1} = emit('LIFH', [R, {imm, B bsr 16}, lz], S),
    {R, S1};
load_value(R, float, B, S) ->
    {_, S1} = emit('LIU', [R, {imm, B band 16#ffff}], S),
    {_, S2} = emit('LIFH', [R, {imm, B bsr 16}], S1),
    {R, S2};
load_value(R, unsigned, V, S) -> load_uint(R, V band 16#ffffffff, S);
load_value(R, _, V, S) -> load_int(R, V, S).

load_int(R, V, S) when V >= -32768, V =< 32767 ->
    {_, S1} = emit('LI', [R, {imm, V}], S),
    {R, S1};
load_int(R, V, S) ->
    U = V band 16#ffffffff,
    {_, S1} = emit('LIU', [R, {imm, U band 16#ffff}], S),
    case U > 16#ffff of
	true -> {_, S2} = emit('LIH', [R, {imm, U bsr 16}], S1), {R, S2};
	false -> {R, S1}
    end.

load_uint(R, U, S) when U =< 32767 ->
    {_, S1} = emit('LI', [R, {imm, U}], S),
    {R, S1};
load_uint(R, U, S) when U =< 16#ffff ->
    {_, S1} = emit('LIU', [R, {imm, U}], S),
    {R, S1};
load_uint(R, U, S) ->
    {_, S1} = emit('LIU', [R, {imm, U band 16#ffff}], S),
    {_, S2} = emit('LIH', [R, {imm, U bsr 16}], S1),
    {R, S2}.

%%% ------------------------------------------------------------ registers

alloc(S = #{free := [R | Fs], rmap := Rm}) ->
    {R, S#{free := Fs, rmap := maps:remove(R, Rm)}};
alloc(_S) -> unsupported(registers).

free(R, S = #{free := Fs, rmap := Rm}) ->
    S#{free := [R | Fs], rmap := maps:remove(R, Rm)}.

emit(Op, Args, S = #{code := C, pc := Pc}) ->
    {Pc, S#{code := [{instr, Pc, Op, Args} | C], pc := Pc + 1}}.

%%% ---------------------------------------------------------- the folder
%%%
%%% The runtime's arithmetic: 32 bits, signed unless an operand is unsigned,
%%% a truth value 1 or 0. What it would do at run time that is not plain --
%%% a division by zero, a shift out of range -- is not folded here.

eval2(Op, A, B, U) ->
    T = fun(true) -> 1; (false) -> 0 end,
    W = fun(X) -> case U of true -> X band 16#ffffffff; false -> X end end,
    case Op of
	'ADD' -> s32(A + B);
	'SUB' -> s32(A - B);
	'MUL' -> s32(A * B);
	'DIV' when B =/= 0 -> s32(W(A) div W(B));
	'REM' when B =/= 0 -> s32(W(A) rem W(B));
	'SLA' when B >= 0, B < 32 -> s32(A bsl B);
	'SRA' when B >= 0, B < 32 -> s32(W(A) bsr B);
	'BAND' -> s32(A band B);
	'BOR' -> s32(A bor B);
	'BXOR' -> s32(A bxor B);
	'AND' -> T(A =/= 0 andalso B =/= 0);
	'OR' -> T(A =/= 0 orelse B =/= 0);
	'LT' -> T(W(A) < W(B));
	'LTE' -> T(W(A) =< W(B));
	'EQEQ' -> T(A =:= B);
	'NEQ' -> T(A =/= B);
	'FADD' -> s32(A + B);
	'FSUB' -> s32(A - B);
	'FMUL' -> s32((A * B) bsr 16);
	'FDIV' when B =:= 0 -> 0;
	'FDIV' -> s32(trunc_div(A bsl 16, B));
	'FLT' -> T(A < B);
	'FLTE' -> T(A =< B);
	'FEQEQ' -> T(A =:= B);
	'FNEQ' -> T(A =/= B);
	_ -> unsupported({fold, Op})
    end.

%% C's division: toward zero.
trunc_div(A, B) -> A div B.

eval1('NEG', A) -> s32(-A);
eval1('NOT', A) -> case A of 0 -> 1; _ -> 0 end;
eval1('BNOT', A) -> s32(bnot A);
eval1('FNEG', A) -> s32(-A);
eval1('FMOV', A) -> A;
eval1('MOV', A) -> A;
eval1('CVTIF', A) -> s32(A bsl 16);
eval1('CVTFI', A) when A < 0 -> -((-A) bsr 16);   % toward zero
eval1('CVTFI', A) -> A bsr 16.

s32(X) ->
    Y = X band 16#ffffffff,
    case Y >= 16#80000000 of
	true -> Y - 16#100000000;
	false -> Y
    end.

%%% -------------------------------------------------------- constant folds

fold({'INT', _, T}, _S) -> int(T);
fold({'FLT', _, T}, _S) -> {f, fltbits(T)};
fold({'-', _, X}, S) ->
    case fold(X, S) of
	{f, B} -> {f, fix_to_ieee(s32(-fix_from_ieee(B)))};
	V -> -V
    end;
fold({field, _, {'WORD', _, N}}, #{consts := Cs, defs := Ds}) ->
    case {maps:find(N, Cs), maps:find(N, Ds)} of
	{{ok, V}, _} -> V;
	{_, {ok, V}} -> V;
	_ -> unsupported({not_constant, N})
    end;
fold({Op, _, A, B}, S) ->
    case {fold(A, S), fold(B, S)} of
	{{f, _} = Fa, Fb} -> farith(Op, q16(Fa), q16(Fb));
	{Fa, {f, _} = Fb} -> farith(Op, q16(Fa), q16(Fb));
	{Fa, Fb} -> arith(Op, Fa, Fb)
    end;
fold(X, _S) -> unsupported({fold, X}).

%% A float in a constant -- a declaration's value, a #define -- is worked as
%% the host compiler works it when no image is being made: in Q16.16, back to
%% IEEE bits after each step.
q16({f, B}) -> fix_from_ieee(B);
q16(I) -> s32(I bsl 16).

farith(Op, A, B) ->
    Q = case Op of
	    '+' -> s32(A + B);
	    '-' -> s32(A - B);
	    '*' -> s32((A * B) bsr 16);
	    '/' when B =:= 0 -> 0;
	    '/' -> s32((A bsl 16) div B);
	    _ -> unsupported({fold, Op})
	end,
    {f, fix_to_ieee(Q)}.

arith('+', A, B) -> A + B;
arith('-', A, B) -> A - B;
arith('*', A, B) -> A * B;
arith('/', A, B) when is_integer(A), is_integer(B), B =/= 0 -> A div B;
arith('%', A, B) when is_integer(A), is_integer(B), B =/= 0 -> A rem B;
arith('<<', A, B) when is_integer(A), is_integer(B) -> A bsl B;
arith('>>', A, B) when is_integer(A), is_integer(B) -> A bsr B;
arith('&', A, B) when is_integer(A), is_integer(B) -> A band B;
arith('|', A, B) when is_integer(A), is_integer(B) -> A bor B;
arith('^', A, B) when is_integer(A), is_integer(B) -> A bxor B;
arith(Op, _, _) -> unsupported({fold, Op}).

int("0x" ++ H) -> list_to_integer(H, 16);
int("0X" ++ H) -> list_to_integer(H, 16);
int("0b" ++ B) -> list_to_integer(B, 2);
int("0B" ++ B) -> list_to_integer(B, 2);
int(T) -> list_to_integer(T).

%%% ------------------------------------------- IEEE bits <-> Q16.16 (C's)
%%%
%%% A float in an image is IEEE binary32 bits (format 23); these are
%%% src/csp_fixpoint.c's, in integers, so csp_cc folds what the host compiler
%%% folds and gets the same bits.

%% A literal's bits: whole + fraction, correctly rounded (to nearest, ties to
%% even), by long division -- fix_ieee_decimal.
fltbits(T) ->
    case string:split(T, ".") of
	[U, F] when F =/= [] ->
	    lists:all(fun(C) -> C >= $0 andalso C =< $9 end, U ++ F)
		orelse unsupported({float_literal, T}),
	    W = list_to_integer(U),
	    W > 32767 andalso unsupported(float_range),    % the host is Q16.16
	    ieee_decimal(W * pow10(length(F)) + list_to_integer(F), pow10(length(F)));
	_ -> unsupported({float_literal, T})
    end.

%% N / D as IEEE bits, rounded to nearest even.
ieee_decimal(0, _D) -> 0;
ieee_decimal(N, D) ->
    %% the exponent: 2^E =< N/D < 2^(E+1)
    E = exp2(N, D, 0),
    %% 24 bits of mantissa and what is left
    {M0, Rest} = case E >= 23 of
		     true -> {N div (D bsl (E - 23)), N rem (D bsl (E - 23))};
		     false -> {(N bsl (23 - E)) div D, (N bsl (23 - E)) rem D}
		 end,
    Den = case E >= 23 of true -> D bsl (E - 23); false -> D end,
    M1 = case 2 * Rest of
	     X when X > Den -> M0 + 1;
	     X when X =:= Den, M0 band 1 =:= 1 -> M0 + 1;
	     _ -> M0
	 end,
    {M, E1} = case M1 of
		  16#1000000 -> {M1 bsr 1, E + 1};
		  _ -> {M1, E}
	      end,
    ((E1 + 127) bsl 23) bor (M band 16#7fffff).

exp2(N, D, E) when N >= 2 * D -> exp2(N, 2 * D, E + 1);
exp2(N, D, E) when N < D -> exp2(2 * N, D, E - 1);
exp2(_N, _D, E) -> E.

%% IEEE bits as Q16.16: nearest, halves away from zero; saturating.
fix_from_ieee(B) ->
    E = (B bsr 23) band 16#ff,
    M = (B band 16#7fffff) bor 16#800000,
    Sh = E - 134,
    Q = if E =:= 0 -> 0;
	   E =:= 16#ff; Sh > 7 -> 16#7fffffff;
	   Sh >= 0 -> M bsl Sh;
	   Sh < -24 -> 0;
	   true -> (M + (1 bsl (-Sh - 1))) bsr -Sh
	end,
    case B bsr 31 of 1 -> -Q; 0 -> Q end.

fix_ieee_exact(B) when B band 16#7fffffff =:= 0 -> true;
fix_ieee_exact(B) ->
    E = (B bsr 23) band 16#ff,
    M = (B band 16#7fffff) bor 16#800000,
    Sh = E - 134,
    if E =:= 0; E =:= 16#ff; Sh > 7 -> false;
       Sh >= 0 -> true;
       Sh < -24 -> false;
       true -> M band ((1 bsl -Sh) - 1) =:= 0
    end.

%% Q16.16 as IEEE bits, to nearest even past 24 bits.
fix_to_ieee(0) -> 0;
fix_to_ieee(Q) ->
    Sign = case Q < 0 of true -> 16#80000000; false -> 0 end,
    A = abs(Q),
    P = msb(A, 31),
    {M, P1} = case P > 23 of
		  false -> {A bsl (23 - P), P};
		  true ->
		      D = P - 23,
		      Half = 1 bsl (D - 1),
		      Rest = A band ((1 bsl D) - 1),
		      M0 = A bsr D,
		      M1 = case Rest > Half orelse (Rest =:= Half andalso M0 band 1 =:= 1) of
			       true -> M0 + 1;
			       false -> M0
			   end,
		      case M1 of
			  16#1000000 -> {M1 bsr 1, P + 1};
			  _ -> {M1, P}
		      end
	      end,
    Sign bor ((P1 - 16 + 127) bsl 23) bor (M band 16#7fffff).

msb(A, P) when A band (1 bsl P) =/= 0 -> P;
msb(A, P) -> msb(A, P - 1).

%% Toward zero (fix_trunc).
fix_trunc(Q) when Q < 0 -> -((-Q) bsr 16);
fix_trunc(Q) -> Q bsr 16.

float_bits(F) -> <<B:32>> = <<F:32/float>>, B.
bits_float(B) -> <<F:32/float>> = <<B:32>>, F.

%% A constant's value in code: a float is its bits.
cval({f, B}) -> B;
cval(V) -> V.

pow10(0) -> 1;
pow10(N) -> 10 * pow10(N - 1).

%% What makes an interrupt fire (include/csp.h trigger_t); soft is a flag.
irq(undefined) -> 0;   irq(soft) -> 0;
irq(rising) -> 1;      irq(falling) -> 2;     irq(both) -> 3;
irq(high) -> 4;        irq(low) -> 5;         irq(ready) -> 6.

%% A declaration's own index, whatever object it is read through.
raw(Ix) -> Ix band (?CURRENT - 1).

b2i(true) -> 1;
b2i(false) -> 0.

unsupported(What) -> throw({unsupported, What}).
