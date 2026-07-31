"""Find methods invoked on a known singleton, by scanning for the call idiom.

For a non-polymorphic class like CSWGuiManager there is no vtable to map
slot-by-slot, so the RTTI route does not reach it. But its methods are still
reached the same way everywhere: load the singleton into ECX, then CALL.

    8B 0D <addr>    mov ecx, dword ptr [singleton]
    ...             (a few bytes of argument setup)
    E8 <rel32>      call <method>

Scanning .text for that pattern yields the set of methods called on the
singleton, plus how many distinct sites call each — and call-site count is a
useful signal on its own, since the per-frame tick and the input dispatcher are
called from very few places while common accessors are called from many.

Usage:
    find_thiscall_targets.py <exe> <singleton-va> [max-gap]
"""
import struct
import sys
from collections import defaultdict


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class PE:
    def __init__(self, path):
        d = self.data = open(path, "rb").read()
        pe = u32(d, 0x3C)
        nsect = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = u32(d, pe + 24 + 28)
        self.sections = []
        for i in range(nsect):
            s = pe + 24 + optsz + i * 40
            self.sections.append({
                "name": d[s:s + 8].rstrip(b"\0").decode("ascii", "replace"),
                "vaddr": u32(d, s + 12), "vsize": u32(d, s + 8),
                "raw": u32(d, s + 20), "rawsize": u32(d, s + 16),
            })

    def text(self):
        for s in self.sections:
            if s["name"] == ".text":
                return s
        return self.sections[0]


class ImageDump:
    """`kdev dump-text` output: a flat buffer indexed by RVA, not file offset.

    Needed for KOTOR 1, whose Steam executable is SteamStub-encrypted on disk —
    its .text reads as ciphertext, so the only source of real bytes is a dump of
    the running process. Presented with the same interface as PE so the scan
    does not care which it got, which is what lets the tool be validated against
    KOTOR 1's known answers before being trusted on KOTOR 2.
    """

    def __init__(self, binpath, jsonpath):
        import json
        self.data = open(binpath, "rb").read()
        meta = json.load(open(jsonpath))
        self.base = meta["imageBase"]
        self._sections = meta["sections"]

    def text(self):
        for s in self._sections:
            if s["name"] == ".text":
                # raw offset == virtualAddress, because the buffer is RVA-indexed
                return {"name": ".text", "vaddr": s["virtualAddress"],
                        "raw": s["virtualAddress"], "rawsize": s["virtualSize"]}
        raise SystemExit("no .text in dump")


def main(exe, singleton_hex, max_gap="24"):
    pe = ImageDump(exe, exe[:-4] + ".json") if exe.endswith(".bin") else PE(exe)
    singleton = int(singleton_hex, 16)
    gap = int(max_gap)
    t = pe.text()
    blob = pe.data[t["raw"]: t["raw"] + t["rawsize"]]
    va0 = pe.base + t["vaddr"]

    # Any `mov r32, [singleton]`, not just ECX. Matching ECX alone missed both
    # of the functions this tool was written to find: KOTOR 1's HandleInputEvent
    # and Update are reached via `mov eax,[singleton]` (opcode A1) with the move
    # into ECX happening afterwards. The `this` register is an artefact of how
    # the compiler scheduled the load, not of the calling convention.
    packed = struct.pack("<I", singleton)
    needles = [b"\xa1" + packed]                    # mov eax, [singleton]
    needles += [bytes((0x8B, modrm)) + packed       # mov r32, [singleton]
                for modrm in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D)]

    targets = defaultdict(list)
    sites = []
    for n in needles:
        start = 0
        while True:
            i = blob.find(n, start)
            if i < 0:
                break
            sites.append((i, len(n)))
            start = i + 1

    for i, nlen in sites:
        # Look ahead a short window for a call rel32.
        j = i + nlen
        end = min(j + gap, len(blob) - 5)
        while j < end:
            if blob[j] == 0xE8:
                rel = struct.unpack_from("<i", blob, j + 1)[0]
                tgt = va0 + j + 5 + rel
                if pe.base <= tgt < pe.base + 0x1000000:
                    targets[tgt].append(va0 + i)
                break
            j += 1

    print(f"# {exe}")
    print(f"# singleton 0x{singleton:08X}  load sites {sum(len(v) for v in targets.values())}"
          f"  distinct targets {len(targets)}")
    print("target,call_sites,first_site")
    for tgt, sites in sorted(targets.items(), key=lambda kv: -len(kv[1])):
        print(f"0x{tgt:08X},{len(sites)},0x{sites[0]:08X}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
