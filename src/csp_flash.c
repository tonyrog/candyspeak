// Flash geometry: sector tables, named regions, and the one write that uses
// them. See csp_flash.h for why any of this exists.
//
// No hardware here. Everything below is arithmetic over the part's sector
// table, which is what lets the host run it against a file -- and the
// arithmetic is the part that gets non-uniform sectors wrong.

#include "csp_flash.h"
#include <string.h>
#include <stdio.h>

uint32_t csp_sector_size(const csp_sectors_t* f, uint8_t s)
{
    if (s >= f->n)
	return 0;
    return f->size[s];
}

uint32_t csp_sector_offset(const csp_sectors_t* f, uint8_t s)
{
    uint32_t off = 0;
    uint8_t i;
    // Summed, not multiplied. The whole reason this is a function.
    for (i = 0; (i < s) && (i < f->n); i++)
	off += f->size[i];
    return off;
}

uint8_t csp_sector_of(const csp_sectors_t* f, uint32_t off)
{
    uint32_t at = 0;
    uint8_t i;
    for (i = 0; i < f->n; i++) {
	at += f->size[i];
	if (off < at)
	    return i;
    }
    return 0xff;
}

const csp_region_t* csp_region_find(const csp_device_t* d,
				    const char* name, int len)
{
    uint8_t i;
    for (i = 0; i < d->nregion; i++) {
	const char* rn = d->region[i].name;
	// Length-counted on both sides: the caller may hand over a token that
	// is not nul-terminated, and "A" must not match "App1".
	if (((int)strlen(rn) == len) && (memcmp(rn, name, len) == 0))
	    return &d->region[i];
    }
    return NULL;
}

uint32_t csp_region_offset(const csp_device_t* d, const csp_region_t* r)
{
    return csp_sector_offset(&d->flash, r->first);
}

uint32_t csp_region_size(const csp_device_t* d, const csp_region_t* r)
{
    uint32_t n = 0;
    uint8_t s;
    // A region naming sectors the part does not have measures 0, so a board
    // file with `A 8..9` on an eight-sector part gets "does not fit" instead of
    // a write past the end. csp_sector_size answers 0 past the table.
    if (r->first > r->last)
	return 0;
    for (s = r->first; s <= r->last; s++) {
	uint32_t sz = csp_sector_size(&d->flash, s);
	if (sz == 0)
	    return 0;
	n += sz;
    }
    return n;
}

int csp_region_app(const csp_region_t* r, const char** app, int* applen,
		   char* slot)
{
    int n;

    *app = NULL; *applen = 0; *slot = 0;
    if (r->kind != CSP_REG_APP)
	return 0;
    n = (int)strlen(r->name);
    // A trailing A or B is the slot; everything before it names the
    // application. `A` alone leaves an empty name, which is the right answer:
    // a board with one application does not have to invent a word for it.
    if ((n > 0) && ((r->name[n-1] == 'A') || (r->name[n-1] == 'B'))) {
	*slot = r->name[n-1];
	n--;
    }
    *app = r->name;
    *applen = n;
    return 1;
}

// Is there a VALID image with this role anywhere in flash, other than in `skip`?
//
// Read straight out of the region rather than from the linked-in registry: the
// registry lists what this firmware carries, and the question here is what is
// PROGRAMMED -- an image put into a slot at run time is not in the registry and
// is exactly the one that matters.
static int role_elsewhere(const csp_device_t* d, const csp_region_t* skip,
			  uint8_t role)
{
    uint8_t i;

    for (i = 0; i < d->nregion; i++) {
	const csp_region_t* r = &d->region[i];
	csp_image_header_t h;
	if (r == skip || r->kind != CSP_REG_APP)
	    continue;
	if (csp_region_size(d, r) < sizeof(h))
	    continue;
	// Through the backend, not a pointer into flash. On a part it is memory
	// mapped and a cast would work; on the host it is a FILE and flash.base
	// is 0, so the cast is a null dereference. One path for both.
	if (csp_flash_read(csp_region_offset(d, r), &h, sizeof(h)) != CSP_FLASH_OK)
	    continue;
	// Its own CRC, not just the role byte: erased flash is 0xFF in every
	// field, and 0xFF is not a role anyone declared -- but a half-written
	// slot can hold anything, and a torn image must not count as a way back.
	if (h.role != role)
	    continue;
	if (csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0) != h.crc_hdr)
	    continue;
	return 1;
    }
    return 0;
}

// Does this region contain that ADDRESS, in the part's own address space?
//
// For the one question the map cannot answer on its own: is this slot the one
// holding the image we are executing. On a part with memory-mapped flash the
// program runs IN PLACE out of its slot, so erasing that slot pulls the program
// out from under the interpreter -- the same failure csp_flash_writable refuses
// for the runtime region, one level up.
//
// Geometry, so it lives here and can be tested without a board. An address
// outside the flash entirely -- which is every address on the host, where the
// "flash" is a file and no image is executed from it -- answers 0.
int csp_region_holds(const csp_device_t* d, const csp_region_t* r, uint32_t addr)
{
    uint32_t off, len;

    if ((d == NULL) || (r == NULL))
	return 0;
    if (addr < d->flash.base)
	return 0;
    addr -= d->flash.base;
    off = csp_region_offset(d, r);
    len = csp_region_size(d, r);
    if (len == 0)
	return 0;
    return (addr >= off) && (addr < (off + len));
}

