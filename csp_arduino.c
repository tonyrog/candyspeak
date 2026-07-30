#include <Arduino.h>
// Include the Adafruit CPX library BEFORE csp.h: csp_config.h poisons the
// `float` keyword (fixpoint-only firmware), and the library's sensor headers
// have float members. Parsed here (before the poison) they are fine; our own
// CPX code below stays float-free so the poison still guards the firmware.
#ifdef CSP_CPX
#include <Adafruit_CircuitPlayground.h>
#endif
#include <stdlib.h>
#include <string.h>

// EEPROM support - not all boards have it
// AVR has real EEPROM. ESP and RP2040 emulate one in flash through a library
// with the same shape -- begin/read/write/commit, and no update(). See the shim
// at csp_eeprom_open_read.
#if defined(__AVR__) || defined(ESP32) || defined(ESP8266) || \
    defined(ARDUINO_ARCH_RP2040)
#define CSP_HAS_EEPROM 1
#include <EEPROM.h>
#elif defined(ARDUINO_ARCH_SAMD)
// SAMD has no EEPROM: emulate one in a reserved flash region (see below).
// csp_flash_samd is a vendored subset of cmaglie/FlashStorage -- the low-level
// FlashClass only. Including the library proper would drag in its EEPROMClass
// global and 1027 bytes of RAM shadow we never use; see csp_flash_samd.h.
#define CSP_HAS_FLASH_EEPROM 1
#include "csp_flash_samd.h"
#endif

// CAN. Opt in per board by defining CSP_HAS_CAN in the board Makefile -- the
// library, the transceiver and the wiring are all board decisions, and linking
// a CAN library that is not there costs flash and breaks the build.
// Expects Sandeep Mistry's arduino-CAN API (CAN.begin/parsePacket/beginPacket),
// which covers the MCP2515 shields and the SAMD/ESP32 built-in controllers.
#if defined(CSP_HAS_CAN)
// Two shapes of controller. CSP_CAN_MCP2515 is one hanging off SPI (the Adafruit
// Feather RP2040 CAN, MCP2515 + TJA1051): it has to be constructed with its chip
// select, so it is an object we own rather than a library global. Everything else
// -- SAMD/ESP32 with the controller on-die -- keeps arduino-CAN's global `CAN`.
// The recv/send bodies are identical either way; only CSP_CANDEV differs.
#if defined(CSP_CAN_MCP2515)
#include <Adafruit_MCP2515.h>
#else
#include <CAN.h>
#endif
#ifndef CSP_CAN_BITRATE
#define CSP_CAN_BITRATE 500E3
#endif
#endif

// --- NeoPixel, as a FEATURE and not a board ---------------------------------
// CSP_NEO says "this board has a NeoPixel strip", nothing about which board.
// Two backends: CPX drives its ring through the CircuitPlayground library that
// already owns it, everything else through Adafruit_NeoPixel on a pin. A board
// Makefile says -DCSP_NEO and, if the variant does not name them, the pin and
// the count.
#if defined(CSP_CPX) && !defined(CSP_NEO)
#define CSP_NEO     1
#define CSP_NEO_CPX 1          // the ring belongs to the CircuitPlayground lib
#endif

#if defined(CSP_NEO) && !defined(CSP_NEO_CPX)
#include <Adafruit_NeoPixel.h>
#ifndef CSP_NEO_PIN
#if defined(PIN_NEOPIXEL)
#define CSP_NEO_PIN PIN_NEOPIXEL      // named by the board variant
#else
#error "CSP_NEO needs CSP_NEO_PIN (the variant does not define PIN_NEOPIXEL)"
#endif
#endif
#ifndef CSP_NEO_COUNT
#define CSP_NEO_COUNT 1               // a Feather has one; a ring says otherwise
#endif
#endif

#define CSP_EMBEDDED 1
#include "csp.h"
#include "csp_print.h"
#include "csp_strings.h"

#if defined(__AVR__)
#define INPUT_PULLDOWN INPUT
#endif

// --- Adafruit Circuit Playground Express (SAMD21) -------------------------
// Port convention used by the .csp:  port 8 = accelerometer (pin 0/1/2 = X/Y/Z),
// port 9 = NeoPixel ring (pin = index, a.val holds an RGB565 "fake DAC" value).
// CPX is ARM, so RODATA/ro_* are plain there (no PROGMEM special case).
#ifdef CSP_CPX
// (Adafruit_CircuitPlayground.h is included above, before csp.h's float poison)
#define PORT_ACCEL 8
#endif

// --- the NeoPixel feature ---------------------------------------------------
// An #analog on CSP_NEO_PORT is a pixel: `pin` is the index in the strip and
// the value is RGB565, a 16-bit colour that fits a.val exactly. Writes are
// buffered and pushed once per cycle -- show() is a blocking bit-banged burst,
// so doing it per pixel would cost the cycle time of the whole program.
#if defined(CSP_NEO)
#ifndef CSP_NEO_PORT
#define CSP_NEO_PORT 9
#endif
#define MINLEV 2
#define MAXLEV 50
uint16_t lev = MAXLEV;                 // brightness, patchable from a rule

