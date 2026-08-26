%% -*- erlang -*-

Terminals
  'D_MODULE' 'D_END' 'D_STATES' %% D_IN => T_IN 
  'D_DIGITAL' 'D_ANALOG' 'D_VARIABLE' 'D_LOCAL' 'D_PARAM' 'D_CONSTANT' 
  'D_TIMER' 'D_FIELD' 'D_BUFFER'
  'T_INTEGER' 'T_UNSIGNED' 'T_STRING' 'T_FLOAT' 'T_IN' 'T_CAN' 'T_UDP'
  'T_OUT' 'T_INOUT' 'T_LITTLE' 'T_BIG' 'T_NATIVE'
  'WORD' 'INT' 'FLT'
  'EQEQ' 'NEQ' 'LTEQ' 'GTEQ' 'LTLT' 'GTGT' 'EQ' 'LT' 'GT'
  'EXCLAMATION' 'HASH' 'MINUS' 'PLUS' 'SLASH' 'PERCENT'
  'ASTERISK' 'QUEST' 'AMPAMP' 'AMP' 'BARBAR' 'BAR' 'CIRC' 'TILDE'
  'LP' 'RP' 'LB' 'RB' 'LBRACE' 'RBRACE' 'DOTDOT' 'DOT' 'COMMA' 'COLON'
  'NEWLINE'
  .

Nonterminals
  file statement declaration rule condition state_list
  expr exprs var_expr array bits buftype pin_item 
  array_list pin_item_list range 
  res port_pin neg
  type endian iodir option options
  .

Rootsymbol file.
Endsymbol '$end'.

Unary 1100 neg. 
Unary 1050 'EXCLAMATION' 'TILDE'.
Left 1000 'ASTERISK' 'SLASH' 'PERCENT'.
Left 900  'PLUS' 'MINUS'.
Left 800  'LTLT' 'GTGT'.
Left 700  'LT' 'LTEQ' 'GT' 'GTEQ'.
Left 600  'EQEQ' 'NEQ'.
Left 500  'AMP'.
Left 400  'CIRC'.
Left 300  'BAR'.

Left 10   'BARBAR'.
Left 20   'AMPAMP'.

file -> statement 'NEWLINE' : ['$1'].
file -> statement 'NEWLINE' file : ['$1'|'$3'].
file -> 'NEWLINE' file : '$2'.

statement -> declaration : '$1'.
statement -> rule : '$1'.

declaration ->
    'HASH' 'D_MODULE' 'WORD' : 
	{'#',module,'$3'}.
declaration ->
    'HASH' 'D_END': 
	{'#','end'}.
declaration ->
    'HASH' 'D_STATES' state_list: 
	{'#','end','$3'}.
declaration ->
    'HASH' 'T_IN' state_list: 
	{'#','in','$3'}.
declaration ->
    'HASH' 'D_DIGITAL' 'WORD' array res options port_pin : 
	{'#',digital,'$3','$4','$5','$6','$7'}.
declaration ->
    'HASH' 'D_ANALOG' 'WORD' array res options port_pin :
	{'#',analog,'$3','$4','$5','$6','$7'}.
declaration ->
    'HASH' 'D_VARIABLE' 'WORD' array res options var_expr :
	{'#',variable,'$3','$4','$5','$6','$7'}.
declaration ->
    'HASH' 'D_LOCAL' 'WORD' res options var_expr :
	{'#',local,'$3','$4','$5','$6'}.
declaration ->
    'HASH' 'D_PARAM' 'WORD' res options var_expr :
	{'#',param,'$3','$4','$5','$6'}.
declaration ->
    'HASH' 'D_CONSTANT' 'WORD' array res options 'EQ' expr :
	{'#',local,'$3','$4','$5','$6','$8'}.
declaration ->
    'HASH' 'D_TIMER' 'WORD' expr :
	{'#',timer,'$3','$4'}.
declaration ->
    'HASH' 'D_FIELD' 'WORD' res options 'WORD' 'LB' range 'RB' :
	{'#',field,'$3','$4','$5','$6','$7'}.
declaration ->
    'HASH' 'D_BUFFER' 'WORD' res options buftype :
	{'#',buffer,'$3','$4','$5','$6'}.


res -> 'COLON' 'INT' : '$2'.
res -> '$empty' : default.

array -> 'LB' 'INT' 'RB' : {array, '$2'}.	
array ->  '$empty' : scalar.

range -> 'INT' 'DOTDOT' 'INT' : {range, '$1','$3'}.
range -> 'INT' : '$1'.