int csp_flash_writable(const csp_device_t* d, const csp_region_t* r)
{
    csp_image_header_t h;

    if ((d == NULL) || (r == NULL))
	return CSP_FLASH_NOREGION;

    // The code doing the writing lives here. Nothing else needs saying.
    if (r->kind == CSP_REG_RUNTIME)
	return CSP_FLASH_PROTECTED;

    // Only app slots can hold an image, so only they can hold the LAST one.
    if (r->kind != CSP_REG_APP)
	return CSP_FLASH_OK;
    if (csp_region_size(d, r) < sizeof(h))
	return CSP_FLASH_OK;

    if (csp_flash_read(csp_region_offset(d, r), &h, sizeof(h)) != CSP_FLASH_OK)
	return CSP_FLASH_OK;             // unreadable: nothing in there to lose
    if (csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0) != h.crc_hdr)
	return CSP_FLASH_OK;             // nothing valid in there to lose

    // A failsafe may be replaced, but not while it is the only one. Refusing
    // outright would make failsafe unupdatable; allowing it freely makes the
    // last one disappear on a typo.
    if ((h.role == CSP_ROLE_FAILSAFE) &&
	!role_elsewhere(d, r, CSP_ROLE_FAILSAFE))
	return CSP_FLASH_PROTECTED;

    return CSP_FLASH_OK;
}

int csp_flash_put(const csp_device_t* d, const csp_region_t* r,
		  const void* data, uint32_t len)
{
    uint32_t room;
    int e;

    int guard;

    if (r == NULL)
	return CSP_FLASH_NOREGION;
    // BEFORE the size check and long before the erase. A caller that got the
    // region wrong should be told that, not told its data was too big.
    if ((guard = csp_flash_writable(d, r)) != CSP_FLASH_OK)
	return guard;
    room = csp_region_size(d, r);
    // Checked BEFORE the erase. Erasing and then discovering it does not fit
    // leaves the slot empty, which on an A/B part is the one state you cannot
    // recover from without a programmer.
    if ((room == 0) || (len > room))
	return CSP_FLASH_TOOBIG;
    if ((e = csp_flash_erase(r->first, r->last)) != CSP_FLASH_OK)
	return e;
    return csp_flash_write(csp_region_offset(d, r), data, len);
}

// (The linker script generator lived here. It is utils/gen_chips.erl --ld now:
// the answer is arithmetic over chips/<vendor>/*.terms and nothing at RUN time
// needs it, so a C table of every part existed only to serve a command-line
// flag -- 21K of generated source to be regenerated, linked and kept in step
// for one question a script can answer from the source directly.)

// --- boot checksum -----------------------------------------------------------

// The boot ROM adds the exception vectors and, if the sum is not zero, decides
// the flash holds no valid program and enters ISP. The part then comes up
// silent with nothing to see -- no fault, no output, and every debugger says
// the code looks fine.
//
// WHICH WORD IS THE CHECKSUM IS NOT THE SAME ON BOTH FAMILIES:
//
//   ARM7 (LPC2000)   vector 5, offset 0x14 -- the reserved slot between the
//                    data abort and IRQ entries
//   Cortex-M (17xx)  vector 7, offset 0x1C -- and vector 5 there is
//                    BusFault_Handler, a real handler
//
// Writing the ARM7 word into a Cortex-M image therefore destroys the bus fault
// handler AND leaves the actual checksum at zero. It can still boot -- if the
// remaining vectors happen to sum to zero, vector 7 being zero is correct by
// accident -- which is exactly the kind of luck that holds until it does not.
//
// The family is detected rather than passed, because this tool is handed a bare
// .bin with nothing else to go on. A Cortex-M table starts with an initial
// STACK POINTER (a RAM address) followed by a reset handler with the Thumb bit
// set; an ARM7 table starts with a branch instruction. Nothing else looks like
// either.
static int is_cortex_m(const uint32_t* v)
{
    uint32_t top = v[0] >> 28;
    // v[0] in a RAM region and v[1] odd. An ARM7 vector 0 is `ldr pc, [pc,#n]`,
    // which is 0xE59FFxxx -- neither.
    return ((v[1] & 1u) != 0) && ((top == 0x1) || (top == 0x2));
}

uint32_t csp_lpc_checksum(void* image)
{
    uint32_t* v = (uint32_t*)image;
    int slot = is_cortex_m(v) ? 7 : 5;
    uint32_t sum = 0;
    int i;

    v[slot] = 0;                    // whatever placeholder was there
    for (i = 0; i < 8; i++)
	sum += v[i];
    v[slot] = (uint32_t)(0u - sum);
    return v[slot];
}

// The old name. Kept because it says LPC2000 and that is now only half true.
uint32_t csp_lpc2000_checksum(void* image) { return csp_lpc_checksum(image); }
