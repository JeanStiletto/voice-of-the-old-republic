"""Dump the node tree of a KOTOR (1/2) binary MDL: name, type flags, world
position (translations accumulated, rotations ignored) and, for mesh nodes,
the mesh bounding box in world coordinates (same approximation).
usage: mdl_nodes.py file.mdl [file2.mdl ...]"""
import struct, sys, os

NODE_HAS_MESH = 0x20

def read_model(path):
    d = open(path, 'rb').read()
    base = 12  # offsets are relative to the end of the 12-byte file header
    def u32(o): return struct.unpack_from('<I', d, base + o)[0]
    def u16(o): return struct.unpack_from('<H', d, base + o)[0]
    def f32(o): return struct.unpack_from('<f', d, base + o)[0]
    def cstr(o, n): return d[base+o:base+o+n].split(b'\0')[0].decode('latin-1')
    # geometry header (80 bytes) at 0
    name = cstr(8, 32)
    off_root = u32(40)
    node_count = u32(44)
    # model header follows at 80: classification etc; name-offset array
    # KOTOR model header: +80 class(1) subclass(1) unk(1) fog(1) childcount(4)
    # anim array (offset,count,count) at +88..+100, parent(4) at 100,
    # bbox min/max at 104/116, radius 128, scale 132, supermodel 136..168,
    # offset to head root node 168, unk 172, mdx size 176, mdx offset 180,
    # names array (offset,count,count) at 184..196
    names_off, names_cnt = u32(184), u32(188)
    name_offs = [u32(names_off + 4*i) for i in range(names_cnt)]
    node_names = [cstr(o, 64) for o in name_offs]
    out = []
    def walk(off, parent_pos, depth):
        ntype = u16(off)
        nodenum = u16(off + 4)
        px, py, pz = f32(off + 16), f32(off + 20), f32(off + 24)
        wx, wy, wz = parent_pos[0]+px, parent_pos[1]+py, parent_pos[2]+pz
        child_off, child_cnt = u32(off + 44), u32(off + 48)
        nm = node_names[nodenum] if nodenum < len(node_names) else '?'
        bbox = None
        if ntype & NODE_HAS_MESH:
            m = off + 80  # mesh header directly after the node header
            # K2 mesh header: funcptr(8) faces array(12) bbox min(12) bbox max(12)
            bmin = (f32(m+20), f32(m+24), f32(m+28))
            bmax = (f32(m+32), f32(m+36), f32(m+40))
            bbox = ((wx+bmin[0], wy+bmin[1], wz+bmin[2]), (wx+bmax[0], wy+bmax[1], wz+bmax[2]))
        out.append((depth, nm, ntype, (wx, wy, wz), bbox))
        for i in range(child_cnt):
            walk(u32(child_off + 4*i), (wx, wy, wz), depth + 1)
    walk(off_root, (0.0, 0.0, 0.0), 0)
    return name, out

if __name__ == "__main__":
  for p in sys.argv[1:]:
      name, nodes = read_model(p)
      print(f"=== {os.path.basename(p)} model={name} nodes={len(nodes)}")
      for depth, nm, ntype, pos, bbox in nodes:
            line = f"{'  '*depth}{nm} type=0x{ntype:x} pos=({pos[0]:.1f},{pos[1]:.1f},{pos[2]:.1f})"
            if bbox:
                (a, b) = bbox
                line += f" bbox=[({a[0]:.1f},{a[1]:.1f},{a[2]:.1f})..({b[0]:.1f},{b[1]:.1f},{b[2]:.1f})]"
            print(line)
