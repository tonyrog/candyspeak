#!/usr/bin/env python3
"""Find RODATA tables that are read as ordinary memory.

On AVR, RODATA is PROGMEM: a separate address space. Indexing such a table
directly -- `err_tab[i]` -- reads the DATA space at a flash address, which is
not the table. The compiler says nothing, the host is unaffected, and the wrong
value looks plausible. Four tables shipped that way (2026-09-09), one of them
the ERROR table, so every error on a mega printed "internal error".

The rule: a RODATA object is read through ro_byte/ro_word/ro_ptr/ro_dword/
ro_memcmp/ro_memcpy, or through a helper that does (rd8/rd16). Anything else is
either a bug or host-only code.

Host-only files are skipped: they have one address space and a plain read there
is correct. Everything the BOARD links is checked.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Compiled into a board image. port/csp_linux.c and port/csp_dump.c are host
# tools -- one address space, so a raw read is right there.
SCAN_DIRS = ["src", "include", "gen"]
SCAN_PORTS = ["csp_avr.c", "csp_arduino.c", "csp_lpcopen.c", "csp_stm32.c",
              "csp_devices.c", "csp_rom.c", "csp_flash_host.c"]

OK_WRAP = r"(?:ro_byte|ro_word|ro_dword|ro_ptr|ro_memcmp|ro_memcpy|ro_instr|" \
          r"ro_decl|ro_header|ro_ref|ro_sect|ro_copy_decl|rd8|rd16|sizeof|DBG)"

DECL = re.compile(r"^\s*(?:static\s+)?(?:const\s+)?[A-Za-z_][\w ]*?"
                  r"\b([A-Za-z_]\w*)\s*\[[^\]]*\]\s*RODATA\b")

def files():
    for d in SCAN_DIRS:
        p = os.path.join(ROOT, d)
        if not os.path.isdir(p):
            continue
        for f in sorted(os.listdir(p)):
            if f.endswith((".c", ".h")):
                yield os.path.join(p, f)
    for f in SCAN_PORTS:
        p = os.path.join(ROOT, "port", f)
        if os.path.exists(p):
            yield p

def strip_debug(lines):
    """Blank out #ifdef DEBUG blocks: those are host dumpers, never built for a
    board. Nesting is not handled -- there is none in this tree, and a wrong
    guess here would HIDE a finding, so it counts depth and gives up loudly."""
    out, depth, dbg = [], 0, None
    for ln in lines:
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b(.*)", ln)
        if m:
            kw, rest = m.group(1), m.group(2)
            if kw in ("if", "ifdef", "ifndef"):
                depth += 1
                if dbg is None and re.search(r"\bDEBUG\b", rest):
                    dbg = depth
            elif kw == "endif":
                if dbg == depth:
                    dbg = None
                depth -= 1
        out.append("" if dbg is not None else ln)
    return out

def main():
    names = {}                      # symbol -> file it was declared in
    body = {}
    for path in files():
        with open(path, errors="replace") as fh:
            lines = strip_debug(fh.read().split("\n"))
        body[path] = lines
        for ln in lines:
            m = DECL.search(ln)
            if m:
                names[m.group(1)] = path

    bad = []
    for path, lines in body.items():
        # The WHOLE file, not line by line: a DBG() or an ro_ accessor may open
        # on one line and take its argument on the next, and a line-local test
        # calls that a finding. Comments are blanked in place so offsets --
        # and therefore line numbers -- still line up.
        text = "\n".join(re.sub(r"//.*", "", ln) for ln in lines)
        starts = [0]
        for ln in text.split("\n"):
            starts.append(starts[-1] + len(ln) + 1)
        def lineno(off):
            lo, hi = 0, len(starts) - 1
            while lo < hi - 1:
                mid = (lo + hi) // 2
                if starts[mid] <= off: lo = mid
                else: hi = mid
            return lo + 1
        for i, ln in enumerate(lines, 1):
            code = re.sub(r"//.*", "", ln)
            for nm in names:
                # A read is `name[` or `name.field` reached by subscript; the
                # DECLARATION itself is not a read.
                for m in re.finditer(r"(?<![\w.>])" + re.escape(nm) + r"\s*\[", code):
                    if "RODATA" in code or code.lstrip().startswith("extern"):
                        continue          # the declaration, not a read
                    before = code[:m.start()]
                    # `&tab[i]` takes an ADDRESS. That is legal in either space
                    # -- what matters is the read, and the read is somewhere
                    # else. It is also how every correct site is written:
                    # ro_ptr(&tab[i]).
                    if re.search(r"&\s*(?:\([^()]*\)\s*)?$", before):
                        continue
                    # Inside an ro_ accessor, or inside DBG() which compiles to
                    # nothing off the host. "Inside" means the call is still
                    # OPEN: more '(' than ')' since it started.
                    # Search backwards through the FILE, not the line.
                    at = starts[i - 1] + m.start()
                    ctx = text[max(0, at - 400):at]
                    open_wrap = False
                    for w in re.finditer(OK_WRAP + r"\s*\(", ctx):
                        tail = ctx[w.end():]
                        if tail.count("(") >= tail.count(")"):
                            open_wrap = True
                            break
                    if open_wrap:
                        continue
                    bad.append((path, i, nm, ln.strip()))

    if not bad:
        print("ro_check: ok -- every RODATA table is read through an ro_ accessor")
        return 0
    for path, i, nm, ln in bad:
        rel = os.path.relpath(path, ROOT)
        print("%s:%d: %s read as ordinary memory" % (rel, i, nm))
        print("    %s" % ln)
    print("\nro_check: %d read(s) of RODATA without an ro_ accessor" % len(bad))
    print("On AVR these read the DATA space at a flash address. See the note at")
    print("csp_format_error in src/csp_rt.c for what that costs.")
    return 1

sys.exit(main())
