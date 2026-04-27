#include <Arduino.h>
#include <stdlib.h>

// EEPROM support - not all boards have it
#if defined(__AVR__) || defined(ESP32) || defined(ESP8266)
#define CSP_HAS_EEPROM 1
#include <EEPROM.h>
#endif

#define CSP_EMBEDDED 1
#include "csp.h"

// Serial input buffer
#define SERIAL_BUF_SIZE 128
static char serial_buf[SERIAL_BUF_SIZE];
static uint8_t serial_pos = 0;

#ifdef CSP_HAS_EEPROM
// EEPROM layout
#define EEPROM_MAGIC      0xC5B0  // "CSP0"
#define EEPROM_VERSION    1
#define EEPROM_ADDR_START 0

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint16_t nn;       // number of instructions
    uint16_t nd;       // number of declarations
    uint16_t strp;     // string pointer position
    uint16_t checksum;
} eeprom_header_t;
#endif

csp_rt_t state;

uint32_t csp_time_ms(void)
{
    return millis();
}

unsigned long csp_time_us(void)
{
    return micros();
}

// platform print functions
int csp_print_char(char c)
{
    return Serial.write(c);
}

int csp_print_str(const char* s)
{
    return Serial.print(s);
}

int csp_print_int(ivalue_t v)
{
    return Serial.print(v);
}

int csp_print_uint(uvalue_t v)
{
    return Serial.print(v);
}

int csp_print_float(fvalue_t v)
{
    return Serial.print(v);
}

int csp_print_hex(uvalue_t v)
{
    Serial.print("0x");
    return Serial.print(v, HEX);
}

int csp_println(void)
{
    return Serial.println();
}

void csp_setup(csp_rt_t* st)
{
    int i;
    unsigned res = 0;
    
    // setup in and inout (inout startup as input)
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int j = INDEX(ix);
	switch(st->decl[j].type) {
	case DECL_DIGITAL:
            if (st->decl[j].in) {
		if (st->decl[j].di.pullup)
		    pinMode(st->decl[j].di.pin, INPUT_PULLUP);
		else
		    pinMode(st->decl[j].di.pin, INPUT);
	    }
	    break;
	case DECL_ANALOG:
	    if (st->decl[j].in && st->decl[j].res)
		res = max(res, st->decl[j].res);
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
	if (st->decl[j].in) continue;
	switch(st->decl[j].type) {
	case DECL_DIGITAL:
	    if (st->decl[j].out)
		pinMode(st->decl[j].di.pin, OUTPUT);
	    break;
	case DECL_ANALOG:
	    if (st->decl[j].out && st->decl[j].an.pwm)
		pinMode(st->decl[j].an.pin, OUTPUT);
	    break;
	default:
	    break;
	}
    }
}


void csp_input(csp_rt_t* st)
{
    int i;
    uvalue_t now_ms;
    
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int di = INDEX(ix);
	int vi = st_index(st, ix);
	switch(st->decl[di].type) {
	case DECL_DIGITAL:
	    if (st->decl[di].in)
		st->xval[vi].i = digitalRead(st->decl[di].di.pin);
	    break;
	case DECL_ANALOG:
	    if (st->decl[di].in)
		st->xval[vi].i = analogRead(st->decl[di].di.pin);
	    break;
	default: break;
	}
    }
    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; i++) {
	index_t ix = st->timer[i];
	int di = INDEX(ix);
	if (st->decl[di].tm.running) {
	    uvalue_t t0 = csp_uvalue(st, st->decl[di].tm.tx);
	    ivalue_t period = csp_ivalue(st, st->decl[di].tm.px);
	    if ((now_ms - t0) >= period) {
		st->decl[di].tm.running = 0;
		csp_set_ivalue(st, ix, 0);
	    }
	    break;
	}
    }
}

