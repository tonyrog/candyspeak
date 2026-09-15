#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% Two back ends for utils/words.terms.
%%
%%   gen/csp_words.h     ordinary C, one function per word
%%   gen/csp_words_bc.h  the same words as micro-csp bytecode, plus the offset
%%                       each one starts at
%%
%% The point is that they come from ONE description, so a board can link
%% whichever suits its flash without the word being written twice. The test
%% runs both against the same inputs and compares; that is the only thing that
%% makes "one source, two back ends" a fact rather than an intention.
%%
%% WHAT THE BYTECODE BACK END ACTUALLY DOES that the C one does not: stack
%% scheduling. An expression tree becomes postfix, locals get slots instead of
%% TOR/RAT juggling, and a short-circuit becomes a jump. None of that is the
%% business of whoever writes the word down.
%%
%%   escript utils/gen_words.erl (emit | check)

-mode(compile).

-define(TERMS, "utils/words.terms").
-define(PH,    "gen/csp_words.h").
-define(CH,    "gen/csp_words_c.h").
-define(BCH,   "gen/csp_words_bc.h").

%% Opcodes -- must track mc_op_t in include/csp_mcsp.h.
%% Opcodes as NAMES, not numbers. The array is C source, so it can hold C
%% identifiers -- which means this generator never has to know an opcode's
%% value, and adding one in the middle of mc_op_t cannot silently renumber a
%% generated word. One list element is still exactly one byte, so the branch
%% arithmetic below is unaffected.
-define(BYE,'MC_BYE'). 
-define(EXIT,'MC_EXIT').
-define(LIT8,'MC_LIT8').
-define(LIT16,'MC_LIT16').
-define(DROP,'MC_DROP').
-define(DUP,'MC_DUP').
-define(ADD,'MC_ADD').
-define(SUB,'MC_SUB').
-define(AND,'MC_AND').
-define(OR,'MC_OR').
-define(EQ,'MC_EQ').
-define(NE,'MC_NE').
-define(LT,'MC_LT').
-define(JMP,'MC_JMP').
-define(JZ,'MC_JZ').
-define(DECL,'MC_DECL').
-define(INSTR,'MC_INSTR').
-define(ZEQ,'MC_ZEQ').
-define(NATIVE,'MC_NATIVE').
-define(NATIVEN,'MC_NATIVEN').
-define(CALL,'MC_CALL').
-define(LGET,'MC_LGET').
-define(LSET,'MC_LSET').
-define(BUF,'MC_BUF').
-define(ST, 'MC_ST').
-define(VIEW,'MC_VIEW').
%% Wide reads: a field of more than sixteen bits is two adjacent rows in the
%% field table, and these read both.
-define(DECL2,'MC_DECL2').   -define(INSTR2,'MC_INSTR2').
-define(BUF2,'MC_BUF2').     -define(VIEW2,'MC_VIEW2').
-define(DLIT,'MC_DLIT').     -define(S2D,'MC_S2D').
-define(DDROP,'MC_DDROP').   -define(DADD,'MC_DADD').
-define(DSUB,'MC_DSUB').     -define(DEQ,'MC_DEQ').
-define(DLT,'MC_DLT').
%% Writes.
-define(DECLS,'MC_DECLS').   -define(INSTRS,'MC_INSTRS').
-define(BUFS,'MC_BUFS').     -define(VIEWS,'MC_VIEWS').
-define(DECLS2,'MC_DECLS2'). -define(INSTRS2,'MC_INSTRS2').
-define(BUFS2,'MC_BUFS2').   -define(VIEWS2,'MC_VIEWS2').
-define(STS,'MC_STS').
%% The runtime's tables.
-define(AGET,'MC_AGET').     -define(ASET,'MC_ASET').


main(["emit"]) ->
    [ok = file:write_file(F, T) || {F, T} <- outputs()],
    io:format("~s ~s~n", [?CH, ?BCH]),
    halt(0);
main(["check"]) -> check(outputs());
main(_) -> io:format("usage: gen_words.erl (emit | check)~n"), halt(2).

check([]) -> io:format("~s ~s: ok~n", [?CH, ?BCH]), halt(0);
check([{F, T} | R]) ->
    Want = iolist_to_binary(T),
    case file:read_file(F) of
	{ok, Want} -> check(R);
	_ -> io:format("~s is out of date with ~s~n  run: make words~n", [F, ?TERMS]),
	     halt(1)
    end.

outputs() ->
    {ok, Terms} = file:consult(?TERMS),
    %% TWO KINDS OF WORD. A `word` is reachable from C: it gets a prototype
    %% and, on a bytecode build, a trampoline to cross into the machine. An
    %% `aux` is reachable only from other words and gets neither -- a wrapper
    %% on a word nobody outside calls is pure cost, and a deep vocabulary is
    %% mostly words like that. Source order is kept: an aux is an ordinary word
    %% to everything below here.
    Words = [setelement(1, W, word)
	     || W <- Terms, is_tuple(W), tuple_size(W) =:= 5,
		(element(1, W) =:= word) orelse (element(1, W) =:= aux)],
    put(aux, sets:from_list([N || {aux, N, _, _, _} <- Terms])),
    put(wordlist, Words),
    %% A `pure` native does NOT take the runtime: csp_print_char is the shape --
    %% it writes to the console and knows nothing about st. The wrapper passes
    %% ctx to a `native` and not to a `pure`.
    Nats  = [N || N = {native, _, _, _} <- Terms]
	++ [setelement(1, N, native) || N = {pure, _, _, _} <- Terms],
    put(pures, sets:from_list([F || {pure, F, _, _} <- Terms])),
    Sts   = [T || T = {state, _, _, _} <- Terms],
    Bfs   = [T || T = {buffield, _, _} <- Terms],
    Arrs  = [T || T = {array, _, _, _, _} <- Terms],
    put(states, maps:from_list([{N, P} || {state, N, _, P} <- Sts])),
    put(buffields, sets:from_list([F || {buffield, F, _} <- Bfs])),
    %% Both back ends need to know which names are words: the C one to spell
    %% the csp_ prefix a word's definition gets, the bytecode one to choose
    %% CALL over NATIVE.
    put(words, sets:from_list([element(2, W) || W <- Words])),
    %% Return width per callable, so {call, ...} knows its own shape: a word
    %% that answers a 32-bit field is a double to whoever uses the answer.
    put(retw, maps:from_list(
		[{N, ctw(CT)} || {word, N, _, CT, _} <- Words] ++
		[{N, ctw(R)}  || {native, N, _, R} <- Nats] ++
		[{N, ctw(R)}  || {pure, N, _, R} <- Terms])),
    [{?PH, pfile(Words, Arrs)}, {?CH, cfile(Words)},
     {?BCH, bcfile(Words, Nats, Sts, Bfs, Arrs)}].

