"""World-space hazard atlas for a K2 swoop track: obstacle origin (from the
.lyt) + every mesh node's local bbox (from the obstacle's own MDL) = world
bbox. Classifies: pillar (lane-edge wall), bar (low, jumpable), block (tall,
in-lane), other.  usage: hazard_atlas.py <models_dir> <track e.g. 211tel>"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from mdl_nodes import read_model

d, track = sys.argv[1], sys.argv[2].lower()
lyt = open(os.path.join(d, track + '.lyt')).read().splitlines()
origins = {}
sect = None
for ln in lyt:
    s = ln.strip()
    if s.startswith('obstaclecount'): sect = 'obs'; continue
    if s.startswith('trackcount') or s.startswith('roomcount') or s.startswith('doorhookcount'): sect = None; continue
    if sect == 'obs' and s:
        parts = s.split()
        if len(parts) >= 4:
            origins[parts[0].lower()] = tuple(float(v) for v in parts[1:4])
rows = []
for name, (ox, oy, oz) in sorted(origins.items(), key=lambda kv: kv[1][1]):
    p = os.path.join(d, name + '.mdl')
    if not os.path.exists(p):
        rows.append((oy, name, 'NO MODEL', None)); continue
    _, nodes = read_model(p)
    meshes = [(nm, bb) for depth, nm, nt, pos, bb in nodes if bb]
    if not meshes:
        rows.append((oy, name, 'no mesh', None)); continue
    # union of all mesh bboxes, world space
    xs0 = min(bb[0][0] for _, bb in meshes) + ox; xs1 = max(bb[1][0] for _, bb in meshes) + ox
    ys0 = min(bb[0][1] for _, bb in meshes) + oy; ys1 = max(bb[1][1] for _, bb in meshes) + oy
    zs0 = min(bb[0][2] for _, bb in meshes) + oz; zs1 = max(bb[1][2] for _, bb in meshes) + oz
    h = zs1 - zs0
    if xs0 >= 17 or xs1 <= -17: cls = 'pillar'
    elif h <= 7.5: cls = 'BAR(jump)'
    else: cls = 'BLOCK(steer)'
    rows.append((ys0, name, cls, (xs0, xs1, ys0, ys1, zs0, zs1, [nm for nm, _ in meshes])))
n_pillar = sum(1 for r in rows if r[2] == 'pillar')
print(f"{track}: {len(rows)} obstacles, {n_pillar} pillars")
for oy, name, cls, bb in sorted(rows):
    if cls == 'pillar': continue
    if bb:
        xs0, xs1, ys0, ys1, zs0, zs1, names = bb
        print(f"{name:16s} {cls:13s} x[{xs0:7.1f}..{xs1:7.1f}] y[{ys0:7.1f}..{ys1:7.1f}] z[{zs0:5.1f}..{zs1:5.1f}] h={zs1-zs0:4.1f} meshes={names}")
    else:
        print(f"{name:16s} {cls}")
