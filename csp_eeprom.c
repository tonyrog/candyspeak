// csp_eeprom.c - Binary eeprom save/load (shared between platforms)
#include "csp.h"
#include "csp_print.h"
#include "csp_strings.h"   // ros_str/ros_decl/ros_instr/ros_states section names
#include <string.h>

// The firmware ROM image header, so a load can identify the firmware the patches
// were saved against (see rom_fp below). Declared here the same way csp_rt.c does.
// The firmware's identity for the patch log: crc_hdr of the linked image. Read
// through the ref, because the image's struct type is generated per program and
// cannot be named here.
static uint16_t rom_fingerprint(void)
{
    const uint8_t* base = ro_ref(&rom_image).base;
    return ro_header((const csp_image_header_t*)base).crc_hdr;
}

// Binary eeprom format. Only the RAM patch area is persisted -- ROM runs from
// flash. The layout mirrors the ROM image: a csp_image_header_t describes the RAM
// patch (counts + per-section CRCs) exactly as rom_header describes the flash
// program, so the same folding/verify shape serves both. rom_fp fingerprints the
// firmware; the #disable bitmap gets its own count+CRC; crc_hdr covers the whole
// header so a flipped count cannot mislead the per-section data checks.
typedef struct {
    uint8_t  magic[4];        // "CSP\0"
    uint16_t version;         // eeprom format version (EEPROM_VERSION)
    uint16_t rom_fp;          // firmware identity: the live rom_header.crc_hdr
    uint16_t nq;              // object count
    csp_image_header_t ram;   // RAM patch AS AN IMAGE: counts + per-section CRCs
    uint16_t n_dis;           // #disable bitmap: rule count it was sized over
    uint16_t crc_dis;         // #disable bitmap CRC (0xFFFF when n_dis == 0)
    uint16_t crc_hdr;         // CRC over every field above -- MUST stay last
} eeprom_header_t;

// In RODATA rather than a plain literal so it costs flash, not RAM, on AVR --
// read and written through ro_memcmp/ro_memcpy. 4 bytes, terminator included.
static rochar eeprom_magic[4] RODATA = "CSP";
#define EEPROM_MAGIC eeprom_magic
// v10: OP_NINSTATE + INSTATE/RULE bitfield changes (multi-state #in) shift the
//      saved instruction wire format -- an older save would misread.
// v9: bufdecl change from nbits nbytes
// v8: RAM patch described by an embedded csp_image_header_t (per-section CRCs +
//     crc_graph placeholder), separate crc_dis for the #disable bitmap, ROM
//     identity is the whole-header rom_header.crc_hdr, and a crc_hdr over the
//     eeprom header itself. Binary-incompatible with v7's flat header.
// v7: data_crc (single payload CRC).  v6: NAMEPOS_BITS widened decl `name`.
// The version is what rejects a stale save, before ps.* is touched.
#define EEPROM_VERSION 10

// Bytes the #disable bitset occupies for a program with n rules. Rounded up to
// whole set_group_t words so the read/write is a straight memcpy of the front
// of st->dis_rule -- the bitset macros index by word, not by byte.
#define DIS_BYTES(n) \
    ((size_t)BITSET_GROUPS((n) > MAX_DIS_RULES ? MAX_DIS_RULES : (n)) \
     * sizeof(set_group_t))

