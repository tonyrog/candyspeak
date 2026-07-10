#ifndef __CSP_CONFIG_H__
#define __CSP_CONFIG_H__

#ifndef SUPPORT_STATES
#define SUPPORT_STATES    1
#define MAX_STATES 16
#endif

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