static int csp_neo_dirty = 0;          // show() once per cycle if a pixel moved

// RGB565 -> 0x00RRGGBB
static uint32_t csp_neo_565(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return ((uint32_t)(r << 3) << 16) | ((uint32_t)(g << 2) << 8) | (b << 3);
}

#if defined(CSP_NEO_CPX)
#define csp_neo_pixel(i,v) CircuitPlayground.strip.setPixelColor((i), (v))
#define csp_neo_push()     do { CircuitPlayground.strip.setBrightness(lev); \
				CircuitPlayground.strip.show(); } while (0)
#define csp_neo_begin()    ((void)0)   // CircuitPlayground.begin() did it
#else
static Adafruit_NeoPixel csp_neo(CSP_NEO_COUNT, CSP_NEO_PIN, NEO_GRB + NEO_KHZ800);
#define csp_neo_pixel(i,v) csp_neo.setPixelColor((i), (v))
#define csp_neo_push()     do { csp_neo.setBrightness(lev); csp_neo.show(); } while (0)
static void csp_neo_begin(void)
{
    // Several Adafruit boards gate the pixel's supply so it draws nothing when
    // unused -- the Feather RP2040 family names it NEOPIXEL_POWER. Without this
    // the pixel is simply dark and every other sign says the code ran.
#if defined(NEOPIXEL_POWER)
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
    csp_neo.begin();
    csp_neo.setBrightness(lev);
    csp_neo.show();                    // all off, and the line driven low
}
#endif

// Called from csp_board_analog_output in both board branches.
static int csp_neo_write(value_t* vptr)
{
    if (vptr->a.port != CSP_NEO_PORT)
	return 0;
    csp_neo_pixel(vptr->a.pin, csp_neo_565(vptr->a.val));
    csp_neo_dirty = 1;
    return 1;
}
#endif

csp_rt_t state;

extern char __StackTop;

int stack_used(void)
{
    char local;
    return &__StackTop - &local;
}

// raw_free(): the actual free RAM right now, heap-top..stack. It EXCLUDES the
// .bss struct AND (with the static-arena backend) the static arena, since both
// are .bss below the heap.
// Free RAM right now, heap-top..stack. Selected by target EXPLICITLY: the AVR
// branch reads avr-libc's __heap_start/__brkval, and it used to be the `#else`,
// which quietly claimed every non-ARM target -- an ESP32 build reached the
// linker before anyone found out.
#if defined(ESP32) || defined(ESP8266)
static uint32_t raw_free()
{
    return (uint32_t)ESP.getFreeHeap();   // the SDK already tracks this
}
#elif defined(__arm__)
extern "C" char* sbrk(int incr);
static uint32_t raw_free()
{
    char top;
    return (uint32_t)(&top - reinterpret_cast<char*>(sbrk(0)));
}
#elif defined(__AVR__)
extern unsigned int __heap_start, *__brkval;
static uint32_t raw_free()
{
    int free_memory;
    if ((int)__brkval == 0)
	free_memory = ((int)&free_memory) - ((int)&__heap_start);
    else
	free_memory = ((int)&free_memory) - ((int)__brkval);
    return (uint32_t)free_memory;
}
#else
// Unknown target: say nothing rather than something wrong. The arena falls back
// to its static size and /memory reports 0 free, which is visibly a non-answer.
static uint32_t raw_free() { return 0; }
#endif

EXTERN_C_BEGIN


// avail = RAM the pool may claim, INCLUDING the struct (added back because
// csp_mem_init subtracts it out again; the struct is already placed in .bss).
// Used only at boot to size the claim -- must not depend on mem_limit, which is
// still being computed then.
uint32_t csp_system_ram_avail()
{
    return raw_free() + sizeof(csp_rt_t);
}

// A board we have no figure for. Kept as a distinct value rather than 0 so
// callers can tell "no idea" from "none", and so the arithmetic below refuses
// to produce a number instead of producing a wrong one -- /memory printed
// "RAM 4294967295 total, system -254872" before this was handled.
#define CSP_RAM_UNKNOWN 0xFFFFFFFFu

uint32_t csp_system_ram_capacity()
{
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
    return 2048; // Arduino Uno, Nano Pro Mini
#elif defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
    return 8192; // Arduino Mega
#elif defined(__AVR_ATmega32U4__)
    return 2560; // Arduino Leonardo, Micro
#elif defined(ARDUINO_ARCH_SAMD)
    return 32768; // Arduino Zero / M0 (32KB)
#elif defined(ESP32) || defined(ESP8266)
    return (uint32_t)ESP.getHeapSize();   // varies by chip and partition table
#elif defined(ARDUINO_ARCH_RP2040)
    return 264*1024;  // RP2040: 264 KB SRAM (six banks, contiguous)
#else
    return CSP_RAM_UNKNOWN;
#endif
}

