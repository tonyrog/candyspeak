// Clock, flash latency and FPU for the STM32F405. The counterpart to
// sysinit_175x.c and sysinit_212x.c, and by far the most dangerous file in this
// port: a part with the PLL set up wrong does not fail, it runs at the wrong
// speed or does not come out of reset at all, and every symptom above it is a
// timing symptom.
//
// EVERYTHING IS DERIVED from the board's crystal and target clock rather than
// written as magic numbers. The board says {xtal, 8000000} and {core,
// 168000000}; the dividers follow, and the compile-time checks below refuse the
// combinations the part cannot do. A number typed twice is a number that drifts;
// a number typed once and checked is one that cannot.

// stdint FIRST: csp_config.h declares csp_gate_mask_t and does not include it
// itself -- it is written to be included by a sketch that has already pulled in
// Arduino.h. Including it cold is what a bare-metal driver does.
#include <stdint.h>
#include "csp_config.h"
#include "stm32f405xx.h"

#ifndef CSP_XTAL_HZ
#error "sysinit_f405: the board must state {xtal, ...}"
#endif
#ifndef CSP_CORE_HZ
#error "sysinit_f405: the board must state {core, ...}"
#endif

// The PLL runs HSE / M -> x N -> / P.
//
// VCO INPUT IS 1 MHz. The reference manual allows 1..2 MHz and recommends 2 to
// keep jitter down, but 1 makes M the crystal in MHz for every crystal, and
// this port has one board. Revisit with the second.
#define PLL_VCO_IN   1000000u
#define PLL_M        (CSP_XTAL_HZ / PLL_VCO_IN)
#define PLL_P        2u
#define PLL_N        ((CSP_CORE_HZ * PLL_P) / PLL_VCO_IN)
#define PLL_VCO      (PLL_VCO_IN * PLL_N)
// 48 MHz for USB and the SDIO clock. Not used yet; wrong here costs nothing
// today and costs an evening the day USB is turned on.
#define PLL_Q        (PLL_VCO / 48000000u)

#if (CSP_XTAL_HZ % PLL_VCO_IN) != 0
#error "sysinit_f405: crystal is not a whole number of MHz"
#endif
#if (PLL_N < 50) || (PLL_N > 432)
#error "sysinit_f405: PLLN out of range -- core clock unreachable from this crystal"
#endif
#if (PLL_VCO < 100000000u) || (PLL_VCO > 432000000u)
#error "sysinit_f405: PLL VCO out of the 100..432 MHz the part allows"
#endif
#if CSP_CORE_HZ > 168000000u
#error "sysinit_f405: 168 MHz is the F405's ceiling"
#endif

// FLASH WAIT STATES, at 2.7..3.6V: one more every 30 MHz. Too few is not a
// slow part, it is a part that reads garbage from flash -- so this rounds UP
// and the part is at worst one state slower than it had to be.
#define FLASH_WS     (CSP_CORE_HZ / 30000000u)
#if FLASH_WS > 7
#error "sysinit_f405: flash latency out of range"
#endif

// APB1 tops out at 42 MHz and APB2 at 84. Both round the divider UP to a power
// of two, because a bus overclocked by one step is a peripheral that works on
// the bench and fails warm.
#if   CSP_CORE_HZ <= 42000000u
#define APB1_DIV 1u
#define APB1_BITS RCC_CFGR_PPRE1_DIV1
#elif CSP_CORE_HZ <= 84000000u
#define APB1_DIV 2u
#define APB1_BITS RCC_CFGR_PPRE1_DIV2
#elif CSP_CORE_HZ <= 168000000u
#define APB1_DIV 4u
#define APB1_BITS RCC_CFGR_PPRE1_DIV4
#else
#define APB1_DIV 8u
#define APB1_BITS RCC_CFGR_PPRE1_DIV8
#endif

#if   CSP_CORE_HZ <= 84000000u
#define APB2_DIV 1u
#define APB2_BITS RCC_CFGR_PPRE2_DIV1
#else
#define APB2_DIV 2u
#define APB2_BITS RCC_CFGR_PPRE2_DIV2
#endif

// What the rest of the port asks for. A timer on APB1 is clocked at twice the
// bus when the bus is divided -- that is the "timer clock doubler", and missing
// it makes every PWM period half of what was asked for.
const uint32_t csp_stm_hclk  = CSP_CORE_HZ;
const uint32_t csp_stm_pclk1 = CSP_CORE_HZ / APB1_DIV;
const uint32_t csp_stm_pclk2 = CSP_CORE_HZ / APB2_DIV;
const uint32_t csp_stm_tim1  = (APB1_DIV == 1u) ? CSP_CORE_HZ
						: (CSP_CORE_HZ / APB1_DIV) * 2u;
const uint32_t csp_stm_tim2  = (APB2_DIV == 1u) ? CSP_CORE_HZ
						: (CSP_CORE_HZ / APB2_DIV) * 2u;

// How long to wait for a clock to come up before giving up on it. Cycles, not
// milliseconds -- there is no time base yet, this IS the code that makes one.
#define SPIN_LIMIT 0x00300000u

void SystemInit(void);

void SystemInit(void)
{
    uint32_t spin;

    // THE FPU FIRST. -mfloat-abi=softfp still emits VFP instructions, and one
    // reached before CP10/CP11 are enabled is a UsageFault at a place that has
    // nothing to do with floating point. CandySpeak's own arithmetic is Q16.16
    // and never needs this -- the library might.
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));

    // Reset to a known clock before touching anything: HSI on, and the CFGR
    // cleared. A warm reset can arrive here with the PLL already running, and
    // reconfiguring a running PLL is undefined.
    RCC->CR |= RCC_CR_HSION;
    RCC->CFGR = 0;
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLLON);
    RCC->PLLCFGR = 0x24003010;          // the reset value
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CIR = 0;                       // no clock interrupts

    // The crystal.
    RCC->CR |= RCC_CR_HSEON;
    for (spin = 0; !(RCC->CR & RCC_CR_HSERDY); spin++)
	if (spin > SPIN_LIMIT)
	    return;                     // no crystal: stay on HSI at 16 MHz
					// rather than hang. Slow and talking
					// beats fast and dead.

    // VOLTAGE SCALE 1, and it has to be set BEFORE the PLL is switched to
    // anything above 144 MHz. The regulator needs the higher setting to supply
    // the core at that speed; get the order wrong and the part browns out under
    // load rather than at boot.
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;                 // the write is posted; read it back
    PWR->CR |= PWR_CR_VOS;

    // Bus dividers BEFORE the switch, so nothing is ever overclocked -- not
    // even for the handful of cycles between the switch and the next write.
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | APB1_BITS | APB2_BITS;

    RCC->PLLCFGR = (PLL_M << RCC_PLLCFGR_PLLM_Pos) |
		   (PLL_N << RCC_PLLCFGR_PLLN_Pos) |
		   (((PLL_P / 2u) - 1u) << RCC_PLLCFGR_PLLP_Pos) |
		   (PLL_Q << RCC_PLLCFGR_PLLQ_Pos) |
		   RCC_PLLCFGR_PLLSRC_HSE;

    RCC->CR |= RCC_CR_PLLON;
    for (spin = 0; !(RCC->CR & RCC_CR_PLLRDY); spin++)
	if (spin > SPIN_LIMIT)
	    return;

    // FLASH LATENCY BEFORE THE SWITCH, always. Raising the clock over flash
    // that is still set for the old one is the one ordering mistake here that
    // cannot be recovered from: the very next instruction fetch is wrong.
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_WS)
	;

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
	;
}
