// csp_eeprom.c - Binary eeprom save/load (shared between platforms)
#include "csp.h"
#include "csp_print.h"
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
    uint16_t rom_crc;    // firmware fingerprint (ROM instrs) -- see rom_fingerprint()
    uint16_t n_dis;      // rules the trailing #disable bitset was counted over
    uint16_t data_crc;   // CRC over the SAVED PAYLOAD itself -- see payload_crc()
} eeprom_header_t;

// In RODATA rather than a plain literal so it costs flash, not RAM, on AVR --
// read and written through ro_memcmp/ro_memcpy. 4 bytes, terminator included.
static rochar eeprom_magic[4] RODATA = "CSP";
#define EEPROM_MAGIC eeprom_magic
// v6: NAMEPOS_BITS widened the decl `name` field, which shifts every bitfield
//     after it in csp_decl_t -- a v5 save is binary-incompatible and would be
//     misread as garbage decls. The rom_crc fingerprint does NOT catch this: it
//     is over the ROM INSTRUCTIONS, whose layout did not change. So the version
//     is what rejects a stale save here, before ps.* is touched.
#define EEPROM_VERSION 7   // v7: data_crc (payload integrity); v6: NAMEPOS layout

// Bytes the #disable bitset occupies for a program with n rules. Rounded up to
// whole set_group_t words so the read/write is a straight memcpy of the front
// of st->dis_rule -- the bitset macros index by word, not by byte.
#define DIS_BYTES(n) \
    ((size_t)BITSET_GROUPS((n) > MAX_DIS_RULES ? MAX_DIS_RULES : (n)) \
     * sizeof(set_group_t))

// Fingerprint the firmware ROM a save was made against. The disable set stores
// rule NUMBERS, and a number only means something relative to a specific
// program; the rom_nd/rom_nn/rom_strp sizes say the firmware is the same SIZE
// but nothing about content, and rule 7 is a different rule the moment the
// content changes. A CRC over the loaded ROM instructions closes that gap.
// (Only the instructions -- enough to tell one firmware from another.)
#define rom_fingerprint(st) \
    csp_crc16(0xFFFF, (st)->rom_instr_p, \
	      (size_t)(st)->rom_nn * sizeof(csp_instr_t), 1)

// CRC over the RAM patch this save carries -- the payload's OWN integrity, as
// opposed to rom_fingerprint which identifies the firmware. Same machine writes
// and reads, so raw bytes are fine (no host/target question) and runtime-scratch
// fields are saved and restored verbatim -- no normalization. Folds the regions
// in write order (see csp_eeprom_save): str, decls, instrs, state additions. The
// #disable bitset is left out; it has its own consistency gate (n_dis).
static uint16_t payload_crc(csp_rt_t* st, uint16_t ram_nd, uint16_t ram_nn,
			    uint16_t ram_strp, uint16_t ram_ns)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;

    crc = csp_crc16(crc, st->ram_str, ram_strp, 0);
    for (i = 0; i < ram_nd; i++)
	crc = csp_crc16(crc, ram_decl_at(st, st->rom_nd + i),
			sizeof(csp_decl_t), 0);
    crc = csp_crc16(crc, st->ram_instr, (size_t)ram_nn * sizeof(csp_instr_t), 0);
    if (ram_ns)
	crc = csp_crc16(crc, &st->states[st->rom_ns],
			(size_t)ram_ns * sizeof(state_t), 0);
    return crc;
}

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
	goto error;

    ro_memcpy(hdr.magic, EEPROM_MAGIC, 4);
    hdr.version  = EEPROM_VERSION;
    hdr.rom_nd   = st->rom_nd;
    hdr.rom_nn   = st->rom_nn;
    hdr.rom_strp = st->rom_strp;
    hdr.ram_nd   = ram_nd;
    hdr.ram_nn   = ram_nn;
    hdr.ram_strp = ram_strp;
    hdr.ram_ns   = ram_ns;
    hdr.nq       = st->ps.nq;
    hdr.rom_crc  = rom_fingerprint(st);
    hdr.n_dis    = (uint16_t)csp_n_rules(st);
    hdr.data_crc = payload_crc(st, ram_nd, ram_nn, ram_strp, ram_ns);

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
    // The #disable set, last: one bitset over rule numbers covering ROM and RAM
    // alike (numbers run 1..r_rom through the ROM rules and on into the RAM
    // ones), so there is nothing to split by segment.
    if (hdr.n_dis &&
	csp_eeprom_write(st->dis_rule, DIS_BYTES(hdr.n_dis)) < 0)
	goto error;

    csp_eeprom_close();
    return 0;

error:
    csp_eeprom_close();
    csp_set_error(st, ERR_CANNOT_SAVE);
    return -1;
}

