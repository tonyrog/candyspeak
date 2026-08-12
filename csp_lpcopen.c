// CandySpeak on NXP LPC, through LPCOpen.
//
// The same job csp_arduino.c does, one layer down: no core, no libraries, just
// the chip drivers in nxp_lpcopen/. What that costs is the things Arduino hands
// you for free -- a millisecond clock, a serial port, a pin-number-to-peripheral
// map -- and each of those is set up explicitly below.
//
// WHAT THIS FILE DECIDES, and what it leaves to you:
//
//   decided here   GPIO in/out/config, ADC input, the SysTick clock, the UART
//                  console, the memory report, the main loop, on-chip EEPROM on
//                  the parts that have it.
//   left to you    pin muxing (IOCON/SCU is board wiring, not chip logic), the
//                  ADC channel map, PWM, DAC, CAN. Every one of them is a
//                  csp_lpc_* function below with a working no-op body and a
//                  comment saying what a real one does. Search for STUB.
//
// PORTS AND PINS. On LPC this is the easy half: `#digital Led out 0:13` is
// GPIO port 0, pin 13, exactly as the chip numbers them -- no board-specific pin
// table in between, unlike Arduino where `0:13` had to mean something invented.
// An #analog needs a peripheral rather than a GPIO, so it is selected by PORT:
//
//   #analog Pot:12 in 15:0        port 15 = ADC, pin = channel  -> ADC0 ch 0
//   #analog Out:10 out 13:0       port 13 = DAC, pin = channel
//   #analog Servo:8 out pwm 2:1   `pwm` on a GPIO port -> csp_lpc_pwm_write
//
// The port numbers are #defines; move them if your board has GPIO ports up
// there. PORT_BITS is 4, so 0..15 is the whole range available.
//
// BUILDING. There is no Makefile for this yet -- what a build needs is:
//   - this file, csp_rt.c, csp_compile.c, csp_parse.c, csp_tok.c, csp_print.c,
//     csp_repl.c, csp_dump.c, csp_eeprom.c, csp_strings.c and a rom.c
//     (`./csp -n -C -O rom.c prog.csp`, or rom_host.c for an empty image)
//   - nxp_lpcopen/<family>/lpc_chip_*/src/*.c and its inc/ on the include path
//   - nxp_lpcopen/<family>/gcc/cr_startup_*.c and a linker script
//   - -DCSP_EMBEDDED -DCSP_LPC_FAMILY_xxx -DCORE_Mn (whatever the chip headers
//     want; see sys_config.h in the family you build for)

// stdint FIRST: csp_config.h types the reactive gate mask (uint16_t) and there
// is no core header here to have pulled it in already, the way Arduino.h does.
#include <stdint.h>
#include <stddef.h>

#include "csp_config.h"

// Bounds the idle sleep, so continuous inputs keep sampling even when a slow
// timer is armed. See the end of csp_loop.
#ifndef SAMPLE_MS
#define SAMPLE_MS 2
#endif

#include "chip.h"

#include <stdlib.h>
#include <string.h>

#define CSP_EMBEDDED 1
#include "csp.h"
#include "csp_print.h"
#include "csp_strings.h"

// --- which family -----------------------------------------------------------
// chip.h has already been included by now, so the family can be recognised from
// what IT defined rather than from a flag we ask the build to pass. The families
// differ in three places only -- ADC, UART status, EEPROM -- and each is guarded
// by one of these.
#if defined(CHIP_LPC177X_8X) || defined(CHIP_LPC40XX)
#define CSP_LPC_ADC_CLASSIC   1     // Chip_ADC_Init(pADC, &ADC_CLOCK_SETUP_T)
#define CSP_LPC_UART_LSR      1     // Chip_UART_ReadLineStatus + UART_LSR_*
#define CSP_LPC_EEPROM_PAGED  1     // Chip_EEPROM_Read/Write(page, offset, ...)
#elif defined(CHIP_LPC175X_6X)
// Same peripherals as its 177x/8x sibling with ONE exception that matters here:
// no EEPROM. eeprom_17xx_40xx.h is in the 175x_6x driver directory, which makes
// it look otherwise -- but chip_lpc175x_6x.h never defines LPC_EEPROM, so a
// paged-EEPROM build fails to compile rather than misbehaving. (LPC1754 is this
// part, and there is a Makefile for it.)
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_NO_EEPROM     1
#elif defined(CHIP_LPC18XX) || defined(CHIP_LPC43XX)
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_EEPROM_MAPPED 1     // memory-mapped at EEPROM_ADDRESS
#elif defined(CHIP_LPC15XX)
#define CSP_LPC_ADC_SEQ       1     // sequencer-based ADC, different API
#define CSP_LPC_UART_STAT     1     // Chip_UART_GetStatus + UART_STAT_*
#define CSP_LPC_EEPROM_IAP    1     // through the IAP ROM calls
#else                                // 11xx, 11u6x, 13xx
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_NO_EEPROM     1     // flash-only parts: /save has nowhere to go
#endif

