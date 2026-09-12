#!/usr/bin/env escript
%% -*- erlang -*-
%%
%% Generate the record accessors from utils/layout.terms.
%%
%%   gen/csp_layout.h         accessors (RAM and ROM, get and set), the MF_*
%%                            ids, the mc_field_t table, the layout fingerprint
%%   gen/csp_layout_oracle.h  a probe that compares every accessor against the
%%                            C bit-field of the same name -- included by the
%%                            test only, and the bridge that makes the cutover
%%                            safe: the existing struct is the oracle until
%%                            nothing reads it any more
%%
%% THE NUMBER NOBODY CAN CHECK BY HAND is the shift.  A field's bit position is
%% the running sum of every width before it, and a wrong one does not fail where
%% it stands -- it returns a plausible value from the neighbouring field.  That
%% is exactly what mc_decl_fields[] restated by hand, and why the sweep in
%% tests/mcsp.c had to exist.  Computed here, it cannot be wrong; the oracle
%% then says whether the DESCRIPTION matches the struct.
%%
%% THE IDIOM MATTERS.  For a field spanning two bytes, measured on avr-gcc -Os:
%%
%%     (uint16_t)b[i] | ((uint16_t)b[j] << 8), masked      14 bytes
%%     gcc's own bit-field read                            16
%%     ((b[j] & 1) << 8) | b[i]                            26
%%
%% The shift must happen in the WIDE type.  Written the other way gcc emits a
%% byte swap through eor/mov instead of just placing the register.  Hundreds of
%% sites will carry whichever form this file picks.
%%
%%   escript utils/gen_layout.erl emit
%%   escript utils/gen_layout.erl check

-mode(compile).

-define(TERMS,  "utils/layout.terms").
-define(HDR,    "gen/csp_layout.h").
-define(ORACLE, "gen/csp_layout_oracle.h").

main(["emit"]) ->
    [ok = file:write_file(F, T) || {F, T} <- outputs()],
    io:format("~s ~s~n", [?HDR, ?ORACLE]),
    halt(0);
main(["check"]) ->
    check(outputs());
main(_) ->
    io:format("usage: gen_layout.erl (emit | check)~n"),
    halt(2).

check([]) ->
    io:format("~s ~s: ok~n", [?HDR, ?ORACLE]),
    halt(0);
check([{File, Text} | Rest]) ->
    %% Text is an iolist and file:read_file hands back a binary, so it has to be
    %% flattened before the compare -- otherwise check() reports every file as
    %% out of date, which reads like a stale tree and is really a type error.
    Want = iolist_to_binary(Text),
    case file:read_file(File) of
	{ok, Want} -> check(Rest);
	_ ->
	    io:format("~s is out of date with ~s~n  run: make layout~n",
		      [File, ?TERMS]),
	    halt(1)
    end.

outputs() ->
    {ok, Terms} = file:consult(?TERMS),
    %% A family is the C type an accessor takes and the name it is spelled
    %% with. Without it every accessor would be csp_decl_*, which was fine
    %% while declarations were the only record described here.
    Fam = maps:from_list([{N, {CT, P}} || {family, N, CT, P} <- Terms]),
    Base = [lay(R) || R = {record, _, _, _} <- Terms],
    Arms = [arm(A, Base) || A = {arm, _, _, _, _} <- Terms],
    Laid = [{N, B, T, F, info(N, Terms, Fam)} || {N, B, T, F} <- Base ++ Arms],
    [{?HDR, header(Laid)}, {?ORACLE, oracle(Laid)}].

%% {CType, AccessorPrefix, IsBase, FamilyName}. A base record names its own
%% family; an arm inherits its base's, and is not a base -- which is what
%% decides whether its fields are spelled bare (shared by every arm) or with
%% the arm's own name in front.
%% An ARM's identity is {Base, Name}, not Name: `em` is a terminator in both
%% families, and two records with one identity collide in the enums, the oracle
%% macros and the field table.
info({Base, _Arm}, _Terms, Fam) ->
    {CT, P} = maps:get(Base, Fam),
    {CT, P, false, Base};
info(Name, _Terms, Fam) ->
    {CT, P} = maps:get(Name, Fam),
    {CT, P, true, Name}.

%% What the record is called in a comment, an enum or an oracle macro.
idname({Base, Arm}) -> atom_to_list(Base) ++ "_" ++ atom_to_list(Arm);
idname(Name)        -> atom_to_list(Name).

