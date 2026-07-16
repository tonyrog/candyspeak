#ifndef __CSP_CONFIG_H__
#define __CSP_CONFIG_H__

// Linux - has plenty of memory
#define SYSTEM_RAM_CAPACITY (256*1024)

#define MAX_STATES 16

#define REACTIVE_DEFAULT 0
#ifndef SUPPORT_REACTIVE
#define SUPPORT_REACTIVE    1 // enq/deq
#endif

#ifndef USE_STATISTICS
#define USE_STATISTICS  1    // need some accounting
#endif

// Use Q16.16 fixed-point instead of float (saves ~4KB on AVR)
#ifndef USE_FIXPOINT
#define USE_FIXPOINT        1  // 0=float, 1=Q16.16 fixpoint
#endif

#endif
