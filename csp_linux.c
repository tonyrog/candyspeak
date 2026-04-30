// linux main

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "csp.h"
#include "csp_dump.h"

#include <sys/time.h>

#define MAX_LINE_SIZE 128

typedef uint64_t tick_t;
struct timeval boot_time;
int debug = 0;
int debug_scan = 0;
int debug_parse = 0;
int debug_trace = 0;

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

int csp_print_float(fvalue_t v)
{
    return printf("%f", v);
}

int csp_print_hex(uvalue_t v)
{
    return printf("0x%x", v);
}

int csp_println(void)
{
    return putchar('\n');
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
	    tokval_t val[MAX_LINE_TOKENS];
	    tok_t tok[MAX_LINE_TOKENS];
	    size_t num = MAX_LINE_TOKENS;
	    int n;
	    if ((n = csp_scan_line(buf, tok, val, &num)) < 0)
		return -1;
	    csp_dump_tokens(stdout, tok, val, num);
	}
	if (csp_parse(st, buf) < 0)
	    return -1;
    }
    csp_new_decl(st, NULL, 0, DECL_END);
    return 0;
}

void print_defines()
{
    printf("WANT_TRANSACTION=%d\n", WANT_TRANSACTION);
    printf("WANT_REACTIVE=%d\n", WANT_REACTIVE);
    printf("WANT_STATISTICS=%d\n",WANT_STATISTICS);

    printf("MAX_INDICES=%d\n", MAX_INDICES);    
    printf("MAX_INSTRS=%d\n", MAX_INSTRS);
    printf("MAX_DECLS=%d\n", MAX_DECLS);    
    printf("MAX_INPUTS=%d\n", MAX_INPUTS);
    printf("MAX_OUTPUTS=%d\n", MAX_OUTPUTS);
    printf("MAX_TIMERS=%d\n", MAX_TIMERS);
    printf("MAX_MODULES=%d\n", MAX_MODULES);
    printf("MAX_OBJECTS=%d\n", MAX_OBJECTS);
    printf("MAX_STR_BUF=%d\n", MAX_STR_BUF);    
    printf("sizeof(value_t) = %ld\n", sizeof(value_t));
    printf("sizeof(csp_instr_t) = %ld\n", sizeof(csp_instr_t));
    printf("sizeof(csp_decl_t) = %ld\n", sizeof(csp_decl_t));
    printf("sizeof(csp_rt_t) = %ld\n", sizeof(csp_rt_t));
}


static struct option long_options[] = {
    {"debug",        no_argument,       0,  'd'},
    {"debug-parse",  no_argument,       0,  'P'},
    {"debug-scan",  no_argument,        0,  'S'},
    {"debug-trace",  no_argument,       0,  'Q'},        
    {"help",         no_argument,       0,  'h'},
    {"transaction",  no_argument,       0,  't'},
    {"reactive",     no_argument,       0,  'r'},
    {"verbose",      no_argument,       0,  'v'},
    {"no-execute",   no_argument,       0,  'n'},
    {"cycles",       required_argument, 0,  'c'},
    {"timeout",      required_argument, 0,  'T'},
    {0,              0,                 0,  0 }
};

void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s [options] [file...]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -d, --debug          Enable debug output\n");
    fprintf(stderr, "  -t, --transaction    Enable transaction mode\n");
    fprintf(stderr, "  -r, --reactive       Enable reactive mode\n");
    fprintf(stderr, "  -n, --no-execute     Parse only, don't execute\n");
    fprintf(stderr, "  -c, --cycles N       Max cycles (0=unlimited)\n");
    fprintf(stderr, "  -T, --timeout MS     Max runtime in ms (0=unlimited)\n");
    fprintf(stderr, "  -P, --debug-parse    Enable parser debugging\n");
    fprintf(stderr, "  -S, --debug-scan     Enable tokenizer debugging\n");
    fprintf(stderr, "  -Q, --debug-trace    Enable variable tracing\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no file is given, reads from stdin.\n");
}

int main(int argc, char** argv)
{
    csp_rt_t state;
    int r;
    index_t x;
    FILE* fin = stdin;
    int execute = 1;
    uint32_t max_cycles = 0;
    uint32_t max_time_ms = 0;
    uint32_t start_time;
    int c;
    int transaction = 0;
    int reactive = 0;

    while (1) {
	int option_index = 0;
	c = getopt_long(argc, argv, "hrtndPQSc:T:",
			long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	case 'h':
	    usage(argv[0]);
	    exit(0);
	case 'r': reactive = 1; break;
	case 't': transaction = 1; break;
	case 'n': execute = 0; break;
	case 'c': max_cycles = atoi(optarg); break;
	case 'T': max_time_ms = atoi(optarg); break;
	case 'd': debug = 1; break;
	case 'P': debug_parse = 1; break;
	case 'Q': debug_trace = 1; break;
	case 'S': debug_scan = 1; break;	    
	case '?':
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }    

    if (debug)
	print_defines();
#if !defined(WANT_TRANSACTION) || (WANT_TRANSACTION==0)
    if (transaction) {
	fprintf(stderr, "transaction mode not configured\n");
	exit(1);
    }
#endif
    
#if !defined(WANT_RECATIVE) || (WANT_RECATIVE==0)
    if (reactive) {
	fprintf(stderr, "reactive mode not configured\n");
	exit(1);
    }
#endif

    csp_rt_init(&state, transaction, reactive);

    if (optind >= argc) {
	// no files given, read from stdin
	if ((r = parse_file(&state, stdin)) < 0) {
	    fprintf(stderr, "*stdin*:%d syntax error\n", state.ps.line);
	    exit(1);
	}
    }
    else {
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

    if (state.reactive)
	csp_csr(&state); // build graph

    // setup all input/output/timers..
    csp_rt_start(&state);
    
    // initialize input/output/timers ... load default values
    csp_setup(&state);

    if (debug_parse) {
	csp_dump(stdout, &state);
    }    
    
    if (!execute)
	exit(0);

    start_time = csp_time_ms();

    if (debug_trace)
	csp_dump_variables(stdout, &state);

loop:
    // check limits
    if (max_cycles && state.cycle >= max_cycles) {
	fprintf(stderr, "max cycles (%u) reached\n", max_cycles);
	goto done;
    }
    if (max_time_ms && (csp_time_ms() - start_time) >= max_time_ms) {
	fprintf(stderr, "timeout (%u ms) reached\n", max_time_ms);
	goto done;
    }

    csp_input(&state);

    if (state.reactive) {
	x = csp_react(&state);
    }
    else {
	x = csp_eval(&state);
    }
    
    csp_output(&state);

    if (state.anyd || state.anyx) {
	if (debug_trace)
	    csp_dump_variables(stdout, &state);	
	csp_commit(&state);
	if (state.wait_ms != NOTIMEOUT) {
	    if (debug) printf("wait for %d ms\n", state.wait_ms);
	    usleep(1000*state.wait_ms);
	}
	goto loop;
    }
    if (state.wait_ms != NOTIMEOUT) {
	if (debug) printf("wait for %d ms\n", state.wait_ms);
	usleep(1000*state.wait_ms);
	goto loop;
    }
done:

    fprintf(stdout, "cycle=%d\n", state.cycle);
#if defined(WANT_STATISTICS) && (WANT_STATISTICS==1)    
    fprintf(stdout, "num_eval0=%d\n", state.num_eval0);
#endif
    if (x == BAD_INDEX)
	fprintf(stdout, "result=none\n");
    else
	fprintf(stdout, "result=%d\n", csp_ivalue(&state, x));
    exit(0);
}