array_list -> 'LBRACE' 'RBRACE' : [].
array_list -> 'LBRACE' exprs 'RBRACE' : '$2'.

exprs -> expr : ['$1'].
exprs -> expr exprs : ['$1'|'$2'].

pin_item_list -> pin_item : ['$1'].
pin_item_list -> pin_item pin_item_list : ['$1'|'$2'].

pin_item -> 'INT' : {pin,'$1'}.
pin_item -> 'INT' 'COLON' 'INT' : {port_pin,'$1','$3'}.
pin_item -> 'INT' 'COLON' 'INT' 'DOTDOT' 'INT' : {port_pin,'$1',
						  {range,'$3','$5'}}.
buftype -> 'T_CAN' 'INT' : [{can,'$2'}].
buftype -> 'T_UDP' 'INT' : [{udp,'$2'}].  %% int should be ip?
    

var_expr -> 'EQ' expr : '$2'.
var_expr -> '$empty' : undefined.

port_pin -> 'INT' 'COLON' 'INT' : {'$1','$3'}.
port_pin -> 'INT' : '$1'.

options -> option options : ['$1'|'$2'].
options -> '$empty'       : [].

option -> iodir  : {dir,'$1'}.
option -> endian : {endian,'$1'}.
option -> type   : {type, '$1'}.

endian -> 'T_BIG'    : big.
endian -> 'T_LITTLE' : little.
endian -> 'T_NATIVE' : native.

iodir -> 'T_IN'    : in.
iodir -> 'T_OUT'   : out.
iodir -> 'T_INOUT' : inout.

type -> 'T_UNSIGNED' : unsigned.
type -> 'T_INTEGER'  : integer.
type -> 'T_FLOAT'    : float.
type -> 'T_STRING'   : string.

state_list -> 'WORD' : ['$1'].
state_list -> 'WORD' state_list : ['$1'|'$2'].
     
rule -> 'WORD' 'EQ' expr 'QUEST' condition  : {rule,'$1','$3','$5'}.

condition -> expr : '$1'.
condition -> 'EXCLAMATION' condition :
		 case '$2' of
		     {'!', Cond} -> Cond;
		     Cond -> {'!',Cond}
		 end.
condition -> 'LP' condition 'RP' : '$2'.
condition -> expr 'EQEQ' expr : {'==','$1','$3'}.
condition -> expr 'NEQ' expr : {'!=','$1','$3'}.
condition -> expr 'LTEQ' expr : {'<=','$1','$3'}.
condition -> expr 'LT' expr : {'<','$1','$3'}.
condition -> expr 'GT' expr : {'>','$1','$3'}.
condition -> expr 'GTEQ' expr : {'>=','$1','$3'}.
condition -> condition 'AMPAMP' condition : {'and','$1','$3'}.
condition -> condition 'BARBAR' condition : {'or','$1','$3'}.

neg -> 'MINUS' : '$1'.

expr -> 'INT'         : '$1'.
expr -> 'FLT'         : '$1'.
expr -> 'WORD'        : '$1'.
expr -> neg expr    : {'-', '$2'}.
expr -> 'TILDE' expr    : {'~', '$2'}.
expr -> expr 'PLUS' expr : {'+','$1','$3'}.
expr -> expr 'MINUS' expr : {'-','$1','$3'}.
expr -> expr 'ASTERISK' expr : {'*','$1','$3'}.
expr -> expr 'SLASH' expr : {'/','$1','$3'}.
expr -> expr 'PERCENT' expr : {'%','$1','$3'}.

expr -> expr 'AMP' expr : {'&','$1','$3'}.
expr -> expr 'BAR' expr : {'|','$1','$3'}.
expr -> expr 'CIRC' expr : {'^','$1','$3'}.
expr -> expr 'LTLT' expr : {'<<','$1','$3'}.
expr -> expr 'GTGT' expr : {'>>','$1','$3'}.
expr -> 'WORD' 'DOT' 'WORD' : {'fld','$1','$2'}.
     
expr -> 'WORD' 'LP' 'RP' : {call, '$1', []}.
expr -> 'WORD' 'LP' expr 'RP' : {call, '$1', ['$3']}.
expr -> 'WORD' 'LP' expr COMMA expr 'RP' : {call, '$1', ['$3','$5']}.
expr -> 'WORD' 'LP' expr COMMA expr COMMA expr 'RP' : 
	    {call, '$1', ['$3','$5','$7']}.
expr -> 'WORD' 'LP' expr COMMA expr COMMA expr COMMA expr 'RP' : 
	    {call, '$1', ['$3','$5','$7','$9']}.
    
