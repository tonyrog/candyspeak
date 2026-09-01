// CandySpeak interactive commands -- the REPL's command layer.
//
// Split out of csp_rt.c because it is the one part with almost no ties back
// into it: the whole block referenced exactly ONE static from the runtime
// (model_state), everything else it needs is already public in csp.h. That
// makes the boundary real rather than a comment, and it is the same boundary
// CSP_EXEC_ONLY draws -- a node that only runs its ROM has no commands.
//
// The file is guarded as a whole rather than per-function: the Arduino build
// compiles every .c in the sketch directory, so leaving an empty translation
// unit is how you opt out there.
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "csp.h"
#include "csp_strings.h"   // shared RODATA strings (generated from strings.tab)
#include "csp_parse.h"
#include "csp_compile.h"
#include "csp_print.h"

#if !defined(CSP_EXEC_ONLY)

// ============================================================
// Interactive command handling
// ============================================================

#define MAX_ARGV 32

static int cmd_help(csp_rt_t* st, int argc, char* argv[]);
static int cmd_list(csp_rt_t* st, int argc, char* arg[]);
static int cmd_state(csp_rt_t* st, int argc, char* argv[]);
// /images -- what this firmware linked in. The registry answers that; it does
// NOT answer what is on the chip (a FAILSAFE flashed on its own, an A/B slot
// updated in the field), which needs a flash scan.
static int cmd_images(csp_rt_t* st, int argc, char* argv[])
{
    int n = csp_image_count();
    int i;
    (void)argc; (void)argv; (void)st;

    for (i = 0; i < n; i++) {
	const uint8_t* base = csp_image_at(i);
	csp_image_header_t h = ro_header((const csp_image_header_t*)base);
	int ok = (csp_crc16(0xFFFF, &h, sizeof(h) - sizeof(uint16_t), 0)
		  == h.crc_hdr);
	csp_print_uint(i);
	csp_print_lit(": ");
	csp_print_rostr((h.role == CSP_ROLE_FAILSAFE) ? ros_FAILSAFE : ros_ROM);
	csp_print_lit(" gen=");
	csp_print_uint(h.generation);
	csp_print_lit(" size=");
	csp_print_uint(h.size);
	csp_print_lit(" rules=");
	csp_print_uint(h.n_instr);
	// csp_print_line stashes its argument as a static flash literal, so it
	// takes a literal and not an expression.
	if (ok)
	    csp_print_line("");
	else
	    csp_print_line("  (header CRC BAD)");
    }
    if (n == 0)
	csp_print_line("no images registered");
    return CSP_CMD_OK;
}

static int cmd_reset(csp_rt_t* st, int argc, char* argv[]);
static int cmd_clear(csp_rt_t* st, int argc, char* argv[]);
static int cmd_undo(csp_rt_t* st, int argc, char* argv[]);
static int cmd_memory(csp_rt_t* st, int argc, char* argv[]);
static int cmd_commit(csp_rt_t* st, int argc, char* argv[]);
static int cmd_quit(csp_rt_t* st, int argc, char* argv[]);
static int cmd_latch(csp_rt_t* st, int argc, char* argv[]);
static int cmd_settings(csp_rt_t* st, int argc, char* argv[]);
static int cmd_save(csp_rt_t* st, int argc, char* argv[]);
static int cmd_load(csp_rt_t* st, int argc, char* argv[]);
static int cmd_images(csp_rt_t* st, int argc, char* argv[]);
static int cmd_pause(csp_rt_t* st, int argc, char* argv[]);
static int cmd_live(csp_rt_t* st, int argc, char* argv[]);
static int cmd_resume(csp_rt_t* st, int argc, char* argv[]);

// name and help point into FLASH (strings.tab); the table itself stays in RAM.
// Moving the whole table would put the FUNCTION POINTERS in flash too, and
// ro_ptr is a 16-bit pgm_read_word -- on a mega2560 (256K flash) a function
// pointer does not fit in 16 bits. That is the same trap as the packed
// csp_func_t that HardFaulted this project once. So: text out, pointers stay.
static const csp_cmd_t builtin_cmds[] = {
    { ros_cmd_help,   ros_h_help,    cmd_help },
    { ros_cmd_query,  NULL,        cmd_help },
    { ros_cmd_list,   ros_h_list,    cmd_list },
    { ros_cmd_state,  ros_h_state,   cmd_state },
    { ros_cmd_memory, ros_h_memory,  cmd_memory },
    { ros_cmd_images, ros_h_images,  cmd_images },
    { ros_cmd_pause,  ros_h_pause,   cmd_pause },
    { ros_cmd_live,   ros_h_live,    cmd_live },
    { ros_cmd_resume, ros_h_resume,  cmd_resume },
    { ros_cmd_reset,  ros_h_reset,   cmd_reset },
    { ros_cmd_clear,  ros_h_clear,   cmd_clear },
    { ros_cmd_undo,   ros_h_undo,    cmd_undo },
    { ros_cmd_latch,  ros_h_latch,   cmd_latch },
    { ros_cmd_commit, ros_h_commit,  cmd_commit },
    { ros_cmd_settings, ros_h_settings, cmd_settings },
    { ros_cmd_save,   ros_h_save,    cmd_save },
    { ros_cmd_load,   ros_h_load,    cmd_load },
    { ros_cmd_quit,   ros_h_quit,    cmd_quit },
    { ros_cmd_exit,   NULL,        cmd_quit },
    { NULL, NULL, NULL }
};

static int cmd_help(csp_rt_t* st, int argc, char* argv[])
{
    (void)st; (void)argv;
    csp_print_line("Commands:");
    for (const csp_cmd_t* c = builtin_cmds; c->name; c++) {
	if (c->help) {
	    int len;
	    csp_print_blank();
	    csp_print_char('/');
	    csp_print_rostr(c->name);       // name/help are in flash
	    len = ro_strlen(c->name);
	    while (len++ < 10) csp_print_blank();
	    csp_print_rostr(c->help);
	    csp_println();
	}
    }
    return CSP_CMD_OK;
}

//
// /list [filter]
//
// List rules
//
// /list LED1 LED2        write to LED1 or LED2
// /list ?A               depend on A (A is in the condition)
// /list ?A ?B            depend on A and B
// /list LED1 ?A ?B :Run  write to LED1 and depend on A and B in state Run
// /list :Fail            list rules in state Fail
//
#define MAX_FILTER 8  // MAX 32 (bitmasks)

typedef struct _filter_var_t {
    index_t ix;   // variable/constant/digital/analog/state-num
    char    typ;  // '?' in condition ' ' in body, ':' state
} filter_var_t;

// lookup filter index from var index or state number
static int lookup_filter(index_t ix, int cnd, filter_var_t* fv, int nf)
{
    int i = 0;
    while(i < nf) {
	if (ix == fv[i].ix) {
	    if ((cnd==1) && (fv[i].typ=='?'))
		return i;
	    // a bare name (typ ' ') matches wherever the var is referenced --
	    // in the body (cnd 0) OR the condition (cnd 1). '?' stays condition-only.
	    if (((cnd==0)||(cnd==1)) && (fv[i].typ==' '))
		return i;
	    if ((cnd == 2) && ((fv[i].typ=='?')||(fv[i].typ==' ')))
		return i;
	    if ((cnd == 3) && (fv[i].typ==':'))
		return i;
	}
	i++;
    }
    return -1;
}

static int is_fvar(index_t ix, int cnd, filter_var_t* fv, int nf)
{
    return (lookup_filter(ix, cnd, fv, nf) >= 0);
}

// The leading column of a /list line: "%3d R  " for a rule, "    R  " for a
// declaration (nothing to number). Same width either way, so the two kinds line
// up. Pass no <= 0 for the blank form. A disabled rule gets '!' in place of the
// trailing space, so the mark sits in its own column and nothing shifts.
//
// The NUMBER is 1-based and counts OP_RULE in instruction order. Every rule
// emits one -- an unguarded rule gets an always-true LI first (see the cnd < 0
// branch in asm_rule) -- so the sequence has no holes and matches what a user
// counts on screen. Deliberately NOT the reactive ordinal from number_rules,
// which also numbers module entries and the implicit body at each range base.
//
// The SEGMENT comes from the OP_RULE's own ip, not from the body start. The
// body start is only reset at OP_NEXT/ENTER/LEAVE, so if the ROM range does not
// happen to end on one, it stays below rom_nn while the walk has already moved
// into RAM -- which is what tagged RAM rules as [ROM].
//
// F = flash/ROM (baked into the firmware, survives everything).
// E = RAM, and eeprom holds a copy -- /clear drops it from RAM, the next boot or
//     a /load brings it straight back.
// R = RAM only. Nothing else holds a copy: /clear or a power cut loses it.
//
// E vs R is the question you actually want answered before typing /clear, and it
// cannot be read off the segment alone -- both live in the same RAM patch. The
// watermark in st->ee_* is what separates them.
//
// The tag used to be printed as a LEADING column, which made every line
// unpasteable: you had to strip "  1 R  " by hand before it was source again.
// It is now remembered here and emitted by list_eol() as a trailing comment, so
// a listing can be selected in one terminal and pasted straight into another --
// which is how a program actually gets copied off a board in the field.
static int list_no, list_seg, list_off, list_pending;

static void list_column(int no, int seg, int off)
{
    list_no = no; list_seg = seg; list_off = off;
    list_pending = 1;
}

// Segment tag for a DECLARATION at logical index i, and for an INSTRUCTION at
// logical position n. Both take the same shape: below the ROM count it is flash,
// otherwise its distance above the RAM base decides whether the eeprom copy
// reaches it. Signed throughout -- the sys area (the implicit State) sits between
// rom_nd and the RAM base, so the subtraction can legitimately go negative.
static int decl_seg(csp_rt_t* st, sindex_t i)
{
    sindex_t ram;
    if (i < (sindex_t)st->rom_nd)
	return 'F';
    if ((ram = i - (sindex_t)CSP_BASE_ND(st)) < 0)
	return 'R';                  // runtime-internal, never actually listed
    return (ram < (sindex_t)st->ee_nd) ? 'E' : 'R';
}

static int instr_seg(csp_rt_t* st, sindex_t n)
{
    sindex_t ram;
    if (n < (sindex_t)st->rom_nn)
	return 'F';
    if ((ram = n - (sindex_t)CSP_BASE_NN(st)) < 0)
	return 'R';
    return (ram < (sindex_t)st->ee_nn) ? 'E' : 'R';
}

// End a listing line: the trailing tag comment, then the newline. With nothing
// pending it is a plain newline, so it is safe to use as the line terminator
// everywhere in the listing -- a line that never set a tag simply gets none.
static void list_eol(void)
{
    if (list_pending) {
	csp_print_lit("  // ");
	if (list_no > 0) {
	    csp_print_uint(list_no);
	    csp_print_blank();
	}
	csp_print_char(list_seg);
	if (list_off)
	    csp_print_char('!');
	list_pending = 0;
    }
    csp_println();
}

// find a #module declaration by name (for /list <Module> scoping)
static index_t find_module(csp_rt_t* st, const char* name)
{
    int i;
    int len = (int)strlen(name);
    for (i = 0; i < st->ps.nd; i++) {
	if (decl(st, i, type) == DECL_MODULE) {
	    if (csp_str_eq(st, decl(st,i,name), name, len))
		return MAKE_INDEX(0, i);
	}
    }
    return BAD_INDEX;
}

