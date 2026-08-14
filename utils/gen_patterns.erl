#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% Generate the pmatch patterns from utils/syntax.terms.
%%
%%   csp_pattern_ids.h   the PAT_* and STOP_* enums (included by csp_parse.h)
%%   csp_patterns.h      the capture structs, the pattern byte arrays and the
%%                       registration macro (included by csp_compile.c)
%%
%% THE POINT IS THE TWO NUMBERS NOBODY CAN CHECK BY HAND.
%%
%% P_OPT, P_ALT and P_REP each carry the BYTE LENGTH of their body, terminator
%% included. Get one wrong and nothing fails where it stands: the parser jumps
%% to the wrong place and it surfaces as a couple of dozen syntax errors
%% somewhere else entirely. There were 36 of them in the tree, hand-counted.
%%
%% A stop-set id may be BUILT ONLY ONCE. Two capture sites sharing an id leaves
%% the first reading the second's followers. Here ids are not written at all --
%% one is allocated per site, so the rule cannot be broken.
%%
%% The one deliberate exception is P_PAT: a sub-pattern used TWICE in the same
%% pattern shares one id, and that sharing is load-bearing. In pat_rule the
%% second PAT_BODY sits inside the repetition, so its own followers are only
%% {QUEST, NEWLINE} -- the COMMA that has to end its expression comes from the
%% FIRST site, through the merge scan_pattern_ does when a set is built twice.
%% Split them and `A=1, B=2, C=3` stops working. So: one id per (pattern,
%% sub-pattern) pair, which is exactly how the hand-written table had it.
%%
%%   escript utils/gen_patterns.erl emit
%%   escript utils/gen_patterns.erl check

-define(TERMS, "utils/syntax.terms").
-define(IDS,   "csp_pattern_ids.h").
-define(PATS,  "csp_patterns.h").

main(["emit"])  -> [ok = file:write_file(F, T) || {F, T} <- outputs()],
		   io:format("~s ~s~n", [?IDS, ?PATS]),
		   halt(0);
main(["check"]) -> check(outputs());
main(_) ->
    io:format("usage: gen_patterns.erl (emit | check)~n"),
    halt(2).

check([]) ->
    io:format("~s ~s: ok~n", [?IDS, ?PATS]),
    halt(0);
check([{File, Text} | Rest]) ->
    case file:read_file(File) of
	{ok, Bin} ->
	    case iolist_to_binary(Text) of
		Bin -> check(Rest);
		_   -> stale(File)
	    end;
	{error, _} -> stale(File)
    end.

stale(File) ->
    io:format("~s is out of date with ~s~n  run: make patterns~n", [File, ?TERMS]),
    halt(1).

%% ---------------------------------------------------------------------------

outputs() ->
    {ok, Terms} = file:consult(?TERMS),
    Structs = [{N, F} || {struct, N, F} <- Terms],
    Pats0   = [{N, S, I} || {pattern, N, S, I} <- Terms],
    Limits  = [{N, V} || {limit, N, V} <- Terms],
    Toks    = [{Sp, E} || {token, E, Sp, _, _, _, _, _} <- Terms, Sp =/= undefined],
    Pats    = order(Pats0),
    %% Encode every pattern once; that pass is what allocates the stop-set ids.
    {Encoded, Sids} =
	lists:foldl(fun(P, {Acc, S}) ->
			    {Rows, S1} = pattern(P, Structs, Pats, Toks, S),
			    {Acc ++ [{P, Rows}], S1}
		    end, {[], []}, Pats),
    [{?IDS,  ids(Pats, Sids)},
     {?PATS, pats(Encoded, Structs, Pats, Limits)}].

%% Sub-patterns must be REGISTERED before any pattern that refers to them:
%% collect_first follows a P_PAT into pattern[id], which scan_pattern fills in.
%% Depth-first over the {pat, ...} references, declaration order within a level.
order(Pats) -> lists:reverse(lists:foldl(fun(P, Acc) -> visit(P, Pats, Acc) end, [], Pats)).

