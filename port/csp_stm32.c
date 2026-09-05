// CandySpeak on STM32F4, on the registers.
//
// The same job csp_lpcopen.c does for NXP, with one deliberate difference:
// there is no vendor HAL under this. cmsis-device-f4 supplies the register map
// (stm32f405xx.h) and ARM's CMSIS-Core supplies NVIC/SysTick/SCB, and
// everything between those and CandySpeak is here. ST's HAL would be a third
// layer with its own handle structs and its own init-time allocation, for six
// peripherals we drive directly in a few hundred lines.
//
// PORTS AND PINS. `#digital Led out 2:0` is GPIOC pin 0 -- port 0 is GPIOA and
// they run in order, exactly as the part numbers them. An #analog needs a
// peripheral rather than a GPIO and is selected by PORT:
//
//   #analog Batt:12 in 15:9        port 15 = ADC1, pin = channel  -> ADC1_IN9
//   #analog Out:12 out 13:0        port 13 = DAC channel 1
//   #analog M1:15 out pwm 0:1      `pwm` on a GPIO port -> the board's PWM map
//
// THE BOARD HEADER IS WHERE THE WIRING LIVES. csp_board.h is generated from
// boards/<name>.terms and carries three things this file cannot know: which
// alternate function each pin needs (CSP_BOARD_PINS), which timer channel a PWM
// pin is (CSP_BOARD_PWM), and which peripheral clocks to enable
// (CSP_STM_RCC_*). See gen_chips.erl.

// stdint FIRST: csp_config.h types the reactive gate mask (uint16_t) and there
// is no core header here to have pulled it in already.
#include <stdint.h>
#include <stddef.h>

#include "csp_config.h"

// Bounds the idle sleep, so continuous inputs keep sampling even when a slow
// timer is armed. See the end of csp_loop.
#ifndef SAMPLE_MS
#define SAMPLE_MS 2
#endif

#include "stm32f405xx.h"

#include <stdlib.h>
#include <string.h>

#define CSP_EMBEDDED 1
#include "csp.h"
#include "csp_print.h"
#include "csp_strings.h"

#if defined(CSP_EXEC_ONLY)
#define CSP_CSTATE NULL
#else
#include "csp_compile.h"
#define CSP_CSTATE csp_cstate()
#endif

// Clocks, computed by sysinit_f405.c from the board's crystal. Read rather than
// recomputed: two places deriving the same frequency is two places to get it
// wrong, and the one that boots is the one that is right.
extern const uint32_t csp_stm_hclk;
extern const uint32_t csp_stm_pclk1;
extern const uint32_t csp_stm_pclk2;
extern const uint32_t csp_stm_tim1;   // timers on APB1, doubler applied
extern const uint32_t csp_stm_tim2;   // timers on APB2

// --- board knobs ------------------------------------------------------------
#ifndef CSP_STM_ADC_PORT
#define CSP_STM_ADC_PORT  15         // an #analog here reads an ADC channel
#endif
#ifndef CSP_STM_DAC_PORT
#define CSP_STM_DAC_PORT  13         // ...and here it writes the DAC
#endif
#ifndef CSP_STM_ADC_BITS
#define CSP_STM_ADC_BITS  12
#endif
#ifndef CSP_STM_BAUD
#define CSP_STM_BAUD      115200
#endif
#ifndef CSP_STM_PWM_HZ
// 328 Hz is what a brushed-motor drone wants and 50 Hz is what a servo wants.
// Neither is a safe default for the other, so this is the middle that damages
// nothing and every board that cares states its own.
#define CSP_STM_PWM_HZ    1000
#endif

// The GPIO blocks, in port order. A port number from a declaration indexes this
// directly -- `2:0` is entry 2, GPIOC.
static GPIO_TypeDef* const gpio_port[] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI
};
#define NPORTS ((int)(sizeof(gpio_port)/sizeof(gpio_port[0])))

// Pin modes, as the board header spells them.
#define CSP_MODE_IN      0
#define CSP_MODE_OUT     1
#define CSP_MODE_AF      2
#define CSP_MODE_ANALOG  3

typedef struct {
    uint8_t port, pin, mode, af;
} csp_stm_pin_t;

typedef struct {
    uint8_t port, pin, tim, ch;      // tim: 1..14, ch: 1..4
} csp_stm_pwm_t;

#if defined(CSP_BOARD_PINS)
static const csp_stm_pin_t board_pins[] = { CSP_BOARD_PINS };
#endif
#if defined(CSP_BOARD_PWM)
static const csp_stm_pwm_t board_pwm[] = { CSP_BOARD_PWM };
#endif

// ============================================================
// GPIO
// ============================================================