static void print_decl(decl_t d)
{
    csp_print_char('#');
    switch(d) {
    case DECL_MODULE: csp_print_rostr(ros_module); break;
    case DECL_VARIABLE:	csp_print_rostr(ros_variable); break;
    case DECL_CONSTANT:	csp_print_rostr(ros_constant); break;
    case DECL_TIMER:	csp_print_rostr(ros_timer); break;
    case DECL_ANALOG:	csp_print_rostr(ros_analog); break;
    case DECL_DIGITAL:	csp_print_rostr(ros_digital); break;
    case DECL_BUFFER:   csp_print_rostr(ros_buffer); break;
    case DECL_FIELD:      csp_print_rostr(ros_field); break;
    case DECL_STATES:   csp_print_rostr(ros_states); break;	
    case DECL_NONE:     csp_print_rostr(ros_none); break;
    case DECL_IN:       csp_print_rostr(ros_in); break;
    case DECL_END:      csp_print_rostr(ros_end); break;
    case DECL_OBJECT:
    case DECL_VIEW:
	csp_print_rostr(ros_undefined); break;
    case DECL_AVAIL:
    case DECL_END_MARK:
	break;
    }    
    csp_print_blank();    
}

// print a leaf name (by logical string position), qualified as Mod.name when
// inside a module. mod == 0 means global. Segment-aware (ROM flash or RAM).
// A member is printed with its BARE name. The enclosing "#module M" line
// already says which module it belongs to, and "M.A" is a display convention
// that is not valid input -- with it, a module body could be read but never
// pasted back. mod is kept in the signature because the caller knows the
// context and a future format may want it again.
static void list_name(csp_rt_t* st, sindex_t mod, sindex_t name)
{
    (void)mod;
    csp_print_str_at(st, name);
}

static void print_decl_and_name(csp_rt_t* st, decl_t d, sindex_t mod, sindex_t name)
{
    print_decl(d);
    list_name(st, mod, name);
}

// `[N]` after the name of an array's head. Nothing for a plain declaration.
static void list_array_len(csp_rt_t* st, int i)
{
    uint16_t alen = csp_array_len(st, i);
    if (alen > 1) {
	csp_print_char('[');
	csp_print_uint(alen);
	csp_print_char(']');
    }
}

// `port:pin` for a device, or the whole spec for a device ARRAY -- consecutive
// elements collapse back into `a..b` and a jump starts a new group, so
// `9:0..9` and `0:1..5,7,9` come out as they went in. Each element holds its own
// port and pin, so this reads them rather than assuming anything about the
// order -- and a PORT change both ends the run in progress and prints itself,
// without which `0:2,1:5,2:6` listed as `0:2,5..7`: three ports collapsed into
// one range on the strength of the pin numbers alone.
static void list_pin_spec(csp_rt_t* st, int i, int is_digital)
{
    uint16_t alen = csp_array_len(st, i);
    unsigned port, start, last;
    uint16_t k;

    port = is_digital ? decl(st,i,di.port) : decl(st,i,an.port);
    csp_print_uint(port);
    csp_print_char(':');
    start = last = is_digital ? decl(st,i,di.pin) : decl(st,i,an.pin);
    csp_print_uint(start);
    for (k = 1; k < alen; k++) {
	unsigned q = is_digital ? decl(st,i+k,di.port) : decl(st,i+k,an.port);
	unsigned p = is_digital ? decl(st,i+k,di.pin)  : decl(st,i+k,an.pin);
	if ((q == port) && (p == last + 1)) {
	    last = p;
	    continue;
	}
	if (last != start) { csp_print_lit(".."); csp_print_uint(last); }
	csp_print_char(',');
	if (q != port) {
	    port = q;
	    csp_print_uint(port);
	    csp_print_char(':');
	}
	csp_print_uint(p);
	start = last = p;
    }
    if (last != start) { csp_print_lit(".."); csp_print_uint(last); }
}

// Two spaces per nesting level. A module's members and its rules share it, so a
// block reads as the unit it is instead of a flat list with a "Mod: " prefix on
// half the lines -- and the prefix was not source you could paste back.
static void list_indent(int n)
{
    while (n-- > 0) { csp_print_blank(); csp_print_blank(); }
}

// The filter set /list was given, so the rule walk can be run over a RANGE
// instead of only over the whole program -- which is what lets a module's rules
// be listed inside its own block.
typedef struct {
    filter_var_t* filt;
    int           nf;
    uint32_t      cmask;    // condition-filter variables (filter index bitmask)
    uint32_t      bmask;    // body-filter variables
    // States named with `:S`, as a bitmask over state NUMBERS. A rule matches
    // when its `#in` block covers one of them -- that is what "runs in S" means.
    // Separate from filt[], which is keyed on declaration index: a state number
    // is not one, and squeezing it in there is why the ':' filter never worked.
    uint32_t      smask;
    const char*   scope;    // restrict to this module, NULL for everything
    // Called before the FIRST line this walk prints, or not at all. The module
    // wrapper is held back the same way an `#in` header is -- and its rules are
    // printed from in here, so the walk has to be able to flush it.
    void (*flush)(csp_rt_t*, void*);
    void*         flush_arg;
} list_ctx_t;

// The `#in <states>` line itself, from st->list_states. The caller has already
// emitted the tag column and the indent -- this is only the text, so the eager
// and the deferred paths cannot render it differently.
static void list_in_header(csp_rt_t* st, int indent)
{
    int k;
    (void)indent;
    csp_print_lit("#in");
    for (k = 0; k < st->list_nstate; k++) {
	sindex_t np = state_name_pos(st, st->list_states[k]);
	csp_print_blank();
	if (np > 0)
	    csp_print_str_at(st, np);
    }
    list_eol();
}

// List the rules in instructions [from, to), numbering them from `first`, and
// return the number the next rule would get. Segment and number come from the
// OP_RULE -- see list_column.
//
// `skip_modules`: at the top level a module's body is walked only to COUNT its
// rules, because they were already printed inside the `#module ... #end` block.
// The numbers stay absolute either way, so #disable means the same thing.
// `indent`: two spaces per level, so a module's rules line up with its members.
static int list_rules(csp_rt_t* st, list_ctx_t* c, int from, int to,
		      int indent, int skip_modules)
{
    int i, f;
    int cnd;         // in condition part
    int rule;        // rule start index (in condition part)
    int rule_pos;    // ip of this body's OP_RULE, -1 if it has none
    int rule_no;     // user-facing rule number, 1-based
    int block_end;   // ip past the current #in block, -1 if not in one
    // A `#in` header is printed LAZILY -- when the first rule inside it actually
    // survives the filter -- and the matching `#end` only if the header went out.
    // Printed eagerly, a filtered listing was mostly scaffolding: `/list Red` on
    // the traffic example gave eight `#in X` / `#end` pairs with nothing between
    // them, more lines of empty block than of matching rule.
    int block_gate;  // ip of the gate whose `#in` is still unprinted, -1 if none
    int block_shown; // 1 once this block's `#in` has been printed
    uint32_t fbits;  // variables/states present in this rule (by filter index)
    sindex_t cur_mod = 0;

    rule = from;
    i  = rule;
    cnd = 1;
    rule_pos = -1;
    rule_no = 0;
    fbits = 0;
    block_end = -1;
    block_gate = -1;
    block_shown = 0;
    st->list_nstate = 0;
    // Rules are numbered by absolute position, so a range starting part-way in
    // has to know how many came before it.
    for (f = 0; f < from; f++) {
	if (instr(st,f,op) == OP_SEGMENT) {
	    f += instr(st,f,sg.num);      // the loop's f++ steps past the header
	    continue;
	}
	if (instr(st,f,op) == OP_RULE)
	    rule_no++;
    }
    while (i < to) {
	// Identifier text, not code: step the whole run. Read as instructions
	// its characters look like a gate or a rule, and the listing either
	// invents a block or -- as it did -- loses the real one that followed.
	if (instr(st,i,op) == OP_SEGMENT) {
	    i += instr(st,i,sg.num) + 1;
	    continue;
	}
	// Close a finished #in block: print `#end` (no number), leave the state.
	// Only when its `#in` was printed -- an empty block writes neither half.
	if ((block_end >= 0) && (i >= block_end)) {
	    if (block_shown) {
		list_column(0, instr_seg(st, i), 0);
		list_indent(indent);
		print_decl(DECL_END);
		list_eol();
	    }
	    block_end = -1;
	    block_gate = -1;
	    block_shown = 0;
	    st->list_nstate = 0;
	}
	// A block gate is `LD State ; NINSTATE* ; INSTATE` (open_in_block). Emit
	// `#in <states>` from the chain, arm block_end, and let list_states drop
	// the per-rule State guard. The whole gate is consumed here, never listed.
	if ((instr(st,i,op) == OP_LD) && (i+1 < to) &&
	    ((instr(st,i+1,op) == OP_NINSTATE) ||
	     (instr(st,i+1,op) == OP_INSTATE))) {
	    int j = i + 1;
	    int ns = 0;
	    while ((j < to) && (instr(st,j,op) == OP_NINSTATE)) {
		if (ns < MAX_IN_STATES) st->list_states[ns++] = instr(st,j,in.imm);
		j++;
	    }
	    // terminating INSTATE
	    if (ns < MAX_IN_STATES) st->list_states[ns++] = instr(st,j,in.imm);
	    block_end = j + instr(st,j,in.nxt);
	    st->list_nstate = ns;
	    // With a filter: remembered, not printed -- the header goes out with
	    // the first rule inside it that survives. Without one: printed now,
	    // because an unfiltered listing is a faithful rendering of the program
	    // and `#in fail` with nothing in it is something the source SAYS.
	    // Empty blocks are noise when they are empty because of the filter,
	    // not when they are empty in the program.
	    //
	    // list_states/list_nstate are set either way: the rule renderer reads
	    // them to drop the per-rule State guard.
	    block_gate = i;
	    block_shown = 0;
	    if (!c->nf && !c->scope && !c->smask) {
		list_column(0, instr_seg(st, i), 0);
		list_indent(indent);
		list_in_header(st, indent);
		block_shown = 1;
	    }
	    i = j + 1;                  // resume after the whole gate
	    rule = i;
	    continue;
	}
	switch(instr(st,i,op)) {
	case OP_ENTER:
	    if (skip_modules) {
		// Already listed inside its own block. Count past it so the
		// numbering of what follows is unchanged, and resume after the
		// matching OP_LEAVE (e.num = instructions between the two).
		int body_n = instr(st,i,e.num);
		int j;
		for (j = i+1; j < i+1+body_n; j++)
		    if (instr(st,j,op) == OP_RULE)
			rule_no++;
		i = i + body_n + 2;
	    }
	    else {
		cur_mod = decl_name_pos(st, MAKE_INDEX(0, instr(st,i,e.mx)));
		i++;
	    }
	    rule = i;
	    break;
	case OP_LEAVE:
	    cur_mod = 0;
	    i++; rule = i;
	    break;
	case OP_RULE:
	    cnd=0; rule_pos = i; rule_no++; i++;
	    break;
	case OP_NEXT:
	    cnd=1; i++;
	    {
		// Guard each mask: a zero mask must NOT vacuously pass (the old
		// `(fbits&0)==0` bug made a bare filter list every rule). Only-":"
		// state filters (cmask==bmask==0) keep the legacy show-all.
		int show = (c->nf==0)
		    || (c->cmask && ((fbits&c->cmask)==c->cmask))
		    || (c->bmask && ((fbits&c->bmask)!=0))
		    || (c->cmask==0 && c->bmask==0);
		if (c->scope && !(cur_mod && csp_str_eq(st, cur_mod, c->scope, strlen(c->scope))))
		    show = 0;
		// `:S` -- does this rule RUN in S? Its enclosing block's states
		// answer that, and they are already collected for the renderer.
		// A rule with no block is the implicit NORMAL+ one, which runs
		// in INIT and NORMAL and nowhere else.
		// Which States does this rule RUN in? Three cases, and the listing
		// cannot tell the last two apart from the block list alone -- both
		// have none -- so the OP_RULE's `implicit` flag decides.
		if (c->smask && (rule_pos >= 0)) {
		    uint32_t rs;
		    if (st->list_nstate) {          // inside an #in block
			int k;
			rs = 0;
			for (k = 0; k < st->list_nstate; k++)
			    rs |= (1u << (st->list_states[k] & 31));
		    }
		    else if (instr(st, rule_pos, r.implicit))
			rs = (1u<<STATE_INIT) | (1u<<STATE_NORMAL);  // bare NORMAL+
		    else
			rs = 0xffffffffu;           // module body: ungated, any State
		    if (!(rs & c->smask))
			show = 0;
		}
		if (show && (rule_pos >= 0)) {
		    // Anything above us that is still pending -- the enclosing
		    // `#module` wrapper -- goes out first. One call site: an `#in`
		    // header only ever prints from here too.
		    if (c->flush)
			c->flush(st, c->flush_arg);
		    // First surviving rule of a block: its `#in` header, held back
		    // until now, goes out immediately above it.
		    if ((block_gate >= 0) && !block_shown) {
			list_column(0, instr_seg(st, block_gate), 0);
			list_indent(indent);
			list_in_header(st, indent);
			block_shown = 1;
		    }
		    list_column(rule_no, instr_seg(st, rule_pos),
				(rule_no <= MAX_DIS_RULES) &&
				bitset_tst(st->dis_rule, rule_no-1));
		    // One level deeper inside an #in block, so a listing nests the
		    // way the source does instead of running flat under the gate.
		    list_indent(indent + ((block_end >= 0) ? 1 : 0));
		    csp_print_rule(st, rule);
		    list_eol();
		}
	    }
	    fbits = 0;
	    rule = i;  // start new rule
	    rule_pos = -1;
	    break;
	case OP_LD:
	case OP_LDP:
	    if (c->nf) {
		if ((f = lookup_filter(instr(st,i,m.mem), cnd, c->filt, c->nf)) >= 0)
		    fbits |= (1 << f);
	    }
	    i++;
	    break;
	case OP_STIMP:
	case OP_ST:
	case OP_STP:
	    if (c->nf) {
		if ((f = lookup_filter(instr(st,i,m.mem), cnd, c->filt, c->nf)) >= 0)
		    fbits |= (1 << f);
	    }
	    i++;
	    break;
	case OP_STI:  // immediate store: memory index in the .mi arm
	    if (c->nf) {
		if ((f = lookup_filter(instr(st,i,mi.mem), cnd, c->filt, c->nf)) >= 0)
		    fbits |= (1 << f);
	    }
	    i++;
	    break;
	default: i++; break;
	}
    }
    // A #in block that ran to the end of the range still needs its `#end` -- but
    // again only if its header was printed.
    if ((block_end >= 0) && block_shown) {
	list_column(0, instr_seg(st, to - 1), 0);
	list_indent(indent);
	print_decl(DECL_END);
	list_eol();                 // csp_print_line here left the tag pending
    }
    st->list_nstate = 0;
    return rule_no;
}


