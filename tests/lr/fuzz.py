#!/usr/bin/env python3
# Mutate example lines (drop, insert, replace tokens) into fuzz/*.csp
import glob, random, re, sys, os
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
lines = []
for f in glob.glob('../tonyrog/candyspeak/examples/*.csp'):
    for l in open(f):
        l = re.sub(r'//.*', '', l).strip()
        if l:
            lines.append(l)
rx = (r'"[^"]*"|\d+\.\d+|0x[0-9a-fA-F]+|\d+|[A-Za-z_$][\w$]*|<<=|>>=|'
      r'==|!=|<-|<=|>=|<<|>>|\|\||&&|\.\.|\S')
pool = [t for l in lines for t in re.findall(rx, l)]
os.makedirs('fuzz', exist_ok=True)
for i in range(n):
    t = re.findall(rx, random.choice(lines))
    for _ in range(random.randint(0, 2)):
        op = random.randrange(3)
        if op == 0 and t:
            del t[random.randrange(len(t))]
        elif op == 1:
            t.insert(random.randrange(len(t) + 1), random.choice(pool))
        elif t:
            t[random.randrange(len(t))] = random.choice(pool)
    open('fuzz/f%04d.csp' % i, 'w').write(' '.join(t) + '\n')