// --- board knobs ------------------------------------------------------------
#ifndef CSP_LPC_UART
#define CSP_LPC_UART      LPC_USART0
#endif
#ifndef CSP_LPC_BAUD
#define CSP_LPC_BAUD      115200
#endif
#ifndef CSP_LPC_ADC
#define CSP_LPC_ADC       LPC_ADC0
#endif
#ifndef CSP_LPC_ADC_RATE
#define CSP_LPC_ADC_RATE  400000     // ADC clock; 400 kHz is the usual max
#endif
#ifndef CSP_LPC_ADC_BITS
#define CSP_LPC_ADC_BITS  12         // 10 on the 11xx parts
#endif
#ifndef CSP_LPC_ADC_PORT
#define CSP_LPC_ADC_PORT  15         // an #analog here reads an ADC channel
#endif
#ifndef CSP_LPC_DAC_PORT
#define CSP_LPC_DAC_PORT  13         // ...and here it writes the DAC
#endif

// ============================================================
// Board hooks -- STUBS. These are the four places board wiring shows through.
// ============================================================

// STUB: pin muxing. LPCOpen keeps this out of the chip drivers on purpose --
// which IOCON/SCU function a pin needs is a property of the board, not of the
// part. Called once per declared device at setup, before anything is driven.
//
// A real one looks like (17xx/40xx):
//     Chip_IOCON_PinMux(LPC_IOCON, port, pin, IOCON_MODE_INACT, IOCON_FUNC0);
// or (18xx/43xx, where the SCU group is NOT the GPIO port number):
//     Chip_SCU_PinMuxSet(group, gpin, SCU_MODE_INACT | SCU_MODE_FUNC0);
//
// `analog` says the caller wants the pin as an ADC/DAC input rather than GPIO,
// which on most parts means clearing the digital-mode bit (IOCON_ADMODE_EN).
// Left empty, a board whose reset-default mux is already GPIO still works --
// which is why this is a no-op and not an error.
void csp_lpc_pin_mux(uint8_t port, uint8_t pin, int analog)
{
    (void)port; (void)pin; (void)analog;
}

// STUB: which ADC channel a `port:pin` names. The default is the identity --
// `15:3` is ADC channel 3 -- which is right whenever the .csp names channels
// directly. Override it if you would rather name board connector numbers.
// Return < 0 to refuse the pin; the read then yields 0 rather than sampling a
// channel nobody asked for.
int csp_lpc_adc_channel(uint8_t port, uint8_t pin)
{
    (void)port;
    return (pin < 8) ? (int)pin : -1;
}

// STUB: PWM output. There is no portable answer here -- 17xx has MCPWM and the
// timer match outputs, 15xx and 43xx have the SCT, and which one is wired to a
// given pin is a board fact. `val` is already scaled to 0..255.
//
// A timer-match version is about ten lines: Chip_TIMER_SetMatch on the channel
// the pin is muxed to, with the period set once at init.
void csp_lpc_pwm_write(uint8_t port, uint8_t pin, int val)
{
    (void)port; (void)pin; (void)val;
}

// STUB: DAC output, for an #analog on CSP_LPC_DAC_PORT. On the parts that have
// one this is genuinely two lines --
//     Chip_DAC_Init(LPC_DAC);                  (once, at setup)
//     Chip_DAC_UpdateValue(LPC_DAC, val);      (here, val is 0..1023)
// -- but dac_*.h is not present on every family, so it stays out of the build
// until you say which one you are on.
void csp_lpc_dac_write(uint8_t pin, int val)
{
    (void)pin; (void)val;
}

// STUB: anything the board needs before CandySpeak has memory. Board_Init() in
// an LPCOpen example does: SystemCoreClockUpdate, clock setup, then the pin mux
// for the console UART. The UART itself is set up by csp_lpc_uart_init below,
// so this is for the rest -- power to peripherals, an external oscillator, a
// PHY reset line.
void csp_lpc_board_init(void)
{
}

// ============================================================
// Time -- SysTick at 1 kHz
// ============================================================
//
// SysTick_Handler is WEAK in the LPCOpen startup files, so defining it here
// overrides the do-nothing one without touching the vector table.
//
// `volatile` is not decoration: the loop below spins on this while an interrupt
// changes it, and without it the compiler is entitled to hoist the read out.

static volatile uint32_t csp_ticks_ms = 0;

void SysTick_Handler(void)
{
    csp_ticks_ms++;
}

static void csp_lpc_systick_init(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);
}

uint32_t csp_time_ms(void)
{
    return csp_ticks_ms;
}

// Microseconds, from the SysTick counter between ticks. Read ms twice around
// the counter read and retry if it moved: the counter wraps exactly when ms
// increments, so a naive pair can report a time a whole millisecond early.
unsigned long csp_time_us(void)
{
    uint32_t ms, val, ms2;
    uint32_t load = SysTick->LOAD + 1;

    do {
	ms   = csp_ticks_ms;
	val  = SysTick->VAL;
	ms2  = csp_ticks_ms;
    } while (ms != ms2);

    // VAL counts DOWN from LOAD, so elapsed-within-the-tick is LOAD - VAL.
    return (unsigned long)ms * 1000UL +
	   (unsigned long)(((load - val) * 1000UL) / load);
}

