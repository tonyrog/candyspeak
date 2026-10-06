// The NXP LPC chip layer behind csp_chip_io.h, through LPCOpen (and the
// LPCOpen-shaped drivers in chips/nxp/drivers/212x for the ARM7).
//
// Pins, the millisecond tick, the ADC and the PWM/DAC seams, with no csp_rt_t
// anywhere: what used to be the bottom half of port/csp_lpcopen.c. The port
// keeps what is CandySpeak's -- the console, EEPROM, CAN and the pin
// interrupts, which deliver into the runtime's event slots.
//
// PORTS AND PINS are the chip's own: `0:13` is GPIO port 0 pin 13. An #analog
// is selected by port -- CSP_LPC_ADC_PORT reads a converter channel (or the
// board's map, csp_lpc_adc_channel), CSP_LPC_DAC_PORT writes the DAC, and any
// other port is a PWM pin through csp_lpc_pwm_write.

#include <stdint.h>
#include "csp_lpc.h"
#include "csp_chip_io.h"

// ============================================================
// Board hooks -- STUBS. These are the four places board wiring shows through.
// ============================================================

// STUB: pin muxing. LPCOpen keeps this out of the chip drivers on purpose --
// which IOCON/SCU function a pin needs is a property of the board, not of the
// part. Called once per declared device at setup, before anything is driven.
//
// A real one looks like (17xx/40xx):
//     Chip_IOCON_PinMux(LPC_IOCON, port, pin, IOCON_MODE_INACT, IOCON_FUNC0);
// or (18xx/43xx, where the SCU group is NOT the GPIO port number):
//     Chip_SCU_PinMuxSet(group, gpin, SCU_MODE_INACT | SCU_MODE_FUNC0);
//
// `analog` says the caller wants the pin as an ADC/DAC input rather than GPIO,
// which on most parts means clearing the digital-mode bit (IOCON_ADMODE_EN).
// Left empty, a board whose reset-default mux is already GPIO still works --
// which is why this is a no-op and not an error.
void csp_lpc_pin_mux(uint8_t port, uint8_t pin, int analog)
{
    (void)port; (void)pin; (void)analog;
}

// STUB: which ADC channel a `port:pin` names. The default is the identity --
// `15:3` is ADC channel 3 -- which is right whenever the .csp names channels
// directly. Override it if you would rather name board connector numbers.
// Return < 0 to refuse the pin; the read then yields 0 rather than sampling a
// channel nobody asked for.
// WEAK: a board that states which pins go to the converter replaces this with
// a real map -- see chips/nxp/drivers/common/csp_board.c, which builds one from
// boards/<name>.terms so a .csp can name the connector pin rather than the
// channel number.
__attribute__((weak))
int csp_lpc_adc_channel(uint8_t port, uint8_t pin)
{
    // The PORT test belongs here and not in the caller. This stub answers only
    // for the pseudo-port -- `15:3` is channel 3 -- and refuses everything else,
    // which is what makes a board's real map able to answer for `0:27`.
    return ((port == CSP_LPC_ADC_PORT) && (pin < 8)) ? (int)pin : -1;
}

// STUB: PWM output. There is no portable answer here -- 17xx has MCPWM and the
// timer match outputs, 15xx and 43xx have the SCT, and which one is wired to a
// given pin is a board fact. `val` is already scaled to 0..255.
//
// WEAK, so a chip layer that has a real one replaces it by linking -- see
// chips/nxp/drivers/212x/pwm_212x.c, which drives both the PWM0 block and the
// timer match outputs because an LPC2129's one PWM block does not reach every
// pin a board wants to dim.
__attribute__((weak))
void csp_lpc_pwm_write(uint8_t port, uint8_t pin, int val)
{
    (void)port; (void)pin; (void)val;
}

// STUB: DAC output, for an #analog on CSP_LPC_DAC_PORT. On the parts that have
// one this is genuinely two lines --
//     Chip_DAC_Init(LPC_DAC);                  (once, at setup)
//     Chip_DAC_UpdateValue(LPC_DAC, val);      (here, val is 0..1023)
// -- but dac_*.h is not present on every family, so it stays out of the build
// until you say which one you are on.
void csp_lpc_dac_write(uint8_t pin, int val)
{
    (void)pin; (void)val;
}

