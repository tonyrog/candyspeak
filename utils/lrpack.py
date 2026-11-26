#!/usr/bin/env python3
# Compact LR tables from bison --xml.
#
# usage: lrpack.py csp.xml tokens.def out.h [--level N] [--stats]
#
# Encoding (all 8 bit, offsets 16 bit):
#   state row:  n, default, [parent], (tok|kind<<7, val) * n
#               kind 0 = shift to state val, 1 = shift-reduce class val
#               default 0 = error, else reduce class default-1
#   goto list:  per nonterminal: n, default, (from, to) * n
#               to < NSTATES is a state, else goto-reduce class to-NSTATES
#   class:      lhs, len (rules with equal lhs, len and action merge)
#
# Levels, each adds to the previous:
#   0 one row per state, every rule its own class
#   1 identical rows shared
#   2 rules merged into action classes
#   3 rows inherit from a parent row (delta encoding)
#   4 parents may have parents (chains)
import collections
import re
import sys
import xml.etree.ElementTree as ET


def load(xmlfile, tokfile):
    r = ET.parse(xmlfile).getroot()
    g = r.find('grammar')
    names = [m.group(1) for m in
             re.finditer(r'X\((\w+),', open(tokfile).read())]
    tcode = {n: i + 1 for i, n in enumerate(names)}
    tcode['$end'] = 0
    rules = {}
    for ru in g.find('rules'):
        n = int(ru.get('number'))
        lhs = ru.find('lhs').text
        rhs = [s.text for s in ru.find('rhs').findall('symbol')]
        rules[n] = (lhs, len(rhs))
    nts = [nt.get('name') for nt in g.find('nonterminals')]
    states = {}
    for s in r.find('automaton').findall('state'):
        a = s.find('actions')
        tr = [(t.get('type'), t.get('symbol'), int(t.get('state')))
              for t in a.find('transitions')]
        rd = [(x.get('symbol'), x.get('rule')) for x in a.find('reductions')
              if x.get('enabled') == 'true']
        states[int(s.get('number'))] = (tr, rd)
    return tcode, rules, nts, states


def build(level, tcode, rules, nts, states, action_of):
    # states whose only action is a default reduction disappear
    only = {}
    for n, (tr, rd) in states.items():
        if not tr and len(rd) == 1 and rd[0][0] == '$default':
            only[n] = rd[0][1]
    real = sorted(n for n in states if n not in only)
    snum = {n: i for i, n in enumerate(real)}

    # reduce classes; 'accept' is class 0
    cls = {'accept': 0}
    ctab = [('$accept', 0, 0)]

    def rclass(rule):
        if rule == 'accept':
            return 0
        lhs, ln = rules[int(rule)]
        key = (lhs, ln, action_of(int(rule))) if level >= 2 else int(rule)
        if key not in cls:
            cls[key] = len(ctab)
            ctab.append((lhs, ln, action_of(int(rule))))
        return cls[key]

    rows = []
    for n in real:
        tr, rd = states[n]
        ents = []
        for kind, sym, to in tr:
            if kind != 'shift':
                continue
            if to in only:
                ents.append((tcode[sym], 1, rclass(only[to])))
            else:
                ents.append((tcode[sym], 0, snum[to]))
        dflt = 0
        for sym, rule in rd:
            assert sym == '$default', 'lookahead reduction'
            dflt = rclass(rule) + 1
        rows.append((dflt, tuple(sorted(ents))))

    gotos = collections.OrderedDict((nt, []) for nt in nts
                                    if nt != '$accept')
    for n in real:
        for kind, sym, to in states[n][0]:
            if kind == 'goto':
                t = len(real) + rclass(only[to]) if to in only else snum[to]
                gotos[sym].append((snum[n], t))
    # renumber classes so the goto-reduce ones come first: a goto target
    # t >= NSTATES then means class t - NSTATES and needs few codes
    ns = len(real)
    gr = sorted({t - ns for l in gotos.values() for _, t in l if t >= ns})
    order = gr + [c for c in range(len(ctab)) if c not in set(gr)]
    perm = {c: i for i, c in enumerate(order)}
    rows = [(perm[d - 1] + 1 if d else 0,
             tuple(sorted((t, k, perm[v] if k else v) for t, k, v in e)))
            for d, e in rows]
    for nt in gotos:
        gotos[nt] = [(f, ns + perm[t - ns] if t >= ns else t)
                     for f, t in gotos[nt]]
    ctab = [ctab[c] for c in order]
    return real, rows, gotos, ctab, perm[0], len(gr)


