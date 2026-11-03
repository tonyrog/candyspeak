%% -*- erlang -*-

Terminals
  'module' 'end' 'states' 
  'when'
  'digital' 'analog' 'variable' 'local' 'param' 'constant' 
  'timer' 'field' 'buffer' 'define' 'disable' 'enable'
  'annotate' 'import'
  'integer' 'unsigned' 'string' 'float' 'in'
  'out' 'inout' 'little' 'big' 'native' 'pwm' 'bind'
  'val' 'pin' 'port' 'dir' 'endian' 'period' 'fired'
  'id' 'rx' 'tx' 'dlc' 'len'
  'pullup' 'pulldown'
  'can' 'i2c' 'spi' 'udp' 'tcp' 'uart' 'sms' 'console' 'repl' 'onewire'
  'low' 'high' 'both'  'soft' 'ready' 'rising' 'falling'
  'WORD' 'INT' 'FLT' 'STR'
  '==' '!=' '<=' '>=' '<<' '>>' '<' '>' 
  '=' '<-' '<<=' '>>='
  '!' '#' '-' '+' '/' '%'
  '*' '?' '&&' '&' '||' '|' '^' '~'
  '(' ')' '[' ']' '{' '}' '..' '.' ',' ':'
  'end of line'
  .

Nonterminals
  annotate_items annotate_item annotate_value aid
  file statement declaration rule state_list
  expr array expr_list expr_array buftype pin_list pin_range 
  bit_range rule_list rule_range
  assignment_list assignment name xid fid part lhs ref pfield 
  pack_list unpack_list
  res neg obj_params
  type order iodir pull trig option options
  .

Rootsymbol file.
Endsymbol '$end'.

Unary 1200 neg. 

Unary 1100 '!' '~'.
Left 1000 '*' '/' '%'.
Left 900  '+' '-'.
Left 800  '<<' '>>'.
Left 700  '<' '<=' '>' '>='.
Left 600  '==' '!='.
Left 500  '&'.
Left 400  '^'.
Left 300  '|'.
Left 10   '||'.
Left 20   '&&'.

file -> statement 'end of line' file : ['$1'|'$3'].
file -> 'end of line' file : '$2'.
file -> '$empty' : [].

statement -> '#' declaration : '$2'.
statement -> '>' expr : {immediate, '$2'}.
statement -> '>' assignment : {immediate, '$2'}.
statement -> rule : '$1'.

declaration -> 'module' name : {module,line('$1'),'$2'}.

declaration -> 'states' state_list: {states,line('$1'),'$2'}.
declaration -> 'in' state_list: {'in',line('$1'),'$2'}.
declaration -> 'when' expr : {'when',line('$1'),'$2'}.
declaration -> 'digital' xid array res options : 
		   {digital,line('$1'),'$2','$3','$4','$5',[]}.
declaration -> 'digital' xid array res options pin_list : 
		   {digital,line('$1'),'$2','$3','$4','$5','$6'}.
declaration -> 'analog' xid array res options pin_list :
		   {analog,line('$1'),'$2','$3','$4','$5','$6'}.
declaration -> 'analog' xid array res options :
		   {analog,line('$1'),'$2','$3','$4','$5',[]}.

declaration -> 'variable' xid array res options :
		   {variable,line('$1'),'$2','$3','$4','$5',undefined}.
declaration -> 'variable' xid array res options '=' expr :
		   {variable,line('$1'),'$2','$3','$4','$5','$7'}.

