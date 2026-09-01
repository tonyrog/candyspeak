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
    // The SETTINGS descriptor, first and with a version of its own. Settings are
    // the one part of the store a NEW firmware is required to still understand:
    // the patch below is fingerprinted and discarded on reflash, a setting is
    // not. See doc/EEPROM.md.
    uint16_t set_ver;         // CSP_SETTINGS_VERSION the payload was written with
    uint16_t set_bytes;       // settings payload size
    uint16_t crc_set;         // CRC over that payload
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
// v11: a SETTINGS payload ahead of the patch -- name-keyed values for what the
//      firmware already declares, with their own version and CRC, and NOT
//      covered by the rom_fp check that drops the patch on reflash.
// v10: OP_NINSTATE + INSTATE/RULE bitfield changes (multi-state #in) shift the
//      saved instruction wire format -- an older save would misread.
// v9: bufdecl change from nbits nbytes
// v8: RAM patch described by an embedded csp_image_header_t (per-section CRCs +
//     crc_graph placeholder), separate crc_dis for the #disable bitmap, ROM
//     identity is the whole-header rom_header.crc_hdr, and a crc_hdr over the
//     eeprom header itself. Binary-incompatible with v7's flat header.
// v7: data_crc (single payload CRC).  v6: NAMEID_BITS widened decl `name`.
// The version is what rejects a stale save, before ps.* is touched.
#define EEPROM_VERSION 11

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
		      uint16_t n_instr)
{
    uint16_t i;
    // Read once, not per declaration: CSP_BASE_ND is a max() over two fields
    // (see the note in csp_eeprom_load), and it sat inside the crc loop.
    index_t base_nd = CSP_BASE_ND(st);

    im->version = ROM_FORMAT_VERSION;
    im->n_str = n_str; im->n_decl = n_decl; im->n_instr = n_instr;
    im->n_edg = 0;

    // 0 bytes, and no CRC of its own. Identifier text lives in OP_SEGMENT runs
    // in the INSTRUCTION stream, so it is saved and restored by the instruction
    // block below -- there is no separate string area left to store.
    im->crc_str = 0xFFFF;
    im->crc_decl = 0xFFFF;
    for (i = 0; i < n_decl; i++)
	im->crc_decl = csp_crc16(im->crc_decl, ram_decl_at(st, base_nd + i),
				 sizeof(csp_decl_t), 0);
    im->crc_instr = csp_crc16(0xFFFF, st->ram_instr + CSP_RAM_NN_OFF(st),
			      (size_t)n_instr * sizeof(csp_instr_t), 0);
    // No state fold: a state added at the prompt is a DECL_STATES declaration
    // and rides in the decl fold above, like every other declaration.
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

    ram_image(st, &chk, im->n_str, im->n_decl, im->n_instr);
    if (chk.crc_str   != im->crc_str)   return ros_str;
    if (chk.crc_decl  != im->crc_decl)  return ros_decl;
    if (chk.crc_instr != im->crc_instr) return ros_instr;
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
    st->ee_nd = st->ee_nn = 0;    // nothing is backed any more
    // An erase takes the settings too. They outlive a reflash, not a deliberate
    // wipe of the store they live in.
    csp_settings_clear(st);
    return 0;
}