// Fill a csp_image_header_t describing the RAM patch currently in st->ram_*. The
// sizes are passed in -- save uses the ps deltas, load uses the stored header's
// counts -- so the same folder serves both. The SAME machine writes and reads, so
// raw bytes are fine (no host/target question, no ROM-style normalization) and
// runtime-scratch decl fields are folded verbatim, matching what save wrote and
// load read back. n_edg/crc_graph are 0: a RAM patch never carries a reactive
// graph -- that is rebuilt in RAM at runtime. Folds the regions in write order:
// str, decls (via the accessor -- decl[] grows down, not contiguous), instrs,
// state additions. crc_hdr closes the sub-header (kept for symmetry with ROM).
static void ram_image(csp_rt_t* st, csp_image_header_t* im,
		      uint16_t n_str, uint16_t n_decl,
		      uint16_t n_instr, uint16_t n_state)
{
    uint16_t i;

    im->version = ROM_FORMAT_VERSION;
    im->n_str = n_str; im->n_decl = n_decl; im->n_instr = n_instr;
    im->n_edg = 0;     im->n_state = n_state;

    im->crc_str  = csp_crc16(0xFFFF, st->ram_str, n_str, 0);
    im->crc_decl = 0xFFFF;
    for (i = 0; i < n_decl; i++)
	im->crc_decl = csp_crc16(im->crc_decl, ram_decl_at(st, st->rom_nd + i),
				 sizeof(csp_decl_t), 0);
    im->crc_instr = csp_crc16(0xFFFF, st->ram_instr,
			      (size_t)n_instr * sizeof(csp_instr_t), 0);
    im->crc_state = csp_crc16(0xFFFF, &st->states[st->rom_ns],
			      (size_t)n_state * sizeof(state_t), 0);
    im->crc_graph = 0;
    im->crc_hdr = csp_crc16(0xFFFF, im, sizeof(*im) - sizeof(uint16_t), 0);
}