// Two bits per pin in MODER and PUPDR, one in OTYPER and ODR, four in AFR.
// Written read-modify-write, so configuring one pin does not disturb its
// fifteen neighbours -- which a whole-register write would, and silently.
static void stm_pin_mode(uint8_t port, uint8_t pin, uint8_t mode, uint8_t af)
{
    GPIO_TypeDef* g;

    if (port >= NPORTS || pin > 15)
	return;
    g = gpio_port[port];

    if (mode == CSP_MODE_AF) {
	// AFR is two 32-bit words of eight 4-bit fields. Pin 8 and up is the
	// high word, and the shift is within that word -- not within 32 pins.
	volatile uint32_t* afr = &g->AFR[pin >> 3];
	*afr = (*afr & ~(0xFu << ((pin & 7u) * 4u))) |
	       ((uint32_t)af << ((pin & 7u) * 4u));
    }
    g->MODER = (g->MODER & ~(3u << (pin * 2u))) | ((uint32_t)mode << (pin * 2u));
}

static void stm_pin_pull(uint8_t port, uint8_t pin, int up, int down)
{
    GPIO_TypeDef* g;
    uint32_t v;

    if (port >= NPORTS || pin > 15)
	return;
    g = gpio_port[port];
    v = up ? 1u : (down ? 2u : 0u);
    g->PUPDR = (g->PUPDR & ~(3u << (pin * 2u))) | (v << (pin * 2u));
}

// BSRR, not ODR. A read-modify-write of ODR loses a concurrent write to another
// pin of the same port from an interrupt; BSRR sets and clears atomically, one
// bit each, which is what the register exists for.
static void stm_pin_write(uint8_t port, uint8_t pin, int on)
{
    if (port >= NPORTS || pin > 15)
	return;
    gpio_port[port]->BSRR = on ? (1u << pin) : (1u << (pin + 16u));
}

static int stm_pin_read(uint8_t port, uint8_t pin)
{
    if (port >= NPORTS || pin > 15)
	return 0;
    return (gpio_port[port]->IDR & (1u << pin)) ? 1 : 0;
}

void csp_board_digital_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    csp_set_ivalue(st, ix, stm_pin_read(vptr->d.port, vptr->d.pin));
}

// An inout pin is borrowed for the length of one write and handed straight back
// as an input, which is what makes a bidirectional line usable from a rule.
void csp_board_digital_output(csp_rt_t* st, value_t* vptr)
{
    (void)st;
    if (vptr->d.dir & DIR_IN) {
	stm_pin_mode(vptr->d.port, vptr->d.pin, CSP_MODE_OUT, 0);
	stm_pin_write(vptr->d.port, vptr->d.pin, (vptr->d.val & 1) != 0);
	stm_pin_mode(vptr->d.port, vptr->d.pin, CSP_MODE_IN, 0);
    }
    else {
	stm_pin_write(vptr->d.port, vptr->d.pin, (vptr->d.val & 1) != 0);
    }
}

// The single description of what a digital slot's configuration MEANS in
// hardware, so setup and a rule that writes .dir cannot drift apart.
//
// PULLUPS ARE HERE, unlike the LPC port. On STM32 they are in the GPIO block
// (PUPDR) rather than in a separate pin-control peripheral, so there is no
// reason to make the board deal with them.
void csp_board_digital_config(value_t* vptr)
{
    if (vptr->d.dir & DIR_IN) {
	stm_pin_mode(vptr->d.port, vptr->d.pin, CSP_MODE_IN, 0);
	stm_pin_pull(vptr->d.port, vptr->d.pin, vptr->d.pullup, vptr->d.pulldown);
    }
    else if (vptr->d.dir & DIR_OUT) {
	stm_pin_mode(vptr->d.port, vptr->d.pin, CSP_MODE_OUT, 0);
    }
}

// ============================================================
// The tick
// ============================================================

static volatile uint32_t csp_ticks_ms = 0;
static uint32_t tick_reload;

void SysTick_Handler(void)
{
    csp_ticks_ms++;
}

void csp_tick_isr(void) { csp_ticks_ms++; }

static void stm_tick_init(void)
{
    tick_reload = csp_stm_hclk / 1000u;
    SysTick_Config(tick_reload);
}

uint32_t csp_time_ms(void)
{
    return csp_ticks_ms;
}

// Microseconds, composed from the ms counter and the fraction of the current
// period SysTick has left. VAL counts DOWN from LOAD, so elapsed-within-the-
// period is LOAD - VAL.
//
// Read ms twice around the counter and retry if it moved: the counter wraps
// exactly when ms increments, so a naive pair can report a time a whole
// millisecond early.
unsigned long csp_time_us(void)
{
    uint32_t ms, val, ms2;

    do {
	ms  = csp_ticks_ms;
	val = tick_reload - SysTick->VAL;
	ms2 = csp_ticks_ms;
    } while (ms != ms2);
    return (unsigned long)(ms * 1000UL +
			   ((val * 1000UL) / (tick_reload ? tick_reload : 1)));
}