static void csp_delay_ms(uint32_t ms)
{
    uint32_t t0 = csp_ticks_ms;
    while ((csp_ticks_ms - t0) < ms)
	__WFI();                       // sleep until the next interrupt
}

// ============================================================
// Console UART
// ============================================================

static int serial_output = 0;

static void csp_lpc_uart_init(void)
{
    Chip_UART_Init(CSP_LPC_UART);
    Chip_UART_SetBaud(CSP_LPC_UART, CSP_LPC_BAUD);
    // 8N1, spelled two ways: the 15xx USART has a config register (UART_CFG_*),
    // the 550-style blocks on every other family have an LCR (UART_LCR_*).
#if defined(CSP_LPC_UART_STAT)
    Chip_UART_ConfigData(CSP_LPC_UART, UART_CFG_DATALEN_8 |
			 UART_CFG_STOPLEN_1 | UART_CFG_PARITY_NONE);
    Chip_UART_Enable(CSP_LPC_UART);
#else
    Chip_UART_ConfigData(CSP_LPC_UART, UART_LCR_WLEN8 |
			 UART_LCR_SBS_1BIT | UART_LCR_PARITY_DIS);
    Chip_UART_SetupFIFOS(CSP_LPC_UART, UART_FCR_FIFO_EN | UART_FCR_TRG_LEV0);
#endif
    Chip_UART_TXEnable(CSP_LPC_UART);
}

static int csp_lpc_uart_can_send(void)
{
#if defined(CSP_LPC_UART_STAT)
    return (Chip_UART_GetStatus(CSP_LPC_UART) & UART_STAT_TXRDY) != 0;
#else
    return (Chip_UART_ReadLineStatus(CSP_LPC_UART) & UART_LSR_THRE) != 0;
#endif
}

static int csp_lpc_uart_available(void)
{
#if defined(CSP_LPC_UART_STAT)
    return (Chip_UART_GetStatus(CSP_LPC_UART) & UART_STAT_RXRDY) != 0;
#else
    return (Chip_UART_ReadLineStatus(CSP_LPC_UART) & UART_LSR_RDR) != 0;
#endif
}

static int csp_lpc_uart_read(void)
{
    return (int)Chip_UART_ReadByte(CSP_LPC_UART);
}

void* csp_set_file_output(void* f)
{
    int prev = serial_output;
    serial_output = (f != NULL);
    return prev ? (void*)1 : (void*)0;
}

int csp_will_output()
{
    return serial_output;
}

// The CR belongs here and nowhere else: the runtime ends a line three different
// ways (csp_println, a bare '\n', a '\n' inside a literal) and all three come
// through csp_print_char. Translating anywhere else leaves half the output bare
// LF. Returns LOGICAL characters, so csp_print_just's column arithmetic matches
// the host's.
int csp_print_char(char c)
{
    if (serial_output) {
	if (c == '\n') {
	    while (!csp_lpc_uart_can_send())
		;
	    Chip_UART_SendByte(CSP_LPC_UART, '\r');
	}
	while (!csp_lpc_uart_can_send())
	    ;
	Chip_UART_SendByte(CSP_LPC_UART, (uint8_t)c);
    }
    return 1;
}

int csp_print_str(const char* s)
{
    int n = 0;
    while (s[n] != '\0') {
	csp_print_char(s[n]);
	n++;
    }
    return n;
}

// ARM: rodata is directly addressable, so a rostring is a plain pointer.
int csp_print_rostr(rostring_t s)
{
    return csp_print_str((const char*) s);
}

void csp_flush(void)
{
}

// ============================================================
// Memory reporting
// ============================================================
//
// The linker script names these. `_pvHeapStart` is what the LPCOpen/LPCXpresso
// scripts call the end of .bss; `_vStackTop` is the top of RAM. If your script
// uses other names, this is the one place to change them -- everything else
// reads the numbers rather than the symbols.

extern char _pvHeapStart;      /* first free byte above .bss */
extern char _vStackTop;        /* top of RAM = initial SP */

csp_rt_t state;

int stack_used(void)
{
    char local;
    return (int)(&_vStackTop - &local);
}

// Free RAM right now: heap top up to the current stack pointer. Excludes the
// .bss struct and (with the static-arena backend) the arena, since both sit
// below the heap.
static uint32_t raw_free(void)
{
    char top;
    return (uint32_t)(&top - &_pvHeapStart);
}

// avail = RAM the pool may claim, INCLUDING the struct -- csp_mem_init subtracts
// it back out, and the struct is already placed in .bss.
uint32_t csp_system_ram_avail(void)
{
    return raw_free() + sizeof(csp_rt_t);
}