%% --------------------------------------------------------------- bit layout

%% Assign each field its bit position, then derive the bytes it touches.
lay({record, Name, Bytes, Fields}) ->
    {Laid, Total} = lists:mapfoldl(fun place/2, 0, Fields),
    case Total > Bytes * 8 of
	true ->
	    io:format("~s: fields total ~p bits, record is ~p bytes~n",
		      [Name, Total, Bytes]),
	    halt(1);
	false -> ok
    end,
    {Name, Bytes, Total, [F || F <- Laid, element(1, F) =/= pad]}.

%% An arm's fields begin where its base record's end, so the positions come out
%% absolute and every accessor indexes from the start of the declaration -- the
%% caller never has to know an arm is an arm.
arm({arm, Prefix, Base, Bytes, Fields}, Laid) ->
    {_, _, BaseBits, _} = lists:keyfind(Base, 1, Laid),
    %% {start, N} says this arm begins somewhere OTHER than where its base ends
    %% -- csp_states_t sits on DECL_HEADER's 17 bits and deliberately overlaps
    %% what DECL_TYPE_HEADER adds. Written out because that overlap is
    %% load-bearing and has cost three bugs when it was implied instead.
    {Start, Rest} = case Fields of
			[{start, N} | T] -> {N, T};
			_                -> {BaseBits, Fields}
		    end,
    {Placed, Total} = lists:mapfoldl(fun place/2, Start, Rest),
    case Total > Bytes * 8 of
	true ->
	    io:format("arm ~s: fields end at bit ~p, record is ~p bytes~n",
		      [Prefix, Total, Bytes]),
	    halt(1);
	false -> ok
    end,
    {{Base, Prefix}, Bytes, Total, [F || F <- Placed, element(1, F) =/= pad]}.

place({pad, Bits}, Pos)        -> {{pad, Pos, Bits, none}, Pos + Bits};
place({FName, Bits}, Pos)      -> {{FName, Pos, Bits, none}, Pos + Bits};
place({FName, Bits, CT}, Pos)  -> {{FName, Pos, Bits, CT}, Pos + Bits}.

%% Bytes a field touches, and the shift within the first of them.
span(Pos, Bits) ->
    Lo = Pos div 8,
    Sh = Pos rem 8,
    N  = (Sh + Bits + 7) div 8,
    {Lo, Sh, N}.

%% --------------------------------------------------------------- the header

header(Laid) ->
    [banner(),
     "#ifndef __CSP_LAYOUT_H__\n#define __CSP_LAYOUT_H__\n\n",
     "#include <stdint.h>\n\n",
     "// The accessors take the record's own type, not void*. A uint8_t*\n"
     "// aliases EVERY object, so with a void* parameter gcc has to assume a\n"
     "// field write may have touched st->ram_decl or st->rom_nd and reload both\n"
     "// after every setter -- 2 KB across the write path before it was measured.\n"
     "//\n"
     "// ro_byte is the ONLY way to read a byte that may be in flash, so the _ro\n"
     "// halves are generated with it and a ROM read cannot be written as a plain\n"
     "// dereference by accident. Include this AFTER csp.h.\n\n",
     [rec(R) || R <- Laid],
     [family_fields(F, Laid) || F <- families(Laid)],
     fingerprint(Laid),
     "#endif\n"].

banner() ->
    ["// generated by utils/gen_layout.erl from utils/layout.terms -- do not edit\n"
     "//\n"
     "// One description, several readers: the C accessors here, the mc_field_t\n"
     "// tables micro-csp reads the same fields by, and the oracle the test\n"
     "// compares both against. An accessor nothing references is not emitted,\n"
     "// so generating the whole cross product costs nothing in the binary.\n\n"].

families(Laid) -> lists:usort([F || {_, _, _, _, {_, _, _, F}} <- Laid]).

%% A base record's fields are shared by every arm, so they are spelled bare --
%% csp_decl_get_type. An arm's carry the arm name, so the generated name and the
%% C spelling are the same word: csp_decl_get_md_n for what the union calls
%% .md.n.
nm(_Id, {_CT, P, true, _})        -> {P, ""};
nm({_B, Arm}, {_CT, P, false, _}) -> {P, atom_to_list(Arm) ++ "_"}.

