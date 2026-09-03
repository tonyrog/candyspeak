// What csp_flash_put must REFUSE.
//
// Three regions are protected, and they are one rule from three sides: never
// erase the only way back.
//
//   runtime        -- the code doing the writing lives there. The erase does
//                     not fail, it stops mid-sector and the part needs a
//                     programmer.
//   the last failsafe -- it exists to be the way back from a bad application.
//
// The point of testing it here rather than trusting the caller is that the
// caller is the part that will be rewritten: a firmware-update mode, a future
// command, something not written yet. The guard has to hold for all of them,
// so it lives in csp_flash_put and this proves it is not routed around.
//
// Run from tests/repl.sh. Prints "ok, refused" on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"
#include "csp_flash.h"

extern const csp_device_t csp_dev_host_ab;

static int bad = 0;

static void ck(const char* what, int got, int want)
{
    if (got != want) {
	printf("%s: got %d, want %d\n", what, got, want);
	bad = 1;
    }
}

// Put a valid image header of `role` into a region.
static void plant(const csp_device_t* d, const char* name, uint8_t role)
{
    const csp_region_t* r = csp_region_find(d, name, (int)strlen(name));
    csp_image_header_t h;

    memset(&h, 0, sizeof(h));
    h.magic[0] = CSP_IMAGE_MAGIC0; h.magic[1] = CSP_IMAGE_MAGIC1;
    h.magic[2] = CSP_IMAGE_MAGIC2; h.magic[3] = CSP_IMAGE_MAGIC3;
    h.version = ROM_FORMAT_VERSION;
    h.role    = role;
    h.size    = sizeof(h);
    h.crc_hdr = csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0);
    csp_flash_erase(r->first, r->last);
    csp_flash_write(csp_region_offset(d, r), &h, sizeof(h));
}

static void wipe(const csp_device_t* d, const char* name)
{
    const csp_region_t* r = csp_region_find(d, name, (int)strlen(name));
    csp_flash_erase(r->first, r->last);
}

static int put(const csp_device_t* d, const char* name)
{
    static const uint8_t junk[16] = {0};
    return csp_flash_put(d, csp_region_find(d, name, (int)strlen(name)),
			 junk, sizeof(junk));
}

int main(void)
{
    const csp_device_t* d = &csp_dev_host_ab;   // runtime, A, B, store

    csp_device_set(d);
    csp_flash_host_file("tmp/flash_guard.bin");

    // The runtime is refused whatever is in it -- there is nothing to inspect,
    // the objection is that the code is executing from there.
    ck("runtime", put(d, "runtime"), CSP_FLASH_PROTECTED);

    // Settings are not an image and are not a way back.
    wipe(d, "A"); wipe(d, "B");
    ck("store", put(d, "store"), CSP_FLASH_OK);

    // Empty slots: nothing to lose.
    ck("empty A", put(d, "A"), CSP_FLASH_OK);

    // An ordinary program is replaceable. That is the whole point of a slot.
    wipe(d, "A"); wipe(d, "B");
    plant(d, "A", CSP_ROLE_ROM);
    ck("A holds a ROM image", put(d, "A"), CSP_FLASH_OK);

    // The LAST failsafe is not.
    wipe(d, "A"); wipe(d, "B");
    plant(d, "A", CSP_ROLE_FAILSAFE);
    ck("A holds the only failsafe", put(d, "A"), CSP_FLASH_PROTECTED);

    // With a second one in B, A may be replaced: a failsafe has to be
    // updatable, just never down to zero.
    plant(d, "B", CSP_ROLE_FAILSAFE);
    ck("A with a failsafe also in B", put(d, "A"), CSP_FLASH_OK);

    // And now B is the last one.
    wipe(d, "A");
    plant(d, "B", CSP_ROLE_FAILSAFE);
    ck("B is now the only failsafe", put(d, "B"), CSP_FLASH_PROTECTED);

    // A TORN image does not count as a way back. Half-written flash can hold
    // anything, including a byte that reads as the failsafe role -- so the
    // header's own CRC decides, not the role field alone.
    wipe(d, "A"); wipe(d, "B");
    plant(d, "B", CSP_ROLE_FAILSAFE);
    {
	csp_image_header_t h;
	const csp_region_t* rb = csp_region_find(d, "B", 1);
	csp_flash_read(csp_region_offset(d, rb), &h, sizeof(h));
	h.crc_hdr ^= 0xFFFF;                       // corrupt it
	csp_flash_erase(rb->first, rb->last);
	csp_flash_write(csp_region_offset(d, rb), &h, sizeof(h));
    }
    plant(d, "A", CSP_ROLE_FAILSAFE);
    ck("A is last: B's failsafe is torn", put(d, "A"), CSP_FLASH_PROTECTED);

    if (!bad)
	printf("ok, refused\n");
    return bad;
}
