#ifndef __VIC_212X_H__
#define __VIC_212X_H__

// The Vectored Interrupt Controller, in the CMSIS spelling.
//
// NVIC_EnableIRQ and friends, because that is what a Cortex-M port calls and
// this file exists so the platform code above needs no #if. What the VIC has
// that an NVIC does not is that a slot holds the HANDLER ADDRESS -- a Cortex-M
// finds it in the vector table by name -- so there is one extra call,
// Chip_VIC_SetHandler, and no way to hide it.
//
// Sixteen slots, and the slot NUMBER is the priority: slot 0 wins. That is the
// whole scheduling policy, which is why this is 60 lines and not the 460 of
// nxp_files/2129/vic_lpc.c -- that one allocates slots by requested priority
// and chains several callbacks per source, neither of which anything here asks
// for.

#include <stdint.h>

// Interrupt numbers, CMSIS names over the LPC2000 assignments. The values are
// from nxp_files/2129/lpc21xx_irq.h; the names are what a 17xx port would use,
// so `NVIC_EnableIRQ(TIMER0_IRQn)` compiles on both.
typedef enum {
    WDT_IRQn        = 0,
    DBGRX_IRQn      = 2,
    DBGTX_IRQn      = 3,
    TIMER0_IRQn     = 4,
    TIMER1_IRQn     = 5,
    UART0_IRQn      = 6,
    UART1_IRQn      = 7,
    PWM1_IRQn       = 8,
    I2C0_IRQn       = 9,
    SSP0_IRQn       = 10,
    SSP1_IRQn       = 11,
    PLL_IRQn        = 12,
    RTC_IRQn        = 13,
    EINT0_IRQn      = 14,
    EINT1_IRQn      = 15,
    EINT2_IRQn      = 16,
    EINT3_IRQn      = 17,
    ADC_IRQn        = 18,
    CAN_IRQn        = 19,
    CAN1TX_IRQn     = 20,
    CAN2TX_IRQn     = 21,
    CANFULL_IRQn    = 25,
    CAN1RX_IRQn     = 26,
    CAN2RX_IRQn     = 27
} IRQn_Type;

typedef void (*vic_handler_t)(void);

void Chip_VIC_Init(void);

// The ARM7 extra: point the source at a plain C function. It runs from the
// trampoline in startup, so it needs no interrupt attribute and no special
// return -- the same shape a Cortex-M handler has.
void Chip_VIC_SetHandler(IRQn_Type irq, vic_handler_t fn);

void NVIC_EnableIRQ(IRQn_Type irq);
void NVIC_DisableIRQ(IRQn_Type irq);
void NVIC_SetPriority(IRQn_Type irq, uint32_t prio);

// What the startup trampoline calls: dispatch whatever the VIC selected, then
// tell it we are done. Exported so the assembly has one symbol to branch to.
void Chip_VIC_Dispatch(void);

// Unmask IRQ (and FIQ) in the CPSR. A Cortex-M comes out of reset with
// interrupts ENABLED, so a port written against one never calls anything like
// this -- and an ARM7 that starts masked then sits forever in the first
// __WFI(), waiting for a tick that cannot arrive.
void EnableIRQ(void);

uint32_t DisableIRQ(void);          // returns the previous CPSR
void     RestoreIRQ(uint32_t cpsr);

#endif
