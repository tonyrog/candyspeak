// The megaAVR chip layer -- ATmega328P and ATmega2560 -- behind csp_chip_io.h.
//
// Pins, the millisecond tick and the ADC, with no csp_rt_t anywhere: what used
// to be the bottom half of port/csp_avr.c. The console and the EEPROM stay in
// the port; they are CandySpeak's, not the chip's.
//
// PORT IS IGNORED. This part keeps the Arduino pin numbering (see below), where
// the number alone names the pin, and board terms and programs written against
// `uno` say `13`, not `1:5`.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#include "csp_chip_io.h"

#ifndef F_CPU
#define F_CPU 16000000UL
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
// code and exists so the same timer can also do PWM on pins 5 and 6. Here
// TIMER0 is the tick and nothing else, so it is exact -- and its two PWM pins
// (5 and 6 on a 328P, 4 on a Mega) have no PWM. See PWM below.

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
// PWM
// ============================================================
//
// 8-bit fast PWM at F_CPU/64/256, 976 Hz at 16 MHz -- the Arduino core's rate,
// so what was tuned against analogWrite looks the same. The 16-bit timers run
// in mode 5, 8-bit fast PWM, which keeps one duty arithmetic for all of them.
//
// A pin is {timer, channel}. TIMER0 is the tick (Time above), so its pins are
// not here, and a pin that is not here never moves -- driving the wrong duty is
// harder to notice than a pin that stays put.
//
// The registers of a timer are where the datasheet puts them, from TCCRnA: a
// 16-bit timer's OCRnB and OCRnC are 2 and 4 bytes past OCRnA, TIMER2's OCR2B
// one past OCR2A, and channel A's COM bit is bit 7, B's 5, C's 3, in all of
// them.

#define PWM_A 0
#define PWM_B 1
#define PWM_C 2
#define PWM(t, c) ((uint8_t)(((t) << 2) | (c)))

#if defined(CSP_AVR_MEGA)
static const uint8_t pwm_pin[] PROGMEM = {
    2, PWM(3, PWM_B),  3, PWM(3, PWM_C),  5, PWM(3, PWM_A),
    6, PWM(4, PWM_A),  7, PWM(4, PWM_B),  8, PWM(4, PWM_C),
    9, PWM(2, PWM_B), 10, PWM(2, PWM_A),
   11, PWM(1, PWM_A), 12, PWM(1, PWM_B), 13, PWM(1, PWM_C),
   44, PWM(5, PWM_C), 45, PWM(5, PWM_B), 46, PWM(5, PWM_A)
};
#else
static const uint8_t pwm_pin[] PROGMEM = {
    3, PWM(2, PWM_B),  9, PWM(1, PWM_A), 10, PWM(1, PWM_B), 11, PWM(2, PWM_A)
};
#endif

// The pin's {timer, channel}, 0 for a pin with no PWM.
static uint8_t pwm_of(uint8_t pin)
{
    uint8_t i;

    for (i = 0; i < (uint8_t)sizeof(pwm_pin); i += 2)
	if (pgm_read_byte(&pwm_pin[i]) == pin)
	    return pgm_read_byte(&pwm_pin[i + 1]);
    return 0;
}

static volatile uint8_t* pwm_tccra(uint8_t t)
{
    switch (t) {
    case 1: return &TCCR1A;
    case 2: return &TCCR2A;
#if defined(CSP_AVR_MEGA)
    case 3: return &TCCR3A;
    case 4: return &TCCR4A;
    case 5: return &TCCR5A;
#endif
    default: return 0;
    }
}

static volatile uint8_t* pwm_ocr(uint8_t t, uint8_t c)
{
    switch (t) {
    case 1: return (volatile uint8_t*)&OCR1A + 2 * c;
    case 2: return &OCR2A + c;
#if defined(CSP_AVR_MEGA)
    case 3: return (volatile uint8_t*)&OCR3A + 2 * c;
    case 4: return (volatile uint8_t*)&OCR4A + 2 * c;
    case 5: return (volatile uint8_t*)&OCR5A + 2 * c;
#endif
    default: return 0;
    }
}

