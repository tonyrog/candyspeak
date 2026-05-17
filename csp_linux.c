// linux main

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <poll.h>

#include "csp.h"
#include "csp_format.h"
#include "csp_dump.h"


#include <sys/time.h>

// Interactive mode globals
static struct termios orig_termios;
static int raw_mode = 0;
static const char* eeprom_file = "eeprom.db";

#define MAX_LINE_SIZE 128

typedef uint64_t tick_t;
struct timeval boot_time;
int debug = 0;
int debug_scan = 0;
int debug_parse = 0;
int debug_trace = 0;

static void *stack_top(void)
{
    static void* StackTop = NULL;
    if (StackTop == NULL) {
	FILE *f;
	unsigned long lo, hi;
	char perms[8];
	char line[256];
	void *result = NULL;	

	f = fopen("/proc/self/maps", "r");
	if (!f)
	    return NULL;
	while (fgets(line, sizeof(line), f)) {
	    if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) == 3) {
		if (strstr(line, "[stack]")) {
		    result = (void *)hi;
		    break;
		}
	    }
	}
	fclose(f);
	StackTop = result;
    }
    return StackTop;
}

int stack_used(void)
{
    char local;
    void *top = stack_top();
    if (!top)
        return -1;
    return (char *)top - &local;
}

static void time_init()
{
    gettimeofday(&boot_time, 0);
}

static tick_t time_tick(void)
{
    struct timeval now;
    struct timeval t;
    gettimeofday(&now, 0);
    timersub(&now, &boot_time, &t);
    return (tick_t)t.tv_sec*1000000 + t.tv_usec;
}

uint32_t csp_time_ms(void)
{
    return time_tick() / 1000;
}

unsigned long csp_time_us(void)
{
    return time_tick();
}

// platform print functions
int csp_print_char(char c)
{
    return putchar(c);
}

int csp_print_str(const char* s)
{
    return printf("%s", s);
}

int csp_print_int(ivalue_t v)
{
    return printf("%d", v);
}

int csp_print_uint(uvalue_t v)
{
    return printf("%u", v);
}

int csp_print_uintw(uvalue_t v, int nw)
{
    int n;
    while (v < nw) {
	csp_print_char('0');
	nw /= 10;
	n++;
    }
    return n+csp_print_uint(v);
}


int csp_print_float(fvalue_t v)
{
#if FVALUE_IS_FIXPOINT
    // Print Q16.16 as decimal
    int n;
    int neg = (v < 0);
    uint32_t absv = neg ? -v : v;
    int32_t intpart = absv >> FIX_SHIFT;
    uint32_t fracpart = absv & FIX_MASK;
    // Use 64-bit to avoid overflow: fracpart * 1000000 can exceed 32 bits
    fracpart = (uint32_t)(((uint64_t)fracpart * 1000000) >> FIX_SHIFT);
    if (neg) {
	csp_print_char('-');
	n = 1 + csp_print_uint(intpart);
    }
    else {
	n = csp_print_uint(intpart);
    }
    csp_print_char('.'); n++;
    return n+csp_print_uintw(fracpart, 100000);
#else
    return printf("%f", v);
#endif
}

int csp_print_hex(uvalue_t v)
{
    return printf("0x%x", v);
}

int csp_println(void)
{
    return putchar('\n');
}

// Terminal raw mode handling
static void disable_raw_mode(void)
{
    if (raw_mode) {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	raw_mode = 0;
    }
}

static int enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO))
	return -1;

    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0)
	return -1;

    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag |= (OPOST);  // keep output processing
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
	return -1;

    raw_mode = 1;
    return 0;
}

// Simple line editor - returns length, -1 on EOF
static int read_line(char* buf, int maxlen, const char* prompt)
{
    int pos = 0;
    int c;

    printf("%s", prompt);
    fflush(stdout);

    while (1) {
	c = getchar();
	if (c == EOF || c == 4) {  // EOF or Ctrl-D
	    if (pos == 0) return -1;
	    break;
	}
	else if (c == '\r' || c == '\n') {
	    printf("\n");
	    break;
	}
	else if (c == 127 || c == 8) {  // Backspace or DEL
	    if (pos > 0) {
		pos--;
		printf("\b \b");
		fflush(stdout);
	    }
	}
	else if (c == 21) {  // Ctrl-U: clear line
	    while (pos > 0) {
		pos--;
		printf("\b \b");
	    }
	    fflush(stdout);
	}
	else if (c >= 32 && c < 127 && pos < maxlen - 1) {
	    buf[pos++] = c;
	    putchar(c);
	    fflush(stdout);
	}
    }
    buf[pos] = '\0';
    return pos;
}