rec({Id, Bytes, Total, Fields, Info}) ->
    N = idname(Id),
    {Pre, Arm} = nm(Id, Info),
    {CT, _, _, _} = Info,
    [io_lib:format("// ~s: ends at bit ~p of ~p bytes\n", [N, Total, Bytes]),
     straddle_note(Fields),
     [acc(Pre, Arm, atom_to_list(CT), F) || F <- Fields],
     "\n"].

%% Fields crossing a byte boundary cost ~2 bytes at every read site on AVR over
%% a byte-aligned slot. Said out loud in the generated file, which is where
%% someone weighing that trade will be looking.
straddle_note(Fields) ->
    Bad = [F || F = {_, P, B, _} <- Fields, element(3, span(P, B)) > 1],
    case Bad of
	[] -> "";
	_  -> ["// STRADDLING: ",
	       string:join([atom_to_list(element(1, F)) || F <- Bad], ", "), "\n"]
    end.

%% A cell is 16 bits, but a payload is not: cn.init is 32 and tm.period 28. The
%% C type follows the WIDTH so the accessor truncates nothing; micro-csp's
%% MC_DECL is where the 16-bit limit belongs, because that is where the cell is.
ctype(B, false) when B =< 8  -> "uint8_t";
ctype(B, false) when B =< 16 -> "uint16_t";
ctype(_, false)              -> "uint32_t";
ctype(B, true) when B =< 8   -> "int8_t";
ctype(B, true) when B =< 16  -> "int16_t";
ctype(_, true)               -> "int32_t".

%% 'value_t.u' -> {"value_t", "u"}: the C type is a union, and the accessor and
%% the oracle both go through that member. The members alias, so a caller
%% reading .i or .f sees the same four bytes.
umember(signed) -> none;
umember(none)   -> none;
umember(CT) ->
    case string:split(atom_to_list(CT), ".") of
	[T, M] when M =/= [] -> {T, M};
	_                    -> none
    end.

is_signed(signed) -> true;
is_signed(_)      -> false.

acc(Pre, Arm, RecCT, {FName, Pos, Bits, CT}) ->
    F = atom_to_list(FName),
    {Lo, Sh, N} = span(Pos, Bits),
    Mask = (1 bsl Bits) - 1,
    case umember(CT) of
	{U, UM} -> acc_union(Pre, Arm, RecCT, F, Lo, U, UM);
	none    -> acc_plain(Pre, Arm, RecCT, F, Lo, Sh, N, Bits, Mask,
			     ctype(Bits, is_signed(CT)), is_signed(CT))
    end.

%% Sign extension is the generator's job, not the caller's. A signed bit-field
%% read as unsigned and then used is a large positive number that looks like a
%% plausible index -- instr carries four of them (imm, nxt) and every one is a
%% jump target or an immediate.
sext(_Bits, false, Expr, Ty) -> io_lib:format("(~s)(~s)", [Ty, Expr]);
sext(Bits, true, Expr, Ty) ->
    io_lib:format("(~s)(((uint32_t)(~s) ^ 0x~.16BUL) - 0x~.16BUL)",
		  [Ty, Expr, 1 bsl (Bits - 1), 1 bsl (Bits - 1)]).

acc_plain(Pre, Arm, RecCT, F, Lo, Sh, N, Bits, Mask, Ty, Sg) ->
    [io_lib:format("static inline ~s ~sget_~s~s(const ~s* p)\n{\n",
		   [Ty, Pre, Arm, F, RecCT]),
     "    const uint8_t* b_ = (const uint8_t*)p;\n",
     io_lib:format("    return ~s;\n",
		   [sext(Bits, Sg, io_lib:format("(~s) & 0x~.16BU",
						 [load(N, Lo, Sh, Ty), Mask]), Ty)]),
     "}\n",
     io_lib:format("static inline ~s ~sget_~s~s_ro(const ~s* p)\n{\n",
		   [Ty, Pre, Arm, F, RecCT]),
     "    const uint8_t* b_ = (const uint8_t*)p;\n",
     io_lib:format("    return ~s;\n",
		   [sext(Bits, Sg, io_lib:format("(~s) & 0x~.16BU",
						 [load_ro(N, Lo, Sh, Ty), Mask]), Ty)]),
     "}\n",
     io_lib:format("static inline void ~sset_~s~s(~s* p, ~s v_)\n{\n",
		   [Pre, Arm, F, RecCT, Ty]),
     "    uint8_t* b_ = (uint8_t*)p;\n",
     [set_byte(Lo + K, byte_mask(Mask, Sh, K), contrib(Sh, K)) || K <- lists:seq(0, N - 1)],
     "}\n"].

