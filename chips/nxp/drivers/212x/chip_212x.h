#ifndef __CHIP_212X_H__
#define __CHIP_212X_H__

// LPC2000 peripherals in the LPCOpen IDIOM: register STRUCTS and Chip_* calls
// taking a peripheral pointer.
//
// WHY BOTHER, when LPCOpen does not and never will cover ARM7. Because
// csp_lpcopen.c already calls exactly these names -- Chip_UART_SendByte,
// Chip_GPIO_SetPinState, Chip_ADC_ReadValue -- so writing the 2129 drivers in
// this shape means ONE platform file serves both chips. The alternative was a
// second csp_lpc2000.c that says the same things differently.
//
// The old header (nxp_files/2129/lpc21xx.h) is flat `#define U0RBR (*(vu32*)
// 0xE000C000)` in the Keil style. That works, but a driver written against it
// can only ever address ONE uart: the register name IS the instance. A struct
// and a pointer is what lets Chip_UART_Init(LPC_UART1) exist at all.
//
// Addresses from that header and the LPC2129 user manual.

#include <stdint.h>

#ifndef __IO
#define __IO volatile
#define __I  volatile const
#define __O  volatile
#endif

// --- UART -------------------------------------------------------------------
// RBR/THR/DLL share an address, as do IER/DLM and IIR/FCR: which one you get
// depends on the divisor-latch bit in LCR. A union says that in the type rather
// than in a comment nobody reads.
// EIGHT-BIT REGISTERS, four bytes apart -- not 32-bit ones.
//
// This matters and it is not cosmetic. The LPC2000 UART registers are byte
// wide; the addresses are merely spaced by four. A 32-bit store to THR is a
// word write to a byte port, and on the VPB bridge it need not land -- in which
// case THRE stays set because nothing was ever written, the polling loop never
// blocks, every byte appears to go out, and the pin stays silent.
//
// Which is a fault with no symptom except silence: the code runs to completion,
// the status bits all read correctly, and nothing is transmitted.
//
// The header from the working driver for this board (nxp_files/2129/lpc21xx.h)
// declares every one of these `vu8` with explicit `_PAD[3]`. That is where this
// shape comes from.
typedef struct {
    union {
	__I  uint8_t RBR;           // DLAB=0: read  -- receive buffer
	__O  uint8_t THR;           // DLAB=0: write -- transmit holding
	__IO uint8_t DLL;           // DLAB=1: divisor low
    };
    uint8_t _pad0[3];
    union {
	__IO uint8_t DLM;           // DLAB=1: divisor high
	__IO uint8_t IER;           // DLAB=0: interrupt enable
    };
    uint8_t _pad1[3];
    union {
	__I  uint8_t IIR;           // read  -- interrupt id
	__O  uint8_t FCR;           // write -- fifo control
    };
    uint8_t _pad2[3];
    __IO uint8_t LCR;               // line control
    uint8_t _pad3[3];
    __IO uint8_t MCR;               // modem control (UART1 only)
    uint8_t _pad4[3];
    __I  uint8_t LSR;               // line status
    uint8_t _pad5[3];
    __I  uint8_t MSR;               // modem status (UART1 only)
    uint8_t _pad6[3];
    __IO uint8_t SCR;               // scratch
    uint8_t _pad7[3];
} LPC_USART_T;

#define LPC_UART0 ((LPC_USART_T *) 0xE000C000)
#define LPC_UART1 ((LPC_USART_T *) 0xE0010000)

#define UART_LSR_RDR   (1u << 0)    // receiver data ready
#define UART_LSR_THRE  (1u << 5)    // transmit holding register empty
#define UART_LSR_TEMT  (1u << 6)    // transmitter empty

#define UART_LCR_DLAB  (1u << 7)
#define UART_FCR_FIFO_EN  (1u << 0)
#define UART_FCR_RX_RS    (1u << 1)
#define UART_FCR_TX_RS    (1u << 2)

// --- GPIO -------------------------------------------------------------------
// The SLOW port at 0xE0028000. LPC2129 has no fast GPIO -- that arrived with
// the 213x -- so there is one block and it is this one. Ports are 0x10 apart.
typedef struct {
    __IO uint32_t PIN;              // current state (read), write = set all
    __O  uint32_t SET;              // write 1 to drive high
    __IO uint32_t DIR;              // 1 = output
    __O  uint32_t CLR;              // write 1 to drive low
} LPC_GPIO_T;

#define LPC_GPIO0 ((LPC_GPIO_T *) 0xE0028000)
#define LPC_GPIO1 ((LPC_GPIO_T *) 0xE0028010)
#define LPC_GPIO_PORT(p) ((LPC_GPIO_T *)(0xE0028000 + (p) * 0x10))