// Platform stub functions for csp_eeprom.c
static FILE* eeprom_fp = NULL;

int csp_eeprom_open_read(void)
{
    eeprom_fp = fopen(eeprom_file, "rb");
    return eeprom_fp ? 0 : -1;
}

int csp_eeprom_open_write(void)
{
    eeprom_fp = fopen(eeprom_file, "wb");
    return eeprom_fp ? 0 : -1;
}

void csp_eeprom_close(void)
{
    if (eeprom_fp) {
	fclose(eeprom_fp);
	eeprom_fp = NULL;
    }
}

int csp_eeprom_read(void* buf, size_t len)
{
    if (!eeprom_fp) return -1;
    return (fread(buf, 1, len, eeprom_fp) == len) ? 0 : -1;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    if (!eeprom_fp) return -1;
    return (fwrite(buf, 1, len, eeprom_fp) == len) ? 0 : -1;
}

// Interactive command handling
static int handle_command(csp_rt_t* st, const char* cmd)
{
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
	printf("Commands:\n");
	printf("  /help, /?      Show this help\n");
	printf("  /list          List declarations\n");
	printf("  /rules         List rules\n");
	printf("  /state         Show current state\n");
	printf("  /save          Save to %s\n", eeprom_file);
	printf("  /load          Load from %s\n", eeprom_file);
	printf("  /reset         Reset to initial values\n");
	printf("  /commit        Commit new values\n");	
	printf("  /quit, /exit   Exit interactive mode\n");
	printf("\n");
	printf("Declarations:\n");
	printf("  #variable X integer [= value]\n");
	printf("  #constant PI float = 3.14159\n");
	printf("  #digital LED out 13\n");
	printf("\n");
	printf("Rules:\n");
	printf("  #X = Y + 1          (always)\n");
	printf("  #X = Y + 1 ? cond   (conditional)\n");
	printf("\n");
	printf("Immediate:\n");
	printf("  X                   Print value of X\n");
	printf("  X + 1               Evaluate and print\n");
	printf("  X = 5               Assign value to X\n");
	return 0;
    }
    else if (strcmp(cmd, "list") == 0) {
	csp_list_declarations(stdout, st);
	return 0;
    }
    else if (strcmp(cmd, "state") == 0) {
	csp_dump_state_erl(stdout, st);
	return 0;
    }
    else if (strcmp(cmd, "save") == 0) {
	if (csp_eeprom_save(st) < 0) {
	    printf("Error: cannot save to %s\n", eeprom_file);
	    return -1;
	}
	printf("Saved to %s (%d decls, %d instrs, %d bytes)\n",
	       eeprom_file, st->ps.nd, st->ps.nn, csp_eeprom_size(st));
	return 0;
    }
    else if (strcmp(cmd, "load") == 0) {
	if (csp_eeprom_load(st) < 0) {
	    printf("Error: cannot load from %s\n", eeprom_file);
	    return -1;
	}
	csp_setup(st);
	printf("Loaded from %s (%d decls, %d instrs)\n",
	       eeprom_file, st->ps.nd, st->ps.nn);
	return 0;
    }
    else if (strcmp(cmd, "reset") == 0) {
	csp_rt_start(st);
	csp_setup(st);
	printf("Reset to initial values\n");
	return 0;
    }
    else if (strcmp(cmd, "commit") == 0) {
	csp_commit(st);
	return 0;
    }    
    else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
	return 1;  // signal exit
    }
    else if (strcmp(cmd, "rules") == 0) {
	// Just show instruction count for now
	printf("Instructions: %d\n", st->ps.nn);
	return 0;
    }
    else {
	printf("Unknown command: /%s (try /help)\n", cmd);
	return 0;
    }
}

