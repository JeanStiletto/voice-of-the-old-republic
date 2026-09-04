"""Offline validation gate for the KOTOR 2 Steam->GOG relocation.

Independent checks, all of which must pass before a live round
(docs/kotor2-port.md "GOG build", execution plan steps 2 and 7):

  1. Lane's per-store AddressDatabases (48 shared functions + 14 globals) are
     a free validation set nobody on our side produced: every pair whose Steam
     address appears in OUR generated tables must map to exactly Lane's GOG
     address.
  2. The two `unique-ordinal` entries are matched by address ORDER among
     byte-identical code — a weak inference under a +-4MB module shuffle, so
     each is confirmed here by call-site cross-reference: every locatable
     CALL rel32 to the Steam address, found in GOG by preceding-byte context,
     must decode to the table's GOG address.
  3. kotor2-gog.hooks.toml gets the exact check KotorPatcher will run:
     original_bytes must equal the GOG exe bytes at every hook address. Each
     consumed_exit_address must land on bytes matching Steam's at the old
     exit (operand-tolerant).
  4. Every row of both generated .incs is re-checked by raw byte windows
     (operand-tolerant), independent of the signature machinery that
     produced them.

Exit 0 = everything passed.
"""
import os
import re
import sqlite3
import struct
import sys

STEAM = r"C:\Tools\k2re\swkotor2.exe"
GOG = r"C:\Program Files\GOG Galaxy\Games\Star Wars - KotOR2\swkotor2.exe"
OUTDIR = r"C:\Users\fabia\Dev\kotor\build\re\sigscan-k2"
DBDIR = (r"C:\Users\fabia\Dev\kotor\third_party\Kotor-Patch-Manager"
         r"\AddressDatabases")


class Pe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
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

    def va2off(self, va):
        rva = va - self.base
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if sva <= rva < sva + max(vsize, rawsize):
                off = rawptr + (rva - sva)
                return off if off < rawptr + rawsize else None
        return None

    def read(self, va, n):
        off = self.va2off(va)
        return None if off is None else self.data[off:off + n]

    def text(self):
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if name == ".text":
                return self.data[rawptr:rawptr + rawsize], rawptr, sva
        raise RuntimeError("no .text")


def load_inc(path):
    table = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}", line)
        if m:
            table[int(m.group(1), 16)] = int(m.group(2), 16)
    return table


def load_lane():
    pairs = {}
    for kind in ("functions", "global_pointers"):
        maps = []
        for store in ("steam", "gog"):
            con = sqlite3.connect(os.path.join(DBDIR, f"kotor2_{store}_aspyr.db"))
            cols = "class_name,function_name" if kind == "functions" else "pointer_name"
            rows = {}
            for r in con.execute(f"select {cols},address from {kind}"):
                key, addr = tuple(r[:-1]), r[-1]
                rows[key] = int(addr, 16) if isinstance(addr, str) else addr
            con.close()
            maps.append(rows)
        for key in maps[0].keys() & maps[1].keys():
            pairs[(kind,) + key] = (maps[0][key], maps[1][key])
    return pairs


def tolerant_eq(a, b, tol):
    return a is not None and b is not None and \
        sum(1 for x, y in zip(a, b) if x != y) <= tol


