%% -*- erlang -*-

Terminals
  'D_MODULE' 'D_END' 'D_STATES' 
   %% D_IN => T_IN
  'D_WHEN'
  'D_DIGITAL' 'D_ANALOG' 'D_VARIABLE' 'D_LOCAL' 'D_PARAM' 'D_CONSTANT' 
  'D_TIMER' 'D_FIELD' 'D_BUFFER' 'D_DEFINE' 'D_DISABLE' 'D_ENABLE'
  'T_INTEGER' 'T_UNSIGNED' 'T_STRING' 'T_FLOAT' 'T_IN' 
  'T_OUT' 'T_INOUT' 'T_LITTLE' 'T_BIG' 'T_NATIVE'
  'T_CAN' 'T_I2C' 'T_SPI' 'T_UDP' 'T_TCP' 'T_UART'
  'WORD' 'INT' 'FLT'
  'EQEQ' 'NEQ' 'LTEQ' 'GTEQ' 'LTLT' 'GTGT' 'EQ' 'LT' 'GT'
  'EXCLAMATION' 'HASH' 'MINUS' 'PLUS' 'SLASH' 'PERCENT'
  'ASTERISK' 'QUEST' 'AMPAMP' 'AMP' 'BARBAR' 'BAR' 'CIRC' 'TILDE'
  'LP' 'RP' 'LB' 'RB' 'DOTDOT' 'DOT' 'COMMA' 'COLON'
  'NEWLINE'
  .

Nonterminals
  file statement declaration rule state_list
  expr array buftype pin_list pin_range 
  bit_range rule_list rule_range
  assignment_list assignment
  res neg
  type endian iodir option options
  .

Rootsymbol file.
Endsymbol '$end'.

Unary 1200 neg. 

Unary 110 'EXCLAMATION' 'TILDE'.
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

statement -> 'HASH' declaration : '$2'.
statement -> rule : '$1'.

declaration -> 'D_MODULE' 'WORD' : {module,line('$1'),'$2'}.

declaration -> 'D_STATES' state_list: {states,line('$1'),'$2'}.
declaration -> 'T_IN' state_list: {'in',line('$1'),'$2'}.
declaration -> 'D_WHEN' expr : {'when',line('$1'),'$2'}.
declaration -> 'D_DIGITAL' 'WORD' array res options : 
		   {digital,line('$1'),'$2','$3','$4','$5',[]}.
declaration -> 'D_DIGITAL' 'WORD' array res options pin_list : 
		   {digital,line('$1'),'$2','$3','$4','$5','$6'}.
declaration -> 'D_ANALOG' 'WORD' array res options pin_list :
		   {analog,line('$1'),'$2','$3','$4','$5','$6'}.
declaration -> 'D_ANALOG' 'WORD' array res options :
		   {analog,line('$1'),'$2','$3','$4','$5',[]}.

declaration -> 'D_VARIABLE' 'WORD' array res options :
		   {variable,line('$1'),'$2','$3','$4','$5',undefined}.
