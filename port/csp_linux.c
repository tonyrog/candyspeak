// linux main

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

// SocketCAN is a Linux kernel facility; nothing else has it.
#if defined(__linux__) && !defined(CSP_NO_SOCKETCAN)
#define CSP_HAS_SOCKETCAN 1
#endif

#include "csp.h"
#include "csp_compile.h"
#include "csp_print.h"
#include "csp_parse.h"    // stop-set budget, reported by print_defines
#include "csp_dump.h"
#include "csp_flash.h"

// Does this build have a compiler? csp_rt_init wants its state, or NULL for a
// node that only runs images -- and the tier is a driver's decision, so it is
// spelled out here rather than guessed at further down.
#if defined(CSP_EXEC_ONLY)
#define CSP_CSTATE NULL
#else
#include "csp_compile.h"
#define CSP_CSTATE csp_cstate()
#endif
#include "csp_boards.h"   // generated: make boards

#include <sys/time.h>

// Interactive mode globals
static struct termios orig_termios;
static int raw_mode = 0;
static const char* eeprom_file = "eeprom.db";

static const char* can_iface = NULL;   // --can=vcan0; NULL = no bus, stubs
static const char* src_file = NULL;   // first .csp on the command line (ROM banner)
static char src_modified[26];

// git version, injected by the Makefile; a plain build still says something.
#ifndef CSP_VERSION
#define CSP_VERSION "unknown"
#endif

#define MAX_LINE_SIZE 128

// A line of SOURCE, which is not the same thing and must not be smaller than
// what the REPL accepts: a rule you can type at the prompt has to survive being
// written to a file and read back, and /list produces exactly such a file.
// CSP_LINE_MAX is the REPL's own ceiling; the +2 is the CR and the LF.
#define MAX_SRC_LINE (CSP_LINE_MAX + 2)

// `> ...` lines met while READING a file cannot run where they are found: the
// declarations are not complete and csp_rt_start has not laid out the arena, so
// csp_process_line answers "not started -- /resume to allocate and run". They
// are held here and run after csp_setup, in source order.
//
// One bump buffer with NUL-separated lines rather than an array of fixed rows:
// the lines are short and few, and a row array would be 16K of .bss for the
// handful anyone writes.
// Parser tier only: CSP_EXEC_ONLY has no csp_process_line to run them WITH, and
// no file reader to collect them in the first place.
#if !defined(CSP_EXEC_ONLY)
#define MAX_PENDING_IMM 4096
static char pending_imm[MAX_PENDING_IMM];
static size_t pending_imm_used = 0;

// Queue one, from anywhere. --id and --name use this: an override IS an
// immediate, and going through the same path means it behaves like one --
// applied on every rebuild, visible in /settings, and UNSAVED until /save.
// That is exactly "override, do not store": nothing here touches eeprom.db.
static void queue_immediate(const char* line)
{
    size_t len = strlen(line);
    if (pending_imm_used + len + 1 <= MAX_PENDING_IMM) {
	memcpy(&pending_imm[pending_imm_used], line, len + 1);
	pending_imm_used += len + 1;
    }
}

// Run everything held, oldest first, then forget it.
static void run_pending_immediates(csp_rt_t* st)
{
    size_t p = 0;
    while (p < pending_imm_used) {
	char* line = &pending_imm[p];
	p += strlen(line) + 1;
	csp_process_line(st, line);
    }
    pending_imm_used = 0;
}
#endif /* !CSP_EXEC_ONLY */

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

#include <stdio.h>
#include <unistd.h>

// Returnerar använt fysiskt RAM i bytes för den aktuella processen
long csp_system_ram_allocated()
{
    long total_pages;
    long resident_pages;
    long page_size;
    FILE* fp;
    
    if ((fp = fopen("/proc/self/statm", "r")) == NULL)
        return -1;
    if (fscanf(fp, "%ld %ld", &total_pages, &resident_pages) != 2) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    page_size = sysconf(_SC_PAGESIZE);
    return resident_pages * page_size;
}

static unsigned long system_ram_capacity = SYSTEM_RAM_CAPACITY;

uint32_t csp_system_ram_capacity()
{
    return system_ram_capacity;
}

// What the SYSTEM takes before CandySpeak gets a look in: on a board that is
// capacity - freeRam(), i.e. the core plus every library linked in -- which is
// exactly the number that moves when you add CircuitPlayground or a CAN driver.
// The host has no such system, so -O supplies it: measure it once on the board
// (/memory reports it there) and hand it to the simulation.
//
// The linker symbols cannot answer this: &_end - &__data_start is the HOST
// process's statics (libc, stdio, ~27K), which has nothing to do with a target.
static uint32_t system_ram_used = 0;

uint32_t csp_system_ram_used()
{
    return system_ram_used;
}

uint32_t csp_system_ram_avail()
{
    return csp_system_ram_capacity() - csp_system_ram_used();
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

// virtual (simulated) time, driven by the -F input file. When enabled the
// clock is a deterministic counter advanced by the main loop (>=1 tick/cycle,
// jumping wait_ms when waiting for a timer) instead of the wall clock.
int      virtual_time = 0;
uint32_t vclock = 0;

uint32_t csp_time_ms(void)
{
    if (virtual_time)
	return vclock;
    return time_tick() / 1000;
}

unsigned long csp_time_us(void)
{
    return time_tick();
}

static FILE* file_output = NULL;

void* csp_set_file_output(void* f)
{
    FILE* prev = file_output;
    file_output = (FILE*) f;
    return prev;
}

int csp_will_output()
{
    return (file_output != NULL);
}

// platform print functions
int csp_print_char(char c)
{
    // The console tap. First thing, and NON-CONSUMING: the port still prints,
    // because a node with a terminal attached wants to see its own output. At
    // CSP_CONSOLE_BYTES == 0 this compiles to nothing.
    csp_repl_tap(c);
    if (file_output) {
	if (fputc(c, file_output) == EOF)
	    return 0;
    }
    return 1;
}

// CHARACTER BY CHARACTER, not one fprintf.
//
// csp_print_char is the node's single output point -- the console tap hangs off
// it, and every other port's csp_print_str already loops through it. This one
// shortcut meant the tap saw only what was printed a character at a time: a
// listing relayed to another node arrived as `# Out:64 R` where it should have
// read `#buffer Out:64 in repl  // R`, with every RODATA word missing and
// nothing anywhere saying so.
//
// A call per character on a host is not a cost worth a second output path.
int csp_print_str(const char* s)
{
    int n = 0;

    while (*s)
	n += csp_print_char(*s++);
    return n;
}

// A rostring is RODATA, and under CSP_RO_POISON that is a second address space
// -- so it is read a byte at a time through ro_byte, exactly as port/csp_avr.c
// has always had to. Without the poison ro_byte is a plain dereference and this
// compiles to the same loop csp_print_str would have run.
//
// Not an #ifdef: having the host and the AVR port read a rostring the SAME way
// is the point. The cast through const char* was the shortcut that only worked
// because the host has one address space.
int csp_print_rostr(rostring_t s)
{
    const uint8_t* p = (const uint8_t*)s;
    int n = 0;
    uint8_t c;

    while ((c = ro_byte(p + n)) != 0) {
	csp_print_char((char)c);
	n++;
    }
    return n;
}

void csp_flush(void)
{
    if (file_output)
	fflush(file_output);
}

// Report st's pending error through the runtime's own formatter. The host used
// to hand csp_format_error's string straight to fprintf, which only worked
// because RODATA is ordinary memory here -- the same call on AVR reads the
// wrong address space, and the two formatters could drift apart unnoticed.
// csp_print_* writes to the current sink and these belong on stderr, so the
// sink is swapped for the duration.
static void print_error(csp_rt_t* st)
{
    void* prev = csp_set_file_output(stderr);
    csp_print_error(st);
    csp_println();
    csp_set_file_output(prev);
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
    struct termios raw;
    
    if (!isatty(STDIN_FILENO))
	return -1;

    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0)
	return -1;

    atexit(disable_raw_mode);

    raw = orig_termios;
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
// Simulated EEPROM capacity in bytes (-E). 0 = unbounded, which is what a host
// file really is. Set it to a board's size to reproduce that board's ceiling.
static uint32_t eeprom_cap = 0;

const char* csp_eeprom_name(void)
{
    return eeprom_file;
}

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
    // Honour a simulated board capacity so the host can reproduce the limit a
    // real MCU imposes (a plain file has none).
    if (eeprom_cap > 0) {
	long pos = ftell(eeprom_fp);
	if ((pos >= 0) && ((uint32_t)pos + len > eeprom_cap))
	    return -1;
    }
    return (fwrite(buf, 1, len, eeprom_fp) == len) ? 0 : -1;
}

uint32_t csp_eeprom_capacity(void)
{
    // A plain file has no ceiling; -E imposes one to mimic a board.
    return eeprom_cap ? eeprom_cap : CSP_EEPROM_UNBOUNDED;
}

// Platform-specific command implementations
// Platform-specific input polling
static int quit_flag = 0;
// stdin is finished with -- EOF on a pipe, or Ctrl-D at a terminal. NOT the
// same as quitting. It ends the PROMPT, not the program: the loop drops to its
// non-interactive rules and runs the program to wherever it settles, which is
// what `./csp prog.csp` does and is the thing a person means by Ctrl-D after
// typing a program in. /quit is how you say quit.
static int stdin_gone = 0;

static void serial_poll(csp_rt_t* st, struct pollfd* fds, nfds_t nfds)
{
    if (nfds > 0 && (fds[0].revents & POLLIN)) {
	struct pollfd more = { STDIN_FILENO, POLLIN, 0 };
	char c;
	// Drain everything waiting, the same way a board drains its port -- past
	// a completed line and on into the queue behind it. The terminal is in
	// raw mode with VMIN=1, so read() BLOCKS; a zero-timeout poll each turn
	// is what keeps "take what is there" from becoming "wait for more".
	while (csp_line_space(&st->line) && csp_con_space()) {
	    ssize_t n = read(STDIN_FILENO, &c, 1);
	    if (n == 0) {
		// EOF -- the pipe's Ctrl-D, and it is treated as one.
		//
		// It must be NOTICED, whatever it then means. poll() reports a
		// closed pipe as READABLE, forever: this read returned 0, broke
		// out, and the main loop went straight round again at 100% CPU
		// until something killed it. One such process ran for two and a
		// half hours; four cases in tests/repl.sh sat here for their
		// full 20-second timeout each.
		//
		// A last line with no newline still counts: terminate it so it
		// is processed like any other.
		if (st->line.fill > 0)
		    csp_line_input(&st->line, '\n');
		stdin_gone = 1;
		return;
	    }
	    if (n != 1)
		break;
	    if (c == 4) { // Ctrl-D -- the same thing, said by hand
		stdin_gone = 1;
		return;
	    }
	    csp_con_input(st, c);
	    more.revents = 0;
	    if (poll(&more, 1, 0) <= 0)
		break;
	}
    }
}