// A value as it would be WRITTEN in source. Only strings differ from
// csp_print_value: a listing has to be pasteable, and `= World` reads as a
// reference to something named World. /state leaves them bare -- that is a value
// column, not source.
static void list_value(csp_rt_t* st, vtype_t vt, value_t val)
{
    if (vt == V_STRING) {
	csp_print_char('"');
	if (val.s > 0)
	    csp_print_str_at(st, val.s);
	csp_print_char('"');
	return;
    }
    csp_print_value(st, vt, val);
}

// The `#module M` wrapper, held back until something inside it prints. See the
// DECL_MODULE arm in cmd_list.
typedef struct {
    sindex_t st_name;   // module name position
    int      seg;       // its segment, for the tag column
    int      pending;   // 1 = seen, not yet printed
} mod_pending_t;

static void mod_flush(csp_rt_t* st, void* arg)
{
    mod_pending_t* mp = (mod_pending_t*)arg;
    if (!mp->pending)
	return;
    mp->pending = 0;
    list_column(0, mp->seg, 0);
    print_decl(DECL_MODULE);
    csp_print_str_at(st, mp->st_name);
    list_eol();
}

static int cmd_list(csp_rt_t* st, int argc, char* argv[])
{
    mod_pending_t mp = { 0, 0, 0 };
    int i;
    index_t ix;
    int nf = 0;   // number of filters
    filter_var_t filt[MAX_FILTER];
    int f;
    uint32_t cmask;  // condition filter variables (filter index bitmask)
    uint32_t bmask;  // body filter variables (filter index bitmask)
    uint32_t smask;  // states named with :S (bitmask over state numbers)
    list_ctx_t ctx;  // what list_rules needs to honour the same filters
    int sys_skip = 0;// declarations of the built-in Sys block still to skip
    int mod_decl = 0;// decl index of the module block being listed
    const char* name;
    sindex_t cur_mod = 0;        // module name pos being listed (0 = global)
    sindex_t npos;               // current decl's name position
    const char* scope = NULL;    // restrict listing to this module (arg)

    cmask = 0;
    bmask = 0;
    smask = 0;
    // register the filter variables and types
    for (i = 0; i < argc; i++) {
	char typ = ' ';
	name = argv[i];
	if (name[0]=='?')      { typ='?'; name++; }
	else if (name[0]==':') { typ=':'; name++; }

	if (typ==':') {
	    const tstr_t sname = { (char*)name, strlen(name) };
	    int s;
	    // Its own mask, not a filt[] entry. filt[] is keyed on DECLARATION
	    // index and matched against the memory operand of a load or store; a
	    // state number is neither, and the old code passed an uninitialised
	    // `ix` to lookup_filter to find out. Nothing then tested the entry
	    // against a rule, so `/list :green` listed the whole program.
	    if ((s = lookup_state(st, &sname)) >= 0) {
		if (s < 32)             // the mask is 32 wide; higher states cannot be asked for
		    smask |= (1u << s);
	    }
	}
	else if ((typ == ' ') && (find_module(st, name) != BAD_INDEX)) {
	    scope = name;   // restrict listing to this module's members
	}
	else {
	    const tstr_t sname = { (char*)name, strlen(name) };
	    if ((ix = csp_lookup_decl(st, &sname)) != BAD_INDEX) {
		if ((f = lookup_filter(ix, (typ == '?'), filt, nf)) < 0) {
		    if (nf >= MAX_FILTER) goto match;
		    if (typ == '?') cmask |= (1 << nf);
		    else if (typ == ' ') bmask |= (1 << nf);
		    filt[nf].typ = typ;
		    filt[nf++].ix = ix;
		}
	    }
	}
    }

match:
    // list declarations that match the filter. Iterate ALL decls (do not stop
    // at the first DECL_END -- module ends and the terminator are ENDs too).
    // Each line is tagged [ROM]/[RAM] by segment; module members are shown with
    // a Mod. prefix. scope != NULL restricts to that module's members.
    for (i = 0; i < st->ps.nd; i++) {
	index_t ix = MAKE_INDEX(0, i);
	csp_decl_t d = csp_get_decl(st, i);	
	int seg = decl_seg(st, i);
	// A param that has been re-declared lists ONCE, as the override. Its
	// original says what the program shipped with, which is no longer what
	// it runs with -- and both lines pasted back would be one declaration
	// and one setting of it, i.e. the same thing twice.
	if (csp_param_shadow(st, i) != BAD_INDEX)
	    continue;
	// The override itself is tagged P: it lives in RAM (or eeprom, once
	// saved) but what matters about it is that it PATCHES a declaration
	// further up -- usually one baked into flash, which cannot be edited.
	if (csp_param_target(st, i) != BAD_INDEX)
	    seg = 'P';
	// S last, because a setting is applied last: csp_rt_start lays the store
	// over the declarations AFTER the patch overrides. Without this the line
	// prints the source's pin or period with no hint that neither is what the
	// unit is running -- /state has the live value, /list did not say to look.
	if (csp_settings_covers(st, i))
	    seg = 'S';
	// The built-in Sys namespace and its instance are runtime machinery, the
	// same as the implicit State below: a listing is meant to paste back as
	// source, and re-declaring a built-in would collide with the one the
	// runtime makes itself. `sys_skip` counts the module's members and its
	// #end past as well. Their VALUES are /state's business, and the fields
	// are documented rather than listed.
	if ((st->sys_mod != BAD_INDEX) && (i == (int)INDEX(st->sys_mod))) {
	    sys_skip = decl(st, i, md.n) + 1;   // members + the #end
	    continue;
	}
	if (sys_skip > 0) {
	    sys_skip--;
	    continue;
	}
	if ((st->sys_obj != BAD_INDEX) && (i == (int)INDEX(st->sys_obj)))
	    continue;
	if (d.type == DECL_MODULE) {
	    cur_mod = d.name;
	    mod_decl = i;
	    // Held back, not printed. A module whose members and rules were all
	    // filtered out contributes nothing, and an empty `#module M` / `#end`
	    // pair is scaffolding around a hole. mod_flush puts it out the moment
	    // something inside it does print -- from here for members, through
	    // ctx.flush for rules -- so a listing that shows the block is still the
	    // complete, pasteable form.
	    if (!scope || csp_str_eq(st, cur_mod, scope, strlen(scope))) {
		mp.st_name = cur_mod;
		mp.seg     = seg;
		mp.pending = 1;
		if (!nf && !scope && !smask)  // unfiltered: faithful, so print now
		    mod_flush(st, &mp);
	    }
	    continue;
	}
	if (d.type == DECL_END) {         // module end or top-level terminator
	    if (cur_mod) {
		if (!scope || csp_str_eq(st, cur_mod, scope, strlen(scope))) {
		    // The module's RULES, before its #end -- that is where they
		    // are in the source, and a listing that puts them after the
		    // block (prefixed "Mod: ") is not source at all. The body is
		    // the instructions between the module's OP_ENTER and its
		    // OP_LEAVE; e.num is how many.
		    // Ask whether there IS an OP_ENTER, rather than whether ent
		    // is non-zero. 0 is a perfectly good entry point -- the
		    // first module in a program puts its OP_ENTER at
		    // instruction 0 -- and testing for it dropped every rule
		    // out of every module that happened to be first.
		    //
		    // A body-less module (csp_sys_module builds one: a namespace
		    // has no rules, so it emits no ENTER/LEAVE at all) leaves
		    // ent at 0 too. The opcode tells the two apart, and it is
		    // self-validating: whatever instruction 0 holds, if it is
		    // not an ENTER there is no body to walk.
		    index_t ent = decl(st, mod_decl, md.ent);
		    int body_n  = (instr(st, ent, op) == OP_ENTER)
			          ? instr(st, ent, e.num) : 0;
		    ctx.filt = filt; ctx.nf = nf;
		    ctx.cmask = cmask; ctx.bmask = bmask; ctx.smask = smask;
		    ctx.scope = NULL;    // inside the block: no further narrowing
		    ctx.flush = mod_flush; ctx.flush_arg = &mp;
		    list_rules(st, &ctx, ent + 1, ent + 1 + body_n, 1, 0);
		    // `#end` only if the wrapper went out: still pending means the
		    // whole module was filtered away. Unfiltered it was flushed at
		    // the `#module`, so an empty module still lists as a block.
		    if (!mp.pending) {
			list_column(0, seg, 0);
			print_decl(DECL_END);
			list_eol();
		    }
		    mp.pending = 0;
		}
		cur_mod = 0;
		mod_decl = 0;
	    }
	    continue;
	}
	if (scope) {
	    if (!(cur_mod && csp_str_eq(st, cur_mod, scope, strlen(scope))))
		continue;                // only this module's members
	}
	// The implicit State variable -- the global one and the per-object copy
	// inside every module -- is runtime machinery, not something the user
	// wrote. A source listing that shows it cannot be pasted back: the
	// declaration would collide with the one the runtime makes itself. Its
	// VALUE is /state's business, which is where to look for it.
	if (state_is_state_var(st, i))
	    continue;
	
	npos = d.name; // decl(st, i, name);
	// A #local has NO name here on purpose -- it lives in the define buffer
	// until #end and lists as $N -- so the nameless skip below would drop
	// its declaration line entirely.
	if (!csp_is_local(st, MAKE_INDEX(0, i)) &&
	    ((npos == 0) || (csp_str_len(st, npos) == 0)))
	    continue;                // no / empty name
	if (nf && !is_fvar(ix, 2, filt, nf))
	    continue;
	mod_flush(st, &mp);        // this member is printing: the wrapper first
	list_column(0, seg, 0);
	if (cur_mod) {
	    csp_print_blank(); csp_print_blank();
	}
	switch (d.type) {
	case DECL_STATES: {
	    // One block, up to CSP_STATES_PER_DECL names, listed as the single
	    // `#states a b c` line it was written as. print_decl_and_name would
	    // show only the first -- `npos` above is slot 0, which is DECL_COMMON's
	    // name and therefore just the block's first state.
	    // csp_decl_t sb = csp_get_decl(st, i);
	    int k, shown = 0;
	    // INIT/NORMAL/FAILSAFE are runtime machinery, like the implicit State
	    // variable filtered above: a listing that shows them cannot be pasted
	    // back, because the runtime declares them itself. They occupy the
	    // first block, so a block with nothing above FAILSAFE prints nothing.
	    for (k = 0; k < CSP_STATES_PER_DECL; k++) {
		sindex_t np = csp_states_name(&d, k);
		if (np == 0)
		    continue;
		if (lookup_state_pos(st, np) <= STATE_FAILSAFE)
		    continue;
		if (!shown) { print_decl(d.type); shown = 1; }
		else csp_print_blank();
		csp_print_str_at(st, np);
	    }
	    if (shown)
		list_eol();
	    break;
	}
	case DECL_VARIABLE: {
	    // `A[3]` lists with its length back on, or it pastes back a scalar
	    // and the elements above it are gone. The tail elements themselves
	    // are skipped further up -- they are declarations, not source lines.
	    uint16_t alen = csp_array_len(st, i);
	    // A #local lists as `#local`, and its FORMULA lists as the rule it
	    // compiled to -- which is where the formula actually lives. Printing
	    // `#variable` would paste back a variable, and the rule below would
	    // then be a plain assignment: same values, one cycle later at every
	    // step of a chain.
	    if (d.local) {
		csp_print_char('#');
		csp_print_rostr(ros_local);
		csp_print_blank();
		// $N, matching what the rules below call it.
		csp_print_char('$');
		csp_print_uint((uvalue_t)csp_local_number(st, (index_t)i));
	    }
	    else
		print_decl_and_name(st, d.type, cur_mod, npos);
	    if (alen > 1) {
		csp_print_char('[');
		csp_print_uint(alen);
		csp_print_char(']');
	    }
	    csp_print_char(':');
	    csp_print_uint(GET_RES(d.res));
	    csp_print_blank();
	    csp_print_rostr(csp_fmt_vtype(d.vt));
	    // A #local has no init value to show -- what defines it is its
	    // FORMULA, and that lists as the rule it compiled to, further down.
	    // Printing `= 0` here would be a lie: the 0 is just what the leaf
	    // starts at, and pasting it back would bind the local to the constant
	    // 0 and then refuse the real formula. See TODO: the listing should put
	    // the formula on this line and suppress that rule.
	    if (d.local)
		;
	    // list the declaration's init value, not the live state (like #constant
	    // below); reading a value here would touch leaf storage /list must not.
	    else if (!d.bound) {
		csp_print_lit(" = ");
		list_value(st, d.vt, d.va.init);
	    }
	    else {
		// bind <buffer>[<lo>..<hi>] -- a bit-field view, so there is no
		// init value to show; without this the bind vanished from /list
		// and the output could not be pasted back.
		//
		// The endian goes before `bind`, where the parser wants it, and
		// for the same reason as on a #field: `big` picks MSB-first bit
		// numbering, so dropping it changes which bits the pasted
		// declaration views.
		if (d.ca.endian != E_NATIVE) {
		    csp_print_blank();
		    csp_print_rostr(csp_fmt_endian(d.ca.endian));
		}
		csp_print_lit(" bind ");
		csp_print_str_at(st, decl_name_pos(st, d.ca.id));
		csp_print_char('[');
		csp_print_uint(d.ca.bit);
		csp_print_lit("..");
		csp_print_uint(d.ca.bit + d.ca.len);
		csp_print_char(']');
	    }
	    list_eol();
	    break;
	}
	case DECL_CONSTANT: {
	    // An array of constants lists as `#constant CT[3]:32 = { a, b, c }`.
	    // Each element is its own declaration holding its own value, so the
	    // list is rebuilt by walking them -- and the head alone would paste
	    // back as a scalar with the other elements gone.
	    uint16_t alen = csp_array_len(st, i);
	    // A #param is a DECL_CONSTANT with `local` set and has to list back
	    // as `#param` -- pasted back as `#constant` it would fold, and every
	    // rule reading it would bake in whatever the value happened to be.
	    if (d.local) {
		csp_print_char('#');
		csp_print_rostr(ros_param);
		csp_print_blank();
		list_name(st, cur_mod, npos);
	    }
	    else
		print_decl_and_name(st, d.type, cur_mod, npos);
	    if (alen > 1) {
		csp_print_char('[');
		csp_print_uint(alen);
		csp_print_char(']');
	    }
	    csp_print_char(':');
	    csp_print_uint(GET_RES(d.res));
	    csp_print_blank();
	    csp_print_rostr(csp_fmt_vtype(decl(st,i,vt)));
	    csp_print_lit(" = ");
	    if (alen > 1) {
		uint16_t e;
		csp_print_lit("{ ");
		for (e = 0; e < alen; e++) {
		    if (e) csp_print_lit(", ");
		    list_value(st, decl(st,i,vt), decl(st,i+e,cn.init));
		}
		csp_print_lit(" }");
	    }
	    else
		list_value(st, decl(st,i,vt), decl(st,i,cn.init));
	    list_eol();
	    break;
	}
	case DECL_OBJECT:
	    csp_print_char('#');
	    csp_print_str_at(st, decl_name_pos(st, decl(st,i,mq.mx)));
	    csp_print_blank();
	    csp_print_str_at(st, npos);
	    list_eol();
	    break;
	case DECL_TIMER:
	    print_decl_and_name(st, d.type, cur_mod, npos);
	    csp_print_blank();
	    csp_print_uint(decl(st,i,tm.period));
	    // `= 1` is part of the declaration, not decoration: it is what starts
	    // the timer at boot. Dropped from the listing, a program copied back
	    // out of a board came home with a timer that never runs.
	    if (decl(st,i,tm.init))
		csp_print_lit(" = 1");
	    list_eol();
	    break;
	case DECL_DIGITAL:
	    print_decl_and_name(st, d.type, cur_mod, npos);
	    list_array_len(st, i);
	    csp_print_blank();
	    csp_print_rostr(csp_fmt_pindir(decl(st,i,dir)));
	    if (d.di.pullup) {
		csp_print_blank();		
		csp_print_rostr(ros_pullup);
	    }
	    else if (d.di.pulldown) {
		csp_print_blank();
		csp_print_rostr(ros_pulldown);
	    }
	    csp_print_blank();  // port:pin (needed to mod/rewire)
	    list_pin_spec(st, i, 1);
	    list_eol();
	    break;
	case DECL_ANALOG:
	    print_decl_and_name(st, d.type, cur_mod, npos);
	    list_array_len(st, i);
	    csp_print_char(':');              // :width (res stored as bits-1)
	    csp_print_uint(GET_RES(d.an.res));
	    csp_print_blank();
	    csp_print_rostr(csp_fmt_pindir(d.dir));
	    // The type, whenever it is not the default. An #analog is SIGNED
	    // unless it says otherwise, so leaving `unsigned` out of the listing
	    // is not a cosmetic omission: the line pastes back as a signed one
	    // and every reading above half scale comes home negative.
	    if (CSP_MASK(d.vt,TYPE_BITS) != V_INTEGER) {
		csp_print_blank();
		csp_print_rostr(csp_fmt_vtype(d.vt));
	    }
	    if (d.an.pwm) {
		csp_print_blank();
		csp_print_rostr(ros_pwm);
	    }
	    csp_print_blank();              // port:pin
	    list_pin_spec(st, i, 0);
	    list_eol();
	    break;
	case DECL_BUFFER:
	    // #buffer <name>:<size> <dir> [can 0x<id>]. Size is BYTES (bf.nbytes)
	    // -- see csp_parse_buffer.
	    print_decl_and_name(st, d.type, cur_mod, npos);
	    csp_print_char(':');
	    csp_print_uint(d.bf.nbytes);
	    if (decl(st,i,dir)) {
		csp_print_blank();
		csp_print_rostr(csp_fmt_pindir(d.dir));
	    }
	    if (decl(st,i,bf.transport) == TR_CAN) {
		csp_print_lit(" can ");   // csp_print_hex emits the 0x itself
		csp_print_hex((uvalue_t)decl(st, d.bf.id, cn.init).i);
	    }
	    list_eol();
	    break;
	case DECL_FIELD:
	    // #field <name>:<width> <dir> <type> <frame>[<lo>..<hi>].ca.id is the
	    // #buffer decl the field is a view into, so the frame is named, not
	    // repeated as a raw id.
	    print_decl_and_name(st, d.type, cur_mod, npos);
	    csp_print_char(':');
	    csp_print_uint(d.ca.len+1);
	    // Only a REAL direction. A field over a plain RAM buffer has none, and
	    // csp_fmt_pindir(0) spells that "none" -- which is not a word the
	    // parser knows, so the listing did not paste back at all. #buffer above
	    // already guards its own direction the same way.
	    if (d.dir) {
		csp_print_blank();
		csp_print_rostr(csp_fmt_pindir(d.dir));
	    }
	    csp_print_blank();
	    csp_print_rostr(csp_fmt_vtype(d.vt));
	    // ENDIAN, and it is not cosmetic: `big` selects MSB-first bit
	    // numbering, so `big F[9]` and `F[9]` name DIFFERENT physical bits.
	    // Left out, two fields that read different bits listed identically,
	    // and the listing pasted back as the wrong one.
	    if (d.ca.endian != E_NATIVE) {
		csp_print_blank();
		csp_print_rostr(csp_fmt_endian(d.ca.endian));
	    }
	    csp_print_blank();
	    csp_print_str_at(st, decl_name_pos(st, d.ca.id));
	    csp_print_char('[');
	    csp_print_uint(d.ca.bit);
	    csp_print_lit("..");
	    csp_print_uint(d.ca.bit + d.ca.len);
	    csp_print_char(']');
	    list_eol();
	    break;
	default:
	    list_eol();
	    break;
	}
    }

    // Rules at the top level. A module's rules are NOT listed here -- they were
    // emitted inside their own `#module ... #end` block above, which is the only
    // arrangement that can be pasted back as source. The walk still counts them,
    // so rule numbers stay absolute and agree with #disable.
    ctx.filt = filt; ctx.nf = nf;
    ctx.cmask = cmask; ctx.bmask = bmask; ctx.scope = scope; ctx.smask = smask;
    ctx.flush = NULL; ctx.flush_arg = NULL;   // top level: no wrapper to hold back
    list_rules(st, &ctx, 0, (int)st->ps.nn, 0, 1);
    st->list_nstate = 0;
    return CSP_CMD_OK;
}

