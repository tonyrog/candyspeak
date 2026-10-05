// The runtime's side of the chip layer: port/csp_io.c.

#ifndef __CSP_IO_H__
#define __CSP_IO_H__

#include "csp.h"

// The pin-mode pass csp_setup runs once. Not a direction -- it applies to a pin
// whichever way it points -- so it is 0, which no direction bit can be.
#define CSP_IO_CONFIG 0

// One pass over st->nio: CSP_IO_CONFIG sets every pin's mode, DIR_IN reads the
// inputs into their slots, DIR_OUT writes the outputs. Leaves the context
// reset, as the per-port loops did.
extern void csp_io_sweep(csp_rt_t* st, uint8_t want);

#endif
