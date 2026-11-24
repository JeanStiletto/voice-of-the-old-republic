"""Offline replica of the mod's wall cache + island bridging.

Mirrors engine_area_walls.cpp BuildAreaWallCache (adj=-1 edges, short-edge
skip, cross-room portal filter, same-room dedup) and engine_navgraph.cpp
BridgeIslands (shortest-first cross-island links, 2D wall test).

usage: pathgraph_bridge_check.py <game_root> <module> [<module> ...] [--max M] [--clearance C] [--detail]
Defaults mirror engine_navgraph.h: --max 40, --clearance 0.5.
"""
import math, os, struct, sys

def load_key(root):
    key = open(os.path.join(root, 'chitin.key'), 'rb').read()
    _, _, nbif, nkey, offbif, offkey = struct.unpack_from('<4s4sIIII', key, 0)
    bifs = []
    for i in range(nbif):
        size, nameoff, namelen, drives = struct.unpack_from('<IIHH', key, offbif + i * 12)
        bifs.append(key[nameoff:nameoff + namelen].split(b'\0')[0].decode('latin-1').replace(chr(92), '/'))
    idx = {}
    for i in range(nkey):
        resref, rtype, resid = struct.unpack_from('<16sHI', key, offkey + i * 22)
        idx[(resref.split(b'\0')[0].decode('latin-1').lower(), rtype)] = (bifs[resid >> 20], resid & 0xFFFFF)
    return idx

def bif_read(root, bif, residx):
    with open(os.path.join(root, bif), 'rb') as f:
        hdr = f.read(20)
        _, _, nvar, nfix, offvar = struct.unpack_from('<4s4sIII', hdr, 0)
        f.seek(offvar + residx * 16)
        rid, off, size, rt = struct.unpack_from('<IIII', f.read(16), 0)
        f.seek(off)
        return f.read(size)

def capsule_get(path, resref, rtype):
    if not os.path.exists(path): return None
    d = open(path, 'rb').read()
    if d[:4] == b'RIM ':
        ecount, off_keys = struct.unpack_from('<II', d, 12)
        for i in range(ecount):
            name, t, rid, o, s = struct.unpack_from('<16sIIII', d, off_keys + i * 32)
            if t == rtype and name.split(b'\0')[0].decode('latin-1').lower() == resref:
                return d[o:o + s]
    else:
        _, _, ecount, _, off_keys, off_res = struct.unpack_from('<6I', d, 8)
        for i in range(ecount):
            name, rid, t, _ = struct.unpack_from('<16sIHH', d, off_keys + i * 24)
            if t == rtype and name.split(b'\0')[0].decode('latin-1').lower() == resref:
                o, s = struct.unpack_from('<II', d, off_res + rid * 8)
                return d[o:o + s]
    return None

EXT = {3000: 'lyt', 2016: 'wok', 3003: 'pth', 2017: '2da'}

class Res:
    def __init__(self, root):
        self.root = root
        self.key = load_key(root)
    def get(self, resref, rtype, module=None):
        resref = resref.lower()
        ov = os.path.join(self.root, 'Override', resref + '.' + EXT[rtype])
        if os.path.exists(ov): return open(ov, 'rb').read()
        if module:
            md = os.path.join(self.root, 'modules')
            for fn in (module + '.mod', module + '.rim', module + '_s.rim',
                       module.upper() + '.rim', module.upper() + '_s.rim'):
                d = capsule_get(os.path.join(md, fn), resref, rtype)
                if d is not None: return d
        hit = self.key.get((resref, rtype))
        if hit: return bif_read(self.root, *hit)
        return None

