"""Caller-tracing census for KOTOR 2 functions with no RTTI route.

Three modes over one linear pass of .text, composable in a single run:

  targets  — for each given function address: every CALL rel32 site, its
             containing function (from the k2-functions.csv boundaries), and a
             short pre-call disassembly window. The window is what exposes
             pushed constant arguments (`push 7` before the Esc-path call is
             how HandleInputEvent was found), and the site COUNT is itself a
             fingerprint: pair it against the KOTOR 1 count for the suspected
             twin before spending any decompile round (SwitchToSWInGameGui
             matched 9-vs-9 with the same 8-trampoline shape).

  --forwarders OFF — scan for `MOV ECX,[reg+OFF]` followed within a few bytes
             by a CALL, and report forwarder → target. With OFF = the offset
             where a known singleton lives (0x40 = CClientExoAppInternal's
             CGuiInGame), every target is a method of that singleton's class.

  --follow ADDR — treat ADDR as an accessor returning an object; after each
             call to it, take the next `MOV ECX,EAX; CALL X` and tally X.
             Every X is a method of the returned class. (Strict pattern —
             unoptimised code that spills EAX to a stack temp first is missed,
             so absence of a method here proves nothing; presence is reliable.)

Usage:
    k2_caller_trace.py <exe> <k2-functions.csv> [0xTARGET ...]
        [--forwarders 0xOFF] [--follow 0xACCESSOR] [--context N]

The exe path is the plain PE on disk (KOTOR 2 is not SteamStub-encrypted).
For KOTOR 1 use find_thiscall_targets.py / the imagedump — this tool assumes
readable file bytes.
"""
import argparse
import bisect
import struct
from collections import defaultdict

import capstone


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("funcs_csv")
    ap.add_argument("targets", nargs="*")
    ap.add_argument("--forwarders", metavar="OFF")
    ap.add_argument("--follow", metavar="ACCESSOR")
    ap.add_argument("--context", type=int, default=6)
    args = ap.parse_args()

    pe = PE(args.exe)
    t = pe.text()
    blob = pe.data[t["raw"]: t["raw"] + t["rawsize"]]
    va0 = pe.base + t["vaddr"]

    funcs = []
    for line in open(args.funcs_csv):
        line = line.strip()
        if line:
            a, s = line.split(",")
            funcs.append((int(a, 16), int(s)))
    funcs.sort()
    starts = [f[0] for f in funcs]
    start_set = set(starts)

    def containing(va):
        i = bisect.bisect_right(starts, va) - 1
        if i < 0:
            return None
        st, sz = funcs[i]
        return st if st <= va < st + sz else None

    wanted = {int(a, 16) for a in args.targets}
    follow = int(args.follow, 16) if args.follow else None

    call_sites = defaultdict(list)   # wanted target -> [site]
    follow_tally = defaultdict(list)  # method -> [accessor site]
    i = 0
    n = len(blob)
    while True:
        i = blob.find(b"\xE8", i)
        if i < 0 or i + 5 > n:
            break
        rel = struct.unpack_from("<i", blob, i + 1)[0]
        tgt = va0 + i + 5 + rel
        if tgt in wanted:
            call_sites[tgt].append(va0 + i)
        if follow is not None and tgt == follow:
            j = i + 5
            if blob[j:j + 2] == b"\x8b\xc8" and blob[j + 2] == 0xE8:
                rel2 = struct.unpack_from("<i", blob, j + 3)[0]
                follow_tally[va0 + j + 2 + 5 + rel2].append(va0 + i)
        i += 1

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def pre_context(site_va, count):
        f = containing(site_va)
        if f is None:
            return ["    <no containing function>"]
        st, sz = funcs[bisect.bisect_left(starts, f)]
        insns = []
        for ins in md.disasm(blob[st - va0: st - va0 + sz], st):
            insns.append(ins)
            if ins.address > site_va:
                break
        idx = next((k for k, ins in enumerate(insns)
                    if ins.address == site_va), None)
        if idx is None:
            return ["    <site not on instruction boundary>"]
        return [f"    {ins.address:08x}  {ins.mnemonic} {ins.op_str}"
                for ins in insns[max(0, idx - count): idx + 1]]

    for tgt in sorted(wanted):
        sites = call_sites.get(tgt, [])
        callers = sorted({containing(s) for s in sites if containing(s)})
        print(f"== 0x{tgt:08X}: {len(sites)} call sites, "
              f"{len(callers)} distinct callers")
        for s in sites:
            c = containing(s)
            print(f"  site 0x{s:08X} in "
                  f"{'0x%08X' % c if c else '?'}")
            for line in pre_context(s, args.context):
                print(line)
        print()

    if args.forwarders:
        off = int(args.forwarders, 16)
        print(f"# forwarders: MOV ECX,[reg+0x{off:x}] ... CALL")
        needles = [bytes((0x8B, m, off)) for m in
                   (0x48, 0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x4F)]
        for nd in needles:
            j = 0
            while True:
                j = blob.find(nd, j)
                if j < 0:
                    break
                for k in range(j + 3, min(j + 14, n - 5)):
                    if blob[k] == 0xE8:
                        rel = struct.unpack_from("<i", blob, k + 1)[0]
                        tgt = va0 + k + 5 + rel
                        if tgt in start_set:
                            f = containing(va0 + j)
                            print(f"forwarder {'0x%08X' % f if f else '?'} "
                                  f"-> target 0x{tgt:08X} "
                                  f"(call at 0x{va0 + k:08X})")
                        break
                j += 3
        print()

    if follow is not None:
        print(f"# methods reached via `CALL 0x{follow:08X}; MOV ECX,EAX; "
              f"CALL X` (strict — misses spill-to-temp callers)")
        print("method,call_sites,first_site")
        for tgt, sites in sorted(follow_tally.items(),
                                 key=lambda kv: -len(kv[1])):
            print(f"0x{tgt:08X},{len(sites)},0x{sites[0]:08X}")


if __name__ == "__main__":
    main()
