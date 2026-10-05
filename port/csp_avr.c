// CandySpeak on a megaAVR -- ATmega328P or ATmega2560 -- with no Arduino core.
//
// WHY. Measured on `uno exec`, the core costs 3350 bytes -- HardwareSerial and
// its two ring buffers and ISR, Print, malloc/free, pinMode/digitalWrite with a
// PROGMEM lookup per call, the C++ static-init machinery -- on a part with
// 32256 bytes of flash that CandySpeak already overflows. The LPC and STM32
// ports are bare metal for the same reason: on a board we own end to end, a
// portability layer for three pins and a UART is not portability, it is weight.
//
// WHAT IS KEPT. The Arduino PIN NUMBERING, so board terms and programs written
// against `uno` do not change: 0..7 are PORTD, 8..13 PORTB, 14..19 PORTC. That
// mapping is three comparisons here and was a PROGMEM table read per call
// there.
//
// On the ATmega2560 it is a table, because there the numbering IS arbitrary --
// pin 4 is PG5 and pin 5 is PE3. See pin_port below. Which part is being built
// for is decided by avr-gcc's own -mmcu define, so a board says nothing about
// it beyond naming its chip.
//
// WHAT IS NOT HERE, deliberately:
//   PWM      -- an #analog out is refused rather than half-driven. The slave
//               this port exists for reads pins and answers; nothing drives.
//               Adding it means owning TIMER1/TIMER2 per pin, and doing that
//               silently wrong is worse than saying no.
//   CAN      -- there is no controller on the part.
//   float    -- USE_FIXPOINT is 1 for __AVR__ (csp_config.h), so the arithmetic
//               is Q16.16 and no float helper should be linked. If one appears
//               in the map, something compared a fixpoint against a double
//               literal; see fsign in csp_rt.c for the one that did.
//
// UPLOAD is unchanged: `arduino-cli upload -i <hex>` takes a prebuilt image, so
// the bootloader on the part is still the way in.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>

// EVERY OTHER PORT DECLARES THIS, and this one did not.
//
// It is what tells the shared code it is running on a target rather than a
// workstation, and the sizes that follow from it are not decoration:
// MAX_LINE_TOKENS is 24 with it and 64 without, so csp_parse's `token_t
// tv[MAX_LINE_TOKENS]` was a HOST-sized array on a part with eight kilobytes of
// RAM -- and the parse path nests several of those.
//
// The symptom was a board that jumped to address 0 on `#variable x = 1` while
// /list, /memory and an immediate expression all worked: those do not go
// through csp_parse. See port/csp_arduino.c, csp_stm32.c and csp_lpcopen.c,
// which have all said this since they were written.
//
// BEFORE csp.h, because that is where the sizes are decided.
#define CSP_EMBEDDED 1

#include "csp.h"
#include "csp_line.h"
#include "csp_print.h"
#include "csp_io.h"
#include "csp_chip_io.h"

// DOES THIS BUILD HAVE A COMPILER? csp_rt_init wants its state, or NULL for a
// node that only runs images -- and the tier is the driver's decision, so it is
// spelled out here rather than guessed at further down. Same shape as
// port/csp_arduino.c, csp_lpcopen.c and csp_stm32.c.
#if defined(CSP_EXEC_ONLY)
#define CSP_CSTATE NULL
#else
#include "csp_compile.h"
#define CSP_CSTATE csp_cstate()
#endif

#ifndef F_CPU
#define F_CPU 16000000UL
#endif
#ifndef CSP_UART_BAUD
#define CSP_UART_BAUD 38400UL
#endif

