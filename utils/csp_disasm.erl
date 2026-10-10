%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Candyspeak image disassembler
%%% @end
%%% Created :  8 Oct 2026 by Tony Rogvall <tony@rogvall.se>

-module(csp_disasm).

-export([file/1]).

file(Filename) ->
    case filename:extension(Filename) of
	".hex" ->
	    {ok, HexBin} = file:read_file(Filename),
	    Bin = hex_to_bin(HexBin),
	    disasm(Bin);
	".img" ->
	    {ok, Bin} = file:read_file(Filename),
	    disasm(Bin)
    end.

hex_to_bin(Bin) ->
    HexData = iolist_to_binary(binary:split(Bin, <<"\n">>, [global,trim])),
    << <<(list_to_integer([H,L],16))>> || <<H,L>> <= HexData >>.

-define(HEADER_DATA_LEN, (11*2 + 7*4)).  %% size of fields

disasm(<<$J,$A,$M,$\n, HeaderData:?HEADER_DATA_LEN/binary, Crc:16/little,
	 Body/binary>>) ->
    Crc1 = crc16(<<$J,$A,$M,$\n, HeaderData:?HEADER_DATA_LEN/binary>>),
    io:format("Header Crc=~p, expected=~p\n", [Crc1, Crc]),
    if Crc1 =/= Crc ->
	    error(bad_crc);
       true  ->
	    ok
    end,
    Header = decode_header(HeaderData),
    Sections = decode_sections(Header, Body),
    _STRS_Ok = check_section_crc("STRS", n_str, crc_str, Header, Sections),
    DECLS_OK = check_section_crc("DECL", n_decl, crc_decl, Header, Sections),
    CODE_OK = check_section_crc("CODE", n_instr, crc_instr, Header, Sections),
    _GRAPH_OK = check_grapg_crc(Header, Sections),
    %% FIXME: check graph
    Decl = disasm_decl(DECLS_OK, Sections),
    Code = disasm_code(CODE_OK, Sections),
    {Header, Decl, Code};
disasm(<<$J,$A,$M,$\n, _/binary>>) ->
    error(bad_file_truncated);
disasm(_) ->
    error(bad_magic).

decode_header(
  <<Size:32/little,    %% total bytes of the image object
    Version:16/little, %% ROM_FORMAT_VERSION at generation
    Role:16/little,       %% CSP_ROLE_*: what this image is for
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
    n_str => N_str, crc_str => Crc_str, ofs_str => Ofs_str,
    n_decl => N_decl, crc_decl => Crc_decl, ofs_decl => Ofs_decl,
    n_instr => N_instr, crc_instr => Crc_instr, ofs_instr => Ofs_instr,
    n_edg => N_edg,  crc_graph => Crc_graph, 
    ofs_idg => Ofs_idg, ofs_ofs => Ofs_ofs, ofs_edg => Ofs_edg }.

decode_sections(Header,
		<<S,E,C,T,Len:32/little,SectionData:Len/binary,
		  Sections/binary>>) ->
    Tag = [S,E,C,T],
    [{Tag, SectionData} | decode_sections(Header,Sections)];
decode_sections(_Header, <<>>) ->
    [].

-define(OPCODE_BITS,      6).
-define(BODY_BITS,        10).
-define(REG_BITS,         4).
-define(FUNC_BITS,        5).
-define(PART_BITS,        4).
-define(OBJ_BITS,         1).
-define(DECL_BITS,       15).
-define(INDEX_BITS,      (?OBJ_BITS+?DECL_BITS)).
-define(TINY_BITS, 6).

-define(get_uint(X, Start, Len),
	(((X) bsr (Start)) band ((1 bsl (Len))-1))).

-define(get_int(X, Start, Len),
	if ((X) band (1 bsl ((Start)+(Len)-1))) =:= 0 ->
		?get_uint(X, Start, Len-1);
	   true ->
		-(((bnot ?get_uint(X, Start, Len-1)) band ((1 bsl (Len-1))-1))+1)
	end).

%% Opcodes
-define(OP_NOP, 0).  %% nothing
-define(OP_NOT, 1).     %% "!"  x=-y == x=0-y
-define(OP_BNOT, 2).    %% "~"  x=~y =  x=1^y        
-define(OP_NEG , 3).     %% "-"  x=-y == x=0-y
-define(OP_MOV , 4).     %% "mov" x=y == x=y
-define(OP_CVTIF, 5).   %% trunc float => integer
-define(OP_CVTFI, 6).   %% cast int to float
    %% node - binary operator
