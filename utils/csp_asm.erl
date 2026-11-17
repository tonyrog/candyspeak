%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image assembler
%%%
%%% The inverse of csp_disasm: the terms it writes -- {header, H}, {strings,
%%% S}, then the declarations and the instructions -- back into the bytes of an
%%% image, CRCs and all, so a host can build what a node loads without the
%%% node's compiler. csp_disasm:file(Img, Asm) writes such a file;
%%%
%%%     {ok, Bin} = csp_asm:file("prog.asm")
%%%
%%% reads one back, and roundtrip/1 is the test that the two agree: an image
%%% taken apart and put together again is the same image, byte for byte.
%%%
%%% Where the bits go is utils/layout.terms -- the one description the C
%%% accessors are generated from -- not a second copy of the widths here.
%%%
%%% The image (port/csp_dump.c, csp_dump_code): a 56-byte header, then six
%%% sections, each an 8-byte prologue (tag, length) and its data padded to four
%%% bytes. STRS is empty -- names travel in OP_SEGMENT runs in the code -- and
%%% holds only a 0xFF sentinel and its CRC; DECL is 8-byte records and a
%%% DECL_END_MARK; CODE 4-byte words and an OP_END_MARK; each mark carries the
%%% CRC over its section and itself. GIDG/GOFS/GEDG are the reactive graph, a
%%% stub each when there is none -- the only kind assembled here.
%%% @end
%%% Created :  5 Nov 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_asm).

-export([file/1, asm/1, image/1, roundtrip/1]).
-export([patch/2, patch_terms/2, patch_roundtrip/2]).
-export([layout/0]).

-include("csp_asm.hrl").

-define(MAP(Key, Val), Key => Val).   % the maps in csp_asm.hrl, name to code

-define(HEADER_BYTES, 56).
-define(SECT_BYTES, 8).
-define(ROM_FORMAT_VERSION, 23).

%%% ------------------------------------------------------------------ entry

%% A file of terms, as csp_disasm:file/2 writes them.
file(Filename) ->
    case file:consult(Filename) of
	{ok, Terms} -> asm(Terms);
	Error -> Error
    end.

%% An image file taken apart and put together again: {ok, Bin}.
image(Filename) ->
    case csp_disasm:file(Filename) of
	{ok, {Header, Strings, Decls, Code}} ->
	    asm([{header, Header}, {strings, Strings} | Decls ++ Code]);
	Error -> Error
    end.

%% The test: the image the assembler makes from the disassembler's terms is
%% the image they came from. ok, or where the two first part.
roundtrip(Filename) ->
    {ok, Orig} = read_image(Filename),
    case image(Filename) of
	{ok, Orig} -> ok;
	{ok, Bin}  -> {differ, first_diff(Orig, Bin)};
	Error      -> Error
    end.

read_image(Filename) ->
    case filename:extension(Filename) of
	".img" -> file:read_file(Filename);
	_ -> {error, not_an_img}
    end.

%%% --------------------------------------------------------------- the image

%% Terms in, the image's bytes out.
asm(Terms) ->
    try asm_(Terms) of
	Bin -> {ok, Bin}
    catch
	throw:Why -> {error, Why}
    end.

asm_(Terms) ->
    H = case [X || {header, X} <- Terms] of
	    [X | _] -> X;
	    [] -> #{}
	end,
    case maps:get(n_edg, H, 0) of
	0 -> ok;
	_ -> throw(reactive_graph_not_assembled)
    end,
    Strings = case [X || {strings, X} <- Terms] of
		  [S | _] -> S;
		  [] -> #{0 => ""}
	      end,
    F = ctx(H, Strings),
    Decls = lists:append(decls(F, Strings, [D || D <- Terms, element(1, D) =:= decl])),
    Code = lists:append([instr(F, I) || I <- Terms, element(1, I) =:= instr]),
    image(F, H, Decls, Code).

ctx(H, Strings) ->
    #{endian => maps:get(endian, H, little),
      float  => ieee,                     % every image's, since format 23
      names  => names(Strings),
      layout => layout()}.