// --- pin function select ----------------------------------------------------
// Two bits per pin. PINSEL0 covers P0.0..P0.15, PINSEL1 P0.16..P0.31 and
// PINSEL2 is P1 plus the debug/trace enables -- which is why P1 is NOT two bits
// per pin and gets its own call.
typedef struct {
    __IO uint32_t SEL0;
    __IO uint32_t SEL1;
    __IO uint32_t _res[3];
    __IO uint32_t SEL2;             // 0xE002C014
} LPC_PINSEL_T;

#define LPC_PINSEL ((LPC_PINSEL_T *) 0xE002C000)

// --- system control ---------------------------------------------------------
// The PLL, the memory accelerator and the peripheral bus divider. Scattered
// through 0xE01FCxxx rather than contiguous, so this is offsets from the base
// and not one tidy struct.
#define LPC_SCB_BASE 0xE01FC000

#define LPC_MAMCR  (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x000))
#define LPC_MAMTIM (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x004))
#define LPC_PLLCON (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x080))
#define LPC_PLLCFG (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x084))
#define LPC_PLLSTAT (*(__I uint16_t *)(LPC_SCB_BASE + 0x088))
#define LPC_PLLFEED (*(__O uint8_t  *)(LPC_SCB_BASE + 0x08C))
#define LPC_VPBDIV (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x100))

// MEMMAP: which copy of the exception vectors the core actually reads. The
// bottom 64 bytes are REMAPPED, so a table linked at 0x00000000 is not
// necessarily the one that gets used -- see Chip_SystemInit.
#define LPC_MEMMAP (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x040))
#define MEMMAP_BOOT_BLOCK  0
#define MEMMAP_USER_FLASH  1
#define MEMMAP_USER_RAM    2
// Power control. Bit 0 (IDL) stops the CORE clock and leaves the peripheral
// clocks running, so any interrupt wakes it -- this family's answer to WFI.
// Bit 1 (PD) is real power-down and needs a good deal more care.
#define LPC_PCON   (*(__IO uint8_t  *)(LPC_SCB_BASE + 0x0C0))
#define PCON_IDL   (1u << 0)
#define PCON_PD    (1u << 1)

// Peripheral power. Bit per block; several come up SET, so a board that wants
// anything off has to write the whole word. See gen_chips.erl --board.
#define LPC_PCONP  (*(__IO uint32_t *)(LPC_SCB_BASE + 0x0C4))

#define PLLCON_PLLE  (1u << 0)      // enable
#define PLLCON_PLLC  (1u << 1)      // connect
#define PLLSTAT_PLLE (1u << 8)
#define PLLSTAT_PLLC (1u << 9)
#define PLLSTAT_LOCK (1u << 10)

#define MAMCR_OFF     0
#define MAMCR_PARTIAL 1
#define MAMCR_FULL    2

// --- timer ------------------------------------------------------------------
typedef struct {
    __IO uint32_t IR;               // interrupt register (write 1 to clear)
    __IO uint32_t TCR;              // 1 = enable, 2 = reset
    __IO uint32_t TC;               // the count
    __IO uint32_t PR;               // prescale
    __IO uint32_t PC;               // prescale counter
    __IO uint32_t MCR;              // match control
    __IO uint32_t MR[4];            // match registers
    __IO uint32_t CCR;
    __I  uint32_t CR[4];
    __IO uint32_t EMR;
} LPC_TIMER_T;

#define LPC_TIMER0 ((LPC_TIMER_T *) 0xE0004000)
#define LPC_TIMER1 ((LPC_TIMER_T *) 0xE0008000)

#define TIMER_TCR_EN    (1u << 0)
#define TIMER_TCR_RESET (1u << 1)
#define TIMER_MCR_MR0I  (1u << 0)   // interrupt on MR0
#define TIMER_MCR_MR0R  (1u << 1)   // reset TC on MR0

// --- ADC --------------------------------------------------------------------
// One converter, eight channels, 10 bits. The 17xx has a sequencer and this
// does not, which is why Chip_ADC_SetupSequencer is a no-op here rather than a
// missing symbol -- see chip_212x.c.
//
// TWO REGISTERS, and that is the whole block. An LPC2119/2129 has ADCR and
// ADGDR and nothing else: the per-channel result registers DR[0..7], INTEN and
// STAT arrived with the LPC213x and are NOT here. This struct used to carry
// them, copied from the 17xx layout, and Chip_ADC_ReadValue then read
// 0xE0034010 + 4*ch -- addresses this part does not implement. Channel 0 was
// the only one that happened to land on a real register.
//
// The consequence for the DRIVER is the interesting part: one global result
// register means exactly one conversion can be in flight, and reading it CLEARS
// the DONE flag. So a poll-then-read pair has to cache -- see chip_212x.c.
typedef struct {
    __IO uint32_t CR;               // ADCR  0xE0034000 -- control
    __IO uint32_t GDR;              // ADGDR 0xE0034004 -- the one result
} LPC_ADC_T;