void process_serial_line(csp_rt_t* st, char* line)
{
#if defined(CSP_EXEC_ONLY)
    // No command layer in this build (csp_repl.c compiles to nothing), so a
    // line has nowhere to go. The reader above still runs -- it is what notices
    // Ctrl-D -- it just has nothing to hand the line to.
    (void)st; (void)line;
#else
    int r = csp_process_line(st, line);
    if (r == CSP_CMD_QUIT)
	quit_flag = 1;
#endif
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

// ============================================================
// CAN backend
//
// SocketCAN when a --can interface was given, otherwise a no-op stub so a
// program with CAN declarations still parses, runs and can be inspected on a
// machine with no bus. Test with a virtual interface:
//   sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
//   ./csp --can=vcan0 prog.csp
// ============================================================

// ------------------------------------------------------------
// Stimulus-injected frames
//
// A -F row can deliver a frame:  `can <id> <b0> <b1> ...`
//
// It queues here and leaves through csp_can_recv, so it takes exactly the path
// a real bus frame takes: into the SHADOW heap, through can_mark_fields, and
// `.rx` raised by the commit that follows. Setting `.rx` from a stimulus row
// instead would skip all three -- and `.rx` is read-only anyway, because a
// frame having arrived is a fact about the bus and not a value anyone writes.
//
// Without this a program that receives CAN cannot be tested at all: the ONLY
// way in is a real interface. Both board programs under private/ had their
// command path compiled and unexercised for that reason.
// ------------------------------------------------------------
// Compiled out of an exec-only build: nothing there can fill the queue, so it
// would be a dead 150 bytes on a target that counts them.
#if !defined(CSP_EXEC_ONLY)
#define MAX_INJ_FRAMES 16

typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
} inj_frame_t;

static inj_frame_t inj_q[MAX_INJ_FRAMES];
static int inj_head = 0;         // next to read
static int inj_tail = 0;         // next to write
static int inj_dropped = 0;

// A full queue is reported once and not per frame: the interesting fact is
// that the test lost frames, not how many times it was told.
static void inj_push(uint32_t id, const uint8_t* d, uint8_t len)
{
    int next = (inj_tail + 1) % MAX_INJ_FRAMES;

    if (next == inj_head) {
	if (!inj_dropped)
	    fprintf(stderr, "stimulus: can queue full, frame 0x%x dropped\n",
		    (unsigned)id);
	inj_dropped = 1;
	return;
    }
    inj_q[inj_tail].id  = id;
    inj_q[inj_tail].len = len;
    memcpy(inj_q[inj_tail].data, d, len);
    inj_tail = next;
}

static int inj_pop(uint32_t* id, uint8_t* data, uint8_t* len)
{
    if (inj_head == inj_tail)
	return 0;
    *id  = inj_q[inj_head].id;
    *len = inj_q[inj_head].len;
    memcpy(data, inj_q[inj_head].data, *len);
    inj_head = (inj_head + 1) % MAX_INJ_FRAMES;
    return 1;
}
#else
#define inj_pop(id, data, len)  0
#endif /* !CSP_EXEC_ONLY */

// --- UDP --------------------------------------------------------------------
//
// One socket per LISTENING port, shared by every buffer that names it -- two
// `in udp 5000` buffers are two views of the same port, not two binds, and the
// second bind would fail. Outbound needs no socket of its own: it borrows any
// open one, and opens an unbound socket if the program only sends.
//
// NON-BLOCKING throughout. csp_buf_input polls once per cycle and must never
// stall it; a datagram that has not arrived is simply not there yet.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef CSP_UDP_MAXSOCK
#define CSP_UDP_MAXSOCK 4
#endif

static struct { uint16_t port; int fd; } udp_sock[CSP_UDP_MAXSOCK];
static int udp_nsock = 0;
static int udp_tx_fd = -1;

// Ports we tried to bind and could not. Kept so the message is printed once
// rather than every cycle, and so a doomed bind is not retried a hundred times
// a second for the life of the program.
static uint16_t udp_dead[CSP_UDP_MAXSOCK];
static int udp_ndead = 0;

// A BUS ADDRESS, and it is the one thing that can never be a SENDER: no
// datagram arrives from a broadcast address. So the address on an `in` buffer
// carries two disjoint meanings and needs no keyword to tell them apart --
// a host address is the peer to accept, a broadcast address names the BUS.
//
// The test is the low octet, which covers the two forms anyone writes:
// 255.255.255.255 and a /24's own broadcast (192.168.1.255, and 127.255.255.255
// for a laptop running several nodes against loopback). A host address never
// ends in .255 on a /24, so nothing legitimate is caught by it.
static int udp_is_bus(uint32_t a)
{
    return (a == 0xFFFFFFFFu) || ((a & 0xffu) == 0xffu);
}

static int udp_find(uint16_t port)
{
    int i;
    for (i = 0; i < udp_nsock; i++)
	if (udp_sock[i].port == port)
	    return udp_sock[i].fd;
    return -1;
}

static int udp_gave_up(uint16_t port)
{
    int i;
    for (i = 0; i < udp_ndead; i++)
	if (udp_dead[i] == port)
	    return 1;
    return 0;
}

static void udp_give_up(uint16_t port)
{
    if (udp_ndead < CSP_UDP_MAXSOCK)
	udp_dead[udp_ndead++] = port;
}

int csp_udp_open(csp_rt_t* st, uint16_t port)
{
    return csp_udp_open_bus(st, port, 0);
}

int csp_udp_open_bus(csp_rt_t* st, uint16_t port, int bus)
{
    struct sockaddr_in a;
    int fd, on = 1;
    (void)st;

    if (udp_find(port) >= 0)
	return 0;                      // already listening -- see above
    if (udp_gave_up(port))
	return -1;                     // said why once; not saying it again
    if (udp_nsock >= CSP_UDP_MAXSOCK)
	return -1;
    if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0)
	return -1;
    // SO_REUSEADDR ONLY ON A BUS, and the difference is the whole point.
    //
    // On UNICAST it means several processes may bind the same port and the
    // kernel hands each datagram to exactly ONE of them -- whichever it likes.
    // A forgotten csp still holding port 12345 then makes the next one bind
    // successfully, receive nothing, and report nothing. That is not a
    // hypothetical: it is what "my program does not get the datagram" turned
    // out to be, and every symptom pointed at the sender. UDP has no TIME_WAIT
    // either, so the restart case the flag is usually there for does not exist.
    //
    // On BROADCAST it means the opposite: every socket bound to the port gets a
    // COPY. That is exactly a bus, and it is the only way several nodes run on
    // one laptop. So the flag follows the declaration -- an `in udp <port>
    // <broadcast>` buffer asks for it, and nothing else gets it.
    //
    // SO_BROADCAST is unconditional: it is needed to SEND to a broadcast
    // address, and an out buffer's socket may be any of these.
    if (bus)
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
	// SAY SO, once, and on STDERR -- like the CAN errors above it. Not
	// through csp_print_*: that is the PROGRAM's output stream, it is NULL
	// until the driver opens it, and the first bind happens on the first
	// cycle. A message written there arrives nowhere, which is precisely the
	// failure this line exists to prevent.
	//
	// Once, because this is polled every cycle: unconditional would be a
	// hundred lines a second, and silence is what made the REUSEADDR bug
	// above take an evening to find.
	fprintf(stderr, "udp: cannot listen on port %u -- %s\n",
		(unsigned)port, strerror(errno));
	close(fd);
	udp_give_up(port);
	return -1;
    }
    udp_sock[udp_nsock].port = port;
    udp_sock[udp_nsock].fd = fd;
    udp_nsock++;
    if (udp_tx_fd < 0)
	udp_tx_fd = fd;
    return 0;
}

int csp_udp_recv(csp_rt_t* st, uint16_t port, uint32_t accept,
		 uint8_t* data, uint16_t* len)
{
    struct sockaddr_in a;
    socklen_t alen;
    ssize_t n;
    int fd, guard;
    uint8_t peek;
    (void)st;

    // Open on first use rather than at setup: the runtime knows which ports a
    // program wants only after it has built its buffer table, and a program
    // edited in the REPL changes that table while running.
    if ((fd = udp_find(port)) < 0) {
	if (csp_udp_open_bus(st, port, udp_is_bus(accept)) < 0)
	    return -1;
	fd = udp_find(port);
    }
    // A bus address is not a peer to match -- see udp_is_bus. Everyone on the
    // bus is welcome; who a message is FOR is the program's business, which is
    // what an id in the payload is for.
    if ((accept == 0) || udp_is_bus(accept)) {
	// No filter: one syscall, straight into the caller's buffer.
	alen = sizeof(a);
	if ((n = recvfrom(fd, data, *len, 0, (struct sockaddr*)&a, &alen)) < 0)
	    return 0;                  // EAGAIN: nothing pending
	*len = (uint16_t)n;
	return 1;
    }
    // FILTERED: PEEK THE SENDER FIRST. `data` is the buffer's own shadow, so a
    // datagram from the wrong peer must not be read into it even to be thrown
    // away -- it would overwrite the last good one with bytes nothing marks.
    // MSG_PEEK fills the address without consuming, so the decision is made
    // before anything lands.
    //
    // Bounded like the core's drain: a flood from the wrong peer must not
    // starve the right one, but it must not own the loop either.
    for (guard = 0; guard < CSP_UDP_RX_BURST; guard++) {
	alen = sizeof(a);
	memset(&a, 0, sizeof(a));
	if (recvfrom(fd, &peek, 1, MSG_PEEK, (struct sockaddr*)&a, &alen) < 0)
	    return 0;                  // EAGAIN: nothing pending
	// Stored in HOST order, which is how `1.2.3.4` and 0x01020304 both read.
	if (ntohl(a.sin_addr.s_addr) != accept) {
	    // Consume and throw away. A UDP read takes the WHOLE datagram
	    // however small the buffer, so one byte drops it -- and it has to be
	    // dropped rather than left, or it sits at the head of the queue and
	    // stalls the port behind it for good.
	    (void)recv(fd, &peek, 1, 0);
	    continue;
	}
	alen = sizeof(a);
	if ((n = recvfrom(fd, data, *len, 0, (struct sockaddr*)&a, &alen)) < 0)
	    return 0;
	*len = (uint16_t)n;
	return 1;
    }
    return 0;
}

// The listening sockets, so the loop can WAIT on a datagram instead of looking
// again every hundred milliseconds -- csp_can_pollfd's counterpart, and what a
// program whose only input is UDP needs to stop spinning.
//
// ENUMERATED rather than handed over as a set, and re-read every time round the
// loop, because a port is bound on FIRST USE: the runtime knows which ports the
// program wants only after it has built its buffer table, and the REPL changes
// that table while running. A set collected once at start would be empty.
//
// slot 0, 1, 2... in open order; -1 past the end.
int csp_udp_pollfd(int slot)
{
    if ((slot < 0) || (slot >= udp_nsock))
	return -1;
    return udp_sock[slot].fd;
}