void csp_output(csp_rt_t* st)
{
    int i;
    uint32_t now_ms;
    uint32_t wait_ms = NOTIMEOUT;    
    
    for (i = 0; i < st->no; ++i) {
	index_t ix = st->output[i];
	int di = INDEX(ix);
	int vi = st_index(st, ix);
	switch(st->decl[di].type) {
	case DECL_DIGITAL:
	    if (st->decl[di].out) {
		if (st->decl[di].in) {
		    pinMode(st->decl[di].di.pin, OUTPUT);
		    digitalWrite(st->decl[di].di.pin, st->xval[vi].i);
		    // prepare for next input
		    if (st->decl[di].di.pullup)
			pinMode(st->decl[di].di.pin, INPUT_PULLUP);
		    else
			pinMode(st->decl[di].di.pin, INPUT);
		}
		else { // plain out
		    digitalWrite(st->decl[di].di.pin, st->xval[vi].i);
		}
	    }
	    break;
	case DECL_ANALOG:
	    if ((st->decl[di].out) && (st->decl[di].an.pwm)) {
		int val = map(st->xval[vi].i,
			      0, (1<<st->decl[di].res)-1,
			      0, 255);
		analogWrite(st->decl[di].an.pin, val);
	    }
	    break;
	default:
	    break;
	}
    }

    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; ++i) {
	index_t ix = st->timer[i];
	int di = INDEX(ix);
	int vi = st_index(st, ix);
	if (st->decl[di].tm.running) {
	    uvalue_t t0 = csp_uvalue(st, st->decl[di].tm.tx);
	    ivalue_t period = csp_ivalue(st, st->decl[di].tm.px);
	    int32_t dt = (now_ms - t0);
	    if (dt > period)
		wait_ms = 0;
	    else
		wait_ms = period - dt;
	}
	else {
	    if (st->dval[vi].i) {
		ivalue_t period = csp_ivalue(st, st->decl[di].tm.px);
		uint32_t dt = period;
		int k = st_index(st, st->decl[di].tm.tx);
		st->decl[di].tm.running = 1;
		st->dval[k].u = now_ms;
		st->dval[vi].i = 0;  // not timeout
		if (dt < wait_ms)
		    wait_ms = dt;
	    }
	}
    }
    st->wait_ms = wait_ms;
}


// ============================================================
// Storage functions
// ============================================================

#ifdef CSP_HAS_EEPROM

static uint16_t calc_checksum(csp_rt_t* st)
{
    uint16_t sum = 0;
    uint8_t* p;
    size_t i;

    p = (uint8_t*)st->instr;
    for (i = 0; i < st->nn * sizeof(csp_instr_t); i++)
        sum += p[i];

    p = (uint8_t*)st->decl;
    for (i = 0; i < st->nd * sizeof(csp_decl_t); i++)
        sum += p[i];

    p = (uint8_t*)&st->str[st->strp];
    for (i = st->strp; i < MAX_STR_BUF; i++)
        sum += p[i - st->strp];

    return sum;
}

int csp_storage_save(csp_rt_t* st)
{
    eeprom_header_t hdr;
    uint16_t addr = EEPROM_ADDR_START;
    size_t i;
    uint8_t* p;

    hdr.magic = EEPROM_MAGIC;
    hdr.version = EEPROM_VERSION;
    hdr.flags = 0;
    hdr.nn = st->nn;
    hdr.nd = st->nd;
    hdr.strp = st->strp;
    hdr.checksum = calc_checksum(st);

    // write header
    p = (uint8_t*)&hdr;
    for (i = 0; i < sizeof(hdr); i++)
        EEPROM.update(addr++, p[i]);

    // write instructions
    p = (uint8_t*)st->instr;
    for (i = 0; i < st->nn * sizeof(csp_instr_t); i++)
        EEPROM.update(addr++, p[i]);

    // write declarations
    p = (uint8_t*)st->decl;
    for (i = 0; i < st->nd * sizeof(csp_decl_t); i++)
        EEPROM.update(addr++, p[i]);

    // write strings (from strp to end)
    for (i = st->strp; i < MAX_STR_BUF; i++)
        EEPROM.update(addr++, st->str[i]);

    return 0;
}

int csp_storage_load(csp_rt_t* st)
{
    eeprom_header_t hdr;
    uint16_t addr = EEPROM_ADDR_START;
    size_t i;
    uint8_t* p;

    // read header
    p = (uint8_t*)&hdr;
    for (i = 0; i < sizeof(hdr); i++)
        p[i] = EEPROM.read(addr++);

    // validate
    if (hdr.magic != EEPROM_MAGIC)
        return -1;  // no valid data
    if (hdr.version != EEPROM_VERSION)
        return -2;  // version mismatch

    // read instructions
    st->nn = hdr.nn;
    p = (uint8_t*)st->instr;
    for (i = 0; i < st->nn * sizeof(csp_instr_t); i++)
        p[i] = EEPROM.read(addr++);

    // read declarations
    st->nd = hdr.nd;
    p = (uint8_t*)st->decl;
    for (i = 0; i < st->nd * sizeof(csp_decl_t); i++)
        p[i] = EEPROM.read(addr++);

    // read strings
    st->strp = hdr.strp;
    for (i = st->strp; i < MAX_STR_BUF; i++)
        st->str[i] = EEPROM.read(addr++);

    // verify checksum
    if (calc_checksum(st) != hdr.checksum)
        return -3;  // checksum error

    return 0;
}