#define LPC_ADC ((LPC_ADC_T *) 0xE0034000)

#define ADC_CR_PDN     (1u << 21)   // 1 = operational
#define ADC_CR_START_NOW (1u << 24)
#define ADC_DR_DONE    (1u << 31)
#define ADC_DR_OVERRUN (1u << 30)
#define ADC_DR_CHN(d)  (((d) >> 24) & 7u)   // which channel this result is

// --- LPCOpen spellings ------------------------------------------------------
// The 17xx library calls the first uart LPC_USART0 and has one GPIO block
// pointer. Aliases rather than renames: the structs above are named for what
// the LPC2000 user manual calls them, and this is the layer that makes one
// platform file fit both.
#define LPC_USART0 LPC_UART0
#define LPC_USART1 LPC_UART1
#define LPC_GPIO   LPC_GPIO0

#define UART_LCR_WLEN5       (0u << 0)
#define UART_LCR_WLEN6       (1u << 0)
#define UART_LCR_WLEN7       (2u << 0)
#define UART_LCR_WLEN8       (3u << 0)
#define UART_LCR_SBS_1BIT    (0u << 2)
#define UART_LCR_SBS_2BIT    (1u << 2)
#define UART_LCR_PARITY_DIS  (0u << 3)
#define UART_LCR_PARITY_EN   (1u << 3)
#define UART_FCR_TRG_LEV0    (0u << 6)
#define UART_FCR_TRG_LEV1    (1u << 6)
#define UART_FCR_TRG_LEV2    (2u << 6)
#define UART_FCR_TRG_LEV3    (3u << 6)

// LPCOpen's ADC vocabulary. This part has no sequencer and no per-channel
// setup struct, so the types exist to make the call sites compile and the
// values are the register encodings.
// lpc_types.h's names, which LPCOpen call sites use as plain arguments.
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef enum { RESET = 0, SET = 1 } FlagStatus;
#define ADC_TRIGGERMODE_RISING  0
#define ADC_TRIGGERMODE_FALLING 1

typedef struct { uint32_t adcRate; uint8_t bitsAccuracy; } ADC_CLOCK_SETUP_T;
typedef uint8_t ADC_CHANNEL_T;
#define ADC_CH0 0
#define ADC_START_NOW   1           // CR bits 26:24
#define ADC_START_NONE  0
#define ADC_DR_DONE_STAT 0
#define LPC_ADC0 LPC_ADC

extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);

// Wait for interrupt. CMSIS spells it __WFI and a Cortex-M has an instruction
// for it; an ARM7TDMI does not -- the equivalent is a coprocessor write.
//
// NOT inline, which was the first attempt: `mcr` is an ARM instruction and
// does not exist in Thumb, so an inline version fails to ASSEMBLE in every
// Thumb caller. It lives in vic_212x.c, the one file built -marm, and waiting
// for an interrupt is at least the right neighbourhood.
//
// This is the SHALLOW idle: peripherals keep their clocks, so a timer match or
// a CAN frame still wakes the core. Power-down stops the oscillator too and
// waking from it needs a reset or an external interrupt -- a different thing to
// ask for, not a deeper version of this one.
void __WFI(void);

// --- watchdog ---------------------------------------------------------------
//
// Not to USE it, but to find out what it did. WDMOD's WDTOF bit SURVIVES the
// reset it caused, and on this family it is the only thing that does -- an
// LPC2129 has no reset-source register (that arrived with the 213x). So it is
// the one way to tell "the watchdog reset me" from "somebody pulled RST".
//
// And WDEN cannot be cleared by software: once the watchdog is enabled, only a
// reset turns it off. If something before us armed it -- a boot loader that
// used it to jump into user code, say -- then not feeding it means being reset
// forever, and the loop looks exactly like a hardware fault.
#define LPC_WDMOD  (*(__IO uint8_t  *)0xE0000000)
#define LPC_WDTC   (*(__IO uint32_t *)0xE0000004)
#define LPC_WDFEED (*(__O  uint8_t  *)0xE0000008)

