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

module             : {token,{'D_MODULE',TokenLine}}.
end                : {token,{'D_END',TokenLine}}.
states             : {token,{'D_STATES',TokenLine}}.
%% in                 : {token,{'D_IN',TokenLine}}.
when               : {token,{'D_WHEN',TokenLine}}.
digital		   : {token,{'D_DIGITAL',TokenLine}}.
analog		   : {token,{'D_ANALOG',TokenLine}}.
variable	   : {token,{'D_VARIABLE',TokenLine}}.
local	           : {token,{'D_LOCAL',TokenLine}}.
param              : {token,{'D_PARAM',TokenLine}}.
constant	   : {token,{'D_CONSTANT',TokenLine}}.
timer              : {token,{'D_TIMER',TokenLine}}.
field              : {token,{'D_FIELD',TokenLine}}.
buffer             : {token,{'D_BUFFER',TokenLine}}.
define             : {token,{'D_DEFINE',TokenLine}}.
disable            : {token,{'D_DISABLE',TokenLine}}.
enable             : {token,{'D_ENABLE',TokenLine}}.
integer            : {token,{'T_INTEGER',TokenLine}}.
unsigned           : {token,{'T_UNSIGNED',TokenLine}}.
string             : {token,{'T_STRING',TokenLine}}.
float              : {token,{'T_FLOAT',TokenLine}}.
in		   : {token,{'T_IN',TokenLine}}.
out		   : {token,{'T_OUT',TokenLine}}.
inout      	   : {token,{'T_INOUT',TokenLine}}.
little	           : {token,{'T_LITTLE',TokenLine}}.
big		   : {token,{'T_BIG',TokenLine}}.
native     	   : {token,{'T_NATIVE',TokenLine}}.
can		   : {token,{'T_CAN',TokenLine}}.
i2c		   : {token,{'T_I2C',TokenLine}}.
spi		   : {token,{'T_SPI',TokenLine}}.
udp		   : {token,{'T_UDP',TokenLine}}.
tcp		   : {token,{'T_TCP',TokenLine}}.
uart		   : {token,{'T_UART',TokenLine}}.
{L}({L}|{D})*	   : {token,{'WORD',TokenLine,TokenChars}}.
0[xX]{H}+          : {token,{'INT',TokenLine,TokenChars}}.
{D}+               : {token,{'INT',TokenLine,TokenChars}}.
{D}+\.{D}+\.{D}+\.{D}+ : {token,{'INT',TokenLine,TokenChars}}.
{D}+\.{D}+          : {token,{'FLT',TokenLine,TokenChars}}.
==                  : {token,{'EQEQ',TokenLine}}.
!=                  : {token,{'NEQ',TokenLine}}.
<=                  : {token,{'LTEQ',TokenLine}}.
>=                  : {token,{'GTEQ',TokenLine}}.
<<		    : {token,{'LTLT',TokenLine}}.
>>		    : {token,{'GTGT',TokenLine}}.
=		    : {token,{'EQ',TokenLine}}.
<		    : {token,{'LT',TokenLine}}.
>		    : {token,{'GT',TokenLine}}.
!		    : {token,{'EXCLAMATION',TokenLine}}.
#		    : {token,{'HASH',TokenLine}}.
-		    : {token,{'MINUS',TokenLine}}.
\+		    : {token,{'PLUS',TokenLine}}.
//.*\n              : {token,{'NEWLINE',TokenLine}}.
/		    : {token,{'SLASH',TokenLine}}.
\%		    : {token,{'PERCENT',TokenLine}}.
\*		    : {token,{'ASTERISK',TokenLine}}.
\?		    : {token,{'QUEST',TokenLine}}.
\|\|		    : {token,{'BARBAR',TokenLine}}.
\|		    : {token,{'BAR',TokenLine}}.
\&\&	            : {token,{'AMPAMP',TokenLine}}.
\&		    : {token,{'AMP',TokenLine}}.
\^		    : {token,{'CIRC',TokenLine}}.
\~		    : {token,{'TILDE',TokenLine}}.
\(		    : {token,{'LP',TokenLine}}.
\)		    : {token,{'RP',TokenLine}}.
\[		    : {token,{'LB',TokenLine}}.
\]		    : {token,{'RB',TokenLine}}.
\{		    : {token,{'LBRACE',TokenLine}}.
\}		    : {token,{'RBRACE',TokenLine}}.
\.\.		    : {token,{'DOTDOT',TokenLine}}.
\.		    : {token,{'DOT',TokenLine}}.
\,		    : {token,{'COMMA',TokenLine}}.
\:		    : {token,{'COLON',TokenLine}}.
\n                  : {token,{'NEWLINE',TokenLine}}.
{WS}+		    : skip_token .

Erlang code.
