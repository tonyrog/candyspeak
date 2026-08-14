#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% Validate utils/syntax.terms.
%%
%% Everything here is a check a GENERATOR would have to make anyway before it
%% could emit anything, so this is the front half of that generator: read the
%% terms, resolve every cross-reference, and complain about what does not line
%% up.  Running it is how a hand edit to syntax.terms gets caught before it
%% turns into a byte pattern that matches the wrong thing.
%%
%%   escript utils/syntax_check.erl [utils/syntax.terms]

main(Args) ->
    File = case Args of
	       [F] -> F;
	       []  -> "utils/syntax.terms"
	   end,
    case file:consult(File) of
	{ok, Terms} ->
	    Errs = check(Terms),
	    report(File, Terms, Errs);
	{error, Reason} ->
	    io:format("~s: ~p~n", [File, Reason]),
	    halt(1)
    end.

report(File, Terms, []) ->
    io:format("~s: ok -- ~p terms, ~p patterns, ~p structs~n",
	      [File, length(Terms),
	       length([P || {pattern,_,_,_} = P <- Terms]),
	       length([S || {struct,_,_} = S <- Terms])]),
    halt(0);
report(File, _Terms, Errs) ->
    io:format("~s: ~p problem(s)~n", [File, length(Errs)]),
    [io:format("  ~s~n", [E]) || E <- lists:reverse(Errs)],
    halt(1).

%% ---------------------------------------------------------------------------

check(Terms) ->
    Structs  = [{N, F} || {struct, N, F} <- Terms],
    Patterns = [{N, S, I} || {pattern, N, S, I} <- Terms],
    %% Every spelled token, by its spelling: that is what a pattern writes.
    Tokens   = [{Sp, Enum} || {token, Enum, Sp, _S, _C, _A, _P, _As} <- Terms,
			      Sp =/= undefined],
    Errs0    = dups("struct", [N || {N,_} <- Structs], []),
    Errs1    = dups("pattern", [N || {N,_,_} <- Patterns], Errs0),
    Errs2    = lists:foldl(fun(P, A) -> pattern(P, Structs, Patterns, Tokens, A) end,
			   Errs1, Patterns),
    Errs3    = lists:foldl(fun(T, A) -> defaults(T, Structs, Patterns, A) end,
			   Errs2, Terms),
    lists:foldl(fun(T, A) -> decls(T, Patterns, A) end, Errs3, Terms).

dups(What, Names, Errs) ->
    lists:foldl(
      fun(N, A) ->
	      case length([X || X <- Names, X =:= N]) of
		  1 -> A;
		  _ -> [f("~s ~p declared more than once", [What, N]) | A]
	      end
      end, Errs, lists:usort(Names)).

%% --- one pattern -----------------------------------------------------------

pattern({Name, SName, Items}, Structs, Patterns, Tokens, Errs) ->
    case lists:keyfind(SName, 1, Structs) of
	false ->
	    [f("pattern ~p: no struct ~p", [Name, SName]) | Errs];
	{SName, Fields} ->
	    Ctx = {Name, SName, Fields, Structs, Patterns, Tokens},
	    items(Items, Ctx, Errs)
    end.

items(Items, Ctx, Errs) -> lists:foldl(fun(I, A) -> item(I, Ctx, A) end, Errs, Items).

item({tok, Lit}, Ctx, Errs) ->
    token(Lit, Ctx, Errs);
item({tok, Field, Lit}, Ctx, Errs) ->
    field(Field, tok, Ctx, token(Lit, Ctx, Errs));
item({str, Field}, Ctx, Errs)  -> field(Field, str, Ctx, Errs);
item({int, Field}, Ctx, Errs)  -> field(Field, int, Ctx, Errs);
item({expr, Field}, Ctx, Errs) -> field(Field, expr, Ctx, Errs);
item({opts, Field}, Ctx, Errs) -> field(Field, opts, Ctx, Errs);
item({const, VtField, Field}, Ctx, Errs) ->
    %% The vt field is an OPTS capture read back as an input, not a value_t.
    field(Field, const, Ctx, field(VtField, opts, Ctx, Errs));
item({opt, Items}, Ctx, Errs) ->
    items(Items, Ctx, Errs);
item({choice, Alts}, Ctx, Errs) ->
    lists:foldl(fun(Alt, A) -> items(Alt, Ctx, A) end, Errs, Alts);

%% {pat, P, F}: F must be declared as that pattern's struct, or as an array of
%% it (a repetition writes element 0 through the same field).
item({pat, P, Field}, Ctx, Errs) ->
    {Name, _S, Fields, Structs, Patterns, _T} = Ctx,
    Errs1 = subpattern(P, Ctx, Errs),
    case {lists:keyfind(P, 1, Patterns), lists:keyfind(Field, 1, Fields)} of
	{{P, PS, _}, {Field, {pat, P}}} ->
	    struct_exists(PS, Structs, Name, Errs1);
	{{P, PS, _}, {Field, {array, PS, _}}} ->
	    Errs1;
	{_, false} ->
	    [f("pattern ~p: field ~p not in its struct", [Name, Field]) | Errs1];
	{{P, PS, _}, {Field, Other}} ->
	    [f("pattern ~p: field ~p is ~p, but ~p fills ~p",
	       [Name, Field, Other, P, PS]) | Errs1];
	_ ->
	    Errs1
    end;
item({pat, P}, Ctx, Errs) ->
    subpattern(P, Ctx, Errs);