// Save state to eeprom (binary format)
int csp_eeprom_save(csp_rt_t* st)
{
    eeprom_header_t hdr;
    // The RAM base, read once -- see csp_eeprom_load. Every use below was a
    // separate max() over two struct fields, including one inside the decl loop.
    index_t  base_nd   = CSP_BASE_ND(st);
    index_t  base_nn   = CSP_BASE_NN(st);
    uint16_t ram_nd   = st->ps.nd   - base_nd;      // RAM patch counts
    uint16_t ram_nn   = st->ps.nn   - base_nn;
    uint16_t ram_strp = 0;   // no separate string area any more
    // (No state block: states are declarations and are saved with them.)

    if (csp_eeprom_open_write() < 0)
	goto error;

    // memset first so any incidental struct padding is a fixed value the crc_hdr
    // fold (and the load-side re-fold) agree on.
    memset(&hdr, 0, sizeof(hdr));
    ro_memcpy(hdr.magic, EEPROM_MAGIC, 4);
    hdr.version = EEPROM_VERSION;
    hdr.set_ver   = CSP_SETTINGS_VERSION;
    hdr.set_bytes = st->set_used;
    hdr.crc_set   = csp_crc16(0xFFFF, st->settings, st->set_used, 0);
    hdr.rom_fp  = rom_fingerprint();
    hdr.nq      = st->ps.nq;
    ram_image(st, &hdr.ram, ram_strp, ram_nd, ram_nn);
    hdr.n_dis   = (uint16_t)csp_n_rules(st);
    hdr.crc_dis = csp_crc16(0xFFFF, st->dis_rule, DIS_BYTES(hdr.n_dis), 0);
    hdr.crc_hdr = csp_crc16(0xFFFF, &hdr, sizeof(hdr) - sizeof(uint16_t), 0);

    if (csp_eeprom_write(&hdr, sizeof(hdr)) < 0)
	goto error;
    // The settings payload FIRST, at a fixed offset immediately behind the
    // header. Reaching it never involves parsing anything of variable length, so
    // a patch that is corrupt, half-written or built by a firmware that is no
    // longer flashed does not stand between you and the calibration.
    if (st->set_used &&
	csp_eeprom_write(st->settings, st->set_used) < 0)
	goto error;
    // Only the RAM patch area (ram_*[0..delta)); ROM stays in flash.
    // (No string block. The names went out with the INSTRUCTIONS, as OP_SEGMENT
    // runs -- one block write instead of a second area to keep in step.)
    // decl[] grows DOWN from the pool top, so the RAM decls are not contiguous in
    // save order -- walk them through the accessor. The stored format is unchanged
    // (logical order 0..ram_nd-1); only the memory walk differs. instr[] still
    // grows up, so it stays one block write.
    {
	uint16_t i;
	for (i = 0; i < ram_nd; i++)
	    if (csp_eeprom_write(ram_decl_at(st, base_nd + i),
				 sizeof(csp_decl_t)) < 0)
		goto error;
    }
    if (csp_eeprom_write(st->ram_instr + (base_nn - st->rom_nn), sizeof(csp_instr_t) * ram_nn) < 0)
	goto error;
    // (No state block -- ram_ns is 0. States went out with the declarations.)
    // The #disable set, last: one bitset over rule numbers covering ROM and RAM
    // alike (numbers run 1..r_rom through the ROM rules and on into the RAM
    // ones), so there is nothing to split by segment.
    if (hdr.n_dis &&
	csp_eeprom_write(st->dis_rule, DIS_BYTES(hdr.n_dis)) < 0)
	goto error;

    csp_eeprom_close();
    // Everything in the RAM patch is now also in eeprom -- /list tags it E from
    // here on. Set only after the last write succeeded: a save that failed
    // half-way must not claim coverage it does not have.
    st->ee_nd = ram_nd;
    st->ee_nn = ram_nn;
    st->set_dirty = 0;
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
    // The RAM base, read once. CSP_BASE_* LOOK like field accesses but each is a
    // max() over two struct fields -- two loads, a compare and a select, every
    // time. There were nine expansions here, one of them INSIDE the decl loop
    // where the same maximum was recomputed per declaration.
    //
    // They are assigned after csp_load_rom, which is what sets rom_*: computing
    // them at the top would capture the pre-rom values. Everything that reads
    // them (including the error path, which only touches them when did_init is
    // set, and did_init implies csp_load_rom ran) comes after that point.
    index_t base_nd = 0, base_nn = 0, base_strp = 0;

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
    csp_rt_init(st, reactive, st->cs);   // keep the compiler, if there is one
    did_init = 1;       // from here a failure has torn down view/heap/tables
    csp_load_rom(st);   // rebase ps.* to the ROM sizes (no-op if no firmware)
    base_nd   = CSP_BASE_ND(st);
    base_nn   = CSP_BASE_NN(st);
    base_strp = CSP_BASE_STRP(st);

    // SETTINGS, before the rom_fp check below -- deliberately. They are keyed by
    // NAME and are meant to cross a reflash, so they must not be dropped along
    // with the patch that is not. A payload this firmware cannot read (a bumped
    // CSP_SETTINGS_VERSION), one too big for the store, or one that fails its own
    // CRC is dropped -- and SAID, rather than silently as the patch is.
    if (hdr.set_bytes) {
	int readable = (hdr.set_ver == CSP_SETTINGS_VERSION) &&
		       (hdr.set_bytes <= CSP_SETTINGS_BYTES);
	if (!readable) {
	    // Not consumed by a read, so the patch behind it would start at the
	    // wrong byte. Step over it -- the stream has no seek.
	    uint16_t left = hdr.set_bytes;
	    uint8_t skip[16];
	    while (left) {
		uint16_t n = (left > sizeof(skip)) ? (uint16_t)sizeof(skip) : left;
		if (csp_eeprom_read(skip, n) < 0)
		    goto error;
		left -= n;
	    }
	    if (hdr.set_ver != CSP_SETTINGS_VERSION)
		csp_print_line("eeprom: settings in an unknown format -- ignored");
	    else
		csp_print_line("eeprom: settings too large for this build -- ignored");
	}
	else if (csp_eeprom_read(st->settings, hdr.set_bytes) < 0)
	    goto error;
	else if (csp_crc16(0xFFFF, st->settings, hdr.set_bytes, 0) != hdr.crc_set) {
	    csp_print_line("eeprom rejected: CRC mismatch in settings section");
	    st->set_used = 0;
	}
	else
	    st->set_used = hdr.set_bytes;
    }

    // Reject patches saved against a different firmware ROM. rom_header.crc_hdr is
    // a complete fingerprint (counts + every section CRC), so any change to the
    // flash program -- size OR content -- shows up here. The RAM patches reference
    // ROM decls by index; a different program makes those indices mean something
    // else, so the whole save must go, not just the disable set.
    if (hdr.rom_fp != rom_fingerprint())
	goto error;

    // Read the RAM patch area into the RAM-local slots (counts from the header,
    // now trusted -- crc_hdr passed).
    // (No string block: the names come back with the instructions.)
    // decl[] grows DOWN (see csp_eeprom_save): place them one at a time, or a
    // block read would write straight past the top of the pool.
    {
	uint16_t i;
	for (i = 0; i < hdr.ram.n_decl; i++)
	    if (csp_eeprom_read(ram_decl_at(st, base_nd + i),
				sizeof(csp_decl_t)) < 0)
		goto error;
    }
    if (csp_eeprom_read(st->ram_instr + (base_nn - st->rom_nn), sizeof(csp_instr_t) * hdr.ram.n_instr) < 0)
	goto error;
    // (No state block: a save made by this firmware has n_state == 0, and states
    // came back with the declarations above.)

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
    st->ps.nd   = base_nd   + hdr.ram.n_decl;
    st->ps.nn   = base_nn   + hdr.ram.n_instr;
    st->ps.nq   = hdr.nq;
    // AFTER the counts: both of these walk 0..ps.nn looking for segments, so
    // they can only find the patch's once nn covers it. resize builds the map
    // and sets ps.strp; recount then counts the handles through that map.
    csp_str_resize(st);
    csp_str_recount(st);

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
    // What came back from eeprom -- /list tags exactly this much of the RAM patch
    // E. csp_rt_init above zeroed the watermark, so on any failure path it stays
    // zero and nothing claims to be backed. Set before csp_rebuild, which can
    // fail on a full pool without making the eeprom copy any less real.
    st->ee_nd = hdr.ram.n_decl;
    st->ee_nn = hdr.ram.n_instr;
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
    // Only restore what we actually disturbed. Before csp_rt_init has run -- an
    // empty or foreign EEPROM fails the header checks above and jumps straight
    // here -- ps.* is exactly as the CALLER left it, and rewriting it was pure
    // damage: it set ps.nd to rom_nd, which is 0 with no firmware image, and so
    // deleted the State variable the caller had just created. Every board with
    // EEPROM enabled then ran with no State; the gate in csp_eval read a slot
    // that was no longer State's, so every ungated rule quiesced and no timer
    // ever rearmed.
    if (did_init) {
	// A failure past the header may have inflated ps.* (see "Logical counts")
	// and half-written RAM slots. Restore the clean baseline so the caller
	// runs the ROM program, not ROM + partially-loaded garbage. Resetting the
	// counts is enough: stale ram_* content is never read once the indices
	// stop there.
	//
	// The floor is rom_* OR what csp_rt_init made, whichever is higher: with
	// a firmware image csp_load_rom raised ps.* to the ROM sizes and State is
	// ROM decl 0, so rom_* wins; with no image rom_nd is 0 and the init
	// baseline is what keeps State alive.
	st->ps.nd   = base_nd;
	st->ps.nn   = base_nn;
	st->ps.strp = base_strp;
	st->ps.nq   = 0;
	csp_str_recount(st);
	// csp_rt_init tore down view/heap/the derived tables (NULL until a
	// rebuild). A caller that just runs the next cycle -- the host main loop
	// does -- would fault, so rebuild the clean baseline before returning.
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
	   st->set_used +
	   (st->ps.strp - CSP_BASE_STRP(st)) +
	   sizeof(csp_decl_t) * (st->ps.nd - CSP_BASE_ND(st)) +
	   sizeof(csp_instr_t) * (st->ps.nn - CSP_BASE_NN(st)) +
	   (nr ? DIS_BYTES(nr) : 0);   // states ride in the decl count above
}
