/* A stand-in for LPCOpen's chip.h, JUST enough of it to type-check
 * csp_lpcopen.c on the host. Signatures copied from
 * the lpc40xx headers -- if one of them is wrong here it
 * is wrong in the same way in csp_lpcopen.c, which is the point.
 */
#ifndef LPC_CHIP_STUB_H
#define LPC_CHIP_STUB_H

#include <stdint.h>
#include <stdbool.h>

/* Which family branch to type-check. -DSTUB_FAMILY=15 exercises the sequencer
 * ADC / UART_STAT / IAP-EEPROM path, =43 the memory-mapped EEPROM path, and
 * anything else the 40xx path. */
#ifndef STUB_FAMILY
#define STUB_FAMILY 40
#endif
#if STUB_FAMILY == 15
#define CHIP_LPC15XX 1
#elif STUB_FAMILY == 17
#define CHIP_LPC175X_6X 1
#elif STUB_FAMILY == 43
#define CHIP_LPC43XX 1
#else
#define CHIP_LPC40XX 1
#endif

typedef enum { ERROR = 0, SUCCESS = !ERROR } Status;
typedef enum { RESET = 0, SET = !RESET } FlagStatus;
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;

/* --- CMSIS core ---------------------------------------------------------- */
typedef struct { volatile uint32_t CTRL, LOAD, VAL, CALIB; } SysTick_Type;
extern SysTick_Type* SysTick;
extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
uint32_t SysTick_Config(uint32_t ticks);
void __WFI(void);

/* --- GPIO ---------------------------------------------------------------- */
typedef struct { uint32_t dummy; } LPC_GPIO_T;
extern LPC_GPIO_T* LPC_GPIO;
void Chip_GPIO_Init(LPC_GPIO_T* p);
void Chip_GPIO_SetPinState(LPC_GPIO_T* p, uint8_t port, uint8_t pin, bool setting);
bool Chip_GPIO_GetPinState(LPC_GPIO_T* p, uint8_t port, uint8_t pin);
void Chip_GPIO_SetPinDIROutput(LPC_GPIO_T* p, uint8_t port, uint8_t pin);
void Chip_GPIO_SetPinDIRInput(LPC_GPIO_T* p, uint8_t port, uint8_t pin);

/* --- ADC ----------------------------------------------------------------- */
typedef struct { uint32_t dummy; } LPC_ADC_T;
extern LPC_ADC_T* LPC_ADC0;
typedef struct { uint32_t adcRate; uint8_t bitsAccuracy; bool burstMode; } ADC_CLOCK_SETUP_T;
typedef enum { ADC_CH0 = 0, ADC_CH1, ADC_CH2, ADC_CH3 } ADC_CHANNEL_T;
typedef enum { ADC_NO_START = 0, ADC_START_NOW } ADC_START_MODE_T;
typedef enum { ADC_TRIGGERMODE_RISING = 0, ADC_TRIGGERMODE_FALLING } ADC_EDGE_CFG_T;
typedef enum { ADC_DR_DONE_STAT = 0, ADC_DR_OVERRUN_STAT } ADC_STATUS_T;
void Chip_ADC_Init(LPC_ADC_T* p, ADC_CLOCK_SETUP_T* s);
void Chip_ADC_SetSampleRate(LPC_ADC_T* p, ADC_CLOCK_SETUP_T* s, uint32_t rate);
Status Chip_ADC_ReadValue(LPC_ADC_T* p, uint8_t channel, uint16_t* data);
FlagStatus Chip_ADC_ReadStatus(LPC_ADC_T* p, uint8_t channel, uint32_t st);
void Chip_ADC_SetStartMode(LPC_ADC_T* p, ADC_START_MODE_T m, ADC_EDGE_CFG_T e);
void Chip_ADC_EnableChannel(LPC_ADC_T* p, ADC_CHANNEL_T ch, FunctionalState s);

/* --- UART ---------------------------------------------------------------- */
typedef struct { uint32_t dummy; } LPC_USART_T;
extern LPC_USART_T* LPC_USART0;
#define UART_LCR_WLEN8      (3 << 0)
#define UART_LCR_SBS_1BIT   (0 << 2)
#define UART_LCR_PARITY_DIS (0 << 3)
#define UART_FCR_FIFO_EN    (1 << 0)
#define UART_FCR_TRG_LEV0   (0 << 6)
#define UART_LSR_RDR        (1 << 0)
#define UART_LSR_THRE       (1 << 5)
/* 15xx USART: a config register and a status register instead of LCR/LSR */
#define UART_CFG_DATALEN_8   (0x01 << 2)
#define UART_CFG_PARITY_NONE (0x00 << 4)
#define UART_CFG_STOPLEN_1   (0x00 << 6)
#define UART_STAT_RXRDY      (0x01 << 0)
#define UART_STAT_TXRDY      (0x01 << 2)
void Chip_UART_Enable(LPC_USART_T* p);
uint32_t Chip_UART_GetStatus(LPC_USART_T* p);
void Chip_UART_Init(LPC_USART_T* p);
uint32_t Chip_UART_SetBaud(LPC_USART_T* p, uint32_t baud);
void Chip_UART_ConfigData(LPC_USART_T* p, uint32_t cfg);
void Chip_UART_SetupFIFOS(LPC_USART_T* p, uint32_t fcr);
void Chip_UART_TXEnable(LPC_USART_T* p);
void Chip_UART_SendByte(LPC_USART_T* p, uint8_t data);
uint8_t Chip_UART_ReadByte(LPC_USART_T* p);
uint32_t Chip_UART_ReadLineStatus(LPC_USART_T* p);

/* --- EEPROM (17xx/40xx paged) -------------------------------------------- */
typedef struct { uint32_t dummy; } LPC_EEPROM_T;
extern LPC_EEPROM_T* LPC_EEPROM;
#define EEPROM_PAGE_SIZE 64
#define EEPROM_PAGE_NUM  63
typedef enum { EEPROM_RWSIZE_8BITS = 0, EEPROM_RWSIZE_16BITS, EEPROM_RWSIZE_32BITS } EEPROM_RWSIZE_T;
void Chip_EEPROM_Init(LPC_EEPROM_T* p);
Status Chip_EEPROM_Write(LPC_EEPROM_T* p, uint16_t pageOffset, uint16_t pageAddress,
			 void* pData, EEPROM_RWSIZE_T wsize, uint32_t byteNum);
void Chip_EEPROM_Read(LPC_EEPROM_T* p, uint16_t pageOffset, uint16_t pageAddress,
		      void* pData, EEPROM_RWSIZE_T rsize, uint32_t byteNum);

#endif
