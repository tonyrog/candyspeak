// csp_eeprom.c - Binary eeprom save/load (shared between platforms)
#include "csp.h"
#include <string.h>

// Binary eeprom format header
typedef struct {
    uint8_t  magic[4];   // "CSP\0"
    uint16_t version;    // format version
    uint16_t nn;         // number of instructions
    uint16_t nd;         // number of declarations
    uint16_t nq;         // number of objects
    uint32_t strp;       // string table size
} eeprom_header_t;

#define EEPROM_MAGIC "CSP"
#define EEPROM_VERSION 1

// Platform stub functions - implement per platform
#if 0
static uint16_t calc_checksum(csp_rt_t* st)
{
    uint16_t sum = 0;
    uint8_t* p;
    size_t i;

    p = (uint8_t*)st->instr;
    for (i = 0; i < st->ps.nn * sizeof(csp_instr_t); i++)
        sum += p[i];

    p = (uint8_t*)st->decl;
    for (i = 0; i < st->ps.nd * sizeof(csp_decl_t); i++)
        sum += p[i];

    p = (uint8_t*)&st->str[st->ps.strp];
    for (i = 0; i < st->ps.strp; i++) 
        sum += p[i];
    return sum;
}
#endif

int csp_eeprom_clear(csp_rt_t* st)
{
    uint8_t invalid[2] = {0xff, 0xff};
    if (csp_eeprom_open_write() < 0)
	return -1;
    if (csp_eeprom_write(invalid, 2) < 0)
	return -1;
    csp_eeprom_close();
    return 0;
}

// Save state to eeprom (binary format)
int csp_eeprom_save(csp_rt_t* st)
{
    eeprom_header_t hdr;

    if (csp_eeprom_open_write() < 0)
	return -1;

    // Write header
    memcpy(hdr.magic, EEPROM_MAGIC, 4);
    hdr.version = EEPROM_VERSION;
    hdr.nn = st->ps.nn;
    hdr.nd = st->ps.nd;
    hdr.nq = st->ps.nq;
    hdr.strp = st->ps.strp;

    if (csp_eeprom_write(&hdr, sizeof(hdr)) < 0)
	goto error;

    // Write string table
    if (csp_eeprom_write(st->ram_str, st->ps.strp) < 0)
	goto error;

    // Write declarations
    if (csp_eeprom_write(st->ram_decl, sizeof(csp_decl_t) * st->ps.nd) < 0)
	goto error;

    // Write instructions
    if (csp_eeprom_write(st->ram_instr, sizeof(csp_instr_t) * st->ps.nn) < 0)
	goto error;

    csp_eeprom_close();
    return 0;

error:
    csp_eeprom_close();
    return -1;
}

// Load state from eeprom (binary format)
int csp_eeprom_load(csp_rt_t* st)
{
    eeprom_header_t hdr;
    int reactive;

    if (csp_eeprom_open_read() < 0)
	return -1;

    // Read and validate header
    if (csp_eeprom_read(&hdr, sizeof(hdr)) < 0)
	goto error;

    if (memcmp(hdr.magic, EEPROM_MAGIC, 4) != 0)
	goto error;

    if (hdr.version != EEPROM_VERSION)
	goto error;

    // Reset state but keep reactive flag
    reactive = st->reactive;
    csp_rt_init(st, 0, reactive);

    // Restore parse state
    st->ram.str_len = hdr.strp;
    st->ram.n_decl  = hdr.nd;
    st->ram.n_instr = hdr.nn;

    // Read string table
    if (csp_eeprom_read(st->ram_str, hdr.strp) < 0)
	goto error;

    // Read declarations
    if (csp_eeprom_read(st->ram_decl, sizeof(csp_decl_t) * hdr.nd) < 0)
	goto error;

    // Read instructions
    if (csp_eeprom_read(st->ram_instr, sizeof(csp_instr_t) * hdr.nn) < 0)
	goto error;

    csp_eeprom_close();

    // Initialize values
    csp_rt_start(st);

    return 0;

error:
    csp_eeprom_close();
    return -1;
}

// Get save size in bytes
int csp_eeprom_size(csp_rt_t* st)
{
    return sizeof(eeprom_header_t) +
	   st->ps.strp +
	   sizeof(csp_decl_t) * st->ps.nd +
	   sizeof(csp_instr_t) * st->ps.nn;
}