-define(OP_ADD  , 7).     %% "+"
-define(OP_SUB  , 8).     %% "-"
-define(OP_MUL  , 9).     %% "*"
-define(OP_DIV  , 10).     %% "/"
-define(OP_REM  , 11).     %% "%"
-define(OP_SLA  , 12).     %% "<<"
-define(OP_SRA  , 13).     %% ">>"    
-define(OP_LT  , 14).      %% "<"
-define(OP_LTE  , 15).     %% "<="
    %% `>` and `>=` used to be 16 and 17; the compiler mirrors them into
    %% OP_LT/OP_LTE with the operands swapped (see mirror_op / asm_alu), so the
    %% numbers came free. 16 is spent again below; 17 is still free.
-define(OP_TMO  , 16).     %% timeout(T): x = the timer's `fired` bit
    %% 17 -- FREE
-define(OP_EQEQ, 18).    %% "=="
-define(OP_NEQ, 19).     %% "!="
-define(OP_BAND, 20).    %% "&"
-define(OP_BOR, 21).     %% "|"
-define(OP_BXOR, 22).    %% "^"
-define(OP_AND, 23).     %% "&&"
-define(OP_OR, 24).      %% "||"

-define(OP_FNEG, 25).     %% "-"  x=-y == x=0-y
-define(OP_FMOV, 26).     %% "mov"  x=y
-define(OP_FADD, 27).     %% "+"
-define(OP_FSUB, 28).     %% "-"
-define(OP_FMUL, 29).     %% "*"
-define(OP_FDIV, 30).     %% "/"

-define(OP_FLT, 31).      %% "<"
-define(OP_FLTE, 32).      %% "<="
    %% 33, 34 -- FREE, for the same reason as 16 and 17 above.
-define(OP_FEQEQ, 35).     %% "=="
-define(OP_FNEQ, 36).     %% "!="    
    
-define(OP_EQ, 37).     %% "="
-define(OP_RIMP, 38).     %% "<-"    

-define(OP_RULE, 39).    %% "?"
-define(OP_NEXT, 40).    %% "next"

-define(OP_ENTER, 41).   %% enter object
-define(OP_LEAVE, 42).   %% leave object
-define(OP_NEW, 43).     %% #<module> <instance-name>
-define(OP_LD, 44).      %% load register from memory
-define(OP_LDP, 45).     %% load register from memory part
-define(OP_ST, 46).      %% store register to memory
-define(OP_STP, 47).     %% store register to memory part
-define(OP_STIMP, 48).  %% store for <- (reactive assign), same as ST but marks rimp
-define(OP_CHG, 49).     %% r |= dset[ix], check if variable changed
-define(OP_LI, 50).      %% load signed 16-bit constant
-define(OP_LIU, 51).     %% load unsigned 16-bit constant (zero extend)
-define(OP_LIH, 52).    %% load high 16-bit (OR into high bits)
-define(OP_ARG, 53).    %% load argument from register
-define(OP_CALL, 54).   %% function call:
-define(OP_STI, 55).    %% store immediate value to memory (mirror of EQI)
    %% A run of identifier text living in the INSTRUCTION pool: this header, then
    %% num slots of characters. Executing it JUMPS the payload, which is what
    %% makes a segment transparent -- it lands mid-stream because a name is
    %% created while code is being generated, and there is no moving it
    %% afterwards without renumbering every jump.
    %%
    %% 17 is a hole (where `>` used to be, before it was mirrored to LT+swap),
    %% taken ahead of 60..62 as the note at OP_AVAIL asks.
-define(OP_SEGMENT,17).