declaration -> 'D_VARIABLE' 'WORD' array res options 'EQ' expr :
		   {variable,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'D_CONSTANT' 'WORD' array res options 'EQ' expr :
		   {constant,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'D_LOCAL' 'WORD' res options 'EQ' expr :
		   {local,line('$1'),'$2','$3','$4','$6'}.
declaration -> 'D_PARAM' 'WORD' res options :
		   {param,line('$1'),'$2','$3','$4',undefined}.
declaration -> 'D_PARAM' 'WORD' res options 'EQ' expr :
		   {param,line('$1'),'$2','$3','$4','$6'}.

declaration -> 'D_TIMER' 'WORD' expr : {timer,line('$1'),'$2','$3',undefined}.
declaration -> 'D_TIMER' 'WORD' expr 'EQ' expr : {timer,line('$1'),'$2','$3','$5'}.
declaration -> 'D_FIELD' 'WORD' res options 'WORD' 'LB' bit_range 'RB' :
		   {field,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'D_BUFFER' 'WORD' res options buftype :
		   {buffer,line('$1'),'$2','$3','$4','$5'}.
declaration -> 'D_DEFINE' 'WORD' expr : {define,line('$1'),'$2','$3'}.
declaration -> 'D_END': {'end',line('$1')}.

%% Special Immediates
declaration -> 'D_DISABLE' rule_list : {disable,line('$1'),'$2'}.
declaration -> 'D_ENABLE' rule_list : {enable,line('$1'),'$2'}.

res -> 'COLON' 'INT' : '$2'.
res -> '$empty' : default.

array -> 'LB' 'INT' 'RB' : {array, '$2'}.	
array ->  '$empty' : scalar.

bit_range -> 'INT' 'DOTDOT' 'INT' : {range, '$1','$3'}.
bit_range -> 'INT' : '$1'.

rule_list -> rule_range : ['$1'].
rule_list -> rule_range rule_list : ['$1'|'$2'].

rule_range -> 'INT' : '$1'.
rule_range -> 'INT' 'MINUS' 'INT' : {'$1', '$3'}.

pin_list -> pin_range : ['$1'].
pin_list -> pin_range pin_list : ['$1'|'$2'].

pin_range -> 'INT' : {pin,'$1'}.
pin_range -> 'INT' 'COLON' 'INT' : {port_pin,'$1','$3'}.
pin_range -> 'INT' 'COLON' 'INT' 'DOTDOT' 'INT' : {port_pin,'$1',
						  {range,'$3','$5'}}.
buftype -> 'T_CAN' 'INT' : [{can,'$2'}].
buftype -> 'T_I2C' 'INT' : [{i2c,'$2'}].
buftype -> 'T_SPI' 'INT' : [{spi,'$2'}].
buftype -> 'T_UDP' 'INT' : [{udp,'$2'}].
buftype -> 'T_TCP' 'INT' : [{tcp,'$2'}].
buftype -> 'T_UART' 'INT' : [{uart,'$2'}].

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
     
rule -> assignment_list 'QUEST' expr : {rule,'$1','$3'}.
rule -> assignment_list : {rule,'$1',true}.

assignment -> 'WORD' 'EQ' expr : {'=', '$1', '$3'}.
assignment -> 'WORD' 'DOT' 'WORD' 'EQ' expr : {'=', {fld,'$1','$3'}, '$5'}.

assignment_list -> assignment : ['$1'].
assignment_list -> assignment 'COMMA' assignment_list : ['$1'|'$3'].

neg -> 'MINUS' : '$1'.

expr -> 'INT'         : '$1'.
expr -> 'FLT'         : '$1'.
expr -> 'WORD'        : '$1'.
expr -> neg expr    : {'-', '$2'}.
expr -> 'TILDE' expr    : {'~', '$2'}.
expr -> 'EXCLAMATION' expr :
	    case '$2' of
		{'!', Cond} -> Cond;
		Cond -> {'!',Cond}
	    end.
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
expr -> expr 'EQEQ' expr : {'==','$1','$3'}.
expr -> expr 'NEQ' expr : {'!=','$1','$3'}.
expr -> expr 'LTEQ' expr : {'<=','$1','$3'}.
expr -> expr 'LT' expr : {'<','$1','$3'}.
expr -> expr 'GT' expr : {'>','$1','$3'}.
expr -> expr 'GTEQ' expr : {'>=','$1','$3'}.
expr -> expr 'AMPAMP' expr : {'and','$1','$3'}.
expr -> expr 'BARBAR' expr : {'or','$1','$3'}.
expr -> 'WORD' 'DOT' 'WORD' : {'fld','$1','$2'}.
expr -> 'LP' expr 'RP' : '$2'.
expr -> 'WORD' 'LP' 'RP' : {call, '$1', []}.
expr -> 'WORD' 'LP' expr 'RP' : {call, '$1', ['$3']}.
expr -> 'WORD' 'LP' expr 'COMMA' expr 'RP' : {call, '$1', ['$3','$5']}.
expr -> 'WORD' 'LP' expr 'COMMA' expr 'COMMA' expr 'RP' : 
	    {call, '$1', ['$3','$5','$7']}.
expr -> 'WORD' 'LP' expr 'COMMA' expr 'COMMA' expr 'COMMA' expr 'RP' : 
	    {call, '$1', ['$3','$5','$7','$9']}.

Erlang code.

line([H|_]) -> line(H);
line(T) when is_tuple(T) -> element(2, T).
