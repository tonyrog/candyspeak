// The flash geometry and the three region layouts a board can choose.
//
// The host part is 1K,1K,1K,1K, 8K,8K,8K,8K, 1K,1K -- deliberately NOT uniform,
// because every offset here is a running sum and a `sector * size` shortcut
// would pass on a part whose sectors were all the same. The interesting bytes
// are the ones on the step from small sectors to big ones.
//
// The same flash carries three region maps, which is the real choice: four
// application sectors buy either one app with a fallback slot, or two apps with
// none, or two apps with half-size slots each.

#include "csp_flash.h"
#include <stdio.h>
#include <string.h>

extern const csp_device_t csp_dev_host_ab;
extern const csp_device_t csp_dev_host_apps;
extern const csp_device_t csp_dev_host_full;

static int fails = 0;

static void ck(const char* what, unsigned long want, unsigned long got)
{
    if (want == got) {
	printf("  PASS %s\n", what);
    } else {
	printf("  FAIL %s: want %lu, got %lu\n", what, want, got);
	fails++;
    }
}

// A region's offset and size in one call, so a layout reads as a table.
static void ck_region(const csp_device_t* d, const char* name,
		      unsigned long off, unsigned long size)
{
    const csp_region_t* r = csp_region_find(d, name, (int)strlen(name));
    char what[64];
    if (r == NULL) {
	printf("  FAIL %s/%s: no such region\n", d->name, name);
	fails++;
	return;
    }
    snprintf(what, sizeof(what), "%s/%s at %lu, %lu bytes", d->name, name,
	     off, size);
    if ((csp_region_offset(d, r) == off) && (csp_region_size(d, r) == size))
	printf("  PASS %s\n", what);
    else {
	printf("  FAIL %s: got %lu, %lu\n", what,
	       (unsigned long)csp_region_offset(d, r),
	       (unsigned long)csp_region_size(d, r));
	fails++;
    }
}

// The application a slot belongs to, spelled out.
static void ck_app(const csp_device_t* d, const char* name,
		   const char* want_app, char want_slot)
{
    const csp_region_t* r = csp_region_find(d, name, (int)strlen(name));
    const char* app;
    int applen;
    char slot;
    char what[64];

    snprintf(what, sizeof(what), "%s -> app \"%s\" slot %c", name, want_app,
	     want_slot ? want_slot : '-');
    if ((r == NULL) || !csp_region_app(r, &app, &applen, &slot)) {
	printf("  FAIL %s: not an app region\n", what);
	fails++;
	return;
    }
    if ((applen == (int)strlen(want_app)) &&
	(memcmp(app, want_app, applen) == 0) && (slot == want_slot))
	printf("  PASS %s\n", what);
    else {
	printf("  FAIL %s: got \"%.*s\" slot %c\n", what, applen, app,
	       slot ? slot : '-');
	fails++;
    }
}

