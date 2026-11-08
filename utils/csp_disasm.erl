%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image disassembler
%%% @end
%%% Created :  8 Oct 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_disasm).

-export([file/1, image/1, patch/2]).
-export([crc16/1]).                       % csp_node: a dump line's CRC
-export([disabled/2]).                    % csp_list: the patch's #disable set
-export([reg/1, part/1, type/1, dir/1, endian/1]).
-export([decl/1, op_code/1, op_class/1]).

-include("csp_asm.hrl").


file(Filename) ->
    case filename:extension(Filename) of
	".hex" ->
	    case file:read_file(Filename) of
		{ok, HexBin} ->
		    Bin = hex_to_bin(HexBin),
		    disasm(Bin);
		Error -> Error
	    end;
	".img" ->
	    case file:read_file(Filename) of
		{ok, Bin} ->
		    disasm(Bin);
		Error -> Error
	    end;
	_ ->
	    {error, unknown_file_type}
    end.

hex_to_bin(Bin) ->
    HexData = iolist_to_binary(binary:split(Bin, <<"\n">>, [global,trim])),
    << <<(list_to_integer([H,L],16))>> || <<H,L>> <= HexData >>.

-define(HEADER_DATA_LEN, (11*2 + 7*4)).  %% size of fields

%% How the image reads, from its header's flags byte -- at the same offset in
%% either byte order. Handed to everything that decodes a field or a value.
ctx(<<_:7/binary, Flags, _/binary>>) ->
    #{endian => case Flags band 2 of 0 -> little; 2 -> big end,
      float => case Flags band 1 of 0 -> fixpoint; 1 -> ieee end}.

