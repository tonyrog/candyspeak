#define _GNU_SOURCE        // posix_openpt, ptsname, cfmakeraw
// The host harness for C generated from a .csp (utils/candyspeak_c.erl).
//
//   prog [-F stimulus] [-c cycles]
//   prog -P [-F stimulus]
//
// Runs the program the way `csp -F` does -- the same stimulus format, the same
// virtual clock, the same stopping rule -- and prints the final value of every
// scalar as `name=value`, one a line. Comparing that with csp's own last state
// is the oracle: one .csp, interpreted and translated, must end in the same
// place.
//
// -P runs it LIVE instead, for a program with a link (link.c beside the .csp,
// compiled in through port/csp_lib_prog.c): the clock is the wall clock, the
// link's bytes go over a pseudo-terminal whose name is printed first, and a
// line `Name=value ...` on stdin sets inputs as a stimulus row would. That is
// a CoCo on the desk: tools/coco_master.escript talks to the pty as to the
// board.
//
// No pins. As in port/csp_linux.c, an input is what the stimulus wrote and an
// output goes nowhere, so the generated file is built with -DCSP_LIB_HOST and
// skips its config/input/output walks.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <termios.h>
#include "csp_lib.h"
#include "csp_lib_host.h"

#define MAX_LINE 1024

static uint32_t vclock = 0;
static FILE*    fin = NULL;
static char     row[MAX_LINE];
static uint32_t row_time = 0;
static int      row_applied = 1;      // the loaded row has been applied
static int      input_done = 0;

static int      pty = -1;           // -P: the master side

void csp_lib_putc(char c)
{
    if (pty >= 0) {
	ssize_t n = write(pty, &c, 1);
	(void)n;             // a byte the other end was not there for is gone
    }
    else
	putchar(c);
}

int csp_lib_getc(void)
{
    unsigned char c;

    if ((pty >= 0) && (read(pty, &c, 1) == 1))
	return c;
    return -1;
}

// "m.q.V": the instance is everything before the last dot ("" for Main), the
// field what follows it. Sets *fp to the field number.
static const csp_lib_inst_t* lookup(const char* name, size_t len, uint16_t* fp)
{
    const csp_lib_inst_t* x;
    size_t n = len;
    size_t fl;
    const char* fn;
    uint16_t f;

    while ((n > 0) && (name[n-1] != '.'))
	n--;
    fn = name + n;
    fl = len - n;
    if (n > 0)
	n--;                        // the dot is not the instance's
    for (x = csp_lib_insts; x->name; x++) {
	if ((strlen(x->name) != n) || (strncmp(x->name, name, n) != 0))
	    continue;
	for (f = 0; f < x->mod->nfield; f++) {
	    const char* m = x->mod->field[f];
	    if ((strlen(m) == fl) && (strncmp(m, fn, fl) == 0)) {
		*fp = f;
		return x;
	    }
	}
	return NULL;
    }
    return NULL;
}

// CAN frames from the stimulus, `<time> can <id> <byte> ...`, as csp -F has
// them. A row queues the frame; csp_chip_can_recv hands it over at the next
// step's poll -- the cycle after the row, which is when the runtime's
// csp_can_recv takes it too.
#define CAN_QUEUE 16
static struct { uint32_t id; uint8_t d[8]; uint8_t n; } can_q[CAN_QUEUE];
static int can_head, can_tail;

int csp_chip_can_recv(uint32_t* id, uint8_t* d, uint8_t* len)
{
    if (can_head == can_tail)
	return 0;
    *id = can_q[can_head].id;
    memcpy(d, can_q[can_head].d, 8);
    *len = can_q[can_head].n;
    can_head = (can_head + 1) % CAN_QUEUE;
    return 1;
}

static void can_row(char* s)
{
    int next = (can_tail + 1) % CAN_QUEUE;
    uint8_t n = 0;

    if (next == can_head)
	return;                       // full: the frame is lost, as on a bus
    can_q[can_tail].id = (uint32_t)strtoul(s, &s, 0);
    memset(can_q[can_tail].d, 0, 8);
    while (n < 8) {
	char* e;
	unsigned long b = strtoul(s, &e, 0);
	if (e == s)
	    break;
	can_q[can_tail].d[n++] = (uint8_t)b;
	s = e;
    }
    can_q[can_tail].n = n;
    can_tail = next;
}