%% A native is an EXISTING C function, reached from bytecode through a wrapper.
%% The wrapper is not optional: csp_print_uint is void f(csp_rt_t*, uint16_t),
%% which is not the type mc_leaf_t is, and casting between function types is
%% undefined even where the ABI happens to line up. Ten bytes each, written by
%% the generator so it is no hand work -- but it is not no bytes either.
nat_kind({native, _, 0, _}) -> leaf;
nat_kind({native, _, 1, _}) -> leaf;
nat_kind({native, _, _, _}) -> leafn.

%% ONE index space per table, and the order here is the order wrappers/3 emits.
%% A native, a buffer field and a state field all end up in the same two tables,
%% so the keys are tagged -- a native called `flags` and a buffer field called
%% `flags` are different things.
nat_index(Nats, Sts, Bfs) ->
    L = [{nat, element(2, N)} || N <- Nats, nat_kind(N) =:= leaf]
	++ [{buf, F} || {buffield, F, _} <- Bfs],
    S = [{nat, element(2, N)} || N <- Nats, nat_kind(N) =:= leafn]
	++ [{state, N} || {state, N, _, _} <- Sts],
    maps:from_list(
      [{K, {leaf, I}} || {K, I} <- lists:zip(L, lists:seq(0, length(L)-1))] ++
      [{K, {leafn, I}} || {K, I} <- lists:zip(S, lists:seq(0, length(S)-1))]).

banner() ->
    ["// generated by utils/gen_words.erl from utils/words.terms -- do not edit\n\n"].

%% ------------------------------------------------------------------ C back end

%% WHAT A CALLER SEES, and all it sees. The same prototypes whether the board
%% links the C bodies or runs the bytecode -- that is the whole point of one
%% description: csp_print.c says csp_is_local(st, ix) and is not entitled to
%% know which of the two answered.
pfile(Words, Arrs) ->
    [banner(),
     "#ifndef __CSP_WORDS_H__\n#define __CSP_WORDS_H__\n\n",
     "// One prototype per word. The BODY is either gen/csp_words_c.h (ordinary\n"
     "// C) or gen/csp_words_bc.h (micro-csp bytecode plus a trampoline), and\n"
     "// src/csp_words.c picks between them. Nothing else includes either.\n",
     arr_accessors(Arrs),
     [proto(W) || W <- Words, not sets:is_element(element(2, W), get(aux))],
     "\n// Installs the leaf hooks on a bytecode build, and does nothing on a C\n"
     "// one. Call it once, before the first word -- a word that reaches a\n"
     "// record with no hook installed stops with MC_E_LEAF rather than\n"
     "// answering wrongly.\n"
     "extern void csp_words_init(csp_rt_t* st);\n\n"
     "// The last micro-csp error, kept because a faulting word returns 0 and 0\n"
     "// is a perfectly ordinary answer for most of them. Always 0 on a C build.\n"
     "extern uint8_t csp_word_fault;\n"
     "\n#endif\n"].

%% ONE ACCESSOR PAIR PER TABLE, and the bound is in them. st->io[i] written out
%% at a call site is a read nobody checks; this is the same read with the count
%% that lives beside the table in csp_rt_t. Out of range answers zero and a
%% write out of range is dropped, which is what a word can be given when it
%% cannot be given a pointer.
arr_accessors([]) -> [];
arr_accessors(Arrs) ->
    ["\n// The runtime's tables. Generated from {array, ...} in utils/words.terms;\n"
     "// both back ends reach a table through these and nothing else.\n",
     [arr_acc(A) || A <- Arrs]].

arr_acc({array, N, CT, Path, Bound}) ->
    Nm = atom_to_list(N),
    io_lib:format(
      "static inline ~s csp_arr_~s(csp_rt_t* st, index_t i_)\n"
      "{\n    return (i_ < (index_t)st->~s) ? st->~s[i_] : (~s)0;\n}\n"
      "static inline void csp_arr_set_~s(csp_rt_t* st, index_t i_, ~s v_)\n"
      "{\n    if (i_ < (index_t)st->~s) st->~s[i_] = v_;\n}\n",
      [atom_to_list(CT), Nm, atom_to_list(Bound), Path, atom_to_list(CT),
       Nm, atom_to_list(CT), atom_to_list(Bound), Path]).

proto({word, Name, Args, CT, _}) ->
    io_lib:format("extern ~s csp_~s(csp_rt_t* st~s);\n",
		  [atom_to_list(CT), atom_to_list(Name),
		   [io_lib:format(", index_t ~s", [A]) || A <- Args]]).

aproto({word, Name, Args, CT, _}) ->
    io_lib:format("CSP_UNUSED static ~s csp_~s(csp_rt_t* st~s);\n",
		  [atom_to_list(CT), atom_to_list(Name),
		   [io_lib:format(", index_t ~s", [A]) || A <- Args]]).

cfile(Words) ->
    [banner(),
     "#ifndef __CSP_WORDS_C_H__\n#define __CSP_WORDS_C_H__\n\n",
     "// The C back end: one ordinary function per word. Included by\n"
     "// src/csp_words.c when the build does NOT run bytecode.\n\n",
     "// Forward declarations for the aux words, so which word calls which is\n"
     "// not a question of what order utils/words.terms happens to list them in.\n",
     [aproto(W) || W <- Words, sets:is_element(element(2, W), get(aux))],
     "\n",
     [cword(W) || W <- Words],
     "#endif\n"].

%% Every local is DECLARED at the top and ASSIGNED where it stands. The tree is
%% built with -Wdeclaration-after-statement, so a declaration in the middle of a
%% block does not compile -- and hoisting is what the surrounding code does by
%% hand anyway.
cword({word, Name, Args, CT, Body}) ->
    Locals = clocals(Body),
    LW = lwidths(Body),
    %% An aux word has no prototype, so it must be static. CSP_UNUSED because
    %% an aux that only the bytecode reaches has no C caller at all, and that
    %% is not a mistake.
    [case sets:is_element(Name, get(aux)) of
	 true  -> "CSP_UNUSED static ";
	 false -> ""
     end,
     io_lib:format("~s csp_~s(csp_rt_t* st~s)\n{\n",
		   [atom_to_list(CT), atom_to_list(Name),
		    [io_lib:format(", index_t ~s", [A]) || A <- Args]]),
     [io_lib:format("\t~s ~s;\n",
		    [case maps:get(L, LW, 1) of 2 -> "uint32_t"; _ -> "index_t" end, L])
      || L <- Locals],
     case uses_st(Body) of true -> ""; false -> "\t(void)st;\n" end,
     case Locals of [] -> ""; _ -> "\n" end,
     [cstmt(S, "\t") || S <- Body],
     "}\n\n"].

clocals(Body) -> lists:usort(lists:flatten([cl(S) || S <- Body])).
cl({local, N, _})     -> [N];
cl({'if', _, T})      -> [cl(S) || S <- T];
cl({while, _, B})     -> [cl(S) || S <- B];
cl({switch, _, Cs, D}) -> [[[cl(S) || S <- B] || {_, B} <- Cs], [cl(S) || S <- D]];
cl({'if', _, T, F})   -> [[cl(S) || S <- T], [cl(S) || S <- F]];
cl(_)                 -> [].

