#ifndef __SOUND_H__
#define __SOUND_H__

#include <stdio.h>
#include <stdint.h>

#define SOUND_DIR_INPUT  0x01
#define SOUND_DIR_OUTPUT 0x02
typedef unsigned int sound_dir_t;

#define SOUND_FORMAT_BIG_ENDIAN  0x01
#define SOUND_FORMAT_FLOAT       0x02
#define SOUND_FORMAT_SIGNED      0x04

typedef unsigned int sound_format_flag_t;

typedef void* sound_device_t;

//
// Requested format, passed to sound_device_open. Zero fields mean
// "don't care". On return the struct holds the format actually
// negotiated with the device.
//
typedef struct _sound_format_t
{
    char* name;                  // device name (ALSA pcm name, NULL=default)
    sound_dir_t dir;
    sound_format_flag_t flags;
    float    sample_rate;
    unsigned bytes_per_packet;   // bytes per period
    unsigned frames_per_packet;  // frames per period (callback size)
    unsigned bytes_per_frame;
    unsigned channels_per_frame;
    unsigned bits_per_channel;
} sound_format_t;

//
// Buffer passed to the callbacks (ALSA backend).
// Interleaved samples in the negotiated format.
//
typedef struct _sound_buffer_t
{
    void*    data;
    unsigned frames;
    unsigned bytes;
} sound_buffer_t;

//
// Callbacks (ALSA backend):
//   now    : uint64_t*        CLOCK_MONOTONIC ns when the callback was called
//   buffer : sound_buffer_t*  input data to consume / output data to fill
//   time   : uint64_t*        estimated CLOCK_MONOTONIC ns when the first
//                             frame was captured (input) / will be heard
//                             (output)
//   data   : user data
//
typedef struct _sound_thread_t
{
    sound_device_t*   device;
    sound_dir_t       dir;
    volatile int      running;
    void (*sound_input)(void* now, void* ibuffer, void* time, void* data);
    void (*sound_output)(void* now, void* obuffer, void* time, void* data);
    void* data;
    void* priv;                  // backend private (thread handle)
} sound_thread_t;

extern void sound_device_print_last_error(FILE* f);

extern sound_device_t* sound_device_open(sound_dir_t dir,
					 sound_format_t* format);

extern void sound_device_close(sound_device_t* device);

extern int  sound_device_buffer_size(sound_device_t* device,
				     sound_dir_t dir);
extern int sound_device_name(sound_device_t* device,
			     char* name, uint32_t maxlen);
extern int sound_device_manuf(sound_device_t* device,
			      char* name, uint32_t maxlen);

// Run the I/O loop in the calling thread until td->running is cleared
extern int sound_main(sound_thread_t* td);

// Run sound_main in a new thread / stop and join it (ALSA backend)
extern int  sound_thread_start(sound_thread_t* td);
extern void sound_thread_stop(sound_thread_t* td);

#endif
