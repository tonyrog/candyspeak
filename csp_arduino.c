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
#if defined(__AVR__) || defined(ESP32) || defined(ESP8266)
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
#include <CAN.h>
#ifndef CSP_CAN_BITRATE
#define CSP_CAN_BITRATE 500E3
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
#define PORT_NEO   9
// RGB565 -> 0x00RRGGBB for the NeoPixels
static uint32_t cpx_565(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return ((uint32_t)(r << 3) << 16) | ((uint32_t)(g << 2) << 8) | (b << 3);
}
static int cpx_neo_dirty = 0;   // strip.show() once per cycle if a pixel changed
#define MINLEV 2
#define MAXLEV 50

uint16_t lev = MAXLEV;

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
#ifdef __arm__
extern "C" char* sbrk(int incr);
static uint32_t raw_free()
{
    char top;
    return (uint32_t)(&top - reinterpret_cast<char*>(sbrk(0)));
}
#else
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
#else
    return -1; // Unknown board
#endif
}

// system = core + libraries: everything present minus CandySpeak's own pool and
// struct. Subtracting mem_limit is what stops the (static) arena, which sits in
// .bss and so is excluded from raw_free(), from being double-counted as system.
uint32_t csp_system_ram_used()
{
    uint32_t total = csp_system_ram_capacity() - raw_free();  // all statics + stack used
    uint32_t ours  = (uint32_t)state.mem_limit + (uint32_t)sizeof(csp_rt_t);
    return (total > ours) ? (total - ours) : 0;
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
    CircuitPlayground.strip.setBrightness(lev);
    CircuitPlayground.strip.show();
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
    cpx_neo_dirty = 0;    
}

// push the frame once per cycle (dirty is only set while not latched)
void csp_board_stop_output(csp_rt_t* st)
{
    if (cpx_neo_dirty) {
	CircuitPlayground.strip.setBrightness(lev);
	CircuitPlayground.strip.show();
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
    if (vptr->a.port == PORT_NEO) {   // NeoPixel: RGB565 -> pixel
	CircuitPlayground.strip.setPixelColor(vptr->a.pin,
					      cpx_565(vptr->a.val));
	cpx_neo_dirty = 1; // need update
    }
    else if (vptr->a.pwm) {
	int val = map(vptr->a.val, 0, (1<<decl(st,di,res))-1, 0, 255);
	analogWrite(vptr->a.pin, val);
    }	
}

#else

void csp_board_setup(csp_rt_t* st)
{
}

void csp_board_start_input(csp_rt_t* st)
{
}

void csp_board_start_output(csp_rt_t* st)
{
}

// push the frame once per cycle (dirty is only set while not latched)
void csp_board_stop_output(csp_rt_t* st)
{
}

void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    csp_set_ivalue(st, ix, analogRead(vptr->a.pin));    
}

// handle type! accept float as well
void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
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


void csp_setup(csp_rt_t* st)
{
    int i;
    unsigned res = 0;

    csp_board_setup(st);
    csp_can_init(st);

    // setup in and inout (inout startup as input)
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int j = INDEX(ix);
	switch(decl(st,j,type)) {
	case DECL_DIGITAL:
            if (decl(st,j,dir) & DIR_IN) {
		if (decl(st,j,di.pullup))
		    pinMode(decl(st,j,di.pin), INPUT_PULLUP);
		else if (decl(st,j,di.pulldown))
		    pinMode(decl(st,j,di.pin), INPUT_PULLDOWN);
		else
		    pinMode(decl(st,j,di.pin), INPUT);
	    }
	    break;
	case DECL_ANALOG:
	    if ((decl(st,j,dir) & DIR_IN) && decl(st,j,res))
		res = max(res, decl(st,j,res));
	    break;
	default:
	    break;
	}
    }

//    if (res)
//	analogReadResolution(res);


    // setup output (that is NOT inout)
    for (i = 0; i < st->no; i++) {
	index_t ix = st->output[i];
	int j = INDEX(ix);
	if (decl(st,j,dir) & DIR_IN) continue;
	switch(decl(st,j,type)) {
	case DECL_DIGITAL:
	    if (decl(st,j,dir) & DIR_OUT)
		pinMode(decl(st,j,di.pin), OUTPUT);
	    break;
	case DECL_ANALOG:
	    if ((decl(st,j,dir) & DIR_OUT) && decl(st,j,an.pwm))
		pinMode(decl(st,j,an.pin), OUTPUT);
	    break;
	default:
	    break;
	}
    }
}

void csp_input(csp_rt_t* st)
{
    int i;

    csp_board_start_input(st);

    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int di = INDEX(ix);
	value_t* vptr;	
	int vi = st_index(st, ix);
	switch(decl(st,di,type)) {
	case DECL_DIGITAL:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->d.dir & DIR_IN) {
		csp_board_digital_input(st, ix, vptr);
		int value = digitalRead(vptr->d.pin);
		csp_set_ivalue(st, ix, value);
	    }
	    break;
	case DECL_ANALOG:
	    vptr = csp_dio_slot(st, ix, DOUT);
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

int csp_can_init(csp_rt_t* st)
{
    (void)st;
    if (!CAN.begin(CSP_CAN_BITRATE)) {
	csp_print_line("can: init failed");
	return -1;
    }
    return 0;
}

int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    int n, i;
    (void)st;

    if ((n = CAN.parsePacket()) <= 0)
	return 0;
    if (CAN.packetRtr())            // remote request carries no data
	return 0;
    if (n > 8) n = 8;               // classic CAN
    *id = (uint32_t)CAN.packetId();
    for (i = 0; i < n; i++)
	data[i] = (uint8_t)CAN.read();
    *len = (uint8_t)n;
    return 1;
}

int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st;
    if (len > 8) len = 8;
    // Anything past the 11-bit standard id range goes out extended.
    if (id > 0x7FF)
	CAN.beginExtendedPacket(id);
    else
	CAN.beginPacket((int)id);
    CAN.write(data, len);
    return CAN.endPacket() ? 0 : -1;
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

	for (i = 0; i < st->no; ++i) {
	    index_t ix = st->output[i];
	    int di = INDEX(ix);
	    value_t* vptr;
	    switch(decl(st,di,type)) {
	    case DECL_DIGITAL:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->d.dir & DIR_OUT) {
		    csp_board_digital_output(st, vptr);
		}
		break;
	    case DECL_ANALOG:
		vptr = csp_dio_slot(st, ix, DOUT);
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
    return (uint32_t) EEPROM.length();   // AVR: E2END+1 (uno 1K, mega 4K)
}

int csp_eeprom_write(const void* buf, size_t len)
{
    uint8_t* ptr = (uint8_t*) buf;
    if (eeprom_addr < 0)
	return -1;
    // Refuse to run off the end: EEPROM.update() past the last cell wraps or
    // corrupts, so a program too big to persist would half-save silently.
    if ((uint32_t)eeprom_addr + len > csp_eeprom_capacity())
	return -1;
    while(len--) {
	EEPROM.update(eeprom_addr, *ptr++);
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