visit({N, _, _} = P, Pats, Acc) ->
    case lists:keymember(N, 1, Acc) of
	true  -> Acc;
	false ->
	    Deps = refs(element(3, P)),
	    Acc1 = lists:foldl(fun(D, A) ->
				       case lists:keyfind(D, 1, Pats) of
					   false -> A;
					   Sub   -> visit(Sub, Pats, A)
				       end
			       end, Acc, Deps),
	    [P | Acc1]
    end.

refs(Items) -> lists:usort(lists:append([refs1(I) || I <- Items])).

refs1({pat, P})       -> [P];
refs1({pat, P, _})    -> [P];
refs1({opt, Is})      -> refs(Is);
refs1({choice, As})   -> refs(lists:append(As));
refs1({rep, _, _, Is})-> refs(Is);
refs1(_)              -> [].

%% ---------------------------------------------------------------------------
%% csp_pattern_ids.h
%% ---------------------------------------------------------------------------

ids(Pats, Sids) ->
    [banner(),
     "//\n"
     "// PAT_* is the order patterns are REGISTERED in, sub-patterns first.\n"
     "// STOP_* is one id per capture site, plus one per (pattern, sub-pattern)\n"
     "// pair. Nothing here is written by hand, which is the whole point.\n",
     "#ifndef __CSP_PATTERN_IDS_H__\n#define __CSP_PATTERN_IDS_H__\n\n",
     "enum {\n",
     [["    ", patid(N), ",\n"] || {N, _, _} <- Pats],
     "    NUM_PAT\n};\n\n",
     "enum {\n",
     "    STOP_NONE = 0,       // empty/invalid placeholder\n",
     "    STOP_OPTS = 1,       // fixed set of all OPTION tokens (init_stop_sets)\n",
     [["    ", S, ",\n"] || S <- Sids],
     "    NUM_STOP_SETS\n};\n\n",
     "#endif\n"].

%% ---------------------------------------------------------------------------
%% csp_patterns.h
%% ---------------------------------------------------------------------------

pats(Encoded, Structs, Pats, Limits) ->
    [banner(),
     "//\n"
     "// Capture structs and pattern byte arrays. Every byte length here was\n"
     "// computed, not counted. See utils/syntax.terms for what each pattern\n"
     "// means and why it is shaped the way it is.\n",
     "#ifndef __CSP_PATTERNS_H__\n#define __CSP_PATTERNS_H__\n\n",
     [[io_lib:format("#define ~s ~s~n", [N, val(V)])] || {N, V} <- Limits],
     "\n",
     [struct(S, Structs, Pats) || S <- used_structs(Encoded, Structs, Pats)],
     [["\nstatic const uint8_t ", pname(N), "[] = {\n", rows(Rows), "};\n"]
      || {{N, _, _}, Rows} <- Encoded],
     "\n// Registration, in dependency order: a sub-pattern must be in\n"
     "// pattern[] before collect_first follows a P_PAT into it.\n",
     "#define CSP_SCAN_PATTERNS \\\n",
     join([io_lib:format("    scan_pattern(~s, ~s);", [patid(N), pname(N)])
	   || {{N, _, _}, _} <- Encoded], " \\\n"),
     "\n\n#endif\n"].

val(V) when is_integer(V) -> integer_to_list(V);
val(V) when is_atom(V)    -> atom_to_list(V).

%% Structs in dependency order too: a struct with a {pat, P} field embeds the
%% struct that pattern fills, so that one has to be complete first.
used_structs(Encoded, Structs, Pats) ->
    Order = [S || {{_, S, _}, _} <- Encoded],
    lists:foldl(fun(S, Acc) -> sdep(S, Structs, Pats, Acc) end, [], Order).

sdep(S, Structs, Pats, Acc) ->
    case lists:member(S, Acc) of
	true  -> Acc;
	false ->
	    {S, Fields} = lists:keyfind(S, 1, Structs),
	    Acc1 = lists:foldl(
		     fun({_F, {pat, P}}, A) -> sdep(struct_of(P, Pats), Structs, Pats, A);
			({_F, {array, E, _}}, A) -> sdep(E, Structs, Pats, A);
			(_, A) -> A
		     end, Acc, Fields),
	    Acc1 ++ [S]
    end.

