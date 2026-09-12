#!/usr/bin/env python3
"""A declaration or instruction field reached WITHOUT a generated accessor.

utils/layout.terms is the one description of how a decl and an instr are laid
out, and gen/csp_layout.h is the accessors computed from it.  That only stays
true while nothing names a bit-field of csp_decl_t or csp_instr_t directly --
and nothing does today, which is precisely when it is cheap to say so.

Naming one is not a style question.  The layout is the ROM FORMAT: the moment a
site reads `d.type` instead of csp_decl_get_type(&d), gcc's bit-field packer is
back in the definition and the format is whatever the compiler decided again.

The layout tests are exempt.  tests/layout.c compares every accessor against the
bit-field it replaces, which it can only do by naming both; instr_layout.c and
states_layout.c are the older hand-written versions of the same idea.
"""
import re, sys, glob, os

DECL = ("type|vt|name|local|cont|dir|reg|bound|is_mapped|res|"
        "md|mq|va|cn|di|an|ca|bf|rt|tm|em")
INSTR = "op|a|m|mi|i|r|x|in|e|sg|v|n|f|o|ox|em|raw|rest"
EXEMPT = {"tests/layout.c", "tests/instr_layout.c", "tests/states_layout.c"}

def strip_comments(src):
    """Blank out comments, keeping every newline so line numbers survive.

    Without this the guard reports its own documentation: the header explains
    the layout in prose, and prose says `d.cont`.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in src[i:j])); i = j
        else:
            out.append(src[i]); i += 1
    return "".join(out)

def scan(path):
    raw = open(path).read()
    src = strip_comments(raw)
    lines = raw.split("\n")
    hits = []
    # The SIGNATURE comes with the body. A parameter is declared before the
    # brace, so scanning from `{` onward missed every `csp_instr_t ci` that
    # arrived as an argument -- and the guard reported a clean tree while
    # eval_op read the union forty times.
    for fn in re.finditer(r"\n([^\n;{}]*\([^;{}]*\))\s*\n?\{\n(?:.*?)\n\}\n",
                          src, re.S):
        # The span is taken STRAIGHT from the source, signature included, so a
        # match position maps to a line without any arithmetic. Rebuilding the
        # body out of two groups put every reported line number off by one.
        body = src[fn.start(1):fn.end(0)]
        base = src[:fn.start(1)].count("\n")
        for ty, fields in (("csp_decl_t", DECL), ("csp_instr_t", INSTR)):
            decls = set(re.findall(r"\b%s\s*(\*?)\s*([a-z_][a-z0-9_]*)\s*[;=,)]" % ty,
                                   body))
            for star, name in decls:
                op = "->" if star else r"\."
                pat = r"\b%s\s*%s\s*(?:%s)\b" % (name, op, fields)
                for m in re.finditer(pat, body):
                    ln = base + body[:m.start()].count("\n")
                    hits.append((ln + 1, lines[ln].strip(), m.group(0)))
        # A field read straight off the RETURN VALUE never touches a declared
        # variable, so the loop above cannot see it. Two of those hid in csp.h
        # until the union was made opaque and the compiler found them.
        for getter, fields in (("csp_get_decl", DECL), ("csp_get_instr", INSTR)):
            pat = r"%s\s*\([^;]*?\)\s*\.\s*(?:%s)\b" % (getter, fields)
            for m in re.finditer(pat, body):
                ln = base + body[:m.start()].count("\n")
                hits.append((ln + 1, lines[ln].strip(), m.group(0)))
    return hits

def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    os.chdir(root)
    files = (glob.glob("src/*.c") + glob.glob("port/*.c") +
             glob.glob("tests/*.c") + ["include/csp.h"])
    total = 0
    for f in sorted(files):
        if f.replace("\\", "/") in EXEMPT:
            continue
        for ln, text, what in scan(f):
            print("%s:%d: %s" % (f, ln, text[:90]))
            total += 1
    if total == 0:
        print("layout_guard: ok -- every decl/instr field goes through an accessor")
        return 0
    print()
    print("layout_guard: %d field(s) reached without a generated accessor." % total)
    print("Use csp_decl_get_<f>()/csp_instr_get_<f>() -- see utils/layout.terms.")
    return 1

sys.exit(main())
