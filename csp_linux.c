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

// SocketCAN is a Linux kernel facility; nothing else has it.
#if defined(__linux__) && !defined(CSP_NO_SOCKETCAN)
#define CSP_HAS_SOCKETCAN 1
#endif

#include "csp.h"
#include "csp_print.h"
#include "csp_dump.h"
#include "csp_boards.h"   // generated: make boards

#include <sys/time.h>

// Interactive mode globals
static struct termios orig_termios;
static int raw_mode = 0;
static const char* eeprom_file = "eeprom.db";

static const char* can_iface = NULL;   // --can=vcan0; NULL = no bus, stubs
static const char* src_file = NULL;   // first .csp on the command line (ROM banner)

// git version, injected by the Makefile; a plain build still says something.
#ifndef CSP_VERSION
#define CSP_VERSION "unknown"
#endif

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
    if (file_output) {
	if (fputc(c, file_output) == EOF)
	    return 0;
    }
    return 1;
}

int csp_print_str(const char* s)
{
    if (file_output)
	return fprintf(file_output, "%s", s);
    return strlen(s);
}

int csp_print_rostr(rostring_t s)
{
    return csp_print_str((const char*) s);
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

static void serial_poll(csp_rt_t* st, struct pollfd* fds, nfds_t nfds)
{
    if (nfds > 0 && (fds[0].revents & POLLIN)) {
	char c;
	while (!st->line_ready && read(STDIN_FILENO, &c, 1) == 1) {
	    if (c == 4) { // Ctrl-D
		quit_flag = 1;
		return;
	    }
	    csp_line_input(st, c);
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

// ============================================================
// CAN backend
//
// SocketCAN when a --can interface was given, otherwise a no-op stub so a
// program with CAN declarations still parses, runs and can be inspected on a
// machine with no bus. Test with a virtual interface:
//   sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
//   ./csp --can=vcan0 prog.csp
// ============================================================

#if defined(CSP_HAS_SOCKETCAN)
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
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
    (void)st; (void)id; (void)data; (void)len;
    return 0;
}
int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st; (void)id; (void)data; (void)len;
    return 0;
}
#endif

void csp_setup(csp_rt_t* st)
{
    time_init();
    csp_can_init(st);
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
    csp_input_timer(st);
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
    }
    csp_output_timer(st);
}

int parse_file(csp_rt_t* st, FILE* fin)
{
    char buf[MAX_LINE_SIZE];
    const tstr_t empty = { .ptr = NULL, .len = 0};
    csp_pmark_t pm;

    st->ps.line = 1;
    while(fgets(buf, MAX_LINE_SIZE, fin)) {
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
    csp_new_decl(st, &empty, DECL_END, 0);
    return 0;
}

void print_defines()
{
    printf("SUPPORT_REACTIVE=%d\n", SUPPORT_REACTIVE);
    printf("USE_STATISTICS=%d\n",USE_STATISTICS);
    printf("REACTIVE_DEFAULT=%d\n", REACTIVE_DEFAULT);
    printf("OP_AVAIL=%d\n", OP_AVAIL);  // next available = #opcodes
    printf("DECL_AVAIL=%d\n", DECL_AVAIL);  // next available = #decls
    printf("T_LAST=%d\n", T_LAST);        // #tokens
    printf("D_LAST=%d\n", D_LAST);        // #dtok
    printf("PART_LAST=%d\n", PART_LAST);  // <= 16 (4-bit max)
    printf("MAX_NAME_LEN=%d\n", MAX_NAME_LEN);
    printf("MAX_ARGS=%d\n", MAX_ARGS);

    printf("OBJ_BITS=%d\n", OBJ_BITS);
    printf("DECL_BITS=%d\n", DECL_BITS);
    printf("INDEX_BITS=%d\n", INDEX_BITS);
    printf("STRING_BITS=%d\n", STRING_BITS);
    printf("MAX_INDICES=%d\n", MAX_INDICES);
    printf("MAX_INSTRS=%d\n", MAX_INSTRS);
    printf("MAX_DECLS=%d\n", MAX_DECLS);
    printf("MAX_MODULES=%d\n", MAX_MODULES);
    printf("MAX_OBJECTS=%d\n", MAX_OBJECTS);
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
    
    printf("sizeof(csp_decl_t) = %ld\n", sizeof(csp_decl_t));
    printf("sizeof(csp_module_t) = %ld\n", sizeof(csp_module_t));
    printf("sizeof(csp_object_t) = %ld\n", sizeof(csp_object_t));
    printf("sizeof(csp_variable_t) = %ld\n", sizeof(csp_variable_t));
    printf("sizeof(csp_constant_t) = %ld\n", sizeof(csp_constant_t));
    printf("sizeof(csp_digital_t) = %ld\n", sizeof(csp_digital_t));
    printf("sizeof(csp_analog_t) = %ld\n", sizeof(csp_analog_t));
    printf("sizeof(csp_field_t) = %ld\n", sizeof(csp_field_t));    
    printf("sizeof(csp_bufdecl_t) = %ld\n", sizeof(csp_bufdecl_t));    
    printf("sizeof(csp_timer_t) = %ld\n", sizeof(csp_timer_t));

    printf("sizeof(csp_instr_t) = %ld\n", sizeof(csp_instr_t));
    printf("sizeof(csp_instr_enter_t) = %ld\n", sizeof(csp_instr_enter_t));
    printf("sizeof(csp_instr_leave_t) = %ld\n", sizeof(csp_instr_leave_t));
    printf("sizeof(csp_instr_new_t) = %ld\n", sizeof(csp_instr_new_t));
    printf("sizeof(csp_instr_imm_t) = %ld\n", sizeof(csp_instr_imm_t));        
    printf("sizeof(csp_instr_mem_t) = %ld\n", sizeof(csp_instr_mem_t));
    printf("sizeof(csp_instr_memi_t) = %ld\n", sizeof(csp_instr_memi_t));    
    printf("sizeof(csp_instr_call_t) = %ld\n", sizeof(csp_instr_call_t));
    printf("sizeof(csp_instr_rule_t) = %ld\n", sizeof(csp_instr_rule_t));
    printf("sizeof(csp_instr_next_t) = %ld\n", sizeof(csp_instr_next_t));
    printf("sizeof(csp_instr_instate_t) = %ld\n", sizeof(csp_instr_instate_t));
    printf("sizeof(csp_instr_alu_t) = %ld\n", sizeof(csp_instr_alu_t));
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
    {"board",        required_argument, 0,  1000},
    {"can",          required_argument, 0,  1001},
    {"no-eeprom",    no_argument,       0,  1002},
    {"prefix",       required_argument, 0,  1003},
    {"role",         required_argument, 0,  1004},
    {"generation",   required_argument, 0,  1005},
    {"memory",       required_argument, 0,  'm'},
    {"pause",        no_argument,       0,  'b'},
    {0,              0,                 0,  0 }
};

void usage(const char* prog)
{
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
    fprintf(stderr, "  -P, --debug-parse    Enable parser debugging\n");
    fprintf(stderr, "  -S, --debug-scan     Enable tokenizer debugging\n");
    fprintf(stderr, "  -Q, --debug-trace    Enable variable tracing\n");
    fprintf(stderr, "  -R, --debug-result   Add result to tracing (Erl)\n");
    fprintf(stderr, "  -s, --state-file=F   State file (Erlang format)\n");
    fprintf(stderr, "  -p, --parse-file=F   Parsed structure file\n");
    fprintf(stderr, "  -e, --eeprom=F       EEPROM file for save/load (default: eeprom.db)\n");
    fprintf(stderr, "      --no-eeprom      Do not overlay the saved EEPROM patches at boot\n");
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
}

char    input_buf[MAX_LINE_SIZE];
token_t input_tv[MAX_LINE_TOKENS];
size_t  input_num = 0;
uint32_t input_cycle = 0;
int     input_delay = 0;

// tv[0] is cycle count tv[1] may be delay
void cycle_input_values(csp_rt_t* st, token_t* tv, size_t num)
{
    int i = 1;

    input_delay = 0;
    if (tv[1].t == INT) {
	i = 2;
	input_delay = tv[1].v.val.i;
    }
    while(i < num) {
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

int input_applied = 1;   // virtual mode: has the loaded row been applied?
int input_done = 0;      // virtual mode: input file exhausted

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


int main(int argc, char** argv)
{
    csp_rt_t state;
    int r;
    index_t x;
    FILE* fin = stdin;
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
    struct pollfd pfd[2];
    nfds_t nfds = 0;
    int can_slot = 0;   // index of the CAN socket in pfd (0 = not polled)
    csp_lang_t lang = TEXT;
    int first_cycle = 1;
    int given = 0;    // was a program handed to us (file or stdin)?
    int anyd;

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
	case 'i': interactive = 1; break;
	case 'b': pause_start = 1; interactive = 1; break;  // pause needs the REPL
	case 'e': eeprom_file = optarg; break;
	case 1001: can_iface = optarg; break;
	case 1002: no_eeprom = 1; break;
	case 1003: rom_prefix = optarg; break;   // --prefix: symbol prefix for -C
	case 1004:                               // --role: what the image is for
	    rom_role = (strcmp(optarg, "failsafe") == 0) ? CSP_ROLE_FAILSAFE
							 : CSP_ROLE_ROM;
	    break;
	case 1005: rom_generation = atoi(optarg); break;
	case 'r': reactive = 1; break;   // -r: enable reactive mode (no argument)
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

    csp_rt_init(&state, reactive);
    // -m shrinks the usable code-memory budget to exercise the out-of-memory
    // path. Clamp to what csp_mem_init left for the pool, NOT to mem_size: the
    // line buffer sits in the gap between the two, and raising mem_limit back to
    // the physical size would let decl[] grow down into it.
    if (mem_limit > 0) {
	size_t pool = state.mem_size - state.line_size;
	state.mem_limit = (mem_limit < pool) ? mem_limit : pool;
    }
    csp_set_uconst(&state, csp_uconst);

    // Activate flash-resident firmware: run ROM in place from flash, RAM holds
    // patches. Skip when compiling (-C) so the dump is exactly the parsed program.
    if (!compile)
	csp_load_rom(&state);

    // Parse input files (if any)
    if (optind < argc) {
	src_file = argv[optind];   // first one, for the ROM provenance banner
	while (optind < argc) {
	    if ((fin = fopen(argv[optind], "r")) == NULL) {
		fprintf(stderr, "unable to open file '%s'\n", argv[optind]);
		exit(1);
	    }
	    if ((r = parse_file(&state, fin)) < 0) {
		fprintf(stderr, "%s:%d ", argv[optind], state.ps.line);
		print_error(&state);
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
	if ((r = parse_file(&state, stdin)) < 0) {
	    fprintf(stderr, "*stdin*:%d ", state.ps.line);
	    print_error(&state);
	    exit(1);
	}
    }

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
	}
	csp_setup(&state);
    }

    if (debug_parse) {
	csp_dump(parse_out, &state);
	csp_list_rules(parse_out, &state);
    }

    if (compile) {
	FILE* objf = object_file == NULL ? stdout : object_file;
	csp_rom_meta_t meta;
	meta.src     = src_file;
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
	poll(pfd, nfds, 0);

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
	
	if (interactive)
	    csp_line_prompt(&state);
	timeout_ms = interactive ? 100 : 0;
	// Wait for timer if needed (non-interactive mode)
	if (state.wait_ms != NOTIMEOUT) {
	    if (timeout_ms == 0 || state.wait_ms < (uint32_t)timeout_ms)
		timeout_ms = state.wait_ms;
	}
	poll(pfd, nfds, timeout_ms);
	serial_poll(&state, pfd, nfds);

	if (state.line_ready) {
	    process_serial_line(&state, state.line_buf);
	    state.line_ready = 0;
	    state.line_pos = 0;
	    if (quit_flag) goto done;
	}
    }

    // /pause freezes execution: keep servicing interactive input (above) so
    // /resume and edits still work, but run no input/cycle/commit/output.
    if (state.paused)
	goto loop;

    csp_input(&state);
    if (input_file) {
	if (cycle_input(&state, input_file) < 0)
	    input_done = 1;
    }

    // /live freezes the rules but keeps I/O running (poke outputs, watch inputs).
    x = state.live ? BAD_INDEX : csp_cycle(&state);  // ROM (seq) + RAM, one model

    anyd = state.anyd;  // save before commit clears it

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
	uint32_t adv = (state.wait_ms == NOTIMEOUT) ? 0xFFFFFFFFu : state.wait_ms;
	if (!input_done && !input_applied && (input_cycle > vclock)) {
	    uint32_t inp = input_cycle - vclock;
	    if (inp < adv) adv = inp;
	}
	if ((adv == 0xFFFFFFFFu) || (adv < 1)) adv = 1;
	vclock += adv;
    }
    else if (!interactive) {
	// Wait for the next event: a timer deadline, or a frame on the bus.
	int tmo = (state.wait_ms != NOTIMEOUT) ? (int)state.wait_ms : -1;
	if (csp_can_active(&state) && (nfds > 0)) {
	    // Bounded even when a frame would wake us, so -T still expires
	    // while the bus is quiet.
	    if ((tmo < 0) || (tmo > 100)) tmo = 100;
	    poll(pfd, nfds, tmo);
	}
	else if (tmo > 0)
	    poll(NULL, 0, tmo);
    }

    // Continue loop if: interactive mode, pending changes, timers, or reactive queue
    if (interactive) goto loop;
    if (virtual_time && !input_done) goto loop;  // more input rows to feed
    if (anyd) goto loop;
    if (state.wait_ms != NOTIMEOUT) goto loop;
    if (csp_can_active(&state)) goto loop;   // a frame may still arrive
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
