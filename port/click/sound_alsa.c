//
// ALSA sound backend (input and output)
//
// Works on any Linux; on desktops "default" is routed through
// PulseAudio / PipeWire by the ALSA pulse/pipewire plugin.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include "sound.h"

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#define DEFAULT_PERIOD_FRAMES  256
#define DEFAULT_PERIODS        3

static snd_pcm_format_t pcm_format(sound_format_t* format);

static int s_error = 0;

typedef struct
{
    snd_pcm_t*        handle;
    snd_pcm_stream_t  stream;
    snd_pcm_format_t  fmt;
    unsigned          channels;
    unsigned          rate;
    snd_pcm_uframes_t period_size;
    snd_pcm_uframes_t buffer_size;
    unsigned          bytes_per_frame;
} alsa_device_t;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int str_copy(const char* ptr, char* buf, uint32_t maxlen)
{
    size_t n;
    if ((ptr == NULL) || (maxlen == 0))
	return -1;
    if ((n = strlen(ptr)) >= maxlen)
	n = maxlen-1;
    memcpy(buf, ptr, n);
    buf[n] = '\0';
    return (int) n;
}

void sound_device_print_last_error(FILE* f)
{
    fprintf(f, "sound error: %s\n", snd_strerror(s_error));
}

int sound_device_name(sound_device_t* device, char* buf, uint32_t maxlen)
{
    alsa_device_t* dp = (alsa_device_t*) device;
    return str_copy(snd_pcm_name(dp->handle), buf, maxlen);
}

int sound_device_manuf(sound_device_t* device, char* buf, uint32_t maxlen)
{
    (void) device;
    return str_copy("unknown", buf, maxlen);
}

int sound_device_buffer_size(sound_device_t* device, sound_dir_t dir)
{
    alsa_device_t* dp = (alsa_device_t*) device;
    (void) dir;
    return (int) dp->period_size;
}

#define CHECK(expr) do { if ((s_error = (expr)) < 0) goto fail; } while(0)

sound_device_t* sound_device_open(sound_dir_t dir, sound_format_t* format)
{
    const char* name = (format && format->name) ? format->name : "default";
    snd_pcm_t* handle = NULL;
    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_sw_params_t* sw = NULL;
    snd_pcm_stream_t stream;
    snd_pcm_format_t fmt;
    snd_pcm_uframes_t period, buffer;
    unsigned int rate;
    unsigned int channels;
    alsa_device_t* dp;
    int sdir = 0;

    // full duplex needs two devices, open one per direction
    if (dir == SOUND_DIR_INPUT)
	stream = SND_PCM_STREAM_CAPTURE;
    else if (dir == SOUND_DIR_OUTPUT)
	stream = SND_PCM_STREAM_PLAYBACK;
    else {
	s_error = -EINVAL;
	errno = EINVAL;
	return NULL;
    }

    CHECK(snd_pcm_open(&handle, name, stream, 0));
    CHECK(snd_pcm_hw_params_malloc(&hw));
    CHECK(snd_pcm_hw_params_any(handle, hw));
    CHECK(snd_pcm_hw_params_set_access(handle, hw,
				       SND_PCM_ACCESS_RW_INTERLEAVED));

    if ((format->bits_per_channel == 0) &&
	!(format->flags & SOUND_FORMAT_FLOAT))
	fmt = SND_PCM_FORMAT_S16;           // native endian default
    else if ((fmt = pcm_format(format)) == SND_PCM_FORMAT_UNKNOWN) {
	s_error = -EINVAL;
	goto fail;
    }
    CHECK(snd_pcm_hw_params_set_format(handle, hw, fmt));

    channels = format->channels_per_frame ? format->channels_per_frame : 1;
    CHECK(snd_pcm_hw_params_set_channels_near(handle, hw, &channels));

    rate = format->sample_rate ? (unsigned) format->sample_rate : 48000;
    CHECK(snd_pcm_hw_params_set_rate_near(handle, hw, &rate, &sdir));

    period = format->frames_per_packet ? format->frames_per_packet :
	DEFAULT_PERIOD_FRAMES;
    CHECK(snd_pcm_hw_params_set_period_size_near(handle, hw, &period, &sdir));
    buffer = period * DEFAULT_PERIODS;
    CHECK(snd_pcm_hw_params_set_buffer_size_near(handle, hw, &buffer));

    CHECK(snd_pcm_hw_params(handle, hw));

    // read back what we actually got
    CHECK(snd_pcm_hw_params_get_format(hw, &fmt));
    CHECK(snd_pcm_hw_params_get_channels(hw, &channels));
    CHECK(snd_pcm_hw_params_get_rate(hw, &rate, &sdir));
    CHECK(snd_pcm_hw_params_get_period_size(hw, &period, &sdir));
    CHECK(snd_pcm_hw_params_get_buffer_size(hw, &buffer));

    // software params: wake up every period, start when one period is ready
    CHECK(snd_pcm_sw_params_malloc(&sw));
    CHECK(snd_pcm_sw_params_current(handle, sw));
    CHECK(snd_pcm_sw_params_set_avail_min(handle, sw, period));
    if (stream == SND_PCM_STREAM_PLAYBACK)
	CHECK(snd_pcm_sw_params_set_start_threshold(handle, sw, period));
    CHECK(snd_pcm_sw_params(handle, sw));

    snd_pcm_hw_params_free(hw);
    snd_pcm_sw_params_free(sw);

    if ((dp = (alsa_device_t*) calloc(1, sizeof(alsa_device_t))) == NULL) {
	s_error = -ENOMEM;
	snd_pcm_close(handle);
	return NULL;
    }
    dp->handle      = handle;
    dp->stream      = stream;
    dp->fmt         = fmt;
    dp->channels    = channels;
    dp->rate        = rate;
    dp->period_size = period;
    dp->buffer_size = buffer;
    dp->bytes_per_frame = (snd_pcm_format_physical_width(fmt)/8) * channels;

    // export negotiated format
    format->dir = dir;
    format->flags = 0;
    if (snd_pcm_format_big_endian(fmt) == 1)
	format->flags |= SOUND_FORMAT_BIG_ENDIAN;
    if (snd_pcm_format_float(fmt) == 1)
	format->flags |= SOUND_FORMAT_FLOAT;
    if (snd_pcm_format_signed(fmt) == 1)
	format->flags |= SOUND_FORMAT_SIGNED;
    format->sample_rate        = (float) rate;
    format->channels_per_frame = channels;
    format->bits_per_channel   = snd_pcm_format_width(fmt);
    format->bytes_per_frame    = dp->bytes_per_frame;
    format->frames_per_packet  = (unsigned) period;
    format->bytes_per_packet   = (unsigned) period * dp->bytes_per_frame;
    return (sound_device_t*) dp;

fail:
    if (sw) snd_pcm_sw_params_free(sw);
    if (hw) snd_pcm_hw_params_free(hw);
    if (handle) snd_pcm_close(handle);
    return NULL;
}

