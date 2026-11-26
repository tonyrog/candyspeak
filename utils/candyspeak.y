// CandySpeak, the grammar. An action is a NAME, the callback the
// parser makes when it reduces the rule: candyspeak_parse_act.erl in Erlang.
// lrgen.py numbers the names 0..255 in the order they first
// appear. Tokens: %token NAME "spelling" -- the spelling is the
// lexer's category.
%token MODULE "module"
%token END "end"
%token STATES "states"
%token WHEN "when"
%token ELSE "else"
%token DIGITAL "digital"
%token ANALOG "analog"
%token VARIABLE "variable"
%token LOCAL "local"
%token PARAM "param"
%token CONSTANT "constant"
%token TIMER "timer"
%token FIELD "field"
%token BUFFER "buffer"
%token DEFINE "define"
%token DISABLE "disable"
%token ENABLE "enable"
%token ANNOTATE "annotate"
%token IMPORT "import"
%token HOLE "hole"
%token INTEGER "integer"
%token UNSIGNED "unsigned"
%token STRING "string"
%token FLOAT "float"
%token IN "in"
%token OUT "out"
%token INOUT "inout"
%token LITTLE "little"
%token BIG "big"
%token NATIVE "native"
%token PWM "pwm"
%token BIND "bind"
%token VAL "val"
%token PIN "pin"
%token PORT "port"
%token DIR "dir"
%token ENDIAN "endian"
%token PERIOD "period"
%token FIRED "fired"
%token ID "id"
%token RX "rx"
%token TX "tx"
%token DLC "dlc"
%token LEN "len"
%token PULLUP "pullup"
%token PULLDOWN "pulldown"
%token CAN "can"
%token I2C "i2c"
%token SPI "spi"
%token UDP "udp"
%token TCP "tcp"
%token UART "uart"
%token SMS "sms"
%token CONSOLE "console"
%token REPL "repl"
%token ONEWIRE "onewire"
%token LOW "low"
%token HIGH "high"
%token BOTH "both"
%token SOFT "soft"
%token READY "ready"
%token RISING "rising"
%token FALLING "falling"
%token INVERT "invert"
%token WORD "WORD"
%token INT "INT"
%token FLT "FLT"
%token STR "STR"
%token EQ "=="
%token NE "!="
%token LE "<="
%token GE ">="
%token SHL "<<"
%token SHR ">>"
%token LT "<"
%token GT ">"
%token ASSIGN "="
%token LARROW "<-"
%token PACK "<<="
%token UNPACK ">>="
%token NOT "!"
%token HASH "#"
%token MINUS "-"
%token PLUS "+"
%token SLASH "/"
%token PERCENT "%"
%token STAR "*"
%token QUEST "?"
%token ANDAND "&&"
%token AMP "&"
%token OROR "||"
%token BAR "|"
%token CARET "^"
%token TILDE "~"
%token LP "("
%token RP ")"
%token LB "["
%token RB "]"
%token LC "{"
%token RC "}"
%token DOTDOT ".."
%token DOT "."
%token COMMA ","
%token COLON ":"
%token EOL "end of line"
%left OROR
%left ANDAND
%left BAR
%left CARET
%left AMP
%left EQ NE
%left LT LE GT GE
%left SHL SHR
%left PLUS MINUS
%left STAR SLASH PERCENT
%right NOT TILDE
%right UMINUS
%start program
%%
program
    //A program: its statements, in order.
    : file { program }
    ;
file
    //Left recursive: any number of lines on a bounded stack, the
    //statements gathered last first.
    : %empty { nil }
    | file statement EOL { file_stmt }
    | file EOL { pass1 }
    ;
statement
    : HASH declaration { pass2 }
    | GT expr { immediate }
    | GT assignment { immediate }
    | rule { pass1 }
    ;