static void csp_delay_ms(uint32_t ms)
{
    uint32_t t0 = csp_ticks_ms;
    while ((csp_ticks_ms - t0) < ms)
	__WFI();
}

// ============================================================
// Console UART
// ============================================================

#ifndef CSP_STM_UART
#define CSP_STM_UART USART2
#endif

static int serial_output = 0;

// Which APB the console sits on, because the baud divisor is computed from that
// bus and USART1/6 are on APB2 while the rest are on APB1. Getting it wrong
// gives a port that transmits at half or double the rate -- garbage that looks
// like a wiring fault.
static uint32_t stm_uart_clk(void)
{
    if ((CSP_STM_UART == USART1) || (CSP_STM_UART == USART6))
	return csp_stm_pclk2;
    return csp_stm_pclk1;
}

static void stm_uart_init(void)
{
    uint32_t clk = stm_uart_clk();

    // OVER8 = 0, so the divisor is clk/baud in 12.4 fixed point. Rounded rather
    // than truncated: at 168/4 MHz and 115200 the truncation alone is a 0.6%
    // error, and the receiver's budget for everything is about 2%.
    CSP_STM_UART->BRR = (uint16_t)((clk + (CSP_STM_BAUD / 2u)) / CSP_STM_BAUD);
    CSP_STM_UART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;  // 8N1
    CSP_STM_UART->CR2 = 0;
    CSP_STM_UART->CR3 = 0;
}

static int stm_uart_can_send(void)
{
    return (CSP_STM_UART->SR & USART_SR_TXE) != 0;
}

static int stm_uart_available(void)
{
    return (CSP_STM_UART->SR & USART_SR_RXNE) != 0;
}

static int stm_uart_read(void)
{
    return (int)(CSP_STM_UART->DR & 0xFF);
}

void* csp_set_file_output(void* f)
{
    int prev = serial_output;
    serial_output = (f != NULL);
    return prev ? (void*)1 : (void*)0;
}

int csp_will_output(void)
{
    return serial_output;
}

// The CR belongs here and nowhere else: the runtime ends a line three different
// ways and all three come through csp_print_char.
int csp_print_char(char c)
{
    if (!serial_output)
	return 0;
    if (c == '\n') {
	while (!stm_uart_can_send())
	    ;
	CSP_STM_UART->DR = '\r';
    }
    while (!stm_uart_can_send())
	;
    CSP_STM_UART->DR = (uint8_t)c;
    return 1;
}

int csp_print_str(const char* s)
{
    int n = 0;
    while (*s)
	n += csp_print_char(*s++);
    return n;
}

int csp_print_rostr(rostring_t s)
{
    return csp_print_str((const char*) s);
}

void csp_flush(void)
{
    // Nothing is buffered here -- csp_print_char blocks until the shift
    // register takes the byte -- so this is where the LAST byte is waited for
    // rather than where a buffer is drained. Without it a reset immediately
    // after a print truncates the line.
    while (!(CSP_STM_UART->SR & USART_SR_TC))
	;
}

// ============================================================
// ADC
// ============================================================

static void stm_adc_init(void)
{
    // ADCCLK = PCLK2 / 8. The converter tops out at 36 MHz and PCLK2 is 84, so
    // /4 would be over and /8 is the first legal divisor. Sampling is slower
    // than it has to be by a factor of two; nothing here samples fast enough
    // to notice, and being over the limit is a converter that reads noise.
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE;   // /8
    ADC1->CR1 = 0;                        // 12-bit, no scan, no interrupts
    ADC1->CR2 = ADC_CR2_ADON;
    ADC1->SQR1 = 0;                       // one conversion in the sequence
}

// 480 cycles on every channel. The longest the part offers, chosen because the
// source impedance of whatever a board wires up is unknown -- a short sample
// time on a high-impedance source reads the previous channel, faintly.
static void stm_adc_sample_time(uint8_t ch)
{
    if (ch <= 9)
	ADC1->SMPR2 = (ADC1->SMPR2 & ~(7u << (ch * 3u))) | (7u << (ch * 3u));
    else
	ADC1->SMPR1 = (ADC1->SMPR1 & ~(7u << ((ch - 10u) * 3u))) |
		      (7u << ((ch - 10u) * 3u));
}

static int stm_adc_read(uint8_t ch)
{
    if (ch > 18)
	return 0;
    stm_adc_sample_time(ch);
    ADC1->SQR3 = ch;                      // the one conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC))
	;
    return (int)(ADC1->DR & 0xFFF);
}

