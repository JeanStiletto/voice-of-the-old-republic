"""Read KOTOR binary MDL animations: per node position keyframes, accumulated
with the geometry rest pose of the parent chain (rotations ignored, same
approximation as tools/re-scripts/mdl_nodes.py).

usage: py mdlanim.py file.mdl            -> summary
       py mdlanim.py file.mdl --keys     -> also every position key
Module use: track(path) -> {anim_name: (length, [(t, (x,y,z)), ...])} for the
deepest animated node."""
import struct, sys, math

def load(path):
    d = open(path, 'rb').read()
    B = 12
    u32 = lambda o: struct.unpack_from('<I', d, B + o)[0]
    u16 = lambda o: struct.unpack_from('<H', d, B + o)[0]
    f32 = lambda o: struct.unpack_from('<f', d, B + o)[0]
    cstr = lambda o, n: d[B + o:B + o + n].split(b'\0')[0].decode('latin-1')
    names_off, names_cnt = u32(184), u32(188)
    names = [cstr(u32(names_off + 4 * i), 64) for i in range(names_cnt)]

    def walk(off, acc, out, depth=0):
        num = u16(off + 4)
        pos = (f32(off + 16), f32(off + 20), f32(off + 24))
        wp = tuple(a + b for a, b in zip(acc, pos))
        ckoff, ckcnt = u32(off + 56), u32(off + 60)
        cdoff = u32(off + 68)
        ctrls = {}
        for i in range(ckcnt):
            k = ckoff + i * 16
            ctype = u32(k); rows = u16(k + 6); tki = u16(k + 8); dti = u16(k + 10); cols = d[B + k + 12]
            times = [f32(cdoff + 4 * (tki + r)) for r in range(rows)]
            ncol = cols & 0x0f
            if cols & 0x10: ncol *= 3  # bezier
            if ctype == 20 and ncol == 2: ncol = 1  # compressed quaternion
            if ctype != 8:
                ctrls[ctype] = (times, [])
                continue
            vals = [[f32(cdoff + 4 * (dti + r * ncol + c)) for c in range(ncol)] for r in range(rows)]
            ctrls[ctype] = (times, vals)
        name = names[num] if num < len(names) else '?%d' % num
        out.append((depth, name, pos, wp, ctrls))
        coff, ccnt = u32(off + 44), u32(off + 48)
        for i in range(ccnt):
            walk(u32(coff + 4 * i), wp, out, depth + 1)
        return out

    geom = walk(u32(40), (0, 0, 0), [])
    rest = {n: (lp, wp) for _, n, lp, wp, _ in geom}
    anims = []
    aoff, acnt = u32(88), u32(92)
    for i in range(acnt):
        a = u32(aoff + 4 * i)
        aname = cstr(a + 8, 32); length = f32(a + 80); root = cstr(a + 88, 32)
        nodes = walk(u32(a + 40), (0, 0, 0), [])
        anims.append((aname, length, root, nodes))
    return geom, rest, anims

def parent_world(rest, geom, name):
    """World rest position of the PARENT of `name` (geometry tree)."""
    stack = []
    for depth, n, lp, wp, _ in geom:
        stack = stack[:depth]
        if n == name:
            return tuple(w - l for w, l in zip(wp, lp))
        stack.append(n)
    return (0, 0, 0)

def track(path):
    geom, rest, anims = load(path)
    out = {}
    for aname, length, root, nodes in anims:
        best = None
        for depth, n, lp, wp, ctrls in nodes:
            if 8 in ctrls and len(ctrls[8][0]) > 1:
                if best is None or depth >= best[0]:
                    best = (depth, n, ctrls[8])
        if not best:
            continue
        depth, n, (times, vals) = best
        base = parent_world(rest, geom, n)
        pts = [(t, (base[0] + v[0], base[1] + v[1], base[2] + v[2])) for t, v in zip(times, vals)]
        out[aname] = (length, n, pts)
    return out

if __name__ == '__main__':
    path = sys.argv[1]
    geom, rest, anims = load(path)
    print('nodes:', ' '.join('%s@(%.1f,%.1f,%.1f)' % (n, *wp) for _, n, _, wp, _ in geom)[:600])
    for aname, length, root, nodes in anims:
        moving = [(n, c) for _, n, _, _, c in nodes if 8 in c and len(c[8][0]) > 1]
        print('anim %-14s len=%.2fs root=%s moving=%s' % (aname, length, root, ','.join(n for n, _ in moving)))
    for aname, (length, n, pts) in track(path).items():
        r = [math.sqrt(p[0] ** 2 + p[1] ** 2 + p[2] ** 2) for _, p in pts]
        print('  track %s node=%s keys=%d dist-from-origin min=%.0f max=%.0f' % (aname, n, len(pts), min(r), max(r)))
        if '--keys' in sys.argv:
            for t, p in pts:
                print('    t=%.2f (%.1f, %.1f, %.1f)' % (t, *p))