# ---------- GFF (pth) ----------
def gff(d):
    h = struct.unpack_from('<4s4s12I', d, 0)
    so, sc, fo, fc, lo, lc, fdo, fdc, fio, fic, lio, lic = h[2:]
    labels = [d[lo + i * 16:lo + i * 16 + 16].split(b'\0')[0].decode() for i in range(lc)]
    def field(i):
        t, l, v = struct.unpack_from('<III', d, fo + i * 12)
        if t == 8: return labels[l], struct.unpack('<f', struct.pack('<I', v))[0]
        if t == 15:
            off = lio + v; n = struct.unpack_from('<I', d, off)[0]
            return labels[l], [struct_(x) for x in struct.unpack_from('<%dI' % n, d, off + 4)]
        return labels[l], v
    def struct_(i):
        st, di, fcnt = struct.unpack_from('<III', d, so + i * 12)
        idx = [di] if fcnt == 1 else list(struct.unpack_from('<%dI' % fcnt, d, fio + di))
        return dict(field(j) for j in idx)
    return struct_(0)

# ---------- walls ----------
def parse_lyt(text):
    rooms = []
    lines = text.decode('latin-1').splitlines()
    i = 0
    while i < len(lines):
        p = lines[i].split()
        if p and p[0].lower() == 'roomcount':
            for k in range(int(p[1])):
                q = lines[i + 1 + k].split()
                rooms.append((q[0].lower(), float(q[1]), float(q[2]), float(q[3])))
            break
        i += 1
    return rooms

def parse_bwm(d):
    vcount, voff, fcount, foff, moff = struct.unpack_from('<5I', d, 72)
    adjcount, adjoff = struct.unpack_from('<II', d, 112)
    pos = struct.unpack_from('<3f', d, 60)
    verts = [struct.unpack_from('<3f', d, voff + i * 12) for i in range(vcount)]
    faces = [struct.unpack_from('<3I', d, foff + i * 12) for i in range(fcount)]
    mats = [struct.unpack_from('<I', d, moff + i * 4)[0] for i in range(fcount)]
    adj = [struct.unpack_from('<3i', d, adjoff + i * 12) for i in range(adjcount)]
    return pos, verts, faces, mats, adj

MIN_XY_SQ = 2.5e-3
SEAM_EPS_SQ = 1e-4
WALK_TRIS = []

# ---------- ground-truth walk test ----------
CELL = 4.0
def tri_index():
    grid = {}
    for t in WALK_TRIS:
        xs = [p[0] for p in t]; ys = [p[1] for p in t]
        for gx in range(int(math.floor(min(xs) / CELL)), int(math.floor(max(xs) / CELL)) + 1):
            for gy in range(int(math.floor(min(ys) / CELL)), int(math.floor(max(ys) / CELL)) + 1):
                grid.setdefault((gx, gy), []).append(t)
    return grid

def floor_zs(grid, x, y):
    out = []
    for t in grid.get((int(math.floor(x / CELL)), int(math.floor(y / CELL))), ()):
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = t
        den = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        if abs(den) < 1e-12: continue
        l1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / den
        l2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / den
        l3 = 1 - l1 - l2
        if l1 >= -1e-4 and l2 >= -1e-4 and l3 >= -1e-4:
            out.append(l1 * z1 + l2 * z2 + l3 * z3)
    return out

STEP = 0.25
MAX_DZ_PER_STEP = 0.6   # generous: steep walkable ramps pass, cliffs/ledges don't
def line_walkable(grid, p, q):
    L = math.hypot(q[0] - p[0], q[1] - p[1])
    n = max(1, int(L / STEP))
    for z0 in floor_zs(grid, p[0], p[1]):
        z = z0; ok = True
        for i in range(1, n + 1):
            t = i / n
            zs = floor_zs(grid, p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t)
            if not zs: ok = False; break
            zn = min(zs, key=lambda c: abs(c - z))
            if abs(zn - z) > MAX_DZ_PER_STEP: ok = False; break
            z = zn
        if ok: return True
    return False

BODY = 0.35  # half-width check, roughly a creature's personal space
def link_truth(grid, p, q):
    """'walkable' / 'narrow' (centre line ok, body width not) / 'NOT walkable'"""
    if not line_walkable(grid, p, q): return 'NOT walkable'
    L = math.hypot(q[0] - p[0], q[1] - p[1])
    nx, ny = -(q[1] - p[1]) / L * BODY, (q[0] - p[0]) / L * BODY
    for s in (1, -1):
        if not line_walkable(grid, (p[0] + s * nx, p[1] + s * ny), (q[0] + s * nx, q[1] + s * ny)):
            return 'narrow'
    return 'walkable'

