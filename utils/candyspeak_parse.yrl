%% -*- erlang -*-

Terminals
  'D_MODULE' 'D_END' 'D_STATES' 
   %% D_IN => T_IN
  'D_WHEN'
  'D_DIGITAL' 'D_ANALOG' 'D_VARIABLE' 'D_LOCAL' 'D_PARAM' 'D_CONSTANT' 
  'D_TIMER' 'D_FIELD' 'D_BUFFER' 'D_DEFINE' 'D_DISABLE' 'D_ENABLE'
  'D_ANNOTATE'
  'T_INTEGER' 'T_UNSIGNED' 'T_STRING' 'T_FLOAT' 'T_IN'
  'T_OUT' 'T_INOUT' 'T_LITTLE' 'T_BIG' 'T_NATIVE' 'T_PWM' 'T_BIND'
  'T_VAL' 'T_PIN' 'T_PORT' 'T_DIR' 'T_ENDIAN' 'T_PERIOD' 'T_FIRED'
  'T_ID' 'T_RX' 'T_TX' 'T_DLC' 'T_LEN'
  'T_PULLUP' 'T_PULLDOWN'
  'T_CAN' 'T_I2C' 'T_SPI' 'T_UDP' 'T_TCP' 'T_UART'
  'T_LOW' 'T_HIGH' 'T_BOTH'  'T_SOFT' 'T_READY' 'T_RISING' 'T_FALLING'
  'WORD' 'INT' 'FLT' 'STR'
  'EQEQ' 'NEQ' 'LTEQ' 'GTEQ' 'LTLT' 'GTGT' 'LT' 'GT' 
  'EQ' 'RIMP' 'LTLTEQ' 'GTGTEQ'
  'EXCLAMATION' 'HASH' 'MINUS' 'PLUS' 'SLASH' 'PERCENT'
  'ASTERISK' 'QUEST' 'AMPAMP' 'AMP' 'BARBAR' 'BAR' 'CIRC' 'TILDE'
  'LP' 'RP' 'LB' 'RB' 'LBRACE' 'RBRACE' 'DOTDOT' 'DOT' 'COMMA' 'COLON'
  'NEWLINE'
  .

Nonterminals
  annotate_items annotate_item annotate_value
  file statement declaration rule state_list
  expr array expr_list expr_array buftype pin_list pin_range 
  bit_range rule_list rule_range
  assignment_list assignment id xid fid part lhs field pfield 
  pack_list unpack_list
  res neg obj_params
  type endian iodir pull trig option options
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

file -> statement 'NEWLINE' file : ['$1'|'$3'].
file -> 'NEWLINE' file : '$2'.
file -> '$empty' : [].

statement -> 'HASH' declaration : '$2'.
statement -> 'GT' expr : {immediate, '$2'}.
statement -> 'GT' assignment : {immediate, '$2'}.
statement -> rule : '$1'.

declaration -> 'D_MODULE' id : {module,line('$1'),'$2'}.

declaration -> 'D_STATES' state_list: {states,line('$1'),'$2'}.
declaration -> 'T_IN' state_list: {'in',line('$1'),'$2'}.
declaration -> 'D_WHEN' expr : {'when',line('$1'),'$2'}.
declaration -> 'D_DIGITAL' xid array res options : 
		   {digital,line('$1'),'$2','$3','$4','$5',[]}.
declaration -> 'D_DIGITAL' xid array res options pin_list : 
		   {digital,line('$1'),'$2','$3','$4','$5','$6'}.
declaration -> 'D_ANALOG' xid array res options pin_list :
		   {analog,line('$1'),'$2','$3','$4','$5','$6'}.
declaration -> 'D_ANALOG' xid array res options :
		   {analog,line('$1'),'$2','$3','$4','$5',[]}.

declaration -> 'D_VARIABLE' xid array res options :
		   {variable,line('$1'),'$2','$3','$4','$5',undefined}.
declaration -> 'D_VARIABLE' xid array res options 'EQ' expr :
		   {variable,line('$1'),'$2','$3','$4','$5','$7'}.

