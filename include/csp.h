#ifndef __CSP_H__
#define __CSP_H__

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>   // offsetof, used by CSP_IMAGE_CHECK
#include <string.h>

#include "csp_config.h"
#include "csp_line.h"
// Prevent inlining of large parse functions to reduce code size
#ifdef __GNUC__
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

#ifdef DEBUG
#include <stdio.h>
extern int debug;
#define DBG(...) do { \
	if (debug) printf(__VA_ARGS__);		\
    } while(0)
#else
#define DBG(...)
#endif


#define PACKED __attribute__((packed))

// an index has the following structure
// obj:4, index:12   // declaration object
//
// obj is current object 0 = global, cur = 2^OBJ_BITS-1 or the actual obj index
// tag is TAG_DECL (offs[m]+st->decl[index])
// 

typedef uint16_t index_t;  // sizeof type >= INDEX_BITS
typedef uint8_t  reg_t;    // at most 256 registers
typedef unsigned bool_t;

#define PORT_BITS 4   // port 0..15    mega is 11 ports, with 8 pins each
#define PIN_BITS  7   // pin  0..127

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define RODATA          PROGMEM
#define ro_byte(p)      pgm_read_byte((p))
#define ro_word(p)      pgm_read_word((p))
#define ro_dword(p)     pgm_read_dword((p))
#define ro_ptr(p)       (void *)pgm_read_word((p))
#define ro_memcmp(a,b,n) memcmp_P((a), (b), (n))
#define ro_memcpy(d,s,n) memcpy_P((d), (s), (n))
// The name buffer stays per-target: it is RAM, it appears in no struct that
// travels, and a program compiled on the host runs from an image on a board with
// a much smaller one. On a 2K part this is the difference between fitting and
// not. OBJ_BITS used to be per-target too -- see the note at its definition for
// why it cannot be.
#define CSP_STR_BYTES  128
#else
#define RODATA
#define ro_byte(p)      (*(p))
#define ro_word(p)      (*(p))
#define ro_dword(p)     (*(p))
#define ro_ptr(p)       (*((const void**)(p)))
#define ro_memcmp(a,b,n) memcmp((a), (b), (n))
#define ro_memcpy(d,s,n) memcpy((d), (s), (n))
// BYTES, not a bit width. It was `1 << STRING_BITS` because a name field held a
// byte OFFSET into this buffer, so its size and the format ceiling were the same
// number and had to be a power of two. A name is a HANDLE now -- the Nth string
// -- so the format caps how MANY names there are (MAX_NAMEIDS) and this caps how
// many BYTES they take, independently. Nothing indexes it with a bit field any
// more, so it can be whatever a board actually has room for.
//
// Overridable without editing this header:
//   make csp CFLAGS_EXTRA=-DCSP_STR_BYTES=8000
//
// `-m` does not move it: it is not part of the arena. See the note at ram_str.
//
// 4 KB on the host, which is room for all 512 names at any length real programs
// use. A target gets 512: it runs an image whose names are already in flash, and
// this buffer only holds what the REPL adds at run time.
#ifndef CSP_STR_BYTES
#if defined(ARDUINO) || defined(CSP_SMALL_TARGET)
#define CSP_STR_BYTES  512
#else
#define CSP_STR_BYTES  4096
#endif
#endif
#endif

// OBJ_BITS is 1, and that is not a tuning knob any more -- it is a SELECTOR.
//
// An encoded index (index_t, 16 bits) is  sel:1 | index:15  where sel picks the
// base the index is relative to:
//
//   GLOBAL  (0)   base 0          -- a top-level declaration
//   CURRENT (1)   base st->cbase  -- a member of the object being executed
//
// It used to be obj:5 | index:11, where obj was a real object number: 0 global,
// 31 (CURRENT) "my own", and 1..30 a NAMED object (`safe.State`). Only that
// third case needed more than one bit, it is emitted from three places in the
// compiler, and it made EVERY memory instruction pay four bits for it. It now
// costs one OP_SETO instruction at the reference instead, which buys:
//
//   - objects bounded by RAM instead of by 32
//   - 32768 indices instead of 2048, globally and per object
//   - offs[] as the object->base table only, no longer indexed by an encoding
//
// and, mainly, it takes the width OUT of the image format. It used to reach
// into the bytes of a ROM in three separate ways -- INDEX_BITS is the width of
// csp_instr_mem_t.mem, csp_instr_memi_t.mem, csp_field_t.id and
// csp_buffer_t.id; CURRENT is baked into every module-local reference; and
// csp_decl_t's mq arm had an OBJ_BITS-wide member. With 3 on AVR and 5 on the
// host, the generator CRC'd bytes the target laid out differently, and an image
// built on the host was rejected on AVR as corrupt -- while running correctly,
// because CURRENT truncated back to the right value. The only clue was a
// -Woverflow warning that the Arduino build (-w) hid.
//
// Do not "tune" this. A different value is a different image format.
#define OBJ_BITS     1

// rom-aware scalar reads: on the host both branches are identical (ro_*==plain);
// on AVR the rom branch uses PROGMEM. One code path serves RAM and ROM tables.
static inline uint8_t rd8(const void* p, int rom)
{
    return rom ? ro_byte((const uint8_t*)p): *(const uint8_t*)p;
}

static inline uint16_t rd16(const void* p, int rom)
{
    return rom ? ro_word((const uint16_t*)p): *(const uint16_t*)p;
}

static inline void* rdvp(const void* p, int rom)
{
    void* v;
#if defined(__AVR__)
    if (rom)
	return ro_ptr((void* const*)p);   // PROGMEM: byte-wise read
#endif
    // fn sits at a misaligned offset in the PACKED csp_func_t table; a direct
    // deref HardFaults on Cortex-M0 (no unaligned access). memcpy is byte-wise
    // and alignment-safe -- and the compiler folds it to a plain load where the
    // address happens to be aligned.
    memcpy((void*) &v, p, sizeof(v)); return v;
}

// Shared by every target: nothing is dimensioned from these any more, so a wider
// index costs no RAM. They are pure ceilings on how many decls/instructions can
// be addressed -- what actually binds is the CSP_CODE_BUDGET byte pool (mem_fits)
// and, for the reactive tables, the rule count (they key on a rule ordinal, not a
// raw ip). The last holdout was csp_csr's index_t wr[MAX_DECLS] stack array,
// which now lives in the idg block sized to the actual decl count.
// DECL_BITS is what is LEFT of index_t after the 1-bit selector, so an index is
// relative to a base and 15 bits is the ceiling on globals AND on members per
// object -- not on the two together. It was 11 while obj took the other five.
#define DECL_BITS    15
// 15, not 11. This is NOT an encoding width -- nothing declares a bit field with
// it, it only sizes MAX_INSTRS. What actually bounds the stream are the relative
// offsets instructions carry, and they are all wider or bound something smaller:
//
//   rule.nxt      signed 15   a rule body,     +-16383
//   instate.nxt   signed 13   an #in block,    +-4095
//   enter.num     10          a module body,   1023
//
// The one absolute index was OP_NEW's `ent`, and it silently truncated any
// module past 1023 until it was taken out (see csp_instr_new_t). With that gone
// nothing in the encoding cares how long the stream is, and identifier text
// shares it now -- so the ceiling had to stop being the tightest thing in sight.
#define INSTR_BITS   15

// Width of a decl's `name` field: a string HANDLE, which is an ID -- handle N
// names the Nth string in the table, ROM and RAM together. So this caps how
// MANY names a program may have, and nothing about how long they are.
//
// It used to be a byte POSITION (hence the old name, NAMEPOS_BITS), which made
// the same 9 bits cap the total LENGTH of every identifier at 512 bytes -- a
// wall a single module with long #param names could hit. The two limits are
// separate now: this one, and the string buffer for the bytes.
//
// It also used to reuse STRING_BITS, which is a RAM budget (7 on AVR = a
// 128-byte ram_str). That coupling was a bug in its own right: with a ROM
// string table of 130 bytes the very first RAM decl name landed at position 131
// and a 7-bit field truncated it to garbage.
//
// 9 bits fits inside DECL_COMMON's two spare bits on AVR, so csp_decl_t does not
// grow. new_string refuses a handle that will not fit rather than truncating it
// (ERR_STRING_SPACE).
#define NAMEID_BITS 9
#define MAX_NAMEIDS (1u << NAMEID_BITS)   // handles 1..MAX_NAMEIDS-1; 0 = no name

// Format version of a generated ROM (rom.c). Baked in by `csp -C` as rom_version
// and checked by csp_load_rom at boot. Bump it whenever the ROM layout changes
// in a way an old generate could not survive: csp_decl_t / csp_instr_t bitfield
// widths, the rom_* symbol set, or the meaning of a baked field. A mismatch
// rejects the ROM (runs empty, with a message) instead of executing garbage --
// exactly the "stale generate" trap that cost us the July-18 ROM and EEPROM v5.
//   v1: first versioned ROM (post NAMEID_BITS)
//   v2: csp_image_header_t (per-section CRCs) replaces the loose rom_* scalars
//   v3: crc_graph covers the reactive graph (rom_idg/rom_ofs/rom_edg)
//   v14: strings lost their nul terminator -- [len][chars], nothing after. Every
//        position past the first shifts, so an older table reads as garbage.
//   v15: a decl's `name` is a string HANDLE (the Nth string), not a byte
//        offset, and the table starts at byte 0 instead of 1. The 9-bit field
//        now caps the NUMBER of names at 512 rather than their bytes.
//   v16: identifier text moved into the INSTRUCTION stream as OP_SEGMENT runs
//        (the .str section is gone), and OP_NEW lost its 10-bit `ent` -- the
//        entry point comes from the module declaration, which can address the
//        whole stream.
//   v4: #buffer size is bytes -- csp_bufdecl_t.nbits became nbytes
//   v5: OP_NINSTATE + INSTATE.nxt 14->13 with implicit bit (multi-state #in)
//   v6: DECL_END_MARK / OP_END_MARK self-CRC terminators (header-corruption
//       recovery: each of rom_decl/rom_instr self-verifies without the header)
//   v7: str + state self-CRC trailers (all four sections self-verify)
//   v8: ONE contiguous image object -- magic/size/role/generation in the header,
//       sections reached by offset instead of by symbol, and a tagged prologue
//       in front of each so the whole thing can be walked with no header at all
//   v9: OBJ_BITS 5->1. An encoded index is sel:1|index:15 (global / current
//       object) and a NAMED object is reached with OP_SETO instead of a 5-bit
//       object number in every memory instruction. See the OBJ_BITS note below.
//  v10: no state section. A state is a DECL_STATES declaration (six names per
//       block, csp_states_t), so it is stored, CRC'd, baked and persisted by the
//       declaration machinery -- and the header loses n_state/crc_state/
//       ofs_states, the image loses s_states, and state_t is gone.
//  v13: OP_TMO replaces `timeout(T)` as a call, and fn_timeout is gone from the
//       builtin table -- which SHIFTS every function index after it. A baked
//       OP_CALL names its function by index, so an old image would call the one
//       next door: elapsed() where it meant timeout().
//  v12: `>` and `>=` are gone from the encoded opcodes (16, 17, 33, 34 are free)
//       and csp_instr_alu_t.swap says the operands were exchanged to get there.
//       An old image still has OP_GT at 16 and would decode it as whatever takes
//       that number next; a new image read by an old firmware would compare with
//       the operands the wrong way round, silently. Both directions have to be
//       rejected, which is what the bump does.
//  v11: csp_instr_alu_t.u -- a spare bit of the ALU word now says the operands
//       are UNSIGNED, which decides what / % >> < <= > >= compute. "The meaning
//       of a baked field" exactly: an old image reads back with u == 0 and is
//       still right (that was signed), but a NEW image in an OLD firmware would
//       compute every one of those SIGNED with no complaint. Images travel on
//       their own -- an A/B slot, a FAILSAFE flashed alone -- so that direction
//       is reachable and has to be rejected rather than run.
#define ROM_FORMAT_VERSION 17

// Format version of the SETTINGS store, which is NOT ROM_FORMAT_VERSION and not
// EEPROM_VERSION either. It needs its own because it is the one part of the
// eeprom a new firmware is REQUIRED to still understand: the other two describe
// things that are discarded on reflash, so bumping them is free. Bump this only
// when the entry encoding changes, and then either read the older one or drop it
// and SAY SO. See doc/EEPROM.md.
#define CSP_SETTINGS_VERSION 1

// RAM held for the settings store. An entry is 8 + strlen(path) bytes -- `Kp`
// costs 10, `sys.NodeID` 18 -- so 128 is around ten settings on a board and the
// host gets room to be careless. Boards override it in boards/*.h.
//
// It cannot be traded for a bitmask over the leaves, which is the obvious idea.
// csp_rt_start re-seeds every slot from its declaration, so a rebuild -- one
// more line typed at the prompt -- wipes the live value before anything could
// diff it against the source. A mask would say WHICH leaf the operator owns and
// not WHAT they set. The store is what makes a setting survive that, which is
// also why csp_settings_apply is called from csp_rt_start and not only at boot.
#ifndef CSP_SETTINGS_BYTES
#ifdef ARDUINO
#define CSP_SETTINGS_BYTES 128
#else
#define CSP_SETTINGS_BYTES 1024
#endif
#endif

// Longest path an entry may name ("obj.member"). One byte holds the length, but
// the cap is well under 255 so a corrupt length cannot walk the store off its
// end before the payload CRC is even consulted.
#define CSP_SETTINGS_MAX_PATH 63

// Longest STRING value an entry may carry -- a node name, a version tag. Same
// reason for the cap as the path above: one byte holds the length, and a short
// bound keeps a corrupt one from walking the store apart.
#define CSP_SETTINGS_MAX_STR 63

// Free as in beer.
#define CSP_IMAGE_MAGIC0 'J'
#define CSP_IMAGE_MAGIC1 'A'
#define CSP_IMAGE_MAGIC2 'M'
#define CSP_IMAGE_MAGIC3 '\n'

// What an image is FOR. A scan groups by role and picks one per role, which is
// what makes redundant copies and A/B versions fall out of the same rule
// instead of each being a special case.
// sys.Boot: no preference, take the highest generation. Not 0 -- 0 is a real
// image number, the first one /images prints.
#define CSP_BOOT_AUTO   255

#define CSP_ROLE_ROM      0   // the program
#define CSP_ROLE_FAILSAFE 1   // the one that runs when the program cannot

// Section tags: four ASCII characters, IFF/BEAM style. Four bytes cost nothing
// here (the prologue was carrying 16 unused bits either way) and buy two things:
// a tag is findable in a hex dump or with grep over the binary, and a scan can
// match one with a single 32-bit compare instead of a table lookup.
//
// Written as four chars so they compose into an initializer: `.tag = { CSP_SECT_DECL }`.
// Not a string literal -- `char[4] = "DECL"` is legal C but not C++, and these
// headers are included from a .ino.
#define CSP_SECT_STR     'S','T','R','S'
#define CSP_SECT_DECL    'D','E','C','L'
#define CSP_SECT_INSTR   'C','O','D','E'
#define CSP_SECT_IDG     'G','I','D','G'
#define CSP_SECT_OFS     'G','O','F','S'
#define CSP_SECT_EDG     'G','E','D','G'
// ('STAT' is retired: format v10 dropped the state section. Do not reuse the
//  tag -- a v9 image walked by a v10 loader would land on it.)

// Compare a prologue tag against one of the above:  csp_tag_is(sc.tag, CSP_SECT_DECL)
// Two levels: a macro argument is split on commas BEFORE it is expanded, so the
// four-character tag has to arrive as __VA_ARGS__ and be re-expanded.
#define csp_tag_is(t, ...) csp_tag_is_(t, __VA_ARGS__)
#define csp_tag_is_(t,a,b,c,d) \
    (((t)[0]==(a)) && ((t)[1]==(b)) && ((t)[2]==(c)) && ((t)[3]==(d)))

// One header per image, at offset 0 of the image object, baked by `csp -C`.
//
// Everything the loader needs to reach a section is IN here, as a byte offset
// from the image base -- not as a pointer. Offsets survive being copied to
// another flash page or into RAM; pointers do not, and pointers cannot be
// checked against anything. `magic` lets a scan recognise an image it was never
// told about; `size` lets it step to the next one.
//
// crc_hdr is LAST and covers every byte above it -- magic, size, role,
// generation, the counts, the section CRCs AND the offsets. An offset that
// rotted would otherwise send the loader to a garbage address with nothing
// objecting, which is the wrong failure mode for the part of the system whose
// job is to notice failure. All fields are byte-stable and PACKED, so a CRC
// baked on the host matches a little-endian target (see the CSP_STATIC_ASSERT
// at csp_crc16).
typedef struct PACKED {
    uint8_t  magic[4];   // JAM\n -- start of an image
    uint32_t size;       // total bytes of the image object
    uint16_t version;    // ROM_FORMAT_VERSION at generation
    uint16_t role;       // CSP_ROLE_*: what this image is for
    uint16_t generation; // higher is newer; orders A against B
    uint16_t n_str;      // str bytes (excl. sentinel + trailer)
    uint16_t n_decl;     // decl entries (excl. DECL_END_MARK)
    uint16_t n_instr;    // instr entries (excl. OP_END_MARK)
    uint16_t n_edg;      // edg entries (0 = no reactive graph)
    uint16_t crc_str;    // CRC-16/CCITT per section
    uint16_t crc_decl;
    uint16_t crc_instr;
    uint16_t crc_graph;  // over idg + ofs + edg (0 when n_edg == 0)
    uint32_t ofs_str;    // section DATA starts, bytes from the image base
    uint32_t ofs_decl;   // (each section's prologue sits just before its data)
    uint32_t ofs_instr;
    uint32_t ofs_idg;
    uint32_t ofs_ofs;
    uint32_t ofs_edg;
    uint16_t crc_hdr;    // over all bytes ABOVE this one -- MUST stay last
} csp_image_header_t;

// The prologue in front of each section. Two jobs the header cannot do:
//
//   - it says what the section IS, locally, so a walker that lands on an offset
//     can identify it with no header and a tool can dump an image it only half
//     understands;
//   - `len` is in BYTES, not entries, so a reader can skip a section whose tag
//     it does not know. That is what keeps a v8 image walkable by a later
//     reader; with fixed header fields alone, every added section would break
//     the walk.
//
// No CRC here: the header carries one per section and the sections carry their
// own end markers. A third copy would only give three things to disagree.
typedef struct PACKED {
    char     tag[4];     // CSP_SECT_*: four ASCII, readable in a hex dump
    uint32_t len;        // bytes of data after this prologue (trailers included)
} csp_sect_t;

// Bytes to add after a section of n bytes so the next one starts 4-aligned.
// Every element type is 8, 4 or 2 bytes, so a 4-aligned start satisfies all of
// them -- and index_t genuinely needs 2 (an unaligned 16-bit load faults on
// Cortex-M0). The packed decl/instr types claim alignment 1 and would otherwise
// be placed at any odd offset, costing byte-wise access for nothing.
#define CSP_PAD4(n)  ((4 - ((n) & 3)) & 3)
#define CSP_MASK(n, bn) ((n) & ((1 << (bn))-1))

typedef const char rochar;                // PROGMEM string character type
typedef const struct rostr* rostring_t;  // PROGMEM string object type

// strlen for a string that may live in flash. Walking one with s[n] reads the
// wrong address space on AVR, and the plain `const char*` a helper takes hides
// that from the compiler -- so anything holding a rochar* uses this.
extern int ro_strlen(rostring_t s);


// strncmp against a flash string. NOT ro_memcmp: memcmp compares all n bytes
// even when the flash entry is shorter, reading past its end -- comparing
// "listing" (7) against "list" (4) walks 3 bytes off the array.
extern int ro_strncmp(const char* a, rostring_t b, int n);

extern int ro_strcmp(const char* a, rostring_t b);

// Copy a flash string into RAM, at most max-1 chars plus a terminator; returns
// the copied length. For the few places that need a RODATA string as a plain
// char* -- see RO_TSTR below.
extern int ro_strcpy(char* dst, rostring_t src, int max);