def build_walls(res, module, offset_mode):
    lyt = res.get(module, 3000, module)
    if lyt is None: return None, 'no lyt'
    rooms = parse_lyt(lyt)
    walls, alltri = [], []
    for ri, (name, rx, ry, rz) in enumerate(rooms):
        d = res.get(name, 2016, module)
        if d is None: continue
        pos, verts, faces, mats, adj = parse_bwm(d)
        off = (rx, ry, rz) if offset_mode else (0.0, 0.0, 0.0)
        W = [(v[0] + off[0], v[1] + off[1], v[2] + off[2]) for v in verts]
        for f, tri in enumerate(faces):
            if f < len(adj):  # walkable faces come first; adjacency covers only them
                WALK_TRIS.append((W[tri[0]], W[tri[1]], W[tri[2]]))
            for e in range(3):
                A, B = W[tri[e]], W[tri[(e + 1) % 3]]
                alltri.append((ri, A, B))
                # Walkable-floor boundary only. The game's in-memory set also
                # emits some cliff-face (non-walkable) edges we can't reproduce
                # from the file, so this is a SUBSET of the mod's walls: a link
                # rejected here is rejected in game; one passing here may still
                # be rejected in game.
                if f >= len(adj) or adj[f][e] != -1: continue
                if (B[0] - A[0]) ** 2 + (B[1] - A[1]) ** 2 < MIN_XY_SQ: continue
                walls.append((ri, A, B))
    discovered = len(walls)
    def co(p, q): return (p[0]-q[0])**2 + (p[1]-q[1])**2 + (p[2]-q[2])**2 < SEAM_EPS_SQ
    # portal filter (bucketed by rounded endpoint for speed; same rule)
    buckets = {}
    def bk(p): return (round(p[0], 1), round(p[1], 1))
    for g in alltri:
        buckets.setdefault(bk(g[1]), []).append(g)
    kept = []
    for w in walls:
        portal = False
        for key in {bk(w[1]), bk(w[2])}:
            for dx in (-0.1, 0, 0.1):
                for dy in (-0.1, 0, 0.1):
                    for g in buckets.get((round(key[0] + dx, 1), round(key[1] + dy, 1)), ()):
                        if g[0] == w[0]: continue
                        if (co(w[1], g[1]) and co(w[2], g[2])) or (co(w[1], g[2]) and co(w[2], g[1])):
                            portal = True; break
                    if portal: break
                if portal: break
            if portal: break
        if not portal: kept.append(w)
    # same-room dedup
    def xy(p, q): return (p[0]-q[0])**2 + (p[1]-q[1])**2 < SEAM_EPS_SQ
    i = 0
    while i < len(kept):
        j = i + 1
        while j < len(kept):
            a, b = kept[i], kept[j]
            if a[0] == b[0] and ((xy(a[1], b[1]) and xy(a[2], b[2])) or (xy(a[1], b[2]) and xy(a[2], b[1]))) \
               and (co(a[1], b[1]) or co(a[1], b[2]) or co(a[2], b[1]) or co(a[2], b[2])):
                kept[j] = kept[-1]; kept.pop()
            else:
                j += 1
        i += 1
    return kept, (discovered, len(kept), len(rooms))

def crosses(walls, p, q):
    abx, aby = q[0] - p[0], q[1] - p[1]
    if abx * abx + aby * aby < 1e-10: return None
    for w in walls:
        A, B = w[1], w[2]
        cdx, cdy = B[0] - A[0], B[1] - A[1]
        den = abx * cdy - aby * cdx
        if -1e-8 < den < 1e-8: continue
        dx, dy = A[0] - p[0], A[1] - p[1]
        t = (dx * cdy - dy * cdx) / den
        u = (dx * aby - dy * abx) / den
        if 0 <= t <= 1 and 0 <= u <= 1: return w
    return None