// system = core + libraries: everything present minus CandySpeak's own pool and
// struct. Subtracting mem_limit is what stops the (static) arena, which sits in
// .bss and so is excluded from raw_free(), from being double-counted as system.
uint32_t csp_system_ram_used()
{
    uint32_t cap = csp_system_ram_capacity();
    uint32_t total;
    if (cap == CSP_RAM_UNKNOWN)
	return 0;                     // no capacity, no meaningful "used"
    total = cap - raw_free();         // all statics + stack used
    {
	uint32_t ours = (uint32_t)state.mem_limit + (uint32_t)sizeof(csp_rt_t);
	return (total > ours) ? (total - ours) : 0;
    }
}

uint32_t csp_time_ms(void)
{
    return millis();
}

unsigned long csp_time_us(void)
{
    return micros();
}

static long serial_output = 0;

void* csp_set_file_output(void* f)
{
    long prev = serial_output;
    serial_output = (long) f;
    return (void*) prev;
}

int csp_will_output()
{
    return (serial_output != 0);
}

// platform print functions
//
// The CR belongs HERE, not in csp_println. The runtime ends a line three ways
// -- csp_println(), a bare csp_print_char('\n'), and a '\n' inside a
// csp_print_lit("...\n") literal -- and Serial.print of a literal never passes
// through println, so translating there left the board emitting 37 bare LFs
// against 22 CRLFs. Every byte the runtime prints goes through these three
// functions, so this is the one place that sees them all.
//
// Serial.print(s) is given up for a per-character loop to get that: on a
// buffered UART the cost is noise next to the transmission itself.
//
// Returns LOGICAL characters -- a newline counts as one whatever it costs on
// the wire -- so csp_print_just's column arithmetic matches the host's.
int csp_print_char(char c)
{
    if (serial_output) {
	if (c == '\n')
	    Serial.write('\r');
	Serial.write(c);
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

#if defined(__AVR__)
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
#else
int csp_print_rostr(rostring_t s)
{
    return csp_print_str((const char*) s);
}
#endif

void csp_flush(void)
{
}

EXTERN_C_END


int csp_uconst(csp_rt_t* st, const char* name, int len, ivalue_t* ret)
{
    // handle constants D0..D9
    if ((len == 2) && (name[0]=='D') &&
	(name[1]>='0') && (name[1]<='9')) {
	int d = name[1]-'0';
	*ret = d;
	return 1;
    }
    else if ((len == 3) && (name[0]=='D') &&
	     (name[1]>='0') && (name[1]<='9') &&
	     (name[2]>='0') && (name[2]<='9')) {
	int d = (name[1]-'0')*10 + (name[2]-'0');
	*ret = d;
	return 1;
    }
    return 0;
}

#ifdef CSP_CPX
void csp_board_setup(csp_rt_t* st)
{
    CircuitPlayground.begin();   // owns NeoPixels, accelerometer, sensors
    csp_neo_begin();
}

// Base sampling period (ms). Bounds the loop's idle sleep so continuous inputs
// (accel, sensors) and the reactive display keep updating at ~100 Hz even when a
// slow timer is armed. Timers stay accurate to within one SAMPLE_MS tick.
#define SAMPLE_MS 10

int accel_read = 0;   // read the LIS3DH once per cycle, not once per axis

// start input cycle
void csp_board_start_input(csp_rt_t* st)
{
    accel_read = 0;
}

void csp_board_start_output(csp_rt_t* st)
{
    csp_neo_dirty = 0;
}

// push the frame once per cycle (dirty is only set while not latched)
void csp_board_stop_output(csp_rt_t* st)
{
    if (csp_neo_dirty) {
	csp_neo_push();
	csp_neo_dirty = 0;
    }
}

// float-free reads (soft-float is big/slow on the M0+, and the
// core is built fixpoint): use the raw LIS3DH ints and analogRead.

void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    int value;
    if (vptr->a.port == PORT_ACCEL) {   // accelerometer X/Y/Z (raw)
	if (!accel_read) {
	    CircuitPlayground.lis.read();
	    accel_read = 1;
	}
	value = (vptr->a.pin == 0) ? CircuitPlayground.lis.x
	    : (vptr->a.pin == 1) ? CircuitPlayground.lis.y
	    : CircuitPlayground.lis.z;
	// a.val is unsigned:16 -> a signed reading wraps to ~65000.
	// Offset+clamp to 0..1023 (unsigned 10-bit): 512 = flat,
	// ~1g tilt swings to the ends. Rule: Idx = AccX / 103.
	value = (value >> 5) + 512;
	if (value < 0) value = 0; else if (value > 1023) value = 1023;
    }
    else if (vptr->a.pin == 8)               // A8 light sensor
	value = CircuitPlayground.lightSensor();
    else if (vptr->a.pin == 9)              // A9 thermistor (raw ADC)
	value = analogRead(A9);
    else
	value = analogRead(vptr->a.pin);

    csp_set_ivalue(st, ix, value);    
}

void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
    if (csp_neo_write(vptr))
	;                             // an #analog on the NeoPixel port
    else if (vptr->a.pwm) {
	int val = map(vptr->a.val, 0, (1<<decl(st,di,res))-1, 0, 255);
	analogWrite(vptr->a.pin, val);
    }	
}

