// canbaud -- measure a CAN bus bit rate on an RP2040.
//
// Wire the transceiver's RX (the digital side, NOT CAN_H/CAN_L) to CAN_RX_PIN.
// On an Adafruit Feather RP2040 CAN the transceiver talks to an MCP2515 over
// its own RXD trace, so that needs a wire; a board with a bare transceiver has
// RX on a header already.
//
// WHY SAMPLING AND NOT EDGE TIMING. Timing edges in PIO needs a counting loop
// whose cycle cost must be right in every branch, and a miscount produces a
// plausible-looking wrong answer. Sampling has no branches: one clock per bit,
// autopush every 32, and the run lengths are recovered on the CPU where they
// can be checked against each other.
//
// WHY A HISTOGRAM AND NOT THE MINIMUM. The shortest pulse IS one bit time, but
// a single glitch or a ringing edge makes it shorter, and there is nothing in
// one sample to say it was wrong. Every gap on a CAN bus is an integer multiple
// of the bit time -- bit stuffing allows at most five alike in a row -- so the
// histogram has peaks at 1x, 2x .. 5x, and the SPACING between peaks is the bit
// time measured over hundreds of edges instead of one.
//
// That also reads CAN FD for free: arbitration and data phases run at different
// rates in the same frame, so a FD bus shows two families of peaks.

#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "canbaud.pio.h"

#define CAN_RX_PIN   6          // wire the transceiver's RXD here
#define CAP_WORDS    8192       // 262144 samples = 2.1 ms at 125 MHz
#define MAX_RUN      4096       // runs longer than this are bus-idle, not bits

static uint32_t cap[CAP_WORDS];
static uint32_t hist[MAX_RUN + 1];

static PIO   pio = pio0;
static uint  sm, offset, dma_chan;
static float sample_ns;

static void capture_setup(void)
{
    pio_sm_config c;

    offset = pio_add_program(pio, &cansample_program);
    sm     = pio_claim_unused_sm(pio, true);
    pio_gpio_init(pio, CAN_RX_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, CAN_RX_PIN, 1, false);

    c = cansample_program_get_default_config(offset);
    sm_config_set_in_pins(&c, CAN_RX_PIN);
    // Shift RIGHT with autopush at 32: sample n lands in bit n%32 of word n/32,
    // so the words read back in time order with no reversal.
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    // clkdiv 1: one sample per system clock. Everything downstream is expressed
    // in samples, so the only place the clock rate appears is sample_ns.
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(pio, sm, offset, &c);

    sample_ns = 1e9f / (float)clock_get_hz(clk_sys);
    dma_chan  = dma_claim_unused_channel(true);
}

// One burst. Returns when CAP_WORDS have landed.
static void capture_burst(void)
{
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    dma_channel_configure(dma_chan, &dc, cap, &pio->rxf[sm], CAP_WORDS, true);
    pio_sm_set_enabled(pio, sm, true);
    dma_channel_wait_for_finish_blocking(dma_chan);
    pio_sm_set_enabled(pio, sm, false);
}

// Run lengths into the histogram. The first and last runs are dropped: they are
// cut by the capture window, not by an edge, so their length means nothing.
static uint32_t runs_to_hist(void)
{
    uint32_t i, total = 0, len = 0, first = 1;
    int prev = -1;

    memset(hist, 0, sizeof(hist));
    for (i = 0; i < CAP_WORDS * 32; i++) {
	int bit = (cap[i >> 5] >> (i & 31)) & 1;
	if (bit == prev) {
	    len++;
	    continue;
	}
	if (prev >= 0 && !first) {
	    if (len <= MAX_RUN) { hist[len]++; total++; }
	}
	first = 0;
	prev  = bit;
	len   = 1;
    }
    return total;
}

// The bit time, in samples.
//
// Try each plausible candidate and score it by how much of the bus it explains:
// a real bit time makes nearly every run land within tolerance of an integer
// multiple of itself, and a wrong one does not. Scoring by WEIGHT (runs seen)
// rather than by distinct lengths keeps one rare glitch from outvoting the bus.
static float find_bit_samples(uint32_t total, float *coverage)
{
    uint32_t cand, best = 0;
    float best_score = 0.0f;

    for (cand = 3; cand <= MAX_RUN / 2; cand++) {
	uint32_t hit = 0, len;
	if (hist[cand] == 0)
	    continue;                        // only lengths actually seen
	for (len = 1; len <= MAX_RUN; len++) {
	    float k, frac;
	    if (!hist[len]) continue;
	    k = (float)len / (float)cand;
	    if (k < 0.85f || k > 8.5f) continue;   // 1..8 bit times
	    frac = k - (float)(int)(k + 0.5f);
	    if (frac < 0) frac = -frac;
	    if (frac <= 0.15f) hit += hist[len];   // within 15% of a multiple
	}
	// Prefer the SMALLEST candidate that explains the bus: a bit time's
	// double explains every even multiple just as well, so scoring alone
	// would happily report half the real rate.
	if ((float)hit > best_score * 1.02f) { best_score = (float)hit; best = cand; }
    }
    *coverage = total ? best_score / (float)total : 0.0f;
    return (float)best;
}

