//
// clicker: render time stamped clicks into a continuous ALSA stream
//
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include <time.h>

#include "clicker.h"

#define RING_SIZE  8192                  // must be power of 2
#define RING_MASK  (RING_SIZE-1)

// ring of click time stamps (ns), 0 = empty slot
static _Atomic uint64_t ring[RING_SIZE];
static _Atomic uint64_t ring_head;       // next slot to write (producers)
static uint64_t         ring_tail;       // next slot to read (audio thread)
static _Atomic uint64_t dropped;
static _Atomic float    volume;

static sound_thread_t   td;
static sound_device_t*  device;
static sound_format_t   format;
static float*           click_wave;      // click waveform
static unsigned         click_len;
static float*           acc;             // overlap-add buffer
static unsigned         acc_len;
static uint64_t         t_prev;          // time of previous callback
static int              started;

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ULL + (uint64_t)ts.tv_nsec;
}

void clicker_click(void)
{
    uint64_t t = now_ns();
    uint64_t i = atomic_fetch_add_explicit(&ring_head, 1,
					   memory_order_relaxed);
    atomic_store_explicit(&ring[i & RING_MASK], t, memory_order_release);
}

void clicker_set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    atomic_store(&volume, v);
}

uint64_t clicker_dropped(void)
{
    return atomic_load(&dropped);
}

sound_device_t* clicker_device(void)       { return device; }
const sound_format_t* clicker_format(void) { return &format; }

// add one click starting at frame offset 'pos' of the current period
static void add_click(unsigned pos, float gain)
{
    unsigned i;
    float* p = acc + pos;
    for (i = 0; i < click_len; i++)
	p[i] += gain * click_wave[i];
}

// audio thread: fill one period
static void clicker_output(void* nowp, void* bufp, void* whenp, void* data)
{
    uint64_t now = *(uint64_t*) nowp;
    sound_buffer_t* sb = (sound_buffer_t*) bufp;
    unsigned frames = sb->frames;
    unsigned channels = format.channels_per_frame;
    int16_t* out = (int16_t*) sb->data;
    float gain = atomic_load(&volume);
    uint64_t head, span;
    unsigned i, c;
    (void) whenp; (void) data;

    // the window [t_prev, now) is mapped onto this period
    if ((t_prev == 0) || (now <= t_prev))
	t_prev = now - (uint64_t)frames * 1000000000ULL /
	    (uint64_t) format.sample_rate;
    span = now - t_prev;

    head = atomic_load_explicit(&ring_head, memory_order_acquire);
    if (head - ring_tail > RING_SIZE) {  // producers lapped us
	atomic_fetch_add(&dropped, head - ring_tail - RING_SIZE);
	ring_tail = head - RING_SIZE;
    }
    while (ring_tail != head) {
	uint64_t t = atomic_exchange_explicit(&ring[ring_tail & RING_MASK], 0,
					      memory_order_acquire);
	uint64_t pos;
	if (t == 0)       // slot reserved but not yet written, take it later
	    break;
	ring_tail++;
	pos = (t > t_prev) ? (t - t_prev) * frames / span : 0;
	if (pos >= frames) pos = frames-1;
	add_click((unsigned) pos, gain);
    }
    t_prev = now;

    // convert to int16 with clipping
    for (i = 0; i < frames; i++) {
	float x = acc[i];
	int16_t s;
	if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
	s = (int16_t) lrintf(x * 32767.0f);
	for (c = 0; c < channels; c++)
	    *out++ = s;
    }
    // shift click tails into next period
    memmove(acc, acc + frames, (acc_len - frames) * sizeof(float));
    memset(acc + (acc_len - frames), 0, frames * sizeof(float));
}

int clicker_start(const clicker_config_t* cfg)
{
    clicker_config_t c;
    unsigned i;
    float rate, tau;

    if (started)
	return -1;
    memset(&c, 0, sizeof(c));
    if (cfg) c = *cfg;
    if (!c.volume)    c.volume = 0.5f;
    if (!c.freq)      c.freq = 2500.0f;
    if (!c.length_ms) c.length_ms = 1.0f;

    memset(&format, 0, sizeof(format));
    format.name = (char*) c.device;
    format.dir = SOUND_DIR_OUTPUT;
    format.flags = SOUND_FORMAT_SIGNED;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    format.flags |= SOUND_FORMAT_BIG_ENDIAN;
#endif
    format.bits_per_channel = 16;
    format.channels_per_frame = 1;
    format.sample_rate = c.rate ? c.rate : 48000;
    format.frames_per_packet = c.period ? c.period : 256;

    if ((device = sound_device_open(SOUND_DIR_OUTPUT, &format)) == NULL)
	return -1;

    // click: decaying sine burst, a few cycles long
    rate = format.sample_rate;
    click_len = (unsigned) (rate * c.length_ms / 1000.0f);
    if (click_len < 2) click_len = 2;
    tau = click_len / 5.0f;
    click_wave = (float*) malloc(click_len * sizeof(float));
    acc_len = format.frames_per_packet + click_len;
    acc = (float*) calloc(acc_len, sizeof(float));
    if (!click_wave || !acc)
	goto fail;
    for (i = 0; i < click_len; i++) {
	float x = (float) i;
	float a = fmodf(2.0f*(float)M_PI*c.freq*x/rate, 2.0f*M_PI);
	click_wave[i] = sinf(a) * expf(-x/tau);
	// click_wave[i] = (a > M_PI) ? 1.0 : -1.0;
	// click_wave[i] =(i == 0) ? 1 : -1;
    }

    // forget clicks issued before start
    ring_tail = atomic_load(&ring_head);
    for (i = 0; i < RING_SIZE; i++)
	atomic_store(&ring[i], 0);
    atomic_store(&dropped, 0);
    clicker_set_volume(c.volume);
    t_prev = 0;

    memset(&td, 0, sizeof(td));
    td.device = device;
    td.dir = SOUND_DIR_OUTPUT;
    td.sound_output = clicker_output;
    if (sound_thread_start(&td) < 0)
	goto fail;
    started = 1;
    return 0;

fail:
    sound_device_close(device);
    device = NULL;
    free(click_wave); click_wave = NULL;
    free(acc); acc = NULL;
    return -1;
}

void clicker_stop(void)
{
    if (!started)
	return;
    sound_thread_stop(&td);
    sound_device_close(device);
    device = NULL;
    free(click_wave); click_wave = NULL;
    free(acc); acc = NULL;
    started = 0;
}