// `<time> name=value ...`. A name the program does not have is skipped, as
// csp skips it.
static void apply(char* s)
{
    while (isspace((unsigned char)*s)) s++;
    if ((strncmp(s, "can", 3) == 0) && isspace((unsigned char)s[3])) {
	can_row(s + 3);
	return;
    }
    while (*s) {
	char* name;
	size_t len;
	const csp_lib_inst_t* x;
	uint16_t f;

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
	x = lookup(name, len, &f);
	if (x) {
	    x->mod->set(x, f, (int32_t)strtol(s, &s, 0));
	    csp_lib_wake_all();      // it may be a #param or a #local
	}
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

static uint32_t wall_ms(void)
{
    static struct timespec t0;
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    if ((t0.tv_sec == 0) && (t0.tv_nsec == 0))
	t0 = t;
    return (uint32_t)((t.tv_sec - t0.tv_sec) * 1000 +
		      (t.tv_nsec - t0.tv_nsec) / 1000000);
}

// The pty is opened raw: the link's bytes are binary, and a terminal line
// discipline would turn 0x0A into two of them on the way out.
static int live(void)
{
    char line[MAX_LINE];
    size_t len = 0;
    int stdin_open = 1;
    int slave;

    if (((pty = posix_openpt(O_RDWR | O_NOCTTY)) < 0) ||
	(grantpt(pty) < 0) || (unlockpt(pty) < 0)) {
	perror("pty");
	return 1;
    }
    // One slave fd held open by us: without it the master reads EIO between
    // two runs of the tool on the other end. Raw, so the first open by a tool
    // that does not set the mode itself gets bytes as they are.
    if ((slave = open(ptsname(pty), O_RDWR | O_NOCTTY)) >= 0) {
	struct termios tio;
	if (tcgetattr(slave, &tio) == 0) {
	    cfmakeraw(&tio);
	    (void)tcsetattr(slave, TCSANOW, &tio);
	}
    }
    (void)fcntl(pty, F_SETFL, fcntl(pty, F_GETFL) | O_NONBLOCK);
    printf("pty %s\n", ptsname(pty));
    fflush(stdout);

    csp_lib_setup();
    for (;;) {
	uint32_t wait;
	struct pollfd pf[2];
	int nf = 0;

	vclock = wall_ms();
	csp_lib_poll();
	(void)csp_lib_step(vclock, &wait);

	pf[nf].fd = pty; pf[nf].events = POLLIN; nf++;
	if (stdin_open) {
	    pf[nf].fd = 0; pf[nf].events = POLLIN; nf++;
	}
	(void)poll(pf, nf, 1);
	if (stdin_open && (nf > 1) && (pf[1].revents & (POLLIN | POLLHUP))) {
	    char c;
	    if (read(0, &c, 1) != 1)
		stdin_open = 0;
	    else if (c == '\n') {
		line[len] = '\0';
		apply(line);
		len = 0;
	    }
	    else if (len + 1 < sizeof(line))
		line[len++] = c;
	}
    }
    return 0;
}

int main(int argc, char** argv)
{
    long cycles = -1;
    long max_ms = 0;                    // -T: stop when the clock reaches it
    int settle = 0;                     // --settle: no jump while it changes
    long n = 0;
    int i;
    const csp_lib_inst_t* x;
    uint16_t f;

    int live_mode = 0;

    for (i = 1; i < argc; i++) {
	if (strcmp(argv[i], "-P") == 0)
	    live_mode = 1;
	else if ((strcmp(argv[i], "-F") == 0) && (i + 1 < argc)) {
	    if ((fin = fopen(argv[++i], "r")) == NULL) {
		perror(argv[i]);
		return 1;
	    }
	}
	else if ((strcmp(argv[i], "-c") == 0) && (i + 1 < argc))
	    cycles = strtol(argv[++i], NULL, 0);
	else if ((strcmp(argv[i], "-T") == 0) && (i + 1 < argc))
	    max_ms = strtol(argv[++i], NULL, 0);
	else if (strcmp(argv[i], "--settle") == 0)
	    settle = 1;
	else {
	    fprintf(stderr, "usage: %s [-F stimulus] [-c cycles] [-T ms] [--settle]"
		    " | -P [-F stimulus]\n", argv[0]);
	    return 1;
	}
    }
    if (!fin)
	input_done = 1;
    if (live_mode)
	return live();

    csp_lib_setup();
    for (;;) {
	uint32_t wait;
	uint32_t adv;
	int changed;

	// Counted from 1, and -c N stops when the count reaches N: N-1 cycles,
	// as csp -c N runs.
	n++;
	if ((cycles > 0) && (n >= cycles))
	    break;
	if ((max_ms > 0) && (vclock >= (uint32_t)max_ms))
	    break;
	changed = csp_lib_step(vclock, &wait);

	// The clock jumps to the nearest event -- the next timer or the next
	// stimulus row -- and always moves at least one tick. port/csp_linux.c.
	adv = wait;
	if (!input_done && !row_applied && (row_time > vclock)) {
	    uint32_t inp = row_time - vclock;
	    if (inp < adv) adv = inp;
	}
	if ((adv == 0xFFFFFFFFu) || (adv < 1) || (settle && changed))
	    adv = 1;
	// Never past -T, as csp has it.
	if ((max_ms > 0) && (vclock < (uint32_t)max_ms) &&
	    (adv > (uint32_t)max_ms - vclock))
	    adv = (uint32_t)max_ms - vclock;
	vclock += adv;

	if (!input_done) continue;
	if (changed) continue;
	if (wait != 0xFFFFFFFFu) continue;
	break;
    }
    // After a marker, so the dump cannot be confused with what the program
    // printed itself.
    printf("--- state\n");
    for (x = csp_lib_insts; x->name; x++)
	for (f = 0; f < x->mod->nfield; f++)
	    printf("%s%s%s=%ld\n", x->name, x->name[0] ? "." : "",
		   x->mod->field[f], (long)x->mod->get(x, f));
    return 0;
}