#define WDMOD_WDEN    (1u << 0)
#define WDMOD_WDRESET (1u << 1)
#define WDMOD_WDTOF   (1u << 2)     // timed out -- survives the reset

// WDMOD as it was at reset, before anything touched it.
uint8_t Chip_ResetCause(void);

// --- time ------------------------------------------------------------------
//
// NOT SysTick. That is a Cortex-M peripheral and this is an ARM7. But the
// difference that matters is not the name -- it is that a free-running counter
// and a periodic down-counter are different SHAPES, and the seam has to be the
// one both can honestly provide.
//
// So: microseconds, free-running, wrapping. A Cortex-M composes it from its ms
// counter and SysTick's VAL; an LPC2000 has it already, because TIMER0 counting
// at 1 MHz IS a microsecond clock and reading TC is the whole implementation.
//
// TIMER0 IS NEVER RESET, and that is a constraint from the board, not a
// preference. On a BridgeZone TIMER0 is also the PWM time base: MAT0.0 drives
// an output and MR3 is its period, both scheduled as TC + offset. Resetting TC
// under them moves both. The 1 ms interrupt therefore uses a match WITHOUT
// reset, rescheduled each time, and takes MR2 -- leaving MR0, MR1 and MR3 for
// what the board wants them for.
void     Chip_Tick_Init(uint32_t hz);   // start the periodic interrupt
uint32_t Chip_Tick_Us(void);            // free-running microseconds

// --- the calls csp_lpcopen.c makes ------------------------------------------

void     Chip_SystemInit(uint32_t xtal_hz, uint32_t cclk_hz, uint32_t pclk_div);
uint32_t Chip_Clock_GetSystemClockRate(void);
uint32_t Chip_Clock_GetPeripheralClockRate(void);

void     Chip_UART_Init(LPC_USART_T *u);
void     Chip_UART_SetBaud(LPC_USART_T *u, uint32_t baud);
void     Chip_UART_ConfigData(LPC_USART_T *u, uint32_t cfg);
void     Chip_UART_SetupFIFOS(LPC_USART_T *u, uint32_t cfg);
void     Chip_UART_TXEnable(LPC_USART_T *u);
void     Chip_UART_Enable(LPC_USART_T *u);
void     Chip_UART_SendByte(LPC_USART_T *u, uint8_t b);
uint8_t  Chip_UART_ReadByte(LPC_USART_T *u);
uint32_t Chip_UART_ReadLineStatus(LPC_USART_T *u);
uint32_t Chip_UART_GetStatus(LPC_USART_T *u);

void     Chip_GPIO_Init(LPC_GPIO_T *g);
void     Chip_GPIO_SetPinDIROutput(LPC_GPIO_T *g, uint8_t port, uint8_t pin);
void     Chip_GPIO_SetPinDIRInput(LPC_GPIO_T *g, uint8_t port, uint8_t pin);
void     Chip_GPIO_SetPinState(LPC_GPIO_T *g, uint8_t port, uint8_t pin, int on);
int      Chip_GPIO_GetPinState(LPC_GPIO_T *g, uint8_t port, uint8_t pin);

// Same signature as the 17xx call, so the platform file needs no #if. `mode`
// is ignored: an LPC2000 has no per-pin pull configuration, the pull-ups are
// always on.
// What the board loop passes as the first argument. The 17xx library has an
// LPC_IOCON block; this family does not -- PINSEL is a handful of addresses,
// not a struct -- so it is ignored here, and having the NAME on both sides is
// what lets one csp_board.c serve both.
#define LPC_IOCON_ARG ((void *)0)

void     Chip_IOCON_PinMux(void *iocon, uint8_t port, uint8_t pin,
			   uint16_t mode, uint8_t func);

void     Chip_ADC_Init(LPC_ADC_T *a, void *setup);
void     Chip_ADC_SetSampleRate(LPC_ADC_T *a, void *setup, uint32_t rate);
void     Chip_ADC_EnableChannel(LPC_ADC_T *a, uint8_t ch, int enable);
void     Chip_ADC_SetStartMode(LPC_ADC_T *a, uint8_t mode, uint8_t edge);
int      Chip_ADC_ReadStatus(LPC_ADC_T *a, uint8_t ch, uint32_t what);
int      Chip_ADC_ReadValue(LPC_ADC_T *a, uint8_t ch, uint16_t *out);

void     Chip_TIMER_Init(LPC_TIMER_T *t);
void     Chip_TIMER_SetMatch(LPC_TIMER_T *t, uint8_t n, uint32_t v);
void     Chip_TIMER_Enable(LPC_TIMER_T *t);
uint32_t Chip_TIMER_ReadCount(LPC_TIMER_T *t);

#endif
