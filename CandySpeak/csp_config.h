#ifndef __CSP_CONFIG_H__
#define __CSP_CONFIG_H__

#define MAX_STATES 16

#define REACTIVE_DEFAULT 0
#ifndef SUPPORT_REACTIVE
#define SUPPORT_REACTIVE    0 // enq/deq - saves ~1KB RAM on AVR
#endif

#ifndef USE_STATISTICS
#define USE_STATISTICS      0 // need some accounting
#endif

// Use Q16.16 fixed-point instead of float (saves ~4KB on AVR)
#ifndef USE_FIXPOINT

#if defined(__AVR__)
// all AVR
#define USE_FIXPOINT        1
#elif defined(__SAMD21G18A__)  // alla MKR + Zero + Nano 33 IoT
#define USE_FIXPOINT        1
#elif defined(ESP32)           // ESP32-familjen
#define USE_FIXPOINT        0
#elif defined(ESP8266)         // ESP8266
#define USE_FIXPOINT        0
#elif defined(__SAM3X8E__)     // Arduino Due
#define USE_FIXPOINT        1
#else
#define USE_FIXPOINT        1
#endif

#ifdef USE_FIXPOINT && USE_FIXPOINT == 0
#define float   _Pragma("GCC error \"float not allowed\"") float
#define double  _Pragma("GCC error \"double not allowed\"") double
#endif

#endif

#endif