// Runs a candidate accounts for; the rest are handed back for a second pass.
static uint32_t explained(uint32_t cand, uint32_t *rest, uint32_t *rest_total)
{
    uint32_t len, keep = 0;

    *rest_total = 0;
    memset(rest, 0, (MAX_RUN + 1) * sizeof(uint32_t));
    for (len = 1; len <= MAX_RUN; len++) {
	float k, frac;
	if (!hist[len]) continue;
	k = (float)len / (float)cand;
	frac = k - (float)(int)(k + 0.5f);
	if (frac < 0) frac = -frac;
	if (k >= 0.85f && k <= 8.5f && frac <= 0.15f) keep += hist[len];
	else { rest[len] = hist[len]; *rest_total += hist[len]; }
    }
    return keep;
}

static void report(void)
{
    uint32_t total = runs_to_hist();
    float coverage, bit_s, bit_ns, rate;
    uint32_t len, shown = 0;

    if (total < 20) {
	Serial.println("no traffic (fewer than 20 edges in the window)");
	return;
    }
    bit_s = find_bit_samples(total, &coverage);
    if (bit_s < 1.0f) { Serial.println("no consistent bit time found"); return; }

    bit_ns = bit_s * sample_ns;
    rate   = 1e9f / bit_ns;

    Serial.printf("edges %lu   sample %.1f ns   bit %.0f samples = %.0f ns\n",
		  (unsigned long)total, sample_ns, bit_s, bit_ns);
    Serial.printf("bit rate %.1f kbit/s   (explains %.0f%% of runs)\n",
		  rate / 1000.0f, coverage * 100.0f);

    // A SECOND rate, if what the first one could not explain is a real
    // population rather than tails. On CAN FD the data phase wins the first
    // pass -- it carries the most bits -- and arbitration falls out here.
    //
    // LIMIT, and it is in the method rather than the code: when the two rates
    // differ by a small integer factor, an arbitration bit is indistinguishable
    // from that many data bits by LENGTH alone. 500k over 2M (exactly 4x) comes
    // back as a multiple instead of the real rate; 5M and 8M data phases
    // separate cleanly because their ratios fall outside the 1..8 window. Doing
    // better needs the frames' STRUCTURE -- split each frame at its BRS bit and
    // analyse the halves -- not a smarter histogram.
    if (coverage < 0.90f) {
	static uint32_t rest[MAX_RUN + 1];
	uint32_t rest_total;
	explained((uint32_t)bit_s, rest, &rest_total);
	if (rest_total > total / 10) {
	    float cov2, b2;
	    memcpy(hist, rest, sizeof(rest));    // find_bit reads hist
	    b2 = find_bit_samples(rest_total, &cov2);
	    if (b2 >= 1.0f && (b2 > bit_s * 1.2f || b2 < bit_s * 0.8f))
		Serial.printf("second phase: bit %.0f ns = %.1f kbit/s "
			      "(%.0f%% of runs) -- looks like CAN FD\n",
			      b2 * sample_ns, 1e6f / (b2 * sample_ns),
			      100.0f * rest_total / total);
	    runs_to_hist();                      // rebuild for the dump below
	}
    }

    // The histogram itself, because it is the evidence. Peaks at 1x..5x say the
    // answer is a bit time; a second family of peaks says the bus is CAN FD and
    // this is only one of its two rates.
    Serial.println("run lengths (samples x count):");
    for (len = 1; len <= MAX_RUN && shown < 12; len++) {
	if (hist[len] * 40 < total) continue;        // only the peaks
	Serial.printf("  %5lu  x%-6lu  %.1f bits\n",
		      (unsigned long)len, (unsigned long)hist[len],
		      (float)len / bit_s);
	shown++;
    }
}

void setup(void)
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { }
    pinMode(CAN_RX_PIN, INPUT);
    capture_setup();
    Serial.printf("canbaud: sampling GPIO%d at %.1f MHz\n",
		  CAN_RX_PIN, clock_get_hz(clk_sys) / 1e6f);
}

void loop(void)
{
    capture_burst();
    report();
    Serial.println();
    delay(1000);
}