// Parse and execute immediate expression
static int handle_immediate(csp_rt_t* st, char* line)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t* saved_ap;
    rentry_t result;
    
    if (csp_scan_line(line, tv, &num) < 0) {
	printf("Scan error\n");
	return -1;
    }

    if (num == 0 || tv[0].t == NEWLINE)
	return 0;

    saved_ap = st->ap;
    st->ap = NULL;  // no codegen (YET)
    if (!csp_parse_expr(st, tv, &num, &result)) {
	st->ap = saved_ap;
	printf("Parse error: %s\n", csp_format_error(st->ps.err));
	return -1;	
    }
    st->ap = saved_ap;

    if (result.I)
	csp_print_value(st, result.vt, result.val);
    else if (result.ix != BAD_INDEX)
	csp_print_value(st, result.vt, csp_value(st, result.ix));
    else
	csp_print_str("NONE");
	
    csp_println();
    return 0;
}

// Parse persistent definition (declaration or rule)
static int handle_persistent(csp_rt_t* st, char* line)
{
    // Line starts with # - parse as declaration or rule
    if (csp_parse(st, line) < 0) {
	printf("Parse error: %s\n", csp_format_error(st->ps.err));
	return -1;
    }
    // Re-initialize to apply new declarations and set values
    csp_rt_start(st);
    csp_setup(st);
    printf("OK\n");
    return 0;
}

#define SERIAL_BUF_SIZE 128
static char serial_buf[SERIAL_BUF_SIZE];
static uint8_t serial_pos = 0;
static uint8_t line_ready = 0;

void line_input(csp_rt_t* st, char c)
{
    if ((c == '\n') || (c == '\r')) {
	if (serial_pos > 0) {
	    serial_buf[serial_pos++] = '\n';
	    serial_buf[serial_pos] = '\0';
	    line_ready = 1;
	    serial_pos = 0;  // reset immediately to ignore trailing \n after \r
	}
	csp_print_char('\r');
	csp_print_char('\n');
    }
    else if (c == '\b') {
	if (serial_pos == 0)
	    csp_print_char('\a');
	else {
	    serial_pos--;
	    csp_print_char('\b');
	    csp_print_char(' ');
	    csp_print_char('\b');
	}
    }
    else if (serial_pos < SERIAL_BUF_SIZE - 1) {
	serial_buf[serial_pos++] = c;
	csp_print_char(c); // ECHO
    }
}

void serial_poll(csp_rt_t* st, struct pollfd* fds, nfds_t nfds)
{
    if (nfds > 0) {
	if (fds[0].revents & POLLIN) {
	    char c;
	    if (read(STDIN_FILENO, &c, 1) == 1)
		line_input(st, c);
	}
    }
}

void process_serial_line(csp_rt_t* st, char* line)
{
    // Skip leading whitespace
    char* p = line;
    while (*p && isspace(*p)) p++;
    if (*p == '\0')
	return;
    if (*p == '/') {
	// Command
	int r = handle_command(st, p + 1);
	if (r == 1) return;  // quit
    }
    else if (*p == '#') {
	// Persistent definition
	handle_persistent(st, p);
    }
    else {
	// Immediate expression
	handle_immediate(st, p);
    }
}

// Main interactive loop
static int interactive_loop(csp_rt_t* st)
{
    char buf[MAX_LINE_SIZE];
    int len;

    while (1) {
	len = read_line(buf, sizeof(buf), "> ");
	if (len < 0) {
	    printf("\n");
	    break;
	}
	if (len == 0)
	    continue;


    }

    return 0;
}

int csp_uconst(csp_rt_t* st, const char* name, int len,
	       value_t* ret, vtype_t* vt)
{
    printf("uconst lookup: %*s\n", len, name);
    // handle constants D0..D9
    if ((len == 2) && (name[0]=='D') &&
	(name[1]>='0') && (name[1]<='9')) {
	int d = name[1]-'0';
	ret->i = d;
	*vt = V_INTEGER;
	return 1;
    }
    else if ((len == 3) && (name[0]=='D') &&
	     (name[1]>='0') && (name[1]<='9') &&
	     (name[2]>='0') && (name[2]<='9')) {
	int d = (name[1]-'0')*10 + (name[2]-'0');
	ret->i = d;
	*vt = V_INTEGER;
	return 1;
    }
    else if ((len == 2) && (name[0]=='A') &&
	     (name[1]>='0') && (name[1]<='9')) {
	int a = name[1]-'0';
	ret->i = a;
	*vt = V_INTEGER;
	return 1;
    }
    return 0;
}


