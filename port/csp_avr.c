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
// Pins
// ============================================================
//
// Arduino numbering, computed rather than looked up. The core keeps three
// PROGMEM tables (port, bit mask, timer) and reads all three on every
// digitalWrite; here the part's ports are contiguous and the answer is
// arithmetic.
//
// Returned as a pointer to the PORT register: DDR is one below it and PIN two
// below, which is true for every port on this part and is what lets one lookup
// serve read, write and direction.

#if defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
#define CSP_AVR_MEGA 1
#endif

#if defined(CSP_AVR_MEGA)

// THE MEGA IS A TABLE, and there is no way around it: its numbering is not
// arithmetic in any port. Pin 4 is PG5 with pin 3 on PE5 and pin 5 on PE3, and
// the analog block starts at 54. The board silkscreen is the only authority,
// which is why the core carries three PROGMEM tables for it.
//
// This is ONE table -- port index in the high nibble, bit in the low, a byte a
// pin -- and one table of port ADDRESSES beside it. The addresses come from
// &PORTx rather than from arithmetic on the datasheet's map: PORTA..PORTG sit
// three bytes apart from 0x22 and PORTH..PORTL three bytes apart from 0x102,
// with nothing between, and hand-computing across that gap is a way to write a
// plausible wrong pointer. Let the compiler say where the registers are.
//
// Transcribed from the core's variants/mega/pins_arduino.h, and checked against
// both of its tables entry by entry.
//
// pgm_read_* is LPM, which reaches the first 64K of flash only -- and a full
// image on this part is 116K. It is safe because the AVR linker script puts
// .progmem.data at the START of .text, right behind the vectors: every PROGMEM
// object in this image lives below 0x1800. It stops being safe the day the
// PROGMEM data ITSELF passes 64K, which is a different thing from the image
// doing so.
static uint8_t* const port_addr[] PROGMEM = {
    (uint8_t*)&PORTA, (uint8_t*)&PORTB, (uint8_t*)&PORTC, (uint8_t*)&PORTD,
    (uint8_t*)&PORTE, (uint8_t*)&PORTF, (uint8_t*)&PORTG, (uint8_t*)&PORTH,
    (uint8_t*)&PORTJ, (uint8_t*)&PORTK, (uint8_t*)&PORTL
};
#define PA_ 0x00
#define PB_ 0x10
#define PC_ 0x20
#define PD_ 0x30
#define PE_ 0x40
#define PF_ 0x50
#define PG_ 0x60
#define PH_ 0x70
#define PJ_ 0x80
#define PK_ 0x90
#define PL_ 0xA0

static const uint8_t pin_map[] PROGMEM = {
    PE_|0, PE_|1, PE_|4, PE_|5, PG_|5, PE_|3, PH_|3, PH_|4,   //  0..7
    PH_|5, PH_|6, PB_|4, PB_|5, PB_|6, PB_|7,                 //  8..13
    PJ_|1, PJ_|0, PH_|1, PH_|0,                               // 14..17  serial
    PD_|3, PD_|2, PD_|1, PD_|0,                               // 18..21
    PA_|0, PA_|1, PA_|2, PA_|3, PA_|4, PA_|5, PA_|6, PA_|7,   // 22..29
    PC_|7, PC_|6, PC_|5, PC_|4, PC_|3, PC_|2, PC_|1, PC_|0,   // 30..37
    PD_|7, PG_|2, PG_|1, PG_|0,                               // 38..41
    PL_|7, PL_|6, PL_|5, PL_|4, PL_|3, PL_|2, PL_|1, PL_|0,   // 42..49
    PB_|3, PB_|2, PB_|1, PB_|0,                               // 50..53  SPI
    PF_|0, PF_|1, PF_|2, PF_|3, PF_|4, PF_|5, PF_|6, PF_|7,   // 54..61  A0..A7
    PK_|0, PK_|1, PK_|2, PK_|3, PK_|4, PK_|5, PK_|6, PK_|7    // 62..69  A8..A15
};

// The Arduino pin that IS analog channel 0. A program may name either.
#define CSP_AVR_A0 54

static volatile uint8_t* pin_port(uint8_t pin, uint8_t* bit)
{
    uint8_t v;

    if (pin >= (uint8_t)sizeof(pin_map))
	return 0;
    v = pgm_read_byte(&pin_map[pin]);
    *bit = (uint8_t)(v & 15);
    return (volatile uint8_t*)pgm_read_word(&port_addr[v >> 4]);
}

#else  // ATmega328P and anything else shaped like it

#define CSP_AVR_A0 14

static volatile uint8_t* pin_port(uint8_t pin, uint8_t* bit)
{
    if (pin < 8)  { *bit = pin;        return &PORTD; }
    if (pin < 14) { *bit = pin - 8;    return &PORTB; }
    if (pin < 20) { *bit = pin - 14;   return &PORTC; }
    return 0;
}