%% A word that never reads a declaration, the count or a native does not touch
%% st, and gcc says so. Cheaper to answer here than to cast the warning away.
uses_st(X) when is_tuple(X) ->
    case element(1, X) of
	aget  -> true;
	decl  -> true;
	instr -> true;
	buf   -> true;
	view  -> true;
	state -> true;
	nd    -> true;
	nn    -> true;
	call  -> true;
	_    -> lists:any(fun uses_st/1, tuple_to_list(X))
    end;
uses_st(L) when is_list(L) -> lists:any(fun uses_st/1, L);
uses_st(_) -> false.

cstmt({local, N, E}, I) -> [I, atom_to_list(N), " = ", cexp(E), ";\n"];
cstmt({set, N, E}, I)   -> [I, atom_to_list(N), " = ", cexp(E), ";\n"];
cstmt({return, E}, I)   -> [I, "return ", cexp(E), ";\n"];
cstmt({do, E}, I)       -> [I, cexp(E), ";\n"];
%% A FIELD OR TABLE WRITE. Not cexp's business: an assignment is a statement
%% here, and the write side reaches a different place than the read side does --
%% the RAM slot, never the cache a read may have handed back.
cstmt({store, T, V}, I) -> [I, cstore(T, V), ";\n"];
cstmt({'if', C, T}, I)  -> [I, "if (", cexp(C), ") {\n", [cstmt(S, I ++ "\t") || S <- T], I, "}\n"];
cstmt({while, C, B}, I) ->
    [I, "while (", cexp(C), ") {\n", [cstmt(S, I ++ "\t") || S <- B], I, "}\n"];
%% A real C switch, not an if-chain: it is what the surrounding code writes and
%% what gcc turns into a jump table. The bytecode back end cannot do that -- it
%% emits the chain -- and the cross-check in tests/words.c is what says the two
%% still agree.
cstmt({switch, E, Cases, Default}, I) ->
    [I, "switch (", cexp(E), ") {\n",
     [[I, "case ", cexp({const, K}), ":\n",
       [cstmt(S, I ++ "\t") || S <- B], I, "\tbreak;\n"] || {K, B} <- Cases],
     case Default of
	 [] -> "";
	 _  -> [I, "default:\n", [cstmt(S, I ++ "\t") || S <- Default],
		I, "\tbreak;\n"]
     end,
     I, "}\n"];
cstmt({'if', C, T, F}, I) ->
    [I, "if (", cexp(C), ") {\n", [cstmt(S, I ++ "\t") || S <- T],
     I, "} else {\n", [cstmt(S, I ++ "\t") || S <- F], I, "}\n"].

cstore({decl, E, F}, V) ->
    ["csp_decl_set_", atom_to_list(F), "(ram_decl_at(st, ", cexp(E), "), ",
     cexp(V), ")"];
cstore({instr, E, F}, V) ->
    ["csp_instr_set_", atom_to_list(F), "(ram_instr_at(st, ", cexp(E), "), ",
     cexp(V), ")"];
cstore({buf, E, F}, V) ->
    ["csp_buf_set_", atom_to_list(F), "(&st->buf[", cexp(E), "], ", cexp(V), ")"];
cstore({view, E, F}, V) ->
    ["csp_view_set_", atom_to_list(F), "(&st->view[", cexp(E), "], ", cexp(V), ")"];
cstore({aget, A, E}, V) ->
    ["csp_arr_set_", atom_to_list(A), "(st, ", cexp(E), ", ", cexp(V), ")"];
cstore({state, N}, V) ->
    ["st->", maps:get(N, get(states)), " = ", cexp(V)].

cexp({const, N}) when is_integer(N) -> integer_to_list(N);
cexp({const, N})                    -> atom_to_list(N);
cexp({const16, N})                  -> atom_to_list(N);
cexp({var, N})                      -> atom_to_list(N);
cexp({nd})                          -> "st->ps.nd";
cexp({nn})                          -> "st->ps.nn";
cexp({state, N})                    -> ["st->", maps:get(N, get(states))];
cexp({buf, E, F})                   -> ["csp_buf_get_",atom_to_list(F),"(&st->buf[", cexp(E), "])"];
cexp({view, E, F})                   -> ["csp_view_get_",atom_to_list(F),"(&st->view[", cexp(E), "])"];
cexp({index, E})                    -> ["INDEX(", cexp(E), ")"];
cexp({decl, E, F})                  -> ["decl(st, ", cexp(E), ", ", atom_to_list(F), ")"];
cexp({instr, E, F})                 -> ["instr(st, ", cexp(E), ", ", atom_to_list(F), ")"];
cexp({aget, A, E})                  -> ["csp_arr_", atom_to_list(A), "(st, ", cexp(E), ")"];
cexp({op, Op, A, B})                -> ["(", cexp(A), " ", cop(Op), " ", cexp(B), ")"];
cexp({andthen, A, B})               -> ["(", cexp(A), " && ", cexp(B), ")"];
cexp({orthen, A, B})                -> ["(", cexp(A), " || ", cexp(B), ")"];
cexp({call, F, As}) ->
    N = case sets:is_element(F, get(words)) of
	    true  -> "csp_" ++ atom_to_list(F);
	    false -> atom_to_list(F)
	end,
    %% A `pure` native takes no runtime, here as in the bytecode wrapper.
    Ctx = case sets:is_element(F, get(pures)) of
	      true  -> [];
	      false -> ["st"]
	  end,
    [N, "(", string:join(Ctx ++ [lists:flatten(cexp(A)) || A <- As], ", "), ")"].

%% ------------------------------------------------------------- widths
%%
%% A cell is sixteen bits. Anything wider is a DOUBLE: two cells, high on top,
%% the way Forth holds one. None of that is written in utils/words.terms -- the
%% width comes from the layout, and the generator decides.

stag(decl)  -> {$D, ?DECLS,  ?DECLS2};
stag(instr) -> {$I, ?INSTRS, ?INSTRS2};
stag(buf)   -> {$B, ?BUFS,   ?BUFS2};
stag(view)  -> {$V, ?VIEWS,  ?VIEWS2}.

fld_rd(Rec, Tag, Op1, Op2, E, F, Env) ->
    Op = case cells(fwidth(Rec, F)) of 1 -> Op1; 2 -> Op2 end,
    bexp(E, Env) ++ [Op, {field, {Tag, F}}].

%% A field's width, straight out of utils/layout.terms -- the same file the
%% accessors and the micro-csp field tables come from, so there is no second
%% place to keep in step. Read once and cached.
fwidth(Rec, F) ->
    T = case get(layout) of
	    undefined -> {ok, X} = file:consult("utils/layout.terms"),
			 put(layout, X), X;
	    X -> X
	end,
    W = [B || {record, R, _, _, Fs} <- T,
	      {N, B} <- [fw(Y) || Y <- Fs],
	      match_field(Rec, R, N, F)]
	++ [B || {record, R, _, Fs} <- T,
		 {N, B} <- [fw(Y) || Y <- Fs],
		 match_field(Rec, R, N, F)],
    case W of
	[]      -> 16;                    % not in layout.terms; assume a cell
	[B | _] -> B
    end.