// --- /state rendering ------------------------------------------------------
// Fixed columns: NAME  DIR  KIND  PIN  = VALUE. Only committed (DIN) values are
// shown -- the DOUT shadow is an internal half of the transaction model, not
// something a user reading their program's state should have to decode.
// The DIR column doubles as the timer's running/stopped, and the PIN column as
// its period/remaining -- a timer has neither a direction nor a pin, so it costs
// no extra columns.
#define STATE_W_NAME 13
#define STATE_W_DIR   8
#define STATE_W_KIND  9
#define STATE_W_PIN   8

// Pad a column whose contents were printed by something other than
// csp_print_just -- a name walked out of the string table, a port/pin pair.
// The column helpers themselves are csp_print_just / csp_print_rojust.
static void state_pad(int printed, int w)
{
    while (printed++ < w) csp_print_blank();
}

static int state_udigits(uvalue_t v)
{
    int n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

// Object-qualified name, as the state dump writes it: "p0.State", or the bare
// name for a global.
static void state_name(csp_rt_t* st, index_t ix)
{
    // OBJ(ix) is a one-bit selector; the object number comes from the context
    // the caller bound (csp_ctx_set), exactly as it does at runtime.
    int m = OBJ(ix) ? st->cur : 0;
    int n = 0;
    if (m != 0) {
	index_t ox = csp_object_decl(st, m);
	csp_print_str_at(st, decl_name_pos(st, ox));
	csp_print_char('.');
	n = decl_name_len(st, ox) + 1;
    }
    csp_print_str_at(st, decl_name_pos(st, ix));
    state_pad(n + decl_name_len(st, ix), STATE_W_NAME);
}

// Print the declared state numbered `v`; 0 if no state has that number.
static int state_print_state(csp_rt_t* st, ivalue_t v)
{
    sindex_t np = state_name_pos(st, (int)v);
    if (np == 0)
	return 0;
    csp_print_str_at(st, np);
    return 1;
}

// One leaf row. ix is the object-qualified leaf, di its declaration index.
// A #local gets NONE: /state shows the machine's state, and a
// local is a formula recomputed from that state every cycle -- listing it is
// listing an intermediate result. csp.h's DECL_HEADER note has always said a
// local "never reaches /state"; until now it did, under a name nothing outside
// the module is even allowed to use.
NOINLINE static void state_row(csp_rt_t* st, index_t ix, int di)
{
    decl_t t = decl(st, di, type);
    int is_state;

    if (csp_is_local(st, MAKE_INDEX(0, di)))
	return;

    state_name(st, ix);

    if (t == DECL_TIMER) {
	value_t* v = csp_dio_slot(st, ix, DIN);
	uint32_t left = 0;
	if (v->t.running) {
	    // t0 lives in the slot right after the timer
	    index_t tx = ix + 1;   // same object: the selector rides along
	    uint32_t dt = csp_time_ms() - csp_dio_slot(st, tx, DIN)->u;
	    left = (dt >= v->t.period) ? 0 : (v->t.period - dt);
	}
	if (v->t.running)
	    csp_print_rojust(ros_running, LJUST, STATE_W_DIR);
	else
	    csp_print_rojust(ros_stopped, LJUST, STATE_W_DIR);
	// csp_print_just(v->t.running ? "running" : "stopped", LJUST, STATE_W_DIR);
	csp_print_rojust(ros_timer, LJUST, STATE_W_KIND);
	csp_print_uint(v->t.period);          // period/remaining, in the pin column
	if (v->t.running) {
	    csp_print_char('/');
	    csp_print_uint(left);
	}
	if (v->t.fired)
	    csp_print_lit("  FIRED");
	list_eol();
	return;
    }

    // A buffer is not a pin. Falling through to the digital/analog branch read
    // port/pin/dir off a union arm that holds a heap offset and a transport, so
    // /state printed "analog 13:68" -- a pin pair assembled out of the frame's
    // own bytes. What a buffer HAS is a direction, a transport id and a length,
    // so those are what the columns carry: id/dlc where a timer shows
    // period/remaining, and the bytes themselves as the value.
    if ((t == DECL_BUFFER) || (t == DECL_FIELD)) {
	csp_view_t* vw = csp_view(st, ix);
	csp_buf_t*  b  = &st->buf[vw->buf];
	int n = 0;
	// Direction belongs to the BUFFER: a field is a window into it and cannot
	// be read one way while the frame goes the other.
	csp_print_rojust(csp_fmt_pindir(b->dir), LJUST, STATE_W_DIR);
	csp_print_rojust((t == DECL_BUFFER) ? ros_buffer : ros_field,
			 LJUST, STATE_W_KIND);
	if (t == DECL_FIELD) {
	    // The bit window, named the same way /list writes it. The parent frame
	    // is not repeated -- /list has it, and the column is 8 wide.
	    index_t lo = decl(st, di, ca.bit);
	    index_t hi = lo + decl(st, di, ca.len);
	    csp_print_char('[');
	    csp_print_uint(lo);
	    csp_print_lit("..");
	    csp_print_uint(hi);
	    csp_print_char(']');
	    n = 4 + state_udigits(lo) + state_udigits(hi);
	}
	else if (b->transport == TR_CAN) {
	    n  = csp_print_hex(b->xref);
	    csp_print_char('/');
	    csp_print_uint(b->dlc);
	    n += 1 + state_udigits(b->dlc);
	}
	state_pad(n, STATE_W_PIN);
	csp_print_lit("= ");
	if (t == DECL_FIELD)
	    csp_print_value(st, decl(st, di, vt), csp_value(st, ix));
	else {
	    // The frame itself, byte by byte. A buffer has no scalar value to
	    // print: csp_value would hand back the first sizeof(value_t) bytes and
	    // call it a number, which is how Tx and TxSeq came to show the same
	    // thing. Committed side (DIN), like every other row.
	    const uint8_t* p = st->heap[DIN] + b->hp;
	    uint16_t k;
	    for (k = 0; k < b->nbytes; k++) {
		if (k) csp_print_blank();
		csp_print_hex2(p[k]);
	    }
	    // Pending traffic, in the same place a timer says FIRED: RX means a
	    // frame landed this cycle, TX that one goes out at the end of it.
	    if (b->flags & BUF_F_RX)
		csp_print_lit("  RX");
	    if (b->flags & (BUF_F_DIRTY|BUF_F_TX))
		csp_print_lit("  TX");
	}
	list_eol();
	return;
    }

    is_state = state_is_state_var(st, di);
    // A #param prints like a variable: it has no pin, no direction, and its slot
    // holds a plain value. Falling through to the device branch read that value
    // as a dvalue_t and printed a port and a pin out of the number itself.
    if ((t == DECL_VARIABLE) || (t == DECL_CONSTANT)) {
	csp_print_just("", LJUST, STATE_W_DIR);
	if (is_state)
	    csp_print_rojust(ros_state, LJUST, STATE_W_KIND);
	else
	    csp_print_rojust((t == DECL_CONSTANT) ? ros_param : ros_var,
			     LJUST, STATE_W_KIND);
	csp_print_just("", LJUST, STATE_W_PIN);
    }
    else {   // digital / analog
	// port/pin/dir live in the VALUE slot, not the declaration: an object's
	// init list (P.pin=0, P.pin=1,...) writes them per instance, so reading
	// the decl would print the template's pin for every object.
	value_t* v = csp_dio_slot(st, ix, DIN);
	int port, pin, dir;
	if (t == DECL_DIGITAL) { port = v->d.port; pin = v->d.pin; dir = v->d.dir; }
	else                   { port = v->a.port; pin = v->a.pin; dir = v->a.dir; }
	csp_print_rojust(csp_fmt_pindir(dir), LJUST, STATE_W_DIR);
	if (t == DECL_DIGITAL)
	    csp_print_rojust(ros_digital, LJUST, STATE_W_KIND);
	else
	    csp_print_rojust(ros_analog, LJUST, STATE_W_KIND);
	csp_print_uint(port); csp_print_char(':'); csp_print_uint(pin);
	state_pad(state_udigits(port) + 1 + state_udigits(pin), STATE_W_PIN);
    }
    csp_print_lit("= ");
    if (!is_state || !state_print_state(st, csp_value(st, ix).i))
	csp_print_value(st, decl(st,di,vt), csp_value(st, ix));
    list_eol();
}

typedef enum {
    F_NONE     = 0x0000,
    F_IN       = DIR_IN,  // 0x01
    F_OUT      = DIR_OUT, // 0x02,
    F_VARIABLE = 0x0010,
    F_CONSTANT = 0x0020, 
    F_DIGITAL  = 0x0040,
    F_ANALOG   = 0x0080,
    F_TIMER    = 0x0100,
    F_BUFFER   = 0x0200,
    F_FIELD      = 0x0400,
    F_ANY_CAT  = 0x8000,
    F_ALL      = 0xffff,
} filter_flag_t;

typedef struct {
    rostring_t key;
    filter_flag_t flags;
} filt_entry_t;

const filt_entry_t filt_table[] RODATA = {
    { ros_all,       F_ALL },
    { ros_timer,     F_TIMER|F_ANY_CAT },
    { ros_timers,    F_TIMER|F_ANY_CAT },
    { ros_var,       F_VARIABLE|F_ANY_CAT },
    { ros_variable,  F_VARIABLE|F_ANY_CAT },
    { ros_variables, F_VARIABLE|F_ANY_CAT },
    { ros_digital,   F_DIGITAL|F_ANY_CAT },
    { ros_analog,    F_ANALOG|F_ANY_CAT },
    { ros_buffer,    F_BUFFER|F_ANY_CAT },
    { ros_field,     F_FIELD|F_ANY_CAT },
    { ros_input,     F_IN|F_DIGITAL|F_ANALOG|F_BUFFER|F_ANY_CAT },
    { ros_in,        F_IN|F_DIGITAL|F_ANALOG|F_BUFFER|F_ANY_CAT },
    { ros_output,    F_OUT|F_DIGITAL|F_ANALOG|F_BUFFER|F_ANY_CAT },
    { ros_out,       F_OUT|F_DIGITAL|F_ANALOG|F_BUFFER|F_ANY_CAT },
    { ros_undefined, 0 }
};

// Does declaration `di` pass the /state filters? Shared by the global and the
// per-object pass. A named filter matches on the DECLARATION, so `/state State`
// shows the global State and every object's State alike.
static int state_want(csp_rt_t* st, int i,
		      uint16_t f_flags, const index_t* named, int nnamed)
{
    decl_t t = decl(st, i, type);
    uint16_t f_io;
    
    // A #param is a DECL_CONSTANT with `local` set, and it belongs here: its
    // whole point is that the value can differ from what the source says, which
    // is exactly what /state is for. A plain constant still does not -- its
    // value is in the listing and nowhere else.
    if ((t != DECL_VARIABLE) &&
	(t != DECL_DIGITAL) &&
	(t != DECL_ANALOG) &&
	(t != DECL_BUFFER) &&
	(t != DECL_FIELD) &&	
	(t != DECL_TIMER) &&
	!((t == DECL_CONSTANT) && decl(st, i, local)))
	return 0;
    if (decl_name_empty(st, MAKE_INDEX(0, i)))
	return 0;
    // The mirror image of the listing: a param override has a slot of its own,
    // but nothing reads it -- csp_rt_start copies the value into the param it
    // sets, and THAT is the row with the live value.
    if (csp_param_target(st, i) != BAD_INDEX)
	return 0;
    if (nnamed) {
	int k;
	for (k = 0; k < nnamed; k++)
	    if (INDEX(named[k]) == (index_t)i)
		return 1;
	return 0;
    }
    if (((t == DECL_VARIABLE) && !(f_flags & F_VARIABLE)) ||
	((t == DECL_CONSTANT) && !(f_flags & F_CONSTANT)) ||
	((t == DECL_DIGITAL) && !(f_flags & F_DIGITAL)) ||
	((t == DECL_ANALOG) && !(f_flags & F_ANALOG))   ||
	((t == DECL_TIMER) && !(f_flags & F_TIMER)) ||
	((t == DECL_FIELD) && !(f_flags & F_FIELD)) ||
	((t == DECL_BUFFER) && !(f_flags & F_BUFFER)) )
	return 0;
    f_io = f_flags & (F_IN|F_OUT);
    if (f_io) {
	uint16_t dir = decl(st,i,dir) & f_io;
	if ((t==DECL_DIGITAL) && !dir)
	    return 0;
	if ((t==DECL_ANALOG) && !dir)
	    return 0;
	if ((t==DECL_BUFFER) && !dir)
	    return 0;
    }
    return 1;
}

static int cmd_state(csp_rt_t* st, int argc, char* argv[])
{
    int i, a;
    int in_module = 0;
    index_t named[MAX_ARGV];   // explicit "show just these" decl filters (OR)
    int nnamed = 0;
    uint16_t f_flags = 0;
    
    if (!st->started) {   // -b before /resume: no leaves allocated yet
	csp_print_line("not started -- /resume to allocate and run");
	return CSP_CMD_OK;
    }

    // Tokens are OR'd: category words union, `in`/`out` narrow the direction,
    // bare names pick specific decls. `/state digital analog input` == input.
    for (a = 0; a < argc; a++) {
	const char* w = argv[a];
	int i = 0;
	
	while(i < (sizeof(filt_table)/sizeof(filt_table[0]))) {
	    if (ro_strcmp(w, filt_table[i].key) == 0) {
		f_flags |= filt_table[i].flags;
		break;
	    }
	    i++;
	}
	if (f_flags == 0) {
	    const tstr_t sn = { (char*)w, strlen(w) };
	    index_t ix = csp_lookup_decl(st, &sn);
	    if ((ix != BAD_INDEX) && (nnamed < MAX_ARGV))
		named[nnamed++] = ix;
	    else {
		csp_print_lit("unknown: ");
		csp_print_str(w);
		csp_println();
	    }
	}
    }
    DBG("f_flags = 0x%04x\n", f_flags);
    if ((f_flags & F_IN) && !(f_flags & (F_DIGITAL|F_ANALOG|F_BUFFER)))
	f_flags |= (F_DIGITAL|F_ANALOG|F_BUFFER);
    if ((f_flags & F_OUT) && !(f_flags & (F_DIGITAL|F_ANALOG|F_BUFFER)))
	f_flags |= (F_DIGITAL|F_ANALOG|F_BUFFER);    
    if (!nnamed && !(f_flags & F_ANY_CAT)) // bare /state -> show everything
	f_flags = F_ALL;

    // Status line -- the germ of a future terminal status bar. mode is one of
    // three mutually exclusive run states (paused and live can't both be on).
    csp_print_lit("cycle ");
    csp_print_uint(st->cycle);
    // Measured refresh rate. Blank until the first full second has passed --
    // printing a 0 there would read as "stalled" rather than "not yet known".
    if (st->hz) {
	csp_print_lit("   ");
	csp_print_uint(st->hz);
	csp_print_lit(" Hz");
    }
    csp_print_lit("   latch ");
    if (st->latch)
	csp_print_lit("on");
    else
	csp_print_lit("off");
    csp_print_lit("   ");
    if (st->paused)
	csp_print_lit("paused");
    else if (st->live)
	csp_print_lit("live");
    else
	csp_print_lit("running");
    csp_println();
    csp_println();

    // Two passes so object INSTANCES actually appear: the globals, then each
    // object with its own fields. The old walk indexed MAKE_INDEX(0,i) -- obj 0
    // only -- so it printed the module TEMPLATE once and never p0..p9, while the
    // timers came from st->timer[] (per-object, but named bare "T", so ten
    // indistinguishable rows).
    for (i = 0; i < st->ps.nd; i++) {
	decl_t t = decl(st,i,type);
	if (t == DECL_MODULE) { in_module = 1; continue; }
	if (t == DECL_END)    { in_module = 0; continue; }
	if (in_module)        continue;   // template fields belong to the objects
	if (!state_want(st, i, f_flags, named, nnamed))
	    continue;
	state_row(st, MAKE_INDEX(0, i), i);
    }

    for (i = 0; i < st->ps.nd; i++) {
	index_t mx;
	int dn, base, j, m, shown = 0;
	if (decl(st,i,type) != DECL_OBJECT)
	    continue;
	mx   = decl(st, i, mq.mx);
	m    = decl(st, i, mq.m);
	dn   = decl(st, INDEX(mx), md.n);
	base = INDEX(mx) + 1;
	// Bind the instance for the whole row block: an encoded index carries a
	// selector, not an object number, so a member only resolves to THIS
	// object's storage while the context points at it. Listing runs outside
	// any rule, so there is no OP_NEW/OP_SETO to have done it.
	csp_ctx_set(st, m);
	for (j = 0; j < dn; j++) {
	    int dj = base + j;
	    decl_t t = decl(st, dj, type);
	    if (state_want(st, dj, f_flags, named, nnamed)) {
		if (!shown) {           // header only once, and only if non-empty
		    csp_println();
		    csp_print_char('#');
		    csp_print_str_at(st, decl_name_pos(st, MAKE_INDEX(0, i)));
		    csp_print_blank();
		    csp_print_str_at(st, decl_name_pos(st, mx));
		    csp_println();
		    shown = 1;
		}
		state_row(st, MAKE_INDEX(CURRENT, dj), dj);
	    }
	    if (t == DECL_TIMER)   // a timer owns the next slot too (its t0)
		j++;
	}
	csp_ctx_reset(st);
    }
    return CSP_CMD_OK;
}

// /settings -- what the unit is configured to, as opposed to what the source
// says. One row per stored entry.
//
// An ORPHAN (the path no longer names anything) is shown, not hidden and not
// dropped: the next firmware may reintroduce the name and a calibration is
// expensive to recreate, but a store that silently accumulates entries nobody
// can account for is how you end up mistrusting the whole mechanism.
static int cmd_settings(csp_rt_t* st, int argc, char* argv[])
{
    csp_setting_t s;
    int n;

    (void)argc; (void)argv;
    for (n = 0; csp_settings_get(st, n, &s); n++) {
	int status = csp_settings_status(st, &s);
	int k;

	for (k = 0; k < (int)s.plen; k++)
	    csp_print_char(s.path[k]);
	if (s.part != PART_VAL) {
	    csp_print_char('.');
	    csp_print_rostr(csp_part_name((csp_part_t)s.part));
	}
	csp_print_lit(" = ");
	if (s.vt == V_STRING) {
	    csp_print_char('"');
	    for (k = 0; k < (int)s.slen; k++)
		csp_print_char(s.str[k]);
	    csp_print_char('"');
	}
	else if (s.vt == V_UNSIGNED)
	    csp_print_uint(s.val.u);
	else
	    csp_print_int(s.val.i);
	if (status == CSP_SET_ORPHAN)
	    csp_print_lit("   // orphan");
	else if (status == CSP_SET_REFUSED)
	    csp_print_lit("   // not applied: width or type moved");
	csp_println();
    }
    if (n == 0)
	csp_print_line("no settings");
    else {
	csp_print_uint((uvalue_t)st->set_used);
	csp_print_lit(" of ");
	csp_print_uint((uvalue_t)CSP_SETTINGS_BYTES);
	// Not a ternary: csp_print_line builds a RODATA array from the literal,
	// so the text has to be one at the call site.
	if (st->set_dirty)
	    csp_print_line(" bytes, UNSAVED");
	else
	    csp_print_line(" bytes");
    }
    return CSP_CMD_OK;
}

static int cmd_reset(csp_rt_t* st, int argc, char* argv[])
{
    (void)argv;
    (void)argc;
    csp_rebuild(st);
    csp_setup(st);
    // Leave FAILSAFE. The sticky guard in OP_STI stops a RULE from bouncing the
    // device out of its safe configuration, which is exactly what it is for --
    // but the operator at the console is not a rule, and /reset is the escape
    // hatch that guard's own comment promises. Without this the only ways back
    // were /clear, which throws the program away, and a power cycle.
    // Both slots directly, not csp_set_value: a normal store lands in the
    // shadow copy and the guard above sits on the path that would commit it, so
    // the write would be politely ignored -- which is what the guard is FOR.
    // Same shape as csp_output_timer clearing running/fired in both halves.
    {
	value_t* iptr;
	value_t* optr;
	// gsx, not cs.sx: /reset means the GLOBAL State. cs.sx is a parse-time
	// cursor that points at a module's own State between #module and #end,
	// so a /reset typed mid-definition used to poke that module's State
	// instead -- the same trap csp_rt_t.gsx exists for.
	if (csp_dio_slots(st, st->gsx, &iptr, &optr) == 0)
	    iptr->i = optr->i = STATE_INIT;
    }
    csp_print_line("Reset");
    return CSP_CMD_OK;
}

// /pause -- freeze execution. The driver runs no cycle while paused, so you can
// inspect (/state, /list, /memory) and add rules/declarations. Edits are staged
// and integrated by /resume, keeping the running values until then.
static int cmd_pause(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    st->paused = 1;
    st->live = 0;
    csp_print_line("Paused (execution stopped; edit/inspect, then /resume)");
    return CSP_CMD_OK;
}

// /live -- freeze the rules but keep I/O running: inputs keep sampling (watch
// sensors with /state) and outputs keep being driven (> Led=1 lights the pin) so
// you can poke at the hardware while the program logic stands still. /resume goes
// back to running the rules.
static int cmd_live(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (!st->started) {
	csp_print_line("not started -- /resume first");
	return CSP_CMD_OK;
    }
    st->live = 1;
    st->paused = 0;
    csp_print_line("Live (rules frozen, I/O running -- poke away; /resume to run)");
    return CSP_CMD_OK;
}

// /resume -- continue execution. If the program was edited while paused, do one
// full rebuild first: the graph (reactive), leaf/device setup, and initial
// values -- so rules and declarations added while paused take effect.
static int cmd_resume(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (st->edited) {
	if (csp_rebuild(st) < 0) {   // say so: this used to fail silently
	    csp_print_lit("Cannot resume: ");
	    csp_print_error(st);
	    csp_println();
	    csp_print_line("(the code pool must hold the program AND its data --"
			   " see /memory, raise -m)");
	    csp_clr_error(st);
	    return CSP_CMD_ERROR;
	}
	csp_setup(st);
	st->edited = 0;
	csp_print_line("Resumed (rebuilt)");
    }
    else {
	csp_print_line("Resumed");
    }
    st->paused = 0;
    st->live = 0;
    return CSP_CMD_OK;
}

// /clear -- drop all RAM patches so the firmware ROM baseline reappears.
// (RAM-added #states are not yet unwound -- see doc/ROM_RAM.md section 4.)
static int cmd_clear(csp_rt_t* st, int argc, char* argv[])
{
    index_t rom_rules;

    (void)argc; (void)argv;
    // Drop the disable bits of the rules that are about to disappear, or a rule
    // added later inherits a disable it never asked for. ROM rules keep theirs:
    // they are numbered 1..rom_rules and survive the clear.
    st->ps.nn   = CSP_BASE_NN(st);
    rom_rules = csp_n_rules(st);
    // bitset_clr expands its index twice -- no side effects in the argument.
    while (rom_rules < MAX_DIS_RULES) {
	bitset_clr(st->dis_rule, rom_rules);
	rom_rules++;
    }
    // Floor at the runtime boundary, not at rom_*: with no firmware image
    // rom_nd is 0 and State lives at decl 0, so a plain truncate dropped the
    // State variable and left the board unable to run any ungated rule.
    st->ps.nd   = CSP_BASE_ND(st);
    st->ps.strp = CSP_BASE_STRP(st);
    st->ps.nq   = 0;
    csp_str_recount(st);        // strp moved: handles must be recounted
    // Every line /undo could have taken back is gone. Marks left behind would
    // point above the cursors and the next /undo would push them back UP into
    // a pool that no longer holds what they described.
    st->undo_n = st->undo_head = 0;
    csp_rebuild(st);
    csp_setup(st);
    csp_print_lit("Cleared RAM patches -- ROM restored");
    // The eeprom copy is untouched by a clear, so an E line is not gone, only
    // unloaded -- say so, because "cleared" reads like it is. The watermark drops
    // to zero either way: it measures what RAM holds, and RAM now holds nothing.
    if (st->ee_nn || st->ee_nd)
	csp_print_lit(" (eeprom copy kept, /load restores it)");
    csp_println();
    st->ee_nd = st->ee_nn = 0;
    return CSP_CMD_OK;
}

// --- /undo -------------------------------------------------------------------
//
// cmd_clear with a NEARER floor. Everything a typed line adds -- instructions,
// declarations, the names it introduced, anything it queued -- grows from a
// bump cursor, so taking the line back is putting the four cursors where they
// stood before it. No deletion, no gap to compact, and the string table is
// reclaimed with the rest because ps.strp is one of the four.
//
// The floor still cannot go below the ROM baseline: those lines are in flash
// and were never ours to take back. The ring simply runs out first.

// Where the cursors stand right now.
void csp_undo_mark(csp_rt_t* st, csp_undo_t* s)
{
    s->nn   = st->ps.nn;
    s->nd   = st->ps.nd;
    s->strp = st->ps.strp;
    s->nq   = st->ps.nq;
}

// Keep the mark ONLY if the line actually appended something.
//
// #disable, a #param override and an assignment to an existing variable all
// edit in place and move no cursor. Pushing for those would make the next
// /undo withdraw some older line the user had stopped thinking about -- an
// undo that takes back a thing you did not just do is worse than none.
void csp_undo_push(csp_rt_t* st, const csp_undo_t* s)
{
#if CSP_UNDO_DEPTH > 0
    if ((st->ps.nn == s->nn) && (st->ps.nd == s->nd) &&
	(st->ps.strp == s->strp) && (st->ps.nq == s->nq))
	return;
    st->undo[st->undo_head] = *s;
    st->undo_head = (uint8_t)((st->undo_head + 1) % CSP_UNDO_DEPTH);
    if (st->undo_n < CSP_UNDO_DEPTH)
	st->undo_n++;
#else
    (void)st; (void)s;
#endif
}

static int cmd_undo(csp_rt_t* st, int argc, char* argv[])
{
#if CSP_UNDO_DEPTH > 0
    csp_undo_t s;
    index_t rules;
    int n = 1;
    int i;

    if (argc >= 1) {
	const char* p = argv[0];
	n = 0;
	while ((*p >= '0') && (*p <= '9'))
	    n = n * 10 + (*p++ - '0');
	if (*p != '\0' || n < 1)
	    return CSP_CMD_ERROR;         // "/undo two" is a typo, not a request
    }

    if (st->undo_n == 0) {
	csp_print_line("Nothing to take back");
	return CSP_CMD_OK;
    }
    // Asking for more than the ring holds takes back what it has and SAYS how
    // many, rather than refusing: the count is the thing being reported.
    if (n > (int)st->undo_n)
	n = (int)st->undo_n;

    // Walk back n marks. The OLDEST of them is where those lines began.
    for (i = 0; i < n; i++) {
	st->undo_head = (uint8_t)((st->undo_head + CSP_UNDO_DEPTH - 1) %
				  CSP_UNDO_DEPTH);
	st->undo_n--;
    }
    s = st->undo[st->undo_head];

    st->ps.nn = s.nn;
    // Same reason as /clear: drop the disable bits of the rules that are about
    // to disappear, or a rule added later inherits a disable it never asked
    // for. Counted AFTER nn moves, so it counts what survives.
    rules = csp_n_rules(st);
    while (rules < MAX_DIS_RULES) {
	bitset_clr(st->dis_rule, rules);
	rules++;
    }
    st->ps.nd   = s.nd;
    st->ps.strp = s.strp;
    st->ps.nq   = s.nq;
    csp_str_recount(st);        // strp rewound: handles must be recounted

    // The EEPROM watermark measures how much of the RAM patch has a saved copy.
    // RAM just shrank past it, so bring it down or /memory reports more backed
    // than exists. The stored image itself is untouched -- /load still brings
    // the withdrawn lines back, which is the escape hatch if the undo was the
    // mistake.
    if (st->ee_nd > st->ps.nd) st->ee_nd = st->ps.nd;
    if (st->ee_nn > st->ps.nn) st->ee_nn = st->ps.nn;

    csp_rebuild(st);
    csp_setup(st);

    csp_print_lit("Took back ");
    csp_print_int(n);
    if (n == 1)
	csp_print_line(" line");
    else
	csp_print_line(" lines");
    return CSP_CMD_OK;
#else
    (void)st; (void)argc; (void)argv;
    csp_print_line("This firmware was built without /undo");
    return CSP_CMD_OK;
#endif
}

// print v right-aligned in a field of width w (v assumed >= 0)
static void mem_int_r(int v, int w)
{
    int n = 1, t = v;
    while (t >= 10) { n++; t /= 10; }
    while (n++ < w) csp_print_blank();
    csp_print_int(v);
}

static void mem_roname(rostring_t name)
{
    csp_print_lit("  ");
    csp_print_rojust(name, LJUST, 9);
}

// --- /save and /load -------------------------------------------------------
// Shared, so a board and the host say exactly the same thing. The backend is
// the platform part (csp_eeprom_* + csp_eeprom_name); the failure text comes
// from csp_print_error, like every other error the runtime reports.
static void cmd_eeprom_failed(csp_rt_t* st)
{
    csp_print_lit("Error: ");
    csp_print_error(st);
    csp_println();
    csp_clr_error(st);
}

static int cmd_save(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (csp_eeprom_save(st) < 0) {
	cmd_eeprom_failed(st);
	return CSP_CMD_ERROR;
    }
    csp_print_lit("Saved to ");
    csp_print_str(csp_eeprom_name());
    csp_print_lit(" (");
    csp_print_uint(st->ps.nd - st->rom_nd);
    csp_print_lit(" RAM decls, ");
    csp_print_uint(st->ps.nn - st->rom_nn);
    csp_print_lit(" RAM instrs, ");
    csp_print_uint((uvalue_t)csp_eeprom_size(st));
    csp_print_line(" bytes)");
    return CSP_CMD_OK;
}

static int cmd_load(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (csp_eeprom_load(st) < 0) {
	cmd_eeprom_failed(st);
	return CSP_CMD_ERROR;
    }
    // A load replaces the whole RAM patch, so the marks describe a pool that no
    // longer exists. Same reasoning as /clear.
    st->undo_n = st->undo_head = 0;
    csp_setup(st);
    csp_print_lit("Loaded from ");
    csp_print_str(csp_eeprom_name());
    csp_print_lit(" (");
    csp_print_uint(st->ps.nd - st->rom_nd);
    csp_print_lit(" RAM decls, ");
    csp_print_uint(st->ps.nn - st->rom_nn);
    csp_print_line(" RAM instrs)");
    return CSP_CMD_OK;
}

// One /memory row: NAME  used  limit [ pct ]. `limit` < 0 means there is no
// ceiling -- the row is sized to whatever the program needs -- and prints "-".
// pct is only meaningful against a real limit.
static void mem_row(rostring_t name, uint32_t used, int32_t limit, int show_pct)
{
    mem_roname(name);
    mem_int_r((int)used, 8);
    if (limit < 0) {
	int k;
	for (k = 1; k < 9; k++) csp_print_blank();
	csp_print_char('-');
    }
    else
	mem_int_r(limit, 9);
    if (show_pct && (limit > 0)) {
	mem_int_r((int)((used * 100) / (uint32_t)limit), 5);
	csp_print_char('%');
    }
}

// Bytes handed out of the middle region. The bump cursor already knows exactly,
// alignment padding included -- no need to re-add up the individual tables and
// hope the sum tracks what was actually allocated.
// A flat "name  value" row for the RAM breakdown (no ceiling, no percent).
static void mem_val(rostring_t name, uint32_t v)
{
    mem_roname(name);
    mem_int_r((int)v, 8);
    csp_println();
}

NOINLINE static uint32_t csp_derived_bytes(csp_rt_t* st)
{
    return (uint32_t)(st->mid - st->mid_base);
}

// What CandySpeak itself costs the board's RAM, computed from our OWN structures
// so it means the same thing on host and target. The backend's ram_used() cannot
// answer this on the host: it measures the whole PROCESS (libc, stdio, the host's
// own oversized arena), so comparing that against a simulated -M is meaningless
// -- it read 338% of a mega for a program that fits. This is arena + state +
// derived. The stack is deliberately NOT in here: it is measured, not computed.
// /memory: what CandySpeak costs and what binds it. Three ceilings matter and
// they are different: the board's RAM, the code pool's byte budget (mem_fits),
// and the EEPROM -- code that will not fit the last cannot be saved, only tested.
// The category table below is counts, not bytes: decl/instr/string live in the
// RAM arrays (ROM stays in flash) so those show the RAM-patch usage; the rest are
// rebuilt whole into RAM arrays, so their counts include any ROM content.
static int cmd_memory(csp_rt_t* st, int argc, char* argv[])
{
    size_t ib = (size_t)(st->ps.nn - st->rom_nn) * sizeof(csp_instr_t);
    size_t db = (size_t)(st->ps.nd - st->rom_nd) * sizeof(csp_decl_t);
    size_t used = ib + db;
    (void)argc; (void)argv;

    // Where the board's RAM went. Since CandySpeak claims all free RAM, a
    // "used / total" line would always read 100% -- useless. What is worth seeing
    // is the OVERHEAD: what everything except your program's code costs, because
    // that is what decides how much is left to grow into, and what a linked
    // library actually costs you. These sum to capacity.
    //   system   Arduino core + libraries + startup + C++ globals (moves when you
    //            link CircuitPlayground, a CAN driver, ...)
    //   struct   the csp_rt_t runtime state
    //   buffers  CandySpeak's derived tables (view/heap/buf/graph/inq/pending/...)
    //   stack    reserved
    //   code     your program: instr + decl
    //   free     spare -- room for a bigger program or more data
    {
	uint32_t cap     = csp_system_ram_capacity();
	uint32_t sys     = csp_system_ram_used();
	uint32_t buffers = csp_derived_bytes(st);
	uint32_t acc     = sys + model_state() + buffers
	                 + st->line.buf_size
			 + CSP_STACK_RESERVE + (uint32_t)used;
	uint32_t freeram = (cap > acc) ? (cap - acc) : 0;
	csp_print_lit("RAM ");
	csp_print_uint((uvalue_t)cap);
	csp_print_line(" total:");
	mem_val(ros_system,  sys);
	mem_val(ros_struct,  model_state());
	mem_roname(ros_buffers); mem_int_r((int)buffers, 8);
	if (!st->started) csp_print_lit("   (allocated on /resume)");
	csp_println();
	// The REPL line buffer. Its own row because it is the one allocation whose
	// size is a user-visible LIMIT -- it says how long a line may be pasted.
	mem_val(ros_line,    st->line.buf_size);
	mem_val(ros_stack,   CSP_STACK_RESERVE);
#ifdef CSP_STACK_WATCH
	// Measured, not reserved: the closest the stack has come to the arena.
	// Small or negative means declarations are being overwritten. Diagnostic
	// build only (the `watch` target); a normal build shows just the reserve.
	mem_roname(ros_margin);
	if (csp_stack_low > 0x7fff)          // host: stack and arena never meet
	    csp_print_just("-", RJUST, 8);
	else
	    mem_int_r((int)csp_stack_low, 8);
	csp_print_lit("  at ");
	csp_print_hex((uvalue_t)(uintptr_t)csp_stack_low_fn);
	csp_println();
#endif
	mem_val(ros_code,    (uint32_t)used);
	mem_val(ros_free,    freeram);
    }

    {
	uint32_t cap  = csp_eeprom_capacity();
	uint32_t need = (uint32_t)csp_eeprom_size(st);
	if (cap == CSP_EEPROM_NONE) {
	    mem_row(ros_EEPROM, need, 0, 0);
	    csp_print_lit("   ");
	    csp_print_lit("(NONE)");
	}
	else if (cap == CSP_EEPROM_UNBOUNDED) {
	    mem_row(ros_EEPROM, need, -1, 0);
	    csp_print_lit("   ");	    
	    csp_print_lit("(OK)");
	}
	else {
	    mem_row(ros_EEPROM, need, (int32_t)cap, 1);
	    csp_print_lit("   ");	    
	    if (need > cap)
		csp_print_lit("(FULL)");
	    else
		csp_print_lit("(OK)");
	}
	csp_println();
    }

    if (st->rom_nd || st->rom_nn || st->rom_strp) {
	csp_println();
	csp_print_lit("  ROM base   ");
	csp_print_int(st->rom_nd);   csp_print_lit(" decl, ");
	csp_print_int(st->rom_nn);   csp_print_lit(" instr, ");
	csp_print_int(st->rom_strp); csp_print_line(" str");
    }

    csp_println();
    mem_row(ros_instr,   st->ps.nn   - st->rom_nn,   MAX_INSTRS, 0);   csp_println();
    mem_row(ros_decl,    st->ps.nd   - st->rom_nd,   MAX_DECLS,  0);   csp_println();
    mem_row(ros_string,  st->ps.strp - st->rom_strp, MAX_STR_BUF, 0);  csp_println();
    // The row above is BYTES of the RAM buffer. This one is the count, and it
    // is the ceiling that actually binds: a decl's name field is NAMEID_BITS
    // wide, so a program gets 512 names however short they are. Shown as the
    // TOTAL, ROM plus RAM, because that is what the 512 covers -- unlike every
    // other row here, which measures the RAM patch against a per-target buffer.
    mem_row(ros_names,   st->ps.nstr, MAX_NAMEIDS, 0);          csp_println();
    // -1 = no fixed ceiling: both tables are sized to the program (csp_estimate)
    // and come out of the arena, so what limits them is the `pool` row below.
    mem_row(ros_objects, st->ps.nq,  -1, 0);                           csp_println();
    mem_row(ros_modules, st->nm,     -1, 0);                           csp_println();
    // Counted from the DECL_STATES blocks; no ceiling of its own -- what binds
    // is the declaration pool, which the rows above already report.
    mem_row(ros_states,  csp_num_states(st), -1, 0);                   csp_println();
    mem_row(ros_io,      st->nio,  -1, 0);                             csp_println();
    mem_row(ros_timers,  st->nt,   -1, 0);                             csp_println();
    mem_row(ros_buffers, st->nbuf, -1, 0);                             csp_println();
    return CSP_CMD_OK;
}

static int cmd_commit(csp_rt_t* st, int argc, char* argv[])
{
    (void)argv;
    csp_commit(st);
    csp_print_line("Committed");
    return CSP_CMD_OK;
}

static int cmd_quit(csp_rt_t* st, int argc, char* argv[])
{
    (void)st; (void)argv;
    return CSP_CMD_QUIT;
}

void csp_cmd_help(void)
{
    cmd_help(NULL, 0, NULL);
}

static int cmd_latch(csp_rt_t* st, int argc, char* argv[])
{
    int latch = 0;
    if ((argc == 1) && (ro_strcmp(argv[0], ros_on) == 0))
	latch = 1;
    else if ((argc == 1) && (ro_strcmp(argv[0], ros_off) == 0))
	latch = 0;
    else
	return CSP_CMD_ERROR;
    csp_set_latch(st, latch);
    return CSP_CMD_OK;
}

// dispatch cmd, note that cmd is written to!
int csp_cmd_dispatch(csp_rt_t* st, char* cmd)
{
    char* ptr = cmd;
    const csp_cmd_t* c;
    int namelen;
    char* argv[MAX_ARGV];
    int argc = 0;
    
    // Skip command name to find args
    while (*ptr && !ISBLANK(*ptr)) ptr++;
    namelen = ptr - cmd;
    while (ISBLANK(*ptr)) ptr++;
    
    while((*ptr) && (argc < MAX_ARGV)) {
	// advance ptr to next arg
	char *arg = ptr;
	while (*ptr && !ISBLANK(*ptr)) ptr++;
	if (*ptr) *ptr++ = '\0';
	while (ISBLANK(*ptr)) ptr++;
	argv[argc++] = arg;
    }
    argv[argc] = NULL;
    for (c = builtin_cmds; c->name; c++) {
	// c->name is in flash: compare and index it segment-aware.
	if ((ro_strncmp(cmd, c->name, namelen) == 0) &&
	    (ro_byte((const uint8_t*)c->name + namelen) == '\0')) {
	    return c->fn(st, argc, argv);
	}
    }
    return CSP_CMD_NOTFOUND;
}

// Process immediate expression (> expr or > var = expr)
static int csp_process_immediate(csp_rt_t* st, char* line)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t* saved_ap;
    rentry_t result;

    if (!st->started) {   // -b before /resume: value slots not allocated yet
	csp_print_line("not started -- /resume to allocate and run");
	return -1;
    }

    if (csp_scan_line(st, line, tv, &num) < 0) {
	csp_print_line("Scan error");
	return -1;
    }
    if (num == 0 || tv[0].t == NEWLINE)
	return 0;

    saved_ap = st->cs->ap;
    st->cs->ap = NULL;
    st->cs->ev = 1; // eval variables during (compile)
    if (!csp_parse_expr(st, tv, &num, &result)) {
	st->cs->ap = saved_ap;
	csp_print_lit("Error: ");
	csp_print_error(st);
	csp_println();
	csp_clr_error(st);
	return -1;
    }
    st->cs->ap = saved_ap;
    st->cs->ev = 0;

    if (result.I)
	csp_print_value(st, result.vt, result.val);
    else if (result.ix != BAD_INDEX) {
	// result.ix may still name an object (`> safe.State`). Nothing was
	// emitted, so there is no OP_SETO to do this -- bind it here instead.
	unsigned m = XOBJ(result.ix);
	uint8_t  save_cur   = st->cur;
	index_t  save_cbase = st->cbase;
	if ((m != XOBJ_GLOBAL) && (m != XOBJ_CURRENT))
	    csp_ctx_set(st, m);
	csp_print_value(st, result.vt,
			csp_value(st, MAKE_INDEX(m ? CURRENT : GLOBAL,
						 XIDX(result.ix))));
	st->cur   = save_cur;
	st->cbase = save_cbase;
    }
    else
	csp_print_lit("NONE");
    csp_println();
    return 0;
}

