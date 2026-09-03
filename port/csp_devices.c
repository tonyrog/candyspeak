// The part this build is for, and the host stand-ins for it.
//
// csp_flash.c is pure arithmetic over a sector table and knows no hardware;
// this is the table. A board supplies its own and calls csp_device_set from
// startup; the host supplies the three below, which exist so the geometry can
// be tested against a file instead of against a board.
//
// RECONSTRUCTED from tests/flash_geom.c, which asserts every offset and size
// here. That is what a test that states a layout rather than a checksum buys:
// the file it exercises can be rebuilt from it. If a number below and a number
// in that test ever disagree, the TEST is the source.

#include <string.h>
#include "csp_flash.h"

// --- the host part -----------------------------------------------------------
//
// Not a real chip. Sized to make the arithmetic interesting: four small sectors
// then a run of big ones, so an offset cannot be derived by multiplication and
// csp_sector_offset has to walk the table.
//
//   0..3   1K each   ->      0 ..  4095
//   4..7   8K each   ->   4096 .. 36863
//   8      2K        ->  36864 .. 38911
static const uint32_t host_sectors[] = {
    1024, 1024, 1024, 1024,
    8192, 8192, 8192, 8192,
    2048
};

#define HOST_FLASH { 0, host_sectors, 9 }
#define HOST_RAM    16384, 0x40000000, 64

// The names a generated linker script needs. The host links nothing, but the
// fields are not optional and a NULL here would be a trap for whoever first
// asks a host device for them.
#define HOST_LINK   "_start", ".vectors"

// One application, TWO SLOTS. The fallback layout: a failed update leaves the
// other slot bootable, and it costs a slot to have that.
static const csp_region_t host_ab_regions[] = {
    { "runtime", 0, 3, CSP_REG_RUNTIME },   //     0 ..  4095   4K
    { "A",       4, 5, CSP_REG_APP     },   //  4096 .. 20479  16K
    { "B",       6, 7, CSP_REG_APP     },   // 20480 .. 36863  16K
    { "store",   8, 8, CSP_REG_STORE   }    // 36864 .. 38911   2K
};

const csp_device_t csp_dev_host_ab = {
    "host-ab", HOST_FLASH,
    host_ab_regions, (uint8_t)(sizeof(host_ab_regions)/sizeof(host_ab_regions[0])),
    HOST_RAM, HOST_LINK
};

// TWO APPLICATIONS, one slot each. The same sectors spent the other way: two
// programs, and an interrupted update loses the one being written.
static const csp_region_t host_apps_regions[] = {
    { "runtime", 0, 3, CSP_REG_RUNTIME },
    { "App1",    4, 5, CSP_REG_APP     },   //  4096 .. 20479  16K
    { "App2",    6, 7, CSP_REG_APP     },   // 20480 .. 36863  16K
    { "store",   8, 8, CSP_REG_STORE   }
};

const csp_device_t csp_dev_host_apps = {
    "host-apps", HOST_FLASH,
    host_apps_regions, (uint8_t)(sizeof(host_apps_regions)/sizeof(host_apps_regions[0])),
    HOST_RAM, HOST_LINK
};

// Two applications with two slots each -- everything, at half the slot size.
// The `A`/`B` suffix is the naming convention csp_region_app decodes, so these
// are App1 slot A, App1 slot B, and so on.
static const csp_region_t host_full_regions[] = {
    { "runtime", 0, 3, CSP_REG_RUNTIME },
    { "App1A",   4, 4, CSP_REG_APP     },   //  4096 .. 12287   8K
    { "App1B",   5, 5, CSP_REG_APP     },   // 12288 .. 20479   8K
    { "App2A",   6, 6, CSP_REG_APP     },   // 20480 .. 28671   8K
    { "App2B",   7, 7, CSP_REG_APP     },   // 28672 .. 36863   8K
    { "store",   8, 8, CSP_REG_STORE   }
};

const csp_device_t csp_dev_host_full = {
    "host-full", HOST_FLASH,
    host_full_regions, (uint8_t)(sizeof(host_full_regions)/sizeof(host_full_regions[0])),
    HOST_RAM, HOST_LINK
};

// --- which one this build is using -------------------------------------------
//
// Held in a variable rather than chosen by #if: the host harness switches
// between the three at run time to exercise all of them in one binary, and a
// board sets its own once from startup.
static const csp_device_t* active = &csp_dev_host_ab;

// Pick one by name. For the host harness and its --part option: the three
// layouts differ only in how the same sectors are spent, and the bugs worth
// finding are the ones that show up in one arrangement and not another.
const csp_device_t* csp_device_by_name(const char* name)
{
    if (name == NULL) return NULL;
    if (strcmp(name, "ab")   == 0) return &csp_dev_host_ab;
    if (strcmp(name, "apps") == 0) return &csp_dev_host_apps;
    if (strcmp(name, "full") == 0) return &csp_dev_host_full;
    return NULL;
}

const csp_device_t* csp_device(void)
{
    return active;
}

void csp_device_set(const csp_device_t* d)
{
    if (d != NULL)
	active = d;
}