// Scale a raw converter reading into the declared width and sign, exactly as
// the LPC port does -- so `#analog X:8 in` means the same thing on both.
static int stm_scale(csp_rt_t* st, index_t ix, int raw)
{
    csp_decl_t d = csp_get_decl(st, INDEX(ix));
    int res = GET_RES(d.res);
    int sgn = (CSP_MASK(d.vt,TYPE_BITS) != V_UNSIGNED);
    int v;

    // Clamped the same way the LPC port clamps it: a width outside 2..16 is a
    // shift by more than the type has bits, which is undefined rather than
    // merely wrong.
    if (res < 2) res = 2; else if (res > 16) res = 16;
    if (res >= CSP_STM_ADC_BITS)
	v = raw << (res - CSP_STM_ADC_BITS);
    else
	v = raw >> (CSP_STM_ADC_BITS - res);
    if (sgn)
	v -= (1 << (res - 1));                // 0 = mid scale
    return v;
}

void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    int value = 0;

    if (vptr->a.port == CSP_STM_ADC_PORT)
	value = stm_scale(st, ix, stm_adc_read(vptr->a.pin));
    csp_set_ivalue(st, ix, value);
}

// ============================================================
// PWM
// ============================================================

#if defined(CSP_BOARD_PWM)
static TIM_TypeDef* const tim_of[] = {
    NULL, TIM1, TIM2, TIM3, TIM4, TIM5, TIM6, TIM7,
    TIM8, TIM9, TIM10, TIM11, TIM12, TIM13, TIM14
};
#define NTIM ((int)(sizeof(tim_of)/sizeof(tim_of[0])))

// TIM1 and TIM8..11 are on APB2; the rest on APB1. Both buses feed their timers
// at twice the bus clock when the bus is divided -- the "timer clock doubler" --
// which sysinit has already worked out.
static uint32_t tim_clk(uint8_t t)
{
    return ((t == 1) || ((t >= 8) && (t <= 11))) ? csp_stm_tim2 : csp_stm_tim1;
}

static const csp_stm_pwm_t* pwm_of(uint8_t port, uint8_t pin)
{
    unsigned i;
    for (i = 0; i < sizeof(board_pwm)/sizeof(board_pwm[0]); i++)
	if ((board_pwm[i].port == port) && (board_pwm[i].pin == pin))
	    return &board_pwm[i];
    return NULL;
}
#endif  // CSP_BOARD_PWM

// PWM resolution. The period is ARR+1 counts, and the duty this port is handed
// is 0..255 -- so ARR is set to make one count of duty land on a whole number
// of timer counts, which is what keeps the low end from quantising to nothing.
#define PWM_STEPS 255u

static void stm_pwm_init(void)
{
#if defined(CSP_BOARD_PWM)
    unsigned i;
    uint8_t done[NTIM];

    for (i = 0; i < (unsigned)NTIM; i++)
	done[i] = 0;

    for (i = 0; i < sizeof(board_pwm)/sizeof(board_pwm[0]); i++) {
	const csp_stm_pwm_t* p = &board_pwm[i];
	TIM_TypeDef* t;
	uint32_t psc;

	if ((p->tim == 0) || (p->tim >= NTIM))
	    continue;
	t = tim_of[p->tim];

	if (!done[p->tim]) {
	    // period = (PSC+1) * (ARR+1) / clk. ARR is fixed at the duty
	    // resolution and the prescaler absorbs the rest, so every channel
	    // of every timer takes the same 0..255 and means the same thing.
	    psc = tim_clk(p->tim) / (CSP_STM_PWM_HZ * (PWM_STEPS + 1u));
	    t->PSC = (psc > 0) ? (psc - 1u) : 0u;
	    t->ARR = PWM_STEPS;
	    t->CR1 = TIM_CR1_ARPE;
	    done[p->tim] = 1;
	}

	// PWM mode 1 with preload, so a duty written mid-period takes effect at
	// the next update rather than producing one short or long pulse.
	switch (p->ch) {
	case 1:
	    t->CCMR1 = (t->CCMR1 & ~0xFFu) | (6u << 4) | TIM_CCMR1_OC1PE;
	    t->CCER |= TIM_CCER_CC1E;
	    break;
	case 2:
	    t->CCMR1 = (t->CCMR1 & ~0xFF00u) | (6u << 12) | TIM_CCMR1_OC2PE;
	    t->CCER |= TIM_CCER_CC2E;
	    break;
	case 3:
	    t->CCMR2 = (t->CCMR2 & ~0xFFu) | (6u << 4) | TIM_CCMR2_OC3PE;
	    t->CCER |= TIM_CCER_CC3E;
	    break;
	case 4:
	    t->CCMR2 = (t->CCMR2 & ~0xFF00u) | (6u << 12) | TIM_CCMR2_OC4PE;
	    t->CCER |= TIM_CCER_CC4E;
	    break;
	default:
	    break;
	}
	// TIM1 and TIM8 are the advanced timers and their outputs stay
	// disconnected until MOE is set. Nothing else needs this, and a board
	// that puts a motor on TIM1 without it sees a timer counting happily
	// and a pin that never moves.
	if ((p->tim == 1) || (p->tim == 8))
	    t->BDTR |= TIM_BDTR_MOE;
	t->EGR = TIM_EGR_UG;              // load PSC/ARR now
	t->CR1 |= TIM_CR1_CEN;
    }
#endif
}

