//
// clicker: "listen" to a loop.
//
// A background thread streams audio continuously to an ALSA output.
// Every call to clicker_click() is time stamped and rendered as a short
// click at (nearly) the same relative time in the audio stream, so a
// loop running at 50 Hz sounds like a buzz, a stalled loop goes quiet and
// jitter is audible.
//
// clicker_click() is lock free and cheap (clock_gettime + atomic add),
// safe to call from any thread, including several threads at once.
//
#ifndef __CLICKER_H__
#define __CLICKER_H__

#include <stdint.h>
#include "sound.h"

typedef struct {
    const char* device;      // ALSA pcm name, NULL = "default"
    unsigned    rate;        // sample rate, 0 = 48000
    unsigned    period;      // frames per period, 0 = 256 (~5ms latency)
    float       volume;      // 0..1, 0 = 0.5
    float       freq;        // click tone frequency Hz, 0 = 2500
    float       length_ms;   // click length, 0 = 2.0 ms
} clicker_config_t;

// start the output thread, cfg may be NULL. returns 0 on success
extern int  clicker_start(const clicker_config_t* cfg);
extern void clicker_stop(void);

// emit one click now
extern void clicker_click(void);

// change volume while running (0..1)
extern void clicker_set_volume(float volume);

// number of clicks lost because the queue overflowed
extern uint64_t clicker_dropped(void);

// device and negotiated format (valid between start and stop)
extern sound_device_t* clicker_device(void);
extern const sound_format_t* clicker_format(void);

#endif
