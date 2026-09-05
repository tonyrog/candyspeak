// Reset entry for the STM32F405. The counterpart to startup_cm3.c (LPC17xx)
// and startup_212x.S (LPC2000), against the SAME linker symbols -- _data_load,
// _data_start, _data_end, _bss_start, _bss_end, _stack_top -- so one generated
// script serves every architecture here and a person reading one startup
// recognises the others.
//
// NOT cmsis-device-f4/Source/Templates/gcc/startup_stm32f405xx.s. That one is
// assembly against ST's own symbol names (_sdata, _edata, _sbss, _estack) and
// their linker script's conventions. Linking it against the script this tree
// generates gives undefined references, which is a confusing way to discover a
// naming difference. This is the same thing in C, against our names.
//
// C and not assembly, because on Cortex-M it can be: the core loads SP and PC
// from the vector table itself before any instruction runs, so there is no
// window where the stack is undefined.

#include <stdint.h>

extern uint32_t _data_load, _data_start, _data_end;
extern uint32_t _bss_start, _bss_end;
extern uint32_t _stack_top;

extern int main(void);
void SystemInit(void);

void Reset_Handler(void);
static void fault_loop(void);

// The default for every exception this build does not name. A weak alias means
// a driver can define, say, CAN1_RX0_IRQHandler and have it replace the entry
// without anything here knowing the name.
void fault_loop_sym(void) __attribute__((weak));
void fault_loop_sym(void) { for (;;) ; }

#define IRQ(name) void name(void) __attribute__((weak, alias("fault_loop_sym")))

IRQ(NMI_Handler);        IRQ(HardFault_Handler);  IRQ(MemManage_Handler);
IRQ(BusFault_Handler);   IRQ(UsageFault_Handler); IRQ(SVC_Handler);
IRQ(DebugMon_Handler);   IRQ(PendSV_Handler);     IRQ(SysTick_Handler);

// The 82 peripheral interrupts, in the F405's order. The order is the whole
// content of this list -- a handler in the wrong slot is a handler that runs
// for the wrong source, and nothing says so.
IRQ(WWDG_IRQHandler);            IRQ(PVD_IRQHandler);
IRQ(TAMP_STAMP_IRQHandler);      IRQ(RTC_WKUP_IRQHandler);
IRQ(FLASH_IRQHandler);           IRQ(RCC_IRQHandler);
IRQ(EXTI0_IRQHandler);           IRQ(EXTI1_IRQHandler);
IRQ(EXTI2_IRQHandler);           IRQ(EXTI3_IRQHandler);
IRQ(EXTI4_IRQHandler);           IRQ(DMA1_Stream0_IRQHandler);
IRQ(DMA1_Stream1_IRQHandler);    IRQ(DMA1_Stream2_IRQHandler);
IRQ(DMA1_Stream3_IRQHandler);    IRQ(DMA1_Stream4_IRQHandler);
IRQ(DMA1_Stream5_IRQHandler);    IRQ(DMA1_Stream6_IRQHandler);
IRQ(ADC_IRQHandler);             IRQ(CAN1_TX_IRQHandler);
IRQ(CAN1_RX0_IRQHandler);        IRQ(CAN1_RX1_IRQHandler);
IRQ(CAN1_SCE_IRQHandler);        IRQ(EXTI9_5_IRQHandler);
IRQ(TIM1_BRK_TIM9_IRQHandler);   IRQ(TIM1_UP_TIM10_IRQHandler);
IRQ(TIM1_TRG_COM_TIM11_IRQHandler); IRQ(TIM1_CC_IRQHandler);
IRQ(TIM2_IRQHandler);            IRQ(TIM3_IRQHandler);
IRQ(TIM4_IRQHandler);            IRQ(I2C1_EV_IRQHandler);
IRQ(I2C1_ER_IRQHandler);         IRQ(I2C2_EV_IRQHandler);
IRQ(I2C2_ER_IRQHandler);         IRQ(SPI1_IRQHandler);
IRQ(SPI2_IRQHandler);            IRQ(USART1_IRQHandler);
IRQ(USART2_IRQHandler);          IRQ(USART3_IRQHandler);
IRQ(EXTI15_10_IRQHandler);       IRQ(RTC_Alarm_IRQHandler);
IRQ(OTG_FS_WKUP_IRQHandler);     IRQ(TIM8_BRK_TIM12_IRQHandler);
IRQ(TIM8_UP_TIM13_IRQHandler);   IRQ(TIM8_TRG_COM_TIM14_IRQHandler);
IRQ(TIM8_CC_IRQHandler);         IRQ(DMA1_Stream7_IRQHandler);
IRQ(FSMC_IRQHandler);            IRQ(SDIO_IRQHandler);
IRQ(TIM5_IRQHandler);            IRQ(SPI3_IRQHandler);
IRQ(UART4_IRQHandler);           IRQ(UART5_IRQHandler);
IRQ(TIM6_DAC_IRQHandler);        IRQ(TIM7_IRQHandler);
IRQ(DMA2_Stream0_IRQHandler);    IRQ(DMA2_Stream1_IRQHandler);
IRQ(DMA2_Stream2_IRQHandler);    IRQ(DMA2_Stream3_IRQHandler);
IRQ(DMA2_Stream4_IRQHandler);    IRQ(ETH_IRQHandler);
IRQ(ETH_WKUP_IRQHandler);        IRQ(CAN2_TX_IRQHandler);
IRQ(CAN2_RX0_IRQHandler);        IRQ(CAN2_RX1_IRQHandler);
IRQ(CAN2_SCE_IRQHandler);        IRQ(OTG_FS_IRQHandler);
IRQ(DMA2_Stream5_IRQHandler);    IRQ(DMA2_Stream6_IRQHandler);
IRQ(DMA2_Stream7_IRQHandler);    IRQ(USART6_IRQHandler);
IRQ(I2C3_EV_IRQHandler);         IRQ(I2C3_ER_IRQHandler);
IRQ(OTG_HS_EP1_OUT_IRQHandler);  IRQ(OTG_HS_EP1_IN_IRQHandler);
IRQ(OTG_HS_WKUP_IRQHandler);     IRQ(OTG_HS_IRQHandler);
IRQ(DCMI_IRQHandler);            IRQ(CRYP_IRQHandler);
IRQ(HASH_RNG_IRQHandler);        IRQ(FPU_IRQHandler);
#undef IRQ