-define(OP_INSTATE,56). %% #in <state> block gate: if reg != state, skip block(nxt)
-define(OP_NINSTATE,57). %% #in A B C OR-chain gate: if reg == state, jump INTO block (nxt)
-define(OP_SETO, 58).    %% point CURRENT at a NAMED object for the next memory access
-define(OP_SETOX, 59).   %% same, but the object number comes from a register (arrays)
-define(OP_END_MARK, 16#3f).

opcodes() ->
    #{ ?OP_NOP => {'NOP', nop},
       ?OP_NOT => {'NOT', alu, 1},
       ?OP_BNOT => {'BNOT', alu, 1},
       ?OP_NEG => {'NEG', alu, 1},
       ?OP_MOV => {'MOV', alu, 1},
       ?OP_CVTIF => {'CVTIF', alu, 1},
       ?OP_CVTFI => {'CVTFI', alu, 1},
       ?OP_ADD   => {'ADD', alu, 2},
       ?OP_SUB   => {'SUB', alu, 2},
       ?OP_MUL   => {'MUL', alu, 2},
       ?OP_DIV   => {'DIV', alu, 2},
       ?OP_REM   => {'REM', alu, 2},
       ?OP_SLA   => {'SLA', alu, 2},
       ?OP_SRA   => {'SRA', alu, 2},
       ?OP_LT    => {'LT',   alu, 2},
       ?OP_LTE   => {'LTE', alu, 2},
       ?OP_TMO   => {'TMO', mem},
       ?OP_SEGMENT => {'SEGMENT', seg},
       ?OP_EQEQ  => {'EQEQ', alu, 2},
       ?OP_NEQ   => {'NEQ',  alu, 2},
       ?OP_BAND  => {'BAND', alu, 2},
       ?OP_BOR   => {'BOR',  alu, 2},
       ?OP_BXOR  => {'BXOR', alu, 2},
       ?OP_AND   => {'AND',  alu, 2},
       ?OP_OR    => {'OR',   alu, 2},
       ?OP_FNEG  => {'FNEG', alu, 1},
       ?OP_FMOV  => {'FMOV', alu, 1},
       ?OP_FADD  => {'FADD', alu, 2},
       ?OP_FSUB  => {'FSUB', alu, 2},
       ?OP_FMUL  => {'FMUL', alu, 2},
       ?OP_FDIV  => {'FDIV', alu, 2},
       ?OP_FLT   => {'FLT',  alu, 2},
       ?OP_FLTE  => {'FLTE', alu, 2},
       ?OP_FEQEQ => {'FEQEQ', alu, 2},
       ?OP_FNEQ  => {'FNEQ', alu, 2},
       ?OP_EQ    => {'EQ', alu ,2},
       ?OP_RIMP  => {'RIMP', alu, 2},
       ?OP_RULE  => {'RULE', rule},
       ?OP_NEXT  => {'NEXT', next},
       ?OP_ENTER => {'ENTER', enter},
       ?OP_LEAVE => {'LEAVE', leave},
       ?OP_NEW   => {'NEW', new},
       ?OP_LD    => {'LD', mem},
       ?OP_LDP   => {'LDP', mem},
       ?OP_ST    => {'ST', mem},
       ?OP_STP   => {'STP', mem},
       ?OP_STIMP => {'STIMP', mem},
       ?OP_CHG   => {'CHG', alu},
       ?OP_LI    => {'LI', imm},
       ?OP_LIU   => {'LIU', imm},
       ?OP_LIH   => {'LIH', imm},
       ?OP_ARG   => {'ARG', imm},
       ?OP_CALL  => {'CALL', call},
       ?OP_STI   => {'STI', memi},
       ?OP_INSTATE => {'INSTATE', instate},
       ?OP_NINSTATE => {'NINSTATE', instate},
       ?OP_SETO => {'SETO', seto},
       ?OP_SETOX => {'SETOX', setox},
       ?OP_END_MARK => {'END', 'end'}
     }.

disasm_code(ok, Sections) ->
    {_, Code} = lists:keyfind("CODE", 1, Sections),
    disasm_code(Code, 0, []).

