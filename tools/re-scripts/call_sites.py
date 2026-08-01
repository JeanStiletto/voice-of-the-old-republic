"""List call sites of a function with a pre-call disassembly window.

The caller-side complement to string_xref_stores.py: when a callee is known
(e.g. a Load/Save method), the interesting fact is what ECX the caller loads
before the CALL — `lea ecx,[esi+0x1b770]` IS the field offset being hunted.

Usage:
    call_sites.py <exe> <target-addr> [--funcs functions.csv] [--before N]
"""
import argparse
import bisect
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("capstone not installed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("target", type=lambda x: int(x, 0))
    ap.add_argument("--funcs")
    ap.add_argument("--before", type=int, default=48)
    ap.add_argument("--after", type=int, default=8)
    ap.add_argument("--limit", type=int, default=20)
    args = ap.parse_args()

    data = open(args.binary, "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    opt = struct.unpack_from("<H", data, e + 20)[0]
    base = struct.unpack_from("<I", data, e + 24 + 28)[0]
    tva = tfoff = tfsize = None
    for i in range(nsec):
        s = e + 24 + opt + i * 40
        name = data[s:s + 8].rstrip(b"\0").decode()
        vsize, va, fsize, foff = struct.unpack_from("<IIII", data, s + 8)
        if name == ".text":
            tva, tfoff, tfsize = base + va, foff, fsize
    blob = data[tfoff:tfoff + tfsize]

    funcs = []
    if args.funcs:
        for line in open(args.funcs):
            try:
                a, sz = line.strip().split(",")
                funcs.append((int(a, 16), int(sz)))
            except ValueError:
                continue
        funcs.sort()
    starts = [f[0] for f in funcs]

    def fn_of(va):
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and funcs[i][0] <= va < funcs[i][0] + funcs[i][1]:
            return funcs[i][0]
        return None

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    shown = 0
    i = 0
    while True:
        i = blob.find(b"\xe8", i)
        if i < 0 or shown >= args.limit:
            break
        rel = struct.unpack_from("<i", blob, i + 1)[0] if i + 5 <= len(blob) else None
        if rel is not None and tva + i + 5 + rel == args.target:
            site = tva + i
            fn = fn_of(site)
            print(f"\n=== call @0x{site:08x}" +
                  (f"  (in 0x{fn:08x})" if fn else "") + " ===")
            w0 = max(0, i - args.before)
            # walk instruction stream from window start; resync by trying
            # a few start offsets until the call decodes on-boundary
            for adj in range(0, 16):
                insns = list(md.disasm(blob[w0 + adj:i + 5 + args.after],
                                       tva + w0 + adj))
                if any(x.address == site for x in insns):
                    for x in insns:
                        mark = " <== call" if x.address == site else ""
                        print(f"  0x{x.address:08x}: {x.mnemonic} {x.op_str}{mark}")
                    break
            shown += 1
        i += 1
    if shown == 0:
        print("no call sites found")


if __name__ == "__main__":
    main()