// Process persistent definition (# declaration or rule)
static int csp_process_persistent(csp_rt_t* st, char* line)
{
    csp_pmark_t pm;
    index_t nd0 = st->ps.nd;

    csp_pstate_save(st, &pm);
    if (csp_parse(st, line) < 0) {
	csp_print_lit("Error: ");
	csp_print_error(st);
	csp_println();
	csp_clr_error(st);
	// Rewind. Inside an unclosed module the whole module goes: otherwise
	// mdef stays set with no #end in sight and every following line is
	// swallowed by a module that can never be closed.
	if (st->cs->mdef != BAD_INDEX) {
	    csp_pstate_restore(st, &st->cs->mod_mark);
	    csp_print_line("Module aborted");
	}
	else
	    csp_pstate_restore(st, &pm);
	return -1;
    }
    // A new declaration grows the leaf space, so once started we must re-run
    // rt_start now to keep view/dset/buf/heap sized to it (a stale, too-small
    // view would be read out of bounds). Values re-init as they always do on a
    // declaration.
    if ((st->ps.nd != nd0) && st->started && !st->paused) {
	// The rebuild is where a declaration that does not FIT shows up: parsing
	// only writes the declaration, and whether the derived tables still go in
	// the arena is not known until they are laid out. So its result has to be
	// checked here -- it used to be discarded, and the line was answered "OK"
	// while the runtime was left with no tables at all.
	if (csp_rebuild(st) < 0) {
	    csp_print_lit("Error: ");
	    csp_print_error(st);
	    csp_println();
	    csp_clr_error(st);
	    // Put the program back the way it was and lay it out again, so the
	    // declaration that did not fit costs nothing: the previous layout is
	    // known to have fitted, so this rebuild is the one that just worked.
	    csp_pstate_restore(st, &pm);
	    csp_rebuild(st);
	    csp_setup(st);
	    return -1;
	}
	csp_setup(st);
    }
    else {
	// Any other add -- rule-only, paused, live, or before /resume. Mark it
	// and let the rebuild happen where it is safe: /resume, or the top of
	// the next cycle. A rule-only add used to skip this entirely, which left
	// the new rule without graph edges: reactively it never fired.
	st->edited = 1;
    }
    csp_print_line("OK");
    return 0;
}