// A POINTER ARRAY, not branch instructions: the core reads entry 0 into SP and
// entry 1 into PC on reset, which is why the initial stack pointer is data here
// and an ARM7 has to load it in code.
//
// NO CHECKSUM WORD. The LPC families put one at offset 0x1C and the ISP boot
// loader refuses to run an image whose first eight vectors do not sum to zero;
// ST's boot loader is selected by the BOOT pins and checks nothing. So
// `csp --checksum=` has nothing to do to an STM32 image -- see Makefile.board.
__attribute__((used, section(".isr_vector")))
void (* const g_vectors[])(void) = {
    (void (*)(void))&_stack_top,    // 0  initial SP
    Reset_Handler,                  // 1  reset
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,                     // reserved
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,

    WWDG_IRQHandler,            PVD_IRQHandler,
    TAMP_STAMP_IRQHandler,      RTC_WKUP_IRQHandler,
    FLASH_IRQHandler,           RCC_IRQHandler,
    EXTI0_IRQHandler,           EXTI1_IRQHandler,
    EXTI2_IRQHandler,           EXTI3_IRQHandler,
    EXTI4_IRQHandler,           DMA1_Stream0_IRQHandler,
    DMA1_Stream1_IRQHandler,    DMA1_Stream2_IRQHandler,
    DMA1_Stream3_IRQHandler,    DMA1_Stream4_IRQHandler,
    DMA1_Stream5_IRQHandler,    DMA1_Stream6_IRQHandler,
    ADC_IRQHandler,             CAN1_TX_IRQHandler,
    CAN1_RX0_IRQHandler,        CAN1_RX1_IRQHandler,
    CAN1_SCE_IRQHandler,        EXTI9_5_IRQHandler,
    TIM1_BRK_TIM9_IRQHandler,   TIM1_UP_TIM10_IRQHandler,
    TIM1_TRG_COM_TIM11_IRQHandler, TIM1_CC_IRQHandler,
    TIM2_IRQHandler,            TIM3_IRQHandler,
    TIM4_IRQHandler,            I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler,         I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler,         SPI1_IRQHandler,
    SPI2_IRQHandler,            USART1_IRQHandler,
    USART2_IRQHandler,          USART3_IRQHandler,
    EXTI15_10_IRQHandler,       RTC_Alarm_IRQHandler,
    OTG_FS_WKUP_IRQHandler,     TIM8_BRK_TIM12_IRQHandler,
    TIM8_UP_TIM13_IRQHandler,   TIM8_TRG_COM_TIM14_IRQHandler,
    TIM8_CC_IRQHandler,         DMA1_Stream7_IRQHandler,
    FSMC_IRQHandler,            SDIO_IRQHandler,
    TIM5_IRQHandler,            SPI3_IRQHandler,
    UART4_IRQHandler,           UART5_IRQHandler,
    TIM6_DAC_IRQHandler,        TIM7_IRQHandler,
    DMA2_Stream0_IRQHandler,    DMA2_Stream1_IRQHandler,
    DMA2_Stream2_IRQHandler,    DMA2_Stream3_IRQHandler,
    DMA2_Stream4_IRQHandler,    ETH_IRQHandler,
    ETH_WKUP_IRQHandler,        CAN2_TX_IRQHandler,
    CAN2_RX0_IRQHandler,        CAN2_RX1_IRQHandler,
    CAN2_SCE_IRQHandler,        OTG_FS_IRQHandler,
    DMA2_Stream5_IRQHandler,    DMA2_Stream6_IRQHandler,
    DMA2_Stream7_IRQHandler,    USART6_IRQHandler,
    I2C3_EV_IRQHandler,         I2C3_ER_IRQHandler,
    OTG_HS_EP1_OUT_IRQHandler,  OTG_HS_EP1_IN_IRQHandler,
    OTG_HS_WKUP_IRQHandler,     OTG_HS_IRQHandler,
    DCMI_IRQHandler,            CRYP_IRQHandler,
    HASH_RNG_IRQHandler,        FPU_IRQHandler
};

static void fault_loop(void) { for (;;) ; }

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    // .data from flash to RAM. Word at a time; the linker aligned both ends.
    src = &_data_load;
    for (dst = &_data_start; dst < &_data_end; )
	*dst++ = *src++;

    // .bss to zero. C promises it; nothing else does it.
    for (dst = &_bss_start; dst < &_bss_end; )
	*dst++ = 0;

    // Clock, FPU and flash latency, before any C that might depend on them.
    SystemInit();

    main();

    // main returned. There is nowhere to go, and falling off the end of the
    // reset handler executes whatever follows it.
    fault_loop();
}