// A tstr_t naming a RODATA string, materialized on the stack. The string table
// and the name comparators take RAM pointers (new_string memcpy's the text in),
// so an internal name kept in flash has to be pulled into RAM to be used as a
// declaration name. Buffer is sized for those internal names; anything longer
// truncates, which is why this is not a general-purpose conversion.
#define RO_TSTR(var, ros)					\
    char var##_b[12];						\
    tstr_t var = { var##_b, ro_strcpy(var##_b, (ros), sizeof(var##_b)) }

// A .ino compiles this header as C++, where _Static_assert is only an extension
// (the SAMD toolchain accepts it, the older AVR one rejects it outright).
#if defined(__cplusplus)
#define CSP_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define CSP_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#define INDEX_BITS   (OBJ_BITS+DECL_BITS)
// A relative body length / entry-point index that shares the 32-bit instruction
// word with a full index_t member (enter/leave/new): 32 - INSTR_COMMON(6) -
// index_t(16) = 10 bits. This caps a module body / entry offset at 1024, which is
// independent of (and smaller than) the INSTR_BITS instruction-count ceiling.
#define BODY_BITS    10
#define REG_BITS     4   // r0..r15
#define FUNC_BITS    5   // 0..31 (need more!)
#define PART_BITS    4
// The two values the 1-bit selector of an encoded index can take. Not object
// numbers: GLOBAL means "base 0" and CURRENT means "base st->cbase". A NAMED
// object is reached by pointing cbase at it with OP_SETO first.
#define GLOBAL       0                       // global level
#define CURRENT      ((1 << OBJ_BITS)-1)     // current obj
#define MAX_INDICES  (1UL << INDEX_BITS)   // 1<<16: needs the long on a 16-bit int
#define MAX_REGS     (1 << REG_BITS)
#define MAX_INSTRS   (1 << INSTR_BITS)
#define MAX_DECLS    (1 << DECL_BITS)
// The input/output/timer lists and the buffer heap are sized to what the program
// actually declares (csp_estimate -> csp_rt_start), so they have no MAX_* here.
// What remains are limits the ENCODING imposes, not reservations: an index must
// fit its bit field.
// Concurrent variable refs on the RHS of one <- binding: parse-time scratch
// (var_buf), bounded by expression size, not by memory.
#if defined(__AVR__)
#define MAX_VARREFS  4    // tiny RAM: keep <- expressions short
#else
#define MAX_VARREFS  16
#endif
// The highest object number the ENCODING can carry: csp_object_t.m and
// csp_instr_seto_t.obj are both 16 bits, and 0 is reserved for the global base.
// This is not a reservation -- offs[]/object[]/module[] are sized to what a
// program actually declares (csp_estimate) and laid out with the other derived
// tables. What binds in practice is the arena, and that fails at rebuild with
// ERR_TOO_MANY_DECLARATIONS. This only refuses a number that cannot be stored.
#define MAX_OBJECT_NUM 0xffffu
// (buffers need no MAX_*: csp_view_t.buf is as wide as the nbuf counter feeding it)
// Highest rule number #disable can address. A fixed bitset on csp_rt_t (it has
// to outlive every rebuild), so this is RAM spent whether or not it is used --
// 128 bits = 16 bytes. Programs with more rules than this still RUN fine; only
// #disable refuses past the cap.
#define MAX_DIS_RULES 128
#define DIR_BITS 2
#define TYPE_BITS 4  // supports up to 15 types & objects
#define ENDIAN_BITS 2

// Queue entry: pack obj and ip together
// A pending-work key packs (rule ORDINAL, object) -- ORDINAL IN THE HIGH BITS.
// That order is the whole point: ordinals are handed out in instruction order
// (number_rules), so walking keys upwards evaluates rules in the order they are
// written, and a rule added later overrides an earlier one -- which is how a
// running program gets patched. Keying obj-major instead would run a late global
// patch before the object rules it is meant to override.
//
// The object field is st->es.obj_shift wide -- sized to the objects that EXIST, not
// to OBJ_BITS. A program with no objects gets a 0-bit field (one slot per rule);
// one with 10 objects gets 4 bits, not 5. A shift, not a multiply by nq+1, so the
// decode stays a mask and the set can still be walked a word at a time.
#define MAKE_QENTRY(st, obj, ord) (((ord) << (st)->es.obj_shift) | (obj))
#define QENTRY_OBJ(st, e)         ((e) & ((1u << (st)->es.obj_shift) - 1))
#define QENTRY_ORD(st, e)         ((e) >> (st)->es.obj_shift)
#define MAX_STACK_DEPTH 4
#define NAME_BITS    5
#define MAX_STR_BUF  CSP_STR_BYTES   // total bytes of identifier text in RAM

// --- string segments -------------------------------------------------------
//
// RAM string bytes live in the DECLARATION pool, in runs of DECL_SEGMENT: a
// header slot with the payload count, then the slots holding characters. The
// decl pool is the only allocator here that hands out permanent memory LAZILY
// (the middle is reset by every rebuild; the two growing ends are anchored once
// in csp_mem_init), so a segment inherits exactly the growth the table needs and
// mem_fits() already counts it.
//
// ONE SIZE FOR EVERY PLATFORM. Not because an image forces it -- a segment
// never reaches one, since csp -C writes a flat string section and a board's own
// names live in flash -- but because two sizes would be two layouts to reason
// about for no gain. 128 is what an AVR can spare: 17 slots is 136 bytes of a
// 512-byte arena, about what its old fixed ram_str took, while a host arena
// does not notice either way.
//
// The cost is 6% metadata (a whole 8-byte slot to carry 4 bytes of header) plus
// ~2% lost to strings that do not fit at the end of a segment. A record may not
// straddle a boundary: csp_str_at hands out a raw char* into one.
#define CSP_STR_SEG_BITS  7
#define CSP_STR_SEG_BYTES (1u << CSP_STR_SEG_BITS)          // 128
#define CSP_STR_SEG_MASK  (CSP_STR_SEG_BYTES - 1u)
#define CSP_STR_SEG_SLOTS (CSP_STR_SEG_BYTES / sizeof(csp_instr_t))

// How many segments the map can hold. A table, not a format -- it may differ per
// target. 512 names of the longest kind would want ~132; a host can afford that,
// a small part cannot and will run out of arena long before.
#ifndef CSP_STR_MAX_SEGS
#if defined(__AVR__)
#define CSP_STR_MAX_SEGS 4
#elif defined(ARDUINO) || defined(CSP_SMALL_TARGET)
#define CSP_STR_MAX_SEGS 16
#else
#define CSP_STR_MAX_SEGS 136
#endif
#endif


// Scratch for the up-to-three %s arguments an error message carries. Its own
// buffer, not the top of the name table: an error argument is a COPY of a name
// (a ROM one lives in flash, and neither fprintf nor csp_print_error can tell
// the segments apart from a pointer), it lives for one message, and sharing
// space with the table meant a long program could not report what was wrong
// with it. Same reasoning as CSP_DEFINE_BYTES.
//
// Three arguments of a name each, and a name is bounded by the line length.
#ifndef CSP_ERR_STR_BYTES
#define CSP_ERR_STR_BYTES 128
#endif

// The disassembler's scratch, where ONE rendered rule is built. It was sized to
// MAX_STR_BUF, which is a different quantity entirely -- the whole name table --
// and the cursor into it is a uint8_t, so on the host 3841 of those 4096 bytes
// could never be reached. A rendered line is what it has to hold.
#define MAX_EXPRBUF  ((MAX_STR_BUF < 255) ? MAX_STR_BUF : 255)

// --- #define: compile-time names, and NOT in the string table ---------------
//
// `#define NAME VALUE` binds a name to a constant for the COMPILER only. The
// value is folded into the code exactly as a #constant's is; the difference is
// that the NAME is forgotten as soon as the program is built. It never becomes
// a declaration, never reaches ram_str, and never appears in a ROM image.
//
// WHY IT NEEDS ITS OWN BUFFER. The obvious place -- ram_str -- would buy
// nothing: that array is already double-ended (names grow up from 0, error
// strings down from MAX_STR_BUF), so a define stored there competes for the
// very 512 bytes it is meant to relieve. And the ceiling is not the buffer but
// NAMEID_BITS: a declaration's name field is 9 bits, so ROM and RAM names
// together cannot pass 512 whatever the buffer's size. A define has no
// declaration and therefore no name field, which is exactly what puts it
// outside that limit.
//
// Entries are LENGTH-NAME-TYPE-VALUE, bump-allocated and searched linearly.
// Programs have a handful of defines, not hundreds, and a linear walk over a
// few hundred bytes at parse time costs nothing worth a table.
//
// Sized by what the build can DO, not by which chip it is for:
//
//   CSP_EXEC_ONLY   0. There is no parser, so a #define cannot be created --
//                   the buffer would be RAM spent on a feature the build does
//                   not have. This is the tier CoCo (ATmega328p) runs: it
//                   executes a ROM image compiled on the host, where the names
//                   were already folded away and forgotten.
//   ARDUINO         128. The REPL can still take a #define at the prompt, but
//                   a 2K part should not spend more than that on names it is
//                   going to throw away.
//   host            512.
//
// Set to 0 explicitly to refuse them entirely.
#ifndef CSP_DEFINE_BYTES
#if defined(CSP_EXEC_ONLY)
#define CSP_DEFINE_BYTES 0
#elif defined(ARDUINO) || defined(CSP_SMALL_TARGET)
#define CSP_DEFINE_BYTES 128
#else
#define CSP_DEFINE_BYTES 512
#endif
#endif

#define MAX_NAME_LEN 31    // max var name len
#define MAX_ARGS     4     // max number of arguments to function

// Reserved state numbers, installed by csp_rt_init in this order. INIT is the
// boot state (cycle()==0), NORMAL the running state. FAILSAFE is the designated
// safe state: once the State variable holds it, it is STICKY -- no rule may
// leave it, only a reset -- so a safe configuration cannot be bounced out of by
// a flaky guard. Its `#in FAILSAFE` block (at most one) drives outputs to a
// known-safe setting. User states are numbered from FAILSAFE+1 up.
#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2

// Also the sentinel for an xindex_t: it is XOBJ_GLOBAL with an impossible decl
// index, so `x == BAD_INDEX` reads the same on both types.
#define BAD_INDEX   ((index_t)(MAX_INDICES-1))
#define PARSE_ERROR -1

// Decode/encode an index_t. `u` suffixes throughout: DECL_BITS is 15, and on a
// 16-bit int (AVR) a plain `1 << 15` is undefined.
#define INDEX(n)  ((index_t)((n) & ((1u << DECL_BITS)-1u)))
#define OBJ(n)    ((unsigned)((n) >> DECL_BITS))
#define MAKE_INDEX(obj,x) ((index_t)(((unsigned)(obj) << DECL_BITS) | \
				     ((unsigned)(x) & ((1u << DECL_BITS)-1u))))

// --- xindex_t: what the COMPILER carries -------------------------------------
// An index_t has ONE selector bit, so it can say "global" or "this object" and
// nothing else. The compiler needs a third thing -- "object 7", from `safe.State`
// -- for as long as the reference is travelling from the parser to the emitter.
// xindex_t is that: obj:16 | index:16, narrowed to an index_t by asm_mem_part /
// asm_memi, which emit OP_SETO when the object turns out to be a named one.
//
// Object in the HIGH half on purpose. A xindex_t accidentally assigned to an
// index_t loses the object and reads back as GLOBAL -- wrong object, visibly, in
// a test -- rather than landing on some other object's storage.
typedef uint32_t xindex_t;
#define XOBJ_GLOBAL   0u
#define XOBJ_CURRENT  0xffffu             // "the object being executed"
#define XIDX(n)       ((index_t)((n) & 0xffffu))
#define XOBJ(n)       ((unsigned)(((n) >> 16) & 0xffffu))
#define MAKE_XINDEX(obj,x) (((xindex_t)(unsigned)(obj) << 16) | (index_t)(x))
// A global xindex is its bare decl index (XOBJ_GLOBAL == 0), so a plain index
// widens for free -- but say so where it happens.
#define XGLOBAL(x)    ((xindex_t)(index_t)(x))

#define MAX_PARSE_STACK_DEPTH 10
#ifdef CSP_EMBEDDED
#define MAX_LINE_TOKENS 24
#else
#define MAX_LINE_TOKENS 64
#endif

#define CSP_TRUE  -1  // all bits set, like openCL/Forth
#define CSP_FALSE 0

typedef enum {
    V_VOID     = 0,  // value / don't care
    V_INTEGER  = 1,  // signed integer
    V_UNSIGNED = 2,  // unsigned integer
    V_FLOAT    = 3,  // floating point
    V_STRING   = 4,  // string index
    V_INDEX    = 5,  // declaration index (pass index, not value)
    // match types (not passed in type code)
    V_NUMBER   = 6,  // V_INTEGER | V_FLOAT
    V_ANY      = 7,  // 7 - V_INTEGER | V_FLOAT | V_STRING | V_INDEX
    // object types type that can be use for builtin functions
    V_TIMER    = 8,
    V_DIGITAL  = 9,
    V_ANALOG   = 10,
    V_FIELD    = 11,    
} vtype_t;

// A flag on a CONFIG vtype (what decl_cfg_vt returns), above TYPE_BITS so the
// switches that mask still see the plain type. Everything that indexes a table
// by a cfg vtype must mask -- see CSP_PART_LAY.
//
// V_ANALOG says "this slot is laid out as an analog leaf"; it says nothing about
// how to READ the 16-bit value in it. The declaration's own vt does, and it used
// to be discarded here: `#analog X signed` parsed and meant nothing.
#define CFG_SIGNED 0x10

// create argument type bitmask
#define MAKE_TYPE0()            0
#define MAKE_TYPE1(t0)          (t0)
#define MAKE_TYPE2(t0,t1)       ((t0)|((t1)<<4))
#define MAKE_TYPE3(t0,t1,t2)    ((t0)|((t1)<<4)|((t2)<<8))
#define MAKE_TYPE4(t0,t1,t2,t3) ((t0)|((t1)<<4)|((t2)<<8)|((t3)<<12))

typedef enum {
    E_NATIVE    = 0x00,    
    E_LITTLE    = 0x01,
    E_BIG       = 0x02,
    E_UNDEFINED = 0x03,
} vendian_t;

typedef int32_t  ivalue_t;
typedef uint32_t uvalue_t;
typedef int32_t  sindex_t;

typedef struct PACKED {
    unsigned long period:28; // timeout value ms (74h max)
    unsigned _res:1;         // reserved (auto restart?)
    unsigned fired:1;        // timeout occurred this cycle (edge-triggered)
    unsigned running:1;      // timer is runnig (tx is valid time)
    unsigned val:1;          // one bit value 1 = start, 0 = stop
} tvalue_t;

// cfg is the request "this pin's configuration changed, apply it to the
// hardware". A rule writing .pin/.port/.dir/.pullup/.pulldown only moves bits in
// the value slot; pinMode is called from the board layer, which needs to be
// told. Both slots below are exactly the 32 bits of a value_t with it.
typedef struct PACKED {
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned dir:DIR_BITS;
    unsigned pullup:1;
    unsigned pulldown:1;
    unsigned cfg:1;     // configuration changed, board must re-apply it
    unsigned val:16;    // we may shift in bits...?
} dvalue_t;

// No endian here, unlike csp_analog_t further down. The declaration keeps it and
// .endian answers from there; the copy that used to sit in this slot was written
// by setup, echoed back by .endian and read by nothing else -- byte order that
// MEANS something lives in csp_view_t.endian, which is what a bound field lays
// itself out with. Those two bits pay for cfg and leave one spare.
typedef struct PACKED {
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned dir:DIR_BITS;
    unsigned pwm:1;
    unsigned cfg:1;    // configuration changed, board must re-apply it
    unsigned _res:1;   // spare
    unsigned val:16;
} avalue_t;

typedef enum  {
    PART_VAL=0,
    PART_PIN,
    PART_PORT,
    PART_DIR,
    PART_PWM,
    PART_ENDIAN,
    PART_PULLUP,
    PART_PULLDOWN,
    PART_PERIOD,
    PART_FIRED,
    PART_ID,
    PART_RX,     // TR_CAN buffer: a frame arrived and is now readable (1 cycle)
    PART_TX,     // TR_CAN buffer: write 1 to force a send this cycle
    PART_DLC,    // TR_CAN buffer: bytes to send / bytes last received
    PART_LEN,    // V_STRING: characters in the string. Read-only -- a string
		 // variable holds a POSITION, and lengths are a property of
		 // what it points at, not of the variable.
    PART_LAST,
} csp_part_t;

CSP_STATIC_ASSERT(PART_LAST <= (1 << PART_BITS), "too many parts");

// Which parts describe CONFIGURATION rather than state -- the ones a settings
// entry may carry across a reboot and a reflash (see doc/EEPROM.md). `Led.pin`
// says how this board is wired; `Led` (PART_VAL) turns the LED on to see which
// one it is, and only the first is worth keeping.
//
// A RANGE, and the assert below is what keeps it one: PIN..PERIOD are contiguous
// above, so reordering the enum without moving this line would silently start
// persisting `fired` or a CAN transmit flag.
#define CSP_PART_IS_CFG(p) (((p) >= PART_PIN) && ((p) <= PART_PERIOD))
CSP_STATIC_ASSERT(PART_PERIOD - PART_PIN == 7, "config parts no longer contiguous");

// How to reach a leaf's value. Everything lives in the buffer heap.
// See doc/DESCRIPTORS.md.
// A leaf either OWNS its storage or is a view INTO someone else's. That is the
// distinction that decides whether it needs a csp_buf_t at all.
//
// A csp_buf_t is 16 bytes -- hp, nbytes, transport, dir, flags, dlc, a 32-bit
// xref and an owner index. A leaf that owns four bytes nobody looks into needs
// exactly one of those fields, `hp`, and used to be given all sixteen: measured
// at 38 bytes of RAM per `#variable` on a mega, of which 16 were this. An owner
// carries its heap offset in `pos` instead and allocates no buffer.
//
// Only a #buffer is ever a view PARENT -- `bind` refuses anything else
// (csp_parse_variable checks DECL_BUFFER), and a #field lives inside one by
// construction -- so nothing can be left pointing at a buffer that no longer
// exists.
typedef enum {
    VIEW_SLOT = 0,   // OWNS: a value_t struct at heap offset `pos`
    VIEW_HEAP = 1,   // VIEW: bit-field at bit `pos` of buffer `buf`
    VIEW_OWN  = 2,   // OWNS: bit-field of `len`+1 bits at heap offset `pos`
} view_kind_t;

// SLOT is 0 so "does this leaf own a plain value_t struct" is a test against
// zero. The two owning kinds differ only in how the bytes are read: SLOT is a
// whole value_t (config parts packed into it), OWN is a width the declaration
// gave, which may be narrower than a byte.
#define VIEW_OWNS(v)  ((v)->kind != VIEW_HEAP)

#define VIEW_F_SIMPLE 0x01   // covers the whole storage, byte aligned, native
#define VIEW_F_GLOBAL 0x02   // VIEW_HEAP: buf id is global (not object-offset)
#define VIEW_F_LOCAL  0x02   // VIEW_OWN: a #local -- SINGLE-BUFFERED, both
			     // directions resolve to the DIN half so a rule
			     // reads back what an earlier rule in the SAME cycle
			     // wrote. Shares bit 1 with VIEW_F_GLOBAL because
			     // `kind` already tells the two apart, and the flags
			     // field has no third bit to spend. It used to be
			     // BUF_F_LOCAL on the buffer -- which is gone.

#define VIEW_F_BITS 2
#define VIEW_LEN_BITS 6      // max length is 64 bits
#define VIEW_MAX_LEN  (1 << VIEW_LEN_BITS)
#define VIEW_MAX      (VIEW_MAX_LEN-1)

// One per leaf index_t (indexed by st_index) -- the biggest per-program table
// (nleaf entries), so every byte here is multiplied by the leaf count. kind/vt/
// endian pack into one byte (2+4+2), which pays for a 16-bit buf.
// NOTE: uint8_t bit fields, deliberately NOT a PACKED struct -- packing would
// misalign `buf` and fault on M0 (see the csp_func_t lesson), and `unsigned:16`
// after 26 bits would spill to 8 bytes.
// `buf` is uint16_t: the same width as the nbuf counter (index_t) that produces
// it, so a buffer id can no longer silently truncate the way uint8_t did.
typedef struct {
    uint8_t kind:2;              // view_kind_t
    uint8_t vt:TYPE_BITS;        // value type (vtype_t 0..11); SLOT reads it from decl
    uint8_t endian:ENDIAN_BITS;  // HEAP/OWN: vendian_t (native/little/big)
    uint8_t flags:VIEW_F_BITS;   // VIEW_F_* -- read according to `kind`
    uint8_t len:VIEW_LEN_BITS;   // HEAP/OWN: number of bits - 1
    uint16_t pos;                // HEAP: start bit in buffer
				 // SLOT/OWN: heap BYTE offset of the storage
    uint16_t buf;                // VIEW_HEAP: buffer id. An owner has none.
} csp_view_t;

