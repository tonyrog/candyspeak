#ifndef __CSP_FLASH_H__
#define __CSP_FLASH_H__

// Where things live in flash, and how to put them there.
//
// THE PROBLEM THIS SOLVES. Flash sectors are not the same size. An LPC212x is
// eight 8K sectors, then two of 64K, then seven more of 8K; an LPC213x is eight
// 4K, fourteen 32K, five 4K. So "the application starts at 128K" is not a thing
// you can say -- erasing is per SECTOR, and a byte offset does not tell you
// which sectors that touches. Everything here is therefore expressed in
// SECTORS, and byte offsets are derived from the map rather than assumed.
//
// WHAT THE USER SAYS. Not sector numbers -- a REGION name and a range:
//
//     runtime  0..7     the interpreter (and, on a full build, the compiler)
//     A        8        one 64K sector: application slot A
//     B        9        the other: slot B
//     store    10..16   images, saved settings, whatever else persists
//
// which on an LPC2129 is 64K + 64K + 64K + 56K and needs no arithmetic from
// whoever writes it. csp_region_offset/size do the sums, once, from the sector
// table the part actually has.
//
// WHAT IS DELIBERATELY NOT HERE. Nothing knows how to ERASE or WRITE -- that is
// four functions a backend supplies (see the bottom). The geometry above is
// pure arithmetic over a table, which is what makes it testable on the host
// against a file instead of against a board.

#include "csp.h"

// A part's flash: where it starts and how its sectors are sized. `size` has
// `n` entries, one per sector, in address order.
typedef struct {
    uint32_t        base;    // address of sector 0
    const uint32_t* size;    // bytes per sector, n entries
    uint8_t         n;       // sectors
} csp_sectors_t;

// What a region is FOR. Needed because the names alone cannot say it: `store`
// has no A/B suffix and neither does `App1`, but only one of them is somewhere
// an application can be booted from.
#define CSP_REG_RUNTIME 0   // the interpreter (and the compiler, on a full build)
#define CSP_REG_APP     1   // an application slot: one baked image
// The two persistent stores, and they are separate because they have separate
// LIFETIMES -- see doc/EEPROM.md. A patch is program text belonging to one
// firmware and is dropped when the fingerprint moves; a setting is keyed by
// name and outlives a reflash. Different lifetimes, and different rewrite
// rates: a tuning is written far more often than a patch, so it wants its own
// erase unit rather than sharing one.
#define CSP_REG_PATCH   2   // the EEPROM patch: declarations and rules
#define CSP_REG_STORE   3   // settings: name-keyed values

// A named span of sectors. Inclusive at both ends, because that is how a data
// sheet and an IAP prepare/erase command both talk about them -- `8..9` is two
// sectors, and an exclusive end would make every board file read `8..10`.
//
// A/B is a NAMING CONVENTION over CSP_REG_APP, not a third field: an app region
// whose name ends in `A` or `B` is one slot of the application named by the rest
// of it. That is what lets one mechanism cover all three layouts a board might
// choose --
//
//     runtime A B store                       one app, two slots (fallback)
//     runtime App1 App2 store                 two apps, no fallback
//     runtime App1A App1B App2A App2B store   two apps, two slots each
//
// -- and the choice between them is how many sectors the part has to spend.
// Fallback costs a slot; two apps cost a slot. You do not get both for free.
typedef struct {
    const char* name;
    uint8_t     first;
    uint8_t     last;
    uint8_t     kind;    // CSP_REG_*
} csp_region_t;

// One part, with its regions. `ram` is bytes and it is NOT optional: there is no
// freeRam() here the way there is under Arduino, so the arena has to be sized
// from what the part is known to have.
typedef struct {
    const char*         name;     // "lpc2129"
    csp_sectors_t       flash;
    const csp_region_t* region;
    uint8_t             nregion;
    // RAM. Bytes and address, and NOT optional: there is no freeRam() here the
    // way there is under Arduino, so the arena has to be sized from what the
    // part is known to have. `reserve` is what the part keeps at the bottom --
    // 64 bytes of remapped vectors on an LPC2129 -- which the linker must not
    // hand out and which the ld script used to state on its own.
    uint32_t            ram;
    uint32_t            ram_base;
    uint32_t            ram_reserve;
    // What the linker script has to name. NOT the same across families: an
    // ARM7 vector table is eight branch words in `.vectors` reached at
    // ENTRY(_start), a Cortex-M one is a pointer array in `.isr_vector` whose
    // first two entries are the stack top and ResetISR. Getting this from the
    // family is what stops the generated script from being right for one
    // architecture and silently empty for the other -- a --gc-sections link
    // with the wrong entry symbol produces a 0-byte image, no error.
    const char*         entry;    // ENTRY() symbol -- see gen_chips.erl --ld
    const char*         vectors;  // input section holding the vector table
} csp_device_t;