#else

void csp_board_setup(csp_rt_t* st)
{
    (void)st;
#if defined(CSP_NEO)
    csp_neo_begin();
#endif
}

void csp_board_start_input(csp_rt_t* st)
{
    (void)st;
}

void csp_board_start_output(csp_rt_t* st)
{
    (void)st;
}

// push the strip once per cycle: show() is a blocking bit-banged burst, so one
// per changed pixel would dominate the cycle time.
void csp_board_stop_output(csp_rt_t* st)
{
    (void)st;
#if defined(CSP_NEO)
    if (csp_neo_dirty) {
	csp_neo_push();
	csp_neo_dirty = 0;
    }
#endif
}

void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    csp_set_ivalue(st, ix, analogRead(vptr->a.pin));    
}

// handle type! accept float as well
void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
#if defined(CSP_NEO)
    if (csp_neo_write(vptr))
	return;                       // an #analog on the NeoPixel port
#endif
    if (vptr->a.pwm) {
	int val = map(vptr->a.val, 0, (1<<decl(st,di,res))-1, 0, 255);
	analogWrite(vptr->a.pin, val);
    }
}

#endif

// The generic board routines
void csp_board_digital_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    int value = digitalRead(vptr->d.pin);
    csp_set_ivalue(st, ix, value);    
}

void csp_board_digital_output(csp_rt_t* st, value_t* vptr)
{
    if (vptr->d.dir & DIR_IN) {  // in & out
	pinMode(vptr->d.pin, OUTPUT);
	digitalWrite(vptr->d.pin, (vptr->d.val & 1));
	// prepare for next input
	if (vptr->d.pullup)
	    pinMode(vptr->d.pin, INPUT_PULLUP);
	else
	    pinMode(vptr->d.pin, INPUT);
    }
    else { // plain out
	digitalWrite(vptr->d.pin, (vptr->d.val & 1));
    }
}

// The single description of what a digital slot's configuration MEANS in
// hardware. Setup, a rule that writes .dir/.pullup/.pulldown, and anything that
// has to force pins into a known state all come here, so those paths cannot
// drift apart -- which is how a pin ends up configured one way and driven
// another.
//
// An inout pin RESTS as an input: csp_board_digital_output above borrows it for
// the length of one write and hands it straight back. A pin with no direction
// at all is left untouched; the program said nothing about it, and asserting a
// mode on a pin someone else owns is worse than leaving it alone.
void csp_board_digital_config(value_t* vptr)
{
    if (vptr->d.dir & DIR_IN) {
	if (vptr->d.pullup)
	    pinMode(vptr->d.pin, INPUT_PULLUP);
	else if (vptr->d.pulldown)
	    pinMode(vptr->d.pin, INPUT_PULLDOWN);
	else
	    pinMode(vptr->d.pin, INPUT);
    }
    else if (vptr->d.dir & DIR_OUT)
	pinMode(vptr->d.pin, OUTPUT);
}

// The analog counterpart. Only a PWM output owns its pin in a way that has to be
// asserted -- analogRead needs no mode at all -- so an input direction puts the
// pin back to INPUT and nothing else is touched.
//
// This runs ONLY when a rule wrote a configuration part, never at setup. That
// matters on a board where an #analog names a sensor through its port rather
// than a pin (CPX: port 8 is the accelerometer, port 9 the NeoPixel ring):
// asserting a mode on those at boot would reach pins nobody asked about, while
// a program that writes .dir on one has asked for exactly this.
void csp_board_analog_config(value_t* vptr)
{
    if ((vptr->a.dir & DIR_OUT) && vptr->a.pwm)
	pinMode(vptr->a.pin, OUTPUT);
    else if (vptr->a.dir & DIR_IN)
	pinMode(vptr->a.pin, INPUT);
}

// Apply a configuration a rule asked for, and take the request down in BOTH
// slots. The DIN/DOUT pair is copied on commit, so clearing one leaves a stale
// request in the other that would spend a pinMode on some later cycle. Same
// shape as csp_output_timer's handling of running/fired.
//
// d.cfg and a.cfg do NOT land on the same bit -- digital has pullup and
// pulldown ahead of it, analog only pwm -- so the flag is cleared through the
// member that was set. Clearing the wrong one leaves the request standing.
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
    unsigned res = 0;

    csp_board_setup(st);
    csp_can_init(st);

    // One pass over the device list. Configuration is read from the value SLOT,
    // not from the declaration, so this is the same source of truth the runtime
    // gates on -- setup_digital has already copied the declaration in by now.
    //
    // If the same physical pin is declared twice under two names, the LAST
    // declaration decides its mode, because it configures last. That is the
    // same "last one wins" the rest of the language patches by; it used to be
    // decided by which phase ran second, which was an accident.
    for (i = 0; i < st->nio; i++) {
	index_t ix = st->io[i];
	int j = INDEX(ix);
	value_t* vptr = csp_dio_slot(st, ix, DOUT);
	switch(decl(st,j,type)) {
	case DECL_DIGITAL:
	    csp_board_digital_config(vptr);
	    break;
	case DECL_ANALOG:
	    if ((vptr->a.dir & DIR_IN) && decl(st,j,res))
		res = max(res, decl(st,j,res));
	    // A PWM output needs the pin driven; an analog input does not need
	    // any mode at all, so nothing is asserted for it.
	    if ((vptr->a.dir & DIR_OUT) && !(vptr->a.dir & DIR_IN) && vptr->a.pwm)
		pinMode(vptr->a.pin, OUTPUT);
	    break;
	default:
	    break;
	}
    }

