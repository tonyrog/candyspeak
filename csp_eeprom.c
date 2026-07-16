// csp_eeprom.c - Binary eeprom save/load (shared between platforms)
#include "csp.h"
#include <string.h>

// Binary eeprom format header. Only the RAM patch area is persisted -- ROM runs
// from flash. The rom_* fields fingerprint the firmware the patches were made
// against, so a load can reject patches saved for a different ROM.
typedef struct {
    uint8_t  magic[4];   // "CSP\0"
    uint16_t version;    // format version
    uint16_t rom_nd;     // firmware fingerprint: ROM decl/instr/string sizes
    uint16_t rom_nn;
    uint16_t rom_strp;
    uint16_t ram_nd;     // RAM patch sizes (counts above the ROM base)
    uint16_t ram_nn;
    uint16_t ram_strp;
    uint16_t ram_ns;     // runtime state additions (above the ROM/init baseline)
    uint16_t nq;         // number of objects
} eeprom_header_t;

#define EEPROM_MAGIC "CSP"
#define EEPROM_VERSION 4   // v4: persist runtime state-table additions (ram_ns)

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
    uint16_t ram_nd   = st->ps.nd   - st->rom_nd;   // RAM patch counts
    uint16_t ram_nn   = st->ps.nn   - st->rom_nn;
    uint16_t ram_strp = st->ps.strp - st->rom_strp;
    uint16_t ram_ns   = st->ps.ns   - st->rom_ns;

    if (csp_eeprom_open_write() < 0)
	return -1;

    memcpy(hdr.magic, EEPROM_MAGIC, 4);
    hdr.version  = EEPROM_VERSION;
    hdr.rom_nd   = st->rom_nd;
    hdr.rom_nn   = st->rom_nn;
    hdr.rom_strp = st->rom_strp;
    hdr.ram_nd   = ram_nd;
    hdr.ram_nn   = ram_nn;
    hdr.ram_strp = ram_strp;
    hdr.ram_ns   = ram_ns;
    hdr.nq       = st->ps.nq;

    if (csp_eeprom_write(&hdr, sizeof(hdr)) < 0)
	goto error;
    // Only the RAM patch area (ram_*[0..delta)); ROM stays in flash.
    if (csp_eeprom_write(st->ram_str, ram_strp) < 0)
	goto error;
    // decl[] grows DOWN from the pool top, so the RAM decls are not contiguous in
    // save order -- walk them through the accessor. The stored format is unchanged
    // (logical order 0..ram_nd-1); only the memory walk differs. instr[] still
    // grows up, so it stays one block write.
    {
	uint16_t i;
	for (i = 0; i < ram_nd; i++)
	    if (csp_eeprom_write(ram_decl_at(st, st->rom_nd + i),
				 sizeof(csp_decl_t)) < 0)
		goto error;
    }
    if (csp_eeprom_write(st->ram_instr, sizeof(csp_instr_t) * ram_nn) < 0)
	goto error;
    // Runtime state-table additions (name offsets already covered by ram_str).
    if (ram_ns &&
	csp_eeprom_write(&st->states[st->rom_ns], sizeof(state_t) * ram_ns) < 0)
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

    // Rebuild the ROM baseline, then load the RAM patches on top of it.
    reactive = st->reactive;
    csp_rt_init(st, reactive);
    csp_load_rom(st);   // rebase ps.* to the ROM sizes (no-op if no firmware)

    // reject patches saved against a different firmware ROM
    if ((hdr.rom_nd != st->rom_nd) || (hdr.rom_nn != st->rom_nn) ||
	(hdr.rom_strp != st->rom_strp))
	goto error;

    // Read the RAM patch area into the RAM-local slots
    if (csp_eeprom_read(st->ram_str, hdr.ram_strp) < 0)
	goto error;
    // decl[] grows DOWN (see csp_eeprom_save): place them one at a time, or a
    // block read would write straight past the top of the pool.
    {
	uint16_t i;
	for (i = 0; i < hdr.ram_nd; i++)
	    if (csp_eeprom_read(ram_decl_at(st, st->rom_nd + i),
				sizeof(csp_decl_t)) < 0)
		goto error;
    }
    if (csp_eeprom_read(st->ram_instr, sizeof(csp_instr_t) * hdr.ram_nn) < 0)
	goto error;
    // Runtime state additions land above the baseline (rom_ns = INIT/NORMAL or
    // the restored ROM table); their name strings came in with ram_str above.
    if (hdr.ram_ns &&
	csp_eeprom_read(&st->states[st->rom_ns], sizeof(state_t) * hdr.ram_ns) < 0)
	goto error;

    // Logical counts = ROM base + RAM patch
    st->ps.strp = st->rom_strp + hdr.ram_strp;
    st->ps.nd   = st->rom_nd   + hdr.ram_nd;
    st->ps.nn   = st->rom_nn   + hdr.ram_nn;
    st->ps.ns   = st->rom_ns   + hdr.ram_ns;
    st->ps.nq   = hdr.nq;

    csp_eeprom_close();
    csp_rt_start(st);   // initialise values (ROM + RAM leaves)
    return 0;

error:
    csp_eeprom_close();
    return -1;
}

// Get save size in bytes (RAM patch area only)
int csp_eeprom_size(csp_rt_t* st)
{
    return sizeof(eeprom_header_t) +
	   (st->ps.strp - st->rom_strp) +
	   sizeof(csp_decl_t) * (st->ps.nd - st->rom_nd) +
	   sizeof(csp_instr_t) * (st->ps.nn - st->rom_nn) +
	   sizeof(state_t) * (st->ps.ns - st->rom_ns);
}