void csp_setup(csp_rt_t* st)
{
    time_init();
}

void csp_input(csp_rt_t* st)
{
    int i;
    uvalue_t now_ms;
    
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	switch(st->decl[INDEX(ix)].type) {
	case DECL_DIGITAL: break;
	case DECL_ANALOG: break;
	default: break;
	}
    }
    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; i++) {
	index_t ix = st->timer[i];
	int di = st_index(st, ix);
	if (st->decl[di].tm.running) {
	    uvalue_t t0 = csp_uvalue(st, st->decl[di].tm.tx);
	    ivalue_t period = csp_ivalue(st, st->decl[di].tm.px);
	    if ((now_ms - t0) >= period) {
		st->decl[di].tm.running = 0;
		csp_set_ivalue(st, ix, 0);
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
		if (st->reactive) {
		    csp_enq_elist(st, ix);
		}
#endif
	    }
	    break;
	}
    }    
}

void csp_output(csp_rt_t* st)
{
    int i;
    uint32_t now_ms;
    uint32_t wait_ms = NOTIMEOUT;
    
    for (i = 0; i < st->no; ++i) {
	index_t ix = st->output[i];
	switch(st->decl[INDEX(ix)].type) {
	case DECL_DIGITAL: break;
	case DECL_ANALOG: break;
	default: break;
	}
    }
    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; ++i) {
	index_t ix = st->timer[i];
	int di = st_index(st, ix);
	if (st->decl[di].tm.running) {
	    uvalue_t t0 = csp_uvalue(st, st->decl[di].tm.tx);
	    ivalue_t period = csp_ivalue(st, st->decl[di].tm.px);
	    int32_t dt = (now_ms - t0);
	    if (dt > period)
		wait_ms = 0;
	    else
		wait_ms = period - dt;
	}
	else {
	    if (st->din[di].i) { // should be started
		ivalue_t period = csp_ivalue(st, st->decl[di].tm.px);
		uint32_t dt = period;
		int k = st_index(st, st->decl[di].tm.tx);
		st->decl[di].tm.running = 1;
		 st->dout[k].u = now_ms;
		st->dout[di].i = 0;  // not timeout
		if (dt < wait_ms)
		    wait_ms = dt;
	    }
	}
    }
    st->wait_ms = wait_ms;
}

int parse_file(csp_rt_t* st, FILE* fin)
{
    char buf[MAX_LINE_SIZE];
    st->ps.line = 1;
    while(fgets(buf, MAX_LINE_SIZE, fin)) {
	if (debug_scan) {
	    token_t tv[MAX_LINE_TOKENS];
	    size_t num = MAX_LINE_TOKENS;
	    int n;
	    if ((n = csp_scan_line(buf, tv, &num)) < 0)
		return -1;
	    csp_dump_tokens(stdout, tv, num);
	}
	if (csp_parse(st, buf) < 0)
	    return -1;
    }
    csp_new_decl(st, NULL, 0, DECL_END);
    return 0;
}