// The pin an output, its timer started. Not connected yet: duty 0 is the pin
// held low, as fast PWM at OCR 0 still gives a spike each period.
static void pwm_init(uint8_t pin)
{
    uint8_t tc = pwm_of(pin);
    volatile uint8_t* a;

    pin_write(pin, 0);
    pin_mode_out(pin);
    if (!tc || !(a = pwm_tccra((uint8_t)(tc >> 2))))
	return;
    if ((tc >> 2) == 2) {
	*a |= (uint8_t)((1 << WGM21) | (1 << WGM20));   // fast PWM, TOP 0xFF
	TCCR2B = (1 << CS22);                           // TIMER2's /64
    }
    else {
	*a |= (uint8_t)(1 << 0);                        // WGMn0
	a[1] = (uint8_t)((1 << 3) | (1 << 1) | (1 << 0)); // WGMn2, /64
    }
}

// v is sixteen bits, left-justified; the top eight are the duty.
static void pwm_write(uint8_t pin, uint16_t v)
{
    uint8_t tc = pwm_of(pin), c = (uint8_t)(tc & 3), d = (uint8_t)(v >> 8);
    volatile uint8_t* a;
    volatile uint8_t* o;

    if (!tc || !(a = pwm_tccra((uint8_t)(tc >> 2))))
	return;
    o = pwm_ocr((uint8_t)(tc >> 2), c);
    if (d == 0) {
	*a &= (uint8_t)~(1 << (7 - 2 * c));
	pin_write(pin, 0);
    }
    else {
	if ((tc >> 2) != 2)
	    o[1] = 0;                                   // high byte first
	o[0] = d;
	*a |= (uint8_t)(1 << (7 - 2 * c));
    }
}

// ============================================================
// csp_chip_io.h
// ============================================================

void csp_chip_init(void)
{
    time_init();
    adc_init();
}

uint32_t csp_chip_millis(void)
{
    uint32_t v;
    uint8_t s = SREG;
    cli();
    v = ms_ticks;                                 // four bytes: not atomic
    SREG = s;
    return v;
}

void csp_chip_dcfg(uint8_t port, uint8_t pin, uint8_t dir, uint8_t pull)
{
    (void)port;
    // No PULLDOWN: the part has none. A program that asks for one gets an
    // input with no pull rather than a pull the wrong way, which is the safer
    // of the two wrong answers and the only one available.
    if (dir & CSP_CHIP_IN)
	pin_mode_in(pin, pull & CSP_CHIP_PULLUP);
    else if (dir & CSP_CHIP_OUT)
	pin_mode_out(pin);
}

int csp_chip_din(uint8_t port, uint8_t pin)
{
    (void)port;
    return pin_read(pin);
}

void csp_chip_dout(uint8_t port, uint8_t pin, int v)
{
    (void)port;
    pin_write(pin, v);
}

// A0 upwards are Arduino pins CSP_AVR_A0 and up, and ADC channels 0 and up --
// 14/A0..A5 on a 328P, 54/A0..A15 on a Mega. A program may name either, so
// both are accepted. Only a pin needs its pullup off; a channel number has no
// pin behind it to configure.
//
// An output is PWM whether the program says `pwm` or not: the part has no DAC.
void csp_chip_acfg(uint8_t port, uint8_t pin, uint8_t dir, uint8_t pwm)
{
    (void)port; (void)pwm;
    if (dir & CSP_CHIP_IN) {
	if (pin >= CSP_AVR_A0)
	    pin_mode_in(pin, 0);
    }
    else if (dir & CSP_CHIP_OUT)
	pwm_init(pin);
}

// Ten bits, left-justified to sixteen.
uint16_t csp_chip_ain(uint8_t port, uint8_t pin)
{
    uint8_t ch = (pin >= CSP_AVR_A0) ? (uint8_t)(pin - CSP_AVR_A0) : pin;
    (void)port;
    return (uint16_t)(adc_read(ch) << 6);
}

void csp_chip_aout(uint8_t port, uint8_t pin, uint16_t v)
{
    (void)port;
    pwm_write(pin, v);
}