//    if (res)
//	analogReadResolution(res);
}

void csp_input(csp_rt_t* st)
{
    int i;

    csp_board_start_input(st);

    for (i = 0; i < st->nio; i++) {
	index_t ix = st->io[i];
	int di = INDEX(ix);
	value_t* vptr;	
	int vi = st_index(st, ix);
	switch(decl(st,di,type)) {
	case DECL_DIGITAL:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    // A rule may have turned this pin round since we last looked. Do it
	    // before reading, or the first sample after a flip comes off the old
	    // mode. Both loops check: whichever list the pin is in, it is served.
	    if (vptr->d.cfg)
		csp_apply_config(st, ix, vptr, 0);
	    if (vptr->d.dir & DIR_IN)
		csp_board_digital_input(st, ix, vptr);
	    break;
	case DECL_ANALOG:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->a.cfg)
		csp_apply_config(st, ix, vptr, 1);
	    if (vptr->a.dir & DIR_IN) {
		csp_board_analog_input(st, ix, vptr);
	    }
	    break;
	default: break;
	}
    }
    csp_can_input(st);
    csp_input_timer(st);
}

// ============================================================
// CAN backend
// ============================================================

#if defined(CSP_HAS_CAN)

#if defined(CSP_CAN_MCP2515)
// Pin defaults come from the board variant when it names them -- the Adafruit
// Feather RP2040 CAN defines PIN_CAN_CS/PIN_CAN_STANDBY -- so a board Makefile
// only has to say CSP_CAN_MCP2515. Override with -DCSP_CAN_CS=n for a shield on
// a board that knows nothing about CAN.
#ifndef CSP_CAN_CS
#if defined(PIN_CAN_CS)
#define CSP_CAN_CS PIN_CAN_CS
#else
#define CSP_CAN_CS 10           // arduino-CAN's historical default
#endif
#endif
#if !defined(CSP_CAN_STANDBY) && defined(PIN_CAN_STANDBY)
#define CSP_CAN_STANDBY PIN_CAN_STANDBY
#endif

static Adafruit_MCP2515 csp_mcp2515(CSP_CAN_CS);
#define CSP_CANDEV csp_mcp2515
#else
#define CSP_CANDEV CAN
#endif

int csp_can_init(csp_rt_t* st)
{
    (void)st;
#if defined(CSP_CAN_MCP2515) && defined(CSP_CAN_STANDBY)
    // The transceiver sleeps until STANDBY is pulled low. Miss this and the bus
    // is silent while every other symptom says the controller came up fine --
    // begin() succeeds, frames get queued, nothing ever appears on the wire.
    pinMode(CSP_CAN_STANDBY, OUTPUT);
    digitalWrite(CSP_CAN_STANDBY, LOW);
#endif
    // PIN_CAN_RESET is deliberately left alone. begin() issues the MCP2515's
    // software reset over SPI, so the hardware line is not needed -- and driving
    // it with the polarity guessed wrong would hold the chip in reset, which
    // looks exactly like the standby mistake above.
    if (!CSP_CANDEV.begin(CSP_CAN_BITRATE)) {
	csp_print_line("can: init failed");
	return -1;
    }
    // No setClockFrequency call: the library's default is 16 MHz and that is
    // what the Feather RP2040 CAN has. A board with an 8 MHz crystal needs
    // -DCSP_CAN_CLOCK=8000000 and a call here, or every bitrate is half.
#if defined(CSP_CAN_MCP2515) && defined(CSP_CAN_CLOCK)
    csp_mcp2515.setClockFrequency(CSP_CAN_CLOCK);
#endif
    return 0;
}

int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    int n, i;
    (void)st;

    if ((n = CSP_CANDEV.parsePacket()) <= 0)
	return 0;
    if (CSP_CANDEV.packetRtr())            // remote request carries no data
	return 0;
    if (n > 8) n = 8;               // classic CAN
    *id = (uint32_t)CSP_CANDEV.packetId();
    for (i = 0; i < n; i++)
	data[i] = (uint8_t)CSP_CANDEV.read();
    *len = (uint8_t)n;
    return 1;
}

int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st;
    if (len > 8) len = 8;
    // Anything past the 11-bit standard id range goes out extended.
    if (id > 0x7FF)
	CSP_CANDEV.beginExtendedPacket(id);
    else
	CSP_CANDEV.beginPacket((int)id);
    CSP_CANDEV.write(data, len);
    return CSP_CANDEV.endPacket() ? 0 : -1;
}

#else   /* no CAN on this board: stubs, so CAN still parses and runs dry */

