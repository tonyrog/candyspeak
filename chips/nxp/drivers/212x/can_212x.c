#include "chip_212x.h"
#include "can_212x.h"

// --- bit timing -------------------------------------------------------------
//
// A CAN bit is divided into time quanta: 1 for SYNC, then TSEG1, then TSEG2.
// The sample point sits between TSEG1 and TSEG2, and every node on the bus has
// to agree on where it is -- so this is not "close enough" arithmetic. Two
// nodes that disagree do not run slowly, they fail on every frame.
//
//   tq_per_bit = 1 + TSEG1 + TSEG2
//   BRP        = pclk / (bitrate * tq_per_bit)
//
// The sample point is placed at 75%, which is what CANopen and DeviceNet ask
// for and what everything else tolerates. Searching from the LARGEST tq count
// down is deliberate: more quanta per bit means finer resynchronisation, so the
// first exact division found is also the best one.
static int solve_btr(uint32_t pclk, uint32_t bitrate, uint32_t *btr_out)
{
    uint32_t tq;

    for (tq = 25; tq >= 8; tq--) {
	uint32_t brp = pclk / (bitrate * tq);
	uint32_t tseg1, tseg2, sjw;

	if (brp == 0)
	    continue;
	// EXACT, not near: an integer division that rounds is a bit rate that
	// is a fraction of a percent off, which is exactly enough to fail.
	if ((pclk % (bitrate * tq)) != 0)
	    continue;
	if (brp > 1024)
	    continue;

	// 75%: TSEG1 spans from after SYNC to the sample point.
	tseg2 = tq / 4;
	if (tseg2 < 1) tseg2 = 1;
	if (tseg2 > 8) tseg2 = 8;
	tseg1 = tq - 1 - tseg2;
	if ((tseg1 < 1) || (tseg1 > 16))
	    continue;

	// SJW: how far a node may stretch a bit to resynchronise. Capped at 4
	// and never more than TSEG2, which is the hard limit.
	sjw = (tseg2 < 4) ? tseg2 : 4;

	// The register holds each field minus one.
	*btr_out = ((brp - 1) & 0x3ff)
		 | ((sjw - 1) << 14)
		 | ((tseg1 - 1) << 16)
		 | ((tseg2 - 1) << 20);
	return 0;
    }
    return -1;
}

void Chip_CAN_Init(LPC_CAN_T *can, LPC_CANAF_T *af, LPC_CANAF_RAM_T *afram)
{
    (void)af; (void)afram;          // no filter RAM on this family
    can->MOD = CANMOD_RM;           // reset mode: where BTR is writable
    can->IER = 0;                   // polled; nothing here needs an interrupt
}

void Chip_CAN_SetAFMode(LPC_CANAF_T *af, CAN_AF_MODE_T mode)
{
    (void)af;
    LPC_CANAF_MODE = (uint32_t)mode;
}

Status Chip_CAN_SetBitRate(LPC_CAN_T *can, uint32_t bitrate)
{
    uint32_t btr;
    uint32_t pclk = Chip_Clock_GetPeripheralClockRate();

    if ((pclk == 0) || (bitrate == 0))
	return ERROR;
    if (solve_btr(pclk, bitrate, &btr) < 0)
	return ERROR;

    // BTR is writable ONLY in reset mode. Writing it while the controller runs
    // is silently ignored -- the node then sits on the bus at whatever rate it
    // had, erroring on every frame, which looks like a wiring fault.
    can->MOD = CANMOD_RM;
    can->BTR = btr;
    can->MOD = 0;                   // leave reset: operational
    return SUCCESS;
}

Status Chip_CAN_Send(LPC_CAN_T *can, CAN_BUFFER_ID_T buf, CAN_MSG_T *msg)
{
    uint32_t fi, d1 = 0, d2 = 0;
    uint32_t i, len = (msg->DLC > 8) ? 8 : msg->DLC;

    (void)buf;                      // buffer 1 only, which is all polling needs
    if (!(can->SR & CANSR_TBS1))
	return ERROR;               // busy; the caller retries

    fi = (len << 16);
    if (msg->ID & CAN_EXTEND_ID_USAGE) {
	fi |= CANFI_FF;
	can->TID1 = msg->ID & CSP_CAN_EFF_MASK;
    } else {
	can->TID1 = msg->ID & CSP_CAN_SFF_MASK;
    }
    if (msg->Type & CAN_REMOTE_MSG)
	fi |= CANFI_RTR;
    can->TFI1 = fi;

    // Byte by byte into two words: the data registers are 32-bit and little
    // endian by byte number, and Data has no alignment promise -- a word store
    // through a cast would fault on an odd address.
    for (i = 0; i < len; i++) {
	if (i < 4)
	    d1 |= (uint32_t)msg->Data[i] << (8 * i);
	else
	    d2 |= (uint32_t)msg->Data[i] << (8 * (i - 4));
    }
    can->TDA1 = d1;
    can->TDB1 = d2;

    can->CMR = CANCMR_TR | CANCMR_STB1;
    return SUCCESS;
}

Status Chip_CAN_Receive(LPC_CAN_T *can, CAN_MSG_T *msg)
{
    uint32_t rfs, d1, d2;
    uint32_t i, n;

    if (!(can->SR & CANSR_RBS))
	return ERROR;               // nothing pending

    rfs = can->RFS;
    n = (rfs >> 16) & 0x0f;
    if (n > 8) n = 8;

    msg->ID = can->RID & ((rfs & CANFI_FF) ? CSP_CAN_EFF_MASK
					   : CSP_CAN_SFF_MASK);
    if (rfs & CANFI_FF)
	msg->ID |= CAN_EXTEND_ID_USAGE;
    msg->Type = (rfs & CANFI_RTR) ? CAN_REMOTE_MSG : 0;
    msg->DLC = n;

    d1 = can->RDA;
    d2 = can->RDB;
    for (i = 0; i < n; i++)
	msg->Data[i] = (uint8_t)((i < 4) ? (d1 >> (8 * i))
					 : (d2 >> (8 * (i - 4))));

    // Release LAST. The registers above are only valid while the buffer is
    // held; releasing first lets the next frame overwrite what we are reading.
    can->CMR = CANCMR_RRB;
    return SUCCESS;
}