static void stm_pwm_write(uint8_t port, uint8_t pin, int val)
{
#if defined(CSP_BOARD_PWM)
    const csp_stm_pwm_t* p = pwm_of(port, pin);
    TIM_TypeDef* t;

    if (p == NULL)
	return;
    t = tim_of[p->tim];
    if (val < 0) val = 0;
    if (val > (int)PWM_STEPS) val = (int)PWM_STEPS;

    switch (p->ch) {
    case 1: t->CCR1 = (uint32_t)val; break;
    case 2: t->CCR2 = (uint32_t)val; break;
    case 3: t->CCR3 = (uint32_t)val; break;
    case 4: t->CCR4 = (uint32_t)val; break;
    default: break;
    }
#else
    (void)port; (void)pin; (void)val;
#endif
}

static void stm_dac_write(uint8_t ch, int val)
{
    if (ch == 0)
	DAC->DHR12R1 = (uint32_t)val & 0xFFF;
    else
	DAC->DHR12R2 = (uint32_t)val & 0xFFF;
}

void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
    if (vptr->a.port == CSP_STM_DAC_PORT) {
	stm_dac_write(vptr->a.pin, vptr->a.val);
	return;
    }
    if (vptr->a.pwm) {
	// Scale the declared width down to the 0..255 the hook takes, so a
	// `:16` and a `:8` output differ in precision and not in meaning.
	int full = (1 << GET_RES(decl(st,di,res))) - 1;
	int val  = full ? (int)((vptr->a.val * (int)PWM_STEPS) / full) : 0;
	stm_pwm_write(vptr->a.port, vptr->a.pin, val);
    }
}

void csp_board_analog_config(value_t* vptr)
{
    // An ADC pin needs analog mode -- and on STM32 that is a real mode, not a
    // flag: leaving it digital puts the input buffer across the source and the
    // reading sags. A PWM pin was already put in AF mode by the board pin
    // table, so there is nothing to assert here.
    if ((vptr->a.dir & DIR_IN) && (vptr->a.port != CSP_STM_ADC_PORT))
	stm_pin_mode(vptr->a.port, vptr->a.pin, CSP_MODE_ANALOG, 0);
}

// ============================================================
// Board lifecycle
// ============================================================

static void stm_clocks_on(void)
{
    // Every peripheral this board uses, written WHOLE from the board header --
    // so a peripheral not in {enable,...} stays off rather than being left at
    // whatever a warm reset had. The same argument as PCONP on an LPC.
#if defined(CSP_STM_RCC_AHB1)
    RCC->AHB1ENR = CSP_STM_RCC_AHB1;
#endif
#if defined(CSP_STM_RCC_APB1)
    RCC->APB1ENR |= CSP_STM_RCC_APB1;
#endif
#if defined(CSP_STM_RCC_APB2)
    RCC->APB2ENR = CSP_STM_RCC_APB2;
#endif
    // The writes are posted on this bus: a peripheral touched in the next
    // instruction can be touched before its clock has actually arrived. Reading
    // one back orders it.
    (void)RCC->AHB1ENR;
}

static void stm_board_pins(void)
{
#if defined(CSP_BOARD_PINS)
    unsigned i;
    for (i = 0; i < sizeof(board_pins)/sizeof(board_pins[0]); i++) {
	const csp_stm_pin_t* p = &board_pins[i];
	// Output speed HIGH on every muxed pin. The reset default is 2 MHz,
	// which rounds a 328 Hz PWM edge into something a motor driver reads as
	// noise -- and the cost of high speed on a pin that does not need it is
	// a few mA of edge current.
	if (p->port < NPORTS && p->pin <= 15)
	    gpio_port[p->port]->OSPEEDR |= (3u << (p->pin * 2u));
	stm_pin_mode(p->port, p->pin, p->mode, p->af);
    }
#endif
}

void csp_board_init(void)
{
    stm_clocks_on();
    stm_board_pins();
    stm_adc_init();
    stm_pwm_init();
}

