/* Bodies for the stubbed chip API, so csp_lpcopen.c can be LINKED against the
 * CandySpeak core on the host. Nothing here pretends to be hardware -- the
 * point is the link, which is what catches a port that forgot to implement
 * something the core calls, or implemented something twice.
 */
#include "chip.h"

static SysTick_Type systick_mem;
SysTick_Type* SysTick = &systick_mem;
uint32_t SystemCoreClock = 96000000;
void SystemCoreClockUpdate(void) { }
uint32_t SysTick_Config(uint32_t ticks) { systick_mem.LOAD = ticks - 1; return 0; }
/* "Wait for interrupt", and the interrupt this firmware waits for IS SysTick --
 * so run its handler. That makes the idle path in csp_lpc_loop testable on the
 * host: the millisecond counter advances exactly when the firmware sleeps, and
 * a delay completes instead of spinning forever. */
void SysTick_Handler(void);
void __WFI(void) { SysTick_Handler(); }

static LPC_GPIO_T   gpio_mem;   LPC_GPIO_T*   LPC_GPIO   = &gpio_mem;
static LPC_ADC_T    adc_mem;    LPC_ADC_T*    LPC_ADC0   = &adc_mem;
static LPC_USART_T  uart_mem;   LPC_USART_T*  LPC_USART0 = &uart_mem;
static LPC_EEPROM_T ee_mem;     LPC_EEPROM_T* LPC_EEPROM = &ee_mem;

void Chip_GPIO_Init(LPC_GPIO_T* p) { (void)p; }
void Chip_GPIO_SetPinState(LPC_GPIO_T* p, uint8_t a, uint8_t b, bool c)
{ (void)p; (void)a; (void)b; (void)c; }
bool Chip_GPIO_GetPinState(LPC_GPIO_T* p, uint8_t a, uint8_t b)
{ (void)p; (void)a; (void)b; return false; }
void Chip_GPIO_SetPinDIROutput(LPC_GPIO_T* p, uint8_t a, uint8_t b)
{ (void)p; (void)a; (void)b; }
void Chip_GPIO_SetPinDIRInput(LPC_GPIO_T* p, uint8_t a, uint8_t b)
{ (void)p; (void)a; (void)b; }

void Chip_ADC_Init(LPC_ADC_T* p, ADC_CLOCK_SETUP_T* s) { (void)p; (void)s; }
void Chip_ADC_SetSampleRate(LPC_ADC_T* p, ADC_CLOCK_SETUP_T* s, uint32_t r)
{ (void)p; (void)s; (void)r; }
Status Chip_ADC_ReadValue(LPC_ADC_T* p, uint8_t c, uint16_t* d)
{ (void)p; (void)c; *d = 0; return SUCCESS; }
FlagStatus Chip_ADC_ReadStatus(LPC_ADC_T* p, uint8_t c, uint32_t s)
{ (void)p; (void)c; (void)s; return SET; }
void Chip_ADC_SetStartMode(LPC_ADC_T* p, ADC_START_MODE_T m, ADC_EDGE_CFG_T e)
{ (void)p; (void)m; (void)e; }
void Chip_ADC_EnableChannel(LPC_ADC_T* p, ADC_CHANNEL_T c, FunctionalState s)
{ (void)p; (void)c; (void)s; }

void Chip_UART_Init(LPC_USART_T* p) { (void)p; }
uint32_t Chip_UART_SetBaud(LPC_USART_T* p, uint32_t b) { (void)p; return b; }
void Chip_UART_ConfigData(LPC_USART_T* p, uint32_t c) { (void)p; (void)c; }
void Chip_UART_SetupFIFOS(LPC_USART_T* p, uint32_t f) { (void)p; (void)f; }
void Chip_UART_TXEnable(LPC_USART_T* p) { (void)p; }
/* Route the console to stdout, so a host run shows the boot banner the board
 * would print. This is what makes the link test a smoke test as well. */