def main():
    steam, gog = Pe(STEAM), Pe(GOG)
    stext, sraw, ssva = steam.text()
    gtext, graw, gsva = gog.text()

    table = load_inc(os.path.join(OUTDIR, "engine_rebase_k2_table.inc"))
    rdata = load_inc(os.path.join(OUTDIR, "engine_rebase_k2_rdata.inc"))
    combined = table | rdata
    fails = []

    # ---- 1. Lane's pairs ----
    lane = load_lane()
    checked = 0
    for key, (s_va, g_va) in sorted(lane.items()):
        if s_va in combined:
            checked += 1
            if combined[s_va] != g_va:
                fails.append(f"Lane disagrees on {key}: {s_va:#x} -> ours "
                             f"{combined[s_va]:#x}, Lane {g_va:#x}")
    print(f"1. Lane validation set: {len(lane)} pairs, {checked} overlap our "
          f"tables, {checked - sum('Lane' in f for f in fails)} agree")

    # ---- 2. ordinal entries by call-site xref ----
    ordinals = []
    for line in open(os.path.join(OUTDIR, "engine_rebase_k2_table.inc"),
                     encoding="utf-8"):
        if "// ordinal" in line:
            m = re.match(r"\s*\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)", line)
            ordinals.append((int(m.group(1), 16), int(m.group(2), 16),
                             line.split("//")[-1].strip()))
    for s_va, g_va, name in ordinals:
        votes = {}
        sites = 0
        i = 0
        while True:
            i = stext.find(b"\xe8", i)
            if i < 0:
                break
            rel = struct.unpack_from("<i", stext, i + 1)[0] \
                if i + 5 <= len(stext) else None
            if rel is None:
                break
            src_va = steam.base + ssva + i
            if (src_va + 5 + rel) & 0xFFFFFFFF == s_va:
                sites += 1
                for ctx in (16, 14, 12, 10, 8, 6, 5):
                    if i < ctx:
                        continue
                    pat = stext[i - ctx:i]
                    j = gtext.find(pat)
                    if j < 0:
                        continue
                    if gtext.find(pat, j + 1) >= 0:
                        break
                    if gtext[j + ctx:j + ctx + 1] != b"\xe8":
                        break
                    grel = struct.unpack_from("<i", gtext, j + ctx + 1)[0]
                    gsrc = gog.base + gsva + j + ctx
                    votes[(gsrc + 5 + grel) & 0xFFFFFFFF] = \
                        votes.get((gsrc + 5 + grel) & 0xFFFFFFFF, 0) + 1
                    break
            i += 1
        best = max(votes, key=votes.get) if votes else None
        dissent = sum(v for k, v in votes.items() if k != best)
        verdict = "CONFIRMED" if best == g_va and not dissent and \
            votes.get(best, 0) >= 2 else "NOT CONFIRMED"
        if verdict != "CONFIRMED":
            fails.append(f"ordinal {name} {s_va:#x}->{g_va:#x}: call-site "
                         f"votes {votes} ({sites} steam sites)")
        print(f"2. ordinal {name}: {s_va:#x} -> {g_va:#x} {verdict} "
              f"({votes.get(best, 0)} agreeing call sites of {sites}, "
              f"dissent {dissent})")

    # ---- 3. hooks file, the patcher's own check ----
    hooks_path = os.path.join(OUTDIR, "kotor2-gog.hooks.toml")
    text = open(hooks_path, encoding="utf-8").read()
    src_text = open(os.path.join(os.path.dirname(OUTDIR), "..", "..",
                                 "patches", "Accessibility",
                                 "kotor2.hooks.toml"), encoding="utf-8").read()
    old_exits = [int(x, 16) for x in re.findall(
        r"^consumed_exit_address = (0x[0-9a-fA-F]+)", src_text, re.M)]
    n_hooks = n_ok = n_exit_ok = 0
    exits_seen = 0
    for block in re.split(r"^\[\[hooks\]\]", text, flags=re.M)[1:]:
        am = re.search(r"^address = (0x[0-9a-fA-F]+)", block, re.M)
        bm = re.search(r"original_bytes = \[([^\]]*)\]", block, re.S)
        em = re.search(r"^consumed_exit_address = (0x[0-9a-fA-F]+)", block, re.M)
        wm = re.search(r"was (0x[0-9a-fA-F]+)", block)
        if not am:
            continue
        n_hooks += 1
        addr = int(am.group(1), 16)
        ob = bytes(int(x, 16) for x in
                   re.findall(r"0x[0-9a-fA-F]{1,2}", bm.group(1)))
        actual = gog.read(addr, len(ob))
        if actual == ob:
            n_ok += 1
        else:
            fails.append(f"hook @{addr:#x}: GOG bytes "
                         f"{actual.hex() if actual else None} != original_bytes "
                         f"{ob.hex()}")
        if em:
            exits_seen += 1
            new_exit = int(em.group(1), 16)
            ex_m = re.search(r"was (0x[0-9a-fA-F]+)[^\n]*\n"
                             r"consumed_exit_address", block)
            old_exit = int(ex_m.group(1), 16) if ex_m else None
            if old_exit is not None:
                if tolerant_eq(steam.read(old_exit, 12), gog.read(new_exit, 12), 4):
                    n_exit_ok += 1
                else:
                    fails.append(f"consumed exit {old_exit:#x}->{new_exit:#x}: "
                                 "bytes do not match")
    print(f"3. hooks: {n_ok}/{n_hooks} original_bytes verified at GOG "
          f"addresses; {n_exit_ok}/{exits_seen} consumed exits byte-checked "
          f"(source file lists {len(old_exits)})")
    if n_hooks != 26 or exits_seen != len(old_exits):
        fails.append(f"hook/exit count mismatch: {n_hooks} hooks, "
                     f"{exits_seen} exits vs source {len(old_exits)}")

    # ---- 4. window re-check of every table row, pointer-aware ----
    # .text rows: raw bytes with operand tolerance. .rdata/.data rows are
    # largely ARRAYS OF CODE POINTERS (vtables), where every dword legitimately
    # differs; there the check is per-dword: equal, or mapped by our own table,
    # or both sides point at .text code whose first bytes match.
    def sec_of(pe_obj, va):
        rva = va - pe_obj.base
        for name, sva, vsize, rawptr, rawsize in pe_obj.sections:
            if sva <= rva < sva + max(vsize, rawsize):
                return name
        return None

    bad = 0
    for s_va, g_va in sorted(combined.items()):
        a, b = steam.read(s_va, 24), gog.read(g_va, 24)
        if a is None or b is None:
            # BSS .data rows have no on-disk content; identity was established
            # by xref votes, nothing to byte-check here.
            continue
        if sec_of(steam, s_va) == ".text":
            if not tolerant_eq(a, b, 12):
                bad += 1
                fails.append(f"table row {s_va:#x}->{g_va:#x}: 24-byte code "
                             f"window differs beyond operand tolerance")
            continue
        ok_dwords = 0
        for k in range(6):
            s_dw = struct.unpack_from("<I", a, 4 * k)[0]
            g_dw = struct.unpack_from("<I", b, 4 * k)[0]
            if s_dw == g_dw:
                ok_dwords += 1
            elif s_dw in combined and combined[s_dw] == g_dw:
                ok_dwords += 1
            elif sec_of(steam, s_dw) == ".text" and sec_of(gog, g_dw) == ".text" \
                    and tolerant_eq(steam.read(s_dw, 12), gog.read(g_dw, 12), 4):
                ok_dwords += 1
            elif sec_of(steam, s_dw) not in (None, ".text") \
                    and sec_of(steam, s_dw) == sec_of(gog, g_dw) \
                    and steam.read(s_dw, 12) is not None \
                    and steam.read(s_dw, 12) == gog.read(g_dw, 12):
                # data-to-data pointer (string tables): the pointed-at bytes
                # must be identical, and are
                ok_dwords += 1
            elif sec_of(steam, steam.base + s_dw) == ".rdata" \
                    and sec_of(gog, gog.base + g_dw) == ".rdata" \
                    and steam.read(steam.base + s_dw + 2, 12) is not None \
                    and steam.read(steam.base + s_dw + 2, 12) \
                        == gog.read(gog.base + g_dw + 2, 12):
                # unbound IAT thunk: the dword is an RVA of a hint/name entry
                # (2-byte hint, then the import's NAME) — names must match
                ok_dwords += 1
        # 5 of 6: one slot may fall past the vtable's end into a neighbour
        if ok_dwords < 5:
            bad += 1
            fails.append(f"table row {s_va:#x}->{g_va:#x}: only {ok_dwords}/6 "
                         f"dwords check out (pointer-aware)")
    print(f"4. window re-check (pointer-aware): {len(combined)} rows, "
          f"{bad} suspicious")

    print()
    if fails:
        print(f"FAILED — {len(fails)} problem(s):")
        for f in fails:
            print(f"  {f}")
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