%% The declarations' records, a list per term.
%%
%% A text can stand in the table more than once -- a module's own State beside
%% the program's, a module's A beside a global A, and every view (`Buf[2..3]')
%% and route has an EMPTY name of its own -- and the terms give only the text.
%% The compiler made one string per declaration that brought a name in, in
%% order, so the k-th declaration with a text takes that text's k-th index. An
%% empty string VALUE (`#param P string = ""') is the literal "", interned at
%% the lowest empty index: that one is not a name.
decls(F, Strings, Decls0) ->
    ByText = maps:groups_from_list(fun({_, T}) -> bin(T) end, fun({I, _}) -> I end,
				   lists:sort([{I, T} || {I, T} <- maps:to_list(Strings),
							 I > 0])),
    Left0 = case lists:any(fun empty_value/1, Decls0) of
		true -> maps:update_with(<<>>, fun tl/1, [], ByText);
		false -> ByText
	    end,
    {Ds, _} = lists:mapfoldl(fun(D, Left) -> named(D, Left) end, Left0, Decls0),
    [decl(F, D) || D <- Ds].

%% The sections, then the header that counts them. Decls and Code are lists
%% of integers, one per record.
image(F, H, Decls, Code) ->
    StrData = <<16#ff, 16#ff, 16#ff>>,              % sentinel, CRC of nothing
    DeclBin = << <<(word(F, 64, D))/binary>> || D <- Decls >>,
    CodeBin = << <<(word(F, 32, C))/binary>> || C <- Code >>,
    CrcDecl = crc16(DeclBin),
    CrcCode = crc16(CodeBin),
    DeclMark = pack(F, decl, em, #{type => ?DECL_END_MARK}),
    CodeMark = pack(F, instr, em, #{op => ?OP_END_MARK}),
    DeclEm = pack(F, decl, em, #{type => ?DECL_END_MARK,
				 crc => crc16(CrcDecl, word(F, 64, DeclMark))}),
    CodeEm = pack(F, instr, em, #{op => ?OP_END_MARK,
				  crc => crc16(CrcCode, word(F, 32, CodeMark))}),
    Stub = <<0:16, 0:16>>,                           % one index_t, padded
    Sects = [{"STRS", StrData},
	     {"DECL", <<DeclBin/binary, (word(F, 64, DeclEm))/binary>>},
	     {"CODE", <<CodeBin/binary, (word(F, 32, CodeEm))/binary>>},
	     {"GIDG", Stub}, {"GOFS", Stub}, {"GEDG", Stub}],
    %% Every section's data on a multiple of 16 (format 23): the padding is in
    %% the section's length; after the last, to 4.
    {Body, Ofs, _} =
	lists:foldl(fun({Tag, Data0}, {Acc, Os, At}) ->
			    DataAt = At + ?SECT_BYTES,
			    Data = case Tag of
				       "GEDG" -> pad4(Data0);
				       _ -> pad16(Data0, DataAt)
				   end,
			    P = <<(list_to_binary(Tag))/binary,
				  (word(F, 32, byte_size(Data)))/binary>>,
			    {<<Acc/binary, P/binary, Data/binary>>,
			     Os ++ [DataAt],
			     DataAt + byte_size(Data)}
		    end, {<<>>, [], ?HEADER_BYTES}, Sects),
    Size = ?HEADER_BYTES + byte_size(Body),
    [OStr, ODecl, OCode, OIdg, OOfs, OEdg] = Ofs,
    Fields = [{32, Size}, {16, maps:get(version, H, ?ROM_FORMAT_VERSION)},
	      {8, maps:get(role, H, 1)}, {8, flags(F, H)},
	      {16, maps:get(generation, H, 0)},
	      {16, 0}, {16, length(Decls)}, {16, length(Code)}, {16, 0},
	      {16, 16#ffff}, {16, CrcDecl}, {16, CrcCode}, {16, 0},
	      {32, OStr}, {32, ODecl}, {32, OCode},
	      {32, OIdg}, {32, OOfs}, {32, OEdg}],
    Hdr = <<"JAM\n", << <<(word(F, B, V))/binary>> || {B, V} <- Fields >>/binary>>,
    <<Hdr/binary, (word(F, 16, crc16(Hdr)))/binary, Body/binary>>.

%% CSP_IMG_F_BIG 2: what the image IS, from the context it is written in. A
%% header's own `flags' (csp_disasm's, of the image it was read from) keeps
%% only any other bits: taken whole, an image read little and written big said
%% little of itself, and read back as one with a bad CRC. Bit 0 said IEEE
%% before format 23, which keeps every float constant IEEE.
flags(F, H) ->
    (maps:get(flags, H, 0) band bnot 3) bor
	(case maps:get(endian, F) of big -> 2; little -> 0 end).

%% Data starting at At, padded so the next section's data (after its 8-byte
%% prologue) starts on a multiple of 16: CSP_PAD16.
pad16(B, At) ->
    N = (16 - ((At + byte_size(B) + ?SECT_BYTES) band 15)) band 15,
    <<B/binary, 0:N/unit:8>>.

pad4(B) when byte_size(B) rem 4 =:= 0 -> B;
pad4(B) -> pad4(<<B/binary, 0>>).

word(#{endian := big}, Bits, V)    -> <<V:Bits/big>>;
word(#{endian := little}, Bits, V) -> <<V:Bits/little>>.

%%% ---------------------------------------------------------- declarations

empty_value({decl, _, _, _, Ps}) when is_list(Ps) ->
    lists:member({type, string}, Ps) andalso
	lists:keyfind(init, 1, Ps) =:= {init, <<>>};
empty_value(_) -> false.

%% The declaration with its name as an index: the next one for its text.
%% No name at all is index 0, and csp_disasm says so with "" -- a list, where
%% the text of a string in the table, even an empty one, is a binary.
named(D, Left) when element(1, D) =:= decl, tuple_size(D) >= 5 ->
    N = tuple_size(D) - 1,                     % the name is next to last
    case element(N, D) of
	[] -> {setelement(N, D, 0), Left};
	T -> named(D, N, bin(T), Left)
    end;
named(D, Left) -> {D, Left}.

named(D, N, T, Left) ->
    case maps:get(T, Left, []) of
	[I] -> {setelement(N, D, I), Left};                % the last: it stays
	[I | Is] -> {setelement(N, D, I), Left#{T := Is}};
	[] -> {D, Left}
    end.

%% A declaration term: the records it stands for, an array being a head and
%% len-1 continuations (nameless, cont set), a timer two slots.
decl(F, {decl, _I, 'end'}) ->
    [pack(F, decl, none, #{type => ?DECL_END, res => 31, vt => ?V_INTEGER})];
decl(F, {decl, _I, states, Name, Ns}) ->
    Names = [name(F, Name) | [name(F, proplists:get_value(K, Ns, ""))
			      || K <- [name2, name3, name4, name5, name6]]],
    [pack(F, decl, sn, #{type => ?DECL_STATES, names => Names})];
decl(F, {decl, _I, module, Name, Ps}) ->
    [pack(F, decl, md, (common(F, ?DECL_MODULE, Name, []))#{
			  n => get(n, Ps), ent => get(ent, Ps)})];
decl(F, {decl, _I, object, _Module, Name, Ps}) ->
    [pack(F, decl, mq, (common(F, ?DECL_OBJECT, Name, []))#{
			  mx => get(mx, Ps), m => get(m, Ps)})];
decl(F, {decl, _I, timer, Name, Ps}) ->
    C = (common(F, ?DECL_TIMER, Name, Ps))#{vt => vt(Ps)},
    [pack(F, decl, tm, C#{period => get(period, Ps),
			  init => proplists:get_value(init, Ps, 0)}),
     %% the second slot: where a running timer keeps its start -- a nameless
     %% unsigned variable
     pack(F, decl, va, #{type => ?DECL_VARIABLE, res => 31,
			 vt => ?V_UNSIGNED})];
decl(F, {decl, _I, Window, Name, Ps}) when Window =:= field; Window =:= view ->
    [pack(F, decl, ca, maps:merge((common(F, decl_type(Window), Name, Ps))#{
				      vt => vt(Ps)}, ca(Ps)))];
decl(F, {decl, _I, buffer, Name, Ps}) ->
    [pack(F, decl, bf, (common(F, ?DECL_BUFFER, Name, Ps))#{
			  vt => ?V_UNSIGNED,
			  nbytes => get(nbytes, Ps),
			  transport => transport(get(transport, Ps)),
			  id => get(id, Ps)})];
decl(F, {decl, _I, route, Name, Ps}) ->
    [pack(F, decl, rt, (common(F, ?DECL_ROUTE, Name, []))#{
			  src => get(src, Ps), dst => get(dst, Ps)})];
decl(F, {decl, _I, Type, Name, Ps}) when Type =:= variable; Type =:= local;
					 Type =:= constant; Type =:= param;
					 Type =:= digital; Type =:= analog ->
    case proplists:get_value(len, Ps) of
	undefined -> [elem(F, Type, Name, Ps, 0)];
	Len ->
	    Common = [P || P <- Ps, not lists:member(element(1, P),
						     [len, slots, elems])],
	    Each = proplists:get_value(elems, Ps, lists:duplicate(Len, [])),
	    [elem(F, Type, case K of 0 -> Name; _ -> [] end,
		  Common ++ E, case K of 0 -> 0; _ -> 1 end)
	     || {K, E} <- lists:zip(lists:seq(0, Len - 1), Each)]
    end;
decl(F, {decl, _I, Type, Name, Ps}) ->
    [pack(F, decl, none, common(F, decl_type(Type), Name, Ps))];
decl(_F, D) ->
    throw({bad_decl, D}).

%% One element of a variable, constant, pin -- an array's or a scalar's own.
elem(F, Type, Name, Ps, Cont) ->
    Local = case Type of param -> 1; local -> 1; _ -> 0 end,
    C = (common(F, decl_type(Type), Name, Ps))#{cont => Cont, local => Local},
    case Type of
	digital ->
	    pack(F, decl, di, maps:merge(C#{vt => ?V_INTEGER},
					 maps:with([soft, pin, port, irq, pullup,
						    pulldown, invert],
						   maps:from_list(Ps))));
	analog ->
	    pack(F, decl, an,
		 maps:merge(C#{vt => vt(Ps),
			       endian => endian(proplists:get_value(endian, Ps,
								    native))},
			    maps:with([soft, pin, port, irq, pwm],
				      maps:from_list(Ps))));
	_ ->
	    C1 = C#{vt => vt(Ps)},
	    case proplists:get_value(init, Ps, 0) of
		{bind, Window} ->
		    pack(F, decl, ca, maps:merge(C1#{bound => 1}, ca(Window)));
		Init ->
		    Arm = case Type of
			      variable -> va; local -> va;
			      constant -> cn; param -> cn
			  end,
		    pack(F, decl, Arm, C1#{init => value(F, get(type, Ps), Init)})
	    end
    end.

%% The four bytes every declaration starts with.
common(F, Type, Name, Ps) ->
    Local = case decl_local(Type, Ps) of true -> 1; false -> 0 end,
    #{type => Type, name => name(F, Name), local => Local,
      dir => dir(proplists:get_value(dir, Ps, none)),
      res => proplists:get_value(size, Ps, 32) - 1, vt => ?V_INTEGER,
      reg => proplists:get_value(reg, Ps, 0)}.

%% #local and #param are a variable and a constant with the local bit set;
%% the generic form says it outright.
decl_local(_Type, Ps) ->
    lists:member({local, 1}, Ps) orelse lists:member(local, Ps).

decl_type(variable) -> ?DECL_VARIABLE;
decl_type(local)    -> ?DECL_VARIABLE;
decl_type(constant) -> ?DECL_CONSTANT;
decl_type(param)    -> ?DECL_CONSTANT;
decl_type(T) ->
    case maps:find(T, decl_map()) of
	{ok, N} -> N;
	error -> throw({bad_decl_type, T})
    end.

decl_map() -> #{ ?DECL_MAP }.

%% The window arm: a #field, a buffer view, a bound variable. len is stored
%% less one, as res is.
ca(Ps) ->
    #{id => get(id, Ps), endian => endian(proplists:get_value(endian, Ps, native)),
      bit => get(bit, Ps), len => get(len, Ps) - 1}.

value(_F, integer, V)  -> V band 16#ffffffff;
value(_F, unsigned, V) -> V band 16#ffffffff;
value(#{float := ieee}, float, V) ->
    <<X:32>> = <<(float(V)):32/float>>, X;
value(#{float := fixpoint}, float, V) ->
    round(V * 65536) band 16#ffffffff;
value(F, string, V) -> name(F, V);
value(_F, _T, V) when is_integer(V) -> V band 16#ffffffff;
value(_F, T, V) -> throw({bad_value, T, V}).

vt(Ps) ->
    case proplists:get_value(type, Ps) of
	undefined -> ?V_INTEGER;
	T -> type_code(T)
    end.

type_code(T) -> maps:get(T, #{ ?TYPE_MAP }).
dir(D) -> maps:get(D, #{ ?DIR_MAP }).
endian(E) -> maps:get(E, #{ ?ENDIAN_MAP }).

%% transport_t in csp.h, as csp_disasm names them.
transport(T) when is_integer(T) -> T;
transport(T) ->
    Ts = [none, pin, can, i2c, spi, udp, console, repl, tcp, uart, sms, onewire],
    length(lists:takewhile(fun(X) -> X =/= T end, Ts)).

get(K, Ps) ->
    case lists:keyfind(K, 1, Ps) of
	{_, V} -> V;
	false -> throw({missing, K, Ps})
    end.

%%% ----------------------------------------------------------- the names

%% The string table, text to index. An index is what a declaration's name
%% and a string constant hold.
%% A string VALUE takes the first index its text has: a literal is interned,
%% so two `#param P string = ""' share one.
names(Strings) ->
    maps:fold(fun(I, T, M) when I > 0 ->
		      K = bin(T),
		      case maps:find(K, M) of
			  {ok, J} when J < I -> M;
			  _ -> M#{K => I}
		      end;
		 (_, _, M) -> M
	      end, #{}, Strings).

name(_F, I) when is_integer(I) -> I;          % placed by named/2
name(_F, []) -> 0;                            % none, see named/2
name(#{names := Ns}, Text) ->
    case maps:find(bin(Text), Ns) of
	{ok, I} -> I;
	error -> throw({no_such_name, Text})
    end.

bin(T) when is_binary(T) -> T;
bin(T) when is_list(T) -> unicode:characters_to_binary(T).

%%% ---------------------------------------------------------- instructions

%% An instruction term, as csp_disasm writes it: the words it stands for --
%% one, or a segment's header and its payload.
instr(F, {instr, _Addr, Op}) ->
    [pack(F, instr, none, #{op => op(Op)})];
instr(F, {instr, _Addr, Op, Args}) ->
    O = op(Op),
    case maps:get(Op, ?OPCLASS_MAP) of
	nop -> [pack(F, instr, none, #{op => O})];
	seg ->
	    [{slots, N}, {used, Used}, {data, Data}] = Args,
	    %% the payload as it lies in the image, in its byte order
	    [pack(F, instr, sg, #{op => O, num => N, used => Used}) |
	     case maps:get(endian, F) of
		 little -> [W || <<W:32/little>> <= Data];
		 big    -> [W || <<W:32/big>> <= Data]
	     end];
	alu1 ->
	    [X, Y | Fl] = Args,
	    [pack(F, instr, a, #{op => O, x => reg(X), y => reg(Y),
				 u => flag(unsigned, Fl), swap => flag(swap, Fl)})];
	alu2 ->
	    [X, Y, Z | Fl] = Args,
	    [pack(F, instr, a, #{op => O, x => reg(X), y => reg(Y), z => reg(Z),
				 u => flag(unsigned, Fl), swap => flag(swap, Fl)})];
	mem ->
	    case Args of
		[X, {mem, M}] ->
		    [pack(F, instr, m, #{op => O, x => reg(X), mem => M})];
		[X, {mem, M}, Part] ->
		    [pack(F, instr, m, #{op => O, x => reg(X), mem => M,
					 y => part(Part)})]
	    end;
	memi ->
	    [X, {imm, I}, {mem, M}] = Args,
	    [pack(F, instr, mi, #{op => O, x => reg(X), imm => I, mem => M})];
	imm ->
	    [X, {imm, I} | Fl] = Args,
	    [pack(F, instr, i, #{op => O, x => reg(X), imm => I,
				 lz => flag(lz, Fl)})];
	rule ->
	    [C, Nxt | Fl] = Args,
	    [pack(F, instr, r, #{op => O, cnd => reg(C), nxt => Nxt,
				 implicit => flag(implicit, Fl)})];
	instate ->
	    [X, Imm, {nxt, Nxt} | Fl] = Args,
	    [pack(F, instr, in, #{op => O, x => reg(X), imm => Imm, nxt => Nxt,
				  implicit => flag(implicit, Fl)})];
	next ->
	    [X] = Args,
	    [pack(F, instr, x, #{op => O, x => reg(X)})];
	enter ->
	    [{mem, M}, {n, N}] = Args,
	    [pack(F, instr, e, #{op => O, num => N, mx => M})];
	leave ->
	    [{mem, M}, {n, N}] = Args,
	    [pack(F, instr, v, #{op => O, num => N, mx => M})];
	new ->
	    [{obj, Obj}] = Args,
	    [pack(F, instr, n, #{op => O, obj => Obj})];
	call ->
	    [X, {idx, Idx}, {usr, Usr}, {avt, Ts}] = Args,
	    [pack(F, instr, f, #{op => O, x => reg(X), idx => Idx, usr => Usr,
				 avt => avt(Ts)})];
	seto ->
	    [{obj, Obj}] = Args,
	    [pack(F, instr, o, #{op => O, obj => Obj})];
	setox ->
	    [X, {len, Len}, {stride, Stride} | Fl] = Args,
	    [pack(F, instr, ox, #{op => O, x => reg(X), len => Len,
				  stride => Stride, bits => flag(bits, Fl)})];
	'end' ->
	    []
    end;
instr(_F, I) ->
    throw({bad_instr, I}).

op(Op) ->
    case maps:find(Op, #{ ?OPCODE_MAP }) of
	{ok, N} -> N;
	error -> throw({bad_opcode, Op})
    end.

reg(R) -> maps:get(R, #{ ?REG_MAP }).
part(P) -> maps:get(P, #{ ?PART_MAP }).

flag(K, Fl) -> case lists:member(K, Fl) of true -> 1; false -> 0 end.

%% Four bits of argument type per argument, the first lowest.
avt([]) -> 0;
avt([T | Ts]) -> type_code(T) bor (avt(Ts) bsl 4).

%%% -------------------------------------------------------------- packing

%% The record Arm of Family (decl or instr) with these fields set: an
%% integer, the bits as the target lays them out, least significant first.
%% The common fields (decl_common, instr_common) come first and an arm's
%% fields after, so an arm that overlays them (a states block's names) wins.
%% A field the layout does not have is an error, not a silent zero.
pack(#{layout := L}, Family, Arm, Vals) ->
    Base = case Family of decl -> decl_common; instr -> instr_common end,
    Fs = maps:get({Base, Base}, L) ++
	case Arm of none -> []; _ -> maps:get({Base, Arm}, L) end,
    Known = [N || {N, _, _, _} <- Fs],
    case [K || K <- maps:keys(Vals), not lists:member(K, Known)] of
	[] -> ok;
	Extra -> throw({no_such_field, Arm, Extra})
    end,
    lists:foldl(fun({N, Start, Bits, Count}, W) ->
			case maps:find(N, Vals) of
			    error -> W;
			    {ok, V} when Count > 0 ->
				lists:foldl(fun({K, E}, Wk) ->
						    set(Wk, Start + K * Bits, Bits, E)
					    end, W,
					    lists:zip(lists:seq(0, length(V) - 1), V));
			    {ok, V} -> set(W, Start, Bits, V)
			end
		end, 0, Fs).

set(W, Start, Bits, V) ->
    Mask = (1 bsl Bits) - 1,
    (W band bnot (Mask bsl Start)) bor ((V band Mask) bsl Start).

%% utils/layout.terms as #{{Base, Arm} => [{Field, StartBit, Bits, Count}]}
%% -- Count 0 for a plain field, the element count for an array. A base's own
%% fields are under {Base, Base}; an arm's start where its base ends unless it
%% says {start, Bit}.
layout() ->
    {ok, Ts} = file:consult(layout_file()),
    Recs = [R || R <- Ts, element(1, R) =:= record],
    Bases = maps:from_list([{N, fields(0, Fs)} || {record, N, _, Fs} <- Recs]),
    End = fun(B) -> lists:max([0 | [S + Bi * max(1, C) || {_, S, Bi, C} <- maps:get(B, Bases)]])
	  end,
    maps:merge(maps:from_list([{{N, N}, Fs} || {N, Fs} <- maps:to_list(Bases)]),
	       maps:from_list([{{B, A}, fields(End(B), Fs)}
			       || {record, A, B, _, Fs} <- Recs])).

fields(_Pos, [{start, P} | Fs]) -> fields(P, Fs);
fields(Pos, [{pad, B} | Fs]) -> fields(Pos + B, Fs);
fields(Pos, [{N, B, {array, _, C}} | Fs]) -> [{N, Pos, B, C} | fields(Pos + B * C, Fs)];
fields(Pos, [{N, B} | Fs]) -> [{N, Pos, B, 0} | fields(Pos + B, Fs)];
fields(Pos, [{N, B, _} | Fs]) -> [{N, Pos, B, 0} | fields(Pos + B, Fs)];
fields(_Pos, []) -> [].

layout_file() ->
    Here = filename:dirname(code:which(?MODULE)),
    Cs = [filename:join([Here, "..", "utils", "layout.terms"]),
	  filename:join([Here, "..", "priv", "layout.terms"]),
	  "utils/layout.terms"],
    case [C || C <- Cs, filelib:is_regular(C)] of
	[C | _] -> C;
	[] -> throw(no_layout_terms)
    end.

%%% --------------------------------------------------------------- patches
%%%
%%% The EEPROM store (csp_eeprom_emit in src/csp_eeprom.c): what /save writes
%%% and /dump patch sends, and what a node lays over its ROM at boot. A 78-byte
%%% header -- "CSP\0", the store's version, the settings' version, size and
%%% CRC, rom_fp (the crc_hdr of the image the patch belongs to), the object
%%% count, the patch described as an image header with its own CRCs, the
%%% #disable bitmap's rule count and CRC, and a CRC over all of it -- then the
%%% settings, the patch's declarations and instructions, and the bitmap.

-define(EEPROM_VERSION, 11).
-define(SETTINGS_VERSION, 1).
-define(STORE_HEADER_BYTES, 78).
-define(MAX_DIS_RULES, 128).

%% A patch on Rom: its declarations and instructions -- numbered on from the
%% image's, as csp_disasm:patch/2 numbers them -- {settings, Bin} and
%% {disabled, [RuleNo]}. The store's bytes, {ok, Bin}.
patch(Rom, Terms) ->
    try patch_(Rom, Terms) of
	Bin -> {ok, Bin}
    catch
	throw:Why -> {error, Why}
    end.

patch_(Rom, Terms) ->
    {ok, {H, RomStrings, RomDecls, RomCode}} = csp_disasm:image(Rom),
    PDecls = [D || D <- Terms, element(1, D) =:= decl],
    PCode = [I || I <- Terms, element(1, I) =:= instr],
    %% The patch's names follow the image's in the one table: its segments,
    %% numbered on.
    Strings = segment_strings(PCode, RomStrings),
    F = ctx(H, Strings),
    %% The image's trailing `end' gives its index to the patch's first
    %% declaration (csp_load_image drops it).
    ND0 = maps:get(n_decl, H),
    RomDecls1 = case lists:last(RomDecls) of
		    {decl, I, 'end'} when I =:= ND0 - 1 -> lists:droplast(RomDecls);
		    _ -> RomDecls
		end,
    Base = ND0 - (length(RomDecls) - length(RomDecls1)),
    Recs = decls(F, Strings, RomDecls1 ++ PDecls),
    {RomRecs, PRecs} = lists:split(length(RomDecls1), Recs),
    DeclW = lists:append(PRecs),
    CodeW = lists:append([instr(F, I) || I <- PCode]),
    %% Every declaration by index, for the rule count below.
    Table = maps:from_list(lists:zip(lists:seq(0, Base - 1 + length(DeclW)),
				     lists:append(RomRecs) ++ DeclW)),
    NDis = n_rules(Table, RomCode ++ PCode),
    Off = proplists:get_value(disabled, Terms, []),
    [throw({no_such_rule, N}) || N <- Off, N < 1 orelse N > NDis],
    DisBin = dis_bitmap(F, NDis, Off),
    Settings = proplists:get_value(settings, Terms, <<>>),
    DeclBin = << <<(word(F, 64, D))/binary>> || D <- DeclW >>,
    CodeBin = << <<(word(F, 32, C))/binary>> || C <- CodeW >>,
    RamFields = [{16, ?ROM_FORMAT_VERSION}, {8, 0}, {8, flags(F, H)}, {16, 0},
		 {16, 0}, {16, length(DeclW)}, {16, length(CodeW)}, {16, 0},
		 {16, 16#ffff}, {16, crc16(DeclBin)}, {16, crc16(CodeBin)}, {16, 0},
		 {32, 0}, {32, 0}, {32, 0}, {32, 0}, {32, 0}, {32, 0}],
    Ram0 = <<0:32, 0:32, << <<(word(F, B, V))/binary>> || {B, V} <- RamFields >>/binary>>,
    Ram = <<Ram0/binary, (word(F, 16, crc16(Ram0)))/binary>>,
    NObj = length([x || {decl, _, object, _, _, _} <- RomDecls1 ++ PDecls]),
    Hdr0 = <<"CSP", 0, (word(F, 16, ?EEPROM_VERSION))/binary,
	     (word(F, 16, ?SETTINGS_VERSION))/binary,
	     (word(F, 16, byte_size(Settings)))/binary,
	     (word(F, 16, crc16(Settings)))/binary,
	     (word(F, 16, crc_hdr(Rom)))/binary,
	     (word(F, 16, NObj))/binary, Ram/binary,
	     (word(F, 16, NDis))/binary, (word(F, 16, crc16(DisBin)))/binary>>,
    Hdr = <<Hdr0/binary, (word(F, 16, crc16(Hdr0)))/binary>>,
    ?STORE_HEADER_BYTES = byte_size(Hdr),
    <<Hdr/binary, Settings/binary, DeclBin/binary, CodeBin/binary,
      (case NDis of 0 -> <<>>; _ -> DisBin end)/binary>>.

%% The strings the patch's segments bring, numbered on from the image's.
segment_strings(Code, Strings) ->
    lists:foldl(fun({instr, _, 'SEGMENT', [{slots, _}, {used, U}, {data, D}]}, S) ->
			seg_strings(U, D, S);
		   (_, S) -> S
		end, Strings, Code).

seg_strings(0, _, S) -> S;
seg_strings(U, <<N, T:N/binary, Rest/binary>>, S) ->
    seg_strings(U - (N + 1), Rest, S#{maps:size(S) => T}).

crc_hdr(<<_:4/binary, _:50/binary, Crc:2/binary, _/binary>> = Rom) ->
    <<_:7/binary, Flags, _/binary>> = Rom,
    case Flags band 2 of
	0 -> <<V:16/little>> = Crc, V;
	2 -> <<V:16/big>> = Crc, V
    end.

%% csp_n_rules: an OP_RULE is a rule -- it has a number, #disable reaches it
%% -- unless the first thing it stores to is a #local, which makes it a
%% formula or a binding (csp_rule_binds).
n_rules(Table, Code) -> n_rules(Table, Code, 0).

n_rules(T, [{instr, _, 'RULE', _} | Code], N) ->
    case binds(T, Code) of
	true -> n_rules(T, Code, N);
	false -> n_rules(T, Code, N + 1)
    end;
n_rules(T, [_ | Code], N) -> n_rules(T, Code, N);
n_rules(_T, [], N) -> N.

binds(_T, [{instr, _, 'NEXT', _} | _]) -> false;
binds(T, [{instr, _, Op, Args} | Code]) when Op =:= 'ST'; Op =:= 'STIMP'; Op =:= 'STI' ->
    {mem, M} = lists:keyfind(mem, 1, Args),
    W = maps:get(M band 16#7fff, T, 0),
    %% decl_common: type in bits 0..3, local at bit 5
    (W band 16#f =:= ?DECL_VARIABLE andalso (W bsr 5) band 1 =:= 1)
	orelse begin _ = Code, false end;
binds(T, [_ | Code]) -> binds(T, Code);
binds(_T, []) -> false.

%% n bits, rule k at bit k-1, in 32-bit words (set_group_t) of the node's
%% order; as many words as DIS_BYTES gives (capped at MAX_DIS_RULES).
dis_bitmap(F, N, Off) ->
    Words = (min(N, ?MAX_DIS_RULES) + 31) div 32,
    Bits = lists:foldl(fun(K, B) -> B bor (1 bsl (K - 1)) end, 0, Off),
    << <<(word(F, 32, (Bits bsr (32 * I)) band 16#ffffffff))/binary>>
       || I <- lists:seq(0, Words - 1) >>.

%% A store taken apart: the patch's own terms -- what patch/2 takes.
patch_terms(Rom, Store) ->
    case csp_disasm:patch(Rom, Store) of
	{ok, {H, _S, Decls, Code}} ->
	    {ok, {_, _, RomDecls, RomCode}} = csp_disasm:image(Rom),
	    ND0 = maps:get(n_decl, H),
	    Base = case lists:last(RomDecls) of
		       {decl, I, 'end'} when I =:= ND0 - 1 -> ND0 - 1;
		       _ -> ND0
		   end,
	    <<_:8/binary, SetBytes:16/little, _/binary>> = Store,
	    <<_:?STORE_HEADER_BYTES/binary, Settings:SetBytes/binary, _/binary>> = Store,
	    {ok, [{settings, Settings}, {disabled, csp_disasm:disabled(Rom, Store)}]
	     ++ [D || D <- Decls, element(2, D) >= Base]
	     ++ lists:nthtail(length(RomCode), Code)};
	Error -> Error
    end.

%% The test for patches: a store taken apart and put together again.
patch_roundtrip(RomFile, StoreFile) ->
    {ok, Rom} = file:read_file(RomFile),
    {ok, Store} = file:read_file(StoreFile),
    {ok, Terms} = patch_terms(Rom, Store),
    case patch(Rom, Terms) of
	{ok, Store} -> ok;
	{ok, Bin} -> {differ, first_diff(Store, Bin)};
	Error -> Error
    end.

%%% ---------------------------------------------------------------- CRC

crc16(Data) -> crc16(16#ffff, Data).

%% CRC-16/CCITT, as csp_crc16: seeded with 0xFFFF, foldable.
crc16(Crc, <<B, Rest/binary>>) ->
    crc16(crc8(8, Crc bxor (B bsl 8)), Rest);
crc16(Crc, <<>>) -> Crc band 16#ffff.

crc8(0, C) -> C;
crc8(I, C) when C band 16#8000 =/= 0 -> crc8(I - 1, ((C bsl 1) bxor 16#1021) band 16#ffff);
crc8(I, C) -> crc8(I - 1, (C bsl 1) band 16#ffff).

%%% ---------------------------------------------------------------- tests

%% Where two images first part: the section and the record in it.
first_diff(A, B) ->
    N = length(lists:takewhile(fun({X, Y}) -> X =:= Y end,
			       lists:zip(binary_to_list(A) ++ [none],
					 binary_to_list(B) ++ [none]))),
    #{offset => N, size => {byte_size(A), byte_size(B)},
      where => where(A, N),
      want => slice(A, N), got => slice(B, N)}.

where(_Img, N) when N < ?HEADER_BYTES -> {header, N};
where(Img, N) ->
    sect(Img, ?HEADER_BYTES, N).

sect(Img, At, N) when At + ?SECT_BYTES =< byte_size(Img) ->
    <<_:At/binary, Tag:4/binary, Len:32/little, _/binary>> = Img,
    case N < At + ?SECT_BYTES + Len of
	true ->
	    Off = N - At - ?SECT_BYTES,
	    Rec = case Tag of <<"DECL">> -> 8; <<"CODE">> -> 4; _ -> 1 end,
	    {binary_to_list(Tag), {record, Off div Rec}};
	false -> sect(Img, At + ?SECT_BYTES + Len, N)
    end;
sect(_Img, _At, N) -> {past_end, N}.

slice(B, N) ->
    S = (N div 4) * 4,
    binary:part(B, min(S, byte_size(B)), min(8, max(0, byte_size(B) - S))).