acc_union(Pre, Arm, RecCT, F, Lo, U, UM) ->
    [io_lib:format("static inline ~s ~sget_~s~s(const ~s* p)\n{\n", [U, Pre, Arm, F, RecCT]),
     "    const uint8_t* b_ = (const uint8_t*)p;\n",
     io_lib:format("    ~s v_;\n    v_.~s = (uint32_t)b_[~p] | ((uint32_t)b_[~p] << 8)\n"
		   "\t   | ((uint32_t)b_[~p] << 16) | ((uint32_t)b_[~p] << 24);\n"
		   "    return v_;\n}\n", [U, UM, Lo, Lo + 1, Lo + 2, Lo + 3]),
     io_lib:format("static inline ~s ~sget_~s~s_ro(const ~s* p)\n{\n", [U, Pre, Arm, F, RecCT]),
     "    const uint8_t* b_ = (const uint8_t*)p;\n",
     io_lib:format("    ~s v_;\n    v_.~s = (uint32_t)ro_byte(&b_[~p])"
		   " | ((uint32_t)ro_byte(&b_[~p]) << 8)\n"
		   "\t   | ((uint32_t)ro_byte(&b_[~p]) << 16)"
		   " | ((uint32_t)ro_byte(&b_[~p]) << 24);\n"
		   "    return v_;\n}\n", [U, UM, Lo, Lo + 1, Lo + 2, Lo + 3]),
     io_lib:format("static inline void ~sset_~s~s(~s* p, ~s v_)\n{\n", [Pre, Arm, F, RecCT, U]),
     "    uint8_t* b_ = (uint8_t*)p;\n",
     [io_lib:format("    b_[~p] = (uint8_t)(v_.~s >> ~p);\n", [Lo + K, UM, K * 8])
      || K <- lists:seq(0, 3)],
     "}\n"].

%% The shift is done in the WIDE type on purpose. Measured on avr-gcc -Os for a
%% field spanning two bytes: this form 14 bytes, gcc's own bit-field read 16,
%% and ((b[j] & 1) << 8) | b[i] twenty-six -- gcc emits a byte swap through
%% eor/mov instead of just placing the register.
load(1, Lo, 0, _Ty)  -> io_lib:format("b_[~p]", [Lo]);
load(1, Lo, Sh, _Ty) -> io_lib:format("(b_[~p] >> ~p)", [Lo, Sh]);
load(2, Lo, 0, Ty)   -> io_lib:format("(~s)b_[~p] | ((~s)b_[~p] << 8)", [uw(Ty), Lo, uw(Ty), Lo + 1]);
load(2, Lo, Sh, Ty)  -> io_lib:format("((~s)b_[~p] | ((~s)b_[~p] << 8)) >> ~p",
				      [uw(Ty), Lo, uw(Ty), Lo + 1, Sh]);
load(N, Lo, Sh, _Ty) ->
    ["(", string:join([io_lib:format("((uint32_t)b_[~p] << ~p)", [Lo + K, K * 8])
		       || K <- lists:seq(0, N - 1)], " | "),
     io_lib:format(") >> ~p", [Sh])].

load_ro(1, Lo, 0, _Ty)  -> io_lib:format("ro_byte(&b_[~p])", [Lo]);
load_ro(1, Lo, Sh, _Ty) -> io_lib:format("(ro_byte(&b_[~p]) >> ~p)", [Lo, Sh]);
load_ro(2, Lo, 0, Ty)   -> io_lib:format("(~s)ro_byte(&b_[~p]) | ((~s)ro_byte(&b_[~p]) << 8)",
					 [uw(Ty), Lo, uw(Ty), Lo + 1]);
load_ro(2, Lo, Sh, Ty)  -> io_lib:format("((~s)ro_byte(&b_[~p]) | ((~s)ro_byte(&b_[~p]) << 8))"
					 " >> ~p", [uw(Ty), Lo, uw(Ty), Lo + 1, Sh]);
load_ro(N, Lo, Sh, _Ty) ->
    ["(", string:join([io_lib:format("((uint32_t)ro_byte(&b_[~p]) << ~p)", [Lo + K, K * 8])
		       || K <- lists:seq(0, N - 1)], " | "),
     io_lib:format(") >> ~p", [Sh])].

