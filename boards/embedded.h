// Settings every constrained target wants. Board files include this first and
// then add their own deltas, so "what a small board wants" is written once
// instead of repeated in eleven files -- and a board that wants something else
// simply defines it before including this (every setting here is #ifndef).
//
// These are the values the sketch build has always used; they lived in a second
// csp_config.h under CandySpeak/ that had quietly drifted from the one in the
// repo root. That is the divergence this directory exists to prevent.
#ifndef __CSP_BOARD_EMBEDDED_H__
#define __CSP_BOARD_EMBEDDED_H__

#ifndef SUPPORT_REACTIVE
#define SUPPORT_REACTIVE 0    // enq/deq -- saves ~1KB RAM on AVR
#endif

#ifndef USE_STATISTICS
#define USE_STATISTICS   0    // cycle/rule counters: diagnostics, not function
#endif

#endif
