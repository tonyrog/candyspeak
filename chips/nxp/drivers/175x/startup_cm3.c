// Reset entry for Cortex-M3. The counterpart to startup_212x.S, against the
// SAME linker symbols -- _data_load, _data_start, _data_end, _bss_start,
// _bss_end, _stack_top -- so one generated script serves both architectures
// and a person reading either startup recognises the other.
//
// NOT cr_startup_lpc175x_6x.c. That one is Code Red / MCUXpresso and walks
// __data_section_table: a list of (load, run, size) triples the LPCXpresso
// script emits, which lets it initialise several RAM banks. It is a fine
// design and it is a CONVENTION -- link it against any other script and you
// get "undefined reference to __data_section_table", which is exactly what
// happened here. This is the plain version.
//
// C and not assembly, because on Cortex-M it can be: the core loads SP and PC
// from the vector table itself before any instruction runs, so there is no
// window where the stack is undefined. On ARM7 the reset vector is an
// instruction and each mode's stack has to be set by hand -- hence the .S over
// there.

#include <stdint.h>

extern uint32_t _data_load, _data_start, _data_end;
extern uint32_t _bss_start, _bss_end;
extern uint32_t _stack_top;

extern int main(void);
void SystemInit(void);

void ResetISR(void);
static void fault_loop(void);

// The default for every exception this build does not name. A weak alias means
// a driver can define, say, CAN_IRQHandler and have it replace the entry
// without anything here knowing the name.
void NMI_Handler(void)        __attribute__((weak, alias("fault_loop_sym")));
void HardFault_Handler(void)  __attribute__((weak, alias("fault_loop_sym")));
void MemManage_Handler(void)  __attribute__((weak, alias("fault_loop_sym")));
void BusFault_Handler(void)   __attribute__((weak, alias("fault_loop_sym")));
void UsageFault_Handler(void) __attribute__((weak, alias("fault_loop_sym")));
void SVC_Handler(void)        __attribute__((weak, alias("fault_loop_sym")));
void DebugMon_Handler(void)   __attribute__((weak, alias("fault_loop_sym")));
void PendSV_Handler(void)     __attribute__((weak, alias("fault_loop_sym")));
void SysTick_Handler(void)    __attribute__((weak, alias("fault_loop_sym")));

// Peripheral interrupts, in the LPC17xx order. Every one weak and aliased, so
// naming a handler anywhere in the build is all it takes to hook it.
#define IRQ(name) void name(void) __attribute__((weak, alias("fault_loop_sym")))
IRQ(WDT_IRQHandler);     IRQ(TIMER0_IRQHandler);  IRQ(TIMER1_IRQHandler);
IRQ(TIMER2_IRQHandler);  IRQ(TIMER3_IRQHandler);  IRQ(UART0_IRQHandler);
IRQ(UART1_IRQHandler);   IRQ(UART2_IRQHandler);   IRQ(UART3_IRQHandler);
IRQ(PWM1_IRQHandler);    IRQ(I2C0_IRQHandler);    IRQ(I2C1_IRQHandler);
IRQ(I2C2_IRQHandler);    IRQ(SPI_IRQHandler);     IRQ(SSP0_IRQHandler);
IRQ(SSP1_IRQHandler);    IRQ(PLL0_IRQHandler);    IRQ(RTC_IRQHandler);
IRQ(EINT0_IRQHandler);   IRQ(EINT1_IRQHandler);   IRQ(EINT2_IRQHandler);
IRQ(EINT3_IRQHandler);   IRQ(ADC_IRQHandler);     IRQ(BOD_IRQHandler);
IRQ(USB_IRQHandler);     IRQ(CAN_IRQHandler);     IRQ(DMA_IRQHandler);
IRQ(I2S_IRQHandler);     IRQ(ENET_IRQHandler);    IRQ(RIT_IRQHandler);
IRQ(MCPWM_IRQHandler);   IRQ(QEI_IRQHandler);     IRQ(PLL1_IRQHandler);
IRQ(USBActivity_IRQHandler); IRQ(CANActivity_IRQHandler);
#undef IRQ

void fault_loop_sym(void) __attribute__((weak));
void fault_loop_sym(void) { for (;;) ; }

// A POINTER ARRAY, not branch instructions: the core reads entry 0 into SP and
// entry 1 into PC on reset, which is why the initial stack pointer is data
// here and an ARM7 has to load it in code.
__attribute__((used, section(".isr_vector")))
void (* const g_vectors[])(void) = {
    (void (*)(void))&_stack_top,    // 0  initial SP
    ResetISR,                       // 1  reset
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0,                        // reserved
    // 7 is the CHECKSUM word on this family too: the ISP boot loader adds the
    // first eight vectors and enters ISP if they do not sum to zero. Left 0
    // here and patched into the image -- see csp_lpc2000_checksum, which is
    // the same arithmetic on both families.
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,

    WDT_IRQHandler,    TIMER0_IRQHandler, TIMER1_IRQHandler, TIMER2_IRQHandler,
    TIMER3_IRQHandler, UART0_IRQHandler,  UART1_IRQHandler,  UART2_IRQHandler,
    UART3_IRQHandler,  PWM1_IRQHandler,   I2C0_IRQHandler,   I2C1_IRQHandler,
    I2C2_IRQHandler,   SPI_IRQHandler,    SSP0_IRQHandler,   SSP1_IRQHandler,
    PLL0_IRQHandler,   RTC_IRQHandler,    EINT0_IRQHandler,  EINT1_IRQHandler,
    EINT2_IRQHandler,  EINT3_IRQHandler,  ADC_IRQHandler,    BOD_IRQHandler,
    USB_IRQHandler,    CAN_IRQHandler,    DMA_IRQHandler,    I2S_IRQHandler,
    ENET_IRQHandler,   RIT_IRQHandler,    MCPWM_IRQHandler,  QEI_IRQHandler,
    PLL1_IRQHandler,   USBActivity_IRQHandler, CANActivity_IRQHandler
};

static void fault_loop(void) { for (;;) ; }

void ResetISR(void)
{
    uint32_t *src, *dst;

    // .data from flash to RAM. Word at a time; the linker aligned both ends.
    src = &_data_load;
    for (dst = &_data_start; dst < &_data_end; )
	*dst++ = *src++;

    // .bss to zero. C promises it; nothing else does it.
    for (dst = &_bss_start; dst < &_bss_end; )
	*dst++ = 0;

    // Clock, and whatever else the chip layer needs, before any C that might
    // depend on either.
    SystemInit();

    main();

    // main returned. There is nowhere to go, and falling off the end of the
    // reset handler executes whatever follows it.
    fault_loop();
}