// Total RAM. Nothing is dimensioned from this -- it feeds /memory -- so getting
// it wrong reports oddly rather than misbehaving.
//
// There is no honest way to derive it from the linker symbols: _vStackTop is the
// top of ONE bank, and these parts scatter RAM across several (LPC43xx has a
// 32 kB local bank plus 40 kB more at two other addresses). So: say what the
// board file says, and 0 -- visibly a non-answer -- when it says nothing. 0 is
// what csp_system_ram_used tests for, so /memory then reports free RAM and
// declines to report a percentage rather than inventing one.
uint32_t csp_system_ram_capacity(void)
{
#ifdef CSP_LPC_RAM
    return CSP_LPC_RAM;
#else
    return 0;
#endif
}

// system = core + libraries: everything present, minus our pool and struct.
uint32_t csp_system_ram_used(void)
{
    uint32_t cap = csp_system_ram_capacity();
    uint32_t total;
    uint32_t ours;

    if (cap == 0)
	return 0;
    total = cap - raw_free();
    ours  = (uint32_t)state.mem_limit + (uint32_t)sizeof(csp_rt_t);
    return (total > ours) ? (total - ours) : 0;
}

// ============================================================
// GPIO
// ============================================================

void csp_board_digital_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    int value = Chip_GPIO_GetPinState(LPC_GPIO, vptr->d.port, vptr->d.pin) ? 1 : 0;
    csp_set_ivalue(st, ix, value);
}

// An inout pin is borrowed for the length of one write and handed straight back
// as an input, which is what makes a bidirectional line usable from a rule.
void csp_board_digital_output(csp_rt_t* st, value_t* vptr)
{
    (void)st;
    if (vptr->d.dir & DIR_IN) {
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, vptr->d.port, vptr->d.pin);
	Chip_GPIO_SetPinState(LPC_GPIO, vptr->d.port, vptr->d.pin,
			      (vptr->d.val & 1) != 0);
	Chip_GPIO_SetPinDIRInput(LPC_GPIO, vptr->d.port, vptr->d.pin);
    }
    else {
	Chip_GPIO_SetPinState(LPC_GPIO, vptr->d.port, vptr->d.pin,
			      (vptr->d.val & 1) != 0);
    }
}

// The single description of what a digital slot's configuration MEANS in
// hardware. Setup, a rule that writes .dir/.pullup/.pulldown, and anything that
// forces pins into a known state all come here, so those paths cannot drift
// apart -- which is how a pin ends up configured one way and driven another.
//
// A pin with no direction at all is left alone: the program said nothing about
// it, and asserting a mode on a pin someone else owns is worse than silence.
//
// PULLUPS ARE NOT HERE. They live in IOCON/SCU, not in the GPIO block, and the
// register layout differs per family -- so `pullup`/`pulldown` on an #digital
// reach csp_lpc_pin_mux and are yours to apply. A pin declared `in pullup`
// works as a plain input until then, which is the safe way to be wrong.
void csp_board_digital_config(value_t* vptr)
{
    if (vptr->d.dir & DIR_IN)
	Chip_GPIO_SetPinDIRInput(LPC_GPIO, vptr->d.port, vptr->d.pin);
    else if (vptr->d.dir & DIR_OUT)
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, vptr->d.port, vptr->d.pin);
}

// ============================================================
// ADC
// ============================================================

#if defined(CSP_LPC_ADC_CLASSIC)
static ADC_CLOCK_SETUP_T csp_adc_setup;

static void csp_lpc_adc_init(void)
{
    Chip_ADC_Init(CSP_LPC_ADC, &csp_adc_setup);
    Chip_ADC_SetSampleRate(CSP_LPC_ADC, &csp_adc_setup, CSP_LPC_ADC_RATE);
}

// One blocking conversion. Burst mode would be better for several channels --
// it samples them in the background and this becomes a register read -- but it
// needs a channel set known up front, and the device list is not fixed until
// csp_setup has walked it. Start there if the ADC ever shows up in a profile.
static int csp_lpc_adc_read(int ch)
{
    uint16_t data = 0;

    Chip_ADC_EnableChannel(CSP_LPC_ADC, (ADC_CHANNEL_T)ch, ENABLE);
    Chip_ADC_SetStartMode(CSP_LPC_ADC, ADC_START_NOW, ADC_TRIGGERMODE_RISING);
    while (Chip_ADC_ReadStatus(CSP_LPC_ADC, ch, ADC_DR_DONE_STAT) != SET)
	;
    Chip_ADC_ReadValue(CSP_LPC_ADC, ch, &data);
    Chip_ADC_EnableChannel(CSP_LPC_ADC, (ADC_CHANNEL_T)ch, DISABLE);
    return (int)data;
}
#else
// STUB: the LPC15xx ADC is sequencer-based -- Chip_ADC_Init takes flags, then a
// sequence is configured (Chip_ADC_SetupSequencer) and started, and the result
// comes from Chip_ADC_GetDataReg. Same shape, different calls; the rest of this
// file does not care which.
static void csp_lpc_adc_init(void) { }
static int csp_lpc_adc_read(int ch) { (void)ch; return 0; }
#endif

