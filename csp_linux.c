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
int debug_result = 0;

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

void csp_flush(void)
{
    fflush(stdout);
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

// Platform-specific command implementations
int csp_cmd_save(csp_rt_t* st, const char* args)
{
    (void)args;
    if (csp_eeprom_save(st) < 0) {
	printf("Error: cannot save to %s\n", eeprom_file);
	return CSP_CMD_ERROR;
    }
    printf("Saved to %s (%d decls, %d instrs, %d bytes)\n",
	   eeprom_file, st->ps.nd, st->ps.nn, csp_eeprom_size(st));
    return CSP_CMD_OK;
}

int csp_cmd_load(csp_rt_t* st, const char* args)
{
    (void)args;
    if (csp_eeprom_load(st) < 0) {
	printf("Error: cannot load from %s\n", eeprom_file);
	return CSP_CMD_ERROR;
    }
    csp_setup(st);
    printf("Loaded from %s (%d decls, %d instrs)\n",
	   eeprom_file, st->ps.nd, st->ps.nn);
    return CSP_CMD_OK;
}

// Platform-specific input polling
static int quit_flag = 0;

static void serial_poll(struct pollfd* fds, nfds_t nfds)
{
    if (nfds > 0 && (fds[0].revents & POLLIN)) {
	char c;
	while (!csp_line_ready && read(STDIN_FILENO, &c, 1) == 1) {
	    if (c == 4) { // Ctrl-D
		quit_flag = 1;
		return;
	    }
	    csp_line_input(c);
	}
    }
}

void process_serial_line(csp_rt_t* st, char* line)
{
    int r = csp_process_line(st, line);
    if (r == CSP_CMD_QUIT)
	quit_flag = 1;
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
    
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	switch(st->decl[INDEX(ix)].type) {
	case DECL_DIGITAL: break;
	case DECL_ANALOG: break;
	default: break;
	}
    }
    csp_input_timer(st);
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {  // allow output
	for (i = 0; i < st->no; ++i) {
	    index_t ix = st->output[i];
	    switch(st->decl[INDEX(ix)].type) {
	    case DECL_DIGITAL: break;
	    case DECL_ANALOG: break;
	    default: break;
	    }
	}
    }
    csp_output_timer(st);
}

