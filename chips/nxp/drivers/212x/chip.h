#ifndef __CHIP_H__
#define __CHIP_H__

// The family entry point, the same name LPCOpen uses -- so a platform file says
// `#include "chip.h"` and the include path decides which chip it gets. That is
// what lets csp_lpcopen.c serve an LPC1754 and an LPC2129 without an #if.
#include "chip_212x.h"
#include "vic_212x.h"
#include "can_212x.h"
#include "i2c_212x.h"

#endif
