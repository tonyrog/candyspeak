// Line editing 
#ifndef __CSP_LINE_H__
#define __CSP_LINE_H__

#include <stdint.h>

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

// Line input handling (shared between platforms). The buffer is sized from the
// arena at boot, between these two bounds: never less than the old fixed AVR
// size, never more than a line anyone types by hand. A 32nd of the pool, so a
// board with room gets a longer line and a small one is not squeezed for it.
#define CSP_LINE_MIN   64
#define CSP_LINE_MAX  512
#define CSP_LINE_SHARE 32

typedef struct {
    // The REPL line being typed or pasted. The buffer is carved off the TOP of
    // the arena in csp_mem_init (see there for why not csp_mid_alloc), so how
    // long a line may be is a property of the BOARD rather than a compile-time
    // guess -- a mega and a Feather no longer have to agree on 64.
    // It doubles as the input QUEUE: while a completed line is being run, what
    // keeps arriving is stored raw behind it and re-fed afterwards, so a paste
    // does not have to wait on the driver's FIFO alone. Hence two cursors.
    char*    buf;
    uint16_t buf_size;  // capacity in bytes, terminator included
    uint16_t pos;       // LENGTH of the line being assembled (NUL position
                        // once it is ready). Deliberately still the length
			// and not the cursor: csp_line_done reads it to
			// find where the queued paste begins, and the
			// "one byte in, at most one byte out" invariant
			// that keeps the re-feed from overtaking itself is
			// stated in terms of it.
    uint16_t cur;       // where the next character is INSERTED, 0..line_pos
    uint8_t  esc;       // escape-sequence decoder state (0 = idle)
    uint8_t  refeed;    // re-feeding the queue after a line ran: history
			// and cursor keys are ignored, because a recall
			// would expand the line under the read cursor the
			// re-feed is walking

    // Command history. NULL and 0 when the pool was too small to carve one.
    char*    hist;
    uint16_t hist_size;   // capacity
    uint16_t hist_used;   // bytes in use; the newest entry ends here
    uint16_t hist_at;     // browse point: offset just past the entry being
			  // shown, or hist_used when not browsing
    uint16_t fill;        // bytes held in total; == line_pos unless a ready
			  // line has raw bytes queued behind it
    uint8_t  ready;       // a complete line is waiting at the front
    uint8_t  ovf;         // a character was dropped -> refuse the whole line
    uint8_t  need_prompt; // print "> " before the next read
    uint8_t  serial_xoff; // status of soft flow control
} csp_line_t;


// Command history, carved off the arena the same way and by the same share, so
// a board with room gets it and a board without does not. Entries are stored
// LENGTH-TEXT-LENGTH: two bytes of overhead buys a walk in both directions --
// forwards to drop the oldest when it fills, backwards to recall the newest
// first, which is the only order anybody browses in.
//
// Under CSP_HIST_MIN there is no point: one short line is not a history. Set
// CSP_HISTORY_BYTES to 0 to leave it out entirely -- the cursor editing below
// costs nothing extra and stays.
#define CSP_HIST_SHARE 32
#define CSP_HIST_MAX  512
#define CSP_HIST_MIN   32
// A line longer than this is edited normally but not REMEMBERED: the length
// byte at each end holds 255, and widening it to two would cost every entry a
// byte to buy back a case nobody types.
#define CSP_HIST_LINE_MAX 255

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

extern void csp_line_init(csp_line_t* st);
extern void csp_line_input(csp_line_t* st, char c);
extern void csp_line_prompt(csp_line_t* st);
// Room for one more byte. A reader keeps draining its port while this is true,
// INCLUDING while a line is waiting to run -- that spare room is what absorbs a
// paste. When it goes false the driver's FIFO takes over.
extern int  csp_line_space(csp_line_t* st);
// Finished with the line at the front: drop it and bring anything queued behind
// it down to the start. MUST be called instead of clearing line_ready by hand.
extern void csp_line_done(csp_line_t* st);

extern void csp_line_left(csp_line_t* st);
extern void csp_line_right(csp_line_t* st);
extern void csp_line_kill_to_end(csp_line_t* st);
extern void csp_line_replace(csp_line_t* st, const char* s, uint16_t n);
extern void csp_line_recall(csp_line_t* st, int dir);

#ifdef __cplusplus
EXTERN_C_END
#endif


#endif