// csp_buf_t.transport -- what the buffer is bound to on the outside.
//
// TWO SHAPES, and the difference decides where the work happens:
//
//   ASYNCHRONOUS (CAN, UDP) -- packets arrive on their own. csp_buf_input
//   collects whatever showed up; csp_buf_output sends what changed. Nothing
//   here initiates a read.
//
//   SYNCHRONOUS (I2C, SPI) -- a master transaction, and WE are the master.
//   csp_buf_output starts the transfer and csp_buf_input collects it on the
//   NEXT cycle, so a 14-byte read at 100 kHz (1.4 ms, against a 2 ms sample
//   period) costs no loop time at all. The data is one cycle old, which is the
//   trade: overlapped, not blocking.
//
// The numbers are ABI -- a ROM image carries them -- so they are never
// reordered and a retired one is never reused.
typedef enum {
    TR_NONE = 0,        // plain RAM buffer
    TR_PIN  = 1,        // reserved: pin-mapped
    TR_CAN  = 2,        // a CAN frame; xref is the frame id
    TR_I2C  = 3,        // a register read/write; xref is bus/addr/reg
    TR_SPI  = 4,        // a transfer; xref is bus/cs-pin/command
    TR_UDP  = 5,        // a datagram; xref is the IPv4 address, port is separate
} transport_t;

// Is this transport one WE start? The two synchronous buses are, and that is
// the only place the distinction is needed.
#define TR_IS_SYNC(t)  (((t) == TR_I2C) || ((t) == TR_SPI))

// How xref is packed, per transport. One 32-bit constant carries the whole
// endpoint for every transport but UDP, whose address and port do not fit in
// one -- see csp_buf_t.port.
//
//   TR_CAN   the frame id
//   TR_I2C   bus << 16 | addr << 8 | reg
//   TR_SPI   bus << 24 | cs_port << 20 | cs_pin << 16 | command
//   TR_UDP   the IPv4 address in host order; 0 means "listen on any"
#define TR_I2C_XREF(bus,addr,reg)  (((uint32_t)(bus) << 16) | \
				    ((uint32_t)(addr) << 8) | (uint32_t)(reg))
#define TR_I2C_BUS(x)   (((x) >> 16) & 0xff)
#define TR_I2C_ADDR(x)  (((x) >> 8) & 0xff)
#define TR_I2C_REG(x)   ((x) & 0xff)

#define TR_SPI_XREF(bus,po,pi,cmd) (((uint32_t)(bus) << 24) | \
				    ((uint32_t)(po) << 20) | \
				    ((uint32_t)(pi) << 16) | (uint32_t)(cmd))
#define TR_SPI_BUS(x)   (((x) >> 24) & 0x0f)
#define TR_SPI_PORT(x)  (((x) >> 20) & 0x0f)
#define TR_SPI_PIN(x)   (((x) >> 16) & 0x1f)
#define TR_SPI_CMD(x)   ((x) & 0xffff)

// One per unique buffer. RAM table, filled at start.
typedef struct {
    uint16_t hp;        // heap byte offset
    uint16_t nbytes;    // size in bytes (up to 1023 -- widened from the freed loc)
    uint8_t  transport; // transport_t
    uint8_t  dir;       // in/out
    uint8_t  flags;     // BUF_F_*
    uint8_t  dlc;       // TR_CAN: bytes to send / bytes last received. Starts
			// at nbytes (the declared frame size) and is never
			// allowed past it -- the heap has room for no more.
    // UDP's endpoint does not fit in xref: an IPv4 address is already 32 bits
    // and the port is another 16. Here rather than in the DECLARATION, which a
    // ROM image carries and which has four spare bits, not sixteen -- the
    // declaration keeps a string constant and setup_buffer parses it into these
    // two. Zero for every other transport.
    uint16_t port;
    uint32_t xref;      // pin-number / can-id / i2c or spi endpoint / IPv4
    index_t  owner;     // the decl (with object) whose leaf IS this buffer, or
			// BAD_INDEX. Set by setup_buffer, which is the only
			// place that knows both ends. can_mark_fields used to
			// find it by scanning every declaration -- a flash read
			// per decl, per received CAN frame -- and that scan
			// could only ever match a GLOBAL, since it compared a
			// decl index against a leaf index. Those agree only
			// when offs is 0, so a #buffer inside a module was
			// never marked at all.
} csp_buf_t;

// csp_buf_t.flags
#define BUF_F_DIRTY  0x01  // a field changed: an out frame needs sending.
			   // Set at commit (the dset walk already resolves the
			   // owning buffer), cleared by csp_can_output.
#define BUF_F_RXPEND 0x02  // a frame arrived this cycle, not committed yet
#define BUF_F_RX     0x04  // ...and now it is. Set at commit so it is visible
			   // in the SAME cycle as the data it describes, which
			   // is what makes `? F.rx` line up. Lives one cycle.
#define BUF_F_TX     0x08  // a rule asked for a send (F.tx = 1), regardless of
			   // whether any field changed -- cyclic PDO
#define BUF_F_BUSY   0x10  // TR_IS_SYNC: a transaction is in flight. Set when
			   // csp_buf_output starts one, cleared when
			   // csp_buf_input collects it. Without it a slow bus
			   // gets a second transfer queued on top of the first
			   // every cycle, and the bus never drains.
// (BUF_F_LOCAL is gone: a #local owns its storage and has no buffer to carry a
// flag. It is VIEW_F_LOCAL on the view now -- no extra bit, since VIEW_F_GLOBAL
// is a VIEW_HEAP meaning and `kind` separates the two.)

#if defined(USE_FIXPOINT) && (USE_FIXPOINT == 1)
#include "csp_fixpoint.h"
typedef fixpoint_t fvalue_t;
#define FVALUE_IS_FIXPOINT 1
extern int csp_print_fixpoint(fvalue_t v);
#else
typedef float fvalue_t;
#define FVALUE_IS_FIXPOINT 0
#endif

#define op_ADD(y, z)  ((y)+(z))
#define op_SUB(y, z)  ((y)-(z))
#define op_MUL(y, z)  ((y)*(z))
#define op_DIV(y, z)  ((y)/(z))
#define op_REM(y, z)  ((y)%(z))
#define op_BAND(y, z)  ((y)&(z))
#define op_BOR(y, z)   ((y)|(z))
#define op_BXOR(y, z)  ((y)^(z))
// logical 1 == -1 (all bits set)
#define op_AND(y, z)  (-((y)&&(z)))
#define op_OR(y, z)   (-((y)||(z)))
#define op_LT(y, z)   (-((y)<(z)))
#define op_LTE(y, z)  (-((y)<=(z)))
#define op_GT(y, z)   (-((y)>(z)))
#define op_GTE(y, z)  (-((y)>=(z)))
#define op_EQEQ(y, z) (-((y)==(z)))
#define op_NEQ(y, z)  (-((y)!=(z)))
#define op_SLA(y, z)  ((y) << (z))
#define op_SRA(y, z)  ((y) >> (z))
#define op_COMMA(y,z) z

// Float/fixpoint operations - conditional on FVALUE_IS_FIXPOINT
#if FVALUE_IS_FIXPOINT
#define op_FADD(y, z)  FIX_ADD((y), (z))
#define op_FSUB(y, z)  FIX_SUB((y), (z))
#define op_FMUL(y, z)  FIX_MUL((y), (z))
#define op_FDIV(y, z)  FIX_DIV((y), (z))
#define op_FLT(y, z)   (-FIX_LT((y), (z)))
#define op_FLTE(y, z)  (-FIX_LTE((y), (z)))
#define op_FGT(y, z)   (-FIX_GT((y), (z)))
#define op_FGTE(y, z)  (-FIX_GTE((y), (z)))
#define op_FEQEQ(y, z) (-FIX_EQ((y), (z)))
#define op_FNEQ(y, z)  (-FIX_NEQ((y), (z)))
#define op_FNEG(y)     FIX_NEG(y)
#define op_FMOV(y)     (y)
#define op_CVTIF(y)    FIX_FROM_INT(y)
#define op_CVTFI(y)    FIX_TO_INT(y)
#else
#define op_FADD(y, z)  ((y)+(z))
#define op_FSUB(y, z)  ((y)-(z))
#define op_FMUL(y, z)  ((y)*(z))
#define op_FDIV(y, z)  ((y)/(z))
#define op_FLT(y, z)   (-((y)<(z)))
#define op_FLTE(y, z)  (-((y)<=(z)))
#define op_FGT(y, z)   (-((y)>(z)))
#define op_FGTE(y, z)  (-((y)>=(z)))
#define op_FEQEQ(y, z) (-((y)==(z)))
#define op_FNEQ(y, z)  (-((y)!=(z)))
#define op_FNEG(y)     (-(y))
#define op_FMOV(y)     (y)
#define op_CVTIF(y)    ((fvalue_t)(y))
#define op_CVTFI(y)    ((ivalue_t)(y))
#endif

#define op_NOT(y)  (~BOOL((y)))
#define op_NEG(y)  (-(y))
#define op_MOV(y)  (y)
#define op_BNOT(y)  (~(y))

typedef union {
    ivalue_t i;  // V_INTEGER
    uvalue_t u;  // V_UNSIGNED
    fvalue_t f;  // V_FLOAT
    sindex_t s;  // V_STRING (index into string buf)
    tvalue_t t;  // V_TIMER
    dvalue_t d;  // V_DIGITAL
    avalue_t a;  // V_ANALOG
} value_t;

typedef uint32_t set_group_t;  // bit set element
#define BITSET_GROUP_BITS (8*sizeof(set_group_t))
#define BITSET_GROUPS(size) (((size)+BITSET_GROUP_BITS-1)/BITSET_GROUP_BITS)
#define BITSET_GROUP(i) ((i)/BITSET_GROUP_BITS)
#define BITSET_BIT(i)   ((set_group_t)1 << ((i)%BITSET_GROUP_BITS))

#define bitset_decl(name,size) set_group_t (name)[BITSET_GROUPS(size)]
#define bitset_zero(name) memset(&(name), 0x00, sizeof(name))
#define bitset_set(name,i) (name)[BITSET_GROUP((i))] |= BITSET_BIT((i))
#define bitset_clr(name,i) (name)[BITSET_GROUP((i))] &= ~BITSET_BIT((i))
#define bitset_tst(name,i) (((name)[BITSET_GROUP((i))] & BITSET_BIT((i)))!=0)

// The tok_t and dtok_t enums, generated from utils/syntax.terms together with
// the tables they index (CSP_TOK_TABLE in csp_tok.c, CSP_DECL_TABLE in
// csp_compile.c). They used to live here, with nothing able to check them
// against those tables.
#include "csp_tokens.h"

typedef struct {
    char* ptr;
    int len;
} tstr_t;

typedef union
{
    tstr_t str;
    value_t val;
} tokval_t;

// combined token and value
typedef struct
{
    tok_t    t;
    tokval_t v;
} token_t;

typedef enum {
    OP_NOP = 0,  // nothing
    OP_NOT = 1,     // "!"  x=-y == x=0-y
    OP_BNOT = 2,    // "~"  x=~y =  x=1^y        
    OP_NEG  = 3,     // "-"  x=-y == x=0-y
    OP_MOV  = 4,     // "mov" x=y == x=y
    OP_CVTIF = 5,   // trunc float => integer
    OP_CVTFI = 6,   // cast int to float
    // node - binary operator
    OP_ADD   = 7,     // "+"
    OP_SUB   = 8,     // "-"
    OP_MUL   = 9,     // "*"
    OP_DIV   = 10,     // "/"
    OP_REM   = 11,     // "%"
    OP_SLA   = 12,     // "<<"
    OP_SRA   = 13,     // ">>"    
    OP_LT   = 14,      // "<"
    OP_LTE   = 15,     // "<="
    // `>` and `>=` used to be 16 and 17; the compiler mirrors them into
    // OP_LT/OP_LTE with the operands swapped (see mirror_op / asm_alu), so the
    // numbers came free. 16 is spent again below; 17 is still free.
    OP_TMO   = 16,     // timeout(T): x = the timer's `fired` bit
    // 17 -- FREE
    OP_EQEQ   = 18,    // "=="
    OP_NEQ   = 19,     // "!="
    OP_BAND   = 20,    // "&"
    OP_BOR   = 21,     // "|"
    OP_BXOR   = 22,    // "^"
    OP_AND   = 23,     // "&&"
    OP_OR   = 24,      // "||"

    OP_FNEG   = 25,     // "-"  x=-y == x=0-y
    OP_FMOV   = 26,     // "mov"  x=y
    OP_FADD   = 27,     // "+"
    OP_FSUB   = 28,     // "-"
    OP_FMUL   = 29,     // "*"
    OP_FDIV   = 30,     // "/"

    OP_FLT   = 31,      // "<"
    OP_FLTE  = 32,      // "<="
    // 33, 34 -- FREE, for the same reason as 16 and 17 above.
    OP_FEQEQ = 35,     // "=="
    OP_FNEQ  = 36,     // "!="    
    
    OP_EQ    = 37,     // "="
    OP_RIMP  = 38,     // "<-"    

    OP_RULE = 39,    // "?"
    OP_NEXT = 40,    // "next"

    OP_ENTER = 41,   // enter object
    OP_LEAVE = 42,   // leave object
    OP_NEW = 43,     // #<module> <instance-name>
    OP_LD = 44,      // load register from memory
    OP_LDP = 45,     // load register from memory part
    OP_ST = 46,      // store register to memory
    OP_STP = 47,     // store register to memory part
    OP_STIMP = 48,  // store for <- (reactive assign), same as ST but marks rimp
    OP_CHG = 49,     // r |= dset[ix], check if variable changed
    OP_LI = 50,      // load signed 16-bit constant
    OP_LIU = 51,     // load unsigned 16-bit constant (zero extend)
    OP_LIH = 52,     // load high 16-bit (OR into high bits)
    OP_ARG = 53,     // load argument from register
    OP_CALL = 54,    // function call:
    OP_STI = 55,     // store immediate value to memory (mirror of EQI)
    // A run of identifier text living in the INSTRUCTION pool: this header, then
    // num slots of characters. Executing it JUMPS the payload, which is what
    // makes a segment transparent -- it lands mid-stream because a name is
    // created while code is being generated, and there is no moving it
    // afterwards without renumbering every jump.
    //
    // 17 is a hole (where `>` used to be, before it was mirrored to LT+swap),
    // taken ahead of 60..62 as the note at OP_AVAIL asks.
    OP_SEGMENT = 17,

    OP_INSTATE = 56, // #in <state> block gate: if reg != state, skip block(nxt)
    OP_NINSTATE = 57,// #in A B C OR-chain gate: if reg == state, jump INTO block (nxt)
    OP_SETO = 58,    // point CURRENT at a NAMED object for the next memory access
    OP_SETOX = 59,   // same, but the object number comes from a register (arrays)
    OP_AVAIL = 60,   // SENTINEL, not an opcode: one past the highest number in
		// use. NOT the count any more -- 33 and 34 are holes where `>=`
		// used to be (16 was one until OP_TMO took it, 17 until
		// OP_SEGMENT did), so 58 opcodes occupy 60 numbers and there are
		// FIVE free: those two plus 60..62.
		// (63 is OP_END_MARK.) A new opcode should take a hole first.
		// Printed by print_defines and asserted against OP_END_MARK
		// below. Keep it last.
    OP_END_MARK = 0x3f
} opcode_t;

#define CSP_OPCODE_BITS       6
#define CSP_OPCODE_ARITY_BITS 2 // 0
CSP_STATIC_ASSERT(OP_AVAIL <= ((1 << CSP_OPCODE_BITS)-1), "too many opcodes");

// NOT OPCODES. These two are what op_table_code returns for `>` and `>=`, and no
// instruction is ever built from one: process_op mirrors each into OP_LT/OP_LTE
// with the two operands exchanged and
// csp_instr_alu_t.swap set, because `a > b` IS `b < a` -- so the runtime needs
// no cases for them at all and four encodings come free.
//
// #define and not enum members, deliberately: putting them in opcode_t widens
// the enum's range past 63, and `opcode_t op:CSP_OPCODE_BITS` then warns that
// the field is narrower than its own type -- on every translation unit.
//
// ABOVE the 6-bit field, also deliberately. If the mirror is ever missed the
// value cannot be encoded, and asm_alu refuses instead of truncating into an
// unrelated opcode. They are absent from op_info[] and op_tok[]: nothing may
// index a table with one.
#define OP_GT   64      /* ">"   -> OP_LT,   swapped */
#define OP_GTE  65      /* ">="  -> OP_LTE,  swapped */
/* No float pair: mirror_op runs before float_op, so `>` on floats arrives as
   OP_LT and becomes OP_FLT by the ordinary route. */

// Forward declarations
struct _csp_rt_t;
struct csp_instr;

// 4 bits may be used to describe declaration type
// but decl type from 8-15 are also used as object types
typedef enum {
    DECL_NONE=0,            // emtpy declaration
    DECL_VARIABLE=1,        // 'variable'
    DECL_CONSTANT=2,        // 'constant'
    DECL_MODULE=3,          // 'module'
    DECL_END=4,             // 'end'
    DECL_OBJECT=5,          // module instance
    // Was only the `#states` KEYWORD code (like DECL_IN), never stored. It is
    // now also the stored type: one declaration per state NAME. As a declaration
    // it is SCOPED like one -- a state named inside a module body belongs to
    // that module and numbers from its own base, which is what lets many small
    // objects each carry a state machine without sharing one budget -- and it is
    // stored, rolled back, ROM-baked and EEPROM-persisted by the machinery that
    // already does all four for declarations. No separate state table, no
    // separate image section.
    DECL_STATES=6,
    DECL_IN=7,
    
    // 8-15
    DECL_TIMER=V_TIMER,     // 'timer'
    DECL_DIGITAL=V_DIGITAL, // 'digital'
    DECL_ANALOG=V_ANALOG,   // 'analog'
    DECL_FIELD=V_FIELD,         // 'can'
    DECL_BUFFER=12,         // 'buffer' (heap-backed storage)
    DECL_VIEW=13,           // synthetic bit/byte view into a buffer (Buf[a..b])

    DECL_AVAIL,   // SENTINEL, not a decl type: see OP_AVAIL.
    DECL_END_MARK = 0xf
} decl_t;

#define CSP_DECL_TYPE_BITS 4
CSP_STATIC_ASSERT(DECL_AVAIL <= ((1 << CSP_DECL_TYPE_BITS)-1), "too many types");

#define MAKE_RES(r) ((r)-1)
#define GET_RES(rr) ((rr)+1)

#define MAKE_FIELD_LEN(len) ((len)-1)
#define GET_FIELD_LEN(len) ((len)+1)

// A scalar width lives in DECL_COMMON.res and in csp_field_t.len -- both 5 bits
// holding bits-1 -- and a view start bit lives in csp_field_t.bit, 9 bits. Any
// declaration past these wrapped silently and produced a field of the wrong
// width at the wrong place, so the parser refuses instead.
#define MAX_RES_BITS  32    // widest scalar / view a declaration may ask for
#define MAX_VIEW_BIT  511   // highest start bit: the last bit of a 64-byte frame

#define NOTIMEOUT 0xffffffff

typedef struct {
    rostring_t name;     // token name (RODATA)
    uint8_t namelen;
    uint8_t  tok;
    int8_t   code;
    int8_t   arity;
    int8_t   prec;
    int8_t   assoc;
} op_entry_t;

extern const op_entry_t tok_table[] RODATA;
extern const op_entry_t decl_table[] RODATA;

// new instruction format
// general operations OP_ADD ...

#define INSTR_COMMON \
    opcode_t op:CSP_OPCODE_BITS

// u: the operands are UNSIGNED. Only seven opcodes care -- / % >> < <= > >= --
// and everything else (+ - * & | ^ == !=) gives the same bits either way.
//
// A FLAG and not seven more opcodes: OP_AVAIL is already 60 of the 63 the 6-bit
// field can hold, so an unsigned mirror of each would not fit. The word has room
// -- op(6) + three registers(4) is 18 of 32 -- and an image compiled before this
// existed reads back with u == 0, which is the signed behaviour it had.
//
// swap: the operands were EXCHANGED to get here. `a > b` is emitted as `b < a`,
// which is why there is no OP_GT: the runtime already computes the answer, and
// four opcodes buy nothing a swap of two register numbers does not.
//
// Nothing reads it at RUN time -- it is a note for the LISTING. To render the
// source back, both halves have to be undone: exchange the operands AND mirror
// the operator (`LT y=b z=a` -> `a > b`). Doing only one gives `a < b` or
// `b > a`, which are different programs. See exprbuf_expr.
//
// Only the ordered comparisons ever set it. `==` and `!=` are symmetric, so a
// swap on them would be a bit that never means anything.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;
    unsigned y:REG_BITS;
    unsigned z:REG_BITS;
    unsigned u:1;
    unsigned swap:1;    // y <-> z: `y < z` was written `z > y`
} csp_instr_alu_t;