disasm_code(<<Instr:32/little, Data/binary>>, Addr, Acc) ->
    Op = ?get_uint(Instr, 0, ?OPCODE_BITS),
    case maps:get(Op, opcodes()) of
	{_OpCode, 'end'} ->
	    lists:reverse(Acc);
	{OpCode, instate} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Imm = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS, 8),
	    Nxt = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS+8, 13),
	    Imp = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+8+13, 1),
	    Implicit = if Imp =:= 1 -> [implicit]; true -> [] end,
	    I = {instr, Addr, OpCode, [reg(X),Imm,{nxt,Nxt}|Implicit]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, rule} ->
	    Cnd = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Nxt = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS+6, 15),
	    Imp = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+6+15, 1),
	    Implicit = if Imp =:= 1 -> [implicit]; true -> [] end,
	    I = {instr, Addr, OpCode, [reg(Cnd),Nxt|Implicit]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, enter} ->
	    Num = ?get_uint(Instr, ?OPCODE_BITS, ?BODY_BITS),
	    Mx  = ?get_uint(Instr, ?OPCODE_BITS+?BODY_BITS, ?INDEX_BITS),
	    I = {instr,Addr,OpCode,[{mem,Mx},{n,Num}]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, leave} ->
	    Num = ?get_uint(Instr, ?OPCODE_BITS, ?BODY_BITS),
	    Mx  = ?get_uint(Instr, ?OPCODE_BITS+?BODY_BITS, ?INDEX_BITS),
	    I = {instr,Addr,OpCode,[{mem,Mx},{n,Num}]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, next} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    I = {instr,Addr,OpCode,[reg(X)]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, new} ->
	    Obj = ?get_uint(Instr, ?OPCODE_BITS, ?INDEX_BITS),
	    I = {instr,Addr,OpCode,[{obj,Obj}]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, call} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Idx = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS, ?FUNC_BITS),
	    Usr = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+?FUNC_BITS, 1),
	    Avt = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+?FUNC_BITS+1, 16),
	    I = {inst,Addr,OpCode,[reg(X),{idx,Idx},{usr,Usr},{avt,Avt}]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, alu, 1} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Y = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS, ?REG_BITS),
	    _Z = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS, ?REG_BITS),
	    U = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS+1, 1),
	    _Swap = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS+2, 1),
	    I = if U =:= 0 ->
			{instr, Addr, OpCode, [reg(X),reg(Y)]};
		   U =:= 1 ->
			{instr, Addr, OpCode, [reg(X),reg(Y),unsigned]}
		end,
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, alu, 2} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Y = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS, ?REG_BITS),
	    Z = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS, ?REG_BITS),
	    U = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS+1, 1),
	    _Swap = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS+2, 1),
	    I = if U =:= 0 ->
			{instr, Addr, OpCode, [reg(X),reg(Y),reg(Z)]};
		   U =:= 1 ->
			{instr, Addr, OpCode, [reg(X),reg(Y),reg(Z),unsigned]}
		end,
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, mem} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Y = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS, ?REG_BITS),
	    Mem = ?get_uint(Instr, ?OPCODE_BITS+2*?REG_BITS+2, ?INDEX_BITS),
	    I = case OpCode of
		    'TMO' -> {instr,Addr,OpCode,[reg(X), {mem,Mem}]};
		    'CHG' -> {instr,Addr,OpCode,[reg(X), {mem,Mem}]};
		    'LDP' -> {instr, Addr, OpCode, [reg(X), {mem,Mem}, Y]};
		    'STP' -> {instr, Addr, OpCode, [reg(X), {mem,Mem}, Y]};
		    _ -> {instr, Addr, OpCode, [reg(X), {mem,Mem}]}
		end,
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, imm} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Imm = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS+6, 16),
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
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, memi} ->
	    X = ?get_uint(Instr, ?OPCODE_BITS, ?REG_BITS),
	    Imm = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS, ?TINY_BITS),
	    Mem = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+?TINY_BITS,
			    ?INDEX_BITS),
	    I = {instr, Addr, OpCode, [reg(X), {imm,Imm}, {mem,Mem}]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, seg} ->
	    Num = ?get_uint(Instr, ?OPCODE_BITS+2, ?BODY_BITS),
	    Used = ?get_uint(Instr, ?OPCODE_BITS+2+?BODY_BITS+6, 8),
	    <<StringData:(4*Num)/binary, Data1/binary>> = Data,
	    I = {instr,Addr,OpCode,[{slots,Num},{used,Used},{data,StringData}]},
	    add_instr(Data1, Addr, 1+Num, I, Acc);
	{OpCode, seto} ->
	    Obj = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS+2, 16),
	    I = {instr,Addr,OpCode,[{obj,Obj}]},
	    add_instr(Data, Addr, 1, I, Acc);
	{OpCode, setox} ->
	    Len = ?get_int(Instr, ?OPCODE_BITS+?REG_BITS+2, 14),
	    X = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+2+14, ?REG_BITS),
	    Stride = ?get_uint(Instr, ?OPCODE_BITS+?REG_BITS+2+14+?REG_BITS, 6),
	    I = {instr,Addr,OpCode,[reg(X),{len,Len},{stride,Stride}]},
	    add_instr(Data, Addr, 1, I, Acc);	    
	{OpCode, nop} ->
	    I = {instr,Addr,OpCode},
	    add_instr(Data, Addr, 1, I, Acc);	    
	{OpCode, Format} ->
	    I = {instr,Addr,OpCode,Format},
	    add_instr(Data, Addr, 1, I, Acc)
    end;
