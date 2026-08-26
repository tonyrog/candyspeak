#ifndef __CAN_212X_H__
#define __CAN_212X_H__

// LPC2000 CAN: send a frame, receive a frame, set the bit rate. Enough for a
// node to be on a bus.
//
// NOT here: the acceptance filter (the LUT at 0xE0038000 with its five section
// pointers), FullCAN auto-receive, error counters, self-test. They are real and
// nxp_files/2129/can_lpc.c has them; what is here is what CandySpeak's own CAN
// layer asks for, which is csp_can_send/recv/init and nothing else. The filter
// stays bypassed -- every frame is delivered and the program decides -- because
// a #buffer declares which id it wants and the match already happens there.

#include <stdint.h>

typedef struct {
    __IO uint32_t MOD;              // mode: bit 0 reset, bit 2 self-test
    __O  uint32_t CMR;              // command
    __I  uint32_t GSR;              // global status
    __I  uint32_t ICR;              // interrupt + capture (read clears)
    __IO uint32_t IER;              // interrupt enable
    __IO uint32_t BTR;              // bit timing
    __IO uint32_t EWL;              // error warning limit
    __I  uint32_t SR;               // status
    __IO uint32_t RFS;              // receive frame status
    __IO uint32_t RID;              // receive id
    __IO uint32_t RDA;              // receive data 1-4
    __IO uint32_t RDB;              // receive data 5-8
    __IO uint32_t TFI1;             // transmit 1: frame info
    __IO uint32_t TID1;
    __IO uint32_t TDA1;
    __IO uint32_t TDB1;
    __IO uint32_t TFI2;             // transmit 2
    __IO uint32_t TID2;
    __IO uint32_t TDA2;
    __IO uint32_t TDB2;
    __IO uint32_t TFI3;             // transmit 3
    __IO uint32_t TID3;
    __IO uint32_t TDA3;
    __IO uint32_t TDB3;
} LPC_CAN_T;

#define LPC_CAN1 ((LPC_CAN_T *) 0xE0044000)
#define LPC_CAN2 ((LPC_CAN_T *) 0xE0048000)

// The acceptance filter, shared by both controllers. Mode 2 is "bypass":
// accept everything. See the note above.
#define LPC_CANAF_MODE (*(__IO uint32_t *) 0xE003C000)
#define CANAF_ACCOFF   0x00
#define CANAF_ACCBP    0x02         // bypass -- deliver every frame

#define CANMOD_RM    (1u << 0)      // reset mode: BTR is writable only here
#define CANMOD_STM   (1u << 2)      // self-test: transmits need no ack

#define CANCMR_TR    (1u << 0)      // transmission request
#define CANCMR_AT    (1u << 1)      // abort
#define CANCMR_RRB   (1u << 2)      // release receive buffer
#define CANCMR_STB1  (1u << 5)      // select transmit buffer 1

#define CANSR_RBS    (1u << 0)      // receive buffer has a frame
#define CANSR_TBS1   (1u << 2)      // transmit buffer 1 free

#define CANFI_RTR    (1u << 30)     // remote frame
#define CANFI_FF     (1u << 31)     // 29-bit id

// Same flag bits SocketCAN uses, so a frame crossing this boundary needs no
// translation -- csp_can_send takes an id in exactly this encoding.
#define CSP_CAN_EFF_FLAG 0x80000000u
#define CSP_CAN_RTR_FLAG 0x40000000u
#define CSP_CAN_SFF_MASK 0x000007ffu
#define CSP_CAN_EFF_MASK 0x1fffffffu

// LPCOpen's shape, so csp_lpcopen.c's CAN path is the same code on both
// families. The acceptance-filter pointers are the 17xx library's; this family
// has one global AFMR register and no RAM table worth a type, so they are
// ignored -- named rather than removed, because a call that compiles on one
// chip and not the other is the thing this whole layer exists to avoid.
typedef struct { uint32_t _unused; } LPC_CANAF_T;
typedef struct { uint32_t _unused; } LPC_CANAF_RAM_T;
#define LPC_CANAF     ((LPC_CANAF_T *)0)
#define LPC_CANAF_RAM ((LPC_CANAF_RAM_T *)0)

typedef enum { CAN_BUFFER_1 = 0, CAN_BUFFER_2 = 1, CAN_BUFFER_3 = 2 }
    CAN_BUFFER_ID_T;
typedef enum { CAN_AF_NORMAL_MODE = 0, CAN_AF_BYBASS_MODE = 2 } CAN_AF_MODE_T;

typedef struct {
    uint32_t ID;                    // bit 30 set = 29-bit id
    uint32_t Type;                  // CAN_REMOTE_MSG
    uint32_t DLC;                   // 0..8
    uint8_t  Data[8];
} CAN_MSG_T;

#define CAN_REMOTE_MSG      (1u << 0)
#define CAN_EXTEND_ID_USAGE (1u << 30)

#ifndef SUCCESS
typedef enum { ERROR = 0, SUCCESS = 1 } Status;
#endif

void   Chip_CAN_Init(LPC_CAN_T *can, LPC_CANAF_T *af, LPC_CANAF_RAM_T *afram);
void   Chip_CAN_SetAFMode(LPC_CANAF_T *af, CAN_AF_MODE_T mode);

// Returns ERROR when the peripheral clock cannot produce the rate exactly --
// a real answer and not a rounding. A CAN bus with two nodes at slightly
// different rates does not half-work, it fails on every frame.
Status Chip_CAN_SetBitRate(LPC_CAN_T *can, uint32_t bitrate);

Status Chip_CAN_Send(LPC_CAN_T *can, CAN_BUFFER_ID_T buf, CAN_MSG_T *msg);
Status Chip_CAN_Receive(LPC_CAN_T *can, CAN_MSG_T *msg);

#endif