int csp_can_init(csp_rt_t* st) { (void)st; return 0; }
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

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {  // allow output
	csp_board_start_output(st);

	for (i = 0; i < st->nio; ++i) {
	    index_t ix = st->io[i];
	    int di = INDEX(ix);
	    value_t* vptr;
	    switch(decl(st,di,type)) {
	    case DECL_DIGITAL:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->d.cfg)
		    csp_apply_config(st, ix, vptr, 0);
		if (vptr->d.dir & DIR_OUT) {
		    csp_board_digital_output(st, vptr);
		}
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
	csp_can_output(st);
	csp_board_stop_output(st);
    }
    csp_output_timer(st);
}


// ============================================================
// Storage functions
// ============================================================

#ifdef CSP_HAS_EEPROM

int eeprom_addr = -1;

#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_ARCH_RP2040)
// A flash-emulated EEPROM (ESP, and RP2040 through the Philhower core, which
// uses the last flash sector). Three things follow that do not apply to AVR:
//
//   begin(size)  must be called before ANYTHING works -- length() is 0 until it
//                has run, so an un-begun EEPROM reports zero capacity and every
//                write is refused as "too big".
//   commit()     is what actually reaches flash. Writes land in a RAM buffer,
//                so without it a save looks perfect and is gone after a reboot.
//   update()     does not exist on EEPROMClass. It is read-compare-write, and
//                on a flash-emulated EEPROM it matters MORE than on real
//                EEPROM: commit() rewrites a whole sector, so a byte that did
//                not change is a sector erase nobody asked for.
// One flash sector on RP2040, and a sane default on ESP. begin() allocates a
// RAM buffer of this size, so it is not free -- but it is also the whole
// persistent store, so undersizing it costs saves.
#ifndef CSP_EEPROM_SIZE
#define CSP_EEPROM_SIZE 4096
#endif

static int ee_ready = 0;
static int ee_dirty = 0;

static void ee_begin_once(void)
{
    if (!ee_ready) {
	EEPROM.begin(CSP_EEPROM_SIZE);
	ee_ready = 1;
    }
}

static void ee_update(int addr, uint8_t v)
{
    if (EEPROM.read(addr) != v) {
	EEPROM.write(addr, v);
	ee_dirty = 1;
    }
}

static void ee_flush(void)
{
    if (ee_dirty) {
	EEPROM.commit();
	ee_dirty = 0;
    }
}
#else
// AVR writes straight through and its library already has update().
#define ee_begin_once() ((void)0)
#define ee_flush()      ((void)0)
static void ee_update(int addr, uint8_t v) { EEPROM.update(addr, v); }
#endif

int csp_eeprom_open_read(void)
{
    ee_begin_once();
    eeprom_addr = 0;
    return 0;
}

int csp_eeprom_open_write(void)
{
    ee_begin_once();
    eeprom_addr = 0;
    return 0;
}

void csp_eeprom_close(void)
{
    ee_flush();               // no-op unless something actually changed
    eeprom_addr = -1;
}

int csp_eeprom_read(void* buf, size_t len)
{
    uint8_t* ptr = (uint8_t*) buf;
    if (eeprom_addr < 0)
	return -1;
    while(len--) {
	*ptr++ = EEPROM.read(eeprom_addr);
	eeprom_addr++;
    }
    return 0;
}

uint32_t csp_eeprom_capacity(void)
{
    ee_begin_once();                     // ESP: length() is 0 until begin() ran
    return (uint32_t) EEPROM.length();   // AVR: E2END+1 (uno 1K, mega 4K)
}

int csp_eeprom_write(const void* buf, size_t len)
{
    uint8_t* ptr = (uint8_t*) buf;
    if (eeprom_addr < 0)
	return -1;
    // Refuse to run off the end: a write past the last cell wraps or corrupts,
    // so a program too big to persist would half-save silently.
    if ((uint32_t)eeprom_addr + len > csp_eeprom_capacity())
	return -1;
    while(len--) {
	ee_update(eeprom_addr, *ptr++);
	eeprom_addr++;
    }
    return 0;
}

#elif defined(CSP_HAS_FLASH_EEPROM)

// SAMD21 has no EEPROM, so emulate one in a reserved flash region.
//
// We deliberately do NOT use the library's own FlashAsEEPROM layer: it keeps a
// RAM shadow of the WHOLE emulated region (byte data[EEPROM_EMULATION_SIZE]),
// because its API allows scattered writes with a deferred commit and so must
// read-modify-write. Our access is strictly sequential and csp_eeprom_save
// rewrites the entire image every time -- we never need to preserve what is
// already there. So: erase the region up front, then stream forward through one
// row buffer. That costs CSP_FLASH_ROW bytes of RAM instead of the region size.
//
// Reads cost nothing at all: SAMD flash is memory-mapped, so a read is a memcpy
// straight out of the region.
//
// Geometry (FlashClass): erase works per ROW, write per PAGE with a page-aligned
// destination and no auto-erase. SAMD21 ROW_SIZE = PAGE_SIZE*4 = 256, and
// FlashClass::write() loops the page writes for us, so writing 256-aligned
// chunks satisfies the page alignment automatically.
#ifndef CSP_EEPROM_FLASH_SIZE
#define CSP_EEPROM_FLASH_SIZE 2048   // must be a multiple of CSP_FLASH_ROW
#endif
#define CSP_FLASH_ROW 256            // SAMD21 erase granularity

