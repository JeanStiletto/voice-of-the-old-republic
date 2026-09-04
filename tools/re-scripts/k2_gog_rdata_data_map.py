"""Steam-K2 -> GOG-K2 map for the NON-CODE addresses: .rdata (vtables and
constants) and .data (globals).

`kdev sigscan --k2` covers .text; it cannot signature data (no code to decode
at the address). This script resolves the rest with the same two-method
discipline the K1 relink's .rdata pass used (engine_rebase.cpp's comment):

  * RTTI identity  - both Aspyr exes carry full RTTI; rtti_scan's chain
    (TypeDescriptor <- COL <- vtable[-1]) names every vtable, so a vtable
    resolves by (mangled class name, base offset) lookup in the target.
  * operand cross-reference - the address appears in .text as an imm32
    (constructors storing vtables, code reading globals); locate each site in
    the target by the bytes PRECEDING the imm32 and read the value there.
    Multiple agreeing sites are required.

Evidence rules (measured against this exe pair, 2026-09-04):

  * vtable: RTTI identity is primary; the second witness is SLOT CONTENT -
    corresponding early vtable slots must point at functions whose first bytes
    match (operand-tolerant). Xref votes corroborate when present, but a
    SINGLE dissenting vote is a warning, not a veto: GUI-class constructors
    are near-identical, and a context window broken by a relocated operand in
    the true constructor can land uniquely in a SIBLING class's constructor
    and read its vtable instead. Measured: exactly 1 stray vote against solid
    RTTI+content on 3 of 46 vtables.
  * non-vtable: xref votes are primary. Unanimity is the norm; where heavily
    referenced globals collect a few false-context votes (241:3, 4157:21,
    828:15 measured), a >=95% majority with >=5 votes carries. Dissent is
    always logged.

Inputs : build/re/sigscan-k2/sigscan-report.json  (names + the data records)
Outputs: build/re/sigscan-k2/engine_rebase_k2_rdata.inc
         build/re/sigscan-k2/rdata-data-report.txt
Exit   : 0 only if EVERY harvested .rdata/.data address resolved.
"""
import json
import os
import re
import struct
import sys

STEAM = r"C:\Tools\k2re\swkotor2.exe"
GOG = r"C:\Program Files\GOG Galaxy\Games\Star Wars - KotOR2\swkotor2.exe"
OUTDIR = r"C:\Users\fabia\Dev\kotor\build\re\sigscan-k2"
REPORT_JSON = os.path.join(OUTDIR, "sigscan-report.json")


