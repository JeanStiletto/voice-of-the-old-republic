"""Extract an MSVC RTTI class -> vtable map from a 32-bit PE.

Chain walked:
    TypeDescriptor (.data, holds the ".?AVFoo@@" name)
      <- CompleteObjectLocator (.rdata, field pTypeDescriptor)
        <- vtable[-1] (the COL pointer sits one slot before the vtable)

Prints: vtable VA, COL VA, base-class offset, demangled-ish class name.
"""
import re
import struct
import sys
from collections import defaultdict


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


class PE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = u32(d, 0x3C)
        assert d[pe:pe + 4] == b"PE\0\0", "not a PE"
        n_sect = u16(d, pe + 6)
        opt_size = u16(d, pe + 20)
        opt = pe + 24
        self.image_base = u32(d, opt + 28)
        sect = opt + opt_size
        self.sections = []
        for i in range(n_sect):
            s = sect + i * 40
            name = d[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            self.sections.append({
                "name": name,
                "vaddr": u32(d, s + 12),
                "vsize": u32(d, s + 8),
                "raw": u32(d, s + 20),
                "rawsize": u32(d, s + 16),
            })

    def off_to_rva(self, off):
        for s in self.sections:
            if s["raw"] <= off < s["raw"] + s["rawsize"]:
                return s["vaddr"] + (off - s["raw"])
        return None

    def rva_to_off(self, rva):
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
                o = s["raw"] + (rva - s["vaddr"])
                if o < s["raw"] + s["rawsize"]:
                    return o
        return None

    def section_of_rva(self, rva):
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
                return s["name"]
        return None


def pretty(mangled):
    """.?AVCSWGuiButton@@ -> CSWGuiButton ; nested A@B@@ -> B::A"""
    m = mangled
    if m.startswith(".?AV") or m.startswith(".?AU"):
        m = m[4:]
    m = m.rstrip("@")
    parts = [p for p in m.split("@") if p]
    return "::".join(reversed(parts)) if parts else mangled


def main(path):
    pe = PE(path)
    d = pe.data
    base = pe.image_base

    # 1. Type descriptors. The name is at TD+8, so TD_rva = name_rva - 8.
    td_by_va = {}
    for m in re.finditer(rb"\.\?A[VU][A-Za-z0-9_@?$]{2,200}@@\x00", d):
        name_off = m.start()
        name_rva = pe.off_to_rva(name_off)
        if name_rva is None:
            continue
        td_va = base + name_rva - 8
        td_by_va[td_va] = m.group(0)[:-1].decode("ascii", "replace")

    # 2. Index every 4-byte-aligned dword in the image by value, so the two
    #    back-references below are lookups instead of full rescans.
    dwords = defaultdict(list)
    for s in pe.sections:
        if s["name"] not in (".rdata", ".data", ".text"):
            continue
        start, end = s["raw"], s["raw"] + s["rawsize"]
        for off in range(start, end - 3, 4):
            dwords[u32(d, off)].append(off)

    # 3. COLs: a dword equal to a TD VA, with signature 0 twelve bytes earlier.
    cols = {}
    for td_va, name in td_by_va.items():
        for off in dwords.get(td_va, ()):
            col_off = off - 12
            if col_off < 0:
                continue
            if u32(d, col_off) != 0:          # signature (0 = 32-bit)
                continue
            col_rva = pe.off_to_rva(col_off)
            if col_rva is None:
                continue
            cols[base + col_rva] = (name, u32(d, col_off + 4))  # + base offset

    # 4. vtables: a dword equal to a COL VA; the vtable starts 4 bytes later.
    rows = []
    for col_va, (name, base_off) in cols.items():
        for off in dwords.get(col_va, ()):
            vt_rva = pe.off_to_rva(off + 4)
            if vt_rva is None:
                continue
            # First slot must look like a .text pointer, else it is not a vtable.
            first = u32(d, off + 4)
            if pe.section_of_rva(first - base) != ".text":
                continue
            rows.append((base + vt_rva, col_va, base_off, pretty(name), name))

    rows.sort(key=lambda r: (r[3], r[2]))
    print(f"image_base=0x{base:X}  type_descriptors={len(td_by_va)}  "
          f"COLs={len(cols)}  vtables={len(rows)}")
    print("vtable_va,col_va,base_offset,class,mangled")
    for vt, col, boff, nice, mang in rows:
        print(f"0x{vt:08X},0x{col:08X},{boff},{nice},{mang}")


if __name__ == "__main__":
    main(sys.argv[1])