// ============================================================
// Console
// ============================================================
//
// OUTPUT IS POLLED, INPUT IS NOT -- and the asymmetry is the point.
//
// Output can be polled because the writer is us: csp_print_char blocks until the
// UART takes the byte, which at 38400 is 260 us and only while it is printing.
// Nothing is lost by waiting.
//
// INPUT cannot, and the first version of this port tried. The receiver has TWO
// bytes of hardware buffer -- UDR0 and the shift register -- and the main loop
// leaves the read loop to run a whole cycle and to print a command's answer. A
// /state dump is fifteen lines; anything typed while it goes out has to survive
// in those two bytes, and a terminal in line mode delivers a whole command as
// one burst. What came back was a prompt that ate characters.
//
// So: an ISR and a ring. 32 bytes, which is eight cycles' worth at 38400 and
// more than a typed line needs to survive one dump. Still nothing like
// HardwareSerial -- there is no TX ring, no Print, no Stream.
//
// The ring is the ONLY thing the ISR and the loop share, so the discipline is
// the usual one: head is written by the ISR alone, tail by the loop alone, and
// each is a single byte so neither read tears.

#define UART_RX_RING 32                 // power of two: the mask below

static volatile uint8_t rx_buf[UART_RX_RING];
static volatile uint8_t rx_head = 0;    // ISR writes
static volatile uint8_t rx_tail = 0;    // main loop writes

// DROPS THE NEW BYTE WHEN FULL, and says nothing. There is no back-pressure to
// apply from inside an interrupt -- the sender is a person or another board and
// neither is listening -- and a ring that overwrites its oldest byte would
// corrupt the front of a line rather than the end of it.
// THE VECTOR HAS TWO NAMES. A part with one USART calls it USART_RX_vect; one
// with four numbers them, and USART0_RX_vect is the console's. Getting it wrong
// is not a link error -- avr-gcc emits an ordinary function with that name, the
// vector table keeps its default handler, and the ISR simply never runs. The
// console then goes deaf with nothing to see. Only -Wmisspelled-isr says so,
// which is one more reason this port is built with -Wall.
#if defined(USART0_RX_vect)
#define CSP_UART_RX_VECT USART0_RX_vect
#else
#define CSP_UART_RX_VECT USART_RX_vect
#endif

ISR(CSP_UART_RX_VECT, __attribute__((no_instrument_function)))
{
    uint8_t c = UDR0;                   // ALWAYS read: it clears RXC0
    uint8_t h = (uint8_t)((rx_head + 1) & (UART_RX_RING - 1));

    if (h != rx_tail) {
	rx_buf[rx_head] = c;
	rx_head = h;
    }
}

static void uart_init(void)
{
    uint16_t ubrr = (uint16_t)((F_CPU / (16UL * CSP_UART_BAUD)) - 1);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);       // 8N1
}

static int uart_available(void)
{
    return (rx_head != rx_tail);
}

static uint8_t uart_read(void)
{
    uint8_t c;

    if (rx_head == rx_tail)
	return 0;
    c = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1) & (UART_RX_RING - 1));
    return c;
}

int csp_print_char(char c)
{
    // The console tap, first thing and non-consuming -- see csp_console.c. At
    // CSP_CONSOLE_BYTES 0 it compiles to nothing.
    csp_repl_tap(c);
    // CR belongs here and nowhere else: the runtime ends a line three ways and
    // all three arrive as '\n'. Translating anywhere else leaves half the
    // output bare LF.
    if (c == '\n') {
	while (!(UCSR0A & (1 << UDRE0)))
	    ;
	UDR0 = '\r';
    }
    while (!(UCSR0A & (1 << UDRE0)))
	;
    UDR0 = (uint8_t)c;
    return 1;
}

int csp_print_str(const char* s)
{
    int n = 0;
    while (*s)
	n += csp_print_char(*s++);
    return n;
}

// THE OUTPUT SINK, which on a board is one UART and nothing else. The host has
// a real FILE* here and /dump can redirect it; the value is opaque to the
// runtime and only ever tested against NULL, so it is kept as what it is.
//
// A void*, not a long. On AVR a pointer is TWO bytes and a long is four, so
// round-tripping one through the other is a narrowing cast in one direction and
// a widening one back -- which gcc says out loud (-Wpointer-to-int-cast) and
// which has no reason to exist when nothing here inspects the value.
//
// Only reachable from the REPL side, but not #ifdef'd out: an empty translation
// unit is cheaper to keep whole than to guard, and --gc-sections drops both when
// nothing calls them.

