"""Find the functions that write a class's vtable pointer — i.e. its constructors.

Why this exists: the vtable-slot map reaches every VIRTUAL method of a class,
which is enough to place a field that some virtual happens to touch. It does not
reach a panel's constructor, and the constructor is the function worth having:
it constructs every embedded child control in declaration order, so ONE
decompile yields the whole embedded-member layout of a panel rather than one
field at a time. Panel-internal offsets do not follow any delta rule between the
games (measured: one CSWGuiPortraitCharGen field moves 0xAB8), so each panel
needs its own witness, and the constructor is the cheapest one per field.

A constructor is recognisable without symbols: it stores the class's vtable
address into `[this]`, which the compiler emits as a `MOV r/m32, imm32` carrying
the vtable VA. Scanning .text for that immediate finds the constructor, the
destructor, and nothing much else. This script reports every hit with the
function that contains it, and marks which ones look like a vtable STORE
(`C7 xx ... <imm32>`) versus a bare mention.

Both games work, given the right inputs:
  * KOTOR 2 — pass swkotor2.exe; it is not encrypted, so its bytes are on disk.
  * KOTOR 1 — pass the decrypted image dump from `kdev dump-text`, whose base
    the accompanying .json records (the Steam exe is SteamStub-encrypted).

Usage:
    vtable_xrefs.py <exe-or-dump> <functions.csv> <vtable-va> [<vtable-va> ...]

`functions.csv` is the `addr,size` catalogue (docs/llm-docs/re/k2/k2-functions.csv
for KOTOR 2); it is what turns a hit address into "which function is this".
"""
import bisect
import struct
import sys


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Image:
    """A PE read from disk, or a flat image dump alongside its .json meta."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        if self.data[:2] == b"MZ":
            self._load_pe()
        else:
            self._load_flat(path)

    def _load_pe(self):
        d = self.data
        pe = u32(d, 0x3C)
        nsect = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        base = u32(d, pe + 24 + 28)
        self.spans = []
        for i in range(nsect):
            s = pe + 24 + optsz + i * 40
            name = d[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            va = base + u32(d, s + 12)
            vsize = u32(d, s + 8)
            raw = u32(d, s + 20)
            rawsize = u32(d, s + 16)
            self.spans.append((name, va, min(vsize, rawsize), raw))

    def _load_flat(self, path):
        import json
        import os
        meta_path = os.path.splitext(path)[0] + ".json"
        base = 0x400000
        if os.path.exists(meta_path):
            meta = json.load(open(meta_path))
            base = meta.get("ImageBase") or meta.get("imageBase") or base
        # One span covering the whole dump: offset 0 is the image base.
        self.spans = [(".image", base, len(self.data), 0)]

    def text_spans(self):
        return [s for s in self.spans if s[0] in (".text", ".image")]


def load_functions(path):
    starts, sizes = [], {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("addr"):
            continue
        parts = line.split(",")
        try:
            va = int(parts[0], 16)
        except ValueError:
            continue
        starts.append(va)
        sizes[va] = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0
    starts.sort()
    return starts, sizes


def containing(starts, sizes, va):
    i = bisect.bisect_right(starts, va) - 1
    if i < 0:
        return None
    start = starts[i]
    size = sizes.get(start, 0)
    if size and va >= start + size:
        return None
    return start


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    img = Image(sys.argv[1])
    starts, sizes = load_functions(sys.argv[2])
    targets = [int(a, 16) for a in sys.argv[3:]]

    for vt in targets:
        needle = struct.pack("<I", vt)
        print(f"# vtable {vt:#010x}")
        hits = []
        for name, va, size, raw in img.text_spans():
            blob = img.data[raw:raw + size]
            pos = blob.find(needle)
            while pos != -1:
                site = va + pos
                # A vtable STORE is `C7 /r imm32` — the opcode sits 2 or 3
                # bytes before the immediate depending on the addressing form.
                store = blob[max(0, pos - 3):pos].find(b"\xC7") != -1
                hits.append((site, containing(starts, sizes, site), store))
                pos = blob.find(needle, pos + 1)
        if not hits:
            print("    (no .text reference — vtable never stored inline?)")
        for site, fn, store in hits:
            fns = f"{fn:#010x}" if fn else "?"
            print(f"    {site:#010x}  in {fns}  {'STORE' if store else 'ref'}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
