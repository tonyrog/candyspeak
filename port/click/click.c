//
// click: demo of clicker - listen to a (simulated) main loop
//
//   click                 50 Hz loop for 10 s
//   click -r 200 -j 0.3   200 Hz loop with 30% timing jitter
//   click -r 20 -s        20 Hz loop that stalls now and then
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "clicker.h"

static volatile sig_atomic_t quit = 0;

static void on_sigint(int sig) { (void) sig; quit = 1; }

// Print basic channel info
static void print_audio_description(sound_device_t* device,
				    const sound_format_t* fmt)
{
    char buf[256];

    if (sound_device_name(device,buf,sizeof(buf)) != -1)
	printf("    device name: %s\n", buf);
    printf("    buffer size: %d frames\n",
	   sound_device_buffer_size(device, fmt->dir));
    printf("    sample_rate: %.0f\n", fmt->sample_rate);
    printf("    format:");
    printf(fmt->flags & SOUND_FORMAT_BIG_ENDIAN ? " big-endian" :
	   " little-endian");
    if (fmt->flags & SOUND_FORMAT_FLOAT)  printf(" float");
    if (fmt->flags & SOUND_FORMAT_SIGNED) printf(" signed");
    printf("\n");
    printf("    bytes_per_packet: %u\n", fmt->bytes_per_packet);
    printf("    frames_per_packet: %u\n",fmt->frames_per_packet);
    printf("    bytes_per_frame: %u\n",  fmt->bytes_per_frame);
    printf("    channels_per_frame: %u\n", fmt->channels_per_frame);
    printf("    bits_per_channel: %u\n", fmt->bits_per_channel);
}

static void usage(void)
{
    fprintf(stderr,
	    "usage: click [-a device] [-r loop-hz] [-j jitter 0..1]\n"
	    "             [-t seconds] [-v volume 0..1] [-f click-hz] [-s]\n");
    exit(1);
}

static void sleep_s(double s)
{
    struct timespec ts;
    if (s <= 0) return;
    ts.tv_sec  = (time_t) s;
    ts.tv_nsec = (long) ((s - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

int main(int argc, char** argv)
{
    clicker_config_t cfg;
    double loop_hz = 50.0, jitter = 0.0, seconds = 10.0;
    int stalls = 0;
    long n, iterations;
    int c;

    memset(&cfg, 0, sizeof(cfg));
    while((c = getopt(argc, argv, "a:r:j:t:v:f:s")) != -1) {
	switch(c) {
	case 'a': cfg.device = optarg; break;
	case 'r': loop_hz = atof(optarg); break;
	case 'j': jitter = atof(optarg); break;
	case 't': seconds = atof(optarg); break;
	case 'v': cfg.volume = atof(optarg); break;
	case 'f': cfg.freq = atof(optarg); break;
	case 's': stalls = 1; break;
	default:  usage();
	}
    }
    if (loop_hz <= 0) usage();

    if (clicker_start(&cfg) < 0) {
	sound_device_print_last_error(stderr);
	exit(1);
    }
    print_audio_description(clicker_device(), clicker_format());

    signal(SIGINT, on_sigint);
    iterations = (long) (seconds * loop_hz);
    for (n = 0; !quit && (n < iterations); n++) {
	double period = 1.0 / loop_hz;
	// "work"
	if (jitter > 0)
	    period *= 1.0 + jitter * (2.0*rand()/RAND_MAX - 1.0);
	if (stalls && (rand() % (int)(loop_hz*2+1)) == 0)
	    period += 0.3;               // simulated hiccup
	sleep_s(period);

	clicker_click();                 // <- the one line you add to your loop
    }
    clicker_stop();
    if (clicker_dropped())
	fprintf(stderr, "dropped %llu clicks\n",
		(unsigned long long) clicker_dropped());
    return 0;
}
