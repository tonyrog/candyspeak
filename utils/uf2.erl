-module(uf2).

-export([decode/0, decode/1]).

-define(UF2_MAGIC_START0, 16#0A324655). %% "UF2\n"
-define(UF2_MAGIC_START1, 16#9E5D5157). %% Randomly selected
-define(UF2_MAGIC_END,    16#0AB16F30). %% Ditto

-define(UF2_FLAG_NOFLASH,        16#00000001).
-define(UF2_FLAG_FILECONTAINER,  16#00001000). %% file container
-define(UF2_FLAG_FAMILY_ID,      16#00002000). %% familyID present
-define(UF2_FLAG_MD5_CHKSUM,     16#00004000). %% MD5 checksum present
-define(UF2_FLAG_EXTENSION_TAGS, 16#00008000). %% extension tags present

decode() ->
    decode("CURRENT.UF2").

decode(File) ->
    {ok,Bin} = file:read_file(File),
    decode_blocks(Bin, []).

decode_blocks(Bin, Acc) ->
    case Bin of
	<<?UF2_MAGIC_START0:32/little, ?UF2_MAGIC_START1:32/little, 
	  Flags:32/little, TargetAddr:32/little, 
	  PayloadSize:32/little, 
	  BlockNo:32/little, NumBlocks:32/little,
	  _Reserved:32/little,
	  Data:476/binary,
	  ?UF2_MAGIC_END:32/little,
	  Bin1/binary>> ->
	    Fs = decode_flags(Flags, _Reserved),
	    decode_blocks(Bin1, 
			 [#{ flags => Flags,
			     fs => Fs,
			     target_addr => TargetAddr,
			     payloadSize => PayloadSize,
			     blockNo => BlockNo,
			     numBlocks => NumBlocks,
			     reserved => _Reserved,
			     data => Data
			   }|Acc]);
	<<_MAGIC_START0:32/little, _MAGIC_START1:32/little, 
	  _Flags:32/little, _TargetAddr:32/little, 
	  _PayloadSize:32/little, 
	  BlockNo:32/little, _NumBlocks:32/little,
	  _Reserved:32/little,
	  _Data:476/binary,
	  _MAGIC_END:32/little,
	  _Bin1/binary>> ->
	    {error, {bad_magic, block, BlockNo}};
	_ ->
	    lists:reverse(Acc)
    end.

decode_flags(Flags, Reserved) ->
	if Flags band ?UF2_FLAG_NOFLASH =/= 0 -> [noflash]; true -> [] end ++
	if Flags band ?UF2_FLAG_FILECONTAINER =/= 0 -> [file]; true -> [] end ++
	if Flags band ?UF2_FLAG_FAMILY_ID =/= 0 -> [{family,Reserved}]; true -> [] end ++
	if Flags band ?UF2_FLAG_MD5_CHKSUM =/= 0 -> [md5]; true -> [] end ++
	if Flags band ?UF2_FLAG_EXTENSION_TAGS =/= 0 -> [ext]; true -> [] end.