class Pe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        assert d[pe:pe + 4] == b"PE\0\0"
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        sec = opt + struct.unpack_from("<H", d, pe + 20)[0]
        self.sections = []
        for _ in range(nsec):
            name = d[sec:sec + 8].rstrip(b"\0").decode()
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", d, sec + 8)
            self.sections.append((name, va, vsize, rawptr, rawsize))
            sec += 40

    def sec(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        return None

    def va2off(self, va):
        rva = va - self.base
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if sva <= rva < sva + max(vsize, rawsize):
                off = rawptr + (rva - sva)
                return off if off < rawptr + rawsize else None  # BSS tail
        return None

    def off2va(self, off):
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if rawptr <= off < rawptr + rawsize:
                return self.base + sva + (off - rawptr)
        return None

    def section_of(self, va):
        rva = va - self.base
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if sva <= rva < sva + max(vsize, rawsize):
                return name
        return None

    def read(self, va, n):
        off = self.va2off(va)
        return None if off is None else self.data[off:off + n]


def rtti_vtables(pe):
    """(mangled_name, base_offset) -> [vtable VA, ...] via TD <- COL <- vt[-1]."""
    d = pe.data
    td_by_va = {}
    for m in re.finditer(rb"\.\?A[VU][A-Za-z0-9_@?$]{2,200}@@\x00", d):
        rva = pe.off2va(m.start())
        if rva is None:
            continue
        td_by_va[rva - 8] = m.group(0)[:-1].decode("ascii", "replace")

    from collections import defaultdict
    dwords = defaultdict(list)
    for name, sva, vsize, rawptr, rawsize in pe.sections:
        if name not in (".rdata", ".data"):
            continue
        for off in range(rawptr, rawptr + rawsize - 3, 4):
            dwords[struct.unpack_from("<I", d, off)[0]].append(off)

    cols = {}
    for td_va, name in td_by_va.items():
        for off in dwords.get(td_va, ()):
            col_off = off - 12
            if col_off < 0 or struct.unpack_from("<I", d, col_off)[0] != 0:
                continue
            col_va = pe.off2va(col_off)
            if col_va is None:
                continue
            cols[col_va] = (name, struct.unpack_from("<I", d, col_off + 4)[0])

    out = {}
    for col_va, (name, base_off) in cols.items():
        for off in dwords.get(col_va, ()):
            vt_va = pe.off2va(off + 4)
            if vt_va is None:
                continue
            first = struct.unpack_from("<I", d, off + 4)[0]
            if pe.section_of(first) != ".text":
                continue
            out.setdefault((name, base_off), []).append(vt_va)
    return out


def xref_votes(steam, gog, steam_text, gog_text, steam_text_base_off, va):
    """Find imm32 references to `va` in Steam .text, locate each site in GOG by
    the bytes preceding the imm32, and read the GOG dword there. Returns
    {candidate_va: votes} plus the number of Steam sites found."""
    needle = struct.pack("<I", va)
    votes = {}
    sites = 0
    start = 0
    while True:
        i = steam_text.find(needle, start)
        if i < 0:
            break
        start = i + 1
        sites += 1
        # Try progressively shorter preceding-context windows; take the first
        # that is UNIQUE in the GOG .text. Long contexts can embed other moved
        # operands (then: zero hits -> shrink); short ones repeat (then: many
        # hits -> already tried longer). 5 bytes minimum keeps luck out.
        for ctx in (16, 14, 12, 10, 8, 6, 5):
            if i < ctx:
                continue
            pat = steam_text[i - ctx:i]
            j = gog_text.find(pat)
            if j < 0:
                continue
            if gog_text.find(pat, j + 1) >= 0:
                break  # ambiguous even at this length; longer already failed
            cand = struct.unpack_from("<I", gog_text, j + ctx)[0]
            votes[cand] = votes.get(cand, 0) + 1
            break
    return votes, sites


def slots_content_match(steam, gog, s_vt, g_vt, nslots=8, probe=12, tol=4):
    """Second witness for a vtable pair: corresponding slots must point at
    functions whose first `probe` bytes differ in at most `tol` positions
    (operand relocations). Returns (passing, comparable)."""
    passing = comparable = 0
    for i in range(nslots):
        s_slot = steam.read(s_vt + 4 * i, 4)
        g_slot = gog.read(g_vt + 4 * i, 4)
        if s_slot is None or g_slot is None:
            break
        s_fn = struct.unpack("<I", s_slot)[0]
        g_fn = struct.unpack("<I", g_slot)[0]
        if steam.section_of(s_fn) != ".text" or gog.section_of(g_fn) != ".text":
            break  # end of vtable
        a, b = steam.read(s_fn, probe), gog.read(g_fn, probe)
        if a is None or b is None:
            continue
        comparable += 1
        if sum(1 for x, y in zip(a, b) if x != y) <= tol:
            passing += 1
    return passing, comparable


def main():
    steam, gog = Pe(STEAM), Pe(GOG)
    rep = json.load(open(REPORT_JSON, encoding="utf-8"))
    data_recs = [e for e in rep["entries"] if e["status"] == "data"]

    # dedupe by address, keep all names
    by_va = {}
    for e in data_recs:
        va = int(e["oldAddress"], 16)
        by_va.setdefault(va, []).append(e["name"])

    name, sva, vsize, rawptr, rawsize = steam.sec(".text")
    steam_text = steam.data[rawptr:rawptr + rawsize]
    gname, gsva, gvsize, grawptr, grawsize = gog.sec(".text")
    gog_text = gog.data[grawptr:grawptr + grawsize]

    print("building RTTI maps...")
    s_rtti = rtti_vtables(steam)
    g_rtti = rtti_vtables(gog)
    s_vt_index = {}  # vtable VA -> (key)
    for key, vts in s_rtti.items():
        for vt in vts:
            s_vt_index[vt] = key
    print(f"  steam vtables: {sum(len(v) for v in s_rtti.values())}, "
          f"gog vtables: {sum(len(v) for v in g_rtti.values())}")

    rows = []      # (steam_va, gog_va, name, method_note)
    failures = []
    report = []

    for va in sorted(by_va):
        names = " / ".join(sorted(set(by_va[va])))
        sec = steam.section_of(va)
        votes, sites = xref_votes(steam, gog, steam_text, gog_text, rawptr, va)
        best = max(votes, key=votes.get) if votes else None
        agree = votes.get(best, 0) if best is not None else 0
        dissent = sum(v for k, v in votes.items() if k != best)

        if va in s_vt_index:
            key = s_vt_index[va]
            cands = g_rtti.get(key, [])
            if len(cands) != 1:
                # same class+offset appearing twice (vbtable variants): let a
                # unanimous xref vote arbitrate if it points at one of them
                if best is not None and best in cands and dissent == 0:
                    cands = [best]
            if len(cands) != 1:
                failures.append((va, names, f"RTTI ambiguous: {key} -> "
                                 f"{[hex(c) for c in cands]}, xref votes {votes}"))
                continue
            gv = cands[0]
            ok_slots, comparable = slots_content_match(steam, gog, va, gv)
            content_ok = comparable >= 2 and ok_slots >= comparable - 1
            if not content_ok:
                failures.append((va, names, f"RTTI candidate {gv:#x} fails slot-"
                                 f"content check ({ok_slots}/{comparable})"))
                continue
            xref_total = sum(votes.values())
            if best is not None and best != gv and xref_total > 1:
                # more than a single stray vote against RTTI+content: real doubt
                failures.append((va, names, f"RTTI+content say {gv:#x} but xref "
                                 f"votes {votes} — methods disagree"))
                continue
            method = (f"vtable {key[0]} boff={key[1]}; RTTI identity + slot "
                      f"content {ok_slots}/{comparable}")
            if best == gv and agree:
                method += f" + {agree} agreeing xref(s)"
            elif best is not None and best != gv:
                method += (f"; 1 stray xref vote for {best:#x} DISCOUNTED "
                           "(sibling-constructor false context — see header)")
            rows.append((va, gv, names, method))
            report.append(f"OK   {va:#010x} -> {gv:#010x}  {names}\n"
                          f"      {method} ({sites} steam ref site(s))")
            continue

        # non-vtable: xref votes are the primary method
        if best is None:
            failures.append((va, names, f"no locatable xref site "
                             f"({sites} steam ref sites, none unique in GOG)"))
            continue
        total = agree + dissent
        if dissent and (agree < 5 or agree / total < 0.95):
            failures.append((va, names, f"xref votes disagree: {votes}"))
            continue
        if agree >= 2:
            method = f"{agree} agreeing operand xrefs (of {sites} steam sites)"
            if dissent:
                method += (f"; {dissent} false-context dissenter(s) DISCOUNTED "
                           f"at {agree}:{dissent} — {votes}")
        else:
            # one vote: require identical initialized content as second witness
            sc = steam.read(va, 16)
            gc = gog.read(best, 16)
            if sc is not None and gc is not None and sc == gc:
                method = "1 xref + identical 16-byte content"
            else:
                failures.append((va, names, f"single xref vote {best:#x} and "
                                 "no content witness (BSS or differs)"))
                continue
        # sanity: same section kind on both sides
        if gog.section_of(best) != sec:
            failures.append((va, names, f"section mismatch: steam {sec}, "
                             f"gog {gog.section_of(best)} at {best:#x}"))
            continue
        rows.append((va, best, names, method))
        report.append(f"OK   {va:#010x} -> {best:#010x}  {names}\n      {method}")

    # ---- delta clustering (corroboration): the relink moves whole modules by
    #      constant deltas, so resolved neighbours sharing a delta vouch for
    #      each other. Informational — singleton deltas are possible (small
    #      modules) but worth an eye during review. ----
    from collections import Counter
    deltas = Counter(gva_ - sva_ for sva_, gva_, _, _ in rows)
    report.append("\n# delta clusters (size x delta):")
    for d, n in deltas.most_common():
        report.append(f"#   {n:3d} x {d:+#x}")

    # ---- emit ----
    inc_path = os.path.join(OUTDIR, "engine_rebase_k2_rdata.inc")
    with open(inc_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// GENERATED by tools/re-scripts/k2_gog_rdata_data_map.py"
                " -- do not edit by hand.\n")
        f.write("// Steam-K2 -> GOG-K2 for the NON-CODE addresses: .rdata"
                " vtables/constants and\n")
        f.write("// .data globals. Vtables resolved by RTTI identity (both"
                " builds carry full\n")
        f.write("// RTTI), everything else by agreeing operand cross-references;"
                " see the\n")
        f.write("// script header for the evidence rules. Sorted ascending for"
                " binary search.\n\n")
        f.write("// clang-format off\n")
        for sva_, gva_, names, method in sorted(rows):
            f.write(f"    {{ 0x{sva_:08X}, 0x{gva_:08X} }},  // {names}\n")
        f.write("// clang-format on\n")

    rep_path = os.path.join(OUTDIR, "rdata-data-report.txt")
    with open(rep_path, "w", encoding="utf-8") as f:
        f.write("# k2_gog_rdata_data_map report\n\n")
        f.write("\n".join(report))
        f.write("\n")
        if failures:
            f.write("\n# FAILURES\n")
            for va, names, why in failures:
                f.write(f"FAIL {va:#010x}  {names}\n      {why}\n")

    print(f"resolved {len(rows)}/{len(by_va)} -> {inc_path}")
    print(f"report   -> {rep_path}")
    if failures:
        print(f"\n{len(failures)} UNRESOLVED:")
        for va, names, why in failures:
            print(f"  {va:#010x}  {names}: {why}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