// Verify the RAM patch just read against the stored image header, section by
// section (symmetry with rom_verify). Recompute the CRCs over the RAM slots now
// holding the loaded data and compare to what save baked. Returns the corrupt
// section's name, or NULL if intact. Counts come from the header, trusted because
// the outer crc_hdr already passed.
static rostring_t ram_verify(csp_rt_t* st, const csp_image_header_t* im)
{
    csp_image_header_t chk;

    ram_image(st, &chk, im->n_str, im->n_decl, im->n_instr, im->n_state);
    if (chk.crc_str   != im->crc_str)   return ros_str;
    if (chk.crc_decl  != im->crc_decl)  return ros_decl;
    if (chk.crc_instr != im->crc_instr) return ros_instr;
    if (chk.crc_state != im->crc_state) return ros_states;
    return NULL;
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

    // memset first so any incidental struct padding is a fixed value the crc_hdr
    // fold (and the load-side re-fold) agree on.
    memset(&hdr, 0, sizeof(hdr));
    ro_memcpy(hdr.magic, EEPROM_MAGIC, 4);
    hdr.version = EEPROM_VERSION;
    hdr.rom_fp  = rom_fingerprint();
    hdr.nq      = st->ps.nq;
    ram_image(st, &hdr.ram, ram_strp, ram_nd, ram_nn, ram_ns);
    hdr.n_dis   = (uint16_t)csp_n_rules(st);
    hdr.crc_dis = csp_crc16(0xFFFF, st->dis_rule, DIS_BYTES(hdr.n_dis), 0);
    hdr.crc_hdr = csp_crc16(0xFFFF, &hdr, sizeof(hdr) - sizeof(uint16_t), 0);

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
    rostring_t bad;
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

    // Header integrity before trusting ANY count: a flipped count would make the
    // section reads below run off into garbage. crc_hdr covers everything above it.
    if (csp_crc16(0xFFFF, &hdr, sizeof(hdr) - sizeof(uint16_t), 0) != hdr.crc_hdr)
	goto error;

    // Rebuild the ROM baseline, then load the RAM patches on top of it.
    reactive = st->reactive;
    csp_rt_init(st, reactive);
    did_init = 1;       // from here a failure has torn down view/heap/tables
    csp_load_rom(st);   // rebase ps.* to the ROM sizes (no-op if no firmware)

    // Reject patches saved against a different firmware ROM. rom_header.crc_hdr is
    // a complete fingerprint (counts + every section CRC), so any change to the
    // flash program -- size OR content -- shows up here. The RAM patches reference
    // ROM decls by index; a different program makes those indices mean something
    // else, so the whole save must go, not just the disable set.
    if (hdr.rom_fp != rom_fingerprint())
	goto error;

    // Read the RAM patch area into the RAM-local slots (counts from the header,
    // now trusted -- crc_hdr passed).
    if (csp_eeprom_read(st->ram_str, hdr.ram.n_str) < 0)
	goto error;
    // decl[] grows DOWN (see csp_eeprom_save): place them one at a time, or a
    // block read would write straight past the top of the pool.
    {
	uint16_t i;
	for (i = 0; i < hdr.ram.n_decl; i++)
	    if (csp_eeprom_read(ram_decl_at(st, st->rom_nd + i),
				sizeof(csp_decl_t)) < 0)
		goto error;
    }
    if (csp_eeprom_read(st->ram_instr, sizeof(csp_instr_t) * hdr.ram.n_instr) < 0)
	goto error;
    // Runtime state additions land above the baseline (rom_ns = INIT/NORMAL or
    // the restored ROM table); their name strings came in with ram_str above.
    if (hdr.ram.n_state &&
	csp_eeprom_read(&st->states[st->rom_ns],
			sizeof(state_t) * hdr.ram.n_state) < 0)
	goto error;

    // Payload integrity, per section: recompute each section CRC over what we just
    // read and compare to the header. Catches a flipped storage cell independently
    // of the firmware fingerprint above, and NAMES the corrupt section. Mismatch
    // -> error path restores the ROM baseline, so a corrupt save never half-loads.
    if ((bad = ram_verify(st, &hdr.ram)) != NULL) {
	csp_print_lit("eeprom rejected: CRC mismatch in ");
	csp_print_rostr(bad);
	csp_print_line(" section");
	goto error;
    }

    // Logical counts = ROM base + RAM patch
    st->ps.strp = st->rom_strp + hdr.ram.n_str;
    st->ps.nd   = st->rom_nd   + hdr.ram.n_decl;
    st->ps.nn   = st->rom_nn   + hdr.ram.n_instr;
    st->ps.ns   = st->rom_ns   + hdr.ram.n_state;
    st->ps.nq   = hdr.nq;

    // The #disable set, with its own count+CRC descriptor. csp_rt_init above
    // zeroed dis_rule, so a save without one (n_dis == 0) leaves everything
    // enabled. The rule count has to agree with what the restored program actually
    // has: if it does not, the numbers address different rules than the ones that
    // were disabled, so drop the set and SAY SO rather than silence the wrong
    // rules. When it agrees, read the bitmap and verify crc_dis before trusting it.
    if (hdr.n_dis) {
	index_t have = csp_n_rules(st);
	if (hdr.n_dis != (uint16_t)have) {
	    csp_print_lit("eeprom: disable set dropped (saved for ");
	    csp_print_uint(hdr.n_dis);
	    csp_print_lit(" rules, program has ");
	    csp_print_uint(have);
	    csp_print_line(")");
	}
	else {
	    if (csp_eeprom_read(st->dis_rule, DIS_BYTES(hdr.n_dis)) < 0)
		goto error;
	    if (csp_crc16(0xFFFF, st->dis_rule, DIS_BYTES(hdr.n_dis), 0)
		!= hdr.crc_dis) {
		csp_print_line("eeprom rejected: CRC mismatch in disable set");
		goto error;
	    }
	}
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
    // by our own csp_load_rom above -- either way correct.)
    st->ps.nd   = st->rom_nd;
    st->ps.nn   = st->rom_nn;
    st->ps.strp = st->rom_strp;
    st->ps.ns   = st->rom_ns;
    st->ps.nq   = 0;
    // If csp_rt_init ran, it tore down view/heap/the derived tables (they are
    // NULL until a rebuild). A caller that just runs the next cycle -- the host
    // main loop does -- would then fault. The boot callers rebuild themselves;
    // a failing /load did not, so a post-init failure crashed. Rebuild the clean
    // ROM baseline here so the state is always runnable after we return.
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
