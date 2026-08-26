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

int Chip_CAN_Init(LPC_CAN_T *can, uint32_t bitrate)
{
    uint32_t btr;
    uint32_t pclk = Chip_Clock_GetPeripheralClockRate();

    if ((pclk == 0) || (bitrate == 0))
	return -1;
    if (solve_btr(pclk, bitrate, &btr) < 0)
	return -1;

    // BTR is writable ONLY in reset mode. Writing it while the controller runs
    // is silently ignored -- the node then sits on the bus at whatever rate it
    // had, erroring on every frame, which looks like a wiring fault.
    can->MOD = CANMOD_RM;
    can->IER = 0;                   // polled; nothing here needs an interrupt
    can->BTR = btr;
    can->MOD = 0;                   // leave reset: operational

    // Accept everything. The filter LUT would let the controller drop frames
    // this node does not want, but a #buffer already says which id it wants and
    // the match happens there -- two places to state it is one too many.
    LPC_CANAF_MODE = CANAF_ACCBP;
    return 0;
}

int Chip_CAN_Send(LPC_CAN_T *can, uint32_t id, const uint8_t *data, uint8_t len)
{
    uint32_t fi;
    uint32_t d1 = 0, d2 = 0;
    int i;

    if (!(can->SR & CANSR_TBS1))
	return -1;                  // buffer 1 busy; the caller retries
    if (len > 8)
	len = 8;

    fi = ((uint32_t)len << 16);
    if (id & CSP_CAN_EFF_FLAG) {
	fi |= CANFI_FF;
	can->TID1 = id & CSP_CAN_EFF_MASK;
    } else {
	can->TID1 = id & CSP_CAN_SFF_MASK;
    }
    if (id & CSP_CAN_RTR_FLAG)
	fi |= CANFI_RTR;
    can->TFI1 = fi;

    // Byte by byte into two words: the data registers are 32-bit and little
    // endian by byte number, and `data` has no alignment promise -- a word
    // store through a cast would fault on an odd address.
    for (i = 0; i < len; i++) {
	if (i < 4)
	    d1 |= (uint32_t)data[i] << (8 * i);
	else
	    d2 |= (uint32_t)data[i] << (8 * (i - 4));
    }
    can->TDA1 = d1;
    can->TDB1 = d2;

    can->CMR = CANCMR_TR | CANCMR_STB1;
    return 0;
}

int Chip_CAN_Recv(LPC_CAN_T *can, uint32_t *id, uint8_t *data, uint8_t *len)
{
    uint32_t rfs, d1, d2;
    int i, n;

    if (!(can->SR & CANSR_RBS))
	return 0;                   // nothing pending -- not an error

    rfs = can->RFS;
    n = (int)((rfs >> 16) & 0x0f);
    if (n > 8) n = 8;

    *id = can->RID & ((rfs & CANFI_FF) ? CSP_CAN_EFF_MASK : CSP_CAN_SFF_MASK);
    if (rfs & CANFI_FF)
	*id |= CSP_CAN_EFF_FLAG;
    if (rfs & CANFI_RTR)
	*id |= CSP_CAN_RTR_FLAG;

    d1 = can->RDA;
    d2 = can->RDB;
    for (i = 0; i < n; i++)
	data[i] = (uint8_t)((i < 4) ? (d1 >> (8 * i)) : (d2 >> (8 * (i - 4))));
    *len = (uint8_t)n;

    // Release LAST. The registers above are only valid while the buffer is
    // held; releasing first lets the next frame overwrite what we are reading.
    can->CMR = CANCMR_RRB;
    return 1;
}
