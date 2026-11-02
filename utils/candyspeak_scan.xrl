%% -*- erlang -*-
%% CandySpeak scanner
%%

Definitions.

B	= [0-1]
D	= [0-9]
L       = [a-zA-Z_\$]
A       = ({L}|{U})
H	= [a-fA-F0-9]
E	= [Ee][+-]?{D}+
WS      = [\t\s\r]

Rules.

%% keywords

module             : {token,{'module',TokenLine}}.
end                : {token,{'end',TokenLine}}.
states             : {token,{'states',TokenLine}}.
when               : {token,{'when',TokenLine}}.
digital		   : {token,{'digital',TokenLine}}.
analog		   : {token,{'analog',TokenLine}}.
variable	   : {token,{'variable',TokenLine}}.
local	           : {token,{'local',TokenLine}}.
param              : {token,{'param',TokenLine}}.
constant	   : {token,{'constant',TokenLine}}.
timer              : {token,{'timer',TokenLine}}.
field              : {token,{'field',TokenLine}}.
buffer             : {token,{'buffer',TokenLine}}.
define             : {token,{'define',TokenLine}}.
annotate           : {token,{'annotate',TokenLine}}.
import             : {token,{'import',TokenLine}}.
disable            : {token,{'disable',TokenLine}}.
enable             : {token,{'enable',TokenLine}}.
integer            : {token,{'integer',TokenLine}}.
unsigned           : {token,{'unsigned',TokenLine}}.
string             : {token,{'string',TokenLine}}.
float              : {token,{'float',TokenLine}}.
in		   : {token,{'in',TokenLine}}.
out		   : {token,{'out',TokenLine}}.
inout      	   : {token,{'inout',TokenLine}}.
little	           : {token,{'little',TokenLine}}.
big		   : {token,{'big',TokenLine}}.
native     	   : {token,{'native',TokenLine}}.
pwm                : {token,{'pwm',TokenLine}}.
bind               : {token,{'bind',TokenLine}}.
pullup             : {token,{'pullup',TokenLine}}.
pulldown           : {token,{'pulldown',TokenLine}}.
low                : {token,{'low',TokenLine}}.
high               : {token,{'high',TokenLine}}.
low                : {token,{'low',TokenLine}}.
both               : {token,{'both',TokenLine}}.
soft               : {token,{'soft',TokenLine}}.
ready              : {token,{'ready',TokenLine}}.
rising             : {token,{'rising',TokenLine}}.
falling            : {token,{'falling',TokenLine}}.
can		   : {token,{'can',TokenLine}}.
i2c		   : {token,{'i2c',TokenLine}}.
spi		   : {token,{'spi',TokenLine}}.
udp		   : {token,{'udp',TokenLine}}.
tcp		   : {token,{'tcp',TokenLine}}.
uart		   : {token,{'uart',TokenLine}}.
sms		   : {token,{'sms',TokenLine}}.
onewire		   : {token,{'onewire',TokenLine}}.
console		   : {token,{'console',TokenLine}}.
repl		   : {token,{'repl',TokenLine}}.
%% parts
val                : {token,{'val', TokenLine}}.
pin                : {token,{'pin', TokenLine}}.
port               : {token,{'port', TokenLine}}.
dir                : {token,{'dir', TokenLine}}.
endian             : {token,{'endian', TokenLine}}.
period             : {token,{'period', TokenLine}}.
fired              : {token,{'fired', TokenLine}}.
id                 : {token,{'id', TokenLine}}.
rx                 : {token,{'rx', TokenLine}}.
tx                 : {token,{'tx', TokenLine}}.
len                : {token,{'len', TokenLine}}.

{L}({L}|{D})*	   : {token,{'WORD',TokenLine,TokenChars}}.
0[xX]{H}+          : {token,{'INT',TokenLine,TokenChars}}.
{D}+               : {token,{'INT',TokenLine,TokenChars}}.
{D}+\.{D}+\.{D}+\.{D}+ : {token,{'INT',TokenLine,TokenChars}}.
{D}+\.{D}+          : {token,{'FLT',TokenLine,TokenChars}}.
"(\^.|\.|[^\"])*" : S = lists:sublist(TokenChars,2,TokenLen-2),
                    {token,{'STR',TokenLine,S}}.
==                  : {token,{'==',TokenLine}}.
!=                  : {token,{'!=',TokenLine}}.
<-                  : {token,{'<-',TokenLine}}.
<=                  : {token,{'<=',TokenLine}}.
>=                  : {token,{'>=',TokenLine}}.
<<=		    : {token,{'<<=',TokenLine}}.
>>=		    : {token,{'>>=',TokenLine}}.
<<		    : {token,{'<<',TokenLine}}.
>>		    : {token,{'>>',TokenLine}}.
=		    : {token,{'=',TokenLine}}.
<		    : {token,{'<',TokenLine}}.
>		    : {token,{'>',TokenLine}}.
!		    : {token,{'!',TokenLine}}.
#		    : {token,{'#',TokenLine}}.
-		    : {token,{'-',TokenLine}}.
\+		    : {token,{'+',TokenLine}}.
//.*\n              : {token,{'end of line',TokenLine}}.
//.*                : skip_token.
/		    : {token,{'/',TokenLine}}.
\%		    : {token,{'%',TokenLine}}.
\*		    : {token,{'*',TokenLine}}.
\?		    : {token,{'?',TokenLine}}.
\|\|		    : {token,{'||',TokenLine}}.
\|		    : {token,{'|',TokenLine}}.
\&\&	            : {token,{'&&',TokenLine}}.
\&		    : {token,{'&',TokenLine}}.
\^		    : {token,{'^',TokenLine}}.
\~		    : {token,{'~',TokenLine}}.
\(		    : {token,{'(',TokenLine}}.
\)		    : {token,{')',TokenLine}}.
\[		    : {token,{'[',TokenLine}}.
\]		    : {token,{']',TokenLine}}.
\{		    : {token,{'{',TokenLine}}.
\}		    : {token,{'}',TokenLine}}.
\.\.		    : {token,{'..',TokenLine}}.
\.		    : {token,{'.',TokenLine}}.
\,		    : {token,{',',TokenLine}}.
\:		    : {token,{':',TokenLine}}.
\n                  : {token,{'end of line',TokenLine}}.
{WS}+		    : skip_token .

Erlang code.