declaration -> 'D_CONSTANT' xid array res options 'EQ' expr :
		   {constant,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'D_CONSTANT' xid array res options 'EQ' expr_array :
		   {constant,line('$1'),'$2','$3','$4','$5','$7'}.

declaration -> 'D_LOCAL' xid res options 'EQ' expr :
		   {local,line('$1'),'$2','$3','$4','$6'}.
declaration -> 'D_PARAM' xid res options :
		   {param,line('$1'),'$2','$3','$4',undefined}.
declaration -> 'D_PARAM' xid res options 'EQ' expr :
		   {param,line('$1'),'$2','$3','$4','$6'}.

declaration -> 'D_TIMER' xid expr : {timer,line('$1'),'$2','$3',undefined}.
declaration -> 'D_TIMER' xid expr 'EQ' expr : {timer,line('$1'),'$2','$3','$5'}.
declaration -> 'D_FIELD' xid res options id 'LB' bit_range 'RB' :
		   {field,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'D_BUFFER' xid res options buftype :
		   {buffer,line('$1'),'$2','$3','$4','$5'}.
declaration -> 'D_DEFINE' xid expr : {define,line('$1'),'$2','$3'}.

%% #annotate <tool> <target> [key[=value] ...]
%%
%% Inert: the parser carries it, nothing else looks at it, and it never reaches
%% the compiler or a ROM. It is for tools that read the SOURCE -- the panel's
%% widget choice, a property for the model checker, a tuning range -- so that
%% presentation and tooling metadata live next to the declaration they describe
%% instead of in a separate file that drifts out of step.
%%
%% The TOOL name owns the key space: without it every tool invents keys the
%% others silently ignore. candyspeak validates neither the keys nor the values;
%% each tool validates its own and must warn on what it does not know.
%%
%% What candyspeak DOES check is the target: an annotation naming a signal that
%% does not exist is the same kind of mistake as a rule naming one, and build/1
%% already holds the map to catch it.
declaration -> 'D_ANNOTATE' id id annotate_items :
		   {annotate,line('$1'),'$2','$3','$4'}.

annotate_items -> '$empty' : [].
annotate_items -> annotate_item annotate_items : ['$1'|'$2'].

annotate_item -> id 'EQ' annotate_value : {'$1','$3'}.
annotate_item -> id : {'$1',true}.

annotate_value -> id    : '$1'.
annotate_value -> 'INT' : '$1'.
annotate_value -> 'FLT' : '$1'.
annotate_value -> 'STR' : '$1'.
declaration -> 'D_END': {'end',line('$1')}.

%% Special Immediates
declaration -> 'D_DISABLE' rule_list : {disable,line('$1'),'$2'}.
declaration -> 'D_ENABLE' rule_list : {enable,line('$1'),'$2'}.

%% #<modeule-name> <object-name>  (Field (=|<-) Expr)*

declaration -> id id obj_params :
		   {object,line('$1'),'$1','$2','$3'}.

id -> 'WORD' : '$1'.

xid -> id : '$1'.
xid -> 'T_IN' : {'WORD', line('$1'), "in"}.
xid -> 'T_OUT' : {'WORD', line('$1'), "out"}.
xid -> 'T_LOW' : {'WORD', line('$1'), "low"}.
xid -> 'T_READY' : {'WORD', line('$1'), "ready"}.

part -> 'T_VAL' : {'WORD', line('$1'), "val"}.
part -> 'T_PIN' : {'WORD', line('$1'), "pin"}.
part -> 'T_PORT' : {'WORD', line('$1'), "port"}.
part -> 'T_DIR' : {'WORD', line('$1'), "dir"}.
part -> 'T_PWM' : {'WORD', line('$1'), "pwm"}.
part -> 'T_ENDIAN' : {'WORD', line('$1'), "enidan"}.
part -> 'T_PULLUP' : {'WORD', line('$1'), "pullup"}.
part -> 'T_PULLDOWN' : {'WORD', line('$1'), "pulldown"}.
part -> 'T_PERIOD' : {'WORD', line('$1'), "period"}.
part -> 'T_FIRED' : {'WORD', line('$1'), "fired"}.
part -> 'T_ID' : {'WORD', line('$1'), "id"}.
part -> 'T_RX' : {'WORD', line('$1'), "rx"}.
part -> 'T_TX' : {'WORD', line('$1'), "tx"}.
part -> 'T_DLC' : {'WORD', line('$1'), "dlc"}.
part -> 'T_LEN' : {'WORD', line('$1'), "len"}.
    

obj_params -> '$empty' : [].
obj_params -> assignment obj_params : ['$1'|'$2'].

res -> 'COLON' 'INT' : '$2'.
res -> '$empty' : default.

array -> 'LB' 'INT' 'RB' : {array_size,line('$1'),'$2'}.
array ->  '$empty' : scalar.

bit_range -> 'INT' 'DOTDOT' 'INT' : {range,line('$2'),'$1','$3'}.
bit_range -> 'INT' : '$1'.

rule_list -> rule_range : ['$1'].
rule_list -> rule_range rule_list : ['$1'|'$2'].

rule_range -> 'INT' : '$1'.
rule_range -> 'INT' 'MINUS' 'INT' : {'$1', '$3'}.

pin_list -> pin_range : ['$1'].
pin_list -> pin_range pin_list : ['$1'|'$2'].

pin_range -> 'INT' : {pin,line('$1'),'$1'}.
pin_range -> 'WORD' : {pin,line('$1'),'$1'}.  %% named pin
pin_range -> 'INT' 'COLON' 'INT' : {port_pin,line('$1'),'$1','$3'}.
pin_range -> 'INT' 'COLON' 'INT' 'DOTDOT' 'INT' : {port_pin,line('$1'),'$1',
						   {range,line('$4'),'$3','$5'}}.
%% can <frame-id> 
buftype -> 'T_CAN' 'INT' : [{can,'$2'}].
%% i2c <bus> <addr> <reg>
buftype -> 'T_I2C' 'INT' 'INT' 'INT' : [{i2c,'$2','$3','$4'}].
%% spi <bus> <cs-port> ':' <cs-pin> <cmd>
buftype -> 'T_SPI' 'INT' 'INT' 'COLON' 'INT' 'INT' :
	       [{spi,'$2',{'$3','$5'},'$6'}].
%% udp <port> == udp <port> 0
buftype -> 'T_UDP' 'INT' : [{udp,'$2',udefined}].
%% udp <port> <ip>
buftype -> 'T_UDP' 'INT' 'INT' : [{udp,'$2','$3'}].
%% tcp <port> == tcp <port> 0
buftype -> 'T_TCP' 'INT' : [{tcp,'$2',udefined}].
%% tcp <port> <ip>
buftype -> 'T_TCP' 'INT' 'INT' : [{tcp,'$2','$3'}].
%% uart <unit> [<baud>] (default baud = 9600)
buftype -> 'T_UART' 'INT' : [{uart,'$2',9600}].
buftype -> 'T_UART' 'INT' 'INT' : [{uart,'$2','$3'}].
%% plain byte buffer
buftype -> '$empty' : [].

%% uart config?
%% Data:4,Parity:4,Stop:4,Baud100:20 = 32
%% Data      = 5-9  
%% parity    = N=0|E=1|O=2|M=3|S=4
%% stop-bits = 1,2
%% mode = 81100000+192 

options -> option options : ['$1'|'$2'].
options -> '$empty'       : [].

option -> iodir  : {dir,'$1'}.
option -> endian : {endian,'$1'}.
option -> type   : {type, '$1'}.
option -> pull   : {pull, '$1'}.
option -> trig   : {trig, '$1'}.
option -> 'T_PWM' : pwm.
option -> 'T_BIND' id 'LB' bit_range 'RB':  {bind,{'$2','$4'}}.

trig -> 'T_LOW' : low.
trig -> 'T_HIGH' : high.
trig -> 'T_BOTH' : both.
trig -> 'T_SOFT' : soft.
trig -> 'T_READY' : ready.
trig -> 'T_RISING' : rising.
trig -> 'T_FALLING' : falling.

pull -> 'T_PULLUP'   : pullup.
pull -> 'T_PULLDOWN' : pulldown.

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

state_list -> id : ['$1'].
state_list -> id state_list : ['$1'|'$2'].
     
rule -> assignment_list 'QUEST' expr : {rule,line('$2'),'$1','$3'}.
rule -> assignment_list : {rule,line('$1'),'$1',undefined}.
rule -> lhs 'LTLTEQ' pack_list : {rule, line('$2'), [{pack, '$1', '$3'}], true}.
rule -> lhs 'GTGTEQ' unpack_list : {rule, line('$2'), [{unpack, '$1', '$3'}], trur}.
rule -> lhs 'LTLTEQ' pack_list 'QUEST' expr  : {rule, line('$2'),[{pack,'$1','$3'}],'$5'}.
rule -> lhs 'GTGTEQ' unpack_list 'QUEST' expr :{rule, line('$2'),[{unpack,'$1','$3'}],'$5'}.

assignment -> lhs 'EQ' expr   : {'=', line('$2'), '$1', '$3'}.
assignment -> lhs 'RIMP' expr : {'<-',line('$2'), '$1', '$3'}.
assignment -> expr : '$1'.
    
assignment_list -> assignment : ['$1'].
assignment_list -> assignment 'COMMA' assignment_list : ['$1'|'$3'].

pack_list -> pfield : [{'$1',default}].
pack_list -> pfield 'COLON' 'INT' : [{'$1','$3'}].
pack_list -> pfield pack_list : [{'$1',default}|'$2'].
pack_list -> 'LP' expr 'RP' 'COLON' 'INT' : [{'$2','$5'}].
pack_list -> pfield 'COLON' 'INT' pack_list : [{'$1','$3'}|'$4'].
pack_list -> 'LP' expr 'RP' 'COLON' 'INT' pack_list : [{'$2','$5'}|'$6'].

pfield -> 'INT' : '$1'.
pfield -> field : '$1'.

unpack_list -> field : [{'$1',default}].
unpack_list -> field 'COLON' 'INT' : [{'$1','$3'}].
unpack_list -> field unpack_list : [{'$1',default}|'$2'].
unpack_list -> field 'COLON' 'INT' unpack_list : [{'$1','$3'}|'$4'].

lhs -> field : '$1'.

%% rhs field
fid -> xid  : '$1'.
%%fid -> xid 'DOT' xid   : {fld,'$1','$3'}.
    
field -> fid            : {field,line('$1'),'$1'}.
field -> fid 'DOT' part : {field,line('$1'),{part,line('$2'),'$1','$3'}}.
field -> fid 'DOT' xid  : {field,line('$1'),{fld,line('$2'),'$1','$3'}}.
field -> fid 'DOT' xid 'DOT' part : {field,line('$1'),{part,line('$2'),{fld,line('$4'),'$1','$3'},'$5'}}.
field -> fid 'LB' expr 'RB' : {field,line('$1'),{index, line('$2'), '$1','$3'}}.
field -> fid 'LB' 'INT' 'DOTDOT' 'INT' 'RB' : 
	     {field,line('$1'),{index,line('$2'),'$1',
				{range,line('$4'),'$3','$5'}}}.

neg -> 'MINUS' : '$1'.

expr -> 'INT'         : '$1'.
expr -> 'FLT'         : '$1'.
expr -> 'STR'         : '$1'.
expr -> field         : '$1'.
expr -> neg expr      : {'-',line('$1'),'$2'}.
expr -> 'TILDE' expr  : {'~',line('$1'),'$2'}.
expr -> 'EXCLAMATION' expr :
	    case '$2' of
		{'!', _Ln, Cond} -> Cond;
		Cond -> {'!',line('$1'),Cond}
	    end.
expr -> expr 'PLUS' expr : {'+',line('$2'),'$1','$3'}.
expr -> expr 'MINUS' expr : {'-',line('$2'),'$1','$3'}.
expr -> expr 'ASTERISK' expr : {'*',line('$2'),'$1','$3'}.
expr -> expr 'SLASH' expr : {'/',line('$2'),'$1','$3'}.
expr -> expr 'PERCENT' expr : {'%',line('$2'),'$1','$3'}.
expr -> expr 'AMP' expr : {'&',line('$2'),'$1','$3'}.
expr -> expr 'BAR' expr : {'|',line('$2'),'$1','$3'}.
expr -> expr 'CIRC' expr : {'^',line('$2'),'$1','$3'}.
expr -> expr 'LTLT' expr : {'<<',line('$2'),'$1','$3'}.
expr -> expr 'GTGT' expr : {'>>',line('$2'),'$1','$3'}.
expr -> expr 'EQEQ' expr : {'==',line('$2'),'$1','$3'}.
expr -> expr 'NEQ' expr : {'!=',line('$2'),'$1','$3'}.
expr -> expr 'LTEQ' expr : {'<=',line('$2'),'$1','$3'}.
expr -> expr 'LT' expr : {'<',line('$2'),'$1','$3'}.
expr -> expr 'GT' expr : {'>',line('$2'),'$1','$3'}.
expr -> expr 'GTEQ' expr : {'>=',line('$2'),'$1','$3'}.
expr -> expr 'AMPAMP' expr : {'&&',line('$2'),'$1','$3'}.
expr -> expr 'BARBAR' expr : {'||',line('$2'),'$1','$3'}.
expr -> 'LP' expr 'RP' : '$2'.
expr -> id 'LP' 'RP' : {call, line('$2'), '$1', []}.
expr -> id 'LP' expr_list 'RP' : {call, line('$2'), '$1', '$3'}.

expr_list -> expr : ['$1'].
expr_list -> expr 'COMMA' expr_list : ['$1'|'$3'].

expr_array -> 'LBRACE' expr_list 'RBRACE' : {array,line('$1'),'$2'}.
     

Erlang code.

line([H|_]) -> line(H);
line(T) when is_tuple(T) -> element(2, T).