%% A field of the image, in the image's byte order.
word(#{endian := big}, Bin) ->
    N = bit_size(Bin),
    <<V:N/big>> = Bin,
    V;
word(#{endian := little}, Bin) ->
    N = bit_size(Bin),
    <<V:N/little>> = Bin,
    V.

u16(F, Bin) -> word(F, Bin).
u32(F, Bin) -> word(F, Bin).

%% The FLAGS byte first: it sits at the same offset in either byte order
%% (size 4, version 2, role 1), and it says which order every other field is
%% in. A node only ever reads its own; a host reads both.
disasm(<<$J,$A,$M,$\n, HeaderData:?HEADER_DATA_LEN/binary, CrcBin:2/binary,
	 Body/binary>>) ->
    F = ctx(HeaderData),
    Crc = u16(F, CrcBin),
    Crc1 = crc16(<<$J,$A,$M,$\n, HeaderData:?HEADER_DATA_LEN/binary>>),
    io:format("Header Crc=~p, expected=~p\n", [Crc1, Crc]),
    if Crc1 =/= Crc ->
	    {error, bad_crc};
       true  ->
	    Header = decode_header(F, HeaderData),
	    Sections = decode_sections(F, Header, Body),
	    _STRS_Ok = check_section_crc("STRS", n_str, crc_str, 
					 Header, Sections),
	    DECLS_OK = check_section_crc("DECL", n_decl, crc_decl, 
					 Header, Sections),
	    CODE_OK = check_section_crc("CODE", n_instr, crc_instr,
					Header, Sections),
	    _GRAPH_OK = check_grapg_crc(Header, Sections),
	    %% FIXME: check graph
	    {Code,Strings} = disasm_code(F, CODE_OK, Sections),
	    Decl = disasm_decl(F, DECLS_OK, Strings, Sections),
	    {ok, {Header, Strings, Decl, Code}}
    end;
disasm(<<$J,$A,$M,$\n, _/binary>>) ->
    {error, bad_file_truncated};
disasm(_) ->
    {error, bad_magic}.

%% An image already in hand -- read off a node with /dump rom, say.
image(Bin) ->
    disasm(Bin).

%% A RAM patch as the EEPROM store holds it (/dump eeprom), on top of the
%% image it was saved against: one program, as the node runs it.
%%
%% The store is a header of its own (eeprom_header_t in csp_eeprom.c, no
%% padding: every field is 16 bits or a packed csp_image_header_t), the
%% settings, the patch's declarations and instructions -- the same records an
%% image holds -- and the #disable bitmap. Its names continue the ROM's
%% numbering, its declarations follow the ROM's. rom_fp is the crc_hdr of the
%% image the patch belongs to; another image's names would be the wrong ones.
%% The store is the node's, so it is in the byte order of the node's image --
%% which disasm(Rom) has just read from the image's flags.
patch(Rom = <<_:4/binary, _:?HEADER_DATA_LEN/binary, RomCrcBin:2/binary, _/binary>>,
      <<"CSP",0, _Version:2/binary, _SetVer:2/binary, SetBytesBin:2/binary,
	_CrcSet:2/binary, RomFpBin:2/binary, _Nq:2/binary,
	_RamMagic:4/binary, RamHeader:?HEADER_DATA_LEN/binary, _RamCrc:2/binary,
	_NDis:2/binary, _CrcDis:2/binary, _Crc:2/binary, Rest/binary>>) ->
    <<_:4/binary, RomHeader:?HEADER_DATA_LEN/binary, _/binary>> = Rom,
    F = ctx(RomHeader),
    Disasm = disasm(Rom),
    RomCrc = u16(F, RomCrcBin),
    RomFp = u16(F, RomFpBin),
    SetBytes = u16(F, SetBytesBin),
    case Disasm of
	{ok, {Header, Strings, Decls, Code}} when RomFp =:= RomCrc ->
	    Ram = decode_header(F, RamHeader),
	    ND = maps:get(n_decl, Ram),
	    NI = maps:get(n_instr, Ram),
	    <<_Settings:SetBytes/binary, DeclBin:(8*ND)/binary,
	      InstrBin:(4*NI)/binary, _Dis/binary>> = Rest,
	    {Code1, Strings1} = disasm_code(F, InstrBin, maps:get(n_instr, Header),
					    maps:size(Strings), Strings, []),
	    %% The image's trailing `end' is dropped when it is loaded
	    %% (csp_load_image), and the patch's first declaration takes its
	    %% index.
	    ND0 = maps:get(n_decl, Header),
	    Decls0 = case lists:last(Decls) of
			 {decl, I, 'end'} when I =:= ND0 - 1 -> lists:droplast(Decls);
			 _ -> Decls
		     end,
	    Base = case Decls0 =:= Decls of true -> ND0; false -> ND0 - 1 end,
	    Decls1 = disasm_decl(F, DeclBin, Base, Strings1, []),
	    {ok, {Header, Strings1, Decls0 ++ Decls1, Code ++ Code1}};
	{ok, _} -> {error, {other_rom, RomFp, RomCrc}};
	Error -> Error
    end;
patch(_Rom, _Eeprom) ->
    {error, bad_eeprom}.

%% The rules the patch has switched off, by number as /list numbers them:
%% the #disable bitmap after its instructions, n_dis bits in 32-bit words of
%% the node's byte order (set_group_t, DIS_BYTES in csp_eeprom.c). [] when it
%% has none, or is not a patch of this image.
disabled(Rom = <<_:4/binary, RomHeader:?HEADER_DATA_LEN/binary, _/binary>>,
	 <<"CSP",0, _Version:2/binary, _SetVer:2/binary, SetBytesBin:2/binary,
	   _CrcSet:2/binary, _RomFpBin:2/binary, _Nq:2/binary,
	   _RamMagic:4/binary, RamHeader:?HEADER_DATA_LEN/binary, _RamCrc:2/binary,
	   NDisBin:2/binary, _CrcDis:2/binary, _Crc:2/binary, Rest/binary>>) ->
    F = ctx(RomHeader),
    _ = Rom,
    Ram = decode_header(F, RamHeader),
    Skip = u16(F, SetBytesBin) + 8 * maps:get(n_decl, Ram) +
	4 * maps:get(n_instr, Ram),
    NDis = u16(F, NDisBin),
    Words = (NDis + 31) div 32,
    case Rest of
	<<_:Skip/binary, Bits:(4 * Words)/binary, _/binary>> ->
	    Ws = [u32(F, W) || <<W:4/binary>> <= Bits],
	    [I + 1 || I <- lists:seq(0, NDis - 1),
		      (lists:nth(I div 32 + 1, Ws) bsr (I rem 32)) band 1 =:= 1];
	_ -> []
    end;
disabled(_Rom, _Eeprom) -> [].

decode_header(F, Bin) ->
    decode_header_(maps:get(endian, F), Bin).

decode_header_(little,
  <<Size:32/little,    %% total bytes of the image object
    Version:16/little, %% ROM_FORMAT_VERSION at generation
    Role:8,               %% CSP_ROLE_*: what this image is for
    Flags:8,              %% CSP_IMG_F_*: how its values read
    Generation:16/little, %% higher is newer; orders A against B
    N_str:16/little,      %% str bytes (excl. sentinel + trailer)
    N_decl:16/little,     %% decl entries (excl. DECL_END_MARK)
    N_instr:16/little,    %% instr entries (excl. OP_END_MARK)
    N_edg:16/little,      %% edg entries (0 = no reactive graph)
    Crc_str:16/little,    %% CRC-16/CCITT per section
    Crc_decl:16/little,
    Crc_instr:16/little,
    Crc_graph:16/little,  %% over idg + ofs + edg (0 when n_edg == 0)
    Ofs_str:32/little,   %% section DATA starts, bytes from the image base
    Ofs_decl:32/little, %% (each section's prologue sits just before its data)
    Ofs_instr:32/little,
    Ofs_idg:32/little,
    Ofs_ofs:32/little,
    Ofs_edg:32/little>>) ->
  #{
    size => Size, version => Version, role => Role, generation => Generation,
    flags => Flags, float => case Flags band 1 of 0 -> fixpoint; 1 -> ieee end,
    endian => case Flags band 2 of 0 -> little; 2 -> big end,
    n_str => N_str, crc_str => Crc_str, ofs_str => Ofs_str,
    n_decl => N_decl, crc_decl => Crc_decl, ofs_decl => Ofs_decl,
    n_instr => N_instr, crc_instr => Crc_instr, ofs_instr => Ofs_instr,
    n_edg => N_edg,  crc_graph => Crc_graph, 
    ofs_idg => Ofs_idg, ofs_ofs => Ofs_ofs, ofs_edg => Ofs_edg };

decode_header_(big,
  <<Size:32/big,    %% total bytes of the image object
    Version:16/big, %% ROM_FORMAT_VERSION at generation
    Role:8,               %% CSP_ROLE_*: what this image is for
    Flags:8,              %% CSP_IMG_F_*: how its values read
    Generation:16/big, %% higher is newer; orders A against B
    N_str:16/big,      %% str bytes (excl. sentinel + trailer)
    N_decl:16/big,     %% decl entries (excl. DECL_END_MARK)
    N_instr:16/big,    %% instr entries (excl. OP_END_MARK)
    N_edg:16/big,      %% edg entries (0 = no reactive graph)
    Crc_str:16/big,    %% CRC-16/CCITT per section
    Crc_decl:16/big,
    Crc_instr:16/big,
    Crc_graph:16/big,  %% over idg + ofs + edg (0 when n_edg == 0)
    Ofs_str:32/big,   %% section DATA starts, bytes from the image base
    Ofs_decl:32/big, %% (each section's prologue sits just before its data)
    Ofs_instr:32/big,
    Ofs_idg:32/big,
    Ofs_ofs:32/big,
    Ofs_edg:32/big>>) ->
  #{
    size => Size, version => Version, role => Role, generation => Generation,
    flags => Flags, float => case Flags band 1 of 0 -> fixpoint; 1 -> ieee end,
    endian => case Flags band 2 of 0 -> little; 2 -> big end,
    n_str => N_str, crc_str => Crc_str, ofs_str => Ofs_str,
    n_decl => N_decl, crc_decl => Crc_decl, ofs_decl => Ofs_decl,
    n_instr => N_instr, crc_instr => Crc_instr, ofs_instr => Ofs_instr,
    n_edg => N_edg,  crc_graph => Crc_graph, 
    ofs_idg => Ofs_idg, ofs_ofs => Ofs_ofs, ofs_edg => Ofs_edg }.

decode_sections(F, Header, <<S,E,C,T,LenBin:4/binary,Rest/binary>>) ->
    Len = u32(F, LenBin),
    <<SectionData:Len/binary, Sections/binary>> = Rest,
    Tag = [S,E,C,T],
    [{Tag, SectionData} | decode_sections(F, Header,Sections)];
decode_sections(_F, _Header, <<>>) ->
    [].

-define(MAP(Key,Val), Val => Key).

reg(I) -> maps:get(I band ?REG_MASK, reg()).
reg() -> #{ ?REG_MAP }.

part(P) -> maps:get(P band ?PART_MASK, part()).
part() -> #{ ?PART_MAP }.

type(T) -> maps:get(T band ?TYPE_MASK, type()).
type() -> #{ ?TYPE_MAP }.

dir(D) -> maps:get(D band ?DIR_MASK, dir()).
dir() -> #{ ?DIR_MAP }.

endian(E) -> maps:get(E band ?ENDIAN_MASK, endian()).
endian() -> #{ ?ENDIAN_MAP }.

avt(0) -> [];
avt(Avt) -> [type(Avt) | avt(Avt bsr 4)].
    
decl(T) -> maps:get(T band ?DECL_TYPE_MASK, decl()).
decl() -> #{ ?DECL_MAP }.

op_class(OpCode) -> maps:get(OpCode, ?OPCLASS_MAP).

op_code() -> #{ ?OPCODE_MAP }.
op_code(Op) -> maps:get(Op, op_code()).
    
disasm_code(F, ok, Sections) ->
    {_, Code} = lists:keyfind("CODE", 1, Sections),
    disasm_code(F, Code, 0, 1, #{ 0 => ""}, []).

disasm_code(F, <<InstrBin:4/binary, Data/binary>>, Addr, Si, Strings, Acc) ->
    Instr = word(F, InstrBin),
    {Pos0, [{opcode,Op}]} = decode_bits(Instr, 0, [{opcode,uint,?OPCODE_BITS}]),
    OpCode = maps:get(Op, op_code()),
    OpClass = op_class(OpCode),
    case OpClass of
	'end' ->
	    {lists:reverse(Acc), Strings};
	seg ->
	    {_Pos, [{num,Num},{used,Used}]} = 
		decode_bits(Instr, Pos0,
			    [{'_',2},
			     {num,uint,?BODY_BITS},
			     {'_', 6},
			     {used,uint,8}]),
	    <<StringData:(4*Num)/binary, Data1/binary>> = Data,
	    I = {instr,Addr,OpCode,[{slots,Num},{used,Used},{data,StringData}]},
	    {Si1, Strings1} = add_strings(Used, StringData, Si, Strings),
	    add_instr(F, Data1, Addr, 1+Num, I, Si1, Strings1, Acc);
	instate ->
	    {_Pos,[{x,X},{imm,Imm},{nxt,Nxt},{implicit,Imp}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {imm, int, 8},
			     {nxt, int, 13},
			     {implicit, uint, 1}]),
	    Implicit = if Imp =:= 1 -> [implicit]; true -> [] end,
	    I = {instr, Addr, OpCode, [reg(X),Imm,{nxt,Nxt}|Implicit]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	rule ->
	    {_Pos,[{cnd,Cnd},{nxt,Nxt},{implicit,Imp}]} =
		decode_bits(Instr, Pos0,
			    [{cnd, uint, ?REG_BITS},
			     {'_', 6},
			     {nxt, int, 15},
			     {implicit, uint, 1}]),
	    Implicit = if Imp =:= 1 -> [implicit]; true -> [] end,
	    I = {instr, Addr, OpCode, [reg(Cnd),Nxt|Implicit]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	enter ->
	    {_Pos,[{num,Num},{mx,Mx}]} =
		decode_bits(Instr, Pos0,
			    [{num, uint, ?BODY_BITS},
			     {mx,  uint, ?INDEX_BITS}]),
	    I = {instr,Addr,OpCode,[{mem,Mx},{n,Num}]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	leave ->
	    {_Pos,[{num,Num},{mx,Mx}]} =
		decode_bits(Instr, Pos0,
			    [{num, uint, ?BODY_BITS},
			     {mx,  uint, ?INDEX_BITS}]),
	    I = {instr,Addr,OpCode,[{mem,Mx},{n,Num}]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	next ->
	    {_Pos,[{x,X}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS}]),
	    I = {instr,Addr,OpCode,[reg(X)]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	new ->
	    {_Pos,[{obj,Obj}]} =
		decode_bits(Instr, Pos0,
			    [{'_', 2},
			     {obj, uint, ?INDEX_BITS}]),
	    I = {instr,Addr,OpCode,[{obj,Obj}]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	call ->
	    {_Pos,[{x,X},{idx,Idx},{usr,Usr},{avt,Avt}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {idx, uint, ?FUNC_BITS},
			     {usr, uint, 1},
			     {avt, uint, 16}]),
	    Sig = avt(Avt),
	    I = {instr,Addr,OpCode,[reg(X),{idx,Idx},{usr,Usr},{avt,Sig}]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	alu1 ->
	    {_Pos,[{x,X},{y,Y},{u,U},{swap,_Swap}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {y, uint, ?REG_BITS},
			     {'_', ?REG_BITS},
			     {u, uint, 1},
			     {swap, uint, 1}]),
	    I = {instr, Addr, OpCode, [reg(X),reg(Y)] ++ [unsigned || U =:= 1]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	alu2 ->
	    %% swap: `a > b' was emitted as `b < a', operands exchanged -- the
	    %% listing needs it to put the source back.
	    {_Pos,[{x,X},{y,Y},{z,Z},{u,U},{swap,Swap}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {y, uint, ?REG_BITS},
			     {z, uint, ?REG_BITS},
			     {u, uint, 1},
			     {swap, uint, 1}]),
	    I = {instr, Addr, OpCode, [reg(X),reg(Y),reg(Z)] ++
		     [unsigned || U =:= 1] ++ [swap || Swap =:= 1]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	mem ->
	    {_Pos,[{x,X},{y,Y},{mem,Mem}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {y, uint, ?REG_BITS},
			     {'_', 2},
			     {mem, uint, ?INDEX_BITS}]),
	    I = case OpCode of
		    'TMO' -> {instr,Addr,OpCode,[reg(X), {mem,Mem}]};
		    'CHG' -> {instr,Addr,OpCode,[reg(X), {mem,Mem}]};
		    'LDP' -> {instr, Addr, OpCode, [reg(X),{mem,Mem},part(Y)]};
		    'STP' -> {instr, Addr, OpCode, [reg(X),{mem,Mem},part(Y)]};
		    _ -> {instr, Addr, OpCode, [reg(X), {mem,Mem}]}
		end,
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	imm ->
	    {_Pos,[{x,X},{imm,Imm}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {'_',6},
			     {imm, int, 16}]),
	    I = case OpCode of
		    'LIU' ->
			UImm = Imm band ((1 bsl 16)-1),
			{instr, Addr, OpCode, [reg(X), {imm,UImm}]};
		    'LIH' ->
			UImm = Imm band ((1 bsl 16)-1),
			{instr, Addr, OpCode, [reg(X), {imm,UImm}]};
		    _ ->
			{instr, Addr, OpCode, [reg(X), {imm,Imm}]}
		end,
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	memi ->
	    {_Pos,[{x,X},{imm,Imm},{mem,Mem}]} =
		decode_bits(Instr, Pos0,
			    [{x, uint, ?REG_BITS},
			     {imm, int, ?TINY_BITS},
			     {mem, uint, ?INDEX_BITS}]),
	    I = {instr, Addr, OpCode, [reg(X), {imm,Imm}, {mem,Mem}]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	seto ->
	    {_Pos,[{obj,Obj}]} = 
		decode_bits(Instr, Pos0,
			    [{'_', 2},
			     {obj, uint, 16}]),
	    I = {instr,Addr,OpCode,[{obj,Obj}]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	setox ->
	    %% bits = 1: stride is in BITS and moves the next buffer view
	    %% (`Frame[BytePos]`), else declarations per array element
	    {_Pos,[{bits,Bits},{len,Len},{x,X},{stride,Stride}]} =
		decode_bits(Instr, Pos0,
			    [{'_', 1},
			     {bits, uint, 1},
			     {len, uint, 14},
			     {x, uint, ?REG_BITS},
			     {stride, uint, 6}]),
	    I = {instr,Addr,OpCode,[reg(X),{len,Len},{stride,Stride}] ++
		     [bits || Bits =:= 1]},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);	    
	nop ->
	    I = {instr,Addr,OpCode},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc);
	undefined -> %% krash!?
	    I = {instr,Addr,OpCode,undefined},
	    add_instr(F, Data, Addr, 1, I, Si, Strings, Acc)
    end;
disasm_code(_F, <<>>, _, _Si,Strings, Acc) ->
    {lists:reverse(Acc), Strings}.

add_strings(0, _, Si, Strings) ->
    {Si, Strings};    
add_strings(Used, <<N,String:N/binary, StringData/binary>>, Si, Strings) ->
    add_strings(Used - (N+1), StringData, Si+1, Strings#{ Si => String }).

add_instr(F, Data, Addr, Size, I, Si, String, Acc) ->
    io:format("~w: ~p\n", [Addr, I]),
    disasm_code(F, Data, Addr+Size, Si, String, [I|Acc]).

disasm_decl(F, ok, Strings, Sections) ->
    {_, Decl} = lists:keyfind("DECL", 1, Sections),
    disasm_decl(F, Decl, 0, Strings, []).  %% arrays()

%% An array is one entry, the way the source declares it: `#analog P[10]:16 out
%% unsigned 9:0..9` is {decl,I,analog,"P",[{len,10},...,{elems,[...]}]}. What
%% every element shares is listed once, what differs (a pin, a constant's
%% value) per element under elems, in order -- left out when nothing differs.
%% The elements occupy I..I+len-1, which is what an OP_SETOX bounds-checks
%% against.
%%
%% In the image an array is still a head and len-1 continuations right after
%% it, nameless and flagged cont -- see array_replicate in csp_compile.c. That
%% is a storage detail and this is where it stops: should the image one day
%% hold an array as one declaration, only the decoding above has to change.
-if(not_used).

arrays([]) -> [];
arrays([H | T]) ->
    {Es, Rest} = lists:splitwith(fun(D) -> cont(D) =:= 1 end, T),
    [array(H, Es) | arrays(Rest)].

array(H, []) ->
    with_opts(H, props(H));
array(H, Es) ->
    Ps = [props(D) || D <- [H | Es]],
    Common = [P || P <- props(H),
		   lists:all(fun(Q) -> lists:member(P, Q) end, Ps)],
    Each = [[P || P <- Q, not lists:member(P, Common)] || Q <- Ps],
    Elems = case lists:all(fun(E) -> E =:= [] end, Each) of
		true  -> [];                    % identical: len says it all
		false -> [{elems, Each}]
	    end,
    with_opts(H, [{len, length(Ps)} | Common] ++ Elems).

props(D) -> [P || P <- opts(D), element(1, P) =/= cont].

cont(D) ->
    case lists:keyfind(cont, 1, opts(D)) of
	{cont, C} -> C;
	false     -> 0
    end.

%% The option list is the last element; {decl,I,'end'} has none.
opts(D) ->
    case element(tuple_size(D), D) of
	L when is_list(L) -> L;
	_                 -> []
    end.

with_opts(D, Opts) ->
    case element(tuple_size(D), D) of
	L when is_list(L) -> setelement(tuple_size(D), D, Opts);
	_                 -> D
    end.
-endif.


disasm_decl(F, <<DeclBin:8/binary, Data/binary>>, 
	    I, Strings, Acc) ->
    Decl = word(F, DeclBin),
    {Pos, [{_, IDeclType}, {_, _ICont}, {_, ILocal}, {_, IDir}, {_, IName}]} =
	 decode_bits(Decl, [{type,uint,?DECL_TYPE_BITS},
			    {cont,uint,1},{local,uint,1},{dir,uint,?DIR_BITS},
			    {name,uint,?NAMEID_BITS}]),
    Name = maps:get(IName, Strings),
    case decl(IDeclType) of
	end_mark -> 
	    lists:reverse(Acc);
	'end' ->
	    D = {decl,I,'end'},
	    add_decl(F, Data, I, 1, D, Strings, Acc);	    
	states ->
	    {_Pos,[{_,Name2},{_,Name3},{_,Name4},{_,Name5},{_,Name6}]} =
		decode_bits(Decl, Pos, 
			[{name2,uint,?NAMEID_BITS},
			 {name3,uint,?NAMEID_BITS},
			 {name4,uint,?NAMEID_BITS},
			 {name5,uint,?NAMEID_BITS},
			 {name6,uint,?NAMEID_BITS}]),
	    D = {decl,I,states,Name,[{name2,maps:get(Name2, Strings)},
				     {name3,maps:get(Name3, Strings)},
				     {name4,maps:get(Name4, Strings)},
				     {name5,maps:get(Name5, Strings)},
				     {name6,maps:get(Name6, Strings)}]},
	    add_decl(F, Data, I, 1, D, Strings, Acc);
	module ->
	    {32, _THeader} = decode_type_header(Decl, Pos),
	    {_Pos2, [{n,N},{ent,Ent}]} = 
		decode_bits(Decl, 32, 
			    [{n, uint, ?INDEX_BITS},
			     {ent, uint, ?INDEX_BITS}]),
	    D = {decl,I,module,Name,[{n,N},{ent,Ent}]},
	    add_decl(F, Data, I, 1, D, Strings, Acc);
	object ->
	    {32, _THeader} = decode_type_header(Decl, Pos),
	    {_Pos2, [{mx,Mx},{m,M}]} = 
		decode_bits(Decl, 32,
			    [{mx, uint, ?INDEX_BITS},
			     {m, uint, 16}]),
	    ModuleDecl = find_decl(Mx, Acc),
	    ModuleName = element(4, ModuleDecl),
	    D = {decl,I,object,ModuleName,Name,[{mx,Mx},{m,M}]},
	    add_decl(F, Data, I, 1, D, Strings, Acc);
	variable ->
	    {LType, Params} = disasm_elem(F, variable,ILocal,IDir,Pos,Decl,Strings),
	    D = {decl,I,LType,Name,[]},
	    disasm_array(F, Data, I, 1, D, Strings, [Params], Acc);
	constant ->
	    {LType, Params} = disasm_elem(F, constant,ILocal,IDir,Pos,Decl,Strings),
	    D = {decl,I,LType,Name,[]},
	    disasm_array(F, Data, I, 1, D, Strings, [Params], Acc);
	digital ->
	    {digital, Params} = disasm_elem(F, digital,ILocal,IDir,Pos,Decl,Strings),
	    D = {decl,I,digital,Name,[]},
	    disasm_array(F, Data, I, 1, D, Strings, [Params], Acc);
	analog ->
	    {analog, Params} = disasm_elem(F, analog,ILocal,IDir,Pos,Decl,Strings),
	    D = {decl,I,analog,Name,[]},
	    disasm_array(F, Data, I, 1, D, Strings, [Params], Acc);
	timer ->
	    {32, [{res,Res},{is_mapped,_},
		  {bound,_},{nx,_},{vt,Vt},{reg,_}]} = 
		decode_type_header(Decl, Pos),
	    Type = type(Vt),
	    
	    [{period,Period},{fired,_},{running,_},{value,Init}] = 
		decode_value(F, Decl, 32, timer, Strings),
	    D =  {decl,I,timer,Name,[{size,Res+1},
				     {type,Type},
				     {period,Period},
				     {init,Init}
				    ]},
	    drop_empty_decl(F, Data, I, 1, D, Strings, Acc);

	%% A window into a #buffer: a #field has a name, the view a slice like
	%% `Buf[2..3]' makes has none. bit is a BIT position, len the width.
	Window when Window =:= field; Window =:= view ->
	    {32, [{res,_},{is_mapped,_},{bound,_},{nx,_},{vt,Vt},{reg,_}]} =
		decode_type_header(Decl, Pos),
	    D = {decl,I,Window,Name,[{dir,dir(IDir)},{type,type(Vt)}|ca(Decl)]},
	    add_decl(F, Data, I, 1, D, Strings, Acc);
	%% id names the constant holding the transport endpoint (and id+1, for
	%% the ones that need two).
	buffer ->
	    {_, [{nbytes,N},{transport,Tr},{id,Id}]} =
		decode_bits(Decl, 32, [{nbytes,uint,10},{transport,uint,4},
				       {'_',2},{id,uint,16}]),
	    D = {decl,I,buffer,Name,[{dir,dir(IDir)},{nbytes,N},
				     {transport,transport(Tr)},{id,Id}]},
	    add_decl(F, Data, I, 1, D, Strings, Acc);
	%% Two #buffer declarations.
	route ->
	    {_, [{src,Src},{dst,Dst}]} =
		decode_bits(Decl, 32, [{src,uint,16},{dst,uint,16}]),
	    D = {decl,I,route,Name,[{src,Src},{dst,Dst}]},
	    add_decl(F, Data, I, 1, D, Strings, Acc);
	DeclType ->
	    D = {decl,I,DeclType,Name,[{dir,dir(IDir)},{local,ILocal}]},
	    add_decl(F, Data, I, 1, D, Strings, Acc)
    end;
disasm_decl(_F, <<>>, _I, _Strings, Acc) ->
    lists:reverse(Acc).

disasm_elem(F, variable,ILocal,IDir,Pos,Decl,Strings) ->
    {32, [{res,Res},{is_mapped,_},
	  {bound,Bound},{nx,_},{vt,Vt},{reg,_}]} = 
	decode_type_header(Decl, Pos),
    Type = type(Vt),
    %% `bind Buf[lo..hi]': a bit-field view, so the arm is the window and
    %% there is no init value.
    Init = case Bound of
	       1 -> {bind, ca(Decl)};
	       0 -> decode_value(F, Decl, 32, Type, Strings)
	   end,
    case ILocal of
	0 -> {variable,[{size,Res+1},
			{dir,dir(IDir)},
			{type,Type},
			{init,Init}]};
	1 -> {local,[{size,Res+1},
		     {dir,dir(IDir)},
		     {type,Type},
		     {init,Init}]}
    end;
disasm_elem(F, constant,ILocal,_IDir,Pos,Decl,Strings) ->    
    {32, [{res,Res},{is_mapped,_},
	  {bound,_},{nx,_},{vt,Vt},{reg,_}]} = 
	decode_type_header(Decl, Pos),
    Type = type(Vt),
    Init = decode_value(F, Decl, 32, Type, Strings),
    case ILocal of
	0 -> {constant,[{size,Res+1},
			{type,Type},
			{init,Init}
		       ]};
	1 -> {param,[{size,Res+1},
		     {type,Type},
		     {init,Init}
		    ]}
    end;
disasm_elem(F, digital,_ILocal,IDir,Pos,Decl,Strings) ->        
    {32, [{res,Res},{is_mapped,_},
	  {bound,_},{nx,_},{vt,_Vt},{reg,_}]} = 
	decode_type_header(Decl, Pos),
    %% Type = type(Vt),
    [{soft,Soft},{pin,Pin},{port,Port},{irq,Irq},{pullup,Pu},{pulldown,Pd}] = 
	decode_value(F, Decl, 32, digital, Strings),
    {digital,[{size,Res+1},
	      {dir, dir(IDir)},
	      {pin,Pin},
	      {port,Port},
	      {soft,Soft},
	      {irq,Irq},
	      {pullup,Pu},
	      {pulldown,Pd}
	     ]};
disasm_elem(F, analog,_ILocal,IDir,Pos,Decl,Strings) ->
    {32, [{res,Res},{is_mapped,_},
	  {bound,_},{nx,_},{vt,Vt},{reg,_}]} = 
	decode_type_header(Decl, Pos),
    Type = type(Vt),
    [{soft,Soft},{pin,Pin},{port,Port},{irq,Irq},
     {pwm,Pwm},{endian,Endian}] =
	decode_value(F, Decl, 32, analog, Strings),
    {analog,[{size,Res+1},
	     {type,Type},
	     {dir,dir(IDir)},
	     {pin,Pin},
	     {port,Port},
	     {soft,Soft},
	     {irq, Irq},
	     {pwm,Pwm},
	     {endian,endian(Endian)}
	    ]}.

%% The window arm of a #field, a buffer view and a bound variable.
ca(Decl) ->
    {_, [{id,Id},{endian,E},{bit,Bit},{len,Len}]} =
	decode_bits(Decl, 32, [{id,uint,16},{endian,uint,2},{bit,uint,9},
			       {len,uint,5}]),
    [{id,Id},{endian,endian(E)},{bit,Bit},{len,Len+1}].

%% transport_t in csp.h.
transport(T) ->
    element(T+1, {none, pin, can, i2c, spi, udp, console, repl, tcp, uart,
		  sms, onewire, T, T, T, T}).

find_decl(Mx, Ds) ->
    lists:keyfind(Mx, 2, Ds).

add_decl(F, Data, I, Size, D, Strings, Acc) ->
    disasm_decl(F, Data, I+Size, Strings, [D|Acc]).

%% Drop decl if next name = 0 (crash otherwise)
drop_empty_decl(F, <<DeclBin:8/binary, Data/binary>>, I, Size, D, Strings, Acc) ->
    Decl = word(F, DeclBin),
    {_Pos, [{_, _IDeclType}, {_, _ICont}, {_, _ILocal}, {_, _IDir}, {_, 0}]} =
	decode_bits(Decl, [{type,uint,?DECL_TYPE_BITS},
			   {cont,uint,1},{local,uint,1},{dir,uint,?DIR_BITS},
			   {name,uint,?NAMEID_BITS}]),
    D1 = add_params(D, [{slots, 2}]),
    disasm_decl(F, Data, I+Size+1, Strings, [D1|Acc]).

disasm_array(F, Data = <<DeclBin:8/binary, Data1/binary>>,
	      I, Size, D, Strings, Elems, Acc) ->
    Decl = word(F, DeclBin),
    {Pos, [{_, IDeclType}, {_, ICont}, {_, ILocal}, {_, IDir}, {_, IName}]} =
	decode_bits(Decl, [{type,uint,?DECL_TYPE_BITS},
			   {cont,uint,1},{local,uint,1},{dir,uint,?DIR_BITS},
			   {name,uint,?NAMEID_BITS}]),
    DType = decl(IDeclType),
    HType = stored_type(element(3, D)),
    if DType =:= HType, ICont =:= 1, IName =:= 0 ->
	    %% yes this slot is collected, part of array
	    {_,Params} = disasm_elem(F, DType,ILocal,IDir,Pos,Decl,Strings),
	    disasm_array(F, Data1,I+1,Size+1,D,Strings,[Params|Elems],Acc);
       ICont =:= 0 ->
	    array_end(F, Data, I, Size, D, Strings, Elems, Acc)
    end;
%% The end of the data with no end mark after it: a patch from the EEPROM
%% store is just its records.
disasm_array(F, <<>>, I, Size, D, Strings, Elems, Acc) ->
    array_end(F, <<>>, I, Size, D, Strings, Elems, Acc).

array_end(F, Data, I, Size, D, Strings, Elems, Acc) ->
	    D1 = if Size > 1 ->
			 {Params0,Es} = make_elems(lists:reverse(Elems)),
			 %% identical elements: len says it all
			 EsP = case lists:all(fun(E) -> E =:= [] end, Es) of
				   true  -> [];
				   false -> [{elems,Es}]
			       end,
			 Params = [{len,Size},{slots,Size}] ++ Params0 ++ EsP,
			 add_params(D, Params);
		    %% a scalar: its one element's parameters are its own
		    true -> add_params(D, hd(Elems))
		 end,
	    disasm_decl(F, Data, I+1, Strings, [D1|Acc]).
	    
%% The declaration type an element is STORED as. A #param head is shown as
%% `param` and a #local as `local`, but in the image both are the plain type
%% with the local bit set -- and so are their continuations.
stored_type(param) -> constant;
stored_type(local) -> variable;
stored_type(Type)  -> Type.

add_params({decl,I,Type,Name,Params}, NewParams) ->
    {decl,I,Type,Name,Params++NewParams}.

make_elems(Elems) ->
    I = intersect_elems(Elems),
    {I, [ (Es -- I) || Es <- Elems]}.

intersect_elems([A,B|Bs]) -> intersect_elems(Bs, A -- (A -- B)).
intersect_elems([A|Bs], I) -> intersect_elems(Bs, (A -- (A -- I)));
intersect_elems([], I) -> I.
    

decode_type_header(Decl, Pos) ->
    decode_bits(Decl, Pos,
		[{res, uint, 5},
		 {is_mapped, uint, 1},
		 {bound, uint, 1},
		 {nx, uint, 1},
		 {vt, uint, ?TYPE_BITS},
		 {reg, uint, ?REG_BITS}]).

decode_value(_F, Decl, Pos, integer, _Strings) ->
    {_Pos1, [{ivalue,Value}]} = decode_bits(Decl, Pos, [{ivalue,int,32}]),
    Value;
decode_value(_F, Decl, Pos, unsigned, _Strings) ->
    {_Pos1, [{uvalue,Value}]} = decode_bits(Decl, Pos, [{uvalue,uint,32}]),
    Value;
decode_value(F, Decl, Pos, float, _Strings) ->
    %% Q16.16 or IEEE binary32, as the header's flags say.
    {_Pos1, [{fvalue,Bits}]} = decode_bits(Decl, Pos, [{fvalue,int,32}]),
    case maps:get(float, F) of
	ieee -> <<V:32/float-little>> = <<Bits:32/little>>, V;
	fixpoint -> Bits / 65536
    end;
decode_value(_F, Decl, Pos, string, Strings) ->
    {_Pos1, [{sindex,Index}]} = decode_bits(Decl, Pos, [{sindex,int,32}]),
    maps:get(Index, Strings);
decode_value(_F, Decl, Pos, timer, _Strings) ->
    {_Pos1, Init} = decode_bits(Decl, Pos, [{period,uint,28},
					    {'_', 1},  %% not used
					    {fired,uint,1},
					    {running,uint,1},
					    {value,uint,1}]),
    Init;
decode_value(_F, Decl, Pos, digital, _Strings) ->
    {_Pos1, Init} = decode_bits(Decl, Pos, [{soft,uint,1},
					    {pin,uint,?PIN_BITS},
					    {port,uint,?PORT_BITS},
					    {irq,uint,3},
					    {pullup,uint,1},
					    {pulldown,uint,1}]),
    Init;
decode_value(_F, Decl, Pos, analog, _Strings) ->
    {_Pos1, Init} = decode_bits(Decl, Pos, [{soft,uint,1},
					    {pin,uint,?PIN_BITS},
					    {port,uint,?PORT_BITS},
					    {irq,uint,3},
					    {pwm,uint,1},
					    {endian,uint,2}
					   ]),
    Init.


-define(get_uint(X, Start, Len),
	(((X) bsr (Start)) band ((1 bsl (Len))-1))).

-define(get_int(X, Start, Len),
	if ((X) band (1 bsl ((Start)+(Len)-1))) =:= 0 ->
		?get_uint(X, Start, Len-1);
	   true ->
		-(((bnot ?get_uint(X, Start, Len-1)) band ((1 bsl (Len-1))-1))+1)
	end).

decode_bits(Bits, Fields) ->
    decode_bits(Bits, 0, Fields, []).
decode_bits(Bits, Pos, Fields) ->
    decode_bits(Bits, Pos, Fields, []).

decode_bits(Bits, Pos, [{'_',Len}|Fields], Acc) -> %% pad
    decode_bits(Bits, Pos+Len, Fields, Acc);
decode_bits(Bits, Pos, [{Name,uint,Len}|Fields], Acc) ->
    decode_bits(Bits, Pos+Len, Fields, 
		[{Name, ?get_uint(Bits, Pos, Len)}|Acc]);
decode_bits(Bits, Pos, [{Name,int,Len}|Fields], Acc) ->
    decode_bits(Bits, Pos+Len, Fields, 
		[{Name, ?get_int(Bits, Pos, Len)}|Acc]);
decode_bits(_Bits, Pos, [], Acc) ->
    {Pos, lists:reverse(Acc)}.
    

check_section_crc(Tag, LenKey, CrcKey, Header, Sections) ->
    %% Crc = crc16(<<S,E,C,T,Len:32/little,SectionData:Len/binary>>),
    Len1 = case Tag of
	       "STRS" -> maps:get(n_str, Header);
	       "DECL" -> maps:get(n_decl, Header)*8;
	       "CODE"  -> maps:get(n_instr, Header)*4
	   end,
    case lists:keyfind(Tag, 1, Sections) of
	false -> ok;
	{Tag, Data} ->
	    <<Data1:Len1/binary, _/binary>> = Data,
	    ComputedCrc = crc16(Data1),
	    case maps:get(CrcKey, Header) of
		0 ->
		    case maps:get(LenKey, Header) of
			0 ->
			    io:format("~s: Crc=~p, expected=~p\n", 
				      [Tag, 0, 0]),
			    ok;
			_Len -> 
			    io:format("~s: Crc=~p, expected=~p\n", 
				      [Tag, 0, ComputedCrc]),
			    badcrc
		    end;
		Crc ->
		    io:format("~s: Crc=~p, expected=~p\n", 
			      [Tag, Crc, ComputedCrc]),
		    if Crc =:= ComputedCrc -> ok;
		       true -> badcrc
		    end
	    end
    end.

check_grapg_crc(Header, Sections) ->
    case maps:get(n_edg, Header, 0) of
	0 -> ok;  %% no graph
	NE ->
	    {_, IDG} = lists:keyfind("GIDG", 1, Sections),
	    {_, OFS} = lists:keyfind("GOFS", 1, Sections),
	    {_, EDG} = lists:keyfind("GEDG", 1, Sections),
	    N = maps:get(n_decl, Header, 0),
	    LenIDG = N * 2,
	    LenOFS = if N =:= 0 -> 0;
		true -> (N+1)*2
		     end,
	    LenEDG = NE * 2,
	    <<IDG1:LenIDG/binary, _/binary>> = IDG,
	    <<OFS1:LenOFS/binary, _/binary>> = OFS,
	    <<EDG1:LenEDG/binary, _/binary>> = EDG,
	    Crc1 = crc16(16#ffff, IDG1),
	    Crc2 = crc16(Crc1,    OFS1),
	    Crc3 = crc16(Crc2,    EDG1),
	    case maps:get(crc_graph, Header) of
		Crc3 -> ok;
		_ -> badcrc
	    end
    end.

%% CRC-16/CCITT, incremental: folds n bytes into `crc` and returns it, so a
%% caller can chain several regions (str, then decls, then instrs, ...). is_rom
%% selects ro_byte, which is memcpy_P on AVR (the region is PROGMEM) and a plain
%% read on the host; pass 0 for ordinary RAM. Table-free. Seed with 0xFFFF.
-define(CRC_INIT, 16#ffff).
-define(CRC_POLY, 16#1021).

crc16(Data) ->
    crc16(?CRC_INIT, Data).
crc16(Crc, <<Byte,Data/binary>>) ->
    Crc1 = Crc bxor (Byte bsl 8),
    crc16(crcbit_(8, Crc1, ?CRC_POLY), Data);
crc16(Crc, <<>>) ->
    Crc band 16#ffff.

crcbit_(0, Crc, _) -> Crc;
crcbit_(I, Crc, Poly) ->
    if Crc band 16#8000 =:= 16#8000 ->
	    crcbit_(I-1, (Crc bsl 1) bxor Poly, Poly);
       true ->
	    crcbit_(I-1, (Crc bsl 1), Poly)
    end.