// The ADC gives 12 bits on most of these parts. A declaration says what width it
// wants (`#analog Pot:10`), so scale rather than assume -- the same rule the
// accelerometer follows on CPX. Signed is the default for an #analog, so an
// unsigned one asks for the plain 0..2^res-1 form instead.
static int csp_lpc_scale(csp_rt_t* st, index_t ix, int raw)
{
    csp_decl_t d = csp_get_decl(st, INDEX(ix));
    int res = GET_RES(d.res);
    int sgn = (CSP_MASK(d.vt,TYPE_BITS) != V_UNSIGNED);
    int v;

    if (res < 2) res = 2; else if (res > 16) res = 16;
    if (res >= CSP_LPC_ADC_BITS)
	v = raw << (res - CSP_LPC_ADC_BITS);
    else
	v = raw >> (CSP_LPC_ADC_BITS - res);
    if (sgn)
	v -= (1 << (res - 1));         // 0 = mid scale
    return v;
}

void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    int value = 0;

    if (vptr->a.port == CSP_LPC_ADC_PORT) {
	int ch = csp_lpc_adc_channel(vptr->a.port, vptr->a.pin);
	if (ch >= 0)
	    value = csp_lpc_scale(st, ix, csp_lpc_adc_read(ch));
    }
    csp_set_ivalue(st, ix, value);
}

void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
    if (vptr->a.port == CSP_LPC_DAC_PORT) {
	csp_lpc_dac_write(vptr->a.pin, vptr->a.val);
	return;
    }
    if (vptr->a.pwm) {
	// Scale the declared width down to the 0..255 the hook takes, so a
	// `:16` and a `:8` output differ in precision and not in meaning.
	int full = (1 << GET_RES(decl(st,di,res))) - 1;
	int val  = full ? (int)((vptr->a.val * 255) / full) : 0;
	csp_lpc_pwm_write(vptr->a.port, vptr->a.pin, val);
    }
}

// Only a PWM or DAC output owns its pin in a way that has to be asserted -- an
// ADC read needs no direction at all -- so an input just re-muxes as analog.
void csp_board_analog_config(value_t* vptr)
{
    if (vptr->a.dir & DIR_IN)
	csp_lpc_pin_mux(vptr->a.port, vptr->a.pin, 1);
    else if ((vptr->a.dir & DIR_OUT) && vptr->a.pwm)
	csp_lpc_pin_mux(vptr->a.port, vptr->a.pin, 0);
}

// ============================================================
// Board lifecycle
// ============================================================

void csp_board_init(void)
{
    csp_lpc_board_init();
    Chip_GPIO_Init(LPC_GPIO);
    csp_lpc_adc_init();
}

void csp_board_setup(csp_rt_t* st)      { (void)st; }
void csp_board_start_input(csp_rt_t* st)  { (void)st; }
void csp_board_start_output(csp_rt_t* st) { (void)st; }
void csp_board_stop_output(csp_rt_t* st)  { (void)st; }

// ============================================================
// The device loops
// ============================================================
//
// NOTE: csp_setup/csp_input/csp_output below are near-identical to the ones in
// csp_arduino.c -- the walk over st->nio, the config-request check, the DIN/DOUT
// slot handling and the ordering are all runtime contract, not board detail.
// Only the leaf calls differ. Worth pulling into a shared csp_io.c the next time
// a third port shows up; two copies is the point at which the duplication is
// visible but still cheaper than an abstraction fitted to a sample of two.

// Apply a configuration a rule asked for, and take the request down in BOTH
// slots -- the pair is copied on commit, so clearing one leaves a stale request
// in the other that spends a config call on some later cycle.
//
// d.cfg and a.cfg do NOT land on the same bit (digital has pullup/pulldown ahead
// of it, analog only pwm), so the flag is cleared through the member that set it.
static void csp_apply_config(csp_rt_t* st, index_t ix, value_t* vptr, int analog)
{
    value_t* iptr;
    value_t* optr;

    csp_dio_slots(st, ix, &iptr, &optr);
    if (analog) {
	csp_board_analog_config(vptr);
	iptr->a.cfg = optr->a.cfg = 0;
    }
    else {
	csp_board_digital_config(vptr);
	iptr->d.cfg = optr->d.cfg = 0;
    }
}

void csp_setup(csp_rt_t* st)
{
    int i;

    csp_board_setup(st);
    csp_can_init(st);

    // One pass over the device list. Configuration is read from the value SLOT,
    // not the declaration, so this is the same source of truth the runtime gates
    // on -- setup_digital has already copied the declaration in by now.
    //
    // If the same physical pin is declared twice under two names, the LAST one
    // decides its mode, because it configures last. Same "last one wins" the
    // rest of the language patches by.
    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);       // binds the entry's object
	int j = INDEX(ix);
	value_t* vptr = csp_dio_slot(st, ix, DOUT);
	switch (decl(st,j,type)) {
	case DECL_DIGITAL:
	    csp_lpc_pin_mux(vptr->d.port, vptr->d.pin, 0);
	    csp_board_digital_config(vptr);
	    break;
	case DECL_ANALOG:
	    csp_lpc_pin_mux(vptr->a.port, vptr->a.pin,
			    (vptr->a.dir & DIR_IN) ? 1 : 0);
	    break;
	default:
	    break;
	}
    }
    csp_ctx_reset(st);
}