// op = ST | LD | STP | LDP?
// load or store register from memory
//
//   x = mem[y]
//   x = mem[y,z]
//   x = mem[part]
// 
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;      // destination register
    unsigned y:REG_BITS;      // y register when pos, y imm when part (STP)
    unsigned mem:INDEX_BITS;  // declaration: variable/constant
} csp_instr_mem_t;

// op EQI - compare 8 bit immediate with memory and store in x
#define TINY_BITS 6
#define TINY_MAX ((1 << 5)-1)
#define TINY_MIN (-(1 << 5))
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;      // destination register
    signed imm:TINY_BITS;     // signed tiny immediate bits
    unsigned mem:INDEX_BITS;  // declaration: variable/constant
} csp_instr_memi_t;

// op LI / ARG
// load immediate LI load small 16 bit signed constant
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;
    signed imm:16;
} csp_instr_imm_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned cnd:REG_BITS; // condition register
    signed   nxt:15;       // relative jump if !cnd (was int16 -- 15 bits is plenty)
    unsigned implicit:1;   // 1 = bare NORMAL+ rule: list bare, suppress its
			   // implicit State==INIT||State==NORMAL guard
} csp_instr_rule_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;   // body result
} csp_instr_next_t;

// op INSTATE - #in <state> block gate. A LD of the state variable precedes it;
// if that register != imm, jump nxt to skip the whole block (sequential path).
// OP_NINSTATE shares this layout but inverts the test: if x == imm, jump nxt to
// enter the block (used to OR-chain a multi-state `#in A B C`, see csp_parse_in).
// implicit: set on the auto NORMAL+ gate wrapping a bare top-level rule, so the
// listing renders that rule bare instead of emitting a `#in NORMAL` header.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;   // register holding the current State value
    signed   imm:8;        // target state number
    signed   nxt:13;       // relative jump (skip block if !=, enter block if ==)
    unsigned implicit:1;   // 1 = auto NORMAL+ wrap: list the rule bare
} csp_instr_instate_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:BODY_BITS;   // number of instructions (shares the word with mx)
    index_t  mx;     // module index
} csp_instr_enter_t;

// A string segment header. `num` payload slots follow it, holding identifier
// text; `used` is how many BYTES of them are in use, which is what lets an
// image say how much string space it carries without a header field (only the
// last segment's value is ever read).
typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:BODY_BITS;   // payload slots that follow
    unsigned used:8;          // bytes used in this segment
} csp_instr_seg_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:BODY_BITS;   // number of instructions (shares the word with mx)
    index_t  mx;     // module index
} csp_instr_leave_t;

// Instantiate an object: enter its module body like a call.
//
// The entry point is NOT here. It used to be `ent:BODY_BITS`, an ABSOLUTE
// instruction index in ten bits -- so a module whose ENTER landed past 1023 had
// its entry truncated silently: 1557 became 533, and the object called into the
// middle of unrelated code with no error anywhere. The module DECLARATION
// already carries it as a full index_t, reachable from obj in two steps
// (obj_entry), which costs nothing at a call and removes the ceiling.
typedef struct PACKED {
    INSTR_COMMON;
    index_t  obj;            // object declaration index
} csp_instr_new_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;     // result register
    unsigned idx:FUNC_BITS;  // function index
    unsigned usr:1;          // user function
    unsigned avt:16;         // argument value types 4 bit per argument
} csp_instr_call_t;

// OP_SETO - point CURRENT at a NAMED object (`safe.State`), for ONE access.
//
// An encoded index has a single selector bit -- global or current object -- so a
// reference to a named object cannot say which one. This instruction says it, and
// the memory instruction that FOLLOWS reads CURRENT-relative.
//
// One-shot: the next memory op consumes it and the runtime puts the object
// context back (see eval_op). That is what makes it safe to place anywhere. A
// sticky base register would have to be paired with a restore, and a rule's
// conditional jump (csp_instr_rule_t.nxt) can skip forward over instructions --
// so a taken jump could leave the base pointing at the wrong object for whatever
// ran next. There is nothing to leave stale here.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned obj:16;         // object table index (1..MAX_OBJECT_NUM)
} csp_instr_seto_t;

// OP_SETOX - the same one-shot as OP_SETO, but for an ARRAY ELEMENT chosen at
// runtime: `P[Idx]`.
//
// NOT an object number. OP_SETO names an object and looks up offs[]; this one
// shifts the base by an ELEMENT, which is why an array costs no DECL_OBJECT, no
// offs[] slot and no object[] slot per element:
//
//     cbase = offs[cur] + reg * stride
//
// and the memory instruction that follows adds the array's own index, so it
// lands on element `reg`. offs[cur] keeps it correct inside a module, where the
// array's index is module-relative; at global level offs[0] is 0 and the base
// is just reg*stride.
//
// An array occupies `len` CONSECUTIVE declarations, one per element -- view[] is
// indexed by declaration index (see st_index), so elements cannot share one
// declaration without colliding with whatever is declared next. That is a cost
// (8 bytes per element) and a feature: each element carries its OWN config, so
// `#analog P[10] out 9:0..9` gives every element its own pin with no special
// case in setup.
//
// stride is members per element: 1 for a scalar array, the module's member count
// for an array of instances. 6 bits, so an arrayed module tops out at 63
// members.
//
// UNLIKE OP_SETO the operand is not trusted -- a register holds whatever the
// program computed. `len` is here so the check is the RIGHT one: `P[99]` fails
// against the array's own length rather than merely staying inside the arena.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;     // register holding the element index
    unsigned len:16;         // element count, for the bounds check
    unsigned stride:6;       // declarations per element (1 = scalar array)
} csp_instr_setox_t;
#define MAX_ARRAY_STRIDE ((1u << 6) - 1u)

// OP_END_MARK: a self-verifying terminator appended after rom_instr's data. Its
// crc is a CRC-16 over [the section's data + this marker with crc zeroed], so the
// instruction section can be verified WITHOUT the header -- scan for OP_END_MARK,
// its position is the section length, its crc confirms integrity. _res pads so
// crc lands byte-aligned at bytes 2-3 (INSTR_COMMON is op:6). See rom_scan_end.
typedef struct PACKED {
    INSTR_COMMON;            // op == OP_END_MARK (0x3f)
    unsigned _res:10;        // pad to a 2-byte boundary
    uint16_t crc;            // section self-CRC (bytes 2-3)
} csp_instr_end_t;

typedef union {
    // uint32 need on arduino uno (unsigned is 16 bit?)
    struct PACKED { INSTR_COMMON; uint32_t rest:26; };
    csp_instr_enter_t e;
    csp_instr_seg_t   sg;
    // A segment's payload slot: four bytes of identifier text, no fields. The
    // arm exists so the ROM generator can write one and the loader read one
    // without pretending it is an instruction.
    struct PACKED { uint8_t b[4]; } raw;
    csp_instr_leave_t v;
    csp_instr_new_t n;
    csp_instr_imm_t i;
    csp_instr_mem_t m;
    csp_instr_memi_t mi;
    csp_instr_call_t f;
    csp_instr_rule_t r;
    csp_instr_next_t x;
    csp_instr_instate_t in;
    csp_instr_alu_t a;
    csp_instr_seto_t o;
    csp_instr_setox_t ox;
    csp_instr_end_t em;
} csp_instr_t;

// The instruction word must stay a clean 4 bytes: every format has to fit
// INSTR_COMMON(6) + a full index_t(16) leaves 10 bits (BODY_BITS) for any packed
// index. If this fails after a bit-width change, a format overflowed 32 bits.
CSP_STATIC_ASSERT(sizeof(csp_instr_t) == 4, "csp_instr_t must be 4 bytes");
// The opcode field is CSP_OPCODE_BITS wide and OP_END_MARK reserves the top
// value, so the sentinel has to stay below it. Adding one opcode past this is a
// silent overlap with the section terminator -- exactly the kind of thing the
// -w Arduino build would hide.
CSP_STATIC_ASSERT(OP_AVAIL <= OP_END_MARK, "out of opcodes");
CSP_STATIC_ASSERT(OP_END_MARK < (1 << CSP_OPCODE_BITS), "opcode field too narrow");

typedef enum {
    DIR_NONE  = 0x00,
    DIR_IN    = 0x01,
    DIR_OUT   = 0x02,
    DIR_INOUT = 0x03
} pindir_t;

// we may mark declarations as system created using
//   type:CSP_DECL_TYPE_BITS;
//   unsigned sys:1
//
// Declarations come in two shapes, and the split matters because csp_states_t
// packs six name positions across the whole 8-byte declaration -- so whatever is
// NOT in the shared part sits on top of a state name.
//
// DECL_HEADER is what EVERY declaration has: what it is, which way it faces, and
// what it is called. 17 bits. csp_states_t begins with it, which is what makes
// slot 0 and `name` the same bits BY CONSTRUCTION rather than by two field lists
// that happen to line up -- the first of the three bugs this aliasing has cost.
//
// DECL_TYPE_HEADER adds what a VALUED declaration needs: its value type, its
// width, and the register cache. Those five fields are exactly the ones that
// overlap a states block's name2 and name3, and tests/states_layout.c pins down
// which slot each one lands in. Anything added here takes bits from a state
// name; anything added to DECL_HEADER takes them from all six.
// `cont`: this declaration CONTINUES the array declared just above it.
//
// An array occupies one declaration per element (view[] is indexed by
// declaration index -- see st_index -- so elements cannot share one). Only the
// head carries the name; the rest are unnamed copies with this bit set. That is
// what lets the compiler recover an array's LENGTH at a use site without storing
// it: scan forward from the head while the bit holds. One bit instead of a decl
// type (there is exactly one left) or a field csp_variable_t has no room for.
//
// It also tells the listing to print `#variable A[3]` once instead of three
// declarations that all claim to be called A.
//
// It is a NAMED field, not a mask into a `_reserved`: a bit with a meaning
// should read as `d.cont` at every use, and the ROM emitter should write
// `.cont=` so the arm says what it carries. One spare bit is left.
// `local`: a #local -- a named FORMULA, not a variable you can assign.
//
// It is a DECL_VARIABLE with this bit rather than a declaration type of its own,
// for safety as much as for space: a new type would need a new arm in every
// switch over decl types (setup_decl, the ROM emitter, both listings, estimate),
// and a missed arm is the bug this project keeps meeting. As a flag, all the
// existing variable handling applies unchanged and only what cares looks.
//
// What it changes: the formula is evaluated once per cycle by a prologue before
// the rules, its leaf is copied DOUT->DIN right after so a read in the SAME
// cycle sees it (csp_set_value; the copy was missing for a long time and a
// chain of locals lagged one cycle per step, exactly like variables), it gets
// no /state row, its name is not stored at all -- it lists as $N and lives in
// the compiler's define buffer until #end -- nothing outside the module may
// read it, and assigning to it is an error.
//
// It DOES go to EEPROM, and must: a local is a declaration plus the rule that
// computes it, and without the declaration that rule has no target. (This note
// claimed otherwise for a long while. The save/load round trip was always
// correct; the sentence was not.)
//
// This was the last spare bit in the header. Nothing free after it.
#define DECL_HEADER \
    decl_t type:CSP_DECL_TYPE_BITS; \
    unsigned cont:1; \
    unsigned local:1; \
    pindir_t dir:DIR_BITS; \
    unsigned name:NAMEID_BITS

#define DECL_TYPE_HEADER \
    DECL_HEADER; \
    unsigned vt:TYPE_BITS; \
    unsigned res:5; \
    unsigned is_mapped:1; \
    unsigned bound:1; \
    unsigned reg:REG_BITS

// The old name, for the arms that carry a value. Kept because it reads well at
// the top of every one of them and there are a dozen.
#define DECL_COMMON DECL_TYPE_HEADER

typedef struct PACKED {
    DECL_COMMON;
    index_t n;          // number of nodes in module definition
    index_t ent;        // entry point in instr
} csp_module_t;

typedef struct PACKED {
    DECL_COMMON;    
    index_t  mx;           // module declaration index
    unsigned m:16;         // index in object table (1..MAX_OBJECT_NUM)
} csp_object_t;

// A `#states` name. The name itself is in DECL_COMMON; snum is the value the
// State variable takes and the number OP_INSTATE compares against, so it has to
// match csp_instr_instate_t.imm -- 8 bits, signed there, and 0..2 are the
// reserved INIT/NORMAL/FAILSAFE. Numbered per SCOPE: global states from 3, and
// each module's own states from 3 again, independently.
/*
typedef struct PACKED {
    DECL_COMMON;
    unsigned snum:8;       // state number within its scope
} csp_statedecl_t;
*/

// Up to 6 state names per declaration: DECL_HEADER (17) + 5 x 9 = 62 of 64 bits.
//
// DECL_HEADER, not a hand-copied prefix: `name` is slot 0, so a states block IS
// a declaration with a name -- the one every other reader already knows how to
// look at -- and the alias cannot drift, because there is only one definition of
// it now. What DECL_TYPE_HEADER adds on top (vt, res, is_mapped, bound, reg)
// lands on name2 and name3; see tests/states_layout.c.
typedef struct PACKED {
    DECL_HEADER;                  // type, dir, and slot 0 as `name`
    unsigned name2:NAMEID_BITS;
    unsigned name3:NAMEID_BITS;
    unsigned name4:NAMEID_BITS;
    unsigned name5:NAMEID_BITS;
    unsigned name6:NAMEID_BITS;
} csp_states_t;

// Slots in one states block. Bit fields cannot be indexed, so reading slot k
// goes through the switch below rather than an array -- which is also why the
// count lives here and not as a bare 6 in the loops.
#define CSP_STATES_PER_DECL 6

typedef struct PACKED  {
    DECL_COMMON;    
    value_t init;    // init value
} csp_variable_t;

typedef struct PACKED  {
    DECL_COMMON;    
    value_t init;   // constant value
} csp_constant_t;

typedef struct PACKED  {
    DECL_COMMON;    
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned pullup:1;
    unsigned pulldown:1;
} csp_digital_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned pwm:1;    // pwm output
    unsigned endian:2; // |little|big
} csp_analog_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned id:INDEX_BITS; // the #buffer this field is a view into
    unsigned endian:2; // |little|big
    unsigned bit:9;   // 0-511   // bit start pos
    unsigned len:5;   // (1-32)  // data length -1
} csp_field_t;

// #buffer. Its size does NOT live in DECL_COMMON.res: that is 5 bits holding
// bits-1, so anything past 32 bits truncated silently (a 64-bit buffer became
// 4 bytes). nbytes here is the one source of truth for how big a buffer is.
typedef struct PACKED {
    DECL_COMMON;
    unsigned nbytes:10;     // 1..1023; a CAN FD frame is 64 bytes
    // 3 bits, not 2: TR_UDP is 5. The word had four spare bits, so this costs
    // nothing -- csp_bufdecl_t is 8 bytes before and after.
    unsigned transport:3;   // transport_t: TR_NONE plain RAM, TR_CAN a frame
    unsigned id:INDEX_BITS; // the constant holding this transport's endpoint:
			    // a frame id, a packed bus/addr/reg, or an IPv4
			    // address. See transport_t for the packings.
			    //
			    // TR_UDP USES TWO: `id` holds the IPv4 address and
			    // `id + 1` the port. A constant is 32 bits and this
			    // endpoint is 48, and a port field here would take
			    // csp_decl_t from 8 bytes to 12 -- four bytes on
			    // every declaration of every kind, to carry one
			    // number for one transport. csp_parse_buffer makes
			    // the pair with new_signed_const twice and REFUSES
			    // to compile if they did not come out adjacent, so
			    // the assumption cannot rot quietly.
} csp_bufdecl_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned long period:28; // timeout value ms (74h max)
    unsigned _res:1;         // reserved
    unsigned fired:1;        // timeout occurred this cycle (edge-triggered)
    unsigned running:1;      // timer is runnig (tx is valid time)
    unsigned init:1;         // one bit value 1 = start, 0 = stop
} csp_timer_t;

// DECL_END_MARK: self-verifying terminator appended after rom_decl's data (the
// counterpart to csp_instr_end_t for the decl section). DECL_COMMON is exactly 4
// bytes, so crc lands byte-aligned at bytes 4-5. See rom_scan_end.
typedef struct PACKED {
    DECL_COMMON;             // type == DECL_END_MARK (0xf)
    uint16_t crc;            // section self-CRC (bytes 4-5)
    uint16_t _res;           // pad to 8 bytes
} csp_decl_end_t;

typedef union {
    struct PACKED { DECL_COMMON; };
    csp_module_t   md;
    csp_object_t   mq;
    csp_variable_t va;
    csp_constant_t cn;
    csp_digital_t  di;
    csp_analog_t   an;
    csp_field_t    ca;
    csp_bufdecl_t  bf;
    csp_timer_t    tm;
    // csp_statedecl_t sd;
    csp_states_t   s6;
    csp_decl_end_t em;
} csp_decl_t;


// Name position of slot k in a states block, 0 when the slot is padding. Slot 0
// is DECL_COMMON's `name` -- csp_states_t lines its first three fields up with
// DECL_COMMON on purpose -- so anything reading `d.name` sees the block's first
// state and nothing has to special-case it.
static inline sindex_t csp_states_name(const csp_decl_t* d, int k)
{
    switch (k) {
    case 0: return d->s6.name;
    case 1: return d->s6.name2;
    case 2: return d->s6.name3;
    case 3: return d->s6.name4;
    case 4: return d->s6.name5;
    case 5: return d->s6.name6;
    default: return 0;
    }
}

// Write slot k. The counterpart to the reader above, and the only place a slot
// is assigned -- a states block must never be filled through DECL_COMMON, whose
// `vt` and `res` fields sit on top of name2 and name3.
static inline void csp_states_set_name(csp_decl_t* d, int k, sindex_t pos)
{
    switch (k) {
    case 0: d->s6.name  = pos; break;
    case 1: d->s6.name2 = pos; break;
    case 2: d->s6.name3 = pos; break;
    case 3: d->s6.name4 = pos; break;
    case 4: d->s6.name5 = pos; break;
    case 5: d->s6.name6 = pos; break;
    default: break;
    }
}

typedef enum {
    ERR_OK = 0,
    ERR_SYNTAX,
    ERR_TOO_MANY_TOKENS,
    ERR_TOO_MANY_DEFINES,
    ERR_NO_DEFINES,
    ERR_LOCAL_SCOPE,
    ERR_RESERVED_NAME,
    ERR_STRING_SPACE_EXHUSTED,    
    ERR_TOO_MANY_DECLARATIONS,
    ERR_TOO_MANY_INSTRUCTIONS,
    ERR_TOO_MANY_OBJECTS,
    ERR_MODULE_NOT_DECLARED,
    ERR_TOO_MANY_STATES,    
    ERR_STATE_NOT_DECLARED,
    ERR_END_MISMATCH,
    ERR_NOT_A_MODULE,
    ERR_NOT_A_BUFFER,
    ERR_OBJECT_NOT_DECLARED,
    ERR_VARIABLE_NOT_DECLARED,
    ERR_FIELD_NOT_FOUND,
    ERR_FUNCTION_DOES_NOT_EXIST,
    ERR_INTERNAL_ERROR,
    ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH,
    ERR_ALREADY_DEFINED,
    ERR_NAME_TOO_LONG,
    ERR_BAD_RULE_RANGE,
    ERR_NO_SUCH_RULE,
    ERR_CANNOT_SAVE,
    ERR_CANNOT_LOAD,
    ERR_NUMBER_RANGE,
    // The arena could not hold the program's derived tables (view, heap, buffer
    // table, reactive graph). Distinct from ERR_TOO_MANY_DECLARATIONS, which is
    // about a COUNT hitting an encoding limit: this one is about bytes, and it
    // is what a `#buffer` too large for the board reports.
    ERR_OUT_OF_MEMORY,
    // The only RUNTIME error in this enum: an array index evaluated outside the
    // array (OP_SETOX). Every other entry is raised while parsing or rebuilding,
    // where there is a line to blame; this one is raised mid-rule, so it is
    // sticky (csp_set_error keeps the first) rather than reported per cycle.
    ERR_INDEX_RANGE,
    // A rule tried to assign to a #local. The name resolves fine -- it is what
    // it MEANS that is wrong -- so this cannot be reported as an unknown
    // variable without sending the reader looking for a typo.
    ERR_ASSIGN_TO_LOCAL,
    ERR_ASSIGN_TO_PARAM,
    ERR_PARAM_SHAPE,
} csp_err_t;