// Load state from eeprom (binary format)
int csp_eeprom_load(csp_rt_t* st)
{
    eeprom_header_t hdr;
    int reactive;
    int did_init = 0;   // csp_rt_init has run -> a failure must leave rebuilt state

    if (csp_eeprom_open_read() < 0)
	goto error;

    // Read and validate header
    if (csp_eeprom_read(&hdr, sizeof(hdr)) < 0)
	goto error;

    if (ro_memcmp(hdr.magic, EEPROM_MAGIC, 4) != 0)
	goto error;

    if (hdr.version != EEPROM_VERSION)
	goto error;

    // Rebuild the ROM baseline, then load the RAM patches on top of it.
    reactive = st->reactive;
    csp_rt_init(st, reactive);
    did_init = 1;       // from here a failure has torn down view/heap/tables
    csp_load_rom(st);   // rebase ps.* to the ROM sizes (no-op if no firmware)

    // reject patches saved against a different firmware ROM. rom_crc catches
    // the case the size fingerprint cannot: same shape, different content.
    // Whole save, not just the disable set -- the RAM patches reference ROM
    // decls by index, and those indices mean something else now.
    if ((hdr.rom_nd != st->rom_nd) || (hdr.rom_nn != st->rom_nn) ||
	(hdr.rom_strp != st->rom_strp) || (hdr.rom_crc != rom_fingerprint(st)))
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

    // Payload integrity: recompute the CRC over what we just read and compare to
    // what was baked at save. Catches a flipped storage cell independently of the
    // firmware fingerprint above. Mismatch -> error path restores the ROM
    // baseline, so a corrupt save never half-loads.
    if (payload_crc(st, hdr.ram_nd, hdr.ram_nn, hdr.ram_strp, hdr.ram_ns)
	!= hdr.data_crc)
	goto error;

    // Logical counts = ROM base + RAM patch
    st->ps.strp = st->rom_strp + hdr.ram_strp;
    st->ps.nd   = st->rom_nd   + hdr.ram_nd;
    st->ps.nn   = st->rom_nn   + hdr.ram_nn;
    st->ps.ns   = st->rom_ns   + hdr.ram_ns;
    st->ps.nq   = hdr.nq;

    // The #disable set. csp_rt_init above zeroed dis_rule, so a save without
    // one (n_dis == 0) simply leaves everything enabled. The rule count has to
    // agree with what the restored program actually has: if it does not, the
    // numbers address different rules than the ones that were disabled, so drop
    // the set and SAY SO rather than silence the wrong rules. The program
    // itself is fine -- only the overlay is suspect.
    if (hdr.n_dis) {
	index_t have = csp_n_rules(st);
	if (hdr.n_dis != (uint16_t)have) {
	    csp_print_lit("eeprom: disable set dropped (saved for ");
	    csp_print_uint(hdr.n_dis);
	    csp_print_lit(" rules, program has ");
	    csp_print_uint(have);
	    csp_print_line(")");
	}
	else if (csp_eeprom_read(st->dis_rule, DIS_BYTES(hdr.n_dis)) < 0)
	    goto error;
    }

    csp_eeprom_close();
    // csp_rebuild, NOT csp_rt_start. rebuild resets the middle bump allocator
    // (csp_mid_reset) that view/buf/heap/input/output/timer are all carved
    // from; csp_rt_init above zeroed mid_base and mid_end, so calling rt_start
    // on its own makes every csp_mid_alloc return NULL. rt_start DOES notice
    // and return -1 -- but that return was dropped here, so /load reported
    // success and the next cycle faulted on a null heap.
    //
    // The boot paths got away with it because main() and the .ino call
    // csp_rebuild themselves right afterwards. /load had nothing to repair it.
    //
    // The error is left as rebuild set it (ERR_TOO_MANY_DECLARATIONS when the
    // pool is genuinely full); ERR_CANNOT_LOAD would be a lie -- the image read
    // back fine, it is the layout that did not fit.
    if (csp_rebuild(st) < 0)
	return -1;
    return 0;

error:
    csp_eeprom_close();
    // A failure past the header may have inflated ps.* (line "Logical counts")
    // and half-written RAM slots. Restore the clean ROM baseline so the caller
    // runs the ROM program, not ROM + partially-loaded garbage. Resetting the
    // counts is enough: the stale ram_* content is never read once the indices
    // stop at rom_nd. (rom_* are the caller's, set by csp_load_rom before us, or
    // by our own line 161 -- either way correct.)
    st->ps.nd   = st->rom_nd;
    st->ps.nn   = st->rom_nn;
    st->ps.strp = st->rom_strp;
    st->ps.ns   = st->rom_ns;
    st->ps.nq   = 0;
    // If csp_rt_init ran, it tore down view/heap/the derived tables (they are
    // NULL until a rebuild). A caller that just runs the next cycle -- the host
    // main loop does -- would then fault. The boot callers rebuild themselves;
    // a failing /load did not, so a post-init failure (bad payload CRC, ROM
    // fingerprint mismatch) crashed. Rebuild the clean ROM baseline here so the
    // state is always runnable after we return.
    if (did_init) {
	csp_rebuild(st);
	csp_setup(st);
    }
    csp_set_error(st, ERR_CANNOT_LOAD);
    return -1;
}

// Get save size in bytes (RAM patch area only)
int csp_eeprom_size(csp_rt_t* st)
{
    index_t nr = csp_n_rules(st);
    return sizeof(eeprom_header_t) +
	   (st->ps.strp - st->rom_strp) +
	   sizeof(csp_decl_t) * (st->ps.nd - st->rom_nd) +
	   sizeof(csp_instr_t) * (st->ps.nn - st->rom_nn) +
	   sizeof(state_t) * (st->ps.ns - st->rom_ns) +
	   (nr ? DIS_BYTES(nr) : 0);
}