#endif

#define DDR_OF(p)  (*((p) - 1))
#define PIN_OF(p)  (*((p) - 2))

static void pin_mode_out(uint8_t pin)
{
    uint8_t b;
    volatile uint8_t* p = pin_port(pin, &b);
    if (p) DDR_OF(p) |= (uint8_t)(1u << b);
}

static void pin_mode_in(uint8_t pin, int pullup)
{
    uint8_t b;
    volatile uint8_t* p = pin_port(pin, &b);
    if (!p) return;
    DDR_OF(p) &= (uint8_t)~(1u << b);
    if (pullup) *p |= (uint8_t)(1u << b);
    else        *p &= (uint8_t)~(1u << b);
}

static void pin_write(uint8_t pin, int on)
{
    uint8_t b;
    volatile uint8_t* p = pin_port(pin, &b);
    if (!p) return;
    if (on) *p |= (uint8_t)(1u << b);
    else    *p &= (uint8_t)~(1u << b);
}

static int pin_read(uint8_t pin)
{
    uint8_t b;
    volatile uint8_t* p = pin_port(pin, &b);
    if (!p) return 0;
    return (PIN_OF(p) & (uint8_t)(1u << b)) ? 1 : 0;
}

// ============================================================
// Time
// ============================================================
//
// TIMER0 in CTC at 1 kHz, so the tick IS a millisecond and there is no
// fractional accumulator to carry. The Arduino core runs TIMER0 in overflow
// mode at 1024 us and corrects with a remainder every 41 ticks, which is more
// code and exists so the same timer can also do PWM on pins 5 and 6. This port
// has no PWM, so the timer is free to be exact.

static volatile uint32_t ms_ticks = 0;

// no_instrument_function, and it matters more than it looks.
//
// -finstrument-functions instruments interrupt handlers too, and this one fires
// a thousand times a second -- so "the last function entered" was ALWAYS this
// one, whatever the program was really doing when it faulted. The crash marker
// recorded the tick and nothing else.
//
// avr-libc's ISR macro passes its second argument through as attributes, which
// is the only place to put this: the function has no declaration of its own.
ISR(TIMER0_COMPA_vect, __attribute__((no_instrument_function)))
{
    ms_ticks++;
}

static void time_init(void)
{
    TCCR0A = (1 << WGM01);                        // CTC
    TCCR0B = (1 << CS01) | (1 << CS00);           // /64 -> 250 kHz
    OCR0A  = (uint8_t)((F_CPU / 64UL / 1000UL) - 1);  // 249 -> 1 kHz
    TIMSK0 = (1 << OCIE0A);
}

uint32_t csp_time_ms(void)
{
    uint32_t v;
    uint8_t s = SREG;
    cli();
    v = ms_ticks;                                 // four bytes: not atomic
    SREG = s;
    return v;
}

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
// ADC
// ============================================================

static void adc_init(void)
{
    ADMUX  = (1 << REFS0);                        // AVcc reference
    // /128 -> 125 kHz at 16 MHz, inside the 50-200 kHz the datasheet wants for
    // full 10-bit accuracy. Faster prescalers trade bits for time, and this
    // part has time.
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

static uint16_t adc_read(uint8_t ch)
{
#if defined(CSP_AVR_MEGA)
    // SIXTEEN channels, and the sixth mux bit is not in ADMUX -- MUX5 lives in
    // ADCSRB. Writing only the low bits reads channel ch-8 instead, which is a
    // real reading off the wrong pin and therefore the hardest kind to notice.
    ADCSRB = (uint8_t)((ADCSRB & (uint8_t)~(1 << MUX5)) |
		       ((ch & 8) ? (uint8_t)(1 << MUX5) : 0));
    ADMUX  = (uint8_t)((ADMUX & 0xE0) | (ch & 0x07));
#else
    ADMUX = (uint8_t)((ADMUX & 0xF0) | (ch & 0x0F));
#endif
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC))
	;
    return ADC;
}

// ============================================================
// Board hooks
// ============================================================

void csp_board_init(void)
{
    time_init();
    uart_init();
    adc_init();
    sei();
}

void csp_board_setup(csp_rt_t* st) { (void)st; }
void csp_board_start_input(csp_rt_t* st) { (void)st; }
void csp_board_start_output(csp_rt_t* st) { (void)st; }
void csp_board_stop_output(csp_rt_t* st) { (void)st; }

void csp_board_digital_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    csp_set_ivalue(st, ix, pin_read(vptr->d.pin));
}