CLEAR = 0.5
def blocked(walls, p, q):
    # Mirror of the mod's link test: centre line plus two parallel lines at
    # +-CLEAR (body width), each must not cross a wall.
    if crosses(walls, p, q): return True
    if CLEAR <= 0: return False
    L = math.hypot(q[0] - p[0], q[1] - p[1])
    if L < 1e-6: return False
    nx, ny = -(q[1] - p[1]) / L * CLEAR, (q[0] - p[0]) / L * CLEAR
    for s_ in (1, -1):
        if crosses(walls, (p[0] + s_ * nx, p[1] + s_ * ny), (q[0] + s_ * nx, q[1] + s_ * ny)): return True
    return False

def bridge(pts, adj, walls, maxm):
    n = len(pts); par = list(range(n))
    def f(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for u in range(n):
        for v in adj[u]:
            if v < n: par[f(u)] = f(v)
    before = sum(1 for i in range(n) if f(i) == i)
    cands = []
    for a in range(n):
        for b in range(a + 1, n):
            if f(a) == f(b): continue
            d = math.hypot(pts[a][0] - pts[b][0], pts[a][1] - pts[b][1])
            if d <= maxm: cands.append((d, a, b))
    cands.sort()
    links, rejects, islands = [], 0, before
    for d, a, b in cands:
        if f(a) == f(b): continue
        if blocked(walls, pts[a], pts[b]): rejects += 1; continue
        par[f(a)] = f(b); islands -= 1; links.append((a, b, d))
        if islands <= 1: break
    sizes = {}
    for i in range(n): sizes[f(i)] = sizes.get(f(i), 0) + 1
    return before, islands, links, rejects, sorted(sizes.values(), reverse=True)

def main():
    args = sys.argv[1:]
    root = args.pop(0)
    maxm = 40.0; detail = False; offset = None
    if '--max' in args: i = args.index('--max'); maxm = float(args[i + 1]); del args[i:i + 2]
    if '--clearance' in args:
        i = args.index('--clearance'); globals()['CLEAR'] = float(args[i + 1]); del args[i:i + 2]
    if '--detail' in args: args.remove('--detail'); detail = True
    if '--offset' in args: args.remove('--offset'); offset = True
    if '--nooffset' in args: args.remove('--nooffset'); offset = False
    res = Res(root)
    for m in args:
        m = m.lower()
        pd = res.get(m, 3003, m)
        if pd is None: print('%-8s no pth' % m); continue
        g = gff(pd); P = g.get('Path_Points', [])
        if not P: print('%-8s empty pth' % m); continue
        C = [c['Destination'] for c in g.get('Path_Conections', [])]
        pts = [(p['X'], p['Y']) for p in P]
        adj = [C[p['First_Conection']:p['First_Conection'] + p['Conections']] for p in P]
        del WALK_TRIS[:]
        walls, info = build_walls(res, m, False if offset is None else offset)
        if walls is None: print('%-8s %s' % (m, info)); continue
        before, after, links, rej, sizes = bridge(pts, adj, walls, maxm)
        if before <= 1:
            print('%-8s one network — untouched' % m); continue
        grid = tri_index()
        verdicts = [link_truth(grid, pts[a], pts[b]) for a, b, d in links]
        from collections import Counter
        vc = Counter(verdicts)
        print('%-8s islands %d -> %d  links=%d longest=%.1fm wallRejects=%d | walk test: %s%s'
              % (m, before, after, len(links), max([l[2] for l in links], default=0.0), rej,
                 ', '.join('%s %d' % (k, v) for k, v in sorted(vc.items())),
                 ('  STILL SPLIT, sizes ' + str(sizes[:6])) if after > 1 else ''))
        if detail:
            for (a, b, d), v in zip(links, verdicts):
                print('    link %d (%.1f,%.1f) <-> %d (%.1f,%.1f) %.1fm  %s' % (a, pts[a][0], pts[a][1], b, pts[b][0], pts[b][1], d, v))

main()