%% A {pat, P} field holds whatever PATTERN P captures into, so the field's type
%% is that pattern's struct -- `{r, {pat, res}}` is a res_param_t.
struct_of(P, Pats) ->
    {P, S, _} = lists:keyfind(P, 1, Pats),
    S.

struct(S, Structs, Pats) ->
    {S, Fields} = lists:keyfind(S, 1, Structs),
    ["typedef struct {\n",
     [["    ", ctype(T, Pats), " ", fname(F, T), ";\n"] || {F, T} <- Fields],
     "} ", ctname(S), ";\n"].

fname(F, {array, _, Max}) -> [atom_to_list(F), "[", val(Max), "]"];
fname(F, _)               -> atom_to_list(F).

ctype(str, _)           -> "tstr_t";
ctype(int, _)           -> "ivalue_t";
ctype(tok, _)           -> "ivalue_t";
ctype(expr, _)          -> "pexpr_t";
ctype(const, _)         -> "value_t";
ctype(opts, _)          -> "decl_opts_t";
ctype({pat, P}, Pats)   -> ctname(struct_of(P, Pats));
ctype({array, E, _}, _) -> ctname(E);
ctype({plain, C}, _)    -> C.

%% A struct named for the PATTERN it belongs to, not for the pattern's own name:
%% `pattern res` fills `res_param`, and the terms say so.
ctname(S) -> [atom_to_list(S), "_t"].

patid(N) -> ["PAT_", string:uppercase(atom_to_list(N))].
pname(N) -> ["pat_", atom_to_list(N)].

%% ---------------------------------------------------------------------------
%% Encoding
%%
%% enc/N returns {Rows, Sids}: a row is {Indent, [Item]} and an Item is one
%% pattern BYTE, written as the C token that produces it. A construct's length
%% is the number of items under it -- which is why nothing has to be counted.
%% ---------------------------------------------------------------------------

pattern({N, S, Items}, Structs, Pats, Toks, Sids) ->
    Ctx = #{pat => N, struct => S, structs => Structs, pats => Pats, toks => Toks},
    {Rows, Sids1} = enc_list(Items, Ctx, 1, Sids),
    {Rows ++ [{1, ["P_END"]}], Sids1}.

enc_list(Items, Ctx, Ind, Sids) ->
    lists:foldl(fun(I, {Acc, S}) ->
			{R, S1} = enc(I, Ctx, Ind, S),
			{Acc ++ R, S1}
		end, {[], Sids}, Items).

enc({tok, Lit}, Ctx, Ind, S) ->
    {[{Ind, ["P_TOK", tok(Lit, Ctx)]}], S};
enc({tok, F, Lit}, Ctx, Ind, S) ->
    {[{Ind, ["P_TOK_W", tok(Lit, Ctx), off(F, Ctx)]}], S};
enc({str, F}, Ctx, Ind, S) ->
    {[{Ind, ["P_STR", off(F, Ctx)]}], S};
enc({int, F}, Ctx, Ind, S) ->
    {Sid, S1} = sid(F, Ctx, S),
    {[{Ind, ["P_INTEGER_S", off(F, Ctx), Sid]}], S1};
enc({expr, F}, Ctx, Ind, S) ->
    {Sid, S1} = sid(F, Ctx, S),
    {[{Ind, ["P_EXPR_S", off(F, Ctx), Sid]}], S1};
enc({const, VtF, F}, Ctx, Ind, S) ->
    {Sid, S1} = sid(F, Ctx, S),
    {[{Ind, ["P_CONST_S", off(VtF, Ctx), off(F, Ctx), Sid]}], S1};
enc({opts, F}, Ctx, Ind, S) ->
    {[{Ind, ["P_OPTS", off(F, Ctx)]}], S};
enc({pat, P}, Ctx, Ind, S) ->
    {Sid, S1} = cont_sid(P, Ctx, S),
    {[{Ind, ["P_PAT", patid(P), "0", Sid]}], S1};
enc({pat, P, F}, Ctx, Ind, S) ->
    {Sid, S1} = cont_sid(P, Ctx, S),
    {[{Ind, ["P_PAT", patid(P), off(F, Ctx), Sid]}], S1};