int main(void)
{
    const csp_device_t* d = &csp_dev_host_ab;
    const csp_sectors_t* f = &d->flash;
    const csp_region_t* r;
    uint8_t buf[64];

    printf("flash geometry:\n");

    // Offsets are running sums: 4 x 1K, then the 8K run.
    ck("sector 0 at 0",       0,     csp_sector_offset(f, 0));
    ck("sector 4 at 4K",      4096,  csp_sector_offset(f, 4));
    ck("sector 5 at 12K",     12288, csp_sector_offset(f, 5));
    ck("sector 8 after them", 36864, csp_sector_offset(f, 8));

    ck("small sector is 1K",  1024,  csp_sector_size(f, 3));
    ck("big sector is 8K",    8192,  csp_sector_size(f, 4));
    ck("past the end is 0",   0,     csp_sector_size(f, 10));

    // The reverse map. The boundary bytes are where an off-by-one lives.
    ck("offset 0 -> sector 0",    0, csp_sector_of(f, 0));
    ck("last byte of sector 3",   3, csp_sector_of(f, 4095));
    ck("first byte of sector 4",  4, csp_sector_of(f, 4096));
    ck("last byte of sector 4",   4, csp_sector_of(f, 12287));
    ck("first byte of sector 5",  5, csp_sector_of(f, 12288));
    ck("past the flash",       0xff, csp_sector_of(f, 38912));

    // --- layout 1: one application, two slots -------------------------------
    printf("layout runtime/A/B/store:\n");
    ck_region(d, "runtime",     0,  4096);
    ck_region(d, "A",        4096, 16384);
    ck_region(d, "B",       20480, 16384);
    ck_region(d, "store",   36864,  2048);
    ck_app(d, "A", "", 'A');
    ck_app(d, "B", "", 'B');
    // store is not somewhere an application can be booted from, whatever its
    // name looks like.
    r = csp_region_find(d, "store", 5);
    {
	const char* a; int n; char sl;
	ck("store is not an app slot", 0, csp_region_app(r, &a, &n, &sl));
    }
    ck("no App1 in this layout", 1, csp_region_find(d, "App1", 4) == NULL);

    // --- layout 2: two applications, no fallback ----------------------------
    printf("layout runtime/App1/App2/store:\n");
    d = &csp_dev_host_apps;
    ck_region(d, "App1",  4096, 16384);
    ck_region(d, "App2", 20480, 16384);
    // No suffix: one slot each, and slot 0 is what says so.
    ck_app(d, "App1", "App1", 0);
    ck_app(d, "App2", "App2", 0);
    ck("no B in this layout", 1, csp_region_find(d, "B", 1) == NULL);

    // --- layout 3: two applications, two slots each -------------------------
    printf("layout runtime/App1A/App1B/App2A/App2B/store:\n");
    d = &csp_dev_host_full;
    ck_region(d, "App1A",  4096, 8192);
    ck_region(d, "App1B", 12288, 8192);
    ck_region(d, "App2A", 20480, 8192);
    ck_region(d, "App2B", 28672, 8192);
    ck_app(d, "App1A", "App1", 'A');
    ck_app(d, "App1B", "App1", 'B');
    ck_app(d, "App2A", "App2", 'A');
    ck_app(d, "App2B", "App2", 'B');
    // The same four sectors as layout 1 and 2 -- spent differently. Fallback
    // costs a slot; a second application costs a slot; you do not get both free.
    ck("half the slot size of A/B", 8192,
       csp_region_size(d, csp_region_find(d, "App1A", 5)));
    // "App1" must not match "App1A": a name is a whole name.
    ck("no partial match", 1, csp_region_find(d, "App1", 4) == NULL);

    // --- the backend --------------------------------------------------------
    printf("flash backend:\n");
    d = &csp_dev_host_ab;
    csp_device_set(d);
    csp_flash_host_file("tmp/flash_geom.bin");
    remove("tmp/flash_geom.bin");

    r = csp_region_find(d, "B", 1);
    ck("put fits", CSP_FLASH_OK, csp_flash_put(d, r, "hello", 5));
    memset(buf, 0, sizeof(buf));
    csp_flash_read(csp_region_offset(d, r), buf, 8);
    ck("wrote at the region's offset", 0, memcmp(buf, "hello", 5));
    ck("tail is erased, not zeroed", 0xff, buf[5]);

    // A is untouched: erasing B must not reach into its neighbour, which on a
    // part where a slot is one sector is the whole safety of A/B.
    memset(buf, 0, sizeof(buf));
    csp_flash_read(csp_region_offset(d, csp_region_find(d, "A", 1)), buf, 4);
    ck("the other slot is untouched", 0xff, buf[0]);

    // One byte past the region is refused, and refused BEFORE the erase -- the
    // image already there has to survive a write that does not fit.
    ck("too big is refused", CSP_FLASH_TOOBIG,
       csp_flash_put(d, r, buf, csp_region_size(d, r) + 1));
    memset(buf, 0, sizeof(buf));
    csp_flash_read(csp_region_offset(d, r), buf, 8);
    ck("refused write did not erase", 0, memcmp(buf, "hello", 5));

    // A region naming sectors the part does not have measures zero, so nothing
    // can go in it -- rather than a write past the end of the flash.
    {
	csp_region_t bad = { "bad", 8, 12, CSP_REG_APP };
	ck("region past the end is 0", 0, csp_region_size(d, &bad));
	ck("and refuses a write", CSP_FLASH_TOOBIG,
	   csp_flash_put(d, &bad, "x", 1));
    }
    ck("unknown region", CSP_FLASH_NOREGION, csp_flash_put(d, NULL, "x", 1));

    printf(fails ? "flash geometry: FAILED\n" : "flash geometry: ok\n");
    return fails ? 1 : 0;
}