// The part this build is for. Supplied by the board (or by the host harness).
extern const csp_device_t* csp_device(void);

// --- geometry (pure; no backend, no hardware) ------------------------------

// Byte offset of sector `s` from flash base, and its size. Both answer 0 for a
// sector past the end rather than walking off the table.
extern uint32_t csp_sector_offset(const csp_sectors_t* f, uint8_t s);
extern uint32_t csp_sector_size(const csp_sectors_t* f, uint8_t s);

// Which sector holds byte offset `off`, or 0xff when it is past the flash.
extern uint8_t  csp_sector_of(const csp_sectors_t* f, uint32_t off);

// The region called `name` (length-counted, so it works on a token), or NULL.
extern const csp_region_t* csp_region_find(const csp_device_t* d,
					   const char* name, int len);

// Offset from flash base, and total bytes. A region whose sectors do not exist
// answers 0 bytes -- a board file naming a sector the part does not have gets
// "does not fit" rather than a write into nowhere.
extern uint32_t csp_region_offset(const csp_device_t* d, const csp_region_t* r);
extern uint32_t csp_region_size(const csp_device_t* d, const csp_region_t* r);

// --- backend (one per target; the host writes to a file) --------------------

#define CSP_FLASH_OK        0
#define CSP_FLASH_ERR      -1   // the backend said no
#define CSP_FLASH_TOOBIG   -2   // more bytes than the region holds
#define CSP_FLASH_NOREGION -3   // no such region
// Refused before anything was erased: the region is the one way back.
#define CSP_FLASH_PROTECTED -4

// May this region be written at all?
//
// Three regions are refused whatever the caller asked for, and they are the same
// rule seen from three sides: NEVER ERASE THE ONLY WAY BACK.
//
//   RUNTIME  -- the code doing the writing lives there. The erase does not
//               fail, it STOPS, mid-sector, and the part needs a programmer.
//   the running image -- erasing what you are executing is the same thing,
//               only later.
//   the last FAILSAFE -- it exists to be the way back from a bad application.
//               Overwriting it is detectable only afterwards, and afterwards
//               the thing that would have detected it is gone.
//
// It lives here rather than in whatever command calls it so that a future
// caller cannot route around it by forgetting.
extern int csp_flash_writable(const csp_device_t* d, const csp_region_t* r);

// Is `addr` (in the part's address space) inside this region? Used to refuse an
// erase of the slot the running image is executing from -- see csp_region_holds.
extern int csp_region_holds(const csp_device_t* d, const csp_region_t* r,
			    uint32_t addr);

// Erase sectors first..last inclusive.
extern int csp_flash_erase(uint8_t first, uint8_t last);
// Write `len` bytes at `off` from flash base. The backend is responsible for
// whatever alignment its IAP demands; callers pass byte counts.
extern int csp_flash_write(uint32_t off, const void* data, uint32_t len);
// Read back, for verification. Flash is memory-mapped on the parts we care
// about, but a host file is not, so it goes through a call like the rest.
extern int csp_flash_read(uint32_t off, void* data, uint32_t len);

// Erase the region and write `data` into it. The one operation the rest of the
// system wants: "put this image in slot B".
extern int csp_flash_put(const csp_device_t* d, const csp_region_t* r,
			 const void* data, uint32_t len);

// The application a slot belongs to, and which slot it is.
//
//   "A"      -> app "",     slot 'A'      the only application, first slot
//   "App1B"  -> app "App1", slot 'B'
//   "App2"   -> app "App2", slot 0        no fallback: one slot, no suffix
//
// Returns 0 for a region that is not CSP_REG_APP. `*slot` is 0 when the name
// carries no suffix, which is what tells "this application has one slot" from
// "this is slot A of it".
extern int csp_region_app(const csp_region_t* r, const char** app, int* applen,
			  char* slot);

// Patch the boot checksum into a runtime image so the eight exception vectors
// sum to zero. Without it the boot ROM enters ISP instead of running the
// program -- silently, with no output and nothing a debugger can see.
//
// WHICH word is the checksum differs: vector 5 (0x14) on ARM7, vector 7 (0x1C)
// on Cortex-M, where vector 5 is BusFault_Handler. The family is detected from
// the table itself; see csp_flash.c.
extern uint32_t csp_lpc_checksum(void* image);
extern uint32_t csp_lpc2000_checksum(void* image);   // the old name

// Host backend only: which file stands in for the part's flash, and which part
// to pretend to be (the three layouts live in csp_devices.c).
extern void csp_flash_host_file(const char* path);
extern void csp_device_set(const csp_device_t* d);
// One of the host layouts by name: "ab", "apps", "full".
extern const csp_device_t* csp_device_by_name(const char* name);

#endif