enc({opt, Items}, Ctx, Ind, S) ->
    {Body0, S1} = enc_list(Items, Ctx, Ind + 1, S),
    Body = Body0 ++ [{Ind + 1, ["P_OPT_END"]}],
    {[{Ind, ["P_OPT", len(Body)]} | Body], S1};
enc({choice, Alts}, Ctx, Ind, S) ->
    {Body, S1} =
	lists:foldl(fun(A, {Acc, St}) ->
			    {B0, St1} = enc_list(A, Ctx, Ind + 2, St),
			    B = B0 ++ [{Ind + 2, ["P_ALT_END"]}],
			    {Acc ++ [{Ind + 1, ["P_ALT", len(B)]} | B], St1}
		    end, {[], S}, Alts),
    {[{Ind, ["P_CHOICE", integer_to_list(length(Alts))]}] ++ Body
     ++ [{Ind, ["P_CHOICE_END"]}], S1};
enc({rep, F, From, Items}, Ctx, Ind, S) ->
    {Body0, S1} = enc_list(Items, Ctx, Ind + 1, S),
    Body = [{Ind + 1, ["P_ARRAY", aoff(F, From, Ctx), esize(F, Ctx)]}]
	   ++ Body0 ++ [{Ind + 1, ["P_REP_END"]}],
    {[{Ind, ["P_REP", len(Body)]} | Body], S1}.

len(Rows) -> integer_to_list(lists:sum([length(Is) || {_, Is} <- Rows])).

%% --- names ---

tok(Lit, #{toks := Toks}) ->
    case lists:keyfind(Lit, 1, Toks) of
	{Lit, Enum} -> atom_to_list(Enum);
	false       -> erlang:error({unknown_literal, Lit})
    end.

off(F, #{struct := S}) -> ["csp_offsetof(", ctname(S), ", ", atom_to_list(F), ")"].

aoff(F, 0, #{struct := S}) ->
    ["csp_offsetof(", ctname(S), ", ", atom_to_list(F), ")"];
aoff(F, N, #{struct := S}) ->
    ["csp_offsetof(", ctname(S), ", ", atom_to_list(F), "[", integer_to_list(N), "])"].

esize(F, #{struct := S, structs := Structs}) ->
    {S, Fields} = lists:keyfind(S, 1, Structs),
    {F, {array, E, _}} = lists:keyfind(F, 1, Fields),
    ["sizeof(", ctname(E), ")"].

%% One id per capture SITE. The field name makes it readable; a field captured
%% more than once in one pattern (pin_item's `hi`, from three alternatives) gets
%% a numbered suffix rather than sharing -- sharing is the bug this prevents.
sid(F, #{pat := P}, Sids) ->
    Base = "STOP_" ++ string:uppercase(atom_to_list(P) ++ "_" ++ atom_to_list(F)),
    Name = unique(Base, Sids, 1),
    {Name, Sids ++ [Name]}.

unique(Base, Sids, N) ->
    Try = case N of 1 -> Base; _ -> Base ++ "_" ++ integer_to_list(N) end,
    case lists:member(Try, Sids) of
	true  -> unique(Base, Sids, N + 1);
	false -> Try
    end.

%% One id per (pattern, sub-pattern) PAIR -- reused when the same sub-pattern
%% appears twice, so scan_pattern_ merges the followers of both sites. See the
%% file header: pat_rule depends on that merge.
cont_sid(Sub, #{pat := P}, Sids) ->
    Name = "STOP_" ++ string:uppercase(atom_to_list(P) ++ "_" ++ atom_to_list(Sub))
	   ++ "_CONT",
    case lists:member(Name, Sids) of
	true  -> {Name, Sids};
	false -> {Name, Sids ++ [Name]}
    end.

%% --- text ---

rows(Rows) ->
    [[lists:duplicate(4 * Ind, $\s), join(Is, ", "), ",\n"] || {Ind, Is} <- Rows].

join([], _)      -> [];
join([X], _)     -> [X];
join([X | Xs], Sep) -> [X, Sep | join(Xs, Sep)].

banner() ->
    ["// generated by utils/gen_patterns.erl from ", ?TERMS, " -- do not edit\n"].