disasm_code(<<>>, _, Acc) ->
    lists:reverse(Acc).

reg(I) ->
    maps:get(I, reg()).
reg() ->
    #{ 0 => r0, 1 => r1, 2 => r2, 3 => r3,
       4 => r4, 5 => r5, 6 => r6, 7 => r4,
       8 => r8, 9 => r9, 10 => r10, 11 => r11,
       12 => r12, 13 => r13, 14 => r14, 15 => r15 }.

add_instr(Data, Addr, Size, I, Acc) ->
    io:format("~w: ~p\n", [Addr, I]),
    disasm_code(Data, Addr+Size, Acc).

-define(CSP_DECL_TYPE_BITS,  4).
-define(DIR_BITS,  2).
-define(NAMEID_BITS, 8).

-define(V_VOID,     0).
-define(V_INTEGER,  1).
-define(V_UNSIGNED, 2).
-define(V_FLOAT,    3).
-define(V_STRING,   4).
-define(V_INDEX,    5).
-define(V_NUMBER,   6).
-define(V_ANY,      7).
-define(V_TIMER,    8).
-define(V_DIGITAL,  9).
-define(V_ANALOG,   10).
-define(V_FIELD,   11).   

-define(DECL_NONE, 0).
-define(DECL_VARIABLE, 1).
-define(DECL_CONSTANT, 2).
-define(DECL_MODULE,   3).
-define(DECL_END,      4).
-define(DECL_OBJECT,   5).
-define(DECL_STATES,   6).
-define(DECL_IN,       7).
-define(DECL_TIMER, ?V_TIMER).
-define(DECL_DIGITAL, ?V_DIGITAL).
-define(DECL_ANALOG, ?V_ANALOG).
-define(DECL_FIELD, ?V_FIELD).
-define(DECL_BUFFER, 12).
-define(DECL_VIEW, 13).
-define(DECL_ROUTE, 14).
-define(DECL_END_MARK, 15).

decl() ->
#{
  ?DECL_NONE => none,
  ?DECL_VARIABLE => variable,
  ?DECL_CONSTANT => constant,
  ?DECL_MODULE   => module,
  ?DECL_END => 'end',
  ?DECL_OBJECT => object,
  ?DECL_STATES => states,
  ?DECL_IN     => 'in',
  ?DECL_TIMER  => timer,
  ?DECL_DIGITAL => digital,
  ?DECL_ANALOG => analog,
  ?DECL_FIELD => field,
  ?DECL_BUFFER => buffer,
  ?DECL_VIEW => view,
  ?DECL_ROUTE => rout,
  ?DECL_END_MARK => end_mark
 }.

decl_type(T) ->
    maps:get(T, decl()).

disasm_decl(ok, Sections) ->
    {_, Decl} = lists:keyfind("DECL", 1, Sections),
    disasm_decl(Decl, 0, []).

disasm_decl(<<Decl:32/little, Decl2:32/little, Data/binary>>, I, Acc) ->
    Type = ?get_uint(Decl, 0, ?CSP_DECL_TYPE_BITS),
    Cont = ?get_uint(Decl, ?CSP_DECL_TYPE_BITS, 1),
    Local = ?get_uint(Decl, ?CSP_DECL_TYPE_BITS+1, 1),
    Dir = ?get_uint(Decl, ?CSP_DECL_TYPE_BITS+1+1, ?DIR_BITS),
    Name = ?get_uint(Decl, ?CSP_DECL_TYPE_BITS+1+1+?DIR_BITS, ?NAMEID_BITS),
    DeclType = decl_type(Type),
    case DeclType of
	end_mark -> lists:reverse(Acc);
	_ ->
	    D = {decl, I, DeclType, [{dir,Dir},{local,Local},{name,Name}]},
	    add_decl(Data, I, 1, D, Acc)
    end;
disasm_decl(<<>>, _I, Acc) ->
    lists:reverse(Acc).    

add_decl(Data, I, Size, D, Acc) ->
    io:format("~w: ~p\n", [I, D]),
    disasm_decl(Data, I+Size, [D|Acc]).


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
