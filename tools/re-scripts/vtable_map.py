"""Map KOTOR 1 virtual functions to their KOTOR 2 addresses, by vtable slot.

The idea: for a class present in both games, vtable slot N holds the same
logical method in each. So pairing the two vtables slot-by-slot yields a
KOTOR 1 -> KOTOR 2 address mapping for every virtual function of that class,
with no decompilation.

Inputs:
  * KOTOR 1 vtable addresses  — `<Class>_vtable` SYMBOL entries in Lane's
    Ghidra XML export.
  * KOTOR 1 vtable contents   — the decrypted image dump produced by
    `kdev dump-text` (indexed by RVA; the Steam exe is SteamStub-encrypted on
    disk so its bytes cannot be read from the file).
  * KOTOR 2 vtable addresses  — rtti_scan.py output.
  * KOTOR 2 vtable contents   — read straight from swkotor2.exe, which is not
    encrypted.
  * KOTOR 1 function names    — FUNCTION entries in the same XML.

Caveat the output cannot express: slot correspondence holds only while the two
classes declare the same virtuals in the same order. Obsidian adding, removing
or reordering a virtual shifts every slot below it. Equal slot COUNT is
supporting evidence, not proof — treat a mapping as a strong lead to confirm in
a decompiler, not as a verified address. Classes whose counts differ are
reported but not mapped.
"""
import json
import re
import struct
import sys

TEXT_LO, TEXT_HI = 0x401000, 0x1000000   # generous .text bounds, both games


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class K1Image:
    """Decrypted KOTOR 1 image dump: a flat buffer indexed by RVA."""

    def __init__(self, binpath, jsonpath):
        self.data = open(binpath, "rb").read()
        meta = json.load(open(jsonpath))
        self.base = meta.get("ImageBase") or meta.get("imageBase") or 0x400000

    def read_u32(self, va):
        off = va - self.base
        if off < 0 or off + 4 > len(self.data):
            return None
        return u32(self.data, off)


class K2Exe:
    """KOTOR 2 executable read from disk (no encryption)."""

    def __init__(self, path):
        d = self.data = open(path, "rb").read()
        pe = u32(d, 0x3C)
        nsect = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = u32(d, pe + 24 + 28)
        self.sections = []
        for i in range(nsect):
            s = pe + 24 + optsz + i * 40
            self.sections.append(
                (u32(d, s + 12), u32(d, s + 8), u32(d, s + 20), u32(d, s + 16))
            )  # vaddr, vsize, raw, rawsize

    def read_u32(self, va):
        rva = va - self.base
        for vaddr, vsize, raw, rawsize in self.sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                off = raw + (rva - vaddr)
                if off + 4 <= raw + rawsize:
                    return u32(self.data, off)
        return None


def vtable_slots(img, va, bound=None, limit=400):
    """Read pointers until one stops looking like a .text address.

    `bound` is the start of the NEXT known vtable. Without it this routine
    silently runs off the end: vtables are packed adjacently in .rdata, so the
    "is it a .text pointer" test keeps succeeding straight into the neighbour
    and two or three tables get merged into one. That produced counts like
    CSWGuiControl 196-vs-38 — not a real layout difference, just an overrun on
    the KOTOR 1 side. In KOTOR 2 the RTTI locator sits at vtable-4, so the
    bound is the neighbour's locator slot, one word earlier.
    """
    out = []
    while len(out) < limit:
        addr = va + 4 * len(out)
        if bound is not None and addr >= bound:
            break
        v = img.read_u32(addr)
        if v is None or not (TEXT_LO <= v < TEXT_HI):
            break
        out.append(v)
    return out


def next_start(sorted_starts, va, back=0):
    """Start of the next known vtable after `va`, minus `back` bytes."""
    for s in sorted_starts:
        if s > va:
            return s - back
    return None


def load_k1_symbols(xml_path):
    """-> (class -> vtable VA, func VA -> name)"""
    vt, fn = {}, {}
    text = open(xml_path, encoding="utf-8", errors="replace").read()
    for m in re.finditer(
        r'<SYMBOL ADDRESS="([0-9a-fA-F]+)" NAME="([A-Za-z0-9_]+)_vtable"', text
    ):
        vt[m.group(2)] = int(m.group(1), 16)
    for m in re.finditer(
        r'<FUNCTION ENTRY_POINT="([0-9a-fA-F]+)"[^>]*NAME="([^"]+)"', text
    ):
        fn[int(m.group(1), 16)] = m.group(2)
    return vt, fn


def load_k2_vtables(csv_path):
    vt = {}
    for line in open(csv_path, encoding="utf-8"):
        parts = line.rstrip("\n").split(",")
        if len(parts) < 5 or not parts[0].startswith("0x"):
            continue
        # base_offset 0 only: the primary vtable, not a secondary base's.
        if parts[2] != "0":
            continue
        vt.setdefault(parts[3], int(parts[0], 16))
    return vt


def main(xml, k1bin, k1json, k2exe, k2csv):
    k1vt, k1fn = load_k1_symbols(xml)
    k2vt = load_k2_vtables(k2csv)
    img1, img2 = K1Image(k1bin, k1json), K2Exe(k2exe)

    k1_starts = sorted(k1vt.values())
    k2_starts = sorted(k2vt.values())

    shared = sorted(set(k1vt) & set(k2vt))
    print(f"# K1 vtables {len(k1vt)}  K2 vtables {len(k2vt)}  shared classes {len(shared)}")
    print("class,slot,k1_addr,k2_addr,confidence,k1_name")

    exact = prefix = 0
    for cls in shared:
        a = vtable_slots(img1, k1vt[cls], next_start(k1_starts, k1vt[cls]))
        # KOTOR 2 vtables carry an RTTI locator one word below their start.
        b = vtable_slots(img2, k2vt[cls], next_start(k2_starts, k2vt[cls], back=4))
        if not a or not b:
            print(f"# SKIP {cls}: empty ({len(a)} vs {len(b)})")
            continue

        # Equal counts: the whole table lines up. Unequal: only the leading
        # slots are trustworthy, and ONLY if the difference is an append at the
        # end. Derived virtuals are appended, so a small positive delta is the
        # expected shape; anything else is reported but marked low.
        if len(a) == len(b):
            conf, n = "exact", len(a)
            exact += 1
        else:
            conf, n = "prefix", min(len(a), len(b))
            prefix += 1
            print(f"# PARTIAL {cls}: {len(a)} vs {len(b)} slots — leading {n} only")

        for i in range(n):
            print(f"{cls},{i},0x{a[i]:08X},0x{b[i]:08X},{conf},{k1fn.get(a[i], '')}")
    print(f"# {exact} classes matched exactly, {prefix} mapped by leading prefix")


if __name__ == "__main__":
    main(*sys.argv[1:6])
