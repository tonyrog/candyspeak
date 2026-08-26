// Flash backend for the host: the part's flash is a FILE.
//
// The point is not to simulate flash faithfully -- it is to run the geometry,
// the region map and csp_flash_put against something that can be inspected
// byte by byte, on a machine with no board attached. What it does copy from
// real flash is the two properties that make bugs: erase works on whole
// SECTORS and sets them to 0xff, and a write does not erase first.

#include "csp_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* flash_file = "flash.bin";

void csp_flash_host_file(const char* path)
{
    flash_file = path;
}

// Total bytes the part's flash holds, from the sector table.
static uint32_t flash_bytes(void)
{
    const csp_device_t* d = csp_device();
    uint32_t n = 0;
    uint8_t i;
    for (i = 0; i < d->flash.n; i++)
	n += d->flash.size[i];
    return n;
}

// Open, creating a full-size erased image if it is not there yet. A short file
// would otherwise read back zeros where a fresh part reads 0xff, and "is this
// sector blank" is exactly the question a flash tool asks.
static FILE* flash_open(const char* mode)
{
    FILE* f = fopen(flash_file, mode);
    if (f == NULL) {
	uint32_t n = flash_bytes();
	uint32_t i;
	if ((f = fopen(flash_file, "wb")) == NULL)
	    return NULL;
	for (i = 0; i < n; i++)
	    fputc(0xff, f);
	fclose(f);
	f = fopen(flash_file, mode);
    }
    return f;
}

int csp_flash_erase(uint8_t first, uint8_t last)
{
    const csp_device_t* d = csp_device();
    FILE* f;
    uint8_t s;

    if ((first > last) || (last >= d->flash.n))
	return CSP_FLASH_ERR;
    if ((f = flash_open("r+b")) == NULL)
	return CSP_FLASH_ERR;
    for (s = first; s <= last; s++) {
	uint32_t off = csp_sector_offset(&d->flash, s);
	uint32_t n   = csp_sector_size(&d->flash, s);
	uint32_t i;
	if (fseek(f, (long)off, SEEK_SET) != 0) {
	    fclose(f);
	    return CSP_FLASH_ERR;
	}
	// 0xff, not 0. An erased cell is all ones, and code that looks for a
	// blank sector looks for that.
	for (i = 0; i < n; i++)
	    fputc(0xff, f);
    }
    fclose(f);
    return CSP_FLASH_OK;
}

int csp_flash_write(uint32_t off, const void* data, uint32_t len)
{
    FILE* f;
    if ((off + len) > flash_bytes())
	return CSP_FLASH_TOOBIG;
    if ((f = flash_open("r+b")) == NULL)
	return CSP_FLASH_ERR;
    if ((fseek(f, (long)off, SEEK_SET) != 0) ||
	(fwrite(data, 1, len, f) != len)) {
	fclose(f);
	return CSP_FLASH_ERR;
    }
    fclose(f);
    return CSP_FLASH_OK;
}

int csp_flash_read(uint32_t off, void* data, uint32_t len)
{
    FILE* f;
    if ((off + len) > flash_bytes())
	return CSP_FLASH_TOOBIG;
    if ((f = flash_open("rb")) == NULL)
	return CSP_FLASH_ERR;
    if ((fseek(f, (long)off, SEEK_SET) != 0) ||
	(fread(data, 1, len, f) != len)) {
	fclose(f);
	return CSP_FLASH_ERR;
    }
    fclose(f);
    return CSP_FLASH_OK;
}
