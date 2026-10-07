// The Arduino backend for C generated from a .csp (utils/candyspeak_c.erl).
//
// csp_chip_io.h on the Arduino core, and setup()/loop() as the main loop: no
// runtime, no bytecode, no compiler on the part -- the program IS the C. This
// is how a node too small for the runtime (CoCo, an ATmega328P beside the
// LPC2129 on BridgeZone) still runs a .csp, and the same file serves any core
// that has pinMode/digitalRead/analogRead/analogWrite/millis.
//
//   make -f Makefile.board BOARD=uno BACKEND=c PROG=examples/blink.csp upload
//
// THE PORT IS IGNORED: an Arduino names a pin by one number. The pin is the
// core's -- 13 for the LED, 14 for A0 on a 328P -- and analogRead accepts both
// an A-pin and a channel number, so a program may write either.
//
// The console is Serial at CSP_LIB_BAUD: what println prints to, and what a
// board's link (csp_lib_poll) reads and answers on.
// -DCSP_LIB_BAUD=0 leaves Serial out of the image altogether: on a slave with
// nothing listening it is a kilobyte of flash and 128 bytes of buffers.

#include <Arduino.h>

// The board's knobs -- {define, ...} in its terms -- the way csp_config.h
// reads them for the runtime.
#define CSP_STR_(x) #x
#define CSP_STR(x)  CSP_STR_(x)
#ifdef CSP_BOARD
#include CSP_STR(CSP_BOARD)
#endif

extern "C" {
#include "csp_lib.h"
}

#ifndef CSP_LIB_BAUD
#define CSP_LIB_BAUD 115200
#endif

// What analogRead returns, in bits. 10 on every AVR and on a SAMD at its
// default resolution.
#ifndef CSP_LIB_ADC_BITS
#define CSP_LIB_ADC_BITS 10
#endif

// What analogWrite takes, in bits. 8 unless the core was told otherwise.
#ifndef CSP_LIB_PWM_BITS
#define CSP_LIB_PWM_BITS 8
#endif

// ============================================================
// csp_chip_io.h
// ============================================================

extern "C" void csp_chip_init(void)
{
#if CSP_LIB_BAUD
    Serial.begin(CSP_LIB_BAUD);
#endif
}

extern "C" uint32_t csp_chip_millis(void)
{
    return millis();
}

// IN|OUT rests as an input, as the header says. A pulldown the core does not
// have is dropped rather than turned into a pullup.
extern "C" void csp_chip_dcfg(uint8_t port, uint8_t pin, uint8_t dir,
			      uint8_t pull)
{
    (void)port;
    if (dir & CSP_CHIP_IN) {
	if (pull & CSP_CHIP_PULLUP)
	    pinMode(pin, INPUT_PULLUP);
#if defined(INPUT_PULLDOWN)
	else if (pull & CSP_CHIP_PULLDOWN)
	    pinMode(pin, INPUT_PULLDOWN);
#endif
	else
	    pinMode(pin, INPUT);
    }
    else if (dir & CSP_CHIP_OUT)
	pinMode(pin, OUTPUT);
}

extern "C" int csp_chip_din(uint8_t port, uint8_t pin)
{
    (void)port;
    return digitalRead(pin) == HIGH;
}

extern "C" void csp_chip_dout(uint8_t port, uint8_t pin, int v)
{
    (void)port;
    digitalWrite(pin, v ? HIGH : LOW);
}

// An analog input needs nothing from pinMode on these cores; an output is a
// PWM pin (or the SAMD's DAC on A0), which analogWrite configures itself.
extern "C" void csp_chip_acfg(uint8_t port, uint8_t pin, uint8_t dir,
			      uint8_t pwm)
{
    (void)port; (void)pwm;
    if (dir & CSP_CHIP_IN)
	pinMode(pin, INPUT);
    else if (dir & CSP_CHIP_OUT)
	pinMode(pin, OUTPUT);
}

// Left-justified to sixteen bits, as every chip layer returns it.
extern "C" uint16_t csp_chip_ain(uint8_t port, uint8_t pin)
{
    (void)port;
    return (uint16_t)((uint16_t)analogRead(pin) << (16 - CSP_LIB_ADC_BITS));
}

extern "C" void csp_chip_aout(uint8_t port, uint8_t pin, uint16_t v)
{
    (void)port;
    analogWrite(pin, v >> (16 - CSP_LIB_PWM_BITS));
}

// ============================================================
// println
// ============================================================

// Bytes as they are: println's '\n' goes out as '\n'. A link speaking a
// binary protocol on the same Serial (CoCo's) cannot have a 0x0A turned
// into two bytes.
#if CSP_LIB_BAUD
extern "C" void csp_lib_putc(char c)
{
    Serial.write((uint8_t)c);
}

extern "C" int csp_lib_getc(void)
{
    return Serial.available() ? Serial.read() : -1;
}
#endif

// ============================================================
// The main loop
// ============================================================

void setup()
{
    csp_chip_init();
    csp_lib_setup();
}

void loop()
{
    uint32_t wait;

    csp_lib_poll();
    (void)csp_lib_step(millis(), &wait);
}