void print_defines()
{
    printf("SUPPORT_TRANSACTION=%d\n", SUPPORT_TRANSACTION);
    printf("SUPPORT_REACTIVE=%d\n", SUPPORT_REACTIVE);
    printf("USE_STATISTICS=%d\n",USE_STATISTICS);

    printf("TRANSACTION_DEFAULT=%d\n", TRANSACTION_DEFAULT);
    printf("REACTIVE_DeFAULT=%d\n", REACTIVE_DEFAULT);
    printf("OP_LAST=%d\n", OP_LAST);

    printf("OBJ_BITS=%d\n", OBJ_BITS);
    printf("DECL_BITS=%d\n", DECL_BITS);
    printf("INDEX_BITS=%d\n", INDEX_BITS);
    printf("STRING_BITS=%d\n", STRING_BITS);
    printf("MAX_INDICES=%d\n", MAX_INDICES);
    printf("MAX_INSTRS=%d\n", MAX_INSTRS);
    printf("MAX_DECLS=%d\n", MAX_DECLS);
    printf("MAX_INPUTS=%d\n", MAX_INPUTS);
    printf("MAX_OUTPUTS=%d\n", MAX_OUTPUTS);
    printf("MAX_TIMERS=%d\n", MAX_TIMERS);
    printf("MAX_MODULES=%d\n", MAX_MODULES);
    printf("MAX_OBJECTS=%d\n", MAX_OBJECTS);
    printf("MAX_INDEX=%d\n", MAX_INDEX);    
    printf("MAX_STR_BUF=%d\n", MAX_STR_BUF);
    printf("MAX_STACK_DEPTH=%d\n", MAX_STACK_DEPTH);

    printf("sizeof(value_t) = %ld\n", sizeof(value_t));
    printf("sizeof(rentry_t) = %ld\n", sizeof(rentry_t));
    printf("sizeof(op_entry_t) = %ld\n", sizeof(op_entry_t));
    printf("sizeof(op_info_t) = %ld\n", sizeof(op_info_t));
    printf("sizeof(csp_func_t) = %ld\n", sizeof(csp_func_t));    
    
    printf("sizeof(csp_decl_t) = %ld\n", sizeof(csp_decl_t));
    printf("sizeof(csp_module_t) = %ld\n", sizeof(csp_module_t));
    printf("sizeof(csp_object_t) = %ld\n", sizeof(csp_object_t));
    printf("sizeof(csp_variable_t) = %ld\n", sizeof(csp_variable_t));
    printf("sizeof(csp_constant_t) = %ld\n", sizeof(csp_constant_t));
    printf("sizeof(csp_digital_t) = %ld\n", sizeof(csp_digital_t));
    printf("sizeof(csp_analog_t) = %ld\n", sizeof(csp_analog_t));
    printf("sizeof(csp_timer_t) = %ld\n", sizeof(csp_timer_t));
    printf("sizeof(csp_can_t) = %ld\n", sizeof(csp_can_t));

    printf("sizeof(csp_instr_t) = %ld\n", sizeof(csp_instr_t));
    printf("sizeof(csp_instr_alu_t) = %ld\n", sizeof(csp_instr_alu_t));    
    printf("sizeof(csp_instr_mem_t) = %ld\n", sizeof(csp_instr_mem_t));
    printf("sizeof(csp_instr_imm_t) = %ld\n", sizeof(csp_instr_imm_t));
    printf("sizeof(csp_instr_call_t) = %ld\n", sizeof(csp_instr_call_t));
    printf("sizeof(csp_instr_rule_t) = %ld\n", sizeof(csp_instr_rule_t));
    printf("sizeof(csp_instr_enter_t) = %ld\n", sizeof(csp_instr_enter_t));
    printf("sizeof(csp_instr_leave_t) = %ld\n", sizeof(csp_instr_leave_t));
    printf("sizeof(csp_instr_new_t) = %ld\n", sizeof(csp_instr_new_t));        
    
    printf("sizeof(csp_rt_t) = %ld\n", sizeof(csp_rt_t));
}


static struct option long_options[] = {
    {"debug",        no_argument,       0,  'd'},
    {"debug-parse",  no_argument,       0,  'P'},
    {"debug-scan",   no_argument,       0,  'S'},
    {"debug-trace",  no_argument,       0,  'Q'},
    {"help",         no_argument,       0,  'h'},
    {"interactive",  no_argument,       0,  'i'},
    {"transaction",  optional_argument, 0,  't'},
    {"reactive",     optional_argument, 0,  'r'},
    {"verbose",      no_argument,       0,  'v'},
    {"no-execute",   no_argument,       0,  'n'},
    {"cycles",       required_argument, 0,  'c'},
    {"timeout",      required_argument, 0,  'T'},
    {"state-file",   required_argument, 0,  's'},
    {"parse-file",   required_argument, 0,  'p'},
    {"result-file",  required_argument, 0,  'R'},
    {"compile",      no_argument,       0,  'C'},
    {"object-file",  required_argument, 0,  'O'},
    {"eeprom",       required_argument, 0,  'e'},
    {0,              0,                 0,  0 }
};

