// The host harness for C generated from a .csp (utils/candyspeak_c.erl).
//
//   prog [-F stimulus] [-c cycles]
//
// Runs the program the way `csp -F` does -- the same stimulus format, the same
// virtual clock, the same stopping rule -- and prints the final value of every
// scalar as `name=value`, one a line. Comparing that with csp's own last state
// is the oracle: one .csp, interpreted and translated, must end in the same
// place.
//
// No pins. As in port/csp_linux.c, an input is what the stimulus wrote and an
// output goes nowhere, so the generated file is built with -DCSP_LIB_HOST and
// skips its config/input/output walks.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "csp_lib.h"
#include "csp_lib_host.h"

#define MAX_LINE 1024

static uint32_t vclock = 0;
static FILE*    fin = NULL;
static char     row[MAX_LINE];
static uint32_t row_time = 0;
static int      row_applied = 1;      // the loaded row has been applied
static int      input_done = 0;

void csp_lib_putc(char c)
{
    putchar(c);
}

static const csp_lib_name_t* lookup(const char* name, size_t len)
{
    const csp_lib_name_t* p;

    for (p = csp_lib_names; p->name; p++)
	if ((strlen(p->name) == len) && (strncmp(p->name, name, len) == 0))
	    return p;
    return NULL;
}

// `<time> name=value ...`. A name the program does not have is skipped, as
// csp skips it.
static void apply(char* s)
{
    while (*s) {
	char* name;
	size_t len;
	const csp_lib_name_t* p;

	while (isspace((unsigned char)*s)) s++;
	name = s;
	while (*s && (isalnum((unsigned char)*s) || (*s == '_') || (*s == '.')))
	    s++;
	len = (size_t)(s - name);
	if ((len == 0) || (*s != '=')) {
	    while (*s && !isspace((unsigned char)*s)) s++;
	    continue;
	}
	s++;
	p = lookup(name, len);
	if (p)
	    p->set((int32_t)strtol(s, &s, 0));
	else
	    (void)strtol(s, &s, 0);
    }
}

// The next row with a time on it; comments and blank lines are not rows.
static int load_row(void)
{
    while (fgets(row, sizeof(row), fin)) {
	char* s = row;
	while (isspace((unsigned char)*s)) s++;
	if (!isdigit((unsigned char)*s))
	    continue;
	row_time = (uint32_t)strtoul(s, &s, 0);
	memmove(row, s, strlen(s) + 1);
	return 1;
    }
    return 0;
}

void csp_lib_host_input(void)
{
    if (!fin || input_done)
	return;
    if (row_applied) {
	if (!load_row()) {
	    input_done = 1;
	    return;
	}
	row_applied = 0;
    }
    if (vclock >= row_time) {
	apply(row);
	row_applied = 1;
    }
}

int main(int argc, char** argv)
{
    long cycles = -1;
    long n = 0;
    int i;
    const csp_lib_name_t* p;

    for (i = 1; i < argc; i++) {
	if ((strcmp(argv[i], "-F") == 0) && (i + 1 < argc)) {
	    if ((fin = fopen(argv[++i], "r")) == NULL) {
		perror(argv[i]);
		return 1;
	    }
	}
	else if ((strcmp(argv[i], "-c") == 0) && (i + 1 < argc))
	    cycles = strtol(argv[++i], NULL, 0);
	else {
	    fprintf(stderr, "usage: %s [-F stimulus] [-c cycles]\n", argv[0]);
	    return 1;
	}
    }
    if (!fin)
	input_done = 1;

    csp_lib_setup();
    for (;;) {
	uint32_t wait;
	uint32_t adv;
	int changed;

	// Counted from 1, and -c N stops when the count reaches N: N-1 cycles,
	// as csp -c N runs.
	n++;
	if ((cycles >= 0) && (n >= cycles))
	    break;
	changed = csp_lib_step(vclock, &wait);

	// The clock jumps to the nearest event -- the next timer or the next
	// stimulus row -- and always moves at least one tick. port/csp_linux.c.
	adv = wait;
	if (!input_done && !row_applied && (row_time > vclock)) {
	    uint32_t inp = row_time - vclock;
	    if (inp < adv) adv = inp;
	}
	if ((adv == 0xFFFFFFFFu) || (adv < 1)) adv = 1;
	vclock += adv;

	if (!input_done) continue;
	if (changed) continue;
	if (wait != 0xFFFFFFFFu) continue;
	break;
    }
    // After a marker, so the dump cannot be confused with what the program
    // printed itself.
    printf("--- state\n");
    for (p = csp_lib_names; p->name; p++)
	printf("%s=%ld\n", p->name, (long)p->get());
    return 0;
}