// parser state, save state before parse
// so that restore may be possible on error
typedef struct PACKED {
    index_t nn;                  // number of instructions
    index_t nd;                  // number of decls
    index_t nq;                  // number of objects
    index_t ns;                  // number of states
    uint32_t strp;               // string table position in BYTES (grows up)
    // How many strings are in the table. A string HANDLE is an index into this
    // count -- handle N is the Nth string -- so this is also the next handle to
    // hand out. Kept alongside strp rather than derived, because new_string
    // needs it on every allocation; anything that moves strp from the outside
    // (ROM load, EEPROM load, /clear, an undo) calls csp_str_recount instead.
    uint32_t nstr;
    uint32_t err_strp;           // error scratch cursor (grows down from CSP_ERR_STR_BYTES)
    csp_err_t err;               // error code
    uintptr_t err_args[3];       // error arguments for printf
    uint32_t line;               // line number when parsing
} csp_pstate_t;

// Full parse mark: csp_pstate_t plus every cursor a parse mutates that does
// not live in it. Rewinding nn/nd/nq/ns/strp un-writes the emitted code
// implicitly, but the module/state cursors would survive a failed line and
// swallow everything typed after it into a module that never gets its #end.
typedef struct {
    csp_pstate_t ps;
    index_t  mdef;               // module being defined
    int      ent;                // entry op of that module
    int      sdef;               // state being defined
    index_t  in_marker;          // pending OP_INSTATE gate
    xindex_t save_sx;            // sx saved across the module body
    xindex_t sx;                 // state variable
    index_t  cur;                // current module index
    index_t  n_rule_emit;        // rules emitted so far
} csp_pmark_t;

// Function pointer types
typedef value_t (*csp_func_fn)(struct _csp_rt_t* st, uint16_t type,
			       value_t* args, uint8_t nargs);

typedef int (*csp_const_fn)(struct _csp_rt_t* st, const char* name, int len,
			    value_t*, vtype_t*);

// csp_func_t.flags bits (packed; a whole byte so ro_byte(&e.flags) works)
#define FUNC_PURE   0x01        // side-effect free
#define FUNC_RONAME 0x02        // .name points to a rochar (RODATA) string

// Is a func table in ROM? Builtin is compile-time (RODATA on AVR); user funcs
// pass a flag to csp_set_ufuncs. On the host RODATA==RAM so this is always 0
// and the rom-aware readers collapse to plain access.
#if defined(__AVR__)
#define BUILTIN_ROM 1
#else
#define BUILTIN_ROM 0
#endif

// Function table entry
typedef struct {  // not packed?
    const char* name;
    uint8_t namelen;
    uint8_t arity;              // number of arguments (0-4)
    uint8_t flags;              // FUNC_PURE | FUNC_RONAME
    uint8_t rtype;              // return type
    uint16_t argtypes;          // argument types MAKE_TYPEx
    csp_func_fn fn;             // function to call
} csp_func_t;

typedef struct
{
    reg_t   free_regs[MAX_REGS];
    index_t rmap[MAX_REGS];
    int top;
    int temp_top;
    int pin_top;
} reg_allocator_t;

// Tokenizer + parser state. See csp_rt_t.cs for why it lives inside the runtime
// struct rather than beside it.
typedef struct {
    reg_allocator_t* ap;         // register allocator
    int ev;                      // eval variables when ev=1
    int sdef;                    // current state (compile time); sdefv[0], -1=none
    uint8_t sdefv[MAX_IN_STATES];// states of the current #in block (OR-list)
    uint8_t n_sdef;              // number of states in sdefv (1 = plain #in <s>)
    uint8_t rule_implicit;       // next OP_RULE is a bare NORMAL+ rule (list bare)
    index_t in_marker;           // instr index of the pending OP_INSTATE block
                                 // gate (the terminating INSTATE of the OR-chain;
                                 // patched with the skip distance at #end)
    // The INSTATE of the last `#in INIT` block a DECLARATION emitted, +1 (0 =
    // none), so consecutive declarations share one gate instead of one each.
    // See asm_decl_init -- which also explains why no separate "is it still the
    // last thing emitted" flag is needed.
    index_t dinit_mark;
    // xindex_t: inside a module body this is that module's own State, which is
    // CURRENT-relative -- an index_t would drop the selector on assignment.
    xindex_t save_sx;            // save sx during module parse
    xindex_t sx;                 // state variable being parsed against; inside a
                                 // module it is that module's own State
    uint8_t no_state;            // #module: do not give it a State of its own.
                                 // See csp_parse_module -- a data-only namespace
                                 // has nothing to name a state for.
    index_t mdef;                // module being defined
    csp_pmark_t mod_mark;        // parse mark taken at #module: a failure before
                                 // #end rewinds the whole module, so the lines
                                 // after it are not silently absorbed into a
                                 // module that can never be closed
    int     ent;                 // entry op of module in st->instr
    // temp var list during <- parsing (own scratch, set by csp_rt_init).
    // xindex_t: an entry becomes an OP_CHG on the variable, and `x <- safe.a`
    // has to watch safe's field, not the module template's.
    xindex_t  var_buf[MAX_VARREFS];
    xindex_t* var;
    index_t   nvar;
    int      rimp;               // 1 if parse_expr is in RHS in <-
    // Pending array subscript, the compile-time mirror of the runtime's one-shot.
    // `A[expr]` evaluates the index into a register and then the ACCESS is
    // emitted by the ordinary path, so the two facts the SETOX needs -- which
    // register, how long the array is -- have to survive the gap. asm_seto is the
    // single funnel every mem instruction passes through, so it is what consumes
    // them, exactly where OP_SETO is emitted for a named object.
    //
    // NOTHING PENDING IS arr_len == 0, not a -1 in arr_reg: csp_rt_init memsets
    // the struct and writes back only the fields whose default is non-zero, so a
    // -1 sentinel would need its own store AND would be wrong for every path
    // that rebuilds the state without going through that line. A declared array
    // is at least one element (csp_parse_variable refuses 0), so a zero length
    // cannot mean anything else.
    uint8_t  arr_reg;            // register holding the element index
    uint16_t arr_len;            // element count; 0 = no subscript pending
    // The #local whose OWN formula is being compiled, +1 so a zeroed struct
    // means "none" (declaration index 0 is a real one). coerce_assign refuses an
    // assignment to any other local; this is what lets the declaration emit the
    // single assignment that defines it.
    index_t  local_def;
} csp_cstate_t;

// Everything that only matters while a CYCLE runs: the evaluator's registers,
// the settle/quiesce flags the loops read, and the reactive scheduler. Grouped
// for the same reason csp_cstate_t is -- so it is visible when the compiler or
// the command layer reaches into exec state, and so the two halves of the
// runtime stop sharing one flat namespace.
//
// Like cs it stays INSIDE csp_rt_t: the evaluator reads decls and writes the
// heap, both reached through st. The struct is a named surface, not a boundary
// you can pass on its own.
typedef struct {
    value_t reg[MAX_REGS];       // register area
    value_t arg[MAX_ARGS];       // loaded before call
    uint32_t update;             // update counter
    uint32_t wait_ms;            // sleep time or NOTIMEOUT
    int8_t   anyd;               // CSP_TRUE|CSP_FALSE: the cycle changed something
    unsigned seed_all:1;         // 1 during the first cycle: OP_CHG reads true so
                                 // every <- binding fires once and seeds a value
    unsigned sweep:1;            // full sequential sweep (OP_NEW/LEAVE enter/leave)
    // Pending one-shot OP_SETO: the executing object number PLUS ONE, so 0 means
    // nothing is pending. The next instruction runs with cur/cbase pointed at the
    // named object, and eval_op's tail puts this back.
    uint8_t  sobj;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    index_t* rule_ip;            // ordinal -> instruction index (own alloc, n_rule)
    // ordinal -> State membership mask (0 = ungated). The mask's WIDTH is the
    // only cap on how many States a `#in` gate can tell apart -- see
    // csp_gate_mask_t / MAX_GATE_STATES.
    csp_gate_mask_t* rule_state;
    index_t  n_rule;             // rule bodies (ROM + RAM); 0 until csr has run
    set_group_t* pending[2];     // own allocation, pending_cap bits each
    uint32_t pending_cap;        // key space in bits = n_rule << obj_shift
    uint8_t  obj_shift;          // object field width: ceil(log2(nq+1))
    uint8_t  gen;                // which set csp_enq fills (the other is running)
    index_t  graph_n;            // node count the graph currently covers
    index_t* idg;                // in-degree per decl                   [graph_n]
    index_t* ofs;                // edge offset per decl                 [graph_n+1]
    index_t* edg;                // back edges (decl -> rules)           [ofs[graph_n]]
#endif
} csp_estate_t;

typedef enum { DIN = 0, DOUT = 1 } dio_t;

// (state_t and the image's state section are gone as of format v10: a state is a
// DECL_STATES declaration, so it is stored, CRC'd, ROM-baked and EEPROM-
// persisted by the declaration machinery. See add_state.)

// The section pointers of one image, derived from its base. The runtime never
// names an image's struct type -- it takes the base and works in offsets, so
// the same code loads rom, a FAILSAFE, or a copy someone flashed onto a spare
// page. Two ways to fill it in: from the header (fast) or by walking the
// section prologues (when the header is the casualty).
typedef struct {
    const char*        str;
    const csp_decl_t*  decl;
    const csp_instr_t* instr;
    const index_t*     idg;
    const index_t*     ofs;
    const index_t*     edg;
} img_p_t;


// The image type for ONE program. The generator knows every count, so it stamps
// them in here and the COMPILER does the layout -- which is what keeps the
// byte-CRC honest. Sizes are the emitted lengths, trailers included:
//   NSTR   = n_str + 3     (0xFF sentinel + 2-byte crc)
//   NDECL  = n_decl + 1    (DECL_END_MARK)
//   NINSTR = n_instr + 1   (OP_END_MARK)
//
// aligned(4) on the object matters: the header is PACKED, so without it the
// struct's own alignment would come from index_t (2) and every section start
// could land on a 2-boundary.
#define CSP_IMAGE_TYPE(tname,NSTR,NDECL,NINSTR,NIDG,NOFS,NEDG)          \
    typedef struct {                                                    \
        csp_image_header_t hdr;                                         \
        csp_sect_t  s_str;                                              \
        char        str[NSTR];                                          \
        uint8_t     _pad_str[CSP_PAD4(NSTR)];                           \
        csp_sect_t  s_decl;                                             \
        csp_decl_t  decl[NDECL];                                        \
        csp_sect_t  s_instr;                                            \
        csp_instr_t instr[NINSTR];                                      \
        csp_sect_t  s_idg;                                              \
        index_t     idg[NIDG];                                          \
        uint8_t     _pad_idg[CSP_PAD4(2*(NIDG))];                       \
        csp_sect_t  s_ofs;                                              \
        index_t     ofs[NOFS];                                          \
        uint8_t     _pad_ofs[CSP_PAD4(2*(NOFS))];                       \
        csp_sect_t  s_edg;                                              \
        index_t     edg[NEDG];                                          \
        uint8_t     _pad_edg[CSP_PAD4(2*(NEDG))];                       \
    } __attribute__((aligned(4))) tname

// The generator computes the offsets ITSELF -- it must, because crc_hdr covers
// them and a CRC cannot be taken over values only the C compiler knows. These
// assert that the compiler agrees with that arithmetic. If the two ever diverge
// the BUILD fails instead of the image being quietly wrong.
#define CSP_IMAGE_CHECK(tname,OSTR,ODECL,OINSTR,OIDG,OOFS,OEDG,SZ)         \
    CSP_STATIC_ASSERT(offsetof(tname,str)    == (OSTR),   "ofs_str");      \
    CSP_STATIC_ASSERT(offsetof(tname,decl)   == (ODECL),  "ofs_decl");     \
    CSP_STATIC_ASSERT(offsetof(tname,instr)  == (OINSTR), "ofs_instr");    \
    CSP_STATIC_ASSERT(offsetof(tname,idg)    == (OIDG),   "ofs_idg");      \
    CSP_STATIC_ASSERT(offsetof(tname,ofs)    == (OOFS),   "ofs_ofs");      \
    CSP_STATIC_ASSERT(offsetof(tname,edg)    == (OEDG),   "ofs_edg");      \
    CSP_STATIC_ASSERT(sizeof(tname)          == (SZ),     "image size")

// Register an image with the LINKER, so a firmware that carries several can
// enumerate them. The section name has no leading dot, which is what makes GNU
// ld generate __start_/__stop_ symbols for it -- no linker script needed.
// Verified on host gcc, avr-gcc 4.8.1 and arm-none-eabi 7.2.1, all surviving
// Arduino's -ffunction-sections -fdata-sections -Wl,--gc-sections.
//
// The entries are ADDRESSES of image objects, and the array is deliberately
// NOT const-qualified as a whole. On AVR that puts it in .data -- RAM, two bytes
// per image -- and a plain read works. Made read-only it would be an orphan
// section placed after all the code: measured at 0x17d30 on a 98 kB mega
// firmware, which is past the 64 kB that memcpy_P/pgm_read_byte can reach, so
// every entry would read back as garbage. Six bytes of RAM for three images is
// the cheaper problem.
// The POINTER is const too, not just what it points at. Without the second
// const the object is writable, so gcc emits `csp_images` as a writable section
// and the linker -- which has no entry for this name in any of the platform
// scripts, so it places it as an orphan -- puts it in RAM right after .data.
// It then falls OUTSIDE __data_start__/__data_end__, so the startup copy never
// initialises it, and outside .bss, so nothing zeroes it either: the entry read
// back whatever was in RAM at power-on. It also pushed .bss along, which is
// where "changing start of section .bss by 4 bytes" came from.
//
// Const all the way makes it read-only, so it lands in flash with the rest of
// the image it names -- initialised, and costing no RAM.
// CSP_NO_IMAGE_REGISTRY drops the custom section entirely. The registry answers
// "what did this build LINK", which only /images and csp_find_image ask -- the
// firmware's own image is reached through `rom_image`, so a board without the
// registry still loads and runs its ROM exactly as before.
//
// It exists because `csp_images` is an ORPHAN section: no linker script on any
// of these platforms names it, so where it lands is a heuristic that differs by
// ld version and by script. That is a lot of link-time behaviour to depend on
// for a convenience, and this is the switch that takes it out of the picture
// when a board will not boot.
#if defined(CSP_NO_IMAGE_REGISTRY)
#define CSP_REGISTER_IMAGE(base_sym) extern int base_sym##_no_registry
#else
#define CSP_REGISTER_IMAGE(base_sym)                                    \
    static const uint8_t* const base_sym##_reg                          \
	__attribute__((section("csp_images"), used)) =                  \
	    (const uint8_t*)&base_sym

extern const uint8_t* const __start_csp_images[];
extern const uint8_t* const __stop_csp_images[];
#endif

// A fixed-type handle on an image whose struct type the runtime cannot name --
// every image has its own type, because the counts differ. The runtime does not
// care what that type is: it takes the base and works in offsets from there.
typedef struct {
    const uint8_t* base;
} csp_image_ref_t;

// One RAM arena holds everything that used to be fixed struct arrays: the parse-
// time code (instr[]/decl[]) plus the rt_start-derived tables (heap, view, buf,
// dset/inq bitsets, reactive graph). Moving them off the struct keeps sizeof
// (csp_rt_t) tiny, so widening the index bits only grows this one block, not the
// struct. Each region is 8-aligned (CSP_A8) so region starts stay aligned when
// bump-allocated in order. These macros are the single source of truth for both
// csp_mem_init and the static-buffer backend default.
#define CSP_A8(n) (((size_t)(n) + 7) & ~(size_t)7)

// How many decls a host build PROVISIONS for, which is not the same thing as how
// many the encoding can address. MAX_DECLS is 32768 since the index lost its
// object field, and reserving that many csp_decl_t is a quarter of a megabyte of
// static buffer for a ceiling no program approaches. The encoding still allows
// 32768 -- a malloc/claim arena (CSP_ARENA_MALLOC, which every build in this tree
// uses) is bounded by the claim, not by this.
#define CSP_PROVISION_DECLS 2048

#define CSP_ARENA_INSTR_BYTES CSP_A8(MAX_INSTRS * sizeof(csp_instr_t))
#define CSP_ARENA_DECL_BYTES  CSP_A8(CSP_PROVISION_DECLS * sizeof(csp_decl_t))
// The worst case: every instruction AND every declaration index in use at once.
#define CSP_ARENA_CODE_BYTES  (CSP_ARENA_INSTR_BYTES + CSP_ARENA_DECL_BYTES)

// Physical size of the shared double-ended instr+decl pool. On host (plenty of
// RAM) it is the full worst case, so capacity is unchanged. On a RAM-constrained
// microcontroller it is capped well below MAX_INSTRS+MAX_DECLS: the index bits can
// still address up to MAX_* each, but their COMBINED bytes must fit this pool
// (mem_fits) -- which is exactly the point, few programs need both maxed. Override
// with -DCSP_CODE_BUDGET=<bytes> to tune per board.
#ifndef CSP_CODE_BUDGET
#if defined(__AVR__)                        // 2K part: the pool is the RAM budget
#define CSP_CODE_BUDGET  CSP_A8(512)
#elif defined(ARDUINO)                      // SAMD21 (mkrzero/cpx) and similar ARM
#define CSP_CODE_BUDGET  CSP_A8(12*1024)
#else                                        // host: full worst case
#define CSP_CODE_BUDGET  CSP_ARENA_CODE_BYTES
#endif
#endif

// Stack CandySpeak keeps clear of the arena, measured on a mega2560.
//
// The arena grows RAM declarations DOWN from its top; the stack grows DOWN from
// RAMEND toward that same top. This is the gap between them. Too small and the
// stack overwrites the newest declarations SILENTLY -- which caused, across a
// three-day hunt: blank /list lines, a false "setup failed: out of memory" (the
// deep csp_rebuild corrupted its own estimate/mid state, not a real shortage),
// and reboot loops (the stack reached a return address).
//
// 512 was a guess and far too low: /memory's `margin` row measured the stack
// reaching 919 bytes PAST a 512 reserve into the arena (deepest point: push_imm,
// in the expression parser). 2048 puts margin at +617 with the cpx.csp ROM and
// leaves the arena big enough (~4080 bytes; cpx needs <3000). WATCH the margin
// row when a program parses deeper (modules, long nested expressions) -- it is
// the live check that 2048 still holds. Override with -DCSP_STACK_RESERVE=<n>.
#ifndef CSP_STACK_RESERVE
#define CSP_STACK_RESERVE 2048
#endif

#ifndef CSP_RAM_RESERVE
#define CSP_RAM_RESERVE 0
#endif

// Gap left between the code growing in from each end and the derived tables in
// the middle, so a handful of adds at the prompt fit without a re-layout.
#ifndef CSP_SCRATCH
#define CSP_SCRATCH 64
#endif

// Everything derived from the program is sized to actual (own allocations), NOT
// reserved at MAX_* worst case: graph (idg/ofs/edg) + inq + queue (csp_csr),
// buffer table + heap + view + dset (csp_rt_start, from csp_estimate). Only the
// shared code pool (instr/decl) stays pre-reserved in this arena.
#define CSP_ARENA_BYTES (CSP_CODE_BUDGET)

// How many typed lines /undo can take back. Eight bytes each, so the default is
// 64 bytes of the runtime struct -- nothing on a board with kilobytes of pool,
// and worth turning down to 1, or off with 0, on one with a few hundred.
#ifndef CSP_UNDO_DEPTH
#define CSP_UNDO_DEPTH 8
#endif

// Where the four bump cursors stood before a line was accepted. Taking the line
// back means putting them here again -- see cmd_undo, which is cmd_clear with a
// nearer floor.
typedef struct {
    index_t nn;                 // instructions
    index_t nd;                 // declarations
    index_t strp;               // string table bump cursor
    index_t nq;                 // pending queue
} csp_undo_t;