int csp_udp_send(csp_rt_t* st, uint32_t addr, uint16_t port,
		 const uint8_t* data, uint16_t len)
{
    struct sockaddr_in a;
    (void)st;

    if (udp_tx_fd < 0) {
	int on = 1;
	if ((udp_tx_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0)
	    return -1;
	setsockopt(udp_tx_fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    // The address is stored in HOST order -- `udp 0xC0A80102` reads as
    // 192.168.1.2 in the source and has to mean that on a little-endian host
    // too, so the conversion is here and not in the declaration.
    a.sin_addr.s_addr = htonl(addr);
    a.sin_port = htons(port);
    if (sendto(udp_tx_fd, data, len, 0, (struct sockaddr*)&a, sizeof(a)) < 0)
	return -1;
    return 0;
}

// --- UART --------------------------------------------------------------------
//
// A host has no second serial port, so `--uart=DEV` gives it one: a tty, a pty
// from socat, or anything else that reads and writes bytes. Unit 0 unless the
// option says otherwise (`--uart=2:/dev/ttyUSB0`).
//
// The BAUD in the declaration is applied when the device is a real tty and
// ignored when it is not -- a pty has no baud, and refusing to run on one would
// make the transport untestable without hardware.
#include <termios.h>

#ifndef CSP_UART_MAXPORT
#define CSP_UART_MAXPORT 2
#endif

static struct { int unit; const char* dev; int fd; } uart_port[CSP_UART_MAXPORT];
static int uart_nport = 0;

// --uart=[<unit>:]<device>
static void uart_add(const char* spec)
{
    const char* colon = strchr(spec, ':');
    int unit = 0;

    if (uart_nport >= CSP_UART_MAXPORT) {
	fprintf(stderr, "uart: no room for '%s'\n", spec);
	return;
    }
    if (colon != NULL) {
	unit = atoi(spec);
	spec = colon + 1;
    }
    uart_port[uart_nport].unit = unit;
    uart_port[uart_nport].dev  = spec;
    uart_port[uart_nport].fd   = -1;
    uart_nport++;
}

static int uart_find(uint32_t xref)
{
    unsigned unit = TR_UART_UNIT(xref);
    int i;

    for (i = 0; i < uart_nport; i++)
	if ((unsigned)uart_port[i].unit == unit)
	    return i;
    return -1;
}

int csp_uart_open(csp_rt_t* st, uint32_t xref)
{
    int i = uart_find(xref);
    (void)st;

    if (i < 0)
	return -1;                     // no --uart for this unit
    if (uart_port[i].fd >= 0)
	return 0;
    if ((uart_port[i].fd = open(uart_port[i].dev,
				O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
	// Once, on stderr, like every other endpoint that cannot be opened.
	fprintf(stderr, "uart: cannot open %s -- %s\n",
		uart_port[i].dev, strerror(errno));
	uart_port[i].unit = -1;        // and not once per cycle after that
	return -1;
    }
    if (isatty(uart_port[i].fd)) {
	struct termios tio;
	if (tcgetattr(uart_port[i].fd, &tio) == 0) {
	    // RAW. A UART carries BYTES, and any line discipline here would
	    // show up as a relayed listing with its newlines rewritten.
	    cfmakeraw(&tio);
	    if (TR_UART_BAUD(xref) != 0) {
		speed_t sp = B0;
		switch (TR_UART_BAUD(xref)) {
		case 9600:   sp = B9600; break;
		case 19200:  sp = B19200; break;
		case 38400:  sp = B38400; break;
		case 57600:  sp = B57600; break;
		case 115200: sp = B115200; break;
		default:     sp = B0; break;
		}
		if (sp != B0) {
		    cfsetispeed(&tio, sp);
		    cfsetospeed(&tio, sp);
		}
	    }
	    tcsetattr(uart_port[i].fd, TCSANOW, &tio);
	}
    }
    return 0;
}

int csp_uart_recv(csp_rt_t* st, uint32_t xref, uint8_t* data, uint16_t* len)
{
    int i;
    ssize_t n;

    if (csp_uart_open(st, xref) < 0)
	return 0;                      // no port: QUIET, not an error -- a wire
    i = uart_find(xref);               // with nobody on it looks the same
    if ((i < 0) || (uart_port[i].fd < 0))
	return 0;
    if ((n = read(uart_port[i].fd, data, *len)) > 0) {
	*len = (uint16_t)n;
	return 1;
    }
    return 0;
}

int csp_uart_send(csp_rt_t* st, uint32_t xref, const uint8_t* data, uint16_t len)
{
    int i;

    if (csp_uart_open(st, xref) < 0)
	return -1;
    i = uart_find(xref);
    if ((i < 0) || (uart_port[i].fd < 0))
	return -1;
    if (write(uart_port[i].fd, data, len) != (ssize_t)len)
	return -1;                     // the caller keeps it and retries
    return 0;
}

// For the loop to wait on, like the other two.
int csp_uart_pollfd(int slot)
{
    int i, k = 0;

    for (i = 0; i < uart_nport; i++)
	if (uart_port[i].fd >= 0) {
	    if (k++ == slot) return uart_port[i].fd;
	}
    return -1;
}

// --- TCP ---------------------------------------------------------------------
//
// UDP's surface with a connection under it. What that costs is a state machine
// per port, and it is small because of two decisions:
//
//   ONE CONNECTION per listening port. Tony's shape is one master and one node
//   at a time, so a second caller waits in the backlog rather than being
//   multiplexed. Accepting it and dropping the first would be worse: the peer
//   that was working goes quiet with nothing said.
//
//   NOTHING BLOCKS, ever. Listen, accept, connect and read are all
//   non-blocking, so "the peer is not up yet" is the same nothing a quiet bus
//   gives -- 0 from recv, -1 from send -- and the program keeps cycling. A
//   connect that has not completed is retried, not waited on.
//
// EOF closes and goes back to listening. A peer that reconnects gets served
// again with no intervention, which is what makes a remote console survive
// restarting the other end.
#include <netinet/tcp.h>

#ifndef CSP_TCP_MAXSOCK
#define CSP_TCP_MAXSOCK 4
#endif

typedef struct {
    uint16_t port;
    int      lfd;                  // listening, for an `in` buffer
    int      cfd;                  // the one connection, either direction
    uint32_t peer;                 // who is on it (host order), 0 = nobody
    uint8_t  outbound;             // 1 = we dialled, so a drop means redial
} tcp_sock_t;

static tcp_sock_t tcp_sock[CSP_TCP_MAXSOCK];
static int tcp_nsock = 0;

static tcp_sock_t* tcp_find(uint16_t port, int make)
{
    int i;

    for (i = 0; i < tcp_nsock; i++)
	if (tcp_sock[i].port == port)
	    return &tcp_sock[i];
    if (!make || (tcp_nsock >= CSP_TCP_MAXSOCK))
	return NULL;
    tcp_sock[tcp_nsock].port = port;
    tcp_sock[tcp_nsock].lfd = -1;
    tcp_sock[tcp_nsock].cfd = -1;
    tcp_sock[tcp_nsock].peer = 0;
    tcp_sock[tcp_nsock].outbound = 0;
    return &tcp_sock[tcp_nsock++];
}

static void tcp_drop(tcp_sock_t* s)
{
    if (s->cfd >= 0)
	close(s->cfd);
    s->cfd = -1;
    s->peer = 0;
}

// The listening side. SO_REUSEADDR belongs here and not on UDP: on TCP it means
// "reuse a port still in TIME_WAIT", which is the ordinary restart case, and it
// does NOT let a second process steal the traffic the way it does on UDP.
static int tcp_listen(tcp_sock_t* s)
{
    struct sockaddr_in a;
    int on = 1;

    if (s->lfd >= 0)
	return 0;
    if ((s->lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
	return -1;
    setsockopt(s->lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(s->port);
    if ((bind(s->lfd, (struct sockaddr*)&a, sizeof(a)) < 0) ||
	(listen(s->lfd, 1) < 0)) {
	// Said once, like the UDP bind, and on stderr for the same reason: this
	// is polled every cycle and the program's own stream may not exist yet.
	fprintf(stderr, "tcp: cannot listen on port %u -- %s\n",
		(unsigned)s->port, strerror(errno));
	close(s->lfd);
	s->lfd = -1;
	return -1;
    }
    return 0;
}

// accept_ip, not `accept`: the parameter would shadow the socket call two
// lines down, and the compiler's word for that is not obvious.
int csp_tcp_recv(csp_rt_t* st, uint16_t port, uint32_t accept_ip,
		 uint8_t* data, uint16_t* len)
{
    tcp_sock_t* s = tcp_find(port, 1);
    ssize_t n;
    (void)st;

    if (s == NULL)
	return -1;
    if ((s->cfd < 0) && !s->outbound) {
	struct sockaddr_in a;
	socklen_t alen = sizeof(a);
	int fd;
	if (tcp_listen(s) < 0)
	    return -1;
	memset(&a, 0, sizeof(a));
	// accept, not accept4: accept4 wants _GNU_SOURCE, and this file defines
	// no feature-test macros -- one fcntl is cheaper than that dependency.
	if ((fd = accept(s->lfd, (struct sockaddr*)&a, &alen)) < 0)
	    return 0;                  // nobody calling yet
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	// THE SAME FILTER UDP HAS, and the same reason it is here rather than in
	// the core: a peer we will not talk to must not get a connection at all,
	// let alone have its bytes read into the buffer's shadow.
	if ((accept_ip != 0) && (ntohl(a.sin_addr.s_addr) != accept_ip)) {
	    close(fd);
	    return 0;
	}
	s->cfd = fd;
	s->peer = ntohl(a.sin_addr.s_addr);
    }
    if (s->cfd < 0)
	return 0;
    if ((n = recv(s->cfd, data, *len, 0)) > 0) {
	*len = (uint16_t)n;
	return 1;
    }
    // 0 is EOF -- the peer hung up. Anything else that is not EAGAIN is the
    // connection failing. Both mean the same here: drop it and go back to
    // listening, so a peer that restarts is served again with no help.
    if ((n == 0) || ((n < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)))
	tcp_drop(s);
    return 0;
}

int csp_tcp_send(csp_rt_t* st, uint32_t addr, uint16_t port,
		 const uint8_t* data, uint16_t len)
{
    tcp_sock_t* s = tcp_find(port, 1);
    ssize_t n;
    (void)st;

    if ((s == NULL) || (addr == 0))
	return -1;                     // an out buffer with no destination
    if (s->cfd < 0) {
	struct sockaddr_in a;
	int fd;
	s->outbound = 1;
	if ((fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
	    return -1;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(addr);
	a.sin_port = htons(port);
	// A non-blocking connect answers EINPROGRESS and finishes later. Rather
	// than track that as a third state, the socket is kept and the WRITE
	// below is what discovers whether it landed: a write on a half-open
	// socket fails, we drop it, and the next cycle dials again. One retry
	// per cycle is fast enough for a console and costs no state.
	if ((connect(fd, (struct sockaddr*)&a, sizeof(a)) < 0) &&
	    (errno != EINPROGRESS)) {
	    close(fd);
	    return -1;
	}
	s->cfd = fd;
	s->peer = addr;
	return -1;                     // nothing written yet; try next cycle
    }
    if (len == 0)
	return 0;                      // a dial check, not a write
    if ((n = send(s->cfd, data, len, MSG_NOSIGNAL)) == (ssize_t)len)
	return 0;
    if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
	return -1;                     // the window is full: the rest waits
    tcp_drop(s);
    return -1;
}

// The connections, for the loop to wait on -- csp_udp_pollfd's counterpart.
// Both ends are here: a listening socket becomes readable when someone calls,
// and a connection when bytes arrive.
int csp_tcp_pollfd(int slot)
{
    int i, k = 0;

    for (i = 0; i < tcp_nsock; i++) {
	if (tcp_sock[i].lfd >= 0) {
	    if (k++ == slot) return tcp_sock[i].lfd;
	}
	if (tcp_sock[i].cfd >= 0) {
	    if (k++ == slot) return tcp_sock[i].cfd;
	}
    }
    return -1;
}

#if defined(CSP_HAS_SOCKETCAN)
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

static int can_fd = -1;

int csp_can_init(csp_rt_t* st)
{
    struct sockaddr_can addr;
    struct ifreq ifr;
    (void)st;

    if (can_iface == NULL)
	return 0;                       // no bus asked for: stay a stub
    if ((can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	perror("can: socket");
	return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, can_iface, IFNAMSIZ-1);
    if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0) {
	fprintf(stderr, "can: no interface '%s': %s\n",
		can_iface, strerror(errno));
	close(can_fd);
	can_fd = -1;
	return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
	perror("can: bind");
	close(can_fd);
	can_fd = -1;
	return -1;
    }
    // Non-blocking: csp_can_input polls once per cycle and must never stall it.
    fcntl(can_fd, F_SETFL, fcntl(can_fd, F_GETFL, 0) | O_NONBLOCK);
    return 0;
}

int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    struct can_frame f;
    ssize_t n;
    (void)st;

    // Stimulus first, and regardless of whether a bus is open: a test may want
    // to drive one frame in while a real interface carries the rest.
    if (inj_pop(id, data, len))
	return 1;
    if (can_fd < 0)
	return 0;
    if ((n = read(can_fd, &f, sizeof(f))) != (ssize_t)sizeof(f)) {
	if ((n < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK))
	    return -1;
	return 0;
    }
    *id  = f.can_id & (f.can_id & CAN_EFF_FLAG ? CAN_EFF_MASK : CAN_SFF_MASK);
    *len = f.can_dlc;
    memcpy(data, f.data, f.can_dlc);
    return 1;
}

// The socket, so the main loop can wait on frames instead of spinning.
int csp_can_pollfd(void) { return can_fd; }

int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    struct can_frame f;
    (void)st;

    if (can_fd < 0)
	return 0;
    memset(&f, 0, sizeof(f));
    // Anything that does not fit the 11-bit standard id goes out extended.
    f.can_id  = (id > CAN_SFF_MASK) ? (id | CAN_EFF_FLAG) : id;
    f.can_dlc = (len > 8) ? 8 : len;    // classic CAN via this socket type
    memcpy(f.data, data, f.can_dlc);
    if (write(can_fd, &f, sizeof(f)) != (ssize_t)sizeof(f))
	return -1;
    return 0;
}

#else  /* no SocketCAN: stubs, so CAN still parses and runs dry */

int csp_can_init(csp_rt_t* st) { (void)st; return 0; }
int csp_can_pollfd(void) { return -1; }
int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    (void)st;
    // No interface, so the stimulus queue IS the bus. This is the path every
    // -F test takes.
    return inj_pop(id, data, len);
}
int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st; (void)id; (void)data; (void)len;
    return 0;
}
#endif

// NO INTERRUPT BACKEND HERE, on purpose.
//
// A host has no interrupt controller, so csp_board_irq_attach's weak default
// refuses every source and the RUNTIME falls back to comparing the pin's level
// between cycles (csp_input_event). That is the same software edge this file
// used to implement, in the one place every board without the silicon needs it
// -- and it is what makes a trigger testable under `-F`: a stimulus row writes
// `Drdy = 1` and the next input phase reports the edge.
//
// /state marks such a source `~`: it samples, so a pulse shorter than a cycle
// is lost where hardware would have caught it.

void csp_setup(csp_rt_t* st)
{
    time_init();
    csp_can_init(st);
    csp_setup_events(st);
}

void csp_input(csp_rt_t* st)
{
    int i;
    
    for (i = 0; i < st->nio; i++) {
	index_t ix = st->io[i];
	switch(decl(st, INDEX(ix), type)) {
	case DECL_DIGITAL: break;
	case DECL_ANALOG: break;
	default: break;
	}
    }
    csp_can_input(st);
    csp_buf_input(st);   // i2c/spi collections and datagrams
    csp_input_timer(st);
    csp_input_event(st);   // deal out this cycle's interrupt edges
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {  // allow output
	for (i = 0; i < st->nio; ++i) {
	    index_t ix = st->io[i];
	    switch(decl(st, INDEX(ix), type)) {
	    case DECL_DIGITAL: break;
	    case DECL_ANALOG: break;
	    default: break;
	    }
	}
	csp_can_output(st);
	csp_buf_output(st);  // i2c/spi starts and datagrams
    }
    csp_output_timer(st);
}

#if !defined(CSP_EXEC_ONLY)
// Returns 0, or -1 with the error in st->ps for the caller to print, or -2 for
// "already reported" -- a line too long is a fact about the FILE, not a parse
// error, and there is nothing in st->ps to describe it.
//
// The length check is not politeness. fgets does not truncate, it SPLITS: what
// did not fit comes back as the next call and is parsed as a line of its own. A
// rule longer than the buffer therefore becomes TWO rules, and both of them may
// parse -- the half before the break loses its `? condition` and runs every
// cycle, and the half after it becomes something else again. That is how
//
//     ErrCurr = 1, IsOn = 0, Lamp = 0 ... ? Measuring && elapsed(Overload) ...
//
// turned into an unconditional trip plus a fragment, with the compiler
// reporting `variable M is not declared` from the middle of a name. Refusing
// the line is the only honest answer; a truncated rule is not a partial rule,
// it is a different one.
int parse_file(csp_rt_t* st, const char* name, FILE* fin)
{
    char buf[MAX_SRC_LINE];
    csp_pmark_t pm;

    st->ps.line = 1;
    while(fgets(buf, (int)sizeof(buf), fin)) {
	size_t n = strlen(buf);

	// Full buffer with no newline at the end: there is more of this line.
	if ((n == sizeof(buf)-1) && (buf[n-1] != '\n')) {
	    fprintf(stderr, "%s:%d line too long, max %d characters\n",
		    name, st->ps.line, (int)(sizeof(buf) - 2));
	    return -2;
	}
	// A `> ...` line is IMMEDIATE -- evaluate it now, exactly as typing it at
	// the prompt would. Without this it went to csp_parse, whose
	// csp_parse_immediate is an empty stub returning 0: the line was accepted
	// and did nothing. That made a `#param` impossible to set from a file,
	// and `> name = value` is the ONLY way to set one -- so no module with
	// parameters could be configured, or tested, outside the REPL.
	{
	    char* p = buf;
	    while ((*p == ' ') || (*p == '\t'))
		p++;
	    if (*p == '>') {
		size_t len = strlen(p);
		while ((len > 0) && ((p[len-1] == '\n') || (p[len-1] == '\r')))
		    p[--len] = '\0';
		if (pending_imm_used + len + 1 <= MAX_PENDING_IMM) {
		    memcpy(&pending_imm[pending_imm_used], p, len + 1);
		    pending_imm_used += len + 1;
		}
		else {
		    fprintf(stderr, "%s:%d too many `>` lines to hold\n",
			    name, st->ps.line);
		    return -2;
		}
		st->ps.line++;
		continue;
	    }
	}
	if (debug_scan) {
	    token_t tv[MAX_LINE_TOKENS];
	    size_t num = MAX_LINE_TOKENS;
	    int n;
	    if ((n = csp_scan_line(st, buf, tv, &num)) < 0)
		return -1;
	    csp_dump_tokens(stdout, tv, num);
	}
	csp_pstate_save(st, &pm);
	if (csp_parse(st, buf) < 0) {
	    // Drop the partial definition so an error report is not followed by
	    // a cascade from a half-open module.
	    csp_pstate_restore(st, &pm);
	    return -1;
	}
    }
    // A #module/#in/#when the file never closed. Reported HERE because this is
    // where "the file ended" is known -- the parser sees only lines, and every
    // one of them after the missing `#end` looked fine on its own.
    //
    // NOT at the prompt: a block is legitimately open there while the rules are
    // being typed.
    if (csp_check_blocks_closed(st) < 0)
	return -1;
    // NULL, not an empty tstr_t: name = 0 already means "no name", and an
    // empty tstr_t would ALLOCATE a zero-length string instead -- one byte of a
    // 512-byte table per source file, for a name nothing can ask for.
    csp_new_decl(st, NULL, DECL_END, 0);
    return 0;
}
#endif

void print_defines()
{
    printf("SUPPORT_REACTIVE=%d\n", SUPPORT_REACTIVE);
    printf("USE_STATISTICS=%d\n",USE_STATISTICS);
    printf("REACTIVE_DEFAULT=%d\n", REACTIVE_DEFAULT);
    printf("OP_AVAIL=%d\n", OP_AVAIL);  // next available = #opcodes
    printf("DECL_AVAIL=%d\n", DECL_AVAIL);  // next available = #decls
    printf("T_LAST=%d\n", T_LAST);        // #tokens
#if !defined(CSP_EXEC_ONLY)
    // Headroom in the SHARED stop-token table. OVERFLOW must be 0: a dropped
    // token silently shortens a stop set, and the parser then fails in places
    // that have nothing to do with whatever pattern outgrew it.
    printf("STOP_TOKENS=%d/%d\n", csp_stop_tokens_used(), MAX_STOP_TOKENS);
#endif
    printf("D_LAST=%d\n", D_LAST);        // #dtok
    printf("PART_LAST=%d\n", PART_LAST);  // <= 16 (4-bit max)
    printf("MAX_NAME_LEN=%d\n", MAX_NAME_LEN);
    printf("MAX_ARGS=%d\n", MAX_ARGS);

    printf("OBJ_BITS=%d\n", OBJ_BITS);
    printf("DECL_BITS=%d\n", DECL_BITS);
    printf("INDEX_BITS=%d\n", INDEX_BITS);
    printf("CSP_STR_BYTES=%d\n", CSP_STR_BYTES);
    printf("MAX_INDICES=%lu\n", (unsigned long)MAX_INDICES);
    printf("MAX_INSTRS=%ld\n", (long)MAX_INSTRS);
    printf("MAX_DECLS=%ld\n", (long)MAX_DECLS);
    printf("MAX_OBJECT_NUM=%u\n", MAX_OBJECT_NUM);
    printf("MAX_STR_BUF=%d\n", MAX_STR_BUF);
    printf("MAX_STACK_DEPTH=%d\n", MAX_STACK_DEPTH);

    // per-leaf / per-buffer tables: multiplied by the program's leaf count
    printf("sizeof(csp_view_t) = %ld\n", sizeof(csp_view_t));
    printf("sizeof(csp_buf_t) = %ld\n", sizeof(csp_buf_t));
    printf("sizeof(value_t) = %ld\n", sizeof(value_t));
    printf("sizeof(rentry_t) = %ld\n", sizeof(rentry_t));
    printf("sizeof(op_entry_t) = %ld\n", sizeof(op_entry_t));
    printf("sizeof(op_info_t) = %ld\n", sizeof(op_info_t));
    printf("sizeof(csp_func_t) = %ld\n", sizeof(csp_func_t));    
    
    // The ARMS are not printed: every declaration arm is eight bytes and every
    // instruction arm four, by construction -- utils/layout.terms says where
    // each field sits and gen/csp_layout.h computes it. There is nothing left
    // for a sizeof to disagree about.
    printf("sizeof(csp_decl_t) = %ld\n", sizeof(csp_decl_t));
    printf("sizeof(csp_instr_t) = %ld\n", sizeof(csp_instr_t));
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
    {"reactive",     no_argument,       0,  'r'},
    {"verbose",      no_argument,       0,  'v'},
    {"no-execute",   no_argument,       0,  'n'},
    {"cycles",       required_argument, 0,  'c'},
    {"timeout",      required_argument, 0,  'T'},
    {"state-file",   required_argument, 0,  's'},
    {"parse-file",   required_argument, 0,  'p'},
    {"compile",      no_argument,       0,  'C'},
    {"object-file",  required_argument, 0,  'O'},
    {"input-file",   required_argument, 0,  'I'},
    {"eeprom",       required_argument, 0,  'e'},
    {"eeprom-size",  required_argument, 0,  'E'},
    {"ram-used",     required_argument, 0,  'U'},
    {"ram",          required_argument, 0,  'M'},
    {"board",        required_argument, 0,  1000},
    {"can",          required_argument, 0,  1001},
    {"no-eeprom",    no_argument,       0,  1002},
    {"prefix",       required_argument, 0,  1003},
    {"role",         required_argument, 0,  1004},
    {"generation",   required_argument, 0,  1005},
    {"virtual-time", no_argument,       0,  1006},
    {"checksum",     required_argument, 0,  1009},
    {"flash",        required_argument, 0,  1010},
    {"part",         required_argument, 0,  1011},
    {"memory",       required_argument, 0,  'm'},
    {"pause",        no_argument,       0,  'b'},
    {"uart",         required_argument, 0,  1014},
    {"id",           required_argument, 0,  1012},
    {"name",         required_argument, 0,  1013},
    {0,              0,                 0,  0 }
};

void usage(const char* prog)
{
#if defined(CSP_EXEC_ONLY)
    // An exec-only build has no compiler and no command layer, so it takes no
    // source and no compiler flags. Listing them would be an offer it cannot
    // keep -- and worse, they used to be ACCEPTED and then walk into an
    // uninitialised parser.
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Runs the ROM image linked into this binary, plus any\n");
    fprintf(stderr, "EEPROM patch. No compiler, no prompt: use ./csp for those.\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -r, --reactive       Enable reactive mode\n");
    fprintf(stderr, "  -c, --cycles=N       Max cycles (0=unlimited)\n");
    fprintf(stderr, "  -T, --timeout=MS     Max runtime in ms (0=unlimited)\n");
    fprintf(stderr, "      --virtual-time   Jump the clock to the next timer instead of sleeping\n");
    fprintf(stderr, "  -Q, --debug-trace    Enable variable tracing\n");
    fprintf(stderr, "  -R, --debug-result   Add result to tracing (Erl)\n");
    fprintf(stderr, "  -s, --state-file=F   State file (Erlang format)\n");
    fprintf(stderr, "  -e, --eeprom=F       EEPROM file for save/load (default: eeprom.db)\n");
    fprintf(stderr, "      --no-eeprom      Do not overlay the saved EEPROM patches at boot\n");
    fprintf(stderr, "  -I, --input-file=F   Data input file\n");
    fprintf(stderr, "      --board=NAME     Simulate a board: mega, mkrzero\n");
    fprintf(stderr, "      --can=IFACE      SocketCAN interface for CAN frames\n");
    fprintf(stderr, "  -M, --ram=N[k]       Total RAM the board has (or Nk KiB)\n");
    fprintf(stderr, "  -U, --ram-used=N[k]  RAM the system/linked libraries take\n");
    fprintf(stderr, "  -m, --memory=N[k]    Usable code memory budget in bytes (or Nk KiB)\n");
    fprintf(stderr, "  -E, --eeprom-size=N[k] Simulated EEPROM capacity (0=unbounded)\n");
    fprintf(stderr, "  -L[erlang|erl|text|txt]  Trace output language\n");
#else
    fprintf(stderr, "Usage: %s [options] [file...]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help\n");
    fprintf(stderr, "  -i, --interactive    Interactive mode\n");
    fprintf(stderr, "  -d, --debug          Enable debug output\n");
    fprintf(stderr, "  -r, --reactive       Enable reactive mode\n");
    fprintf(stderr, "  -n, --no-execute     Parse only, don't execute\n");
    fprintf(stderr, "  -c, --cycles=N       Max cycles (0=unlimited)\n");
    fprintf(stderr, "  -T, --timeout=MS     Max runtime in ms (0=unlimited)\n");
    fprintf(stderr, "  -C, --compile        Compile to object code\n");
    fprintf(stderr, "  -O, --object-file=F  Compiled result file (C code format)\n");
    fprintf(stderr, "      --prefix=NAME    Symbol prefix for -C (default rom)\n");
    fprintf(stderr, "      --role=ROLE      Image role: rom|failsafe (default rom)\n");
    fprintf(stderr, "      --generation=N   Image generation, higher is newer\n");
    fprintf(stderr, "      --virtual-time   Jump the clock to the next timer instead of sleeping\n");
    fprintf(stderr, "      --checksum=BIN   Patch the LPC boot checksum into a .bin and exit\n");
    fprintf(stderr, "      --flash=FILE     Back the simulated flash with FILE, so /upgrade and\n");
    fprintf(stderr, "                       the region guards run without hardware\n");
    fprintf(stderr, "      --part=NAME      Flash layout to simulate: ab, apps, full\n");
    fprintf(stderr, "  -P, --debug-parse    Enable parser debugging\n");
    fprintf(stderr, "  -S, --debug-scan     Enable tokenizer debugging\n");
    fprintf(stderr, "  -Q, --debug-trace    Enable variable tracing\n");
    fprintf(stderr, "  -R, --debug-result   Add result to tracing (Erl)\n");
    fprintf(stderr, "  -s, --state-file=F   State file (Erlang format)\n");
    fprintf(stderr, "  -p, --parse-file=F   Parsed structure file\n");
    fprintf(stderr, "  -e, --eeprom=F       EEPROM file for save/load (default: eeprom.db)\n");
    fprintf(stderr, "      --no-eeprom      Do not overlay the saved EEPROM patches at boot\n");
    fprintf(stderr, "      --id=N           sys.Id for this run (recorded, NOT saved)\n");
    fprintf(stderr, "      --name=TEXT      sys.Name likewise -- both survive /save only if you ask\n");
    fprintf(stderr, "  -I, --input-file=F   Data input file\n");
    fprintf(stderr, "      --board=NAME     Simulate a board: mega, mkrzero (measured;\n");
    fprintf(stderr, "                       sets --ram/--ram-used/--eeprom-size)\n");
    fprintf(stderr, "      --can=IFACE      SocketCAN interface for CAN frames\n");
    fprintf(stderr, "                       (e.g. vcan0); omit to run without a bus\n");
    fprintf(stderr, "  -M, --ram=N[k]       Total RAM the board has (or Nk KiB)\n");
    fprintf(stderr, "  -U, --ram-used=N[k]  RAM the system/linked libraries take\n");    
    fprintf(stderr, "  -m, --memory=N[k]    Usable code memory budget in bytes (or Nk KiB)\n");
    fprintf(stderr, "  -E, --eeprom-size=N[k] Simulated EEPROM capacity (0=unbounded); /save\n");
    fprintf(stderr, "                       fails past it, as it would on a real board\n");
    fprintf(stderr, "  -b, --pause          Start paused after load (inspect, then /resume); implies -i\n");
    fprintf(stderr, "  -L[erlang|erl|text|txt]  Trace output language\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no file is given, reads from stdin.\n");
    fprintf(stderr, "In interactive mode (-i), type /help for commands.\n");
    fprintf(stderr, "Ctrl-D (or EOF on a pipe) ends the PROMPT, not the program:\n");
    fprintf(stderr, "what is left runs on until it settles, as if it had been\n");
    fprintf(stderr, "given as a file. /quit exits; -c and -T bound a run.\n");
#endif
}

char    input_buf[MAX_LINE_SIZE];
token_t input_tv[MAX_LINE_TOKENS];
size_t  input_num = 0;
uint32_t input_cycle = 0;
int     input_delay = 0;

// The two helpers below parse a stimulus row, so they need the TOKENIZER and
// part_from_tstr -- neither of which an exec-only build links. cycle_input is
// already stubbed out there (see below), and nothing else calls them.
#if !defined(CSP_EXEC_ONLY)

// `can <id> <b0> <b1> ...` -- queue a frame for csp_can_recv. Returns the
// number of TOKENS consumed, so the caller can step past the data bytes; a row
// may carry a frame and ordinary assignments together.
static int input_can_frame(token_t* tv, size_t num, int i)
{
    uint8_t data[8];
    uint32_t id;
    int n = 0;
    int j;

    id = (uint32_t)tv[i+1].v.val.i;
    for (j = i + 2; (j < (int)num) && (tv[j].t == INT) && (n < 8); j++)
	data[n++] = (uint8_t)tv[j].v.val.i;
    inj_push(id, data, (uint8_t)n);
    return j - i;
}

// tv[0] is cycle count tv[1] may be delay
//
// Three forms, and they are tried longest-first so `T.period = 500` is not read
// as `T` followed by junk:
//   <name> . <part> = <value>     a part: .pin .port .period .dlc .tx ...
//   <name> = <value>              the value
//   can <id> <byte>...            a frame delivered through csp_can_recv
void cycle_input_values(csp_rt_t* st, token_t* tv, size_t num)
{
    int i = 1;

    input_delay = 0;
    if (tv[1].t == INT) {
	i = 2;
	input_delay = tv[1].v.val.i;
    }
    while(i < num) {
	// `can` is the KEYWORD T_CAN, not a WORD -- it is the same token the
	// scanner hands `#buffer F:16 in can 0x20`.
	if ((tv[i].t == T_CAN) && ((i+1) < (int)num) && (tv[i+1].t == INT)) {
	    i += input_can_frame(tv, num, i);
	    continue;
	}
	if (((i+4) < (int)num) &&
	    (tv[i].t == WORD) && (tv[i+1].t == DOT) && (tv[i+2].t == WORD) &&
	    (tv[i+3].t == EQ) &&
	    ((tv[i+4].t == INT) || (tv[i+4].t == FLT))) {
	    csp_part_t part = part_from_tstr(&tv[i+2].v.str);
	    if (part != PART_LAST) {
		index_t ix = csp_lookup_decl(st, &tv[i].v.str);
		if (ix != BAD_INDEX) {
		    // BOTH halves, the way csp_settings_apply does it. A config
		    // part written only to DOUT reads back from a DIN that still
		    // holds the declared value -- so the write would appear to
		    // land and then not be there.
		    csp_dio_set_part(st, ix, tv[i+4].v.val, part, DOUT);
		    csp_dio_set_part(st, ix, tv[i+4].v.val, part, DIN);
		}
		i += 5;
		continue;
	    }
	    // Not a part. `obj.field` would land here; it is not supported, and
	    // falling through would silently write the OBJECT's own value.
	    i += 5;
	    continue;
	}
	if ((tv[i].t == WORD) && (tv[i+1].t == EQ) &&
	    ((tv[i+2].t == INT) ||(tv[i+2].t == FLT))) {
	    index_t ix;
	    ix = csp_lookup_decl(st, &tv[i].v.str);
	    if (ix != BAD_INDEX)
		csp_set_value(st, ix, tv[i+2].v.val);
	}
	i++;
    }
}
#endif /* !CSP_EXEC_ONLY */

int input_applied = 1;   // virtual mode: has the loaded row been applied?
int input_done = 0;      // virtual mode: input file exhausted

#if defined(CSP_EXEC_ONLY)
// -I feeds rows of `<time_ms> <var>=<val>` and parses them with the TOKENIZER,
// which this build does not have. Refuse the option rather than link half a
// compiler back in for a test harness.
int cycle_input(csp_rt_t* st, FILE* fin) { (void)st; (void)fin; return -1; }
#else
// read <cycle> <delay> <var1> '=' <value1>  <var2> '=' <value2> ...
// In virtual mode the first field is an absolute virtual time (ms); a row is
// applied once vclock reaches it, then the next row is loaded.
int cycle_input(csp_rt_t* st, FILE* fin)
{
    if (virtual_time) {
	while (input_applied) {              // load next row
	    if (fgets(input_buf, MAX_LINE_SIZE, fin) == NULL)
		return -1;
	    input_num = MAX_LINE_TOKENS;
	    if (csp_scan_line(st, input_buf, input_tv, &input_num) < 0)
		return -1;
	    if (debug)
		csp_dump_tokens(stdout, input_tv, input_num);
	    if ((input_num > 0) && (input_tv[0].t == INT)) {
		input_cycle = input_tv[0].v.val.i;
		input_applied = 0;
	    }
	}
	if (vclock >= input_cycle) {         // due (or overshot): apply once
	    cycle_input_values(st, input_tv, input_num);
	    input_applied = 1;
	}
	return 0;
    }
    if (input_cycle < st->cycle) { // catch up
	char* ptr;
	while((ptr = fgets(input_buf, MAX_LINE_SIZE, fin)) != NULL) {
	    int n;
	    input_num = MAX_LINE_TOKENS;
	    if ((n = csp_scan_line(st, input_buf, input_tv, &input_num)) < 0)
		return -1;
	    if (debug)
		csp_dump_tokens(stdout, input_tv, input_num);
	    if ((input_num > 0) && (input_tv[0].t == INT)) {
		if (input_tv[0].v.val.i < st->cycle) {  // read next
		    input_cycle = input_tv[0].v.val.i;
		    continue;
		}
		if (input_tv[0].v.val.i > st->cycle) { // wait for it
		    input_cycle = input_tv[0].v.val.i;
		    return 0;
		}
		input_cycle = input_tv[0].v.val.i;		
		break;
	    }
	}
	if (ptr == NULL)
	    return -1;
    }
    cycle_input_values(st, input_tv, input_num);
    return 0;
}
#endif


// Append the UDP sockets to the fixed slots and say how many fds there are now.
//
// Called at EVERY poll site rather than once at start, because the sockets open
// on first use -- the first cycle a buffer naming the port is polled, and the
// REPL can add such a buffer at any time. stdin and CAN keep their indices, so
// serial_poll and the CAN wake-up are unaffected.
static nfds_t poll_set(struct pollfd* pfd, nfds_t fixed, nfds_t max)
{
    nfds_t n = fixed;
    int i, fd;

    for (i = 0; (n < max) && ((fd = csp_udp_pollfd(i)) >= 0); i++) {
	pfd[n].fd = fd;
	pfd[n].events = POLLIN;
	pfd[n].revents = 0;
	n++;
    }
    // And the TCP ends -- a listening socket is readable when someone calls, a
    // connection when bytes arrive, and both are things to wake for.
    for (i = 0; (n < max) && ((fd = csp_tcp_pollfd(i)) >= 0); i++) {
	pfd[n].fd = fd;
	pfd[n].events = POLLIN;
	pfd[n].revents = 0;
	n++;
    }
    // And a serial port, if --uart gave the host one.
    for (i = 0; (n < max) && ((fd = csp_uart_pollfd(i)) >= 0); i++) {
	pfd[n].fd = fd;
	pfd[n].events = POLLIN;
	pfd[n].revents = 0;
	n++;
    }
    return n;
}

// ============================================================
// The read-only poison (CSP_RO_POISON) -- see the long note at ro_byte in csp.h
// ============================================================
//
// The host has one address space, so a read of RODATA that forgot its ro_
// accessor is CORRECT here and wrong only on AVR, where it reads the data space
// at a flash address. Four tables shipped that way, one of them the error table.
//
// This gives the host the second address space it lacks. The linker has already
// gathered the core objects' .rodata into `csp_ro`, page-aligned and on its own
// pages (utils/ro_ld.sh). What is left is to make the section unreadable and
// keep a copy somewhere the code cannot name:
//
//   shadow = mmap(...); memcpy(shadow, section);   the copy that EXISTS
//   mprotect(section, PROT_NONE);                  the place that does NOT
//   csp_ro_delta = shadow - section;               what ro_* adds
//
// FIRST THING IN main, before anything reads a string: every csp_print_lit in
// the core resolves through ro_byte, and those work either way, but a read that
// happens before the delta is set would use 0 and hit the real (still readable)
// section -- passing a test it should fail.
//
// Not a shipped configuration. `make ro_poison`.
#if defined(CSP_RO_POISON)
// _GNU_SOURCE early enough for siginfo_t/sigaction: this file compiles with
// -std= defaults that hide them behind feature-test macros.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <sys/mman.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

extern const char __start_csp_ro[], __stop_csp_ro[];

const char* csp_ro_lo    = NULL;
const char* csp_ro_hi    = NULL;
long        csp_ro_delta = 0;

static void csp_ro_trap(void);

static void csp_ro_init(void)
{
    const char* lo = __start_csp_ro;
    const char* hi = __stop_csp_ro;
    size_t n = (size_t)(hi - lo);
    void* shadow;

    // Empty means this binary was linked without the fragment -- a one-shot test
    // build, say. Not poisoned, and that is correct rather than an error: the
    // delta stays 0 and csp_ro_real's range test never matches, so every
    // accessor is the plain read it would have been. Silent on purpose; the
    // readelf check in `make ro_poison` is what guards the binary that matters.
    if (n == 0)
	return;
    shadow = mmap(NULL, n, PROT_READ | PROT_WRITE,
		  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (shadow == MAP_FAILED) {
	perror("csp_ro: mmap");
	return;
    }
    memcpy(shadow, lo, n);
    // PROT_NONE, not PROT_READ: the point is that the ADDRESS the code names is
    // not backed by anything. mprotect works in whole pages, which is why the
    // linker script pads the section out to one.
    if (mprotect((void*)(uintptr_t)lo, n, PROT_NONE) != 0) {
	perror("csp_ro: mprotect");
	return;
    }
    csp_ro_lo    = lo;
    csp_ro_hi    = hi;
    csp_ro_delta = (const char*)shadow - lo;
    csp_ro_trap();
}

// A poison that only says "Segmentation fault" is barely worth having: the
// whole point is to name the read, and a bare core file makes you go and get a
// debugger for something the process already knows.
//
// So: catch SIGSEGV, and if the address is inside the section say so and print
// the stack. Anything else is re-raised with the handler removed, so a real bug
// still dies the way it should instead of being swallowed by the tool that was
// meant to find bugs.
//
// THE MESSAGE FIRST, with write(2), and only then the stack.
//
// The order is not style. fprintf takes a lock and backtrace() calls into the
// dynamic linker, which takes another -- and neither is async-signal-safe. If
// the faulting read happened while libc already held one of those, the handler
// DEADLOCKS. The process then sits there until something kills it, and every
// byte of buffered stdout dies with it: no message, no stack, no output at all.
//
// That is not a theory. It is what a test looked like -- `got` empty, `want`
// five lines, and no sign anywhere that a fault had even happened. The tool
// swallowed its own finding, and cost several wrong hypotheses.
//
// So the address goes out through write(2), which is async-signal-safe and
// unbuffered, before anything that can block. If the backtrace then hangs, the
// one fact worth having is already on the terminal.
static void csp_ro_write(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    if (write(STDERR_FILENO, s, n) < 0) { /* dying anyway */ }
}

static void csp_ro_hex(char* out, unsigned long v)
{
    static const char d[] = "0123456789abcdef";
    int i, k = 0;
    for (i = (int)(2 * sizeof(v)) - 1; i >= 0; i--) {
	unsigned nib = (unsigned)((v >> (4 * i)) & 0xf);
	if (nib || k || i == 0) out[k++] = d[nib];
    }
    out[k] = '\0';
}

static void csp_ro_segv(int sig, siginfo_t* si, void* uc)
{
    void*  bt[24];
    char   hex[24];
    int    n;
    (void)uc;

    if ((const char*)si->si_addr < csp_ro_lo ||
	(const char*)si->si_addr >= csp_ro_hi) {
	signal(sig, SIG_DFL);           // not ours -- let it die normally
	raise(sig);
	return;
    }
    csp_ro_write("\n*** RODATA read without an ro_ accessor: csp_ro+0x");
    csp_ro_hex(hex, (unsigned long)((const char*)si->si_addr - csp_ro_lo));
    csp_ro_write(hex);
    csp_ro_write("\n*** On AVR this reads the data space at a flash address.\n");

    // Best effort from here. -rdynamic gives names instead of offsets; a hang
    // in here costs the stack, not the finding.
    n = backtrace(bt, (int)(sizeof(bt)/sizeof(bt[0])));
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
    _exit(90);                          // distinct from a plain crash
}

static void csp_ro_trap(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = csp_ro_segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
}
#endif

int main(int argc, char** argv)
{
    csp_rt_t state;
    index_t x;
    // Both belong to reading SOURCE, which this binary cannot do when it is
    // built to run only its linked image -- the branch that uses them is
    // compiled out below, so declaring them there too is what keeps the exec
    // build warning-free.
#if !defined(CSP_EXEC_ONLY)
    int r;
    FILE* fin = stdin;
#endif
    FILE* state_file = stdout;
    FILE* parse_out = stdout;
    FILE* object_file = NULL;
    FILE* input_file = NULL;
    int execute = 1;
    int interactive = 0;
    int no_eeprom = 0;   // --no-eeprom: skip the boot-time EEPROM overlay
    uint32_t max_cycles = 0;
    uint32_t max_time_ms = 0;
    size_t   mem_limit = 0;   // -m: usable code-memory budget (0 = full arena)
    int      pause_start = 0; // -b: start paused (inspect counters, then /resume)
    uint32_t start_time;
    int c;
    int reactive = REACTIVE_DEFAULT;
    int compile = 0;
    // -C emits <prefix>_str/_decl/_instr/... Default rom, so an unadorned
    // `csp -C` still produces the rom.c every build links.
    const char* rom_prefix = "rom";
    unsigned rom_role = CSP_ROLE_ROM;
    unsigned rom_generation = 0;
    // stdin, the CAN socket, one slot per bound UDP port, and two per TCP port
    // (the listener and the connection on it).
    struct pollfd pfd[2 + CSP_UDP_MAXSOCK + 2*CSP_TCP_MAXSOCK
		      + CSP_UART_MAXPORT];
    nfds_t pfd_max = (nfds_t)(sizeof(pfd)/sizeof(pfd[0]));
    nfds_t nfds = 0;    // the FIXED part: stdin and CAN, whose slots never move
    int can_slot = 0;   // index of the CAN socket in pfd (0 = not polled)
    csp_lang_t lang = TEXT;
    int first_cycle = 1;
    int given = 0;    // was a program handed to us (file or stdin)?
    int anyd;

#if defined(CSP_RO_POISON)
    // FIRST, before anything reads a string from the core's RODATA.
    csp_ro_init();
#endif
    file_output = stdout;

    while (1) {
	int option_index = 0;
	c = getopt_long(argc, argv, "hindPQRSCc:T:s:p:rtO:e:L:I:F:m:M:E:U:b",
			long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	case 'h':
	    usage(argv[0]);
	    exit(0);
#if defined(CSP_EXEC_ONLY)
	// Accepted-and-ignored is the worst answer: -C would print nothing, -i
	// would give a prompt with no commands behind it, and a source file used
	// to reach an uninitialised parser and segfault. Say what is missing.
	case 'i': case 'b': case 'n': case 'C': case 'P': case 'S':
	case 'p': case 'O': case 1003: case 1004: case 1005:
	    fprintf(stderr, "%s: that option needs the compiler; "
		    "this build has none (use ./csp)\n", argv[0]);
	    exit(1);
#else
	case 'i': interactive = 1; break;
	case 'b': pause_start = 1; interactive = 1; break;  // pause needs the REPL
	case 'n': execute = 0; break;
	case 'C': compile = 1; break;
	case 'P': debug_parse = 1; break;
	case 'S': debug_scan = 1; break;
	case 1003: rom_prefix = optarg; break;   // --prefix: symbol prefix for -C
	case 1004:                               // --role: what the image is for
	    rom_role = (strcmp(optarg, "failsafe") == 0) ? CSP_ROLE_FAILSAFE
							 : CSP_ROLE_ROM;
	    break;
	case 1005: rom_generation = atoi(optarg); break;
#endif
	case 'e': eeprom_file = optarg; break;
	case 1001: can_iface = optarg; break;
	case 1014:   // --uart=[<unit>:]<device>: give the host a serial port
	    uart_add(optarg);
	    break;
	case 1010:   // --flash=FILE: back the simulated flash with a file
	    csp_flash_host_file(optarg);
	    break;
	case 1011: { // --part=ab|apps|full: which host layout to simulate
	    const csp_device_t* d = csp_device_by_name(optarg);
	    if (d == NULL) {
		fprintf(stderr, "unknown part '%s' (ab, apps, full)\n", optarg);
		exit(1);
	    }
	    csp_device_set(d);
	    break;
	}
#if !defined(CSP_EXEC_ONLY)
	case 1012: { // --id=N: sys.Id for this run, not written to the store
	    char line[64];
	    // The '>' is the IMMEDIATE marker, not decoration: line_is_rule
	    // sees a lone '=' and would otherwise file this as a rule -- and a
	    // rule may not assign to a #param.
	    snprintf(line, sizeof(line), "> sys.Id = %s", optarg);
	    queue_immediate(line);
	    break;
	}
	case 1013: { // --name=TEXT: likewise sys.Name
	    char line[CSP_SETTINGS_MAX_STR + 16];
	    // Quoted, because sys.Name is a string param and the immediate is
	    // parsed as ordinary source. A name with a quote in it is refused by
	    // the parser rather than smuggled through, which is the right answer
	    // for something that ends up in a settings store.
	    snprintf(line, sizeof(line), "> sys.Name = \"%s\"", optarg);
	    queue_immediate(line);
	    break;
	}
#endif
	case 1002: no_eeprom = 1; break;
	case 'r': reactive = 1; break;   // -r: enable reactive mode (no argument)
	case 'c': max_cycles = atoi(optarg); break;
	case 'T': max_time_ms = atoi(optarg); break;
	case 'd': debug = 1; break;
	case 'R': debug_result = 1; break;
	case 'Q': debug_trace = 1; break;
	case 's':
	    lang = ERLANG;
	    debug_trace = 1;
	    if ((state_file = fopen(optarg, "w")) == NULL) {
		fprintf(stderr, "unable to open state file '%s'\n", optarg);
		exit(1);
	    }
	    break;
#if !defined(CSP_EXEC_ONLY)
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
#endif
	case 'I':
	    // FIXME: multiple input files?
	    if ((input_file = fopen(optarg, "r")) == NULL) {
		fprintf(stderr, "unable to open input file '%s'\n", optarg);
		exit(1);
	    }
	    break;
	case 'F':   // virtual-time input file: rows are <time_ms> <var>=<val> ...
	    if ((input_file = fopen(optarg, "r")) == NULL) {
		fprintf(stderr, "unable to open input file '%s'\n", optarg);
		exit(1);
	    }
	    virtual_time = 1;
	    break;
	case 1009: {  // --checksum=FILE. Patch the boot checksum into a raw
		      // firmware image, in place.
		      //
		      // The word at offset 0x14 is not reserved: the boot ROM
		      // adds the first eight vectors and enters ISP if they do
		      // not sum to zero. The part then comes up silent -- no
		      // fault, no output, and a debugger says the code is fine.
		      //
		      // Here rather than in the startup file because it cannot
		      // be known there: the vector words are what the LINKER
		      // produced, so only something holding the finished image
		      // can add them up.
	    FILE* bf = fopen(optarg, "r+b");
	    uint8_t v[32];
	    uint32_t sum;
	    if (bf == NULL) {
		fprintf(stderr, "%s: %s: cannot open\n", argv[0], optarg);
		exit(1);
	    }
	    if (fread(v, 1, 32, bf) != 32) {
		fprintf(stderr, "%s: %s: shorter than a vector table\n",
			argv[0], optarg);
		exit(1);
	    }
	    sum = csp_lpc_checksum(v);
	    if (fseek(bf, 0, SEEK_SET) != 0 || fwrite(v, 1, 32, bf) != 32) {
		fprintf(stderr, "%s: %s: write failed\n", argv[0], optarg);
		exit(1);
	    }
	    fclose(bf);
	    printf("%s: checksum 0x%08lX\n", optarg, (unsigned long)sum);
	    exit(0);
	}
	case 1006:  // virtual time WITHOUT an -F input file. The clock jumps to
		    // the next timer deadline instead of sleeping, so a run is
		    // deterministic and instant -- which is what a timer test
		    // needs. Without it such a test is at the mercy of the
		    // loop's sleep policy, and a program that never settles
		    // (a free-running counter) starves the clock entirely.
	    virtual_time = 1;
	    break;
	case 'm': {   // usable code-memory budget; accepts a trailing k/K = KiB
	    char* end = NULL;
	    unsigned long v = strtoul(optarg, &end, 0);
	    if (end && (*end == 'k' || *end == 'K'))
		v *= 1024;
	    mem_limit = (size_t)v;
	    break;
	}
	case 'M': {   // total RAM avaiable
	    char* end = NULL;
	    unsigned long v = strtoul(optarg, &end, 0);
	    if (end && (*end == 'k' || *end == 'K'))
		v *= 1024;
	    system_ram_capacity = (size_t)v;
	    break;	    
	}
	case 1000:    // --board=NAME: RAM/system/EEPROM measured from that board's
		      // firmware build (csp_boards.h). Beats guessing the three.
	    if (strcmp(optarg, "mega") == 0) {
		system_ram_capacity = CSP_BOARD_MEGA_RAM;
		system_ram_used     = CSP_BOARD_MEGA_SYSTEM;
		csp_sim_state       = CSP_BOARD_MEGA_STATE;
		eeprom_cap          = CSP_BOARD_MEGA_EEPROM;
	    }
	    else if (strcmp(optarg, "mkrzero") == 0) {
		system_ram_capacity = CSP_BOARD_MKRZERO_RAM;
		system_ram_used     = CSP_BOARD_MKRZERO_SYSTEM;
		csp_sim_state       = CSP_BOARD_MKRZERO_STATE;
		eeprom_cap          = CSP_BOARD_MKRZERO_EEPROM;
	    }
	    else if (strcmp(optarg, "play") == 0) {
		system_ram_capacity = CSP_BOARD_PLAY_RAM;
		system_ram_used     = CSP_BOARD_PLAY_SYSTEM;
		csp_sim_state       = CSP_BOARD_PLAY_STATE;
		eeprom_cap          = CSP_BOARD_PLAY_EEPROM;
	    }	    
	    else {
		fprintf(stderr, "unknown board '%s' (mega, mkrzero)\n", optarg);
		exit(1);
	    }
	    break;
	case 'U': {   // what the system/linked packages take, so the host can model
		      // a board's overhead; accepts a trailing k/K = KiB
	    char* end = NULL;
	    unsigned long v = strtoul(optarg, &end, 0);
	    if (end && (*end == 'k' || *end == 'K'))
		v *= 1024;
	    system_ram_used = (uint32_t)v;
	    break;
	}
	case 'E': {   // simulated EEPROM capacity, so the host can hit a board's
		      // /save ceiling; accepts a trailing k/K = KiB
	    char* end = NULL;
	    unsigned long v = strtoul(optarg, &end, 0);
	    if (end && (*end == 'k' || *end == 'K'))
		v *= 1024;
	    eeprom_cap = (uint32_t)v;
	    break;
	}
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
	printf("reactive=%d\n", reactive);
	printf("execute=%d\n", execute);
	printf("interactive=%d\n", interactive);
	printf("#arguments=%d\n", argc-optind);
	if (!execute && !compile && (optind >= argc) && !interactive)
	    exit(0);
    }
#if !defined(SUPPORT_REACTIVE) || (SUPPORT_REACTIVE==0)
    if (reactive) {
	fprintf(stderr, "reactive mode not configured\n");
	exit(1);
    }
#endif

    csp_rt_init(&state, reactive, CSP_CSTATE);
    // -m shrinks the usable code-memory budget to exercise the out-of-memory
    // path. Clamp to what csp_mem_init left for the pool, NOT to mem_size: the
    // line buffer sits in the gap between the two, and raising mem_limit back to
    // the physical size would let decl[] grow down into it.
    if (mem_limit > 0) {
	size_t pool = state.mem_size - state.line.buf_size;
	state.mem_limit = (mem_limit < pool) ? mem_limit : pool;
    }
    csp_set_uconst(&state, csp_uconst);

    // Activate flash-resident firmware: run ROM in place from flash, RAM holds
    // patches. Skip when compiling (-C) so the dump is exactly the parsed program.
    if (!compile) {
	// WHICH image, before one is loaded. sys.Boot lives in the settings
	// store, so the store is read on its own first -- see csp_eeprom_peek.
	// No store, no preference, and csp_load_rom takes the highest
	// generation, which is what it did before this existed.
#if !defined(CSP_NO_EEPROM)
	if (!no_eeprom && (csp_eeprom_peek(&state) == 0))
	    csp_boot_pick(&state);
#endif
	csp_load_rom(&state);
    }

    // Parse input files (if any)
#if defined(CSP_EXEC_ONLY)
    // THE CRASH. This build links csp_compile.c only as an empty translation
    // unit, and even when it did not, csp_compile_init() is never called -- so
    // the pattern tables are unscanned and csp_parse walked straight into
    // uninitialised pmatch state. A source file is not something this binary can
    // be given; say so instead of dying on it.
    if (optind < argc) {
	fprintf(stderr, "%s: '%s': this build runs its linked ROM image and "
		"cannot read source (use ./csp)\n", argv[0], argv[optind]);
	exit(1);
    }
#else
    if (optind < argc) {
	struct stat src_stat;
	src_file = argv[optind];   // first one, for the ROM provenance banner
	strcpy(src_modified, "unknown");
	if (stat(src_file, &src_stat) >= 0) {
	    if (ctime_r(&src_stat.st_mtime, src_modified)) 
		src_modified[strlen(src_modified)-1] = '\0';
	}
	while (optind < argc) {
	    if ((fin = fopen(argv[optind], "r")) == NULL) {
		fprintf(stderr, "unable to open file '%s'\n", argv[optind]);
		exit(1);
	    }
	    if ((r = parse_file(&state, argv[optind], fin)) < 0) {
		if (r != -2) {          // -2 already said what was wrong
		    fprintf(stderr, "%s:%d ", argv[optind], state.ps.line);
		    print_error(&state);
		}
		exit(1);
	    }
	    fclose(fin);
	    optind++;
	    given = 1;
	}
    }
    else if (!interactive) {
	// no files given, read from stdin (unless interactive)
	given = 1;
	if ((r = parse_file(&state, "*stdin*", stdin)) < 0) {
	    if (r != -2) {
		fprintf(stderr, "*stdin*:%d ", state.ps.line);
		print_error(&state);
	    }
	    exit(1);
	}
    }
#endif

    // Overlay the saved EEPROM patches on top of the ROM baseline -- ALWAYS, the
    // way the Arduino boot does (csp_load_rom then csp_eeprom_load): the patches
    // LIVE in EEPROM and layer on the firmware, so gating this on "no firmware"
    // was wrong -- a board with baked firmware would never see its own patches.
    // csp_eeprom_load re-runs csp_load_rom internally, so it does NOT clobber the
    // ROM; on a bad/absent save it returns before touching ROM. --no-eeprom skips
    // it (testing, or a clean boot). Still skipped when a program was handed to us
    // on the command line: that is an explicit "run THIS", and the load re-inits
    // from scratch, which would discard the given program.
    //
    // `given` asks "was a program given?", NOT "did any instructions appear?" -- a
    // declarations-only program (a data model, or one still being built at the
    // prompt) must not look like "nothing to run" and get replaced by eeprom.db.
    if (!given && !no_eeprom) {
	if (csp_eeprom_load(&state) == 0)
	    printf("Restored %d decls, %d instrs from %s\n",
		   state.ps.nd - state.rom_nd, state.ps.nn - state.rom_nn,
		   eeprom_file);
	else
	    csp_clr_error(&state);   // "no saved state" is the normal case here,
				     // not something to report on the next line
    }
    
    // initialize time before starting timers
    time_init();

    // -b: come up paused *before* csp_rt_start allocates anything, so /memory can
    // show the estimate first; edited makes /resume run csr + rt_start + setup.
    // Otherwise build the graph and set up now (reporting a setup failure -- e.g.
    // the buffer table or heap ran out -- instead of running a corrupt state).
    if (pause_start) {
	state.paused = 1;
	state.edited = 1;
    }
    else {
	if (csp_rebuild(&state) < 0) {   // graph + leaf/device setup, one layout
	    fprintf(stderr, "setup failed: ");
	    print_error(&state);
	    // AND stop. It used to report and fall through to csp_setup, which
	    // left the exit code at 0 -- so `csp prog.csp || handle_it` saw a
	    // success after the one failure that means the program does not fit.
	    // Nothing downstream can do anything useful with a state that failed
	    // to lay out, which is what the comment above already said.
	    exit(1);
	}
	csp_setup(&state);
#if !defined(CSP_EXEC_ONLY)
	// Now the arena exists and the leaves are laid out, so a held `>` can
	// do what it says. In source order, which is what a reader expects.
	run_pending_immediates(&state);
#endif
    }

    if (debug_parse) {
	csp_dump(parse_out, &state);
	csp_list_rules(parse_out, &state);
    }

    if (compile) {
	FILE* objf = object_file == NULL ? stdout : object_file;
	csp_rom_meta_t meta;
	meta.src     = src_file;
	meta.modified = src_modified;
	meta.version = CSP_VERSION;
	meta.date    = __DATE__ " " __TIME__;
	meta.prefix  = rom_prefix;
	meta.role    = rom_role;
	meta.generation = rom_generation;
	csp_dump_code(objf, &state, &meta);
    }

    if (!execute) {
	if (parse_out != stdout) fclose(parse_out);
	if (state_file != stdout) fclose(state_file);
	if (object_file) fclose(object_file);
	if (input_file) fclose(input_file);	
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
	can_slot = 1;

	printf("CandySpeak Interactive Mode\n");
	printf("Type /help for commands, /quit to exit\n");
	// Ctrl-D is NOT exit here, and saying so is cheaper than the surprise:
	// it drops the prompt and leaves the program running, the way handing
	// the same file on the command line would.
	//
	// Only to a TERMINAL. Nobody piping a file in is going to press it, and
	// a third banner line is noise in front of their output -- which is not
	// a guess: adding it unconditionally failed 41 cases in tests/repl.sh,
	// all of them on the banner and none on the behaviour.
	if (isatty(STDIN_FILENO))
	    printf("Ctrl-D drops the prompt and lets the program run on\n");
	if (pause_start)
	    printf("Started paused -- /memory /state to inspect, /resume to run\n");
	printf("\n");
	state.latch = 1; // hold output
    }

    // Wait on the CAN socket alongside stdin. Without this a program whose only
    // input is the bus has nothing to wake it: no timer, nothing changing.
    if (csp_can_pollfd() >= 0) {
	pfd[can_slot].fd = csp_can_pollfd();
	pfd[can_slot].events = POLLIN;
	nfds = can_slot + 1;
    }

    start_time = csp_time_ms();

    // initial trace shows cycle 0 (pre-eval state)
    if (debug_trace)
	csp_dump_state(state_file, &state, lang);

    // inital poll
    if (nfds > 0)
	poll(pfd, poll_set(pfd, nfds, pfd_max), 0);

loop:
    if (quit_flag)
	goto done;

    if (first_cycle) {
	state.cycle = 1;
	first_cycle = 0;
    }
    else if (!state.paused) {   // frozen while /pause is in effect
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
	int timeout_ms;
	
	// Not while a line is already pending: the prompt means "waiting for
	// you", and we are not. Printing it here consumed the need_prompt that
	// the queue re-feed was going to use, so the OK of one line landed after
	// a prompt and the next pasted line echoed with nothing in front of it.
	if (interactive && !state.line.ready)
	    csp_line_prompt(&state.line);
	// Never sleep on a line that is already in hand. With the input queue a
	// whole paste can be waiting, and any wait EACH time turns a pasted file
	// into a minute of watching it trickle in. This has to short-circuit the
	// timer branch too -- a declared timer put wait_ms back in and undid it.
	if (state.line.ready)
	    timeout_ms = 0;
	else {
	    timeout_ms = interactive ? 100 : 0;
	    // Wait for timer if needed (non-interactive mode)
	    if (state.es.wait_ms != NOTIMEOUT) {
		if (timeout_ms == 0 || state.es.wait_ms < (uint32_t)timeout_ms)
		    timeout_ms = state.es.wait_ms;
	    }
	}
	poll(pfd, poll_set(pfd, nfds, pfd_max), timeout_ms);
	serial_poll(&state, pfd, nfds);

	if (state.line.ready) {
	    process_serial_line(&state, state.line.buf);
	    csp_line_done(&state.line);
	    if (quit_flag) goto done;
	}
	// Nothing more can arrive on stdin: stop being interactive.
	//
	// Not "stop", though -- the loop's non-interactive rules take over from
	// here and run the program to wherever it settles, exactly as if it had
	// been given on the command line. A program that quiesces ends the
	// process; one with a timer keeps running, which is what -c and -T are
	// for.
	//
	// fd -1 rather than a shorter nfds: poll ignores it, the CAN slot keeps
	// its index, and the queued-line branch above still runs -- serial_poll
	// reads a whole paste in one sweep and csp_line_done feeds it back one
	// line per turn, so the tail of a piped session is not dropped.
	if (stdin_gone && interactive) {
	    interactive = 0;
	    pfd[0].fd = -1;
	    disable_raw_mode();
	}
    }

    // /pause freezes execution: keep servicing interactive input (above) so
    // /resume and edits still work, but run no input/cycle/commit/output.
    //
    // THE ROUTES STILL RUN. They are not rules -- they move bytes between two
    // transports and touch nothing the pause is protecting. And they have to:
    // /upgrade pauses the node for the duration of a flash write, so a node
    // being upgraded OVER a route would stop reading the very link the image is
    // arriving on. It accepted `/upgrade A force` and then went deaf.
    if (state.paused) {
	csp_route_run(&state);
	goto loop;
    }

    csp_input(&state);
    if (input_file) {
	if (cycle_input(&state, input_file) < 0)
	    input_done = 1;
    }

    // /live freezes the rules but keeps I/O running (poke outputs, watch inputs).
    x = state.live ? BAD_INDEX : csp_cycle(&state);  // ROM (seq) + RAM, one model

    // A RUNTIME error (today: an array index outside its array) is set inside
    // the eval loop, where no command is waiting to report it. Without this it
    // stayed in st->ps.err and the machine just ran on with the access skipped
    // -- a bounds check nobody can see is not a bounds check. Reported once and
    // cleared: the rule fires every cycle, and one bad index must not turn into
    // a stream at 50 Hz.
    if (state.ps.err != ERR_OK) {
	print_error(&state);
	csp_clr_error(&state);
    }

    anyd = state.es.anyd;  // save before commit clears it

    csp_commit(&state);

    csp_output(&state);

    if (anyd) {
	if (debug_trace)
	    csp_dump_state(state_file, &state, lang);
    }

    // Advance time. Virtual: jump the clock (>=1 tick/cycle, wait_ms when a
    // timer is pending) instead of sleeping. Real: sleep until the next timer.
    if (virtual_time) {
	// jump to the nearest pending event (next timer or next input row),
	// but always advance at least one tick so time never stands still.
	uint32_t adv = (state.es.wait_ms == NOTIMEOUT) ? 0xFFFFFFFFu : state.es.wait_ms;
	if (!input_done && !input_applied && (input_cycle > vclock)) {
	    uint32_t inp = input_cycle - vclock;
	    if (inp < adv) adv = inp;
	}
	if ((adv == 0xFFFFFFFFu) || (adv < 1)) adv = 1;
	vclock += adv;
    }
    else if (!interactive && !anyd) {
	// Wait for the next event: a timer deadline, or a frame on the bus.
	// Only once the cycle SETTLED. A cycle that changed something has left
	// work that is runnable right now, and sleeping through it delays output
	// that has nothing to do with any timer: the step out of INIT is such a
	// change, so `#in NORMAL` first became eligible on the cycle AFTER the
	// sleep -- a program whose only timer was 2 s printed nothing for two
	// seconds and then everything at once. anyd is already computed for the
	// continue-test below; this is the same question asked earlier.
	int tmo = (state.es.wait_ms != NOTIMEOUT) ? (int)state.es.wait_ms : -1;
	// The count TESTED is the one with the UDP sockets in it, not the fixed
	// part: with no CAN and no prompt they are the only thing there is to
	// wait on. Without them this branch fell through to `tmo > 0` with
	// tmo == -1 and did not wait at all -- a program whose only input is a
	// datagram spun a core flat between packets, 2.0 s of CPU per 2.0 s of
	// waiting, measured.
	nfds_t pn = poll_set(pfd, nfds, pfd_max);
	if (csp_io_active(&state) && (pn > 0)) {
	    // Bounded even when a frame would wake us, so -T still expires
	    // while the bus is quiet.
	    if ((tmo < 0) || (tmo > 100)) tmo = 100;
	    poll(pfd, pn, tmo);
	}
	else if (tmo > 0)
	    poll(NULL, 0, tmo);
    }

    // Continue loop if: interactive mode, pending changes, timers, or reactive queue
    if (interactive) goto loop;
    // A line still in hand outrunning the program: after Ctrl-D the prompt is
    // gone but a paste can still be queued, and a program that has already
    // settled would otherwise exit on top of it.
    if (state.line.ready || state.line.fill) goto loop;
    if (virtual_time && !input_done) goto loop;  // more input rows to feed
    if (anyd) goto loop;
    if (state.es.wait_ms != NOTIMEOUT) goto loop;
    if (csp_io_active(&state)) goto loop;   // a frame may still arrive
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (state.reactive && csp_pending(&state)) goto loop;
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