int csp_storage_clear(void)
{
    EEPROM.update(EEPROM_ADDR_START, 0xFF);  // invalidate magic
    EEPROM.update(EEPROM_ADDR_START + 1, 0xFF);
    return 0;
}

#else
// No storage available - stub functions
int csp_storage_save(csp_rt_t* st) { (void)st; return -1; }
int csp_storage_load(csp_rt_t* st) { (void)st; return -1; }
int csp_storage_clear(void) { return -1; }
#endif

// ============================================================
// Serial command interface
// ============================================================

void serial_print_ok(void)
{
    Serial.println(F("OK"));
}

void serial_print_error(const char* msg)
{
    Serial.print(F("ERROR: "));
    Serial.println(msg);
}

void handle_immediate_command(csp_rt_t* st, char* cmd)
{
    // skip '>' prefix
    cmd++;
    while (*cmd == ' ') cmd++;

    if (strncmp(cmd, "save", 4) == 0) {
        if (csp_storage_save(st) == 0)
            serial_print_ok();
        else
            serial_print_error("save failed");
    }
    else if (strncmp(cmd, "load", 4) == 0) {
        int r = csp_storage_load(st);
        if (r == 0) {
            csp_rt_start(st);
            serial_print_ok();
        }
        else if (r == -1)
            serial_print_error("no data");
        else if (r == -2)
            serial_print_error("version");
        else
            serial_print_error("checksum");
    }
    else if (strncmp(cmd, "clear", 5) == 0) {
        csp_storage_clear();
        csp_rt_init(st);
        serial_print_ok();
    }
    else if (strncmp(cmd, "reset", 5) == 0) {
        csp_rt_init(st);
        serial_print_ok();
    }
    else if (strncmp(cmd, "list", 4) == 0) {
        Serial.print(F("nn="));
        Serial.print(st->nn);
        Serial.print(F(" nd="));
        Serial.print(st->nd);
        Serial.print(F(" strp="));
        Serial.println(st->strp);
        // TODO: csp_dump to serial
    }
    else if (strncmp(cmd, "run", 3) == 0) {
        csp_rt_start(st);
        serial_print_ok();
    }
    else {
        serial_print_error("unknown command");
    }
}

void process_serial_line(csp_rt_t* st, char* line)
{
    if (line[0] == '\0')
        return;

    if (line[0] == '>') {
        handle_immediate_command(st, line);
    }
    else {
        // parse as rule/declaration
        if (csp_parse(st, line) < 0) {
            serial_print_error("syntax");
        }
        else {
            serial_print_ok();
        }
    }
}

void serial_poll(csp_rt_t* st)
{
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (serial_pos > 0) {
                serial_buf[serial_pos] = '\0';
                process_serial_line(st, serial_buf);
                serial_pos = 0;
            }
        }
        else if (serial_pos < SERIAL_BUF_SIZE - 1) {
            serial_buf[serial_pos++] = c;
        }
    }
}

// ============================================================
// Arduino setup/loop
// ============================================================

void setup()
{
    Serial.begin(115200);
    while (!Serial) { ; }  // wait for USB serial

    csp_rt_init(&state);

    // try to load from EEPROM
    int r = csp_storage_load(&state);
    if (r == 0) {
        Serial.println(F("Loaded from EEPROM"));
    }
    else {
        Serial.println(F("No saved state, starting fresh"));
    }

    csp_setup(&state);
    csp_rt_start(&state);

    Serial.println(F("CandySpeak ready"));
}

void loop()
{
    index_t x;

    // check for serial commands
    serial_poll(&state);

    // run evaluation cycle
    csp_input(&state);
    x = csp_eval(&state);
    csp_output(&state);

    if (state.wait_ms != NOTIMEOUT) {
        // use smaller delays to stay responsive to serial
        uint32_t remaining = state.wait_ms;
	// FIXME: we must re-read current time and update,
	// we do not know how long poll is taking!
        while (remaining > 0) {
            uint32_t chunk = min(remaining, (uint32_t)50);
            delay(chunk);
            remaining -= chunk;
            serial_poll(&state);  // check serial during wait
        }
    }
}