fw({N, B})       -> {N, B};
fw({N, B, _})    -> {N, B}.

%% decl fields are named `md_n` for arm md field n, and `type` for a common
%% one. The arm is the prefix; a common field has none.
match_field(Rec, R, N, F) ->
    RS = atom_to_list(R), NS = atom_to_list(N), FS = atom_to_list(F),
    Fam = atom_to_list(Rec),
    case lists:prefix(Fam ++ "_", RS) of
	true  -> (RS =:= Fam ++ "_common") andalso (NS =:= FS);
	false -> false
    end orelse
    case string:split(RS, "_") of
	_ -> (RS =/= Fam) andalso (RS ++ "_" ++ NS =:= FS)
    end orelse
    ((RS =:= Fam) andalso (NS =:= FS)).

cells(B) when B =< 16 -> 1;
cells(_)              -> 2.

%% Widen a cell to a double where the other side of an operation is one.
wide(E, Env, Code) ->
    case ewidth(E, Env) of
	1 -> Code ++ [?S2D];
	2 -> Code
    end.

dop(eq) -> ?DEQ; dop(lt) -> ?DLT;
dop(add) -> ?DADD; dop(sub) -> ?DSUB;
dop(ne) -> ?DEQ;                       % caller inverts; see bop
dop(Op) -> bop(Op).

ewidth({const, N}, _) when is_integer(N), N =< 65535 -> 1;
ewidth({const, N}, _) when is_integer(N) -> 2;
ewidth({const, _}, _) -> 1;
ewidth({const16, _}, _) -> 1;
ewidth({var, N}, _) -> case get({w, N}) of undefined -> 1; W -> W end;
ewidth({index, _}, _) -> 1;
ewidth({nd}, _) -> 1;
ewidth({nn}, _) -> 1;
ewidth({state, _}, _) -> 1;
ewidth({aget, _, _}, _) -> 1;
ewidth({decl, _, F}, _)  -> cells(fwidth(decl, F));
ewidth({instr, _, F}, _) -> cells(fwidth(instr, F));
ewidth({buf, _, F}, _)   -> cells(fwidth(buf, F));
ewidth({view, _, F}, _)  -> cells(fwidth(view, F));
ewidth({op, Op, A, B}, Env) ->
    case lists:member(Op, [eq, ne, lt]) of
	true  -> 1;                      % a comparison is a flag, never a double
	false -> max(ewidth(A, Env), ewidth(B, Env))
    end;
ewidth({andthen, _, _}, _) -> 1;
ewidth({orthen, _, _}, _) -> 1;
ewidth({call, F, _}, _) -> maps:get(F, get(retw), 1);
ewidth(_, _) -> 1.

%% A C return type in cells.
ctw('uint32_t') -> 2;
ctw(_)          -> 1.

%% Which locals of a word are wide, for the C back end's declarations.
lwidths(Body) -> maps:from_list(lw(Body)).