%% {rep, F, From, Items}: F must be an array whose element struct is what the
%% repeated sub-pattern fills, and From must be inside a plausible range.
item({rep, Field, From, Items}, Ctx, Errs) ->
    {Name, _S, Fields, _Structs, Patterns, _T} = Ctx,
    Errs1 = items(Items, Ctx, Errs),
    Errs2 = case is_integer(From) andalso From >= 0 of
		true  -> Errs1;
		false -> [f("pattern ~p: rep ~p start ~p is not an index",
			    [Name, Field, From]) | Errs1]
	    end,
    case lists:keyfind(Field, 1, Fields) of
	{Field, {array, ES, _Max}} ->
	    Inner = [P || {pat, P} <- Items] ++ [P || {pat, P, _} <- Items],
	    lists:foldl(
	      fun(P, A) ->
		      case lists:keyfind(P, 1, Patterns) of
			  {P, ES, _} -> A;
			  {P, PS, _} ->
			      [f("pattern ~p: rep ~p holds ~p, but ~p fills ~p",
				 [Name, Field, ES, P, PS]) | A];
			  false -> A          % reported by subpattern/3
		      end
	      end, Errs2, Inner);
	{Field, Other} ->
	    [f("pattern ~p: rep ~p is ~p, not an array", [Name, Field, Other]) | Errs2];
	false ->
	    [f("pattern ~p: field ~p not in its struct", [Name, Field]) | Errs2]
    end;
item(Other, {Name,_,_,_,_,_}, Errs) ->
    [f("pattern ~p: unknown item ~p", [Name, Other]) | Errs].

subpattern(P, {Name, _S, _F, _Structs, Patterns, _T}, Errs) ->
    case lists:keyfind(P, 1, Patterns) of
	false -> [f("pattern ~p: refers to undeclared pattern ~p", [Name, P]) | Errs];
	_     -> Errs
    end.

struct_exists(S, Structs, Name, Errs) ->
    case lists:keyfind(S, 1, Structs) of
	false -> [f("pattern ~p: no struct ~p", [Name, S]) | Errs];
	_     -> Errs
    end.

%% A capture's field must exist and its declared type must be the capture kind.
field(Field, Kind, {Name, SName, Fields, _St, _P, _T}, Errs) ->
    case lists:keyfind(Field, 1, Fields) of
	{Field, Kind} ->
	    Errs;
	{Field, Other} ->
	    [f("pattern ~p: ~p captures ~p into field of type ~p",
	       [Name, Kind, Field, Other]) | Errs];
	false ->
	    [f("pattern ~p: field ~p not in struct ~p", [Name, Field, SName]) | Errs]
    end.

%% A literal is either punctuation from the token table or a reserved word --
%% `can` in #buffer is matched as a token and spelled by its reserved word.
token(Lit, {Name, _S, _F, _St, _P, Tokens}, Errs) ->
    case lists:keyfind(Lit, 1, Tokens) of
	false -> [f("pattern ~p: literal ~p is neither a token nor a reserved word",
		    [Name, Lit]) | Errs];
	_     -> Errs
    end.

%% --- defaults --------------------------------------------------------------

defaults({defaults, PName, List}, Structs, Patterns, Errs) ->
    case lists:keyfind(PName, 1, Patterns) of
	false ->
	    [f("defaults ~p: no such pattern", [PName]) | Errs];
	{PName, SName, _} ->
	    Ctx = {PName, Structs, Patterns},
	    lists:foldl(fun({Path, _V}, A) -> path(SName, Path, Ctx, A);
			   (Bad, A) -> [f("defaults ~p: bad entry ~p", [PName, Bad]) | A]
			end, Errs, List)
    end;
defaults(_, _, _, Errs) -> Errs.

%% A default path walks into sub-pattern structs: [r, res] is the `res` field of
%% whatever struct the sub-pattern in field `r` fills.  `opts` is opaque here --
%% decl_opts_t is hand-written C and its members are not declared in this file --
%% so a path into it stops at the opts field.
path(SName, Path, {PName, Structs, _} = Ctx, Errs) ->
    case lists:keyfind(SName, 1, Structs) of
	false -> [f("defaults ~p: no struct ~p", [PName, SName]) | Errs];
	{SName, Fields} -> path_step(SName, Path, Fields, Ctx, Errs)
    end.

path_step(SName, [F], Fields, {PName, _, _}, Errs) ->
    case lists:keyfind(F, 1, Fields) of
	false -> [f("defaults ~p: ~p has no field ~p", [PName, SName, F]) | Errs];
	_     -> Errs
    end;
path_step(SName, [F | Rest], Fields, {PName, _St, Patterns} = Ctx, Errs) ->
    case lists:keyfind(F, 1, Fields) of
	{F, opts} ->
	    Errs;
	{F, {pat, P}} ->
	    case lists:keyfind(P, 1, Patterns) of
		{P, PS, _} -> path(PS, Rest, Ctx, Errs);
		false -> [f("defaults ~p: no pattern ~p", [PName, P]) | Errs]
	    end;
	{F, Other} ->
	    [f("defaults ~p: ~p.~p is ~p, cannot descend", [PName, SName, F, Other]) | Errs];
	false ->
	    [f("defaults ~p: ~p has no field ~p", [PName, SName, F]) | Errs]
    end.

%% --- declaration keywords --------------------------------------------------

decls({decl, _D, _Sp, _S, _C, hand_written}, _Patterns, Errs) -> Errs;
decls({decl, _D, _Sp, _S, _C, undefined}, _Patterns, Errs) -> Errs;
decls({decl, D, _Sp, _S, _C, P}, Patterns, Errs) ->
    case lists:keyfind(P, 1, Patterns) of
	false -> [f("declaration ~p: no pattern ~p", [D, P]) | Errs];
	_     -> Errs
    end;
decls(_, _, Errs) -> Errs.

f(Fmt, Args) -> lists:flatten(io_lib:format(Fmt, Args)).