static void* serial_output = NULL;

void* csp_set_file_output(void* f)
{
    void* prev = serial_output;

    serial_output = f;
    return prev;
}

int csp_will_output(void)
{
    return (serial_output != NULL);
}

// AVR: a rostring lives in FLASH and cannot be dereferenced with a data
// pointer. Every other port can; this is the one that has to read it a byte at
// a time through ro_byte.
int csp_print_rostr(rostring_t s)
{
    rochar* p = (rochar*)s;
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
    // Nothing is buffered -- csp_print_char waits for the shift register -- so
    // this waits for the LAST byte and nothing else.
    while (!(UCSR0A & (1 << TXC0)) && !(UCSR0A & (1 << UDRE0)))
	;
}

// ============================================================
// Board
// ============================================================
//
// Pins, the tick and the ADC are the chip's (chips/atmel/drivers/avr), and
// the sweep over them is port/csp_io.c. What is left here is the console.

void csp_board_init(void)
{
    csp_chip_init();
    uart_init();
    sei();
}

uint32_t csp_time_ms(void)
{
    return csp_chip_millis();
}

// ============================================================
// CAN
// ============================================================
//
// There is none on this part, and the stubs are here rather than absent so a
// program that names a bus still LINKS and RUNS -- it simply never delivers,
// `.rx` stays false, and the rules guarded on it do not fire. That is the same
// contract every other port gives, and it is what lets one program be written
// against a board with a transceiver and moved to one without.

int csp_can_init(csp_rt_t* st) { (void)st; return 0; }

int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    (void)st; (void)id; (void)data; (void)len;
    return 0;
}

int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st; (void)id; (void)data; (void)len;
    return -1;
}

// ============================================================
// EEPROM -- the part's own kilobyte
// ============================================================
//
// 1024 bytes on chip, and free: no I2C part, no flash sector to erase. That is
// what lets `--id` and a saved setting survive a power cycle on a node with
// nothing else attached.

#if !defined(CSP_NO_EEPROM)

static uint16_t ee_addr = 0;

int csp_eeprom_open_read(void)  { ee_addr = 0; return 0; }
int csp_eeprom_open_write(void) { ee_addr = 0; return 0; }
void csp_eeprom_close(void) { }
uint32_t csp_eeprom_capacity(void) { return E2END + 1UL; }

int csp_eeprom_read(void* buf, size_t n)
{
    uint8_t* p = (uint8_t*)buf;
    size_t i;

    if ((ee_addr + n) > (E2END + 1UL))
	return -1;
    for (i = 0; i < n; i++) {
	while (EECR & (1 << EEPE))
	    ;
	EEAR = ee_addr++;
	EECR |= (1 << EERE);
	p[i] = EEDR;
    }
    return (int)n;
}

int csp_eeprom_write(const void* buf, size_t n)
{
    const uint8_t* p = (const uint8_t*)buf;
    size_t i;

    if ((ee_addr + n) > (E2END + 1UL))
	return -1;
    for (i = 0; i < n; i++) {
	uint8_t s;
	while (EECR & (1 << EEPE))
	    ;
	EEAR = ee_addr;
	EECR |= (1 << EERE);
	// A byte that already holds this value is NOT rewritten. An EEPROM cell
	// is good for ~100k writes and a settings store is saved whole, so the
	// bytes that did not change would take the wear of the ones that did.
	if (EEDR == p[i]) {
	    ee_addr++;
	    continue;
	}
	EEDR = p[i];
	// The two-step enable is a timed sequence: EEMPE must be set within
	// four cycles of EEPE, and an interrupt in between breaks it.
	s = SREG;
	cli();
	EECR |= (1 << EEMPE);
	EECR |= (1 << EEPE);
	SREG = s;
	ee_addr++;
    }
    return (int)n;
}

#endif /* !CSP_NO_EEPROM */

// The store has no filename on a board. /save and /load are shared code and
// just need something to print.
const char* csp_eeprom_name(void)
{
#if defined(CSP_NO_EEPROM)
    static const char nm[] = "none";
#else
    static const char nm[] = "EEPROM";
#endif
    return nm;
}