// const => the linker places this in flash; aligned to the erase unit so erasing
// it cannot touch anything else.
__attribute__((__aligned__(CSP_FLASH_ROW)))
static const uint8_t eeprom_region[CSP_EEPROM_FLASH_SIZE] = { 0 };

static FlashClass eeprom_flash(eeprom_region, sizeof(eeprom_region));

static uint32_t ee_pos;        // read cursor (byte offset into the region)
static uint32_t ee_row_base;   // flash offset that ee_row[0] maps to
static uint32_t ee_row_used;   // bytes buffered in ee_row
static int      ee_writing;
static uint8_t  ee_row[CSP_FLASH_ROW];

uint32_t csp_eeprom_capacity(void)
{
    return CSP_EEPROM_FLASH_SIZE;
}

int csp_eeprom_open_read(void)
{
    ee_pos = 0;
    ee_writing = 0;
    return 0;
}

int csp_eeprom_open_write(void)
{
    // Erase everything now: we are about to overwrite the whole image anyway,
    // which is exactly why no read-modify-write (and no shadow) is needed.
    eeprom_flash.erase(eeprom_region, sizeof(eeprom_region));
    ee_row_base = 0;
    ee_row_used = 0;
    ee_writing = 1;
    return 0;
}

int csp_eeprom_read(void* buf, size_t len)
{
    if (ee_writing)
	return -1;
    if (ee_pos + len > CSP_EEPROM_FLASH_SIZE)
	return -1;
    memcpy(buf, eeprom_region + ee_pos, len);   // memory-mapped: no buffer needed
    ee_pos += len;
    return 0;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    const uint8_t* p = (const uint8_t*) buf;
    if (!ee_writing)
	return -1;
    if (ee_row_base + ee_row_used + len > CSP_EEPROM_FLASH_SIZE)
	return -1;   // too big to persist: fail loudly rather than wrap
    while (len > 0) {
	uint32_t n = CSP_FLASH_ROW - ee_row_used;
	if (n > len) n = (uint32_t) len;
	memcpy(ee_row + ee_row_used, p, n);
	ee_row_used += n;
	p += n;
	len -= n;
	if (ee_row_used == CSP_FLASH_ROW) {      // row full -> commit it
	    eeprom_flash.write(eeprom_region + ee_row_base, ee_row, CSP_FLASH_ROW);
	    ee_row_base += CSP_FLASH_ROW;
	    ee_row_used = 0;
	}
    }
    return 0;
}

void csp_eeprom_close(void)
{
    if (ee_writing && (ee_row_used > 0)) {    // flush the partial tail row
	// Pad with 0xFF: the region was erased, so those cells stay untouched.
	memset(ee_row + ee_row_used, 0xFF, CSP_FLASH_ROW - ee_row_used);
	eeprom_flash.write(eeprom_region + ee_row_base, ee_row, CSP_FLASH_ROW);
    }
    ee_writing = 0;
}

#else

int eeprom_addr = -1;

int csp_eeprom_open_read(void)
{
    eeprom_addr = 0;
    return 0;
}

int csp_eeprom_open_write(void)
{
    eeprom_addr = 0;
    return 0;
}

void csp_eeprom_close(void)
{
    eeprom_addr = -1;
}

// This board has no persistent storage (SAMD21 has no EEPROM; it needs a
// flash-emulation library). Report that honestly rather than accepting the call:
// these used to return 0, so /save printed "Saved" and wrote nothing.
uint32_t csp_eeprom_capacity(void)
{
    return 0;
}

int csp_eeprom_read(void* buf, size_t len)
{
    (void)buf; (void)len;
    return -1;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    (void)buf; (void)len;
    return -1;
}

#endif



// ============================================================
// Serial command interface
// ============================================================

// Platform-specific command implementations
// The store has no filename on a board; /save and /load are shared code in
// csp_rt.c and just need something to call it.
const char* csp_eeprom_name(void)
{
    static const char nm[] = "EEPROM";
    return nm;
}

// ============================================================
// Arduino setup/loop
// ============================================================

// Base loop sampling period (ms). Bounds the idle sleep so continuous inputs and
// the reactive display keep updating even when a slow timer is armed. Boards with
// their own value (e.g. CPX above) define it first; this is the default for the
// rest.
#ifndef SAMPLE_MS
#define SAMPLE_MS 10
#endif

