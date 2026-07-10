#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

// EEPROM support - not all boards have it
#if defined(__AVR__) || defined(ESP32) || defined(ESP8266)
#define CSP_HAS_EEPROM 1
#include <EEPROM.h>
#endif

#define CSP_EMBEDDED 1
#include "csp.h"
#include "csp_print.h"

csp_rt_t state;

extern char __StackTop;

EXTERN_C_BEGIN

int stack_used(void)
{
    char local;
    return &__StackTop - &local;
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
int csp_print_char(char c)
{
    if (serial_output)
	return Serial.write(c);
    return 1;
}

int csp_print_str(const char* s)
{
    if (serial_output)
	return Serial.print(s);
    return strlen(s);    
}

#if defined(__AVR__)
int csp_print_str_P(const rochar* s)
{
    if (serial_output)    
	return Serial.print((__FlashStringHelper*)s);
    // fixme number of strlen_P(s);
    return strlen_P(s);
}
#endif

int csp_print_int(ivalue_t v)
{
    if (serial_output)    
	return Serial.print(v);
    return 1; // fixme number of chars?
}

int csp_print_uint(uvalue_t v)
{
    if (serial_output)        
	return Serial.print(v);
    return 1; // fixme number of chars?
}

int csp_print_float(fvalue_t v)
{
    if (serial_output) {
#if FVALUE_IS_FIXPOINT    
	return csp_print_fixpoint(v);
#else
	return Serial.print(v);
#endif	
    }
    return 1; // fixme: number of chars
}

int csp_print_hex(uvalue_t v)
{
    if (serial_output) {
	Serial.print("0x");
	return Serial.print(v, HEX);
    }
    return 1;
}

int csp_println(void)
{
    if (serial_output)
	return Serial.println();
    return 1;
}

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


void csp_setup(csp_rt_t* st)
{
    int i;
    unsigned res = 0;
    
    // setup in and inout (inout startup as input)
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int j = INDEX(ix);
	switch(decl(st,j,type)) {
	case DECL_DIGITAL:
            if (decl(st,j,dir) & DIR_IN) {
		if (decl(st,j,di.pullup))
		    pinMode(decl(st,j,di.pin), INPUT_PULLUP);
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

    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int di = INDEX(ix);
	value_t* vptr;	
	int vi = st_index(st, ix);
	switch(decl(st,di,type)) {
	case DECL_DIGITAL:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->d.dir & DIR_IN) {
		int value = digitalRead(vptr->d.pin);
		csp_set_ivalue(st, ix, value);
	    }
	    break;
	case DECL_ANALOG:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->a.dir & DIR_IN) {
		int value = analogRead(vptr->a.pin);
		csp_set_ivalue(st, ix, value);
	    }
	    break;
	default: break;
	}
    }
    csp_input_timer(st);
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {  // allow output
	for (i = 0; i < st->no; ++i) {
	    index_t ix = st->output[i];
	    int di = INDEX(ix);
	    value_t* vptr;
	    switch(decl(st,di,type)) {
	    case DECL_DIGITAL:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->d.dir & DIR_OUT) {
		    if (vptr->d.dir & DIR_IN) {
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
		break;
	    case DECL_ANALOG:
		vptr = csp_dio_slot(st, ix, DOUT);		
		if ((vptr->a.dir & DIR_OUT) && (vptr->a.pwm)) {
		    // handle type! accept float as well
		    int val = map(vptr->a.val,
				  0, (1<<decl(st,di,res))-1,
				  0, 255);
		    analogWrite(vptr->a.pin, val);
		}
		break;
	    default:
		break;
	    }
	}
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

int csp_eeprom_write(const void* buf, size_t len)
{
    uint8_t* ptr = (uint8_t*) buf;
    if (eeprom_addr < 0)
	return -1;
    while(len--) {
	EEPROM.update(eeprom_addr, *ptr++);
	eeprom_addr++;
    }
    return 0;
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

int csp_eeprom_read(void* buf, size_t len)
{
    return 0;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    return 0;
}

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
    Serial.print("ERROR: ");
    Serial.println(msg);
}

// Platform-specific command implementations
int csp_cmd_save(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    if (csp_eeprom_save(st) < 0) {
	serial_print_error("cannot save eeprom");
	return CSP_CMD_ERROR;
    }
    serial_print_ok();
    return CSP_CMD_OK;
}

int csp_cmd_load(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    if (csp_eeprom_load(st) < 0) {
	serial_print_error("cannot load from eeprom");
	return CSP_CMD_ERROR;
    }
    csp_setup(st);
    serial_print_ok();    
    return CSP_CMD_OK;
}

// ============================================================
// Arduino setup/loop
// ============================================================

void setup()
{
    Serial.begin(115200);
    while (!Serial) { ; }  // wait for USB serial

    serial_output = 1;
    csp_rt_init(&state, REACTIVE_DEFAULT);

    // try to load from EEPROM
    int r = csp_eeprom_load(&state);
    if (r < 0) {
        Serial.println(F("Loaded from EEPROM"));
    }
    else {
        Serial.println(F("No saved state, starting fresh"));
    }

    // setup all input/output/timers... lists
    csp_rt_start(&state);
    
    // initialize input/output/timers ...
    csp_setup(&state);
	
    Serial.println(F("CandySpeak ready"));
}

void loop()
{
    static int first_cycle = 1;
    index_t x;

    if (first_cycle) {
	state.cycle = 1;
	first_cycle = 0;
    }
    else
	state.cycle++;

    while (Serial.available()) {
	csp_line_input(Serial.read());
    }

    if (csp_line_ready) {
	csp_process_line(&state, csp_line_buf);
	csp_line_ready = 0;
	csp_line_pos = 0;
    }

    // run evaluation cycle
    csp_input(&state);
    if (state.reactive)
	x = csp_react(&state);
    else
	x = csp_eval(&state);
    csp_commit(&state);
    csp_output(&state);

    if (state.wait_ms != NOTIMEOUT) {
        // use smaller delays to stay responsive to serial
        uint32_t remaining = state.wait_ms;
	// FIXME: we must re-read current time and update,
	// we do not know how long poll is taking!
        while ((remaining > 0) && !csp_line_ready) {
            uint32_t chunk = min(remaining, (uint32_t)50);
            delay(chunk);
            remaining -= chunk;
	    if (Serial.available())
		csp_line_input(Serial.read());
        }
    }
}