typedef struct _csp_rt_t
{

    // RAM code arena (allocated once in csp_rt_init via csp_mem_init). instr[] and
    // decl[] share ONE double-ended pool of CSP_CODE_BUDGET bytes: instructions
    // grow UP from the base (ram_instr[0..]), declarations grow DOWN from the top
    // (ram_decl[0], [-1], [-2] ...). They meet in the middle -- mem_fits() rejects
    // an add once instr_bytes + decl_bytes would exceed mem_limit, so the split is
    // dynamic (a program with few decls may use more of the pool for instrs, and
    // vice versa) instead of reserving MAX for each end. See doc/MEMORY_LAYOUT.md.
    uint8_t*    mem;                     // arena base (malloc'd or static)
    size_t      mem_size;                // physical arena size in bytes
    size_t      mem_limit;               // usable byte budget for instr+decl (<=
					 // CSP_CODE_BUDGET); the binding cap on how many
					 // instructions/declarations fit. Set from
					 // csp_mem_init(size); -m on linux shrinks it.
    csp_instr_t* ram_instr;              // -> pool base, grows up
    csp_decl_t*  ram_decl;               // -> pool top slot, indexed DOWN (ram_decl[-local])
    // Everything derived from the program is bump-allocated from the MIDDLE of
    // the same pool -- between the instructions growing up and the declarations
    // growing down -- and laid out fresh on every csp_rebuild. So there is
    // nothing to free, no heap, no fragmentation: the whole runtime is one block,
    // which is what lets this run on a target with no malloc at all.
    // A scratch gap is left at each end so a few adds at the prompt do not have
    // to shove the middle around; mem_fits() keeps the ends out of it.
    size_t mid;                          // bump cursor (offset into mem)
    size_t mid_base;                     // where the middle starts (after instr+scratch)
    size_t mid_end;                      // where it must stop (before decl+scratch)
    uint8_t mid_full;                    // 1 = a request did not fit
    csp_instr_t  imm_scratch;            // dummy slot for immediate `> expr` eval fold
    // Segment map: str_seg[k] is the DECL INDEX of segment k's header slot, and
    // its payload holds RAM string bytes [k*128 .. k*128+127]. 0 = not taken
    // yet (index 0 is always State, never a segment). Rebuilt by
    // csp_str_recount, which is what /clear, /undo, a ROM load and an EEPROM
    // load already call.
    index_t     str_seg[CSP_STR_MAX_SEGS];
    uint8_t     nseg;                    // segments taken
    char        err_str[CSP_ERR_STR_BYTES]; // error-message %s arguments
#if CSP_DEFINE_BYTES > 0
    // #define names and values -- see the note at CSP_DEFINE_BYTES. Deliberately
    // NOT part of ram_str: it is the buffer a define is meant to relieve.
    char        def_str[CSP_DEFINE_BYTES];
    uint16_t    def_used;                // bump cursor into def_str
#endif

    csp_line_t  line;                    // line data

    // /undo. Where the four bump cursors stood before each of the last few
    // typed lines, so a line can be taken back by TRUNCATING to where it began
    // -- which is what /clear already does, only to the ROM baseline instead.
    //
    // strp is in here and that is the point: the string table is a bump
    // allocator too, so rolling it back returns the names a withdrawn
    // declaration introduced. Without it an undo would leak them until /clear
    // and the feature would need a compaction pass to be worth having.
    //
    // Only lines that APPEND leave a snapshot. #disable, a #param override and
    // a plain assignment all edit in place and move no cursor, so nothing is
    // pushed and /undo says there is nothing to take back rather than silently
    // withdrawing an older line the user had stopped thinking about.
    csp_undo_t undo[CSP_UNDO_DEPTH];
    uint8_t  undo_n;         // valid entries, 0..CSP_UNDO_DEPTH
    uint8_t  undo_head;      // next slot to write (ring)

    // All leaf values live in the buffer heap (see doc/DESCRIPTORS.md). Each of
    // these is its own allocation, sized to csp_estimate in csp_rt_start.
    csp_view_t* view;             // per-leaf view (own alloc, sized to estimate)
    index_t    view_cap;          // leaves view[]/dset hold (csp_estimate.nleaf);
				  // rt_start reruns on any decl add so it stays >= max st_index
    csp_buf_t*  buf;              // buffer table (own alloc, sized to estimate)
    index_t    buf_cap;          // buffers the table can hold (csp_estimate.nbuf)
    index_t    nbuf;              // number of buffers allocated
    uint16_t   hp;                // heap bump cursor, bytes. Held rather than
				  // derived from buf[nbuf-1]: most leaves take
				  // heap without taking a buffer now, so the
				  // buffer table is no longer a record of what
				  // the heap has handed out.
    // The transaction model is permanent: rules read the committed DIN heap and
    // write the DOUT shadow; csp_commit copies dirty leaves DOUT->DIN. So a cycle
    // never sees its own writes -> sequential and reactive yield the same state.
    // ONE allocation holds both halves: heap[DOUT] points at its second half, so
    // only heap[DIN] is owned (freed). heap_cap is the usable bytes per half.
    uint8_t*   heap[2];           // heap[DIN] = block base, heap[DOUT] = base + half
    uint16_t   heap_cap;          // heap bytes per half (csp_estimate.heap)
    // allow device output latch=0 or disallow latch=1
    uint8_t latch;
    // Firmware upgrade mode: while this is set, csp_process_line hands every
    // line to the hex receiver instead of the parser. Execution is stopped for
    // the duration -- a rule firing mid-write would change timing the flash
    // controller is not expecting, and there is nothing for the program to do
    // while its own storage is being rewritten.
    uint8_t up_active;
    // check if any node has been set: anyx|anyd == CSP_TRUE
    int8_t  anyd;  // CSP_TRUE|CSP_FALSE
    set_group_t* dset;            // mark decl updated during cycle (own alloc, view_cap bits)
    
    // Object number -> where that object's members start in the decl index
    // space. Indexed by a REAL object number (1..nq), never by the selector in
    // an encoded index -- those two used to be the same number space and are not
    // any more. Slot 0 is the global base and is always 0.
    //
    // Sized to the objects the program HAS (csp_estimate.nobj) and laid out with
    // the other derived tables in csp_rt_start, not reserved at a MAX_OBJECTS
    // worst case. object[] is the reverse map, decl index of object m; both are
    // rebuild-time caches -- the durable record of which object a declaration is
    // lives in the declaration itself (mq.m). See csp_object_decl.
    index_t* offs;                 // offset to object locals, obj_cap slots
    index_t* object;               // decl index per object, obj_cap slots
    index_t  obj_cap;              // slots allocated (== nobj + 1, for the 1-base)
    // The base a CURRENT-relative index resolves against: offs[cur], kept in step
    // by OP_NEW/OP_LEAVE, by the reactive dispatcher, and one-shot by OP_SETO.
    // Its own field rather than offs[CURRENT], which is what it was while CURRENT
    // was a spare slot in an object-numbered array.
    index_t cbase;
    // stack used during eval
    int esp;                       // eval stack pointer
    struct PACKED { index_t ix; uint8_t cur; }
	stack[MAX_STACK_DEPTH];
    unsigned reactive:1;         // 1 if push backedges to queue
    unsigned sweep:1;            // 1 during a full sequential sweep (csp_eval /
				 // reactive seed): OP_NEW/LEAVE enter/leave objects.
				 // 0 during csp_react single-rule dispatch.
    unsigned seed_all:1;         // 1 during the first cycle: OP_CHG reads true for
				 // every input so each <- binding fires once to
				 // establish its initial value (least surprise).
    unsigned paused:1;           // 1 = /pause: driver runs no cycle (inspect/edit)
    unsigned live:1;             // 1 = /live: rules frozen but I/O runs, so you can
				 // poke outputs (> Led=1 drives the pin) and watch
				 // inputs while the program logic stands still
    unsigned edited:1;           // 1 = program changed while paused; /resume rebuilds
    unsigned started:1;          // 1 once csp_rt_start has allocated+set up leaves;
				 // 0 with -b before /resume (value ops not ready)

    // Firmware ROM executes in place from flash (see doc/ROM_RAM.md); RAM holds
    // patches. The logical index space is [0,rom_n*) = ROM (read via the pointers
    // below), [rom_n*, .) = RAM (ram_*[logical - rom_n*]). rom_n*==0 => no ROM
    // active, everything is RAM. Also the ROM/RAM boundary /list tags against.
    // ONE descriptor, not six fields. These are the section pointers of a single
    // image and img_p_t already names exactly that, so the loader commits them
    // with one struct assignment instead of six stores -- and it stays one
    // assignment if a section is ever added. csp_enq_elist used to read the
    // rom_idg/rom_ofs/rom_edg globals directly, which tied the runtime to ONE
    // image; only the loader names an image now, everything downstream goes
    // through here. All NULL (rt_init's memset) when no ROM is active, and .idg
    // /.ofs/.edg are NULL when rom_nedg == 0.
    img_p_t rom_p;
    index_t rom_nd;              // # ROM decls   (RAM decl base)
    index_t rom_nn;              // # ROM instrs  (RAM instr base)
    index_t rom_strp;            // # ROM string bytes (RAM string base)
    // Walk cache for csp_str_ofs. A handle is the Nth string, so resolving one
    // means counting length bytes from the start -- and the readers that do it
    // most (a listing, a name lookup over every decl) go through the handles in
    // ascending order, so remembering the last one turns the whole sweep from
    // O(n^2) into O(n). Purely derived: csp_str_recount resets it, and nothing
    // depends on it being warm.
    sindex_t str_cid;            // last handle resolved (0 = cold)
    sindex_t str_cofs;           // and its byte offset
    index_t rom_ns;              // # baseline states (INIT/NORMAL + ROM states);
				 // EEPROM persists only the runtime additions above
    index_t rom_nedg;            // # ROM reactive-graph edges (0 = no baked graph)
    // Where the RUNTIME ends and a program begins. csp_rt_init creates the
    // implicit State variable and its name before any program exists; with no
    // firmware image linked those sit at index 0, exactly where rom_* says a
    // program starts. Anything that resets "back to the ROM baseline" must floor
    // at these instead, or it deletes State -- and then the State gate in
    // csp_eval reads a slot that is not State's and every ungated rule quiesces.
    // Persistence must use the SAME floor on the save and the load side, or the
    // counts disagree and a restore lands one decl out.
    index_t sys_nd;
    index_t sys_nn;
    index_t sys_strp;
    // The built-in Sys namespace (csp_sys_module): its module declaration and
    // its one instance. Held so the listing can leave them out -- /list is meant
    // to paste back as source, and a built-in cannot be re-declared -- and so
    // platform code has them without a name lookup. BAD_INDEX when the build
    // has no Sys (CSP_NO_SYS_MODULE) or a loaded image predates it.
    index_t sys_mod;
    index_t sys_obj;
    // Registry index of the image csp_load_rom actually booted, or -1 for the
    // linked rom_image (which is not in the registry). Published as sys.Image
    // once csp_rt_start has slots to publish into -- the NUMBER /images prints,
    // so "which one am I running" has one answer in both places.
    int8_t  image_no;
    // crc_hdr of the image csp_load_rom actually BOOTED -- the eeprom patch's
    // fingerprint. Not read from rom_image: with more than one image linked the
    // registry picks the highest generation, which is a DIFFERENT program, and
    // fingerprinting the one that was not booted accepts a patch built against
    // something else. Its declaration indices and name handles then address the
    // wrong table -- observed as a saved `Z` coming back as a second `R`.
    uint16_t rom_fp;
    // Which image to boot NEXT time, as sys.Boot holds it -- read out of the
    // settings store before csp_load_rom runs, because the choice has to be
    // made before an image is loaded. CSP_BOOT_AUTO means "no preference".
    uint8_t boot_want;
    // How much of the RAM patch eeprom currently holds a copy of, counted from
    // CSP_BASE_ND/CSP_BASE_NN. Set by a successful save (everything in RAM is now
    // in eeprom) and by a successful load (what came back), zeroed by /clear and
    // by an eeprom erase. /list turns it into the E tag: a RAM line inside the
    // watermark is recoverable, one past it is lost for good when RAM is dropped.
    // It counts, it does not compare -- a #disable after a save leaves the tag in
    // place, because the DECLARATION is still the one eeprom holds.
    index_t ee_nd;
    index_t ee_nn;

    // The SETTINGS store: values for things the firmware already declares -- a
    // #param trimmed against this motor, a pin moved because this board is wired
    // differently. Held in RAM in the same wire format eeprom keeps, so /save is
    // a block write and a load is a block read.
    //
    // A separate store from the patch above because it has a separate LIFETIME.
    // The patch is program text belonging to one firmware and is dropped when
    // rom_fp moves; a setting belongs to the UNIT and must survive a reflash.
    // That is also why an entry names its target as CHARACTERS: a reflash
    // renumbers every declaration, so an index would point somewhere else.
    // See doc/EEPROM.md.
    uint8_t  settings[CSP_SETTINGS_BYTES];
    uint16_t set_used;           // bytes of settings[] in use
    uint8_t  set_dirty;          // changed since the last save (/settings tag)

    csp_pstate_t ps;             // parse state (counts, error, line)

    // Everything the TOKENIZER and PARSER need and nothing else does. Grouped so
    // the compiler half has a named surface instead of a dozen loose fields
    // sharing a struct with the runtime -- and so it is obvious when a runtime
    // path reaches into parse state, which is a bug waiting to happen (see gsx:
    // `sx` moves while a module is being typed, so anything running on a CYCLE
    // must not read it).
    //
    // It stays INSIDE csp_rt_t rather than being passed on its own: the parser
    // emits into the same arena the runtime executes from -- decls, strings and
    // instructions are all reached through st -- so a pointer to just this would
    // not be enough to parse with. What it buys is a boundary you can see.
    // The COMPILER's state, or NULL on a node that cannot compile.
    //
    // A pointer and not a member: none of this is the runtime's. It is the
    // module being defined, the register allocator, the parse-rollback mark,
    // the scratch list a `<-` binding collects its variables in -- all of it
    // meaningless once nothing can be typed. An exec-only node carried 108
    // bytes of it on an AVR and, worse, a runtime struct that named a compiler
    // type as if it owned one.
    //
    // NULL is the tier, checkable at runtime: the three places csp_rt.c still
    // asks about it now read "if there is a compiler, and it is mid-module",
    // which is what they always meant.
    csp_cstate_t* cs;
    csp_estate_t es;

    int list_state;              // during listing: state of the #in block being
                                 // rendered (-1 = none), suppresses State==S in cond
    int list_implicit;           // during listing: this rule is a bare NORMAL+
                                 // rule -- suppress its State==INIT||State==NORMAL
    uint8_t list_states[MAX_IN_STATES]; // during /list: states of the #in block
    uint8_t list_nstate;         // being rendered -- suppress State==<any of them>
    // The GLOBAL State, fixed once the runtime has one. cs.sx is a parse-time
    // cursor -- between #module and #end it points at the module's own State,
    // CURRENT-relative -- so anything that runs on a CYCLE, while a module may
    // be half typed at the prompt, has to use this instead. Deliberately not in
    // csp_pmark_t: a parse rollback must not move it.
    index_t gsx;
    // (no state table: states are DECL_STATES declarations, see add_state)
    index_t mdef;                // module being defined
    csp_pmark_t mod_mark;        // parse mark taken at #module: a failure before
				 // #end rewinds the whole module, so the lines
				 // after it are not silently absorbed into a
				 // module that can never be closed
    int     ent;                 // entry op of module in st->instr
    uint8_t cur;                 // current object number (0 = global)

    // calculated by csp_rt_start
    index_t nt;                  // number of timers
    index_t nio;                 // number of device entries
    index_t nm;                  // number of modules
    // io/timer are sized to the actual program (csp_estimate) and allocated in
    // csp_rt_start -- not MAX_* reserved -- so a program may have as many as it
    // declares (bounded only by RAM). *_cap is the allocated length.
    //
    // ONE list, not an input list and an output list. Direction is a runtime
    // property (a rule may write .dir), so a pin cannot be filed under the
    // direction it was declared with -- it would be missing from the other list
    // the moment it turned round. Both phases walk this list and gate on the
    // slot's CURRENT dir. It is also smaller than the two it replaces: an inout
    // entry used to be stored twice.
    // These lists are walked OUTSIDE any object -- csp_input, csp_output, the CAN
    // mark pass, the board's pin sync -- so each entry has to say which instance
    // it belongs to. The index itself cannot: it carries a one-bit selector, not
    // an object number. Hence the parallel *_obj byte per entry, and csp_io_at /
    // csp_timer_at, which bind the context before handing the index back.
    index_t* io;                 // digital/analog/field entries, io_cap slots
    uint8_t* io_obj;             // owning object per io[] entry (0 = global)
    index_t  io_cap;
    index_t* timer;              // list of timers, timer_cap slots
    uint8_t* timer_obj;          // owning object per timer[] entry (0 = global)
    index_t  timer_cap;
    index_t* module;             // list of modules, mod_cap slots
    index_t  mod_cap;
    // Rule bodies counted at EMIT time (alloc_instr_ptr), no scan. csp_rebuild
    // snapshots the counter into graph_rules, so n_rule_emit != graph_rules
    // means "code was added since the last rebuild" -- the cheap staleness test
    // csp_cycle uses. Unconditional: emission happens with or without reactive.
    index_t  n_rule_emit;
    index_t  graph_rules;        // n_rule_emit the last rebuild covered
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    // Rule bodies are numbered densely 0..n_rule-1 in instruction order (csp_csr),
    // and it is that ORDINAL -- not the raw instruction index -- that edg[] stores
    // and the queue carries. Raw ip spans MAX_INSTRS, of which only rule bodies can
    // ever be queued, so an (obj,ip) key space is almost entirely holes; (obj,ord)
    // is dense. rule_ip maps back on dequeue. The numbering composes with a baked
    // ROM graph for free: dumping ran the same walk with rom_nn == 0, so rescanning
    // [0,rom_nn) reproduces exactly the ordinals rom_edg was baked with.
    // Pending work is a BIT SET over (ord,obj) keys, not a queue. The set already
    // is the pending state -- a queue would only add an order, and the order it
    // adds is the wrong one: fed in change order it runs rules in whatever
    // sequence their triggers fired, so an overriding rule loses whenever its
    // trigger changed first (see tests/unit/rule_order.csp). Walking the set
    // upwards evaluates in ORDINAL order = instruction order = the order the
    // rules are written, which is what "last rule wins" means.
    //
    // Two sets: work for THIS cycle, and what this cycle queues for the NEXT.
    // csp_react swaps them, so a rule enqueued while its own cycle runs waits its
    // turn -- the same generation split the old cycle_end snapshot gave.
    // Dedup is inherent (a bit is set or it is not) and a bit set CANNOT overflow,
    // so the silent drop a full queue used to cause is gone by construction.
    // back references
    // Reactive graph, sized to the actual node/edge counts in csp_csr (own
    // allocation, not the arena). graph_n = nodes it was built for; a decl added
    // after the last csr is >= graph_n and simply has no edges (enq skips it).
#endif
    // #disable / #enable. Two representations, on purpose:
    //
    // dis_rule is keyed by the user-facing rule NUMBER and is the source of
    // truth. It has to survive csp_rebuild (which resets the middle allocator)
    // and it is what a range like "3 7-9" writes, so it lives on the struct.
    //
    // dis_ip is keyed by the rule's FIRST INSTRUCTION -- where its condition
    // starts, which is what csp_eval_rule is entered with and what rule_ip
    // holds -- so the sequential and reactive paths agree without either
    // knowing about the other. Derived in csp_rebuild, and only allocated when
    // something is actually disabled: NULL is the common case and costs one
    // test in the hot loop.
    bitset_decl(dis_rule, MAX_DIS_RULES);
    set_group_t* dis_ip;       // ps.nn bits, or NULL when nothing is disabled
    index_t  n_rule_no;        // rules seen by the last numbering walk
    uint32_t cycle;
    // Refresh rate, measured not estimated: cycles completed per second over the
    // last full second. In sequential mode this is the number that matters --
    // every rule runs every cycle, so it says directly how much headroom a
    // program has left. Costs one compare per cycle and ten bytes.
    uint32_t hz_t0;            // ms at the start of the window
    uint32_t hz_c0;            // cycle count at the start of the window
    uint16_t hz;               // cycles/s over the last completed window
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    uint32_t num_eval_rule;    
    uint32_t num_eval0;
#endif
    // user-defined functions (checked before builtin)
    const csp_func_t* ufuncs;
    uint8_t num_ufuncs;
    uint8_t ufuncs_rom;          // 1 if the ufuncs table is in ROM (PROGMEM)
    // user hook to lookup platform constants
    csp_const_fn uconst;
} csp_rt_t;