void setup()
{
    uint32_t t0;

    Serial.begin(115200);
    // Bounded wait: never hang forever if the USB port is never opened.
    t0 = millis();
    while (!Serial && (millis() - t0) < 3000)
	;

    serial_output = 1;

    // Report the real memory picture on the board -- the numbers we can only
    // model on the host. free is what csp_mem_init claims the pool from.
    csp_print_lit("boot: RAM "); csp_print_uint(csp_system_ram_capacity());
    csp_print_lit(", free ");    csp_print_uint(csp_system_ram_avail());
    csp_print_lit(", struct ");  csp_print_uint((uint32_t)sizeof(csp_rt_t));
    csp_println();

    // A failed init leaves a half-set-up state; say so instead of running into a
    // fault. This is where an over-eager claim (freeRam - reserve too tight)
    // would surface, rather than as a mystery hang.
    if (csp_rt_init(&state, REACTIVE_DEFAULT) < 0) {
	csp_print_line("FATAL: csp_rt_init failed (out of memory)");
	return;   // leave loop() a no-op rather than crash
    }
    csp_print_lit("pool "); csp_print_uint((uint32_t)state.mem_limit);
    csp_println();

    // Wire up the ROM firmware (rom.c) first, so the program runs even when
    // there is no valid save. csp_eeprom_load re-does this on its success path,
    // but on failure (no save / wrong version / wrong firmware) it returns
    // before touching ROM -- so we must load it here.
    csp_load_rom(&state);

    // Overlay any saved RAM patches on top of the ROM. Returns 0 on success.
    if (csp_eeprom_load(&state) == 0) {
        csp_print_line("Loaded from EEPROM");
    }
    else {
	csp_clr_error(&state);   // "no saved state" is the normal case at boot,
				 // not an error to carry into the first command
        csp_print_line("No saved state, running ROM");
    }

    // Lay out the whole program: reactive graph + leaf/device setup. MUST be
    // csp_rebuild, not csp_rt_start alone -- rebuild resets the middle bump
    // allocator (csp_mid_reset) that every derived table is carved from. Calling
    // rt_start directly leaves mid_end = 0, so every table allocation fails and
    // the first cycle faults on null view/heap pointers.
    if (csp_rebuild(&state) < 0)
	csp_print_line("setup failed: out of memory");

    // initialize input/output/timers ...
    csp_setup(&state);

    csp_print_line("CandySpeak ready");
}

void loop()
{
    static int first_cycle = 1;
    index_t x;
    int anyd;

    // If setup could not build a pool, do nothing but let USB/Serial live, so the
    // FATAL message above is readable instead of being buried by a crash loop.
    if (state.mem == NULL || state.mem_limit == 0)
	return;

    // Serial FIRST, unconditionally, so the prompt stays alive even when the
    // program below cannot run. Without this a boot where csp_rebuild failed
    // (mem != NULL but the derived tables did not fit -> started == 0) fell
    // through to csp_cycle and read NULL view/heap, faulting the board into an
    // unresponsive state right after "setup failed". Now the user can /memory,
    // /clear, or edit their way out instead.
    while (Serial.available())
	csp_line_input(Serial.read());
    if (csp_line_ready) {
	Serial.write(0x13);   // XOFF -- pace pasted input (see below)
	csp_process_line(&state, csp_line_buf);
	Serial.write(0x11);   // XON
	csp_line_ready = 0;
	csp_line_pos = 0;
    }
    // No leaves/tables: skip all execution but keep looping (serial handled above).
    if (!state.started)
	return;

    if (first_cycle) {
	state.cycle = 1;
	first_cycle = 0;
    }
    else if (!state.paused)     // frozen while /pause is in effect
	state.cycle++;

    // Pasted input pacing note: parsing a line (and any rt_start) is slow enough
    // to overflow the UART RX buffer, so csp_process_line above brackets itself
    // with XOFF/XON. Enable "software flow control" in the terminal (minicom:
    // Ctrl-A O -> Serial port setup -> G = Yes).

    // /pause freezes execution: serial was already handled at the top so /resume
    // and edits still work; run no input/cycle/commit/output.
    if (state.paused)
	return;

    // run evaluation cycle. /live freezes the rules (skip csp_cycle) but keeps I/O
    // running, so immediate commands drive outputs and inputs keep sampling.
    csp_input(&state);
    x = state.live ? BAD_INDEX : csp_cycle(&state);   // ROM (seq) + RAM, one model
    anyd = state.anyd;  // save before commit clears it
    csp_commit(&state);
    csp_output(&state);

    // A running timer sets wait_ms to time-until-fire, but that must NOT gate the
    // whole loop: continuous inputs (accelerometer, light) and the reactive
    // display have to keep sampling at a steady rate. So never sleep longer than
    // SAMPLE_MS in one pass -- timers still fire on time because csp_input_timer
    // checks wall-clock every cycle. wait_ms only bounds how long we may idle.
    {
	uint32_t remaining = (state.wait_ms != NOTIMEOUT) ? state.wait_ms : SAMPLE_MS;
	if (remaining > SAMPLE_MS)
	    remaining = SAMPLE_MS;   // cap: sample rate wins over timer wait
	while ((remaining > 0) && !csp_line_ready) {
	    uint32_t chunk = min(remaining, (uint32_t)10);
	    delay(chunk);
	    remaining -= chunk;
	    while (Serial.available())   // drain fully, not one byte per chunk
		csp_line_input(Serial.read());
	}
    }
}
