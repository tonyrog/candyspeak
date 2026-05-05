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

#ifndef SUPPORT_STATISTICS
#define SUPPORT_STATISTICS  1 // some accounting
#endif

#endif