void csp_board_setup(csp_rt_t* st)        { (void)st; }
void csp_board_start_input(csp_rt_t* st)  { (void)st; }
void csp_board_start_output(csp_rt_t* st) { (void)st; }
void csp_board_stop_output(csp_rt_t* st)  { (void)st; }

// ============================================================
// Memory
// ============================================================

extern char _heap_start, _stack_top;

// Declared HERE rather than with the loops below, because the memory report
// needs its mem_limit and C wants the object before the use.
static csp_rt_t state;

// The gap between the current stack frame and the bottom of the heap. Measured
// the same way csp_lpcopen.c measures it -- from a local's address -- and NOT
// through sbrk: newlib's sbrk needs a _sbrk this port does not supply, and
// supplying one would hand out memory the arena has already claimed.
static uint32_t raw_free(void)
{
    char top;
    return (uint32_t)(&top - &_heap_start);
}

uint32_t csp_system_ram_capacity(void)
{
#if defined(CSP_STM_RAM)
    return CSP_STM_RAM;
#else
    return (uint32_t)(&_stack_top - &_heap_start);
#endif
}

uint32_t csp_system_ram_avail(void)
{
    return raw_free();
}

// What CandySpeak is NOT responsible for: total used, minus the arena and the
// runtime struct. Reported separately so /memory can say whether a shortage is
// the program's doing or the platform's.
uint32_t csp_system_ram_used(void)
{
    uint32_t cap = csp_system_ram_capacity();
    uint32_t total, ours;

    if (cap == 0)
	return 0;
    total = cap - raw_free();
    ours  = (uint32_t)state.mem_limit + (uint32_t)sizeof(csp_rt_t);
    return (total > ours) ? (total - ours) : 0;
}

// ============================================================
// The device loops -- identical in shape to csp_lpcopen.c
// ============================================================

static int first_cycle = 1;

// Apply a configuration a rule asked for, and take the request down in BOTH
// slots -- the pair is copied on commit, so clearing one leaves a stale request
// in the other that spends a config call on some later cycle.
//
// THIS IS THE THIRD COPY. csp_lpcopen.c says, above its own, that a third port
// is when this belongs in a shared csp_io.c -- and it is right. It is not done
// here because the three csp_setup loops are NOT identical: the LPC one calls
// csp_lpc_pin_mux per device where this one has already muxed everything from
// the board table, so factoring the loops out means moving that call into
// csp_board_digital_config on a port that cannot be tested from here. Worth
// doing; worth doing on its own, with the LPC board in reach.
static void csp_apply_config(csp_rt_t* st, index_t ix, value_t* vptr, int analog)
{
    value_t* iptr;
    value_t* optr;

    csp_dio_slots(st, ix, &iptr, &optr);
    if (analog) {
	csp_board_analog_config(vptr);
	iptr->a.cfg = optr->a.cfg = 0;
    }
    else {
	csp_board_digital_config(vptr);
	iptr->d.cfg = optr->d.cfg = 0;
    }
}

void csp_setup(csp_rt_t* st)
{
    int i;

    csp_board_setup(st);
    csp_can_init(st);

    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);
	int j = INDEX(ix);
	value_t* vptr = csp_dio_slot(st, ix, DOUT);
	switch (decl(st,j,type)) {
	case DECL_DIGITAL:
	    csp_board_digital_config(vptr);
	    break;
	case DECL_ANALOG:
	    csp_board_analog_config(vptr);
	    break;
	default:
	    break;
	}
    }
    csp_ctx_reset(st);
}

void csp_input(csp_rt_t* st)
{
    int i;

    csp_board_start_input(st);
    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);
	int di = INDEX(ix);
	value_t* vptr;
	switch (decl(st,di,type)) {
	case DECL_DIGITAL:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->d.cfg)
		csp_apply_config(st, ix, vptr, 0);
	    if (vptr->d.dir & DIR_IN)
		csp_board_digital_input(st, ix, vptr);
	    break;
	case DECL_ANALOG:
	    vptr = csp_dio_slot(st, ix, DOUT);
	    if (vptr->a.cfg)
		csp_apply_config(st, ix, vptr, 1);
	    if (vptr->a.dir & DIR_IN)
		csp_board_analog_input(st, ix, vptr);
	    break;
	default:
	    break;
	}
    }
    csp_ctx_reset(st);
    csp_can_input(st);
    csp_buf_input(st);   // i2c/spi collections and datagrams
    csp_input_timer(st);
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {
	csp_board_start_output(st);
	for (i = 0; i < st->nio; ++i) {
	    index_t ix = csp_io_at(st, i);
	    int di = INDEX(ix);
	    value_t* vptr;
	    switch (decl(st,di,type)) {
	    case DECL_DIGITAL:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->d.cfg)
		    csp_apply_config(st, ix, vptr, 0);
		if (vptr->d.dir & DIR_OUT)
		    csp_board_digital_output(st, vptr);
		break;
	    case DECL_ANALOG:
		vptr = csp_dio_slot(st, ix, DOUT);
		if (vptr->a.cfg)
		    csp_apply_config(st, ix, vptr, 1);
		if (vptr->a.dir & DIR_OUT)
		    csp_board_analog_output(st, di, vptr);
		break;
	    default:
		break;
	    }
	}
	csp_ctx_reset(st);
	csp_can_output(st);
	csp_buf_output(st);  // i2c/spi starts and datagrams
	csp_board_stop_output(st);
    }
    csp_output_timer(st);
}

