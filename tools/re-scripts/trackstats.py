"""Per-track range/direction stats relative to the turret at the origin.
Position keys are treated as offsets from the node's rest position (KotOR
animation convention), sampled at 0.1 s with linear interpolation."""
import sys, math, os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mdlanim.py')).read().split("if __name__")[0]
ns = {}; exec(src, ns)

def samples(path):
    geom, rest, anims = ns['load'](path)
    for aname, length, root, nodes in anims:
        best = None
        for depth, n, lp, wp, c in nodes:
            if 8 in c and len(c[8][0]) > 1 and (best is None or depth >= best[0]):
                best = (depth, n, c[8])
        if not best: continue
        _, n, (times, vals) = best
        rp = rest[n][1]
        pts = [(t, tuple(rp[i] + v[i] for i in range(3))) for t, v in zip(times, vals)]
        out = []
        t = 0.0; k = 0
        while t <= pts[-1][0]:
            while k + 1 < len(pts) - 1 and pts[k + 1][0] < t: k += 1
            (t0, a), (t1, b) = pts[k], pts[min(k + 1, len(pts) - 1)]
            f = 0 if t1 == t0 else max(0, min(1, (t - t0) / (t1 - t0)))
            out.append((t, tuple(a[i] + (b[i] - a[i]) * f for i in range(3))))
            t += 0.1
        return length, out
    return 0, []

def stats(path, near=(100, 200, 300)):
    length, s = samples(path)
    if not s: return None
    d = [math.sqrt(sum(c * c for c in p)) for _, p in s]
    az = [math.degrees(math.atan2(p[0], p[1])) % 360 for _, p in s]
    el = [math.degrees(math.atan2(p[2], math.hypot(p[0], p[1]))) for _, p in s]
    sp = [math.dist(s[i][1], s[i + 1][1]) / 0.1 for i in range(len(s) - 1)]
    ds = sorted(d)
    frac = {n: sum(1 for x in d if x <= n) / len(d) for n in near}
    return dict(loop=length, dmin=ds[0], dmed=ds[len(ds) // 2], dmax=ds[-1],
                elmin=min(el), elmax=max(el), spmed=sorted(sp)[len(sp) // 2], spmax=max(sp),
                azspan=len({int(a // 30) for a in az}) * 30, frac=frac, start=s[0][1])

for p in sys.argv[1:]:
    r = stats(p)
    if not r: print(os.path.basename(p), 'static'); continue
    print('%-16s loop=%5.1fs dist min/med/max=%4.0f/%4.0f/%4.0f  speed med/max=%3.0f/%3.0f u/s  elev %+.0f..%+.0f deg  az coverage=%d deg  time<=100/200/300: %.0f%%/%.0f%%/%.0f%%  start=(%.0f,%.0f,%.0f)' % (
        os.path.basename(p), r['loop'], r['dmin'], r['dmed'], r['dmax'], r['spmed'], r['spmax'], r['elmin'], r['elmax'], r['azspan'],
        100 * r['frac'][100], 100 * r['frac'][200], 100 * r['frac'][300], *r['start']))