// ============================================================
// RAM
// ============================================================
//
// The arena is a static array (no CSP_ARENA_MALLOC here), so what is free is
// what the stack has not taken. __brkval is zero without malloc, which is why
// the heap start is the reference.

// THE runtime, declared here rather than beside main() because
// csp_system_ram_used below has to subtract its size and the pool's. Same
// placement as port/csp_lpcopen.c and port/csp_stm32.c, for the same reason.
static csp_rt_t state;

extern uint8_t __heap_start;
extern void* __brkval;

static uint32_t raw_free(void)
{
    uint8_t here;
    uint8_t* top = (__brkval == 0) ? &__heap_start : (uint8_t*)__brkval;
    return (uint32_t)(&here - top);
}

uint32_t csp_system_ram_capacity(void) { return RAMEND - RAMSTART + 1UL; }
uint32_t csp_system_ram_avail(void) { return raw_free(); }
// system = everything present that is NOT ours: minus our pool and our struct.
//
// The subtraction is the whole point, and leaving it out is what made a working
// board report `free 0`. raw_free() is measured from the top of .bss, and BOTH
// the static arena and `state` are in .bss -- so `capacity - raw_free()` already
// contains them. /memory then prints them AGAIN in their own `struct` and
// `buffers` rows, the accumulated total passes the part's RAM, and `free` clamps
// to zero on a board with hundreds of bytes to spare. Reading "out of RAM" off a
// board that is fine is the same class of wrong as the message it sits next to.
//
// mem_LIMIT, not mem_size: the line buffer and the history are carved off the
// top of the pool above the limit and have their own row.
//
// port/csp_lpcopen.c and port/csp_stm32.c have done exactly this since they were
// written; this port was the one that did not.
uint32_t csp_system_ram_used(void)
{
    uint32_t cap = csp_system_ram_capacity();
    uint32_t total, ours;

    if (cap == 0)
	return 0;
    total = cap - raw_free();
    ours  = (uint32_t)state.mem_limit + (uint32_t)sizeof(csp_rt_t);
    return (total > ours) ? (total - ours) : 0;
}

// ============================================================
// The cycle
// ============================================================

// The pins are swept by port/csp_io.c; the rest is the runtime's.

void csp_setup(csp_rt_t* st)
{
    csp_io_sweep(st, CSP_IO_CONFIG);
    // AFTER the pin loop: arming an interrupt on a pin still at its reset
    // default arms it on whatever the pin happened to be.
    csp_setup_events(st);
}

void csp_input(csp_rt_t* st)
{
    csp_io_sweep(st, DIR_IN);
    csp_can_input(st);
    csp_buf_input(st);
    csp_input_timer(st);
    csp_input_event(st);
}

void csp_output(csp_rt_t* st)
{
    if (!st->latch) {
	csp_io_sweep(st, DIR_OUT);
	csp_can_output(st);
	csp_buf_output(st);
    }
    csp_output_timer(st);
}

// ============================================================
// main
// ============================================================

// The crash ring is COPIED before anything else runs, because everything else
// overwrites it. csp_board_init, csp_rt_init and the banner's own printing are
// all instrumented, so by the time there is a UART to report on, the twelve
// slots hold the boot -- not the crash. The first attempt printed hex_digit and
// csp_system_ram_capacity, which are the banner reporting on itself.
//
// main carries no_instrument_function for the same reason: its own entry would
// spend a slot before the copy could take it.
#ifdef CSP_STACK_WATCH
static void* crash_ring[CSP_CRASH_TRACE];
static uint8_t crash_dir[CSP_CRASH_TRACE];
static uint8_t crash_at;
static uint16_t crash_magic;
#endif