void csp_input(csp_rt_t* st)
{
    int i;

    csp_board_start_input(st);

    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);
	int di = INDEX(ix);
	value_t* vptr;
	switch (decl(st,di,type)) {
	case DECL_DIGITAL:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    // A rule may have turned this pin round since we last looked. Do it
	    // BEFORE reading, or the first sample after a flip comes off the old
	    // mode.
	    if (vptr->d.cfg)
		csp_apply_config(st, ix, vptr, 0);
	    if (vptr->d.dir & DIR_IN)
		csp_board_digital_input(st, ix, vptr);
	    break;
	case DECL_ANALOG:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->a.cfg)
		csp_apply_config(st, ix, vptr, 1);
	    if (vptr->a.dir & DIR_IN)
		csp_board_analog_input(st, ix, vptr);
	    break;
	default:
	    break;
	}
    }
    csp_ctx_reset(st);
    csp_can_input(st);
    csp_input_timer(st);
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {                      // allow output
	csp_board_start_output(st);

	for (i = 0; i < st->nio; ++i) {
	    index_t ix = csp_io_at(st, i);
	    int di = INDEX(ix);
	    value_t* vptr;
	    switch (decl(st,di,type)) {
	    case DECL_DIGITAL:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->d.cfg)
		    csp_apply_config(st, ix, vptr, 0);
		if (vptr->d.dir & DIR_OUT)
		    csp_board_digital_output(st, vptr);
		break;
	    case DECL_ANALOG:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->a.cfg)
		    csp_apply_config(st, ix, vptr, 1);
		if (vptr->a.dir & DIR_OUT)
		    csp_board_analog_output(st, di, vptr);
		break;
	    default:
		break;
	    }
	}
	csp_ctx_reset(st);
	csp_can_output(st);
	csp_board_stop_output(st);
    }
    csp_output_timer(st);
}

// ============================================================
// CAN -- STUBS
// ============================================================
//
// Every LPC here has a controller, and the drivers are in the tree, but the API
// is not one API: 17xx/40xx have Chip_CAN_* (can_17xx_40xx.h), 18xx/43xx have
// Chip_CCAN_* (ccan_18xx_43xx.h), and 15xx reaches its through the ROM
// (rom_can_15xx.h). Picking one here would be guessing which part you are on.
//
// The contract, from csp.h:
//   csp_can_recv: 1 = a frame was read, 0 = nothing pending, -1 = error
//   csp_can_send: 0 = sent, -1 = error
// csp_can_init returning 0 with recv always saying "nothing" is a working
// no-bus node: #buffer ... can declarations compile and simply never fire.

int csp_can_init(csp_rt_t* st)
{
    (void)st;
    return 0;
}

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
// Persistent storage
// ============================================================
//
// The access pattern is strictly sequential and csp_eeprom_save rewrites the
// whole image every time, so no read-modify-write and no RAM shadow is needed.

const char* csp_eeprom_name(void)
{
    static const char nm[] = "EEPROM";
    return nm;
}

#if defined(CSP_LPC_EEPROM_PAGED)

// 17xx/40xx: 4032 bytes as 63 pages of 64. Chip_EEPROM_Read/Write take a page
// and an offset within it and cross page boundaries themselves, so a byte
// cursor is all this needs to keep.
#define CSP_EE_SIZE  (EEPROM_PAGE_SIZE * EEPROM_PAGE_NUM)

static int ee_pos = -1;

uint32_t csp_eeprom_capacity(void) { return CSP_EE_SIZE; }

int csp_eeprom_open_read(void)
{
    Chip_EEPROM_Init(LPC_EEPROM);
    ee_pos = 0;
    return 0;
}

int csp_eeprom_open_write(void)
{
    Chip_EEPROM_Init(LPC_EEPROM);
    ee_pos = 0;
    return 0;
}

void csp_eeprom_close(void) { ee_pos = -1; }

int csp_eeprom_read(void* buf, size_t len)
{
    if (ee_pos < 0)
	return -1;
    if ((uint32_t)ee_pos + len > CSP_EE_SIZE)
	return -1;
    Chip_EEPROM_Read(LPC_EEPROM, ee_pos % EEPROM_PAGE_SIZE,
		     ee_pos / EEPROM_PAGE_SIZE, buf,
		     EEPROM_RWSIZE_8BITS, (uint32_t)len);
    ee_pos += (int)len;
    return 0;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    if (ee_pos < 0)
	return -1;
    // Refuse to run off the end: a write past the last cell wraps, so a program
    // too big to persist would half-save silently.
    if ((uint32_t)ee_pos + len > CSP_EE_SIZE)
	return -1;
    if (Chip_EEPROM_Write(LPC_EEPROM, ee_pos % EEPROM_PAGE_SIZE,
			  ee_pos / EEPROM_PAGE_SIZE, (void*)buf,
			  EEPROM_RWSIZE_8BITS, (uint32_t)len) != SUCCESS)
	return -1;
    ee_pos += (int)len;
    return 0;
}