// A bare line is a persistent RULE if it has a top-level assignment '=' or a
// '?' guard, an immediate query otherwise. This is the token-level test
// "any EQ or QUEST" done as a char scan, so csp_process_line needs no token_t
// tv[24] of its own one frame above csp_parse's.
//
// The subtlety a raw '=' scan misses: '=' also appears in ==, <=, >=, != (all
// comparisons, NOT assignment) and those must NOT flag a rule. So '=' counts
// only when it is a LONE '=' -- neither preceded by = < > ! nor followed by =.
// Strings and // comments are skipped so a literal `print("a=b")` is a query.
// RIMP '<-' contains no '=' and no '?', so `ra <- fa` classifies as a query
// exactly as the old EQ/QUEST token test did -- behaviour preserved.
static int line_is_rule(const char* line)
{
    const char* s = line;
    int in_str = 0;

    for (; *s; s++) {
	if (in_str) {
	    if (*s == '\\' && s[1]) s++;      // skip an escaped char
	    else if (*s == '"') in_str = 0;
	    continue;
	}
	if (*s == '"') { in_str = 1; continue; }
	if (*s == '/' && s[1] == '/') break;  // rest is a comment
	if (*s == '?') return 1;              // QUEST -- a guard
	if (*s == '=') {
	    char p = (s > line) ? s[-1] : 0;
	    if (p != '=' && p != '<' && p != '>' && p != '!' && s[1] != '=')
		return 1;                     // a lone '=' -- assignment
	}
    }
    return 0;
}