declaration -> 'constant' xid array res options '=' expr :
		   {constant,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'constant' xid array res options '=' expr_array :
		   {constant,line('$1'),'$2','$3','$4','$5','$7'}.

declaration -> 'local' xid res options '=' expr :
		   {local,line('$1'),'$2','$3','$4','$6'}.
%% `#local X in' -- the instance binds it, with no default. (`#local X in = c'
%% and `#local X out = e' are the form above: `options' carries in/out.)
declaration -> 'local' xid res options :
		   {local,line('$1'),'$2','$3','$4',undefined}.
declaration -> 'param' xid res options :
		   {param,line('$1'),'$2','$3','$4',undefined}.
declaration -> 'param' xid res options '=' expr :
		   {param,line('$1'),'$2','$3','$4','$6'}.

declaration -> 'timer' xid expr : {timer,line('$1'),'$2','$3',undefined}.
declaration -> 'timer' xid expr '=' expr : {timer,line('$1'),'$2','$3','$5'}.
declaration -> 'field' xid res options name '[' bit_range ']' :
		   {field,line('$1'),'$2','$3','$4','$5','$7'}.
declaration -> 'buffer' xid res options buftype :
		   {buffer,line('$1'),'$2','$3','$4','$5'}.
declaration -> 'define' xid expr : {define,line('$1'),'$2','$3'}.

%% #annotate <tool> <target> [key[=value] ...]
%% #annotate <tool> <word> "any chars"
%%
%% In the string form the word is the tool's, not a target: `#annotate varp
%% invariant "Red + Green <= 1"'. What the string means -- candyspeak, another
%% language -- is the tool's business too.
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
%% #import <name> | <root> "<path>" | "<path>" -- another source file, loaded
%% once. The parser only carries the request; candyspeak:parse/1 expands it,
%% the way csp's port does (port/csp_linux.c, csp_import_run).
declaration -> 'import' aid : {import,line('$1'),name,element(3,'$2')}.
declaration -> 'import' aid 'STR' :
		   {import,line('$1'),{root,element(3,'$2')},element(3,'$3')}.
declaration -> 'import' 'STR' : {import,line('$1'),quoted,element(3,'$2')}.

declaration -> 'annotate' name name 'STR' :
		   {annotate,line('$1'),'$2','$3','$4'}.

declaration -> 'annotate' name name annotate_items :
		   {annotate,line('$1'),'$2','$3','$4'}.
%% `*' is the tool itself rather than one signal: `#annotate panel * trace=off'.
%% What that means is the tool's business, like the keys.
declaration -> 'annotate' name '*' annotate_items :
		   {annotate,line('$1'),'$2',{'WORD',line('$3'),"*"},'$4'}.


%% id in annotate may be any keyword or id
aid -> name : '$1'.
    
aid -> 'module'   : make_word('$1').
aid -> 'end'      : make_word('$1').
aid -> 'states'   : make_word('$1').
aid -> 'when'     : make_word('$1').
aid -> 'digital'  : make_word('$1').
aid -> 'analog'   : make_word('$1').
aid -> 'variable' : make_word('$1').
aid -> 'local'    : make_word('$1').
aid -> 'param'    : make_word('$1').
aid -> 'constant' : make_word('$1').
aid -> 'timer'    : make_word('$1').
aid -> 'field'    : make_word('$1').
aid -> 'buffer'   : make_word('$1').
aid -> 'define'   : make_word('$1').
aid -> 'disable'  : make_word('$1').
aid -> 'enable'   : make_word('$1').
aid -> 'annotate' : make_word('$1').
aid -> 'import'   : make_word('$1').
aid -> 'integer'  : make_word('$1').
aid -> 'unsigned' : make_word('$1').
aid -> 'string'   : make_word('$1').
aid -> 'float'    : make_word('$1').
aid -> 'in'       : make_word('$1').
aid -> 'out'      : make_word('$1').
aid -> 'inout'    : make_word('$1').
aid -> 'little'   : make_word('$1').
aid -> 'big'      : make_word('$1').
aid -> 'native'   : make_word('$1').
aid -> 'pwm'      : make_word('$1').
aid -> 'bind'     : make_word('$1').
aid -> 'val'      : make_word('$1').
aid -> 'pin'      : make_word('$1').
aid -> 'port'     : make_word('$1').
aid -> 'dir'      : make_word('$1').
aid -> 'endian'   : make_word('$1').
aid -> 'period'   : make_word('$1').
aid -> 'fired'    : make_word('$1').
aid -> 'id'       : make_word('$1').
aid -> 'rx'       : make_word('$1').
aid -> 'tx'       : make_word('$1').
aid -> 'dlc'      : make_word('$1').
aid -> 'len'      : make_word('$1').
aid -> 'pullup'   : make_word('$1').
aid -> 'pulldown' : make_word('$1').
aid -> 'can'      : make_word('$1').
aid -> 'i2c'      : make_word('$1').
aid -> 'spi'      : make_word('$1').
aid -> 'udp'      : make_word('$1').
aid -> 'tcp'      : make_word('$1').
aid -> 'uart'     : make_word('$1').
aid -> 'low'      : make_word('$1').
aid -> 'high'     : make_word('$1').
aid -> 'both'     : make_word('$1').
aid -> 'soft'     : make_word('$1').
aid -> 'ready'    : make_word('$1').
aid -> 'rising'   : make_word('$1').
aid -> 'falling'  : make_word('$1').

annotate_items -> '$empty' : [].
annotate_items -> annotate_item annotate_items : ['$1'|'$2'].

annotate_item -> aid '=' annotate_value : {'$1','$3'}.
annotate_item -> aid : {'$1',true}.

annotate_value -> aid    : '$1'.
annotate_value -> 'INT' : '$1'.
annotate_value -> 'FLT' : '$1'.
annotate_value -> 'STR' : '$1'.
%% A signed channel's range starts below zero: min=-512
annotate_value -> '-' 'INT' : {'INT',line('$2'),"-" ++ element(3,'$2')}.
annotate_value -> '-' 'FLT' : {'FLT',line('$2'),"-" ++ element(3,'$2')}.
declaration -> 'end': {'end',line('$1')}.

%% Special Immediates
declaration -> 'disable' rule_list : {disable,line('$1'),'$2'}.
declaration -> 'enable' rule_list : {enable,line('$1'),'$2'}.

%% #<modeule-name> <object-name>  (Field (=|<-) Expr)*

declaration -> name name obj_params :
		   {object,line('$1'),'$1','$2','$3'}.

name -> 'WORD' : '$1'.

xid -> name : '$1'.
xid -> 'in'  : make_word('$1').
xid -> 'out' : make_word('$1').
xid -> 'low' : make_word('$1').
xid -> 'ready' : make_word('$1').

part -> 'val' : make_word('$1').
part -> 'pin' : make_word('$1').
part -> 'port' : make_word('$1').
part -> 'dir' : make_word('$1').
part -> 'pwm' : make_word('$1').
part -> 'endian' : make_word('$1').
part -> 'pullup' : make_word('$1').
part -> 'pulldown' : make_word('$1').
part -> 'period' : make_word('$1').
part -> 'fired' : make_word('$1').
part -> 'id' : make_word('$1').
part -> 'rx' : make_word('$1').
part -> 'tx' : make_word('$1').
part -> 'dlc' : make_word('$1').
part -> 'len' : make_word('$1').
    
obj_params -> '$empty' : [].
obj_params -> assignment obj_params : ['$1'|'$2'].

%%res -> ':' 'INT' : '$2'.
res -> ':' expr : '$2'.
res -> '$empty' : default.

array -> '[' 'INT' ']' : {array_size,line('$1'),'$2'}.
array ->  '$empty' : scalar.

bit_range -> 'INT' '..' 'INT' : {range,line('$2'),'$1','$3'}.
bit_range -> 'INT' : '$1'.

rule_list -> rule_range : ['$1'].
rule_list -> rule_range rule_list : ['$1'|'$2'].

rule_range -> 'INT' : '$1'.
rule_range -> 'INT' '-' 'INT' : {'$1', '$3'}.

pin_list -> pin_range : ['$1'].
pin_list -> pin_range pin_list : ['$1'|'$2'].

pin_range -> 'INT' : {pin,line('$1'),'$1'}.
pin_range -> 'WORD' : {pin,line('$1'),'$1'}.  %% named pin
pin_range -> 'INT' ':' 'INT' : {port_pin,line('$1'),'$1','$3'}.
pin_range -> 'INT' ':' 'INT' '..' 'INT' : {port_pin,line('$1'),'$1',
						   {range,line('$4'),'$3','$5'}}.
%% can <frame-id> 
buftype -> 'can' 'INT' : [{can,'$2'}].
%% i2c <bus> <addr> <reg>
buftype -> 'i2c' 'INT' 'INT' 'INT' : [{i2c,'$2','$3','$4'}].
%% spi <bus> <cs-port> ':' <cs-pin> <cmd>
buftype -> 'spi' 'INT' 'INT' ':' 'INT' 'INT' :
	       [{spi,'$2',{'$3','$5'},'$6'}].
%% udp <port> == udp <port> 0
buftype -> 'udp' 'INT' : [{udp,'$2',udefined}].
%% udp <port> <ip>
buftype -> 'udp' 'INT' 'INT' : [{udp,'$2','$3'}].
%% tcp <port> == tcp <port> 0
buftype -> 'tcp' 'INT' : [{tcp,'$2',udefined}].
%% tcp <port> <ip>
buftype -> 'tcp' 'INT' 'INT' : [{tcp,'$2','$3'}].
%% uart <unit> [<baud>] (default baud = 9600)
buftype -> 'uart' 'INT' : [{uart,'$2',9600}].
buftype -> 'uart' 'INT' 'INT' : [{uart,'$2','$3'}].
%% the two ends of the console wire
buftype -> 'console' : [{console}].
buftype -> 'repl' : [{repl}].
%% sms <param>: a modem; the string #param lists the numbers
buftype -> 'sms' 'WORD' : [{sms,'$2'}].
%% sms <param> <pin>: ...and the string #param with the SIM's PIN
buftype -> 'sms' 'WORD' 'WORD' : [{sms,'$2','$3'}].
%% onewire <port>:<pin> <param>: a 1-Wire device; the string #param holds its id
buftype -> 'onewire' 'INT' ':' 'INT' 'WORD' :
    [{onewire,{'$2','$4'},'$5'}].
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
option -> order : {endian,'$1'}.
option -> type   : {type, '$1'}.
option -> pull   : {pull, '$1'}.
option -> trig   : {trig, '$1'}.
option -> 'pwm' : pwm.
option -> 'bind' name '[' bit_range ']':  {bind,{'$2','$4'}}.

trig -> 'low' : low.
trig -> 'high' : high.
trig -> 'both' : both.
trig -> 'soft' : soft.
trig -> 'ready' : ready.
trig -> 'rising' : rising.
trig -> 'falling' : falling.

pull -> 'pullup'   : pullup.
pull -> 'pulldown' : pulldown.

order -> 'big'    : big.
order -> 'little' : little.
order -> 'native' : native.

iodir -> 'in'    : in.
iodir -> 'out'   : out.
iodir -> 'inout' : inout.

type -> 'unsigned' : unsigned.
type -> 'integer'  : integer.
type -> 'float'    : float.
type -> 'string'   : string.

state_list -> name : ['$1'].
state_list -> name state_list : ['$1'|'$2'].
     
rule -> assignment_list '?' expr : {rule,line('$2'),'$1','$3'}.
rule -> assignment_list : {rule,line('$1'),'$1',undefined}.
rule -> lhs '<<=' pack_list : {rule, line('$2'), [{pack, '$1', '$3'}], true}.
rule -> lhs '>>=' unpack_list : {rule, line('$2'), [{unpack, '$1', '$3'}], true}.
rule -> lhs '<<=' pack_list '?' expr  : {rule, line('$2'),[{pack,'$1','$3'}],'$5'}.
rule -> lhs '>>=' unpack_list '?' expr :{rule, line('$2'),[{unpack,'$1','$3'}],'$5'}.

assignment -> lhs '=' expr   : {'=', line('$2'), '$1', '$3'}.
assignment -> lhs '<-' expr : {'<-',line('$2'), '$1', '$3'}.
assignment -> expr : '$1'.
    
assignment_list -> assignment : ['$1'].
assignment_list -> assignment ',' assignment_list : ['$1'|'$3'].

pack_list -> pfield : [{'$1',default}].
pack_list -> pfield ':' 'INT' : [{'$1','$3'}].
pack_list -> pfield pack_list : [{'$1',default}|'$2'].
pack_list -> '(' expr ')' ':' 'INT' : [{'$2','$5'}].
pack_list -> pfield ':' 'INT' pack_list : [{'$1','$3'}|'$4'].
pack_list -> '(' expr ')' ':' 'INT' pack_list : [{'$2','$5'}|'$6'].

pfield -> 'INT' : '$1'.
pfield -> ref : '$1'.

unpack_list -> ref : [{'$1',default}].
unpack_list -> ref ':' 'INT' : [{'$1','$3'}].
unpack_list -> ref unpack_list : [{'$1',default}|'$2'].
unpack_list -> ref ':' 'INT' unpack_list : [{'$1','$3'}|'$4'].

lhs -> ref : '$1'.

%% rhs field
fid -> xid  : '$1'.
%%fid -> xid '.' xid   : {fld,'$1','$3'}.
    
ref -> fid            : {field,line('$1'),'$1'}.
ref -> fid '.' part : {field,line('$1'),{part,line('$2'),'$1','$3'}}.
ref -> fid '.' xid  : {field,line('$1'),{fld,line('$2'),'$1','$3'}}.
ref -> fid '.' xid '.' part : {field,line('$1'),{part,line('$2'),{fld,line('$4'),'$1','$3'},'$5'}}.
ref -> fid '[' expr ']' : {field,line('$1'),{index, line('$2'), '$1','$3'}}.
ref -> fid '[' 'INT' '..' 'INT' ']' : 
	     {field,line('$1'),{index,line('$2'),'$1',
				{range,line('$4'),'$3','$5'}}}.

neg -> '-' : '$1'.

expr -> 'INT'         : '$1'.
expr -> 'FLT'         : '$1'.
expr -> 'STR'         : '$1'.
expr -> ref         : '$1'.
expr -> neg expr      : {'-',line('$1'),'$2'}.
expr -> '~' expr  : {'~',line('$1'),'$2'}.
expr -> '!' expr :
	    case '$2' of
		{'!', _Ln, Cond} -> Cond;
		Cond -> {'!',line('$1'),Cond}
	    end.
expr -> expr '+' expr : {'+',line('$2'),'$1','$3'}.
expr -> expr '-' expr : {'-',line('$2'),'$1','$3'}.
expr -> expr '*' expr : {'*',line('$2'),'$1','$3'}.
expr -> expr '/' expr : {'/',line('$2'),'$1','$3'}.
expr -> expr '%' expr : {'%',line('$2'),'$1','$3'}.
expr -> expr '&' expr : {'&',line('$2'),'$1','$3'}.
expr -> expr '|' expr : {'|',line('$2'),'$1','$3'}.
expr -> expr '^' expr : {'^',line('$2'),'$1','$3'}.
expr -> expr '<<' expr : {'<<',line('$2'),'$1','$3'}.
expr -> expr '>>' expr : {'>>',line('$2'),'$1','$3'}.
expr -> expr '==' expr : {'==',line('$2'),'$1','$3'}.
expr -> expr '!=' expr : {'!=',line('$2'),'$1','$3'}.
expr -> expr '<=' expr : {'<=',line('$2'),'$1','$3'}.
expr -> expr '<' expr : {'<',line('$2'),'$1','$3'}.
expr -> expr '>' expr : {'>',line('$2'),'$1','$3'}.
expr -> expr '>=' expr : {'>=',line('$2'),'$1','$3'}.
expr -> expr '&&' expr : {'&&',line('$2'),'$1','$3'}.
expr -> expr '||' expr : {'||',line('$2'),'$1','$3'}.
expr -> '(' expr ')' : '$2'.
expr -> name '(' ')' : {call, line('$2'), '$1', []}.
expr -> name '(' expr_list ')' : {call, line('$2'), '$1', '$3'}.
%% rising(x) and falling(x) are functions as well as a pin's trigger option.
expr -> 'rising' '(' expr_list ')' : {call, line('$2'), make_word('$1'), '$3'}.
expr -> 'falling' '(' expr_list ')' : {call, line('$2'), make_word('$1'), '$3'}.

expr_list -> expr : ['$1'].
expr_list -> expr ',' expr_list : ['$1'|'$3'].

expr_array -> '{' expr_list '}' : {array,line('$1'),'$2'}.
     

Erlang code.

line([H|_]) -> line(H);
line(T) when is_tuple(T) -> element(2, T).

%% A keyword token is named as it is written.
make_word({Atom,Ln}) when is_atom(Atom), is_integer(Ln) ->
    {'WORD', Ln, atom_to_list(Atom)}.