declaration
    : MODULE name { module }
    | STATES state_list { states }
    | IN state_list { in_states }
    | WHEN expr { when }
    // the other half of a #when; candyspeak:parse turns it into #end + #when !C
    | ELSE { else }
    | DIGITAL xid array res options { digital }
    | DIGITAL xid array res options pin_list { digital_pins }
    | ANALOG xid array res options pin_list { analog_pins }
    | ANALOG xid array res options { analog }
    | VARIABLE xid array res options { variable }
    | VARIABLE xid array res options ASSIGN expr { variable_init }
    | CONSTANT xid array res options ASSIGN expr { constant }
    | CONSTANT xid array res options ASSIGN expr_array { constant }
    | LOCAL xid res options ASSIGN expr { local_init }
    // `#local X in' -- the instance binds it, with no default. (`#local X in = c'
    // and `#local X out = e' are the form above: `options' carries in/out.)
    | LOCAL xid res options { local }
    | PARAM xid res options { param }
    | PARAM xid res options ASSIGN expr { param_init }
    | TIMER xid expr { timer }
    | TIMER xid expr ASSIGN expr { timer_init }
    | FIELD xid res options name LB bit_range RB { field }
    | BUFFER xid res options buftype { buffer }
    | DEFINE xid expr { define }
    // #annotate <tool> <target> [key[=value] ...]
    // #annotate <tool> <word> "any chars"
    //
    // In the string form the word is the tool's, not a target: `#annotate varp
    // invariant "Red + Green <= 1"'. What the string means -- candyspeak, another
    // language -- is the tool's business too.
    //
    // Inert: the parser carries it, nothing else looks at it, and it never reaches
    // the compiler or a ROM. It is for tools that read the SOURCE -- the panel's
    // widget choice, a property for the model checker, a tuning range -- so that
    // presentation and tooling metadata live next to the declaration they describe
    // instead of in a separate file that drifts out of step.
    //
    // The TOOL name owns the key space: without it every tool invents keys the
    // others silently ignore. candyspeak validates neither the keys nor the values;
    // each tool validates its own and must warn on what it does not know.
    //
    // What candyspeak DOES check is the target: an annotation naming a signal that
    // does not exist is the same kind of mistake as a rule naming one, and build/1
    // already holds the map to catch it.
    // #import <name> | <root> "<path>" | "<path>" -- another source file, loaded
    // once. The parser only carries the request; candyspeak:parse/1 expands it,
    // the way csp's port does (port/csp_linux.c, csp_import_run).
    | IMPORT aid { import_name }
    | IMPORT aid STR { import_root }
    | IMPORT STR { import_quoted }
    | ANNOTATE name name STR { annotate }
    | ANNOTATE name name annotate_items { annotate }
    // `*' is the tool itself rather than one signal: `#annotate panel * trace=off'.
    // What that means is the tool's business, like the keys.
    | ANNOTATE name STAR annotate_items { annotate_all }
    | END { end }
    // Special Immediates
    | DISABLE rule_list { disable }
    | ENABLE rule_list { enable }
    // #<modeule-name> <object-name>  (Field (=|<-) Expr)*
    //
    // #route <src> <dst> -- bytes arriving at one buffer go out another, no rule
    // between (syntax.terms D_ROUTE). `route' is no reserved word, as it is not
    // in C either: a declaration word is a NAME looked up after the '#'. So the
    // line reads as an object line, and is told apart here.
    | name name obj_params { object }
    ;
aid
    // id in annotate may be any keyword or id
    : name { pass1 }
    | MODULE { word }
    | END { word }
    | STATES { word }
    | WHEN { word }
    | ELSE { word }
    | DIGITAL { word }
    | ANALOG { word }
    | VARIABLE { word }
    | LOCAL { word }
    | PARAM { word }
    | CONSTANT { word }
    | TIMER { word }
    | FIELD { word }
    | BUFFER { word }
    | DEFINE { word }
    | DISABLE { word }
    | ENABLE { word }
    | ANNOTATE { word }
    | IMPORT { word }
    | INTEGER { word }
    | UNSIGNED { word }
    | STRING { word }
    | FLOAT { word }
    | IN { word }
    | OUT { word }
    | INOUT { word }
    | LITTLE { word }
    | BIG { word }
    | NATIVE { word }
    | PWM { word }
    | BIND { word }
    | VAL { word }
    | PIN { word }
    | PORT { word }
    | DIR { word }
    | ENDIAN { word }
    | PERIOD { word }
    | FIRED { word }
    | ID { word }
    | RX { word }
    | TX { word }
    | DLC { word }
    | LEN { word }
    | PULLUP { word }
    | PULLDOWN { word }
    | CAN { word }
    | I2C { word }
    | SPI { word }
    | UDP { word }
    | TCP { word }
    | UART { word }
    | LOW { word }
    | HIGH { word }
    | BOTH { word }
    | SOFT { word }
    | INVERT { word }
    | READY { word }
    | RISING { word }
    | FALLING { word }
    ;
annotate_items
    : %empty { nil }
    | annotate_item annotate_items { cons12 }
    ;
annotate_item
    : aid ASSIGN annotate_value { key_value }
    | aid { key }
    ;