#elif defined(CSP_LPC_EEPROM_MAPPED)

// STUB: 18xx/43xx. The EEPROM is memory-mapped for READS (EEPROM_ADDRESS), so
// csp_eeprom_read is a memcpy -- but a write is per page: fill the page through
// the mapped window, then Chip_EEPROM_EraseProgramPage and wait for the
// end-of-program interrupt status. Buffer a page here the way the SAMD flash
// backend in csp_arduino.c buffers a row, and the rest of the layer is
// unchanged.
uint32_t csp_eeprom_capacity(void) { return 0; }
int csp_eeprom_open_read(void)  { return 0; }
int csp_eeprom_open_write(void) { return 0; }
void csp_eeprom_close(void) { }
int csp_eeprom_read(void* buf, size_t len)        { (void)buf; (void)len; return -1; }
int csp_eeprom_write(const void* buf, size_t len) { (void)buf; (void)len; return -1; }

#else

// No persistent store on this part (or none wired up yet). Say so honestly
// rather than accepting the call: returning 0 here is what once made /save print
// "Saved" and write nothing.
uint32_t csp_eeprom_capacity(void) { return 0; }
int csp_eeprom_open_read(void)  { return 0; }
int csp_eeprom_open_write(void) { return 0; }
void csp_eeprom_close(void) { }
int csp_eeprom_read(void* buf, size_t len)        { (void)buf; (void)len; return -1; }
int csp_eeprom_write(const void* buf, size_t len) { (void)buf; (void)len; return -1; }

#endif

// ============================================================
// Boot and main loop
// ============================================================

// Boot progress on a pin, for a board that will not give you a serial port.
// Build with -DCSP_BOOT_BLINK and count the flashes: the number is how far setup
// got, so the last one you see names the step that hung or faulted.
//
//   1  entered setup      4  ROM loaded         6  program laid out
//   2  UART up            5  EEPROM attempted   7  devices configured -- done
//   3  runtime initialised
#if defined(CSP_BOOT_BLINK)
#ifndef CSP_BOOT_LED_PORT
#define CSP_BOOT_LED_PORT 0
#endif
#ifndef CSP_BOOT_LED_PIN
#define CSP_BOOT_LED_PIN  0
#endif
static void boot_mark(int n)
{
    int i;
    Chip_GPIO_SetPinDIROutput(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN);
    for (i = 0; i < n; i++) {
	Chip_GPIO_SetPinState(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN, true);
	csp_delay_ms(120);
	Chip_GPIO_SetPinState(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN, false);
	csp_delay_ms(180);
    }
    csp_delay_ms(700);                 // gap, so the count is readable
}
#else
#define boot_mark(n) ((void)0)
#endif

static void csp_lpc_setup(void)
{
    boot_mark(1);
    csp_lpc_systick_init();
    csp_lpc_uart_init();
    boot_mark(2);

    serial_output = 1;

    csp_board_init();   // cannot use state: rt_init zeroes it below

#if !defined(CSP_EXEC_ONLY)
    csp_print_lit("boot: RAM "); csp_print_uint(csp_system_ram_capacity());
    csp_print_lit(", free ");    csp_print_uint(csp_system_ram_avail());
    csp_print_lit(", struct ");  csp_print_uint((uint32_t)sizeof(csp_rt_t));
    csp_println();
#endif

    // A failed init leaves a half-set-up state; say so instead of running into a
    // fault. This is where an over-eager claim (free - reserve too tight) shows
    // up, rather than as a mystery hang.
    if (csp_rt_init(&state, REACTIVE_DEFAULT) < 0) {
	csp_print_line("FATAL: csp_rt_init failed (out of memory)");
	return;                        // leave the loop a no-op rather than crash
    }
    boot_mark(3);

#if !defined(CSP_EXEC_ONLY)
    csp_print_lit("pool "); csp_print_uint((uint32_t)state.mem_limit);
    csp_print_lit(", heap left "); csp_print_uint(csp_system_ram_avail());
    csp_print_lit(", reserve "); csp_print_uint((uint32_t)CSP_RAM_RESERVE);
    csp_println();
#endif

    // Wire up the ROM firmware (rom.c) FIRST, so the program runs even when
    // there is no valid save: csp_eeprom_load re-does this on its success path,
    // but on failure it returns before touching ROM.
    csp_load_rom(&state);
    boot_mark(4);

    if (csp_eeprom_load(&state) == 0) {
#if !defined(CSP_EXEC_ONLY)
	csp_print_line("Loaded from EEPROM");
#endif
    }
    else {
	csp_clr_error(&state);         // "no saved state" is the normal case at
				       // boot, not an error to carry forward
#if !defined(CSP_EXEC_ONLY)
	csp_print_line("No saved state, running ROM");
#endif
    }
    boot_mark(5);

    // Lay out the whole program: reactive graph + leaf/device setup. MUST be
    // csp_rebuild, not csp_rt_start alone -- rebuild resets the middle bump
    // allocator every derived table is carved from, and without it mid_end stays
    // 0, every table allocation fails, and the first cycle faults on null
    // view/heap pointers.
    if (csp_rebuild(&state) < 0)
	csp_print_line("setup failed: out of memory");
    boot_mark(6);

    csp_setup(&state);
    boot_mark(7);

#if !defined(CSP_EXEC_ONLY)
    csp_print_line("CandySpeak ready");
#endif
}