// The floor every "reset back to the baseline" must respect: the ROM image if
// one is linked, otherwise what csp_rt_init created. Save and load MUST use the
// same one, or the patch counts disagree and a restore lands a decl out.
#define CSP_BASE_ND(st)   (((st)->rom_nd   > (st)->sys_nd)   ? (st)->rom_nd   : (st)->sys_nd)
#define CSP_BASE_NN(st)   (((st)->rom_nn   > (st)->sys_nn)   ? (st)->rom_nn   : (st)->sys_nn)
#define CSP_BASE_STRP(st) (((st)->rom_strp > (st)->sys_strp) ? (st)->rom_strp : (st)->sys_strp)
// ram_str[] and ram_instr[] are indexed RELATIVE to the ROM sizes -- ram_str[0]
// is logical position rom_strp. So a logical floor has to be turned back into a
// buffer offset before it can slice those arrays. Zero whenever the floor is
// the ROM baseline, non-zero only when the runtime's own decls sit below it.
#define CSP_RAM_STR_OFF(st) (CSP_BASE_STRP(st) - (st)->rom_strp)
#define CSP_RAM_NN_OFF(st)  (CSP_BASE_NN(st)   - (st)->rom_nn)

// Read a whole RO record by value. On AVR the ROM segment is in PROGMEM, so we
// copy it into a RAM temporary -- then bit-field access works as usual. (The
// "clever" bit: never deref a PROGMEM struct directly.)
#if defined(__AVR__)

static inline csp_decl_t  ro_decl(const csp_decl_t* p)
{ csp_decl_t d;  memcpy_P(&d, p, sizeof(d)); return d; }

static NOINLINE void ro_copy_decl(const csp_decl_t* p, csp_decl_t* dst)
{
    memcpy_P(dst, p, sizeof(csp_decl_t));
//    ((uint32_t*)dst)[0] = ro_dword(((uint32_t*)p)[0]);
//    ((uint32_t*)dst)[1] = ro_dword(((uint32_t*)p)[1]);
}

static inline csp_instr_t ro_instr(const csp_instr_t* p)
{ csp_instr_t v; memcpy_P(&v, p, sizeof(v)); return v; }
// NOT static inline, unlike its neighbours: the image header is 60 bytes, so
// gcc never actually inlines the copy -- it emits an out-of-line body in EVERY
// translation unit that mentions it, and the linker cannot merge them because
// each is TU-private. That showed up in the map as ro_header.lto_priv.109 AND
// .110, 82 bytes each. One definition, in csp_rt.c (same reasoning as
// csp_get_decl below). The small records above really do inline and stay here.
extern csp_image_header_t ro_header(const csp_image_header_t* p);
// The descriptor lives in PROGMEM like everything else it names -- eight
// pointers is 16 bytes of RAM per image on AVR, and images are meant to come in
// threes (a program, a FAILSAFE, a spare). Copied out once per load.
extern csp_image_ref_t ro_ref(const csp_image_ref_t* p);   // ditto: 16 bytes
static inline csp_sect_t ro_sect(const csp_sect_t* p)
{ csp_sect_t v; memcpy_P(&v, p, sizeof(v)); return v; }

#else
#define ro_decl(p)  (*(p))
#define ro_copy_decl(p,d) (*(d)) = (*(p))
#define ro_instr(p) (*(p))
#define ro_header(p) (*(p))
#define ro_ref(p)  (*(p))
#define ro_sect(p) (*(p))
#endif

// Segment-aware read by logical index: a firmware ROM index reads flash (via the
// PROGMEM-safe ro_decl/ro_instr), a RAM index reads ram_*[logical - base]. ROM
// is never written -- writes go to the RAM slots (ram_decl_at/ram_instr_at),
// whose logical index is always >= the base. With no ROM active (rom_n*==0) all
// of these reduce to plain ram_* access. NOINLINE (defined in csp_rt.c) so the
// flash-copy is not expanded at every decl()/instr() site (code size on AVR).
extern csp_decl_t  csp_get_decl(csp_rt_t* st, index_t i);
extern csp_instr_t csp_get_instr(csp_rt_t* st, index_t n);

// one string byte at a raw BYTE OFFSET into the table (length byte or char).
// For a string, use csp_str_len / csp_str_at, which take a HANDLE; this one is
// for walking the table itself.
// RAM write slots -- logical index must be at/above the ROM base. decl grows
// DOWN from the pool top, so the local index is negated (ram_decl points at
// local 0, the topmost slot; local 1 is ram_decl[-1], and so on). Declared here
// because the string accessors below resolve segments through it.
#define ram_decl_at(st, logical)  (&(st)->ram_decl[(st)->rom_nd - (logical)])

#define ram_instr_at(st, logical) (&(st)->ram_instr[(logical) - (st)->rom_nn])

// Address of payload slot `k` (1..num) of the segment whose header is at `h`.
//
// Instructions grow UP in RAM and a ROM image is an ascending array, so the two
// run the same way -- unlike declarations, which grow down and would have made
// the same logical slot sit at opposite ends. Nothing has to be written in
// reverse, and one offset works for either.
// Next instruction index after `i`, stepping over a string segment's payload.
//
// EXECUTION does not need this -- OP_SEGMENT is a jump, so eval steps past the
// run on its own. It is the loops that walk the stream LINEARLY that do: the
// reactive graph builder, the rule scanners, the ROM emitter. Payload is
// identifier text, so its opcode nibble is a character, and one in four reads
// as something with operands to chase.
static inline index_t instr_next(csp_rt_t* st, index_t i)
{
    csp_instr_t ci = csp_get_instr(st, i);
    return (ci.op == OP_SEGMENT) ? (index_t)(i + ci.sg.num + 1) : (index_t)(i + 1);
}

static inline char* csp_seg_slot(csp_rt_t* st, index_t h, unsigned k)
{
    index_t i = h + k;
    if (i < st->rom_nn)
	return (char*)&st->rom_p.instr[i];
    return (char*)ram_instr_at(st, i);
}

// Byte pointer for a RAM-local offset, resolved through the segment map.
//
// ADDRESSES RUN BACKWARDS in the decl pool: ram_decl_at(i) is
// &ram_decl[rom_nd - i], so a HIGHER index is a LOWER address. The payload is
// therefore contiguous and ascending from the LAST slot of the run, which is
// why the base is taken at header + CSP_STR_SEG_SLOTS.
static inline char* csp_ram_str_at(csp_rt_t* st, sindex_t r)
{
    index_t h = st->str_seg[r >> CSP_STR_SEG_BITS];
    return csp_seg_slot(st, h, 1) + (r & CSP_STR_SEG_MASK);
}

static inline uint8_t csp_str_byte(csp_rt_t* st, sindex_t pos)
{
    // No ROM/RAM split any more. Identifier text lives in DECL_SEGMENT runs,
    // and a run is reached the same way whether its slots came from flash or
    // from the pool -- so rom_strp is 0 and this is one lookup.
    return (uint8_t)*csp_ram_str_at(st, pos);
}

// Step from the end of one segment's text to the start of the next.
//
// A record may not straddle a boundary, so a segment's tail goes unused when the
// next name will not fit -- and a ROM segment's tail is unused from the moment
// it is loaded, because RAM names cannot be written into flash. Both are the
// same thing: the header's `used` says how far the text goes, and past that the
// walk jumps to the next segment.
//
// This is what a fill BYTE cannot do. Marking the tail works while the tail is
// writable; a loaded image's is not.
static inline sindex_t csp_str_skip_fill(csp_rt_t* st, sindex_t ofs)
{
    while (ofs < (sindex_t)st->ps.strp) {
	index_t h = st->str_seg[ofs >> CSP_STR_SEG_BITS];
	unsigned used;
	if (h == BAD_INDEX)
	    break;
	used = csp_get_instr(st, h).sg.used;
	if ((ofs & CSP_STR_SEG_MASK) < used)
	    break;
	ofs = (ofs + CSP_STR_SEG_BYTES) & ~(sindex_t)CSP_STR_SEG_MASK;
    }
    return ofs;
}


// A string HANDLE -- what a decl's `name` and a V_STRING value hold -- resolved
// to the byte offset of its LENGTH BYTE. Handle 0 means "no string" and has no
// offset, so every reader tests for it first.
//
// A handle is an ID: handle N names the Nth string in the table, counting from
// 1. That is what makes the 9-bit name field hold 512 NAMES rather than 512
// bytes -- at a mean name of four characters, six times the room.
//
// The cost is that resolving one means counting, and the cache above is what
// keeps that from mattering: every sweep that resolves many handles does it in
// ascending order. Nothing in EXECUTION resolves a handle at all -- names are
// for the parser and the listing.
static inline sindex_t csp_str_ofs(csp_rt_t* st, sindex_t h)
{
    sindex_t i, ofs;

    if (st->str_cid && (h >= st->str_cid)) {   // resume from the cache
	i = st->str_cid; ofs = st->str_cofs;
    }
    else {
	i = 1; ofs = 0;
    }
    while (i < h) {                            // one step per string
	ofs = csp_str_skip_fill(st, ofs);      // past a segment's unused tail
	ofs += csp_str_byte(st, ofs) + 1;      // length byte + that many chars
	i++;
    }
    ofs = csp_str_skip_fill(st, ofs);
    st->str_cid = h; st->str_cofs = ofs;
    return ofs;
}

// Length of the string a handle names. 0 for the null handle.
static inline uint8_t csp_str_len(csp_rt_t* st, sindex_t h)
{
    return h ? csp_str_byte(st, csp_str_ofs(st, h)) : 0;
}

// Character i of the string a handle names, read segment-aware (a ROM name is
// in flash, a RAM one is not, and the caller cannot tell which).
static inline uint8_t csp_str_char(csp_rt_t* st, sindex_t h, int i)
{
    return csp_str_byte(st, csp_str_ofs(st, h) + 1 + i);
}

// char* to the CHARACTERS a handle names (host: RODATA is normal memory; an AVR
// PROGMEM string needs a copy-out API -- deferred). Base 0 -> ram_str.
static inline char* csp_str_at(csp_rt_t* st, sindex_t h)
{
    sindex_t pos = csp_str_ofs(st, h) + 1;
    // Safe as a raw pointer because no record straddles a segment boundary --
    // new_string skips to the next one rather than split a string.
    return csp_ram_str_at(st, pos);
}

// RAM write slots -- logical index must be at/above the ROM base (RAM region).
// decl grows DOWN from the pool top, so the local index is negated (ram_decl
// points at local 0, the topmost slot; local 1 is ram_decl[-1], and so on).
#define ram_str_at(st, logical)   (*csp_ram_str_at((st), (logical)))

#define decl(st,i,fld)  (csp_get_decl((st),(i)).fld)
#define instr(st,n,fld) (csp_get_instr((st),(n)).fld)

// Parser stack entry - tracks both register and declaration index
typedef struct PACKED {
    value_t val;     // if constant then the actual value is loaded here
    // xindex_t, not index_t: while an expression is being compiled a reference
    // may still name an object (`safe.State`), which an encoded index cannot say.
    // asm_mem_part/asm_memi narrow it and emit the OP_SETO. Costs two bytes per
    // stack entry, on a stack MAX_PARSE_STACK_DEPTH deep.
    xindex_t ix;     // declaration index (valid for variables)
    reg_t reg;       // register number (valid if loaded)
    union {
	// uint8_t vtf;     // vt + flags(soon)
	struct {
	    unsigned vt:TYPE_BITS;
	    unsigned L:1;    // == 1 when reg is valid (loaded)
	    unsigned I:1;    // == 1 when val is immediate value
	    unsigned X:1;    // == 1 when ix is decl index
	    unsigned part:PART_BITS; // csp_part_t, PART_VAL for the plain value
	    // A[expr]: this entry is an ARRAY ELEMENT and areg holds its subscript.
	    //
	    // It rides on the ENTRY, not on a one-shot in cs, because the access is
	    // DEFERRED -- an entry is pushed at `]` and only becomes an LD/ST when
	    // csp_load or process_assign reaches it. `A[i] = B[j] + 1` has two
	    // subscripts live at once, which a single pending slot cannot hold.
	    //
	    // Free: vt(4)+L+I+X+part(4) is 11 bits of the union's 16, and these two
	    // are the remaining 5. The stack is MAX_PARSE_STACK_DEPTH deep, so a
	    // byte here would have been ten.
	    unsigned A:1;
	    unsigned areg:REG_BITS;
	};
    };
} rentry_t;


// Built-in function table (defined in csp_rt.c)
extern const csp_func_t csp_builtin_funcs[];
extern const uint8_t csp_num_builtin_funcs;

extern csp_func_fn func_fn(const csp_func_t* fn, int i, int rom);
extern uint8_t func_arity(const csp_func_t* fn, int i, int rom);

// Resolve an encoded index to a flat decl index. OBJ(n) is a one-bit selector:
// 0 = global (base 0), 1 = CURRENT (base cbase). It is not an object number and
// does not index offs[].
static inline int st_index(csp_rt_t* st, index_t n)
{
    return (OBJ(n) ? st->cbase : 0) + INDEX(n);
}

// The same thing for a NAMED object, where the caller has the object number in
// hand. Used off the execution path -- setup, per-object init, listing -- which
// is where an object other than "global" or "the one running" gets addressed
// without an OP_SETO to say so.
static inline int st_index_obj(csp_rt_t* st, unsigned m, index_t ix)
{
    return st->offs[m] + ix;
}

// Point the object context at `m`, the way OP_NEW does. For the passes that walk
// per-object data from OUTSIDE a rule: setup, the I/O and timer lists, the
// per-object State step, listing.
static inline void csp_ctx_set(csp_rt_t* st, unsigned m)
{
    st->cur   = (uint8_t)m;
    st->cbase = st->offs[m];
}

// Back to global. Every loop that used csp_ctx_set ends with this, so nothing
// downstream inherits a base belonging to whichever object happened to be last.
static inline void csp_ctx_reset(csp_rt_t* st)
{
    st->cur   = 0;
    st->cbase = 0;
}

// Entry i of the I/O / timer list, with its object bound. See the io/io_obj
// declaration for why the entry alone is not enough.
static inline index_t csp_io_at(csp_rt_t* st, int i)
{
    csp_ctx_set(st, st->io_obj[i]);
    return st->io[i];
}

static inline index_t csp_timer_at(csp_rt_t* st, int i)
{
    csp_ctx_set(st, st->timer_obj[i]);
    return st->timer[i];
}

// Resolve a leaf index to its view descriptor (see doc/DESCRIPTORS.md).
// Step 2: table-driven. Every entry is still VIEW_SLOT with slot == st_index,
// so behaviour is identical to before. Step 3 starts emitting VIEW_HEAP.
static inline csp_view_t* csp_view(csp_rt_t* st, index_t n)
{
    return &st->view[st_index(st, n)];
}

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
// Mark a rule for recalculation in an object's context. `ord` is a rule ORDINAL
// -- edg[] (and the baked rom_edg) already store ordinals, so there is no ip->ord
// lookup here; csp_react maps back via rule_ip when it runs the rule.
// Setting a bit twice is a no-op, so this needs no dedup test, and it cannot
// overflow: a key beyond the built set just means the graph has not been rebuilt
// for it yet (an object added since the last csr) and is dropped, as it was
// before -- such a rule has no edges to reach it anyway.
static inline void csp_enq(csp_rt_t* st, uint8_t obj, uint16_t ord)
{
    index_t e = MAKE_QENTRY(st, obj, ord);
    if (e < st->es.pending_cap)
	bitset_set(st->es.pending[st->es.gen], e);
}

// Is any rule waiting for the next cycle? The driver's idle test -- it used to
// ask whether the queue was non-empty.
static inline int csp_pending(csp_rt_t* st)
{
    uint32_t w, n = BITSET_GROUPS(st->es.pending_cap);
    for (w = 0; w < n; w++)
	if (st->es.pending[st->es.gen][w])
	    return 1;
    return 0;
}
#endif

extern value_t* csp_dio_slot(csp_rt_t* st, index_t ix, dio_t dir);
extern int csp_dio_slots(csp_rt_t* st,index_t ix,value_t** iptr,value_t** optr);
extern void csp_dio_set(csp_rt_t* st, index_t ix, value_t v, dio_t dir);
extern void csp_dio_get(csp_rt_t* st, index_t ix, value_t* vp, dio_t dir);

extern value_t  csp_value(csp_rt_t* st, index_t x);

static inline ivalue_t csp_ivalue(csp_rt_t* st, index_t ix)
{
    value_t v = csp_value(st, ix);
    return v.i;
}

static inline uvalue_t csp_uvalue(csp_rt_t* st, index_t ix)
{
    value_t v = csp_value(st, ix);
    return v.u;    
}

static inline fvalue_t csp_fvalue(csp_rt_t* st, index_t ix)
{
    value_t v = csp_value(st, ix);
    return v.f;        
}

static inline char* decl_name(csp_rt_t* st, index_t ix)
{
    // name is a LOGICAL string position: ROM range -> flash table, else RAM.
    // (On the host RODATA is ordinary memory; an AVR PROGMEM name needs a
    // copy-out API -- deferred.) At base 0 this is plain ram_str access.
    return csp_str_at(st, csp_get_decl(st, INDEX(ix)).name);
}

// logical string position of a decl's name (for the segment-aware str helpers)
static inline sindex_t decl_name_pos(csp_rt_t* st, index_t ix)
{
    return csp_get_decl(st, INDEX(ix)).name;
}

// Length of a decl's name, read segment-aware. decl_name() hands back a raw
// pointer that lands in FLASH for a ROM-range name and in RAM otherwise, and
// the caller cannot tell which -- so walking it with s[n] reads the wrong
// address space on AVR. These two go through csp_str_byte instead.
static inline int decl_name_len(csp_rt_t* st, index_t ix)
{
    return csp_str_len(st, decl_name_pos(st, ix));
}

static inline int decl_name_empty(csp_rt_t* st, index_t ix)
{
    sindex_t h = decl_name_pos(st, ix);
    return (h == 0) || (csp_str_len(st, h) == 0);
}

// Print a decl's name: `printf("%.*s", DNAME(st, ix))`. Two arguments, in the
// order a precision takes them.
//
// A string is NOT nul-terminated. The length byte in front of it is the only
// terminator there is -- which is what lets a name cost one byte of overhead
// instead of two, and on a 3.8-character mean name that was 17% of the table.
#define DNAME(st, ix)  decl_name_len((st), (ix)), decl_name((st), (ix))

// `cs` is the compiler's state, or NULL for a node that only runs images.
extern int     csp_rt_init(csp_rt_t*,  int reactive, csp_cstate_t* cs);
extern int     csp_mem_init(csp_rt_t*, size_t size);
// Memory an already-parsed program needs, computed WITHOUT running csp_rt_start
// (mirrors its global+object walk, counting only). Lets /memory and -b show the
// sizing before allocation, and lets rt_start size its tables to the actual need.
typedef struct {
    index_t  nleaf;   // view[]/dset span = max leaf (st_index) + 1
    index_t  nbuf;    // buffers allocated
    uint16_t heap;    // heap bytes (per DIN/DOUT half). NOT uint32: csp_buf_t.hp
		      // is uint16_t, so a heap past 64K is unrepresentable
		      // anyway -- and on an 8-bit target every arithmetic op on
		      // this value cost twice what it needed to. An oversized
		      // program now fails in csp_buf_alloc (hp + nbytes >
		      // heap_cap -> ERR_TOO_MANY_DECLARATIONS), which is the
		      // same door it would have hit through hp.
    index_t  nio;     // device entries (digital/analog/field)
    index_t  nt;      // timers (global + per-object)
    index_t  nobj;    // objects (DECL_OBJECT), sizes offs[]/object[]
    index_t  nm;      // module definitions (DECL_MODULE), sizes module[]
} csp_estimate_t;
extern void    csp_estimate(csp_rt_t* st, csp_estimate_t* e);
// Backend hook: return >= `need` bytes of RAM for the code arena (called once at
// startup, never freed), or NULL on failure. Default is a static buffer (works
// on any target, no heap); a backend selects malloc with CSP_ARENA_MALLOC or
// provides its own definition with CSP_ARENA_CUSTOM (e.g. claim free RAM).
extern uint8_t* csp_arena_mem(size_t want, size_t* got);
// Load the firmware's default image (rom_image). Thin wrapper over
// csp_load_image, kept because every backend calls it by this name.
extern void    csp_load_rom(csp_rt_t*);
// Load a NAMED image. Verifies it (version, per-section CRC, header-free
// recovery via the end markers) and rebases the parse state onto it.
// 0 when the image is installed, -1 when it is refused. A refusal leaves st
// untouched, so the caller may try another image.
extern int     csp_load_image(csp_rt_t*, const uint8_t* base);
extern const csp_image_ref_t rom_image;
// How many images this firmware linked in, and the base of the i:th one.
extern int            csp_image_count(void);
extern const uint8_t* csp_image_at(int i);
extern const uint8_t* csp_find_image_no(unsigned role, int* nop);
// The same, ignoring registry indices whose bit is set in `skip` (0..31).
extern const uint8_t* csp_find_image_skip(unsigned role, int* nop,
					  uint32_t skip);