// STUB: anything the board needs before CandySpeak has memory. Board_Init() in
// an LPCOpen example does: SystemCoreClockUpdate, clock setup, then the pin mux
// for the console UART. The UART itself is set up by csp_lpc_uart_init below,
// so this is for the rest -- power to peripherals, an external oscillator, a
// PHY reset line.
void csp_lpc_board_init(void)
{
}

// ============================================================
// Time -- SysTick at 1 kHz
// ============================================================
//
// SysTick_Handler is WEAK in the LPCOpen startup files, so defining it here
// overrides the do-nothing one without touching the vector table.
//
// `volatile` is not decoration: the loop below spins on this while an interrupt
// changes it, and without it the compiler is entitled to hoist the read out.

static volatile uint32_t csp_ticks_ms = 0;

void SysTick_Handler(void)
{
    csp_ticks_ms++;
}

// The same thing under a neutral name, for a chip layer that has no SysTick to
// hang it on. The ARM7 VIC points a slot at a wrapper that clears the timer's
// interrupt flag and calls this -- see Chip_Tick_Init in chip_212x.c.
void csp_tick_isr(void) { csp_ticks_ms++; }

// The tick seam. Declared HERE because it is this file's contract -- the chip
// layer implements it, and the two families implement it differently:
// Cortex-M below over SysTick, ARM7 in chip_212x.c over a timer match.
//
// Counting UP, deliberately. SysTick's VAL counts DOWN from LOAD and an
// LPC2000 TC counts up from zero; picking one and making the other pretend
// would hand csp_time_us a number that runs backwards inside every period.
void     Chip_Tick_Init(uint32_t hz);
uint32_t Chip_Tick_Us(void);

// The 1 ms tick, through the seam rather than through SysTick directly: SysTick
// is a Cortex-M peripheral and this file also serves an ARM7, where the tick is
// a timer match. Chip_Tick_* is implemented by both -- see chip_212x.h for why
// the seam counts UP and SysTick does not.
#if defined(__CORTEX_M)
static uint32_t tick_reload;
void Chip_Tick_Init(uint32_t hz)
{
    SystemCoreClockUpdate();
    tick_reload = SystemCoreClock / (hz ? hz : 1000u);
    SysTick_Config(tick_reload);
}
// Microseconds, composed from the ms counter and the fraction of the current
// period SysTick has left. VAL counts DOWN from LOAD, so elapsed-within-the-
// period is LOAD - VAL -- an LPC2000 has the number already and this is the
// side that has to build it.
//
// Read ms twice around the counter and retry if it moved: the counter wraps
// exactly when ms increments, so a naive pair can report a time a whole
// millisecond early.
uint32_t Chip_Tick_Us(void)
{
    uint32_t ms, val, ms2;
    do {
	ms  = csp_ticks_ms;
	val = tick_reload - SysTick->VAL;
	ms2 = csp_ticks_ms;
    } while (ms != ms2);
    return ms * 1000UL + ((val * 1000UL) / (tick_reload ? tick_reload : 1));
}
#endif


// ============================================================
// ADC
// ============================================================

#if defined(CSP_LPC_ADC_CLASSIC)
static ADC_CLOCK_SETUP_T csp_adc_setup;

static void csp_lpc_adc_init(void)
{
    Chip_ADC_Init(CSP_LPC_ADC, &csp_adc_setup);
    Chip_ADC_SetSampleRate(CSP_LPC_ADC, &csp_adc_setup, CSP_LPC_ADC_RATE);
}

// One blocking conversion. Burst mode would be better for several channels --
// it samples them in the background and this becomes a register read -- but it
// needs a channel set known up front, and the device list is not fixed until
// csp_setup has walked it. Start there if the ADC ever shows up in a profile.
static int csp_lpc_adc_read(int ch)
{
    uint16_t data = 0;

    Chip_ADC_EnableChannel(CSP_LPC_ADC, (ADC_CHANNEL_T)ch, ENABLE);
    Chip_ADC_SetStartMode(CSP_LPC_ADC, ADC_START_NOW, ADC_TRIGGERMODE_RISING);
    while (Chip_ADC_ReadStatus(CSP_LPC_ADC, ch, ADC_DR_DONE_STAT) != SET)
	;
    Chip_ADC_ReadValue(CSP_LPC_ADC, ch, &data);
    Chip_ADC_EnableChannel(CSP_LPC_ADC, (ADC_CHANNEL_T)ch, DISABLE);
    return (int)data;
}
#else
// STUB: the LPC15xx ADC is sequencer-based -- Chip_ADC_Init takes flags, then a
// sequence is configured (Chip_ADC_SetupSequencer) and started, and the result
// comes from Chip_ADC_GetDataReg. Same shape, different calls; the rest of this
// file does not care which.
static void csp_lpc_adc_init(void) { }
static int csp_lpc_adc_read(int ch) { (void)ch; return 0; }
#endif