void sound_device_close(sound_device_t* device)
{
    alsa_device_t* dp = (alsa_device_t*) device;
    if (dp == NULL)
	return;
    snd_pcm_drop(dp->handle);
    snd_pcm_close(dp->handle);
    free(dp);
}

// frames -> ns
static inline uint64_t frames_ns(alsa_device_t* dp, snd_pcm_sframes_t n)
{
    if (n <= 0) return 0;
    return (uint64_t) n * 1000000000ULL / dp->rate;
}

// Recover from xrun/suspend, returns 0 if ok to continue
static int recover(alsa_device_t* dp, int err)
{
    if ((err = snd_pcm_recover(dp->handle, err, 1)) < 0) {
	s_error = err;
	return err;
    }
    return 0;
}

int sound_main(sound_thread_t* td)
{
    alsa_device_t* dp = (alsa_device_t*) td->device;
    sound_buffer_t sb;
    uint64_t now, when;
    snd_pcm_sframes_t delay;
    int rc = 0;

    sb.frames = (unsigned) dp->period_size;
    sb.bytes  = sb.frames * dp->bytes_per_frame;
    if ((sb.data = calloc(1, sb.bytes)) == NULL) {
	s_error = -ENOMEM;
	return -1;
    }
    td->running = 1;

    if (dp->stream == SND_PCM_STREAM_CAPTURE) {
	if ((rc = snd_pcm_start(dp->handle)) < 0)
	    rc = recover(dp, rc);
    }

    while (td->running && (rc == 0)) {
	if (dp->stream == SND_PCM_STREAM_PLAYBACK) {
	    snd_pcm_uframes_t off = 0;

	    now = now_ns();
	    if (snd_pcm_delay(dp->handle, &delay) < 0) delay = 0;
	    when = now + frames_ns(dp, delay);
	    if (td->sound_output)
		(*td->sound_output)(&now, &sb, &when, td->data);
	    else
		snd_pcm_format_set_silence(dp->fmt, sb.data,
					   sb.frames*dp->channels);
	    while (off < sb.frames) {
		snd_pcm_sframes_t n;
		n = snd_pcm_writei(dp->handle,
				   (char*)sb.data + off*dp->bytes_per_frame,
				   sb.frames - off);
		if (n == -EAGAIN)
		    continue;
		if (n < 0) {
		    if ((rc = recover(dp, (int) n)) < 0)
			break;
		    continue;
		}
		off += n;
	    }
	}
	else {
	    snd_pcm_uframes_t off = 0;
	    while (off < sb.frames) {
		snd_pcm_sframes_t n;
		n = snd_pcm_readi(dp->handle,
				  (char*)sb.data + off*dp->bytes_per_frame,
				  sb.frames - off);
		if (n == -EAGAIN)
		    continue;
		if (n < 0) {
		    if ((rc = recover(dp, (int) n)) < 0)
			break;
		    continue;
		}
		off += n;
	    }
	    if (rc < 0)
		break;
	    now = now_ns();
	    if (snd_pcm_delay(dp->handle, &delay) < 0) delay = 0;
	    when = now - frames_ns(dp, delay + sb.frames);
	    if (td->sound_input)
		(*td->sound_input)(&now, &sb, &when, td->data);
	}
    }
    free(sb.data);
    td->running = 0;
    return (rc < 0) ? -1 : 0;
}