// The best linked image for a role: highest generation whose header verifies.
// NULL when the firmware carries none for that role.
extern const uint8_t* csp_find_image(unsigned role);
extern uint16_t csp_crc16(uint16_t crc, const void* data, size_t n, int is_rom);
extern int     csp_has_firmware(void);
// Declaration index of object number m, valid even before a rebuild has built
// the object[] cache (see the definition).
extern index_t csp_object_decl(csp_rt_t*, unsigned m);
// Name position of state `snum`, 0 if there is none; and how many states are
// declared. Both derive from the DECL_STATES blocks -- there is no state table.
extern sindex_t state_name_pos(csp_rt_t*, int snum);
extern int      csp_num_states(csp_rt_t*);
// State number of the state named at string position `pos`, -1 if none.
extern int      lookup_state_pos(csp_rt_t*, sindex_t pos);
extern int     csp_rt_start(csp_rt_t*);
// Re-lay the whole program out (graph + leaf/device setup). Use this rather than
// calling csp_csr/csp_rt_start separately: they share one bump-allocated region.
extern int     csp_rebuild(csp_rt_t*);
// The runtime struct's size for the RAM model. Normally sizeof(csp_rt_t); the
// host sets it to a target's size (--board) so the simulation is not skewed by
// the host's 64-bit pointers. 0 = use the real sizeof.
extern size_t  csp_sim_state;
extern void    csp_set_ufuncs(csp_rt_t*, const csp_func_t*, uint8_t count, uint8_t rom);
extern void    csp_set_uconst(csp_rt_t*, csp_const_fn uconst);
extern const csp_func_t* csp_match_func(csp_rt_t*,
					const tstr_t* name,
					uint8_t arity, rentry_t* rarg,
					int* is_user, int* func_idx);
extern int     csp_set_reactive(csp_rt_t*, int onoff);
extern int     csp_set_latch(csp_rt_t*, int onoff);
extern int     csp_scan_line(csp_rt_t*,char* str,token_t* tv,size_t* num_toks);
extern int     csp_parse(csp_rt_t*, char* str);
extern void    csp_csr(csp_rt_t* st);
// Segment-aware string helpers: operate on a logical string position (ROM in
// flash or RAM), so they are AVR-PROGMEM-safe where csp_str_at's raw pointer is
// not. NOINLINE to keep the flash-access logic in one place (code size).
extern int  csp_str_ncmp(csp_rt_t* st, sindex_t pos, const char* s, int n);
extern int  csp_str_eq(csp_rt_t* st, sindex_t pos, const char* s, int n);
// csp_str_eq against a RODATA string: both sides read a byte at a time through
// their own segment accessor, so neither has to be copied out first.
extern int  csp_str_eq_ro(csp_rt_t* st, sindex_t pos, rostring_t s, int n);
// How many rules the program has: the number #disable counts against, i.e. the
// OP_RULE count. Walks the stream, so it is always current -- st->n_rule_no is
// only as fresh as the last csp_rebuild.
extern index_t csp_n_rules(csp_rt_t* st);
extern void csp_print_str_at(csp_rt_t* st, sindex_t pos);
extern index_t csp_eval(csp_rt_t* st);
extern index_t csp_eval_range(csp_rt_t* st, index_t start, index_t stop);
extern int     csp_eval_rule(csp_rt_t* st, int);
extern index_t csp_react(csp_rt_t* st);
extern index_t csp_cycle(csp_rt_t* st);   // one cycle: mixes ROM/RAM modes
extern void    csp_undo(csp_rt_t* st);
extern void    csp_commit(csp_rt_t* st);

extern void csp_set_value(csp_rt_t* st, index_t n, value_t v);
extern void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v);
extern void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v);
extern void csp_set_dvalue(csp_rt_t* st, index_t n, uvalue_t u);
extern void csp_set_avalue(csp_rt_t* st, index_t n, uvalue_t u);
extern void csp_set_tvalue(csp_rt_t* st, index_t n, uvalue_t u);

extern void csp_pstate_save(csp_rt_t* st, csp_pmark_t* pm);
extern void csp_pstate_restore(csp_rt_t* st, csp_pmark_t* pm);

extern int csp_parse_expr(csp_rt_t* st, const token_t* tv, size_t* num_toks,
			  rentry_t* result);
extern int csp_parse_const_expr(csp_rt_t* st, const token_t* tv, size_t* num_toks,
				rentry_t* result);
//
extern index_t csp_new_decl(csp_rt_t* st,const tstr_t* name, decl_t op,int sys);
// Re-derive ps.nstr (and drop the walk cache) after ps.strp moved from outside
// new_string -- ROM activation, EEPROM load, /clear, undo.
extern void csp_str_recount(csp_rt_t* st);
// Derive ps.strp from the OP_SEGMENT runs, after instructions arrive from an
// image or an EEPROM patch.
extern void csp_str_resize(csp_rt_t* st);
// Take string segment k (a DECL_SEGMENT run in the decl pool) if it is not
// there yet. Also used by the EEPROM load, which has to make room before the
// stored bytes can land.
extern int str_seg_ensure(csp_rt_t* st, unsigned k);
extern index_t csp_lookup_decl(csp_rt_t* st, const tstr_t* name);
// #param overrides: shadow = the RAM declaration that SETS param `di`;
// target = the param a RAM declaration sets. See apply_param_overrides.
extern index_t csp_param_shadow(csp_rt_t* st, index_t di);
extern index_t csp_param_target(csp_rt_t* st, index_t di);

// One decoded settings entry. The two pointers aim INTO st->settings, so they
// are valid until the next csp_settings_record -- a caller that keeps one past
// that is reading whatever moved into its place.
typedef struct {
    const char* path;    // "Kp", "Led", "sys.NodeID" -- NOT nul-terminated
    uint8_t     plen;
    uint8_t     part;    // csp_part_t
    uint8_t     vt;      // vtype_t of the value
    uint8_t     res;     // declared width in bits, for the shape check
    value_t     val;     // scalar value (vt != V_STRING)
    const char* str;     // characters (vt == V_STRING), else NULL
    uint8_t     slen;
} csp_setting_t;

// Record what an IMMEDIATE write set, so /save can keep it. Rules never reach
// here: a rule writing a config part is the program doing its job, not someone
// configuring the unit. Returns -1 when the store is full.
extern int  csp_settings_record(csp_rt_t* st, xindex_t ix,
				csp_part_t part, value_t v);
// Lay the store over the declarations. Called from csp_rt_start, BEFORE
// csp_setup configures any hardware -- see doc/EEPROM.md.
extern void csp_settings_apply(csp_rt_t* st);
// Read entry `n` (0-based). Returns 0 at the end of the store.
extern int  csp_settings_get(csp_rt_t* st, int n, csp_setting_t* sp);
// One setting by its path TEXT -- no declarations needed. See csp_boot_pick.
extern int  csp_settings_find(csp_rt_t* st, const char* path, uint8_t plen,
			      csp_setting_t* sp);
// Read sys.Boot out of the settings store into st->boot_want. Call after the
// store has been read (csp_eeprom_peek) and BEFORE csp_load_rom.
extern void csp_boot_pick(csp_rt_t* st);
// Does this entry's path still resolve in the running program? An orphan is
// kept and not applied -- the next firmware may reintroduce the name.
extern int  csp_settings_resolve(csp_rt_t* st, const csp_setting_t* sp,
				 xindex_t* ixp, index_t* objp);
extern void csp_settings_clear(csp_rt_t* st);
#define CSP_SET_ORPHAN  0   // the path names nothing in this firmware
#define CSP_SET_REFUSED 1   // it does, but the width or type moved
#define CSP_SET_LIVE    2   // applied
extern int  csp_settings_status(csp_rt_t* st, const csp_setting_t* sp);
extern int  csp_settings_covers(csp_rt_t* st, index_t di);
// In csp_print.c, where the disassembler already needed it.
extern rostring_t csp_part_name(csp_part_t part);

// backend port (linux/arduino/LPCopen/FreeRTOS)
extern uint32_t csp_time_ms(void);
extern unsigned long csp_time_us(void);
extern void csp_setup(csp_rt_t* st);
extern void csp_input(csp_rt_t* st);
extern void csp_output(csp_rt_t* st);
// common timer processing 
// Frames drained per csp_input. A bound is required: an idle bus costs one
// failed recv, a saturated one must not starve the cycle.
#ifndef CSP_CAN_RX_BURST
#define CSP_CAN_RX_BURST 8
#endif

// The same bound for datagrams. There is no MTU constant to go with it: a
// datagram is read straight into the buffer it feeds, so the declared size IS
// the limit and a longer one is truncated -- which is what recv does anyway.
#ifndef CSP_UDP_RX_BURST
#define CSP_UDP_RX_BURST 4
#endif

// CAN. The core owns the frame logic (id -> buffer, bit packing, dirty
// tracking) and calls down to three driver hooks. A driver with no bus links
// the weak no-op versions in csp_can_none.c and everything above still works.
//   csp_can_recv: 1 = a frame was read, 0 = nothing pending, -1 = error.
//   csp_can_send: 0 = sent, -1 = error.
extern int  csp_can_init(csp_rt_t* st);
extern int  csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len);
extern int  csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len);
// Called from the driver's csp_input/csp_output.
extern void csp_can_input(csp_rt_t* st);
extern void csp_can_output(csp_rt_t* st);
// 1 if the program has an inbound frame, i.e. the driver loop must keep running
// even with no timers and nothing changing.
extern int  csp_can_active(csp_rt_t* st);

// --- the other three transports ---------------------------------------------
//
// Same shape as CAN and for the same reason: the core owns the buffer logic --
// which endpoint feeds which buffer, the bit packing, the dirty tracking -- and
// calls down to hooks a port implements. Every hook below has a weak do-nothing
// default in src/csp_transport.c, so a port that implements none of them links
// and a program using them runs and stays quiet.
//
// UDP is ASYNCHRONOUS, like CAN: datagrams arrive on their own.
//   csp_udp_recv: 1 = a datagram was read, 0 = nothing pending, -1 = error.
//   csp_udp_send: 0 = sent, -1 = error.
extern int csp_udp_open(csp_rt_t* st, uint16_t port);
extern int csp_udp_recv(csp_rt_t* st, uint16_t port, uint8_t* data, uint16_t* len);
extern int csp_udp_send(csp_rt_t* st, uint32_t addr, uint16_t port,
			const uint8_t* data, uint16_t len);

// I2C and SPI are SYNCHRONOUS -- we are the master -- and the pair is
// deliberately split so a transfer can overlap the cycle that started it:
//
//   csp_output   csp_*_start(...)   ->  0 started, -1 busy or no bus
//   csp_input    csp_*_done(...)    ->  1 complete, 0 in flight, -1 failed
//
// `data` is the buffer's own storage and the driver may write into it directly
// (DMA lands there); it stays put for the life of the program. A port with no
// DMA is free to do the transfer inside _start and report 1 immediately from
// _done -- the sequencing is the same, it just costs the loop time.
extern int csp_i2c_start(csp_rt_t* st, uint32_t xref, uint8_t* data,
			 uint16_t len, int is_read);
extern int csp_i2c_done(csp_rt_t* st, uint32_t xref, uint16_t* len);
extern int csp_spi_start(csp_rt_t* st, uint32_t xref, uint8_t* data,
			 uint16_t len, int is_read);
extern int csp_spi_done(csp_rt_t* st, uint32_t xref, uint16_t* len);

// One pass over every buffer with a transport. csp_can_input/output are these
// under their old names -- a driver calls whichever it has always called.
extern void csp_buf_input(csp_rt_t* st);
extern void csp_buf_output(csp_rt_t* st);

extern void csp_input_timer(csp_rt_t* st);
extern void csp_output_timer(csp_rt_t* st);

// eeprom save/load (csp_eeprom.c)
extern int csp_eeprom_save(csp_rt_t* st);
extern int csp_eeprom_load(csp_rt_t* st);
// The settings section ALONE, for the boot path: sys.Boot has to be read before
// csp_load_rom chooses an image. Silent on failure; the caller then boots with
// no preference and csp_eeprom_load reports the real problem later.
extern int csp_eeprom_peek(csp_rt_t* st);
extern int csp_eeprom_size(csp_rt_t* st);   // bytes THIS program needs to save
extern int csp_eeprom_clear(csp_rt_t* st);

// stack check/debug
extern int stack_used();
// Stack watch (diagnostic): worst-ever margin between the stack pointer and the
// arena top. Sample it anywhere; the deeper the call, the more it is worth.
// Gated behind CSP_STACK_WATCH (the `watch` make target) -- off, the sample
// calls vanish and the runtime carries none of it. See csp_rt.c.
#ifdef CSP_STACK_WATCH
extern char* csp_arena_top;
extern long  csp_stack_low;
extern void* csp_stack_low_fn;
extern void  csp_stack_mark(void);
#else
#define csp_stack_mark() ((void)0)
#endif

extern const char  csp_tag(csp_rt_t* st, index_t n);
extern rostring_t csp_fmt_pindir(uint8_t dir);
extern rostring_t csp_fmt_pull(csp_rt_t* st, int ix);
extern rostring_t csp_fmt_pwm(csp_rt_t* st, int ix);
extern rostring_t csp_fmt_vtype(vtype_t vt);
extern rostring_t csp_fmt_endian(vendian_t et);
// The error text itself is not exported: it is a format string that only
// csp_print_error knows how to read (flash on AVR, %s/%d substituted from
// ps.err_args). Print an error with csp_print_error(st).
// Print the current error with %s/%d substituted (light printf, no stdio).
extern void    csp_print_error(csp_rt_t* st);

extern int csp_opcode_to_tok(opcode_t opcode);
extern uint8_t csp_opcode_rtype(opcode_t opcode);
// csp_set_error return 1 if error was set and arguments can be defined!
extern int csp_set_error(csp_rt_t*, csp_err_t);
extern void csp_set_err_arg_tstr(csp_rt_t*, int i, const tstr_t* str);
extern void csp_set_err_arg_rostr(csp_rt_t*, int i, rostring_t str);
extern void csp_set_err_arg_ix(csp_rt_t*, int i, index_t ix);
extern void csp_clr_error(csp_rt_t*);
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
extern void csp_enq_elist(csp_rt_t* st, index_t x);
#endif

// Interactive command handling
#define CSP_CMD_OK       0
#define CSP_CMD_QUIT     1
#define CSP_CMD_NOTFOUND -1
#define CSP_CMD_ERROR    -2

typedef int (*csp_cmd_fn)(csp_rt_t* st, int argc, char* argv[]);

typedef struct {
    rostring_t name;
    rostring_t help;
    csp_cmd_fn fn;
} csp_cmd_t;

extern int csp_cmd_dispatch(csp_rt_t* st, char* cmd);
extern void csp_cmd_help(void);
// /undo: mark where the bump cursors stand, and keep the mark if the line that
// followed actually appended something. See cmd_undo in csp_repl.c.
extern void csp_undo_mark(csp_rt_t* st, csp_undo_t* s);
extern void csp_undo_push(csp_rt_t* st, const csp_undo_t* s);
// Bytes the csp_rt_t struct itself takes -- /memory reports it as `struct`.
extern void csp_set_err_arg_int(csp_rt_t* st, int i, int ival);
extern uint32_t model_state(void);
// True when decl `di` is the implicit State variable -- the runtime's sticky
// FAILSAFE gate asks, and so does the listing (which must not print it).
extern int state_is_state_var(csp_rt_t* st, int di);
extern int lookup_state(csp_rt_t* st, const tstr_t* name);
// Elements in the array headed by declaration `i` (1 for a plain variable).
extern uint16_t csp_array_len(csp_rt_t* st, index_t i);
// Shared by the tokenizer and the command splitter.
#ifndef ISBLANK
#define ISBLANK(c) (((c) == ' ') || ((c) == '\t'))
#endif
extern int csp_process_line(csp_rt_t* st, char* line);


#if 0

// Line input handling (shared between platforms). The buffer is sized from the
// arena at boot, between these two bounds: never less than the old fixed AVR
// size, never more than a line anyone types by hand. A 32nd of the pool, so a
// board with room gets a longer line and a small one is not squeezed for it.
#define CSP_LINE_MIN   64
#define CSP_LINE_MAX  512
#define CSP_LINE_SHARE 32

// Command history, carved off the arena the same way and by the same share, so
// a board with room gets it and a board without does not. Entries are stored
// LENGTH-TEXT-LENGTH: two bytes of overhead buys a walk in both directions --
// forwards to drop the oldest when it fills, backwards to recall the newest
// first, which is the only order anybody browses in.
//
// Under CSP_HIST_MIN there is no point: one short line is not a history. Set
// CSP_HISTORY_BYTES to 0 to leave it out entirely -- the cursor editing below
// costs nothing extra and stays.
#define CSP_HIST_SHARE 32
#define CSP_HIST_MAX  512
#define CSP_HIST_MIN   32
// A line longer than this is edited normally but not REMEMBERED: the length
// byte at each end holds 255, and widening it to two would cost every entry a
// byte to buy back a case nobody types.
#define CSP_HIST_LINE_MAX 255


extern void csp_line_init(csp_rt_t* st);
extern void csp_line_input(csp_rt_t* st, char c);
extern void csp_line_prompt(csp_rt_t* st);
// Room for one more byte. A reader keeps draining its port while this is true,
// INCLUDING while a line is waiting to run -- that spare room is what absorbs a
// paste. When it goes false the driver's FIFO takes over.
extern int  csp_line_space(csp_rt_t* st);
// Finished with the line at the front: drop it and bring anything queued behind
// it down to the start. MUST be called instead of clearing line_ready by hand.
extern void csp_line_done(csp_rt_t* st);
#endif

// eeprom api
// What to CALL the backing store in /save and /load output ("eeprom.db" on the
// host, "EEPROM" on a board). The only platform-specific part of those commands
// -- everything else is shared, so the two print identically.
extern const char* csp_eeprom_name(void);
extern int csp_eeprom_open_read(void);
extern int csp_eeprom_open_write(void);
extern void csp_eeprom_close(void);
extern int csp_eeprom_read(void* buf, size_t len);
extern int csp_eeprom_write(const void* buf, size_t len);
// Backend: bytes of persistent storage this board HAS. Pair it with
// csp_eeprom_size(): RAM can hold more code than you can persist, and code you
// cannot save is only good for testing -- so on a real board this, not
// CSP_CODE_BUDGET, is often the binding limit.
#define CSP_EEPROM_NONE      0u           // board has none: /save can never work
#define CSP_EEPROM_UNBOUNDED 0xFFFFFFFFu  // a host file -- no board ceiling
extern uint32_t csp_eeprom_capacity(void);

// RAM statistics
extern uint32_t csp_system_ram_capacity(void);
extern uint32_t csp_system_ram_used(void);
extern uint32_t csp_system_ram_avail(void);

#ifdef __cplusplus
EXTERN_C_END
#endif

// Poison float on a FIXPOINT build, so a stray float in the firmware fails at
// the line that wrote it instead of dragging in soft-float. Outside the #ifndef
// above on purpose: the guard tracks the VALUE, however it was arrived at, so
// -DUSE_FIXPOINT=1 from a board Makefile is protected the same as the default.
// And `#if defined(X) && (X == 1)`, not `#ifdef X && ...` -- #ifdef takes one
// identifier and silently discards the rest, which made this unconditional and
// poisoned the float targets (ESP32/ESP8266 have USE_FIXPOINT 0) as well.
//
// Only for a BOARD build. The host tools print with printf and the poison would
// fail them at the first %f -- and a host that drags in soft-float costs
// nothing anyway. That is why this lived in the sketch's own csp_config.h
// before the two files were merged.
#if defined(CSP_BOARD) && defined(USE_FIXPOINT) && (USE_FIXPOINT == 1)
#define float   _Pragma("GCC error \"float not allowed\"") float
#define double  _Pragma("GCC error \"double not allowed\"") double
#endif

#endif