void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s [options] [file...]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -i, --interactive    Interactive mode\n");
    fprintf(stderr, "  -d, --debug          Enable debug output\n");
    fprintf(stderr, "  -t, --transaction[=B] Enable transaction mode\n");
    fprintf(stderr, "  -r, --reactive[=B]   Enable reactive mode\n");
    fprintf(stderr, "  -n, --no-execute     Parse only, don't execute\n");
    fprintf(stderr, "  -C, --compile        Compile to object code\n");
    fprintf(stderr, "  -c, --cycles=N       Max cycles (0=unlimited)\n");
    fprintf(stderr, "  -T, --timeout=MS     Max runtime in ms (0=unlimited)\n");
    fprintf(stderr, "  -P, --debug-parse    Enable parser debugging\n");
    fprintf(stderr, "  -S, --debug-scan     Enable tokenizer debugging\n");
    fprintf(stderr, "  -Q, --debug-trace    Enable variable tracing\n");
    fprintf(stderr, "  -s, --state-file=F   Write state to file (Erlang format)\n");
    fprintf(stderr, "  -p, --parse-file=F   Write parsed structure to file\n");
    fprintf(stderr, "  -R, --result-file=F  Write result to file (Erlang format)\n");
    fprintf(stderr, "  -O, --object-file=F  Write compiled result to file (C code format)\n");
    fprintf(stderr, "  -e, --eeprom=F       EEPROM file for save/load (default: eeprom.db)\n");

    fprintf(stderr, "\n");
    fprintf(stderr, "If no file is given, reads from stdin.\n");
    fprintf(stderr, "In interactive mode (-i), type /help for commands.\n");
}