// An inout pin RESTS as an input: it is borrowed for the length of one write
// and handed straight back, which is what the Arduino port does and what a pin
// shared with another device needs.
void csp_board_digital_output(csp_rt_t* st, value_t* vptr)
{
    (void)st;
    if (vptr->d.dir & DIR_IN) {
	pin_mode_out(vptr->d.pin);
	pin_write(vptr->d.pin, vptr->d.val & 1);
	pin_mode_in(vptr->d.pin, vptr->d.pullup);
    }
    else
	pin_write(vptr->d.pin, vptr->d.val & 1);
}

void csp_board_digital_config(value_t* vptr)
{
    if (vptr->d.dir & DIR_IN) {
	// No PULLDOWN: the part has none. A program that asks for one gets an
	// input with no pull rather than a pull the wrong way, which is the
	// safer of the two wrong answers and the only one available.
	pin_mode_in(vptr->d.pin, vptr->d.pullup);
    }
    else if (vptr->d.dir & DIR_OUT)
	pin_mode_out(vptr->d.pin);
}

// A0 upwards are Arduino pins CSP_AVR_A0 and up, and ADC channels 0 and up --
// 14/A0..A5 on a 328P, 54/A0..A15 on a Mega. A program may name either, so both
// are accepted.
void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    uint8_t pin = vptr->a.pin;
    uint8_t ch  = (pin >= CSP_AVR_A0) ? (uint8_t)(pin - CSP_AVR_A0) : pin;

    csp_set_ivalue(st, ix, (ivalue_t)adc_read(ch));
}

// NO PWM on this port -- see the head of the file. Left as a no-op rather than
// a guess: driving a pin at the wrong duty is harder to notice than a pin that
// never moves.
void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
    (void)st; (void)di; (void)vptr;
}

void csp_board_analog_config(value_t* vptr)
{
    if ((vptr->a.dir & DIR_IN) && (vptr->a.pin < CSP_AVR_A0))
	return;                                   // a channel, not a pin
    if (vptr->a.dir & DIR_IN)
	pin_mode_in(vptr->a.pin, 0);
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

void csp_setup(csp_rt_t* st)
{
    int i;

    csp_board_setup(st);
    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);
	int j = INDEX(ix);
	value_t* vptr = csp_dio_slot(st, ix, DOUT);
	switch (decl(st, j, type)) {
	case DECL_DIGITAL: csp_board_digital_config(vptr); break;
	default: break;
	}
    }
    csp_ctx_reset(st);
    // AFTER the pin loop: arming an interrupt on a pin still at its reset
    // default arms it on whatever the pin happened to be.
    csp_setup_events(st);
}

void csp_input(csp_rt_t* st)
{
    int i;

    csp_board_start_input(st);
    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);
	int di = INDEX(ix);
	value_t* vptr;
	switch (decl(st, di, type)) {
	case DECL_DIGITAL:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->d.dir & DIR_IN)
		csp_board_digital_input(st, ix, vptr);
	    break;
	case DECL_ANALOG:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->a.dir & DIR_IN)
		csp_board_analog_input(st, ix, vptr);
	    break;
	default: break;
	}
    }
    csp_ctx_reset(st);
    csp_can_input(st);
    csp_buf_input(st);
    csp_input_timer(st);
    csp_input_event(st);
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {
	csp_board_start_output(st);
	for (i = 0; i < st->nio; ++i) {
	    index_t ix = csp_io_at(st, i);
	    int di = INDEX(ix);
	    value_t* vptr;
	    switch (decl(st, di, type)) {
	    case DECL_DIGITAL:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->d.dir & DIR_OUT)
		    csp_board_digital_output(st, vptr);
		break;
	    case DECL_ANALOG:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->a.dir & DIR_OUT)
		    csp_board_analog_output(st, di, vptr);
		break;
	    default: break;
	    }
	}
	csp_ctx_reset(st);
	csp_can_output(st);
	csp_buf_output(st);
	csp_board_stop_output(st);
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

int main(void) __attribute__((no_instrument_function));

int main(void)
{
    index_t x;
    uint8_t why;

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
    why = MCUSR;
    MCUSR = 0;

    csp_board_init();
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
    if (csp_eeprom_peek(&state) == 0)
	csp_boot_pick(&state);

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
    if (csp_eeprom_load(&state) != 0)
	csp_clr_error(&state);
#endif
    // csp_rebuild, not csp_rt_start alone: rebuild resets the middle bump
    // allocator and lays every derived table out again. Calling start on its
    // own leaves them where the previous layout put them.
    if (csp_rebuild(&state) < 0)
	for (;;)
	    ;                                     // nothing can run: stop
    csp_setup(&state);
    state.latch = 0;

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
	state.cycle++;
	csp_input(&state);
	x = state.live ? BAD_INDEX : csp_cycle(&state);   // /live: I/O, no rules
	(void)x;
	csp_commit(&state);
	csp_output(&state);
    }
    return 0;
}