int csp_process_line(csp_rt_t* st, char* line)
{
    int len;

    // Skip leading whitespace
    while (*line && (*line == ' ' || *line == '\t')) line++;
    if (*line == '\0' || *line == '\n')
	return CSP_CMD_OK;
    // Remove trailing newline
    len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

    // A comment line is source, not a command -- `//` matched the '/' test below
    // and every header line of a pasted .csp file came back as "Unknown command:
    // // This example ...". Pasting a source file is the whole point of the
    // prompt, so a line that is nothing but a comment does nothing, quietly.
    // Trailing comments need no help: the tokenizer already drops them.
    if ((line[0] == '/') && (line[1] == '/'))
	return CSP_CMD_OK;

    if (*line == '/') {
	// Command
	int r = csp_cmd_dispatch(st, line + 1);
	if (r == CSP_CMD_NOTFOUND) {
	    csp_print_lit("Unknown command: ");
	    csp_print_str(line);
	    csp_print_line(" (try /help)");
	}
	return r;
    }
    else if (*line == '#') {
	// Persistent definition. Marked for /undo -- csp_undo_push keeps the
	// mark only if the line actually appended, so `#disable 4` (which edits
	// in place) leaves the history alone.
	csp_undo_t s;
	csp_undo_mark(st, &s);
	csp_process_persistent(st, line);
	csp_undo_push(st, &s);
	return CSP_CMD_OK;
    }
    else if (*line == '>') {
	// Immediate expression
	csp_process_immediate(st, line + 1);
	return CSP_CMD_OK;
    }
    else {
	// Bare line: a persistent rule (stored, captured by /save) if it either
	// assigns (top-level '=') or carries a '?' guard -- e.g. a bare action
	// like `println("hi") ? Idx==1`, which has no '=' at all. A plain
	// expression (a query) has neither and is evaluated once.
	//
	// Classified by line_is_rule (a char scan) rather than a full tokenize:
	// this frame sits directly above csp_parse's own token_t tv[24], and a
	// second tv[24] here was 144 bytes of the deep-path stack for nothing --
	// it was scanned only to look for EQ/QUEST and then thrown away.
	if (line_is_rule(line)) {
	    csp_undo_t s;
	    csp_undo_mark(st, &s);
	    csp_process_persistent(st, line);
	    csp_undo_push(st, &s);
	}
	else
	    csp_process_immediate(st, line);
	return CSP_CMD_OK;
    }
}

// ============================================================
// Line input handling (shared between platforms)
// ============================================================

#endif /* !CSP_EXEC_ONLY */
