# A crossed pty pair, in place of socat: two slaves for the two programs and a
# pump that copies each master to the other.
import os, pty, select, sys, time
m1, s1 = pty.openpty()
m2, s2 = pty.openpty()
open(sys.argv[1], 'w').write("%s %s\n" % (os.ttyname(s1), os.ttyname(s2)))
end = time.time() + float(sys.argv[2])
while time.time() < end:
    r, _, _ = select.select([m1, m2], [], [], 0.2)
    for fd in r:
        try: d = os.read(fd, 256)
        except OSError: d = b''
        if d: os.write(m2 if fd == m1 else m1, d)