#include <stdio.h>
#include <stdlib.h>
void Chip_UART_SendByte(LPC_USART_T* p, uint8_t d)
{
    (void)p;
    putchar((int)d);
    fflush(stdout);      /* the run ends on a timeout signal, with no flush */
}
/* The console reads stdin, one character at a time, so the REPL path is
 * exercised for real: line assembly, csp_process_line, the rebuild it triggers
 * and the listing that comes back. EOF ends the run -- there is no other way
 * out of a firmware main loop.
 *
 * The pending byte is held here because the port asks "is one available?" and
 * "give me one" as two calls, the way a UART works. */
static int rx_pending = -1;
static int rx_eof     = 0;
static int rx_grace   = 0;

static int rx_poll(void)
{
    if (rx_eof) {
	// Do NOT exit the moment stdin runs dry: the loop reads ahead of what
	// it has processed, so at EOF there are still queued lines that have
	// not been through csp_process_line. Report "nothing available" for a
	// while and let the loop drain itself, then stop.
	if (++rx_grace > 10000) {
	    fflush(stdout);
	    exit(0);
	}
	return -1;
    }
    if (rx_pending < 0) {
	int c = getchar();
	if (c == EOF) {
	    rx_eof = 1;
	    return -1;
	}
	rx_pending = c;
    }
    return rx_pending;
}

uint8_t Chip_UART_ReadByte(LPC_USART_T* p)
{
    int c = rx_poll();
    (void)p;
    rx_pending = -1;
    return (uint8_t)c;
}

uint32_t Chip_UART_ReadLineStatus(LPC_USART_T* p)
{
    (void)p;
    return UART_LSR_THRE | (rx_poll() >= 0 ? UART_LSR_RDR : 0);
}

void Chip_UART_Enable(LPC_USART_T* p) { (void)p; }

uint32_t Chip_UART_GetStatus(LPC_USART_T* p)
{
    (void)p;
    return UART_STAT_TXRDY | (rx_poll() >= 0 ? UART_STAT_RXRDY : 0);
}

void Chip_EEPROM_Init(LPC_EEPROM_T* p) { (void)p; }
Status Chip_EEPROM_Write(LPC_EEPROM_T* p, uint16_t o, uint16_t a, void* d,
			 EEPROM_RWSIZE_T w, uint32_t n)
{ (void)p; (void)o; (void)a; (void)d; (void)w; (void)n; return SUCCESS; }
void Chip_EEPROM_Read(LPC_EEPROM_T* p, uint16_t o, uint16_t a, void* d,
		      EEPROM_RWSIZE_T r, uint32_t n)
{ (void)p; (void)o; (void)a; (void)d; (void)r; (void)n; }

/* The linker symbols csp_lpcopen.c reads for its memory report. A real build
 * gets these from the LPCXpresso/LPCOpen linker script. */
char _pvHeapStart;
char _vStackTop;

// --- the tick seam ----------------------------------------------------------
// csp_lpcopen.c asks the chip layer for the periodic tick (see the declaration
// there). The stub has no timer and no interrupts, so this reports a period
// that never advances -- enough to link and to let csp_time_ms, which the
// stub's own SysTick_Handler drives, be the thing under test.
void Chip_Tick_Init(uint32_t hz) { (void)hz; }

// Microseconds. The platform file's own csp_ticks_ms is static to it, so this
// counts separately -- and every __WFI() above calls SysTick_Handler, which is
// what advances both. One step per call, which is enough for a timeout to
// expire and for time to be monotonic, and no more precision than that.
static uint32_t stub_us = 0;
uint32_t Chip_Tick_Us(void) { stub_us += 1000; return stub_us; }

// --- what the IAP flash backend needs ---------------------------------------
// flash_212x.c cannot run here: IAP lives in the part's boot ROM and is called
// through a fixed address. What CAN be checked on the host is that it compiles
// and links against the same headers the board build uses -- which is what
// caught its dependency on the CLOCK driver rather than on a constant, and on
// the interrupt window rather than on nothing.
uint32_t Chip_Clock_GetSystemClockRate(void) { return 60000000u; }
uint32_t DisableIRQ(void) { return 0; }
void     RestoreIRQ(uint32_t cpsr) { (void)cpsr; }

// csp_device() itself comes from port/csp_devices.c, which the link includes --
// a board would set its own with csp_device_set.