static void* sound_thread_main(void* arg)
{
    sound_thread_t* td = (sound_thread_t*) arg;
    sound_main(td);
    return NULL;
}

int sound_thread_start(sound_thread_t* td)
{
    pthread_t* thr;
    int err;

    if ((thr = (pthread_t*) malloc(sizeof(pthread_t))) == NULL)
	return -1;
    td->running = 1;
    if ((err = pthread_create(thr, NULL, sound_thread_main, td)) != 0) {
	free(thr);
	td->running = 0;
	errno = err;
	return -1;
    }
    td->priv = thr;
    return 0;
}

void sound_thread_stop(sound_thread_t* td)
{
    pthread_t* thr = (pthread_t*) td->priv;
    td->running = 0;
    if (thr) {
	pthread_join(*thr, NULL);
	free(thr);
	td->priv = NULL;
    }
}


static snd_pcm_format_t pcm_format(sound_format_t* format)
{
    if (format->flags & SOUND_FORMAT_BIG_ENDIAN) {
	if (format->flags & SOUND_FORMAT_FLOAT) {
	    switch(format->bits_per_channel) {
	    case 0: case 32: return SND_PCM_FORMAT_FLOAT_BE;
	    case 64: return SND_PCM_FORMAT_FLOAT64_BE;
	    default: return SND_PCM_FORMAT_UNKNOWN;
	    }
	}
	else if (format->flags & SOUND_FORMAT_SIGNED) {
	    switch(format->bits_per_channel) {
	    case 8:  return SND_PCM_FORMAT_S8;
	    case 16: return SND_PCM_FORMAT_S16_BE;
	    case 24: return SND_PCM_FORMAT_S24_BE;
	    case 32: return SND_PCM_FORMAT_S32_BE;
	    default: return SND_PCM_FORMAT_UNKNOWN;
	    }
	}
	else {
	    switch(format->bits_per_channel) {
	    case 8: return SND_PCM_FORMAT_U8;
	    case 16: return SND_PCM_FORMAT_U16_BE;
	    case 24: return SND_PCM_FORMAT_U24_BE;
	    case 32: return SND_PCM_FORMAT_U32_BE;
	    default: return SND_PCM_FORMAT_UNKNOWN;
	    }
	}
    }
    else {  // LITTLE ENDIAN
	if (format->flags & SOUND_FORMAT_FLOAT) {
	    switch(format->bits_per_channel) {
	    case 0: case 32: return SND_PCM_FORMAT_FLOAT_LE;
	    case 64: return SND_PCM_FORMAT_FLOAT64_LE;
	    default: return SND_PCM_FORMAT_UNKNOWN;
	    }
	}
	else if (format->flags & SOUND_FORMAT_SIGNED) {
	    switch(format->bits_per_channel) {
	    case 8: return SND_PCM_FORMAT_S8;
	    case 16: return SND_PCM_FORMAT_S16_LE;
	    case 24: return SND_PCM_FORMAT_S24_LE;
	    case 32: return SND_PCM_FORMAT_S32_LE;
	    default: return SND_PCM_FORMAT_UNKNOWN;
	    }
	}
	else {
	    switch(format->bits_per_channel) {
	    case 8: return SND_PCM_FORMAT_U8;
	    case 16: return SND_PCM_FORMAT_U16_LE;
	    case 24: return SND_PCM_FORMAT_U24_LE;
	    case 32: return SND_PCM_FORMAT_U32_LE;
	    default: return SND_PCM_FORMAT_UNKNOWN;
	    }
	}
    }
}