%% The LOAD is always unsigned -- the shift must not drag a sign bit along.
%% Sign extension happens once, afterwards, in sext/4.
uw("int8_t")  -> "uint8_t";
uw("int16_t") -> "uint16_t";
uw("int32_t") -> "uint32_t";
uw(T)         -> T.

byte_mask(Mask, Sh, K) -> ((Mask bsl Sh) bsr (K * 8)) band 16#FF.

%% Casting to uint32_t here -- which this generator did at first -- makes a
%% four-bit field in one byte cost 32-bit arithmetic on AVR, and the tree grew
%% 2154 bytes before it was measured.
contrib(Sh, 0) when Sh =:= 0 -> "v_";
contrib(Sh, 0)               -> io_lib:format("(v_ << ~p)", [Sh]);
contrib(Sh, K)               -> io_lib:format("(v_ >> ~p)", [K * 8 - Sh]).

%% A byte the field owns WHOLE needs no read-modify-write. That is the payoff
%% for byte-aligning a field, and why the generator says which ones straddle.
set_byte(_B, 0, _V) -> "";
set_byte(B, 16#FF, V) -> io_lib:format("    b_[~p] = (uint8_t)~s;\n", [B, V]);
set_byte(B, M, V) ->
    io_lib:format("    b_[~p] = (uint8_t)((b_[~p] & (uint8_t)0x~.16BU)"
		  " | (uint8_t)(~s & 0x~.16BU));\n",
		  [B, B, (bnot M) band 16#FF, V, M]).

%% ONE id space per FAMILY over every field of every arm, which is what
%% micro-csp indexes. The per-record enums above are for C, where the arm is
%% known from the expression; bytecode has only a number.
%%
%% DEDUPLICATED: an arm's payload often sits where another's does -- md.n,
%% mq.mx and rt.src are all {word 1, shift 0, 16 bits} -- and a descriptor is
%% three bytes, so one row per NAME pays for the same numbers several times.
%% The names stay distinct; only the row they point at is shared.
family_fields(Fam, Laid) ->
    Fs = [{Arm, F} || {Id, _, _, Fields, I = {_, _, _, FN}} <- Laid, FN =:= Fam,
		      {_, Arm} <- [nm(Id, I)], F <- Fields],
    Descs = lists:usort([desc(F) || {_, F} <- Fs]),
    Index = maps:from_list(lists:zip(Descs, lists:seq(0, length(Descs) - 1))),
    P = string:uppercase(tag(Fam)),
    ["typedef enum {\n",
     [io_lib:format("    MF~s_~s~s = ~p,\n",
		    [P, string:uppercase(Arm),
		     string:uppercase(atom_to_list(element(1, F))),
		     maps:get(desc(F), Index)]) || {Arm, F} <- Fs],
     io_lib:format("    MF~s_NFIELD = ~p\n} mf_~s_all_t;\n\n", [P, length(Descs), short(Fam)]),
     io_lib:format("// ~p names over ~p distinct descriptors.\n",
		   [length(Fs), length(Descs)]),
     "// Fields wider than 16 bits are in here and a cell is not, so MC_DECL\n"
     "// refuses those rather than handing back a quiet truncation.\n",
     io_lib:format("#define CSP_~s_ALL_FIELDS { \\\n", [string:uppercase(short(Fam))]),
     string:join([io_lib:format("    { ~p, ~p, ~p } /* ~s */",
				[W, Sh, B, string:join(names_for(D, Fs), " ")])
		  || D = {W, Sh, B} <- Descs], ", \\\n"),
     " }\n\n"].

%% MFA_ for declarations, MFI_ for instructions: one letter, and it is the
%% family's own initial rather than a table to keep in step.
tag(decl_common)  -> "a";
tag(instr_common) -> "i";
tag(F)            -> string:slice(atom_to_list(F), 0, 1).

%% The family without its _common suffix: decl_common describes the bytes every
%% arm shares, but the FAMILY is declarations, and CSP_DECL_ALL_FIELDS is what
%% that should be called.
short(F) ->
    case string:split(atom_to_list(F), "_common") of
	[S, []] -> S;
	_       -> atom_to_list(F)
    end.

desc({_FN, Pos, Bits, _}) -> {Pos div 32, Pos rem 32, Bits}.

names_for(D, Fs) ->
    [Arm ++ atom_to_list(element(1, F)) || {Arm, F} <- Fs, desc(F) =:= D].

fingerprint(Laid) ->
    Text = lists:flatten([io_lib:format("~s.~s:~p:~p;", [idname(R), N, P, B])
			  || {R, _, _, Fs, _} <- Laid, {N, P, B, _} <- Fs]),
    <<H:32, _/binary>> = crypto:hash(sha, Text),
    io_lib:format("// Fingerprint of every field name, position and width above, in order.\n"
		  "// An image built against a different layout must be refused, not read.\n"
		  "#define CSP_LAYOUT_FINGERPRINT 0x~.16BUL\n\n", [H]).

%% ---------------------------------------------------------------- the oracle

oracle(Laid) ->
    [banner(),
     "// The BRIDGE. Every accessor above is compared against the C bit-field of\n"
     "// the same name: set through the struct, read through the accessor, and\n"
     "// the other way round. The struct is the oracle only until nothing reads\n"
     "// its bit-fields any more -- after that the layout is ours and this file\n"
     "// goes away with it.\n"
     "#ifndef __CSP_LAYOUT_ORACLE_H__\n#define __CSP_LAYOUT_ORACLE_H__\n\n",
     [oracle_rec(R) || R <- Laid],
     "#endif\n"].

%% The C spelling: a base record's field is bare, an arm's goes through its
%% union member. That mapping is the whole reason the arm name is the prefix.
cpath(_Id, {_, _, true, _}, F)       -> F;
cpath({_B, Arm}, {_, _, false, _}, F) -> atom_to_list(Arm) ++ "." ++ F.

oracle_rec({Id, _Bytes, _Total, Fields, Info}) ->
    N = idname(Id),
    [io_lib:format("#define CSP_ORACLE_~s(REC, FAIL) do { \\\n", [string:uppercase(N)]),
     string:join([oracle_field(Id, Info, F) || F <- Fields], " \\\n"),
     " \\\n    } while (0)\n\n"].

oracle_field(Id, Info, {FName, _Pos, Bits, CT}) ->
    F = atom_to_list(FName),
    {Pre, Arm} = nm(Id, Info),
    C = cpath(Id, Info, F),
    %% REC is the RAW union -- the only thing that can name a bit-field -- and
    %% the accessors take the opaque type the runtime uses. Same bytes, same
    %% size; the cast is what lets one test hold both sides at once.
    {RecCT, _, _, _} = Info,
    G = io_lib:format("(const ~s*)&(REC)", [RecCT]),
    S = io_lib:format("(~s*)&(REC)", [RecCT]),
    case umember(CT) of
	{_U, UM} ->
	    Max = (1 bsl Bits) - 1,
	    io_lib:format(
	      "    memset(&(REC), 0, sizeof(REC)); \\\n"
	      "    (REC).~s.~s = ~pUL; \\\n"
	      "    if (~sget_~s~s(~s).~s != ~pUL) \\\n"
	      "\tFAIL(\"~s\", (long)~sget_~s~s(~s).~s, ~pL);",
	      [C, UM, Max, Pre, Arm, F, G, UM, Max, C, Pre, Arm, F, G, UM, Max]);
	none ->
	    %% A signed field is probed at its MOST NEGATIVE value: -1 has every
	    %% bit set at any width and would pass a wrong one.
	    {Set, Want} = case is_signed(CT) of
			      true  -> {-(1 bsl (Bits - 1)), -(1 bsl (Bits - 1))};
			      false -> {(1 bsl Bits) - 1, (1 bsl Bits) - 1}
			  end,
	    Cast = case CT of
		       none -> ""; signed -> "";
		       _ -> "(" ++ atom_to_list(CT) ++ ")"
		   end,
	    io_lib:format(
	      "    memset(&(REC), 0, sizeof(REC)); \\\n"
	      "    (REC).~s = ~s~p; \\\n"
	      "    if ((long)~sget_~s~s(~s) != ~pL) \\\n"
	      "\tFAIL(\"~s\", (long)~sget_~s~s(~s), ~pL); \\\n"
	      "    memset(&(REC), 0, sizeof(REC)); \\\n"
	      "    ~sset_~s~s(~s, ~p); \\\n"
	      "    if ((long)(REC).~s != ~pL) \\\n"
	      "\tFAIL(\"~s set\", (long)(REC).~s, ~pL);",
	      [C, Cast, Set, Pre, Arm, F, G, Want, C, Pre, Arm, F, G, Want,
	       Pre, Arm, F, S, Set, C, Want, C, C, Want])
    end.
