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
	switch(st->decl[ix].type) {
	case DECL_DIGITAL: break;
	case DECL_ANALOG: break;
	default: break;
	}
    }
    now_ms = csp_time_ms();
    for (i = 0; i< st->nt; i++) {
	index_t ix = st->timer[i];
	if (st->decl[INDEX(ix)].tm.running) {
	    uvalue_t t0 = csp_uvalue(st, st->decl[ix].tm.tx);
	    ivalue_t period = csp_ivalue(st, st->decl[ix].tm.px);
	    if ((now_ms - t0) >= period) {
		st->decl[ix].tm.running = 0;
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
	switch(st->decl[ix].type) {
	case DECL_DIGITAL: break;
	case DECL_ANALOG: break;
	default: break;
	}
    }
    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; ++i) {
	index_t ix = st->timer[i];
	int j = st_index(st, ix);	
	if (st->decl[j].tm.running) {
	    uvalue_t t0 = csp_uvalue(st, st->decl[j].tm.tx);
	    ivalue_t period = csp_ivalue(st, st->decl[j].tm.px);
	    int32_t dt = (now_ms-t0);
	    if (dt > period)
		wait_ms = 0;
	    else
		wait_ms = period - dt;
	}
	else {
	    if (st->dval[j].i) {
		ivalue_t period = csp_ivalue(st, st->decl[j].tm.px);
		uint32_t dt = period;
		int k = st_index(st, st->decl[j].tm.tx);
		st->decl[j].tm.running = 1;
		st->dval[k].u = now_ms;
		st->dval[j].i = 0;  // not timeout
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
    st->line = 1;
    while(fgets(buf, MAX_LINE_SIZE, fin)) {
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
    printf("MAX_STR_BUF=%d\n", MAX_STR_BUF);    
    printf("sizeof(value_t) = %ld\n", sizeof(value_t));
    printf("sizeof(csp_instr_t) = %ld\n", sizeof(csp_instr_t));
    printf("sizeof(csp_decl_t) = %ld\n", sizeof(csp_decl_t));
    printf("sizeof(csp_rt_t) = %ld\n", sizeof(csp_rt_t));
}


static struct option long_options[] = {
    {"debug",        no_argument,   0,  'd'},
    {"transaction",  no_argument,   0,  't'},
    {"reactive",     no_argument,   0,  'r'},
    {"verbose",      no_argument,   0,  'v'},
    {"execute",      no_argument,   0,  'n'},
    {0,              0,             0,  0 }
};

int main(int argc, char** argv)
{
    csp_rt_t state;
    int r;
    index_t x;
    FILE* fin = stdin;
    uint32_t update0 = 0;
    int execute = 1;
    int c;
    
    while (1) {
	// int this_option_optind = optind ? optind : 1;
	int option_index = 0;

	c = getopt_long(argc, argv, "rtnd",
			long_options, &option_index);
	if (c == -1)
	    break;
	
	switch (c) {
	case 0:
	    printf("option %s", long_options[option_index].name);
	    if (optarg)
		printf(" with arg %s", optarg);
	    printf("\n");
	    break;
	    
	case 'r':
	    csp_set_reactive(&state, 1);
	    break;		   
	case 't':
	    csp_set_transaction(&state, 1);
	    break;
	case 'n':
	    execute = 0;
	    break;
	case 'd':
	    debug = 1;
	    print_defines();
	    break;
	default:
	    printf("?? getopt returned character code 0%o ??\n", c);
	    break;
	}
    }

    csp_rt_init(&state);    

    
    while (optind < argc) {
	if ((fin = fopen(argv[optind], "r")) == NULL) {
	    fprintf(stderr, "unable to open file '%s'\n", argv[optind]);
	    exit(1);
	}
	if ((r = parse_file(&state, fin)) < 0) {
	    fprintf(stderr, "%s:%d syntax error\n",
		    argv[optind], state.line);
	    exit(1);
	}
	fclose(fin);
	fin = NULL;
    }
    
    printf("transaction = %d\n", state.transaction);
    printf("reactive = %d\n", state.reactive);
    printf("execute = %d\n", execute);
    
    if (state.reactive)
	csp_csr(&state); // build graph

    csp_rt_start(&state);  // setup default value for variables / constants

    csp_dump(stdout, &state);

    if (!execute)
	exit(0);
loop:
    csp_input(&state);

    if (state.reactive) {
	x = csp_react(&state);
    }
    else {
	x = csp_eval(&state);
    }
    csp_output(&state);
    
    if (state.wait_ms != NOTIMEOUT) {
	printf("wait for %d ms\n", state.wait_ms);
	usleep(1000*state.wait_ms);
	goto loop;
    }
    if (state.update != update0) {
	update0 = state.update;
	goto loop;
    }
    csp_dump(stdout, &state);

    fprintf(stdout, "cycle=%d\n", state.cycle);    
#ifdef WANT_STATISTICS
    fprintf(stdout, "num_eval0=%d\n", state.num_eval0);
#endif
    if (x == BAD_INDEX)
	fprintf(stdout, "result=none\n");
    else
	fprintf(stdout, "result=%d\n", csp_ivalue(&state, x));
    exit(0);
}