// WEAK and empty by default: a chip layer with real PWM defines it
// (pwm_212x.c). Called after the tick is running -- on an LPC2000 the PWM
// period on TIMER0 is scheduled as TC + period.
__attribute__((weak)) void csp_pwm_init(void) { }

// ============================================================
// csp_chip_io.h
// ============================================================

// The tick first, then the peripherals. The console UART is not here: it is
// the port's, and a program translated to C may not have one.
void csp_chip_init(void)
{
    Chip_Tick_Init(1000);
    csp_lpc_board_init();
    Chip_GPIO_Init(LPC_GPIO);
    csp_lpc_adc_init();
    csp_pwm_init();
}

uint32_t csp_chip_millis(void)
{
    return csp_ticks_ms;
}

// The single description of what a digital pin's configuration MEANS in
// hardware. Setup, a rule that writes .dir, and an inout pin handed back as an
// input all come here, so those paths cannot drift apart.
//
// A pin with no direction at all is left alone: the program said nothing about
// it, and asserting a mode on a pin someone else owns is worse than silence.
//
// PULLUPS ARE NOT HERE. They live in IOCON/SCU, not in the GPIO block, and the
// register layout differs per family -- so `pull` reaches csp_lpc_pin_mux's
// territory and is the board's to apply. A pin declared `in pullup` works as a
// plain input until then, which is the safe way to be wrong.
void csp_chip_dcfg(uint8_t port, uint8_t pin, uint8_t dir, uint8_t pull)
{
    (void)pull;
    csp_lpc_pin_mux(port, pin, 0);
    if (dir & CSP_CHIP_IN)
	Chip_GPIO_SetPinDIRInput(LPC_GPIO, port, pin);
    else if (dir & CSP_CHIP_OUT)
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, port, pin);
}

int csp_chip_din(uint8_t port, uint8_t pin)
{
    return Chip_GPIO_GetPinState(LPC_GPIO, port, pin) ? 1 : 0;
}

void csp_chip_dout(uint8_t port, uint8_t pin, int v)
{
    Chip_GPIO_SetPinState(LPC_GPIO, port, pin, (v != 0));
}

// Only a PWM or DAC output owns its pin in a way that has to be asserted -- an
// ADC read needs no direction at all -- so an input just muxes as analog.
void csp_chip_acfg(uint8_t port, uint8_t pin, uint8_t dir, uint8_t pwm)
{
    if (dir & CSP_CHIP_IN)
	csp_lpc_pin_mux(port, pin, 1);
    else if ((dir & CSP_CHIP_OUT) && pwm)
	csp_lpc_pin_mux(port, pin, 0);
}

// Ask the MAP, whatever port the declaration named: the whole reason the map
// exists is so a program can say `in 0:27` -- the screw terminal -- instead of
// `15:0`, the converter channel. An unmapped pin reads 0.
uint16_t csp_chip_ain(uint8_t port, uint8_t pin)
{
    int ch = csp_lpc_adc_channel(port, pin);

    if (ch < 0)
	return 0;
    return (uint16_t)(csp_lpc_adc_read(ch) << (16 - CSP_LPC_ADC_BITS));
}

// The DAC takes ten bits and a PWM pin 0..255. Any analog output not on the
// DAC port goes to csp_lpc_pwm_write, which ignores a pin it has no channel
// for.
void csp_chip_aout(uint8_t port, uint8_t pin, uint16_t v)
{
    if (port == CSP_LPC_DAC_PORT)
	csp_lpc_dac_write(pin, v >> 6);
    else
	csp_lpc_pwm_write(port, pin, v >> 8);
}