// --- software flow control --------------------------------------------------
// Two states, and the condition is the one that actually gates the reader: can
// we accept another byte? XOFF while we cannot, XON the moment we can again.
// A high/low-water mark with hysteresis WEDGES here -- a paste ending mid-line
// leaves a partial line queued, and if it sits above the low mark the XOFF stays
// asserted, so the peer never sends the rest of the line and nothing releases it.
//
// A real UART needs this; over USB CDC the host blocks by itself.
static void serial_xoff_set(csp_rt_t* st, uint8_t on)
{
    if (on == st->serial_xoff)
	return;
    while (!csp_lpc_uart_can_send())
	;
    Chip_UART_SendByte(CSP_LPC_UART, on ? 0x13 : 0x11);
    st->serial_xoff = on;
}

// About to stop reading the port for a while: parsing a line, and any rebuild it
// triggers, takes longer than the FIFO holds.
static void serial_hold(csp_rt_t* st)    { serial_xoff_set(st, 1); }
static void serial_release(csp_rt_t* st) { serial_xoff_set(st, csp_line_space(st) ? 0 : 1); }

static void csp_lpc_loop(void)
{
    static int first_cycle = 1;
    index_t x;

    // If setup could not build a pool, do nothing but keep the port alive, so
    // the FATAL message stays readable instead of being buried by a crash loop.
    if (state.mem == NULL || state.mem_limit == 0)
	return;

#if !defined(CSP_EXEC_ONLY)
    // Serial FIRST, unconditionally, so the prompt stays alive even when the
    // program below cannot run -- otherwise a boot where csp_rebuild failed
    // faults the board into an unresponsive state right after saying so, and
    // there is no way to /clear or edit out of it.
    //
    // Keep draining while the buffer has room, INCLUDING past a completed line:
    // the spare room is the point, since a line that adds a rule stops to
    // rebuild and the burst still coming in needs somewhere to go.
    if (!state.line_ready)
	csp_line_prompt(&state);
    while (csp_lpc_uart_available() && csp_line_space(&state)) {
	csp_line_input(&state, (char)csp_lpc_uart_read());
	serial_release(&state);
    }
    if (state.line_ready) {
	serial_hold(&state);           // about to stop reading for a while
	csp_process_line(&state, state.line_buf);
	csp_line_done(&state);
	serial_release(&state);        // the queue just shrank -- let them talk
    }
#endif

    // No leaves/tables: skip execution but keep looping (serial handled above).
    if (!state.started)
	return;

    if (first_cycle) {
	state.cycle = 1;
	first_cycle = 0;
    }
    else if (!state.paused)                // frozen while /pause is in effect
	state.cycle++;

    if (state.paused)
	return;

    csp_input(&state);
    x = state.live ? BAD_INDEX : csp_cycle(&state);   // ROM (seq) + RAM, one model
    (void)x;
    csp_commit(&state);
    csp_output(&state);

    // A running timer sets wait_ms to time-until-fire, but that must NOT gate
    // the whole loop: continuous inputs have to keep sampling at a steady rate.
    // So never sleep longer than SAMPLE_MS in one pass -- timers still fire on
    // time because csp_input_timer checks wall-clock every cycle. wait_ms only
    // bounds how long we may idle.
    {
	uint32_t remaining = (state.es.wait_ms != NOTIMEOUT)
	                   ? state.es.wait_ms : SAMPLE_MS;
	if (remaining > SAMPLE_MS)
	    remaining = SAMPLE_MS;
	while ((remaining > 0) && !state.line_ready) {
	    uint32_t chunk = (remaining < 10) ? remaining : 10;
	    csp_delay_ms(chunk);
	    remaining -= chunk;
#if !defined(CSP_EXEC_ONLY)
	    while (csp_lpc_uart_available() && csp_line_space(&state))
		csp_line_input(&state, (char)csp_lpc_uart_read());
#endif
	}
    }
}

int main(void)
{
    csp_lpc_setup();
    for (;;)
	csp_lpc_loop();
    return 0;
}