int parse_file(csp_rt_t* st, FILE* fin)
{
    char buf[MAX_LINE_SIZE];
    const tstr_t empty = { .ptr = NULL, .len = 0};
    
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
    csp_new_decl(st, &empty, DECL_END);
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
    {"debug-result", no_argument,       0,  'R'},
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
    fprintf(stderr, "  -c, --cycles=N       Max cycles (0=unlimited)\n");
    fprintf(stderr, "  -T, --timeout=MS     Max runtime in ms (0=unlimited)\n");
    fprintf(stderr, "  -C, --compile        Compile to object code\n");
    fprintf(stderr, "  -O, --object-file=F  Compiled result file (C code format)\n");
    fprintf(stderr, "  -P, --debug-parse    Enable parser debugging\n");
    fprintf(stderr, "  -S, --debug-scan     Enable tokenizer debugging\n");
    fprintf(stderr, "  -Q, --debug-trace    Enable variable tracing\n");
    fprintf(stderr, "  -R, --debug-result   Add result to tracing (Erl)\n");
    fprintf(stderr, "  -s, --state-file=F   State file (Erlang format)\n");
    fprintf(stderr, "  -p, --parse-file=F   Parsed structure file\n");
    fprintf(stderr, "  -e, --eeprom=F       EEPROM file for save/load (default: eeprom.db)\n");
    fprintf(stderr, "  -L[erlang|erl|text|txt]  Trace output language\n");
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
    FILE* state_file = stdout;
    FILE* parse_out = stdout;
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
    csp_lang_t lang = TEXT;    

    while (1) {
	int option_index = 0;
	c = getopt_long(argc, argv, "hindPQRSCc:T:s:p:r:t:O:e:L:",
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
	case 'R': debug_result = 1; break;
	case 'Q': debug_trace = 1; break;
	case 'S': debug_scan = 1; break;
	case 's':
	    lang = ERLANG;
	    debug_trace = 1;
	    if ((state_file = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open state file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'p':
	    debug_parse = 1;
	    if ((parse_out = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open parse file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'O':
	    if ((object_file = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open object file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'L':
	    if (strcmp(optarg, "erlang") == 0)
		lang = ERLANG;
	    else if (strcmp(optarg, "erl") == 0)
		lang = ERLANG;	    
	    else if (strcmp(optarg, "text") == 0)
		lang = TEXT;
	    else if (strcmp(optarg, "txt") == 0)
		lang = TEXT;	    
	    else {
		fprintf(stderr, "unsupported language %s\n", optarg);
		usage(argv[0]);
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
		fprintf(stderr, "%s:%d ", argv[optind], state.ps.line);
		fprintf(stderr, csp_format_error(state.ps.err),
			state.ps.err_args[0], state.ps.err_args[1], state.ps.err_args[2]);
		fprintf(stderr, "\n");
		exit(1);
	    }
	    fclose(fin);
	    optind++;
	}
    }
    else if (!interactive) {
	// no files given, read from stdin (unless interactive)
	if ((r = parse_file(&state, stdin)) < 0) {
	    fprintf(stderr, "*stdin*:%d ", state.ps.line);
	    fprintf(stderr, csp_format_error(state.ps.err),
		    state.ps.err_args[0], state.ps.err_args[1], state.ps.err_args[2]);
	    fprintf(stderr, "\n");
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
	csp_dump(parse_out, &state);
	csp_list_rules(parse_out, &state);	
    }

    if (compile) {
	FILE* objf = object_file == NULL ? stdout : object_file;
	csp_dump_code(objf, &state);
    }

    if (!execute) {
	if (parse_out != stdout) fclose(parse_out);
	if (state_file != stdout) fclose(state_file);
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
	state.latch = 1; // hold output
    }

    start_time = csp_time_ms();
    int first_cycle = 1;

    // initial trace shows cycle 0 (pre-eval state)
    if (debug_trace)
	csp_dump_state(state_file, &state, lang);

    // inital poll
    if (nfds > 0)
	poll(pfd, nfds, 0);

loop:
    if (quit_flag)
	goto done;

    if (first_cycle) {
	state.cycle = 1;
	first_cycle = 0;
    }
    else {
	state.cycle++;
    }

    if (max_cycles && state.cycle >= max_cycles) {
	fprintf(stderr, "max cycles (%u) reached\n", max_cycles);
	goto done;
    }
    if (max_time_ms && (csp_time_ms() - start_time) >= max_time_ms) {
	fprintf(stderr, "timeout (%u ms) reached\n", max_time_ms);
	goto done;
    }

    // Handle interactive input - poll and process complete lines
    if (nfds > 0) {
	if (interactive)
	    csp_line_prompt();
	int timeout_ms = interactive ? 100 : 0;
	// Wait for timer if needed (non-interactive mode)
	if (state.wait_ms != NOTIMEOUT) {
	    if (timeout_ms == 0 || state.wait_ms < (uint32_t)timeout_ms)
		timeout_ms = state.wait_ms;
	}
	poll(pfd, nfds, timeout_ms);
	serial_poll(pfd, nfds);

	if (csp_line_ready) {
	    process_serial_line(&state, csp_line_buf);
	    csp_line_ready = 0;
	    csp_line_pos = 0;
	    if (quit_flag) goto done;
	}
    }

    csp_input(&state);

    if (state.reactive)
	x = csp_react(&state);
    else
	x = csp_eval(&state);

    int anyd = state.anyd;  // save before commit clears it

    csp_commit(&state);

    csp_output(&state);

    if (anyd) {
	if (debug_trace)
	    csp_dump_state(state_file, &state, lang);
    }

    // Wait for timer in non-interactive mode
    if (!interactive && state.wait_ms != NOTIMEOUT && state.wait_ms > 0) {
	poll(NULL, 0, state.wait_ms);
    }

    // Continue loop if: interactive mode, pending changes, timers, or reactive queue
    if (interactive) goto loop;
    if (anyd) goto loop;
    if (state.wait_ms != NOTIMEOUT) goto loop;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (state.reactive && (state.hd != state.tl)) goto loop;
#endif

done:
    if (debug_result)
	csp_dump_result(state_file, &state, x, lang);
    
    if (state_file != stdout) fclose(state_file);
    if (parse_out != stdout) fclose(parse_out);

    if (interactive)
	disable_raw_mode();
    exit(0);
}