int main(int argc, char** argv)
{
    csp_rt_t state;
    int r;
    index_t x;
    FILE* fin = stdin;
    FILE* state_file = NULL;
    FILE* parse_out = NULL;
    FILE* result_file = NULL;
    FILE* object_file = NULL;
    int execute = 1;
    int interactive = 0;
    uint32_t max_cycles = 0;
    uint32_t max_time_ms = 0;
    uint32_t start_time;
    int c;
    int transaction = TRANSACTION_DEFAULT;
    int reactive = REACTIVE_DEFAULT;
    int compile = 0;
    struct pollfd pfd[1];
    nfds_t nfds = 0;
    

    while (1) {
	int option_index = 0;
	c = getopt_long(argc, argv, "hindPQSCc:T:s:p:R:r:t:O:e:",
			long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	case 'h':
	    usage(argv[0]);
	    exit(0);
	case 'i': interactive = 1; break;
	case 'e': eeprom_file = optarg; break;
	case 'r': reactive =  atoi(optarg); break;
	case 't': transaction = atoi(optarg); break;
	case 'n': execute = 0; break;
	case 'C': compile = 1; break;
	case 'c': max_cycles = atoi(optarg); break;
	case 'T': max_time_ms = atoi(optarg); break;
	case 'd': debug = 1; break;
	case 'P': debug_parse = 1; break;
	case 'Q': debug_trace = 1; break;
	case 'S': debug_scan = 1; break;
	case 's':
	    if ((state_file = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open state file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'p':
	    if ((parse_out = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open parse file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'R':
	    if ((result_file = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open result file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'O':
	    if ((object_file = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open object file '%s'\n", optarg);
		exit(1);
	    }
	    break;	    
	case '?':
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }    

    if (debug) {
	print_defines();
	printf("transaction=%d\n", transaction);
	printf("reactive=%d\n", reactive);
	printf("execute=%d\n", execute);	
    }
#if !defined(SUPPORT_TRANSACTION) || (SUPPORT_TRANSACTION==0)
    if (transaction) {
	fprintf(stderr, "transaction mode not configured\n");
	exit(1);
    }
#endif
    
#if !defined(SUPPORT_REACTIVE) || (SUPPORT_REACTIVE==0)
    if (reactive) {
	fprintf(stderr, "reactive mode not configured\n");
	exit(1);
    }
#endif

    csp_rt_init(&state, transaction, reactive);
    csp_set_uconst(&state, csp_uconst);

    // Parse input files (if any)
    if (optind < argc) {
	while (optind < argc) {
	    if ((fin = fopen(argv[optind], "r")) == NULL) {
		fprintf(stderr, "unable to open file '%s'\n", argv[optind]);
		exit(1);
	    }
	    if ((r = parse_file(&state, fin)) < 0) {
		fprintf(stderr, "%s:%d syntax error\n",
			argv[optind], state.ps.line);
		exit(1);
	    }
	    fclose(fin);
	    optind++;
	}
    }
    else if (!interactive) {
	// no files given, read from stdin (unless interactive)
	if ((r = parse_file(&state, stdin)) < 0) {
	    fprintf(stderr, "*stdin*:%d %s\n",
		    state.ps.line, csp_format_error(state.ps.err));
	    exit(1);
	}
    }

    if (state.reactive)
	csp_csr(&state); // build graph

    // initialize time before starting timers
    time_init();

    // setup all input/output/timers..
    csp_rt_start(&state);

    // initialize input/output/timers ... load default values
    csp_setup(&state);

    if (debug_parse) {
	csp_dump(stdout, &state);
    }
    if (parse_out) {
	csp_dump(parse_out, &state);
    }
    if (compile) {
	FILE* objf = object_file == NULL ? stdout : object_file;
	csp_dump_code(objf, &state);
    }

    if (!execute) {
	if (parse_out) fclose(parse_out);
	if (state_file) fclose(state_file);
	if (result_file) fclose(result_file);
	if (object_file) fclose(object_file);	
	exit(0);
    }

    // Interactive mode
    if (interactive) {
	if (isatty(STDIN_FILENO)) {
	    enable_raw_mode();
	}
	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	nfds = 1;

	printf("CandySpeak Interactive Mode\n");
	printf("Type /help for commands, /quit to exit\n\n");		
    }

    start_time = csp_time_ms();

    if (debug_trace)
	csp_dump_variables(stdout, &state);
    if (state_file)
	csp_dump_state_erl(state_file, &state);

    // inital poll
    if (nfds > 0)
	poll(pfd, nfds, 0);

loop:
    if (state.cycle)
	csp_commit(&state);  // always commit before next cycle
    else if (state.reactive) {
	// Initial cycle: run full eval to prime the system
	x = csp_eval(&state);
    }	
    // check limits
    if (max_cycles && state.cycle >= max_cycles) {
	fprintf(stderr, "max cycles (%u) reached\n", max_cycles);
	goto done;
    }
    if (max_time_ms && (csp_time_ms() - start_time) >= max_time_ms) {
	fprintf(stderr, "timeout (%u ms) reached\n", max_time_ms);
	goto done;
    }

    serial_poll(&state, pfd, nfds);

    if (line_ready) {
	process_serial_line(&state, serial_buf);
	line_ready = 0;
	serial_pos = 0;
    }

    csp_input(&state);

    if (state.reactive)
	x = csp_react(&state);
    else
	x = csp_eval(&state);
    
    csp_output(&state);

    if (state.anyd) {
	if (debug_trace)
	    csp_dump_variables(stdout, &state);
	if (state_file)
	    csp_dump_state_erl(state_file, &state);
    }

    if (state.wait_ms != NOTIMEOUT) {
        // use smaller delays to stay responsive to serial
        tick_t remaining = 1000*state.wait_ms;
	tick_t t0 = time_tick();
        while ((remaining > 0) && !line_ready) {
	    tick_t t1;
	    int r = poll(pfd, nfds, state.wait_ms);
	    if (r > 0) goto loop;
	    t1 = time_tick();
	    remaining = (t1-t0);
	}
    }
    if (state.anyd)
	goto loop;

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    // in reactive mode, continue if queue has items (e.g. from timeout)
    if (state.reactive && (state.hd != state.tl))
	goto loop;
#endif

done:

    fprintf(stdout, "cycle=%d\n", state.cycle);
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    fprintf(stdout, "num_eval_rule=%d\n", state.num_eval_rule);
    fprintf(stdout, "num_eval0=%d\n", state.num_eval0);
#endif
    if (x == BAD_INDEX)
	fprintf(stdout, "result=none\n");
    else
	fprintf(stdout, "result=%d\n", csp_ivalue(&state, x));

    if (result_file) {
	csp_dump_result_erl(result_file, &state, x);
	fclose(result_file);
    }
    if (state_file)
	fclose(state_file);
    if (parse_out)
	fclose(parse_out);

    if (interactive)
	disable_raw_mode();
    exit(0);
}
