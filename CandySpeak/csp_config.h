#ifndef __CSP_CONFIG_H__
#define __CSP_CONFIG_H__

#ifndef WANT_TRANSACTION
#define WANT_TRANSACTION 0  // use assert/undo
#endif

#ifndef WANT_REACTIVE
#define WANT_REACTIVE    0 // enq/deq
#endif

#ifndef WANT_STATISTICS
#define WANT_STATISTICS  0 // some accounting
#endif

#endif