lw({local, N, E})      -> [{N, ewidth(E, #{})}];
lw(X) when is_tuple(X) -> lists:append([lw(E) || E <- tuple_to_list(X)]);
lw(L) when is_list(L)  -> lists:append([lw(E) || E <- L]);
lw(_)                  -> [].

cop(eq) -> "=="; cop(ne) -> "!="; cop(lt) -> "<";
cop(add) -> "+"; cop(sub) -> "-"; cop(and_) -> "&"; cop(or_) -> "|".

%% ----------------------------------------------------------- bytecode back end

wrappers(Nats, Sts, Bfs) ->
    L = [{"lw_", F} || N = {native, F, _, _} <- Nats, nat_kind(N) =:= leaf]
	++ [{"lbuf_", F} || {buffield, F, _} <- Bfs],
    S = [{"ln_", F} || N = {native, F, _, _} <- Nats, nat_kind(N) =:= leafn],
    [[wrapper(N) || N <- Nats], buf_leaves(Bfs),
     state_hook(Sts, get(wordlist)),
     "\n", leaf_names(L, "LW_"), leaf_names(S, "LN_"),
     table("csp_word_leaves", "mc_leaf_t", L),
     table("csp_word_leavesn", "mc_leafn_t", S)].

%% A leaf's index, by NAME. The bytecode is meant to be read -- every other
%% operand in it already says what it means (MFD_TYPE, MFS_ND, MFA_IO) and a
%% bare `MC_NATIVE,2` was the one place left where you had to go and count.
leaf_names([], _) -> [];
leaf_names(Fs, P) ->
    [[io_lib:format("#define ~s~s ~p\n",
		    [P, string:uppercase(atom_to_list(F)), I])
      || {{_, F}, I} <- lists:zip(Fs, lists:seq(0, length(Fs) - 1))], "\n"].

%% ONLY WHAT A WORD ASKS FOR. The runtime has twenty-two fields in the state
%% list and the words between them read eight and write two -- but a switch
%% with every case in it was emitted anyway: 176 bytes of reads nobody makes
%% and 202 of writes nobody makes, on a part with 32K of flash. The list in
%% words.terms says what CAN be reached; this says what IS.
state_hook(Sts, Words) ->
    Rd = used_states(Words, read),
    Wr = used_states(Words, write),
    Live = [T || T = {state, N, _, _} <- Sts, lists:member(N, Rd ++ Wr)],
    Ids = maps:from_list(lists:zip([N || {state, N, _, _} <- Live],
				   lists:seq(0, length(Live) - 1))),
    put(sids, Ids),
    ["typedef enum {\n",
     [io_lib:format("    MFS_~s = ~p,\n",
		    [string:uppercase(atom_to_list(N)), maps:get(N, Ids)])
      || {state, N, _, _} <- Live],
     io_lib:format("    MFS_NFIELD = ~p\n} mfs_t;\n\n", [length(Live)]),
     io_lib:format("// ~p of ~p state fields are reached by a word.\n",
		   [length(Live), length(Sts)]),
     hook_read([T || T = {state, N, _, _} <- Live, lists:member(N, Rd)]),
     hook_write([T || T = {state, N, _, _} <- Live, lists:member(N, Wr)])].

hook_read([]) ->
    "// No word reads a runtime field.\n"
    "#define csp_word_state ((mc_cell_t (*)(void*, mc_cell_t))0)\n\n";
hook_read(Live) ->
    ["static mc_cell_t csp_word_state(void* c_, mc_cell_t i_)\n{\n"
     "    switch (i_) {\n",
     [io_lib:format("    case MFS_~s: return (mc_cell_t)((csp_rt_t*)c_)->~s;\n",
		    [string:uppercase(atom_to_list(N)), P]) || {state, N, _, P} <- Live],
     "    default: return 0;\n    }\n}\n"].

hook_write([]) ->
    "// No word writes a runtime field, so MC_STS has nothing to reach and\n"
    "// stops with MC_E_LEAF if a stream ever carries one.\n"
    "#define csp_word_state_set ((void (*)(void*, mc_cell_t, mc_cell_t))0)\n\n";
hook_write(Live) ->
    ["\n// Its own switch rather than a table of member offsets: they are\n"
     "// different widths and different types, and offsetof with a cast is how\n"
     "// a two-byte field gets a four-byte store.\n"
     "static void csp_word_state_set(void* c_, mc_cell_t i_, mc_cell_t v_)\n{\n"
     "    switch (i_) {\n",
     [io_lib:format("    case MFS_~s: ((csp_rt_t*)c_)->~s = (~s)v_; break;\n",
		    [string:uppercase(atom_to_list(N)), P, atom_to_list(CT)])
      || {state, N, CT, P} <- Live],
     "    default: break;\n    }\n}\n"].

%% Every {state, N} a word reads, and every {store, {state, N}, _} it writes.
used_states(Words, Kind) ->
    lists:usort(lists:append([scan_states(B, Kind) || {word, _, _, _, B} <- Words])).

scan_states({store, {state, N}, V}, write) -> [N | scan_states(V, write)];
scan_states({store, {state, _}, V}, read)  -> scan_states(V, read);
scan_states({state, N}, read)              -> [N];
scan_states({nd}, read)                    -> [nd];
scan_states({nn}, read)                    -> [nn];
scan_states(X, K) when is_tuple(X) ->
    lists:append([scan_states(E, K) || E <- tuple_to_list(X)]);
scan_states(L, K) when is_list(L) ->
    lists:append([scan_states(E, K) || E <- L]);
scan_states(_, _) -> [].

%% A zero-length array is a GNU extension, not ISO C, and this header is
%% included by every port. An empty table is simply not emitted.
table(Name, Ty, []) ->
    io_lib:format("// no ~s\n#define ~s     ((const ~s*)0)\n"
		  "#define ~s_N  0\n\n", [Name, Name, Ty, Name]);
table(Name, Ty, Fs) ->
    [io_lib:format("\nstatic const ~s ~s[] RODATA = {\n", [Ty, Name]),
     [io_lib:format("    ~s~s,\n", [P, F]) || {P, F} <- Fs],
     "};\n",
     io_lib:format("#define ~s_N  ~p\n\n", [Name, length(Fs)])].

%% The context argument, or nothing when the native does not take one.
ctxarg(F) ->
    case sets:is_element(F, get(pures)) of
	true  -> [];
	false -> ["(csp_rt_t*)c_"]
    end.

wrapper({native, F, 0, Ret}) ->
    [io_lib:format("static mc_cell_t lw_~s(void* c_, mc_cell_t t_)\n{\n"
		   "    (void)c_;\n", [F]),
     ret_call(Ret, F, ctxarg(F)), "}\n"];
wrapper({native, F, 1, Ret}) ->
    [io_lib:format("static mc_cell_t lw_~s(void* c_, mc_cell_t t_)\n{\n"
		   "    (void)c_;\n", [F]),
     ret_call(Ret, F, ctxarg(F) ++ ["t_"]), "}\n"];
wrapper({native, F, N, Ret}) ->
    %% sp[0] is the top, so the LAST argument is nearest -- sp[N-1] is the
    %% first. The wrapper leaves one cell where N were, and returns where the
    %% stack should stand.
    Args = ctxarg(F) ++
	    [io_lib:format("sp_[~p]", [N - I]) || I <- lists:seq(1, N)],
    [io_lib:format("static mc_cell_t* ln_~s(void* c_, mc_cell_t* sp_)\n{\n"
		   "    (void)c_;\n", [F]),
     case Ret of
	 void -> [io_lib:format("    ~s(~s);\n", [F, string:join(Args, ", ")]),
		  io_lib:format("    sp_ += ~p;\n    sp_[0] = 0;\n", [N - 1])];
	 _    -> [io_lib:format("    mc_cell_t v_ = (mc_cell_t)~s(~s);\n",
				[F, string:join(Args, ", ")]),
		  io_lib:format("    sp_ += ~p;\n    sp_[0] = v_;\n", [N - 1])]
     end,
     "    return sp_;\n}\n"].

ret_call(void, F, Args) ->
    [io_lib:format("    ~s(~s);\n    return t_;\n", [F, string:join(Args, ", ")])];
ret_call(_, F, Args) ->
    [io_lib:format("    return (mc_cell_t)~s(~s);\n", [F, string:join(Args, ", ")])].

buf_leaves(Bfs) ->
    [[io_lib:format("static mc_cell_t lbuf_~s(void* c_, mc_cell_t i_)\n{\n"
		    "    return (mc_cell_t)((csp_rt_t*)c_)->buf[i_].~s;\n}\n",
		    [F, F]) || {buffield, F, _} <- Bfs]].

bcfile(Words, Nats, Sts, Bfs, Arrs) ->
    NI = nat_index(Nats, Sts, Bfs),
    %% The NAMES have to be known before the layout pass, because that is what
    %% decides word-versus-native at each call site. The offsets in this seed
    %% are wrong and unused: only membership is asked during pass one, and the
    %% real map replaces it before anything is rendered.
    put(offs, maps:from_list([{element(2, W), 0} || W <- Words])),
    %% Pass one lays every word out to learn the offsets and frame sizes; the
    %% placeholders a forward call left behind are resolved when the bytes are
    %% rendered. Lengths never change between the passes -- a call is four bytes
    %% either way -- so one layout pass is enough.
    {Code, Offs, Frames} =
	lists:foldl(fun(W, {Acc, O, Fr}) ->
			    {B, N} = bword(W, NI),
			    {Acc ++ B, O ++ [{element(2, W), length(Acc)}],
			     Fr ++ [{element(2, W), N}]}
		    end, {[], [], []}, Words),
    OffMap = maps:from_list(Offs),
    FrMap = maps:from_list(Frames),
    put(offs, OffMap), put(frames, FrMap),
    [banner(),
     "#ifndef __CSP_WORDS_BC_H__\n#define __CSP_WORDS_BC_H__\n\n",
     "// The same words as bytecode. Locals are SLOTS, not stack juggling: the\n"
     "// generator assigns them, so nothing here has to hold a variable with\n"
     "// TOR/RAT the way hand-encoding does.\n",
     [io_lib:format("#define CSP_W_~s_ENTRY ~p\n", [string:uppercase(atom_to_list(N)), O])
      || {N, O} <- Offs],
     io_lib:format("#define CSP_WORDS_BC_LEN ~p\n\n", [length(Code)]),
     wrappers(Nats, Sts, Bfs),
     array_hook(Arrs, Words),
     [io_lib:format("#define CSP_W_~s_FRAME ~p\n",
		    [string:uppercase(atom_to_list(N)), F]) || {N, F} <- Frames],
     "\n", asserts(Code), "\n",
     "// In FLASH: on AVR a const array without RODATA is .rodata, which the\n"
     "// linker puts inside .data and startup copies into RAM -- held there\n"
     "// for the life of the program, for a table that is never written. The\n"
     "// machine reads it with ro_byte, so it never has to be in RAM at all.\n"
     "static const uint8_t csp_words_bc[] RODATA = {\n",
     wrap([render(B) || B <- annotate(Code,Offs)]),
     "};\n\n",
     "// C CALLING BYTECODE. One per word, with the same prototype the C back\n"
     "// end gives it -- the caller cannot tell which it linked. Arguments go\n"
     "// on the data stack, which is where a word\'s caller leaves them, so the\n"
     "// entry word is not a special case. csp_word_run is in src/csp_words.c:\n"
     "// it owns the stacks and is the one place that knows how big they are.\n"
     "//\n"
     "// Behind CSP_WORDS_TRAMPOLINES because tests/words.c links BOTH back ends\n"
     "// into one binary to compare them: there the C bodies already define\n"
     "// these names, and it calls the machine itself.\n"
     "#ifdef CSP_WORDS_TRAMPOLINES\n",
     [tramp(W, FrMap) || W <- Words,
			 not sets:is_element(element(2, W), get(aux))],
     "#endif\n\n#endif\n"].

tramp({word, Name, Args, CT, _}, FrMap) ->
    N = atom_to_list(Name),
    U = string:uppercase(N),
    io_lib:format(
      "~s csp_~s(csp_rt_t* st~s)\n{\n"
      "\tmc_cell_t a_[~p];\n\n"
      "~s"
      "\treturn (~s)csp_word_run(st, CSP_W_~s_ENTRY, a_, ~p, ~p);\n}\n\n",
      [atom_to_list(CT), N,
       [io_lib:format(", index_t ~s", [A]) || A <- Args],
       max(length(Args), 1),
       [io_lib:format("\ta_[~p] = (mc_cell_t)~s;\n", [I, A])
	|| {I, A} <- lists:zip(lists:seq(0, length(Args) - 1), Args)],
       atom_to_list(CT), U, length(Args), maps:get(Name, FrMap)]).

%% ONLY THE TABLES A WORD REACHES, for the same reason the state hook is
%% trimmed: a switch costs its cases whether or not anything takes them.
array_hook(Arrs, Words) ->
    Used = lists:usort(scan_arrays(Words)),
    Live = [A || A = {array, N, _, _, _} <- Arrs, lists:member(N, Used)],
    case Live of
	[] ->
	    "// No word reaches a runtime table.\n"
	    "#define csp_word_array     ((mc_cell_t (*)(void*, mc_cell_t, mc_cell_t))0)\n"
	    "#define csp_word_array_set ((void (*)(void*, mc_cell_t, mc_cell_t, mc_cell_t))0)\n\n";
	_ ->
	    ["typedef enum {\n",
	     [io_lib:format("    MFA_~s = ~p,\n", [string:uppercase(atom_to_list(N)), I])
	      || {{array, N, _, _, _}, I} <-
		     lists:zip(Live, lists:seq(0, length(Live) - 1))],
	     io_lib:format("    MFA_NTABLE = ~p\n} mfa_t;\n\n", [length(Live)]),
	     io_lib:format("// ~p of ~p tables are reached by a word.\n",
			   [length(Live), length(Arrs)]),
	     "static mc_cell_t csp_word_array(void* c_, mc_cell_t id_, mc_cell_t i_)\n"
	     "{\n    switch (id_) {\n",
	     [io_lib:format("    case MFA_~s: return (mc_cell_t)csp_arr_~s((csp_rt_t*)c_,"
			    " (index_t)i_);\n",
			    [string:uppercase(atom_to_list(N)), atom_to_list(N)])
	      || {array, N, _, _, _} <- Live],
	     "    default: return 0;\n    }\n}\n\n",
	     "static void csp_word_array_set(void* c_, mc_cell_t id_, mc_cell_t i_,\n"
	     "\t\t\t       mc_cell_t v_)\n{\n    switch (id_) {\n",
	     [io_lib:format("    case MFA_~s: csp_arr_set_~s((csp_rt_t*)c_, (index_t)i_,"
			    " (~s)v_); break;\n",
			    [string:uppercase(atom_to_list(N)), atom_to_list(N),
			     atom_to_list(CT)])
	      || {array, N, CT, _, _} <- Live],
	     "    default: break;\n    }\n}\n\n"]
    end.

scan_arrays(Words) -> lists:append([sa(B) || {word, _, _, _, B} <- Words]).

sa({aget, A, E})       -> [A | sa(E)];
sa(X) when is_tuple(X) -> lists:append([sa(E) || E <- tuple_to_list(X)]);
sa(L) when is_list(L)  -> lists:append([sa(E) || E <- L]);
sa(_)                  -> [].

%% annotate code with word names
annotate(Code, Offs) ->
    annotate(Code, 0, Offs, []).

annotate(Cs, I, [{N,I}|Offs], Acc) ->
    annotate(Cs, I, Offs, [{word,N,I}|Acc]);
annotate([C|Cs], I, Offs, Acc) ->
    annotate(Cs, I+1, Offs, [C|Acc]);
annotate([], _I, _Offs, Acc) ->
    lists:reverse(Acc).

%% A named constant could exceed a byte, and MC_LIT8 would take the low half
%% without a word. The assert says so at compile time instead.
asserts(Code) ->
    [io_lib:format("CSP_STATIC_ASSERT((~s) <= 255,\n\t\t  \"~s does not fit a"
		   " micro-csp LIT8 operand\");\n", [A, A])
     || {cconst, A} <- lists:usort([C || C = {cconst, _} <- Code])]
	++ [io_lib:format("CSP_STATIC_ASSERT((~s) <= 65535,\n\t\t  \"~s does not fit a"
			  " micro-csp LIT16 operand\");\n", [A, A])
	    || {cconst_lo, A} <- lists:usort([C || C = {cconst_lo, _} <- Code])].

render(N) when is_integer(N)  -> integer_to_list(N);
render({cconst, A})           -> atom_to_list(A);
%% A symbolic constant too wide for one byte. The token stream is C source, so
%% the split is spelled in C and stays correct if the macro changes.
render({cconst_lo, A})        -> ["(uint8_t)((", atom_to_list(A), ") & 0xFF)"];
render({cconst_hi, A})        -> ["(uint8_t)(((", atom_to_list(A), ") >> 8) & 0xFF)"];
render({sid, N}) -> "MFS_" ++ string:uppercase(atom_to_list(N));
render({aid, N}) -> "MFA_" ++ string:uppercase(atom_to_list(N));
render({lname, P, F}) -> P ++ string:uppercase(atom_to_list(F));
render({field, {X,F}})        -> "MF"++[X,$_]++string:uppercase(atom_to_list(F));
render({wlo, F})  -> integer_to_list(maps:get(F, get(offs)) band 16#FF);
render({whi, F})  -> integer_to_list((maps:get(F, get(offs)) bsr 8) band 16#FF);
render({frame, F}) -> integer_to_list(maps:get(F, get(frames)));
render({leaf, F}) ->
    %% Not built yet, and it must not LOOK built. A call needs two things this
    %% generator does not have: a leaf table to index (with a wrapper per
    %% callee, since an existing f(csp_rt_t*, uint16_t) is not the same type as
    %% mc_leaf_t and casting between function types is undefined), and word ->
    %% word calls resolved to offsets in the one shared area. Until both exist
    %% this stops at generation time rather than emitting something plausible.
    io:format("{call, ~s, ...} is not implemented: micro-csp has no leaf table"
	      " yet.~n  See doc/AVR_CODE_SIZE.md.~n", [F]),
    halt(1);
render(W = {word,_,_}) -> W; %% keep for wrap
render(A) when is_atom(A)     -> atom_to_list(A).

wrap(Items) -> 
    wrap(Items, 8, 8, []).
wrap([], _, _N, Acc) -> 
    lists:reverse(Acc);
wrap([{word,W,I}|T], _I, N, Acc) ->
    ENTRY = "CSP_W_"++string:uppercase(atom_to_list(W))++"_ENTRY",
    NL = if I > 0 -> "\n"; true -> "" end,
    wrap(T, N, N, [[NL,"// ",ENTRY,"\n"] | Acc]);
wrap(L, 0, N, Acc) ->
    wrap(L, N, N, ["\n"|Acc]);
wrap([H|T], I, N, Acc) ->
    wrap(T, I-1, N, [[H,","]|Acc]).

%% Arguments become locals 0..n-1; declared locals follow. One flat frame.
bword({word, Name, Args, _CT, Body}, NI) ->
    put(ni, NI), put(self, Name),
    %% Widths are per WORD: a local called `want` in one is not the one in the
    %% next, and a stale entry makes the later word read a slot it never wrote.
    [erase(K) || K <- get_keys(), is_tuple(K), element(1, K) =:= w],
    Env0 = maps:from_list(lists:zip(Args, lists:seq(0, length(Args) - 1))),
    %% PROLOGUE: arguments arrive on the data stack -- the caller pushed them
    %% left to right, so the last one is on top -- and the word moves them into
    %% its own frame before anything else. Without this a called word reads
    %% local 0 of a frame nobody wrote.
    Pro = lists:append([[?LSET, K] || K <- lists:reverse(lists:seq(0, length(Args) - 1))]),
    {Code, Env} = lists:foldl(fun(S, {C, E}) ->
				      {C2, E2} = bstmt(S, E),
				      {C ++ C2, E2}
			      end, {[], Env0}, Body),
    {Pro ++ Code, maps:size(Env)}.

bstmt({local, N, E}, Env) ->
    K = maps:size(Env),
    W = ewidth(E, Env),
    put({w, N}, W),
    Env2 = case W of
	       1 -> maps:put(N, K, Env);
	       2 -> maps:put({hi, N}, K + 1, maps:put(N, K, Env))
	   end,
    Code = case W of
	       1 -> [?LSET, K];
	       2 -> [?LSET, K + 1, ?LSET, K]
	   end,
    {bexp(E, Env) ++ Code, Env2};
%% set is local without the declaration: the name is already in the frame, so
%% this only assigns. In C both emit the same line -- the difference is that
%% `local` is what puts the name at the top of the function.
bstmt({set, N, E}, Env) ->
    case get({w, N}) of
	2 -> {wide(E, Env, bexp(E, Env))
	      ++ [?LSET, maps:get({hi, N}, Env), ?LSET, maps:get(N, Env)], Env};
	_ -> {bexp(E, Env) ++ [?LSET, maps:get(N, Env)], Env}
    end;
%% EXIT, not BYE: a word that halted the machine could not be called from
%% another word, which is what made leaf_mark return nothing at all. The
%% OUTERMOST exit -- an empty return stack -- is what ends a run.
%% VALUE DOWN, INDEX ON TOP -- Forth's order for `!`, and the order the two
%% sub-expressions fall out in anyway.
bstmt({store, {state, N}, V}, Env) ->
    {bexp(V, Env) ++ [?STS, {sid, N}], Env};
bstmt({store, {aget, A, E}, V}, Env) ->
    {bexp(V, Env) ++ bexp(E, Env) ++ [?ASET, {aid, A}], Env};
bstmt({store, T, V}, Env) ->
    {Rec, E, F} = case T of
		      {decl,  I, Fl} -> {decl,  I, Fl};
		      {instr, I, Fl} -> {instr, I, Fl};
		      {buf,   I, Fl} -> {buf,   I, Fl};
		      {view,  I, Fl} -> {view,  I, Fl}
		  end,
    {Tag, Op1, Op2} = stag(Rec),
    %% THE FIELD decides the width, not the value: storing a cell into a 32-bit
    %% field widens it, and storing a double into a narrow one drops the half
    %% that does not fit.
    {Op, Code} = case {cells(fwidth(Rec, F)), ewidth(V, Env)} of
		     {1, 1} -> {Op1, bexp(V, Env)};
		     {1, 2} -> {Op1, bexp(V, Env) ++ [?DROP]};
		     {2, 1} -> {Op2, bexp(V, Env) ++ [?S2D]};
		     {2, 2} -> {Op2, bexp(V, Env)}
		 end,
    {Code ++ bexp(E, Env) ++ [Op, {field, {Tag, F}}], Env};
bstmt({return, E}, Env) -> {bexp(E, Env) ++ [?EXIT], Env};
bstmt({do, E}, Env)     -> {bexp(E, Env) ++ [?DROP], Env};
bstmt({'if', C, T}, Env) ->
    {Tc, Env2} = bseq(T, Env),
    {bexp(C, Env) ++ [?JZ, rel(length(Tc))] ++ Tc, Env2};
bstmt({'if', C, T, F}, Env) ->
    {Tc, E1} = bseq(T, Env),
    {Fc, E2} = bseq(F, E1),
    {bexp(C, Env) ++ [?JZ, rel(length(Tc) + 2)] ++ Tc ++
	 [?JMP, rel(length(Fc))] ++ Fc, E2};
%% while: the condition is re-entered from the bottom, so the back jump spans
%% the whole loop. rel/1 refuses anything outside rel8 rather than wrapping --
%% a branch that silently lands somewhere else is the worst thing a code
%% generator can emit.
bstmt({while, C, B}, Env) ->
    {Bc, Env2} = bseq(B, Env),
    Cc = bexp(C, Env),
    Back = -(length(Cc) + 2 + length(Bc) + 2),
    {Cc ++ [?JZ, rel(length(Bc) + 2)] ++ Bc ++ [?JMP, rel(Back)], Env2};
%% switch: the subject is evaluated ONCE into a slot, then a chain of compares.
%% A jump table is what the C back end gets from gcc and this does not -- for
%% cold code that is the right side of the trade, and it keeps the bytecode
%% free of a second branch form.
bstmt({switch, E, Cases, Default}, Env) ->
    Tmp = maps:size(Env),
    Env1 = maps:put('$switch', Tmp, Env),
    {Arms, Env2} = lists:mapfoldl(fun({K, B}, Ev) ->
					  {Bc, Ev2} = bseq(B, Ev),
					  {{K, Bc}, Ev2}
				  end, Env1, Cases),
    {Dc, Env3} = bseq(Default, Env2),
    %% Every arm jumps to the end, so its trailing JMP has to know how much
    %% comes after it -- built back to front for exactly that reason.
    {Body, _} = lists:foldr(
		  fun({K, Bc}, {Acc, After}) ->
			  %% A case key is a C constant OR a plain number; only
			  %% the first needs a static assert that it fits.
			  KV = case is_integer(K) of
				   true  -> K;
				   false -> {cconst, K}
			       end,
			  Test = [?LGET, Tmp, ?LIT8, KV, ?EQ],
			  Arm = Test ++ [?JZ, rel(length(Bc) + 2)] ++ Bc
			      ++ [?JMP, rel(After)],
			  {Arm ++ Acc, After + length(Arm)}
		  end, {Dc, length(Dc)}, Arms),
    {bexp(E, Env) ++ [?LSET, Tmp] ++ Body, Env3}.

bseq(Stmts, Env) ->
    lists:foldl(fun(S, {C, E}) -> {C2, E2} = bstmt(S, E), {C ++ C2, E2} end,
		{[], Env}, Stmts).

rel(N) when N >= -128, N =< 127 -> N band 16#FF;
rel(N) -> io:format("branch out of rel8 range: ~p~n", [N]), halt(1).

bexp({const, N}, _) when is_integer(N), N =< 255 -> [?LIT8, N];
bexp({const, N}, _) when is_integer(N), N =< 65535 ->
    [?LIT16, N band 255, (N bsr 8) band 255];
bexp({const, N}, _) when is_integer(N) ->
    [?DLIT, N band 255, (N bsr 8) band 255,
	    (N bsr 16) band 255, (N bsr 24) band 255];
bexp({const, A}, _) -> [?LIT8, {cconst, A}];
%% The same, said explicitly for a constant that does not fit a byte. Which it
%% is cannot be worked out here -- the value lives in a C macro -- so the word
%% says so, and asserts() checks the claim at compile time either way.
bexp({const16, A}, _) -> [?LIT16, {cconst_lo, A}, {cconst_hi, A}];
%% A WIDE local is two slots, and pushing it pushes both -- low first, so the
%% high cell ends on top the way a double is held.
bexp({var, N}, Env) ->
    case get({w, N}) of
	2 -> [?LGET, maps:get(N, Env), ?LGET, maps:get({hi, N}, Env)];
	_ -> [?LGET, maps:get(N, Env)]
    end;
%% Shorthands for two state fields, not opcodes of their own: MC_ND used to be
%% `state id 0` written into the machine, which is a hardcoded index into a
%% GENERATED table -- it broke the moment the table stopped listing every field.
bexp({nd}, _) -> [?ST, {sid, nd}];
bexp({nn}, _) -> [?ST, {sid, nn}];
bexp({state, N}, _) -> [?ST, {sid, N}];
bexp({aget, A, E}, Env) -> bexp(E, Env) ++ [?AGET, {aid, A}];
%%bexp({buf, E, F}, Env) ->
%%    {leaf, I} = maps:get({buf, F}, get(ni)),
%%    bexp(E, Env) ++ [?NATIVE, I];
%% INDEX() masks the object bits off a packed index. It is a macro in C and an
%% AND here -- and leaving it out of the word is not a shortcut, it is a
%% different function.
bexp({index, E}, Env) -> bexp(E, Env) ++ [?LIT16, {cconst, 'INDEX_MASK_LO'},
					  {cconst, 'INDEX_MASK_HI'}, ?AND];
bexp({decl, E, F}, Env)  -> fld_rd(decl,  $D, ?DECL,  ?DECL2,  E, F, Env);
bexp({instr, E, F}, Env) -> fld_rd(instr, $I, ?INSTR, ?INSTR2, E, F, Env);
bexp({buf, E, F}, Env)   -> fld_rd(buf,   $B, ?BUF,   ?BUF2,   E, F, Env);
bexp({view, E, F}, Env)  -> fld_rd(view,  $V, ?VIEW,  ?VIEW2,  E, F, Env);

bexp({op, Op, A, B}, Env) ->
    %% Either side wide makes the whole operation wide, and the narrow side is
    %% widened to meet it.
    case max(ewidth(A, Env), ewidth(B, Env)) of
	1 -> bexp(A, Env) ++ bexp(B, Env) ++ [bop(Op)];
	2 -> wide(A, Env, bexp(A, Env)) ++ wide(B, Env, bexp(B, Env)) ++ [dop(Op)]
    end;
%% Short-circuit: if the left is false the result is already on the stack as 0
%% and the right is skipped. That is the whole reason andthen is its own node
%% and not {op, and_, ...} -- the C back end needs && and this needs a jump.
bexp({andthen, A, B}, Env) ->
    Bc = bexp(B, Env),
    %% Past the JZ operand lie DROP (one byte) and the right side, so the
    %% offset is len(Bc) + 1. Writing +2 there sent the branch one byte past
    %% the end of the word, which the cross-check caught as MC_E_BOUNDS.
    bexp(A, Env) ++ [?DUP, ?JZ, rel(length(Bc) + 1)] ++ [?DROP] ++ Bc;
%% The mirror of andthen: if the left is true the result is already on the
%% stack and the right is skipped. JZ jumps on zero, so the sense is inverted
%% with ZEQ rather than by adding a second branch opcode.
bexp({orthen, A, B}, Env) ->
    Bc = bexp(B, Env),
    bexp(A, Env) ++ [?DUP, ?ZEQ, ?JZ, rel(length(Bc) + 1)] ++ [?DROP] ++ Bc;
%% A word or a native, decided here rather than in the source: if the name is
%% one of the words in this file it is a CALL into the shared area, otherwise it
%% is an existing C function reached through its wrapper.
bexp({call, F, As}, Env) ->
    Args = lists:append([bexp(A, Env) || A <- As]),
    case maps:find(F, get(offs)) of
	{ok, _} ->
	    %% The CALLER's frame size travels with the call, because the
	    %% callee's entry is only an offset and nothing there knows how much
	    %% of the local array is already spoken for.
	    Args ++ [?CALL, {wlo, F}, {whi, F}, {frame, get(self)}];
	error ->
	    case maps:find({nat, F}, get(ni)) of
		{ok, {leaf, _}}  ->
		    case As of
			[] -> [?LIT8, 0, ?NATIVE, {lname, "LW_", F}];
			_  -> Args ++ [?NATIVE, {lname, "LW_", F}]
		    end;
		{ok, {leafn, _}} -> Args ++ [?NATIVEN, {lname, "LN_", F}];
		error ->
		    io:format("~s is neither a word nor a declared native.~n"
			      "  add {native, ~s, <args>, <ret>} to ~s~n",
			      [F, F, ?TERMS]),
		    halt(1)
	    end
    end.

bop(eq) -> ?EQ; bop(ne) -> ?NE; bop(lt) -> ?LT;
bop(add) -> ?ADD; bop(sub) -> ?SUB; bop(and_) -> ?AND; bop(or_) -> ?OR.