annotate_value
    : aid { pass1 }
    | INT { pass1 }
    | FLT { pass1 }
    | STR { pass1 }
    // A signed channel's range starts below zero: min=-512
    | MINUS INT { neg_int }
    | MINUS FLT { neg_flt }
    ;
name
    : WORD { pass1 }
    ;
xid
    : name { pass1 }
    | IN { word }
    | OUT { word }
    | LOW { word }
    | READY { word }
    // the other trigger words, and invert, are names as low and ready are:
    // `#variable high = 8'
    | HIGH { word }
    | BOTH { word }
    | SOFT { word }
    | RISING { word }
    | FALLING { word }
    | INVERT { word }
    ;
part
    : VAL { word }
    | PIN { word }
    | PORT { word }
    | DIR { word }
    | PWM { word }
    | ENDIAN { word }
    | PULLUP { word }
    | PULLDOWN { word }
    | PERIOD { word }
    | FIRED { word }
    | ID { word }
    | RX { word }
    | TX { word }
    | DLC { word }
    | LEN { word }
    ;
obj_params
    : %empty { nil }
    | assignment obj_params { cons12 }
    ;
res
    //res -> ':' 'INT' : '$2'.
    : COLON expr { pass2 }
    | %empty { default }
    ;
array
    : LB INT RB { array_size }
    | %empty { scalar }
    ;
bit_range
    : INT DOTDOT INT { bit_range }
    | INT { pass1 }
    ;
rule_list
    : rule_range { list1 }
    | rule_range rule_list { cons12 }
    ;
rule_range
    : INT { pass1 }
    | INT MINUS INT { rule_range }
    ;
// A device array's pins, a comma between them or not: `9:0..9',
// `0:1..3,7,9', `0:2,1:5', `1:1..3,2:1,3,5,9:,7..9'. A port names the
// pins after it; `9:,' is a port on its own.
pin_list
    : pin_item { list1 }
    | pin_item pin_list { cons12 }
    ;
pin_item
    : pin_range { pass1 }
    | pin_range COMMA { pass1 }
    | INT COLON COMMA { port }
    ;
pin_range
    : INT { pin }
    | WORD { pin }
    | INT COLON INT { port_pin }
    | INT COLON INT DOTDOT INT { port_pin_range }
    // pins on the port before
    | INT DOTDOT INT { pin_range }
    // from the pin after the last one named
    | DOTDOT INT { pin_to }
    ;
buftype
    // can <frame-id>
    : CAN INT { can }
    // can <name>: a #define or #constant folds, a #param is read at start
    | CAN WORD { can_name }
    // i2c <bus> <addr> <reg>
    | I2C INT INT INT { i2c }
    // spi <bus> <cs-port> ':' <cs-pin> <cmd>
    | SPI INT INT COLON INT INT { spi }
    // udp <port> == udp <port> 0
    | UDP INT { udp }
    // udp <port> <ip>
    | UDP INT INT { udp_ip }
    // the port by name, as for can
    | UDP WORD { udp_name }
    | UDP WORD INT { udp_name_ip }
    // the address by name too
    | UDP INT WORD { udp_ip_name }
    | UDP WORD WORD { udp_name_name }
    // tcp <port> == tcp <port> 0
    | TCP INT { tcp }
    // tcp <port> <ip>
    | TCP INT INT { tcp_ip }
    | TCP WORD { tcp_name }
    | TCP WORD INT { tcp_name_ip }
    | TCP INT WORD { tcp_ip_name }
    | TCP WORD WORD { tcp_name_name }
    // uart <unit> [<baud>] (default baud = 9600)
    | UART INT { uart }
    | UART INT INT { uart_baud }
    // the two ends of the console wire
    | CONSOLE { console }
    | REPL { repl }
    // sms <param>: a modem; the string #param lists the numbers
    | SMS WORD { sms }
    // sms <param> <pin>: ...and the string #param with the SIM's PIN
    | SMS WORD WORD { sms_pin }
    // onewire <port>:<pin> <param>: a 1-Wire device; the string #param holds its id
    | ONEWIRE INT COLON INT WORD { onewire }
    // plain byte buffer
    | %empty { nil }
    ;
options
    // uart config?
    // Data:4,Parity:4,Stop:4,Baud100:20 = 32
    // Data      = 5-9
    // parity    = N=0|E=1|O=2|M=3|S=4
    // stop-bits = 1,2
    // mode = 81100000+192
    : option options { cons12 }
    | %empty { nil }
    ;