// ONE LETTER PER BOOT STEP, flushed as it goes. An exec-only node has no
// banner and no prompt, so "it did not come up" and "it came up and the
// program does nothing" looked the same -- and one of the steps below used to
// stop the board with a bare `for (;;);`, saying nothing at all.
//
// Letters and not words: csp_print_char is already linked, the sequence reads
// as one line, and where it STOPS is the answer.
//
//     csp i p r e b s run     a clean boot
//     csp i p r e b !         csp_rebuild refused -- no memory for the tables
//     csp i p r               stopped in csp_load_rom
#if defined(CSP_EXEC_ONLY)
#define BOOTMARK(c) do { csp_print_char(c); csp_flush(); } while (0)
#else
#define BOOTMARK(c) do { } while (0)
#endif

int main(void) __attribute__((no_instrument_function));

int main(void)
{
    index_t x;
#if !defined(CSP_EXEC_ONLY)    
    uint8_t why;
#endif

#ifdef CSP_STACK_WATCH
    {
	int k;
	for (k = 0; k < CSP_CRASH_TRACE; k++) {
	    crash_ring[k] = csp_crash_ring[k];
	    crash_dir[k]  = csp_crash_dir[k];
	}
	crash_at    = csp_crash_at;
	crash_magic = csp_crash_magic;
    }
#endif

    // WHY THE PART STARTED. MCUSR carries it across the reset and nothing else
    // does -- and it is the difference between "someone pressed reset" and "the
    // program jumped to address zero", which look identical from the outside.
    //
    // PORF power-on, EXTRF the reset pin, BORF brown-out, WDRF watchdog. ALL
    // ZERO means none of those happened: the part did not reset, it RAN to
    // address 0 -- a corrupted return address or a call through a null pointer.
    // That is a crash, and without this line it is indistinguishable from a
    // command that quietly did nothing.
    //
    // Read once and cleared, or the next boot reports this one's reason too.
    // CLEARED ON EVERY BUILD, printed only where there is a banner to print it
    // in. A reset flag left standing is read by the NEXT boot as its own reason
    // -- and WDRF left standing is worse than cosmetic: with it set, WDE cannot
    // be cleared, so a part that once reset on the watchdog keeps doing it.
    // Optiboot clears this before it jumps, but a part flashed by ISP or run in
    // a simulator has no Optiboot in front of it.
#if !defined(CSP_EXEC_ONLY)
    why = MCUSR;
#endif
    MCUSR = 0;

    csp_board_init();
#if defined(CSP_EXEC_ONLY)
    // A NODE THAT SAYS NOTHING CANNOT BE DEBUGGED. An exec-only node has no
    // banner -- there is no prompt for one to introduce -- and that made "the
    // board is dead" and "the board is running a program that does nothing"
    // the same observation. One mark here, as soon as there IS a UART, and one
    // below once the program is up: between them sit the ROM load, the EEPROM
    // patch and the rebuild, which is where a node that comes back dead stops.
    csp_print_lit("csp");
    csp_flush();
#endif
    // CSP_CSTATE, not 0. The third argument is the COMPILER's state, and a full
    // build has one -- every other port passes it. With NULL here csp_parse's
    // first statement is
    //
    //     st->cs->ap = &alloc;
    //
    // which writes at address 0 plus an offset. On AVR that is the register
    // file and the I/O space, where the STACK POINTER lives at 0x5D/0x5E --
    // so the write moved the stack and execution left for address 0.
    //
    // The symptom was `#variable x = 1` restarting the board while /list,
    // /memory and an immediate expression all worked: none of those reaches
    // csp_parse. It took a boot banner to see it was a restart at all.
    // THE RETURN IS CHECKED. csp_rt_init fails when the arena cannot be claimed,
    // and it leaves st->mem NULL and mem_limit 0 -- so carrying on means every
    // allocation downstream hands back a pointer into nothing. Every other port
    // tests this; ignoring it turns "out of memory" into arbitrary corruption.
    BOOTMARK('i');
    if (csp_rt_init(&state, REACTIVE_DEFAULT, CSP_CSTATE) < 0) {
	// No arena, so no prompt worth having -- say so and stop rather than
	// loop printing from a runtime that does not exist.
	csp_print_line("FATAL: csp_rt_init failed (out of memory)");
	csp_flush();
	for (;;)
	    ;
    }

    // WHICH image, before one is loaded. sys.Boot lives in the settings store,
    // so the store is read on its own first -- the choice has to be made before
    // csp_load_rom picks. Same order as every other port.
    BOOTMARK('p');
    if (csp_eeprom_peek(&state) == 0)
	csp_boot_pick(&state);

    BOOTMARK('r');
    csp_load_rom(&state);
#if !defined(CSP_NO_EEPROM)
    // csp_clr_error on failure, and it is NOT cosmetic. "No saved state" is the
    // NORMAL case at boot, and csp_set_error keeps the FIRST error -- so an
    // uncleared ERR_CANNOT_LOAD is carried into every command that follows.
    //
    // What that looked like: `#variable n = 10` did nothing and said nothing,
    // and the next line reported "cannot load from eeprom". csp_parse_variable
    // opens with `if (st->ps.err != ERR_OK) return -1;`, so a stale error makes
    // every declaration fail at the guard, before it has looked at anything.
    //
    // Every other port has done this since it was written -- csp_arduino.c,
    // csp_lpcopen.c, csp_stm32.c. This one was the exception.
    BOOTMARK('e');
    if (csp_eeprom_load(&state) != 0)
	csp_clr_error(&state);
#endif
    // csp_rebuild, not csp_rt_start alone: rebuild resets the middle bump
    // allocator and lays every derived table out again. Calling start on its
    // own leaves them where the previous layout put them.
    BOOTMARK('b');
    if (csp_rebuild(&state) < 0) {
	// SAY SO. This was a bare `for (;;);` -- the one failure in the whole
	// boot that stopped the board without a word, which is exactly the
	// shape of a board that never came up at all.
	BOOTMARK('!');
	for (;;)
	    ;                                     // nothing can run: stop
    }
    BOOTMARK('s');
    csp_setup(&state);
    state.latch = 0;
#if defined(CSP_EXEC_ONLY)
    // UP. Two words and no numbers: csp_print_hex is not otherwise linked into
    // an exec-only image, and pulling it in for one line cost more than the
    // line is worth. What a host tool needs from here -- rom_fp, to fingerprint
    // an EEPROM patch -- it can read out of the image it built.
    csp_print_lit("run\n");
    csp_flush();
#endif

    // A BOOT LINE, and it is not decoration.
    //
    // This port printed nothing at startup, and an AVR that faults does not
    // stop -- it restarts. So a crash looked EXACTLY like a command that did
    // nothing: silence, then a fresh prompt. `#variable n = 10` was silent for
    // an afternoon before it turned out the board was resetting on it, and the
    // one thing that would have said so is this line.
    //
    // Free RAM as well as the sizes, because the first suspect for a fault in
    // the parser is the stack: it grows down from RAMEND toward .bss, and what
    // is between them is this number.
#if !defined(CSP_EXEC_ONLY)
    csp_print_lit("\nCandySpeak ");
    csp_print_str(CSP_BOARD_NAME);
    csp_print_lit(" -- RAM ");
    csp_print_uint(csp_system_ram_capacity());
    csp_print_lit(", free ");
    csp_print_uint(raw_free());
    csp_print_lit(", pool ");
    csp_print_uint((uint32_t)state.mem_size);
    csp_print_lit(", reset ");
    if (why == 0)
	csp_print_lit("NONE (jumped to 0 -- a CRASH)");
    else {
	if (why & (1 << PORF))  csp_print_lit("power ");
	if (why & (1 << EXTRF)) csp_print_lit("external ");
	if (why & (1 << BORF))  csp_print_lit("brown-out ");
	if (why & (1 << WDRF))  csp_print_lit("watchdog ");
    }
    csp_println();
#ifdef CSP_STACK_WATCH
    // WHERE it died, carried across the reset in .noinit. Only meaningful when
    // main has run before -- on the very first boot these bytes are whatever
    // the RAM powered up holding.
    //
    // A BYTE address, which is what avr-gcc hands the instrument hook and what
    // avr-nm prints, so the two compare directly:
    //     make -f Makefile.board BOARD=mega_bare whichfn ADDR=0x...
    if (crash_magic == CSP_CRASH_MAGIC) {
	int k;
	// NEWEST FIRST, and these are WORD addresses -- what a function pointer
	// is on this architecture. avr-nm prints byte addresses, so double them:
	//     make -f Makefile.board BOARD=mega_bare whichfn ADDR=0x...
	// prints both scales for exactly this reason.
	// OLDEST FIRST, so it reads like a call trace. `>` entered, `<` returned;
	// the last `>` with no matching `<` is the frame whose return never
	// happened. Word addresses -- double them for avr-nm.
	csp_print_line("call trace before the crash (word addresses):");
	for (k = 0; k < CSP_CRASH_TRACE; k++) {
	    uint8_t i = (uint8_t)((crash_at + k) % CSP_CRASH_TRACE);
	    if (crash_ring[i] == NULL)
		continue;
	    // Two calls, not a ternary: csp_print_lit declares a static RODATA
	    // array from its argument, so the argument must be a literal.
	    if (crash_dir[i]) csp_print_lit("  > ");
	    else              csp_print_lit("  < ");
	    csp_print_hex((uvalue_t)(uintptr_t)crash_ring[i]);
	    csp_println();
	}
    }
    else {
	int k;
	for (k = 0; k < CSP_CRASH_TRACE; k++) {
	    csp_crash_ring[k] = NULL;
	    csp_crash_dir[k] = 0;
	}
	csp_crash_at = 0;
    }
    csp_crash_magic = CSP_CRASH_MAGIC;
#endif
    csp_flush();
#endif

    for (;;) {
#if !defined(CSP_EXEC_ONLY)
	// THE PROMPT FIRST, and unconditionally: a board whose program cannot
	// run still has to be typeable at, or there is no way to /clear out of
	// whatever stopped it.
	if (!state.line.ready)
	    csp_line_prompt(&state.line);
#endif
#if !defined(CSP_AVR_NO_CONSOLE_IN)
	// THE LOCAL CONSOLE, and it is optional. A node driven only over a
	// route has no keyboard on it -- the typing happened at the other end
	// -- so -DCSP_AVR_NO_CONSOLE_IN leaves the port write-only.
	//
	// Keep draining while there is room, INCLUDING past a completed line:
	// that spare room is what absorbs a paste while the line before it is
	// still being run.
	while (uart_available() && csp_line_space(&state.line) && csp_con_space())
	    csp_con_input(&state, (char)uart_read());
#endif
#if !defined(CSP_EXEC_ONLY)
	// AND THEN RUN IT. Without this the console echoes and nothing else
	// happens: the line is assembled, marked ready, and never looked at --
	// which also means --gc-sections drops the whole compiler, so the image
	// looks suspiciously small and the board looks suspiciously dead.
	if (state.line.ready) {
	    csp_process_line(&state, state.line.buf);
	    csp_line_done(&state.line);
	}
#endif
	// Routes still run while paused: they move bytes between transports and
	// touch nothing the pause protects. /upgrade pauses the node for the
	// write, so without this a node being upgraded OVER a route stops
	// reading the link its image is arriving on.
	if (state.paused) {
	    csp_route_run(&state);
	    continue;
	}
#ifdef CSP_AVR_TICK_MARK
	// A DOT PER SECOND, straight off csp_time_ms. Not a feature -- build
	// with -DCSP_AVR_TICK_MARK when a board boots ("csp i p r e b s run")
	// and then does nothing. Dots mean TIMER0's compare interrupt is
	// running and the clock a #timer waits on is moving; silence means it
	// is not, and no timeout can ever come due.
	{
	    static uint32_t tick_last;
	    uint32_t tick_now = csp_time_ms();

	    if ((uint32_t)(tick_now - tick_last) >= 1000) {
		tick_last = tick_now;
		csp_print_char('.');
		csp_flush();
	    }
	}
#endif
	state.cycle++;
	csp_input(&state);
	x = state.live ? BAD_INDEX : csp_cycle(&state);   // /live: I/O, no rules
	(void)x;
	csp_commit(&state);
	csp_output(&state);
    }
    return 0;
}
