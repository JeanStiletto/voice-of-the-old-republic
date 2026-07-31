"""Recover KOTOR 2's CGuiInGame slot table: which panel class lives at which
offset inside CGuiInGame.

Why this exists
---------------
`IdentifyPanel` classifies an in-game panel by comparing it against the pointers
CGuiInGame stores for each sub-screen. That table is a description of ONE
binary's layout, and it is what makes equipment / inventory / journal / map
classify at all — without it every in-game panel falls through to vtable
identification and reads as Unknown.

The table cannot be derived from the KOTOR 1 one. Measured: six slots are
identical between the games and CSWGuiInGameMessages moved from +0x1c to +0x78,
so a blanket "Same" is wrong in a way no count would reveal.

The method, which needs no decompiler:

1. RTTI gives every panel class its vtable (k2-vtables.csv).
2. A constructor is the function that STORES that vtable into [this], found by
   scanning .text for the vtable address as an immediate (vtable_xrefs.py's
   trick, reused here).
3. CGuiInGame's creator calls each constructor in turn and assigns the result
   into its own slot. So: find the CALL, follow the returned pointer through
   the unoptimised build's stack temporaries, and read the offset off the
   `mov [this + off], reg` that files it away.

Step 3 is only tractable because KOTOR 2's GUI code is compiled UNOPTIMISED:
every intermediate lands in a named stack slot, so the dataflow is a short
chain of `mov` instructions rather than register colouring.

Usage:
    k2_slot_table.py <exe> <functions.csv> <vtables.csv> <creator-va> [<creator-va> ...]

Prints `class,offset` per resolved slot, plus the calls it could not follow.
Pass more than one creator VA when the sub-screens are built in several places
(KOTOR 2 has a second, smaller creator that rebuilds a subset).
"""
import bisect
import re
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("k2_slot_table.py: needs capstone (pip install capstone)")


def load_pe(path):
    with open(path, "rb") as f:
        data = f.read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    optsz = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    base = struct.unpack_from("<I", data, e_lfanew + 24 + 28)[0]
    secoff = e_lfanew + 24 + optsz
    secs = []
    for i in range(nsec):
        o = secoff + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode()
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, max(vsz, rsz), raw))
    return data, base, secs


def va_to_off(va, base, secs):
    rva = va - base
    for _, sva, size, raw in secs:
        if sva <= rva < sva + size:
            return raw + (rva - sva)
    return None


def load_funcs(csv):
    out = []
    for line in open(csv):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        out.append((int(parts[0], 16), int(parts[1])))
    out.sort()
    return out


def load_vtables(csv):
    """vtable VA -> class name.

    Columns are `vtable_va,col_va,base_offset,class,mangled`. Take column 0:
    col_va is the complete-object locator that sits at vtable_va-4, and it is
    vtable_va that an object actually stores, so it is the immediate a
    constructor carries.
    """
    out = {}
    for line in open(csv):
        parts = line.strip().split(",")
        if len(parts) < 4:
            continue
        try:
            vt = int(parts[0], 16)
        except ValueError:
            continue
        out[vt] = parts[3]
    return out


def find_constructors(data, base, secs, vtables):
    """ctor entry VA -> class name, via the vtable-store immediate."""
    funcs = load_funcs.cache if hasattr(load_funcs, "cache") else None
    ctors = {}
    for name, sva, size, raw in secs:
        if name != ".text":
            continue
        blob = data[raw:raw + size]
        for vt, cls in vtables.items():
            imm = struct.pack("<I", vt)
            i = 0
            while True:
                i = blob.find(imm, i)
                if i < 0:
                    break
                va = base + sva + i
                ctors.setdefault(va, cls)
                i += 1
    return ctors


def main():
    exe, fcsv, vcsv = sys.argv[1], sys.argv[2], sys.argv[3]
    creators = [int(a, 16) for a in sys.argv[4:]]
    data, base, secs = load_pe(exe)
    funcs = load_funcs(fcsv)
    starts = [f[0] for f in funcs]
    vtables = load_vtables(vcsv)

    def containing(va):
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and va < funcs[i][0] + funcs[i][1]:
            return funcs[i][0]
        return None

    # ctor-entry VA -> class, by locating each vtable's store site and mapping
    # it to the function that contains it.
    site_class = find_constructors(data, base, secs, vtables)
    ctor_class = {}
    for site, cls in site_class.items():
        fn = containing(site)
        if fn is not None:
            ctor_class.setdefault(fn, cls)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    results, unresolved = {}, []

    for creator in creators:
        size = dict(funcs).get(creator)
        if not size:
            print(f"# creator 0x{creator:08x} not in catalogue; skipped")
            continue
        off = va_to_off(creator, base, secs)
        insns = list(md.disasm(data[off:off + size], creator))

        for idx, ins in enumerate(insns):
            if ins.mnemonic != "call":
                continue
            try:
                tgt = int(ins.op_str, 16)
            except ValueError:
                continue
            cls = ctor_class.get(tgt)
            if not cls:
                continue

            # Unoptimised dataflow: EAX -> stack temp -> (stack temp) -> reg,
            # then `mov [this + off], reg`. Track which stack slots and
            # registers currently alias the constructed object.
            alias = {"eax"}
            slot = None
            for nxt in insns[idx + 1: idx + 40]:
                m = re.match(r"^(dword ptr \[ebp [-+] 0x[0-9a-f]+\]|\w+), "
                             r"(dword ptr \[ebp [-+] 0x[0-9a-f]+\]|\w+)$",
                             nxt.op_str)
                if nxt.mnemonic == "mov" and m:
                    dst, src = m.group(1), m.group(2)
                    # `mov <slot>, 0` is the allocation-failed branch. It
                    # statically follows the call but is the path NOT taken
                    # when the object exists, so it must not clear the alias
                    # — doing so loses every slot in the table.
                    if re.fullmatch(r"0x[0-9a-f]+|\d+", src):
                        continue
                    if src in alias:
                        alias.add(dst)
                    elif dst in alias:
                        alias.discard(dst)   # overwritten with something else
                # The slot store: mov [reg + off], reg-holding-the-object.
                sm = re.match(r"^dword ptr \[(\w+) \+ (0x[0-9a-f]+)\], (\w+)$",
                              nxt.op_str)
                if nxt.mnemonic == "mov" and sm and sm.group(3) in alias:
                    slot = int(sm.group(2), 16)
                    break
                if nxt.mnemonic == "call":
                    break
            if slot is not None:
                results.setdefault(cls, slot)
            else:
                unresolved.append((cls, ins.address))

    for cls, slot in sorted(results.items(), key=lambda kv: kv[1]):
        print(f"{cls},0x{slot:02x}")
    for cls, va in unresolved:
        print(f"# UNRESOLVED {cls} (call at 0x{va:08x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