option
    : iodir { opt_dir }
    | order { opt_endian }
    | type { opt_type }
    | pull { opt_pull }
    | trig { opt_trig }
    | PWM { pwm }
    // the pin's level is the opposite of the value; nothing above the pin sees it
    | INVERT { invert }
    | BIND name LB bit_range RB { opt_bind }
    ;
trig
    : LOW { low }
    | HIGH { high }
    | BOTH { both }
    | SOFT { soft }
    | READY { ready }
    | RISING { rising }
    | FALLING { falling }
    ;
pull
    : PULLUP { pullup }
    | PULLDOWN { pulldown }
    ;
order
    : BIG { big }
    | LITTLE { little }
    | NATIVE { native }
    ;
iodir
    : IN { in }
    | OUT { out }
    | INOUT { inout }
    ;
type
    : UNSIGNED { unsigned }
    | INTEGER { integer }
    | FLOAT { float }
    | STRING { string }
    ;
state_list
    : name { list1 }
    | name state_list { cons12 }
    ;
rule
    // A target is a ref as an expression is: no `lhs' of its own, so the
    // token after it decides by a shift, and no state reduces by lookahead.
    : assignment_list QUEST expr { rule_guarded }
    | assignment_list { rule }
    | ref PACK pack_list { pack }
    | ref UNPACK unpack_list { unpack }
    | ref PACK pack_list QUEST expr { pack_guarded }
    | ref UNPACK unpack_list QUEST expr { unpack_guarded }
    ;
assignment
    : ref ASSIGN expr { assign }
    | ref LARROW expr { assign_prev }
    // no target: a call for what it does
    | expr { assign_none }
    ;
assignment_list
    : assignment { list1 }
    | assignment COMMA assignment_list { cons13 }
    ;
pack_list
    : pfield { pack_one }
    | pfield COLON INT { pack_one_bits }
    | pfield pack_list { pack_more }
    | LP expr RP COLON INT { pack_expr_bits }
    | pfield COLON INT pack_list { pack_more_bits }
    | LP expr RP COLON INT pack_list { pack_more_expr_bits }
    ;
pfield
    : INT { pass1 }
    | ref { pass1 }
    ;
unpack_list
    : ref { pack_one }
    | ref COLON INT { pack_one_bits }
    | ref unpack_list { pack_more }
    | ref COLON INT unpack_list { pack_more_bits }
    ;
fid
    // rhs field
    : xid { pass1 }
    ;
ref
    //fid -> xid '.' xid   : {fld,'$1','$3'}.
    : fid { ref }
    | fid DOT part { ref_part }
    | fid DOT xid { ref_member }
    | fid DOT xid DOT part { ref_member_part }
    | fid LB expr RB { ref_index }
    // A named object's array element, `a.V[1]'.
    | fid DOT xid LB expr RB { ref_member_index }
    // Both ends of a byte range are constants, and a #define or a #constant
    // names one as well as a number does: `Frame[LO..HI]'.
    | fid LB expr DOTDOT expr RB { ref_slice }
    ;
expr
    : INT { pass1 }
    | FLT { pass1 }
    | STR { pass1 }
    // A hole: parses, evaluates as unknown, never assembles.
    | HOLE { hole }
    | ref { pass1 }
    | MINUS expr %prec UMINUS { neg }
    | TILDE expr { bnot }
    | NOT expr { not }
    | expr PLUS expr { add }
    | expr MINUS expr { sub }
    | expr STAR expr { mul }
    | expr SLASH expr { div }
    | expr PERCENT expr { rem }
    | expr AMP expr { band }
    | expr BAR expr { bor }
    | expr CARET expr { bxor }
    | expr SHL expr { shl }
    | expr SHR expr { shr }
    | expr EQ expr { eq }
    | expr NE expr { ne }
    | expr LE expr { le }
    | expr LT expr { lt }
    | expr GT expr { gt }
    | expr GE expr { ge }
    | expr ANDAND expr { and }
    | expr OROR expr { or }
    | LP expr RP { pass2 }
    | name LP RP { call0 }
    | name LP expr_list RP { call }
    | RISING LP expr_list RP { call_edge }
    // rising(x) and falling(x) are functions as well as a pin's trigger option.
    | FALLING LP expr_list RP { call_edge }
    ;
expr_list
    : expr { list1 }
    | expr COMMA expr_list { cons13 }
    ;
expr_array
    : LC expr_list RC { array }
    ;
