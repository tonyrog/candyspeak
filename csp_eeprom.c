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
extern int csp_eeprom_open_read(void);
extern int csp_eeprom_open_write(void);
extern void csp_eeprom_close(void);
extern int csp_eeprom_read(void* buf, size_t len);
extern int csp_eeprom_write(const void* buf, size_t len);

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
    if (csp_eeprom_write(st->str, st->ps.strp) < 0)
	goto error;

    // Write declarations
    if (csp_eeprom_write(st->decl, sizeof(csp_decl_t) * st->ps.nd) < 0)
	goto error;

    // Write instructions
    if (csp_eeprom_write(st->instr, sizeof(csp_instr_t) * st->ps.nn) < 0)
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

    // Read string table
    if (csp_eeprom_read(st->str, hdr.strp) < 0)
	goto error;

    // Read declarations
    if (csp_eeprom_read(st->decl, sizeof(csp_decl_t) * hdr.nd) < 0)
	goto error;

    // Read instructions
    if (csp_eeprom_read(st->instr, sizeof(csp_instr_t) * hdr.nn) < 0)
	goto error;

    // Restore parse state
    st->ps.nn = hdr.nn;
    st->ps.nd = hdr.nd;
    st->ps.nq = hdr.nq;
    st->ps.strp = hdr.strp;

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
