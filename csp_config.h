#ifndef __CSP_CONFIG_H__
#define __CSP_CONFIG_H__

#define TRANSACTION_DEFAULT 1
#ifndef SUPPORT_TRANSACTION
#define SUPPORT_TRANSACTION 1  // use assert/undo
#endif

#define REACTIVE_DEFAULT 0
#ifndef SUPPORT_REACTIVE
#define SUPPORT_REACTIVE    0 // enq/deq
#endif

#ifndef USE_STATISTICS
#define USE_STATISTICS  1    // need some accounting
#endif

// Use Q16.16 fixed-point instead of float (saves ~4KB on AVR)
#ifndef USE_FIXPOINT
#define USE_FIXPOINT        1  // 0=float, 1=Q16.16 fixpoint
#endif


#endif