def encode_rows(level, rows):
    # returns (bytes, per state offset)
    uniq = []
    index = {}
    smap = []
    for r in rows:
        if level >= 1 and r in index:
            smap.append(index[r])
            continue
        index[r] = len(uniq)
        smap.append(len(uniq))
        uniq.append(r)
    parent = [None] * len(uniq)
    own = [set(e) for _, e in uniq]
    if level >= 4:
        # chains: a parent may have a parent; the child stores only what
        # its parent's whole chain lacks. Sets shrink along a chain, so
        # there are no cycles.
        full = [{(t, k, v) for t, k, v in e} for _, e in uniq]
        for i in range(len(uniq)):
            best = None
            for j in range(len(uniq)):
                if j != i and full[j] < full[i] and \
                   (best is None or len(full[j]) > len(full[best])):
                    best = j
            if best is not None and len(full[best]) > 1:
                parent[i] = best
                own[i] = full[i] - full[best]
    elif level >= 3:
        # greedy: inherit from the row that removes the most entries;
        # a parent never has a parent itself (search depth 2)
        order = sorted(range(len(uniq)), key=lambda i: -len(uniq[i][1]))
        is_parent = set()
        for i in order:
            best, gain = None, 1
            ti = {t: (k, v) for t, k, v in uniq[i][1]}
            for j in order:
                if j == i or parent[j] is not None:
                    continue
                tj = {t: (k, v) for t, k, v in uniq[j][1]}
                if any(t not in ti or ti[t] != tj[t] for t in tj):
                    continue
                # every parent entry must be the child's own action
                g = len(tj) - 1     # the parent byte costs one
                if g > gain:
                    best, gain = j, g
            if best is not None and i not in is_parent:
                parent[i] = best
                is_parent.add(best)
                pj = {t for t, _, _ in uniq[best][1]}
                own[i] = {e for e in uniq[i][1] if e[0] not in pj}
    out = []
    offs = []
    for i, (dflt, ents) in enumerate(uniq):
        offs.append(len(out))
        ents = sorted(own[i])
        if level >= 3:
            out += [len(ents), dflt, 255 if parent[i] is None else 0]
        else:
            out += [len(ents), dflt]
        for t, k, v in ents:
            out += [t | (k << 7), v]
    return out, [offs[s] for s in smap], uniq, parent, offs


def main():
    xmlf, tokf, outf = sys.argv[1:4]
    level = 3
    if '--level' in sys.argv:
        level = int(sys.argv[sys.argv.index('--level') + 1])
    tcode, rules, nts, states = load(xmlf, tokf)
    # dummy actions: every rule has the same action
    action_of = lambda r: 0
    real, rows, gotos, ctab, accept, ngr = build(level, tcode, rules, nts,
                                                 states, action_of)
    rbytes, soff, uniq, parent, roffs = encode_rows(level, rows)
    ns = len(real)
    if ns + ngr > 255 or len(ctab) > 255:
        print('level %d does not fit 8 bits: states %d goto-reduce %d '
              'classes %d' % (level, ns, ngr, len(ctab)))
        return
    # the parent byte indexes lr_parent_off, which holds row offsets
    if level >= 3:
        pidx = sorted({p for p in parent if p is not None})
        pslot = {p: i for i, p in enumerate(pidx)}
        for i, (dflt, ents) in enumerate(uniq):
            if parent[i] is not None:
                rbytes[roffs[i] + 2] = pslot[parent[i]]
    gbytes = []
    goff = []
    for nt, lst in gotos.items():
        goff.append(len(gbytes))
        cnt = collections.Counter(t for _, t in lst)
        d = cnt.most_common(1)[0][0] if lst else 0
        ex = [(f, t) for f, t in lst if t != d]
        gbytes += [len(ex), d]
        for f, t in ex:
            gbytes += [f, t]
    ntidx = {nt: i for i, nt in enumerate(gotos)}
    lhs = [ntidx.get(l, 0) for l, _, _ in ctab]
    ln = [n for _, n, _ in ctab]
    sizes = collections.OrderedDict()
    sizes['rows'] = len(rbytes)
    sizes['row_off'] = 2 * ns
    if level >= 3:
        sizes['parent_off'] = 2 * len(pidx)
    sizes['gotos'] = len(gbytes)
    sizes['goto_off'] = 2 * len(goff)
    sizes['classes'] = 2 * len(ctab)
    total = sum(sizes.values())
    with open(outf, 'w') as f:
        w = f.write
        w('// generated by lrpack.py --level %d\n' % level)
        w('#define LR_NSTATES %d\n#define LR_NCLASS %d\n' % (ns, len(ctab)))
        w('#define LR_PARENT %d\n' % (1 if level >= 3 else 0))
        w('#define LR_ACCEPT %d\n' % accept)

        def arr(t, name, data):
            w('static const %s %s[] RO = {' % (t, name))
            for i, v in enumerate(data):
                w(('\n    ' if i % 12 == 0 else ' ') + '%d,' % v)
            w('\n};\n')
        arr('uint8_t', 'lr_rows', rbytes)
        arr('uint16_t', 'lr_row_off', soff)
        if level >= 3:
            arr('uint16_t', 'lr_parent_off', [roffs[p] for p in pidx])
        arr('uint8_t', 'lr_gotos', gbytes)
        arr('uint16_t', 'lr_goto_off', goff)
        arr('uint8_t', 'lr_lhs', lhs)
        arr('uint8_t', 'lr_len', ln)
    if '--stats' in sys.argv:
        print('level %d: states %d rows %d classes %d (goto-reduce %d) '
              'entries %d total %d'
              % (level, ns, len(uniq), len(ctab), ngr,
                 sum(len(e) for _, e in uniq), total),
              ' '.join('%s=%d' % kv for kv in sizes.items()))


if __name__ == '__main__':
    main()