// ============================================================
// CAN -- STUBS
// ============================================================
//
// The F405 has two bxCAN controllers and no board here wires one up yet. These
// are the same no-bus stubs csp_lpcopen.c compiles when a board names no
// {can,...}: init succeeds and recv always says "nothing", so a program with a
// #buffer runs and stays quiet rather than failing to link.
//
// A real backend is a filter bank, three transmit mailboxes and two receive
// FIFOs -- the shape can_212x.c has, in ST's spelling.

int csp_can_init(csp_rt_t* st) { (void)st; return 0; }

int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    (void)st; (void)id; (void)data; (void)len;
    return 0;
}

int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st; (void)id; (void)data; (void)len;
    return 0;
}

// ============================================================
// Persistent store
// ============================================================
//
// The F405 has no EEPROM, so /save writes into the `store` region of the flash
// map -- one sector, erased once when the write opens and then filled forwards.
//
// NO STAGING BUFFER, and that is why this works at all: the store sector on
// this part is 128K and the whole of RAM is 128K. Flash here takes word writes
// at any offset in an erased sector, so a byte cursor moving forwards is the
// entire mechanism. An LPC has to stage a 512-byte block because IAP writes
// blocks; this does not.

#if defined(CSP_HAVE_FLASH) && !defined(CSP_NO_EEPROM)

#include "csp_flash.h"

// By KIND, not by name: csp_region_find takes a name because that is what
// /upgrade's argument is, and the store is not something a person types. A
// board that spells its settings region something other than "store" still
// works, which a name lookup would not.
static const csp_region_t* store_region(void)
{
    const csp_device_t* d = csp_device();
    uint8_t i;

    for (i = 0; i < d->nregion; i++)
	if (d->region[i].kind == CSP_REG_STORE)
	    return &d->region[i];
    return NULL;
}

static uint32_t store_off;        // byte cursor within the region
static uint32_t store_base;       // the region's offset from the flash base
static uint32_t store_len;
static int      store_mode;       // 0 closed, 1 reading, 2 writing

const char* csp_eeprom_name(void)
{
    static const char nm[] = "FLASH store";
    return nm;
}

uint32_t csp_eeprom_capacity(void)
{
    const csp_region_t* r = store_region();
    return r ? csp_region_size(csp_device(), r) : 0;
}

int csp_eeprom_open_read(void)
{
    const csp_region_t* r = store_region();

    if (r == NULL)
	return -1;
    store_base = csp_region_offset(csp_device(), r);
    store_len  = csp_region_size(csp_device(), r);
    store_off  = 0;
    store_mode = 1;
    return 0;
}

int csp_eeprom_open_write(void)
{
    const csp_region_t* r = store_region();

    if (r == NULL)
	return -1;
    store_base = csp_region_offset(csp_device(), r);
    store_len  = csp_region_size(csp_device(), r);
    store_off  = 0;
    store_mode = 2;
    // ERASE THE WHOLE REGION HERE, once. Flash only goes 1 -> 0, so a save over
    // a previous one has to start from erased -- and doing it at open rather
    // than per write means one erase per /save, which is what the part's
    // endurance budget is written for.
    if (csp_flash_erase(r->first, r->last) < 0) {
	store_mode = 0;
	return -1;
    }
    return 0;
}

void csp_eeprom_close(void) { store_mode = 0; }

int csp_eeprom_read(void* buf, size_t len)
{
    if (store_mode != 1)
	return -1;
    if (store_off + len > store_len)
	return -1;
    if (csp_flash_read(store_base + store_off, buf, (uint32_t)len) < 0)
	return -1;
    store_off += (uint32_t)len;
    return 0;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    if (store_mode != 2)
	return -1;
    // Refuse to run off the end rather than wrap: a program too big to persist
    // would otherwise half-save silently.
    if (store_off + len > store_len)
	return -1;
    if (csp_flash_write(store_base + store_off, buf, (uint32_t)len) < 0)
	return -1;
    store_off += (uint32_t)len;
    return 0;
}

#else

const char* csp_eeprom_name(void)   { static const char nm[] = "none"; return nm; }
uint32_t csp_eeprom_capacity(void)  { return 0; }
int  csp_eeprom_open_read(void)     { return -1; }
int  csp_eeprom_open_write(void)    { return -1; }
void csp_eeprom_close(void)         { }
int  csp_eeprom_read(void* b, size_t n)        { (void)b; (void)n; return -1; }
int  csp_eeprom_write(const void* b, size_t n) { (void)b; (void)n; return -1; }

#endif

// ============================================================
// main
// ============================================================

static void stm_setup(void)
{
    stm_tick_init();
    stm_uart_init();
    serial_output = 1;

    csp_board_init();

#if !defined(CSP_EXEC_ONLY)
    // The clock is reported from what sysinit COMPUTED, and the PLL is checked
    // against what it actually selected -- so a crystal that never started
    // shows up here as a number instead of as a mystery further along.
    csp_print_lit("boot: clk "); csp_print_uint(csp_stm_hclk);
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
	csp_print_lit(" (PLL DID NOT LOCK -- running on HSI)");
    csp_print_lit(", RAM "); csp_print_uint(csp_system_ram_capacity());
    csp_print_lit(", free ");  csp_print_uint(csp_system_ram_avail());
    csp_print_lit(", struct "); csp_print_uint((uint32_t)sizeof(csp_rt_t));
    csp_println();
#endif

    if (csp_rt_init(&state, REACTIVE_DEFAULT, CSP_CSTATE) < 0) {
	csp_print_line("FATAL: csp_rt_init failed (out of memory)");
	return;
    }

    // WHICH image, before one is loaded. sys.Boot lives in the settings store,
    // so the store is read on its own first.
#if !defined(CSP_NO_EEPROM)
    csp_eeprom_peek(&state);
    csp_boot_pick(&state);
#endif
    csp_load_rom(&state);

#if !defined(CSP_NO_EEPROM)
    if (csp_eeprom_load(&state) == 0) {
#if !defined(CSP_EXEC_ONLY)
	csp_print_line("Loaded from store");
#endif
    }
    else {
	// "no saved state" is the normal case at boot, not an error to carry
	// forward into the first cycle.
	csp_clr_error(&state);
#if !defined(CSP_EXEC_ONLY)
	csp_print_line("No saved state, running ROM");
#endif
    }
#endif

    // Lay out the whole program: reactive graph + leaf/device setup. MUST be
    // csp_rebuild and not a bare start -- rebuild resets the middle bump
    // allocator every derived table is carved from, and without it mid_end
    // stays 0, every table allocation fails, and the first cycle faults on null
    // view/heap pointers.
    if (csp_rebuild(&state) < 0)
	csp_print_line("setup failed: out of memory");

    csp_setup(&state);
}

static void stm_loop(void)
{
    index_t x;

    if (state.mem == NULL || state.mem_limit == 0)
	return;

#if !defined(CSP_EXEC_ONLY)
    if (!state.line.ready)
	csp_line_prompt(&state.line);
    while (stm_uart_available() && csp_line_space(&state.line))
	csp_line_input(&state.line, (char)stm_uart_read());
    if (state.line.ready) {
	csp_process_line(&state, state.line.buf);
	csp_line_done(&state.line);
    }
#endif

    if (!state.started)
	return;

    if (first_cycle) {
	state.cycle = 1;
	first_cycle = 0;
    }
    else if (!state.paused)
	state.cycle++;

    if (state.paused)
	return;

    csp_input(&state);
    x = state.live ? BAD_INDEX : csp_cycle(&state);
    (void)x;
    csp_commit(&state);
    csp_output(&state);

    // A running timer sets wait_ms, but that must NOT gate the whole loop:
    // continuous inputs have to keep sampling at a steady rate. Never sleep
    // longer than SAMPLE_MS in one pass.
    {
	uint32_t remaining = (state.es.wait_ms != NOTIMEOUT)
			   ? state.es.wait_ms : SAMPLE_MS;
	if (remaining > SAMPLE_MS)
	    remaining = SAMPLE_MS;
	while ((remaining > 0) && !state.line.ready) {
	    uint32_t chunk = (remaining < 10) ? remaining : 10;
	    csp_delay_ms(chunk);
	    remaining -= chunk;
#if !defined(CSP_EXEC_ONLY)
	    while (stm_uart_available() && csp_line_space(&state.line))
		csp_line_input(&state.line, (char)stm_uart_read());
#endif
	}
    }
}

int main(void)
{
    stm_setup();
    for (;;)
	stm_loop();
    return 0;
}
