"""Export our engine findings into Kotor Patch Manager's AddressDatabase format.

Built for upstream contribution (voice-of-the-old-republic issue 10). Outputs
go to build/kpm-export/ and contain addresses, names and offsets only, never
decompiled code.

Inputs (all already in the repo or build/):
  * KPM's own databases (third_party/Kotor-Patch-Manager/AddressDatabases).
    The KOTOR 1 db supplies every function NAME, so a K2 row is keyed exactly
    like its K1 counterpart and GetFunctionAddress works by one name in both
    games. KPM's existing rows always win; disagreements are reported.
  * build/re/sigscan-k2/sigscan-report.json  Steam-K2 -> GOG-K2, every K2
    address the mod uses (named constants + inline Pick sites).
  * build/re/sigscan/sigscan-report.json     K1 -> 2004 relink, same for K1.
  * docs/llm-docs/re/k2/k2-vtable-slots.csv  K1 <-> K2 virtuals by slot.
  * RTTI scans of both K2 exes (rtti_scan.py output, work/ folder).
  * patches/Accessibility/engine_offsets_fields.h  per-game struct offsets.

Usage:  py tools/re-scripts/kpm_export.py <steam-k2-exe> <gog-k2-exe>
"""
import csv
import json
import os
import re
import shutil
import sqlite3
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
KPM_DB = os.path.join(ROOT, "third_party", "Kotor-Patch-Manager", "AddressDatabases")
KPM_MIG = os.path.join(ROOT, "third_party", "Kotor-Patch-Manager", "tools",
                       "SqliteTools", "Migrations", "005_add_class_vtable.sql")
OUT = os.path.join(ROOT, "build", "kpm-export")
WORK = os.path.join(OUT, "work")

RELINK_HASHES = [
    ("7B961A140667336D22B06686CBDCBE2DDDF8A0EA13C20A27B63BDC57A75D7628",
     "relink 2004-03-05 (Allard Russian 1.72)"),
    ("F96AC62BEE256DAC66B3F7DA3ED62DA4005B4710868E33FD0DB51164015D8137",
     "relink 2004-03-05 (LEM Polish 1.03)"),
]

SRC_MOD = "Voice of the Old Republic: used by the mod on this build"
SRC_SLOT = ("vtable slot match against the KOTOR 1 class (equal slot count); "
            "not individually verified")


def h(x):
    return int(x, 16) if isinstance(x, str) else x


class Exe:
    def __init__(self, path):
        d = self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        n = struct.unpack_from("<H", d, pe + 6)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        sect = opt + struct.unpack_from("<H", d, pe + 20)[0]
        self.secs = []
        for i in range(n):
            s = sect + i * 40
            vsz, va, rsz, raw = struct.unpack_from("<IIII", d, s + 8)
            self.secs.append((self.base + va, max(vsz, rsz), raw, rsz))

    def u32(self, va):
        for lo, size, raw, rsz in self.secs:
            if lo <= va < lo + size and va - lo + 4 <= rsz:
                return struct.unpack_from("<I", self.data, raw + va - lo)[0]
        return None

    def bytes_at(self, va, n):
        for lo, size, raw, rsz in self.secs:
            if lo <= va < lo + size:
                return self.data[raw + va - lo: raw + va - lo + n]
        return b""


def load_vtables(path):
    """class -> vtable VA for the primary (base offset 0) vtable; unique only."""
    seen = defaultdict(list)
    with open(path) as f:
        rows = [r for r in csv.reader(f) if r and r[0].startswith("0x")]
    for va, _col, off, cls, _m in rows:
        if off == "0":
            seen[cls].append(h(va))
    return {c: v[0] for c, v in seen.items() if len(v) == 1}


def k1_names():
    c = sqlite3.connect(os.path.join(KPM_DB, "kotor1_0_3.db"))
    by_addr = {}
    for cls, fn, addr, cc, ps in c.execute(
            "select class_name, function_name, address, calling_convention, "
            "param_size_bytes from functions"):
        by_addr.setdefault(addr, (cls, fn, cc, ps))
    return by_addr, c


def sigscan(path):
    j = json.load(open(path))
    return j["entries"]


def read_pairs(rel):
    """{ 0xFROM, 0xTO } rows of one of our generated rebase tables."""
    out = {}
    for line in open(os.path.join(ROOT, rel)):
        m = re.match(r"\s*\{\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)", line)
        if m:
            out[h(m.group(1))] = h(m.group(2))
    return out


def inline_k1(name):
    m = re.search(r"Pick\w*\(\s*(0x[0-9A-Fa-f]+)\s*,", name)
    return h(m.group(1)) if m else None


def source_k1(source):
    """K1 half of the Pick() on the report's source line ("file:line")."""
    path, _, line = (source or "").rpartition(":")
    try:
        text = open(os.path.join(ROOT, path), encoding="utf-8",
                    errors="replace").readlines()[int(line) - 1]
    except (OSError, ValueError, IndexError):
        return None
    k1 = inline_k1(text)
    return k1 or None  # PickGlobal(0, ...) marks a K2-only global


def open_copy(src_name, dst_name):
    dst = os.path.join(OUT, dst_name)
    shutil.copyfile(os.path.join(KPM_DB, src_name), dst)
    c = sqlite3.connect(dst)
    cols = [r[1] for r in c.execute("pragma table_info(classes)")]
    if "vtable" not in cols:
        c.executescript(open(KPM_MIG).read())
    return c


class Sink:
    """Collects function rows for one db; KPM's existing rows win."""

    def __init__(self, conn, label):
        self.c, self.label = conn, label
        self.by_name = {(a, b): v for a, b, v in conn.execute(
            "select class_name, function_name, address from functions")}
        self.by_addr = defaultdict(list)
        for k, v in self.by_name.items():
            self.by_addr[v].append(k)
        self.added = Counter()
        self.conflicts = []

    def add(self, cls, fn, addr, cc, ps, note, tag):
        key = (cls, fn)
        if key in self.by_name:
            if self.by_name[key] != addr:
                self.conflicts.append((cls, fn, self.by_name[key], addr, tag))
            return
        if addr in self.by_addr:
            return  # same address already named differently; keep theirs
        self.c.execute(
            "insert into functions (class_name, function_name, address, notes, "
            "calling_convention, param_size_bytes) values (?,?,?,?,?,?)",
            (cls, fn, addr, note, cc, ps))
        self.by_name[key] = addr
        self.by_addr[addr].append(key)
        self.added[tag] += 1


def main(steam_path, gog_path):
    os.makedirs(WORK, exist_ok=True)
    steam, gog = Exe(steam_path), Exe(gog_path)
    k1fn, k1db = k1_names()
    report = []

    # ---- KOTOR 2: the mod's own addresses, Steam and GOG -------------------
    k1rep = sigscan(os.path.join(ROOT, "build/re/sigscan/sigscan-report.json"))
    k2rep = sigscan(os.path.join(ROOT, "build/re/sigscan-k2/sigscan-report.json"))
    k1_by_name = {e["name"]: h(e["oldAddress"]) for e in k1rep}

    mod_funcs = []   # (cls, fn, cc, ps, steam, gog)
    mod_data = []    # (name, k1, steam, gog)
    k2_rdata = read_pairs("patches/Accessibility/engine_rebase_k2_rdata.inc")
    for e in k2rep:
        s = h(e["oldAddress"])
        k1 = inline_k1(e["name"]) if e["name"].startswith("(inline)") \
            else k1_by_name.get(e["name"])
        if k1 is None:
            k1 = source_k1(e["source"])
        if e["status"] == "data":  # sigscan leaves data to the rdata/data map
            if s in k2_rdata:
                mod_data.append((e["name"], k1, s, k2_rdata[s]))
            continue
        if not e.get("newAddress"):
            continue
        g = h(e["newAddress"])
        if k1 in k1fn:   # K1 address is a named function start, not a hook site
            cls, fn, cc, ps = k1fn[k1]
            mod_funcs.append((cls, fn, cc, ps, s, g))

    # ---- KOTOR 2: virtuals by vtable slot ----------------------------------
    svt = load_vtables(os.path.join(WORK, "k2-steam-vtables.csv"))
    gvt = load_vtables(os.path.join(WORK, "k2-gog-vtables.csv"))
    slot_funcs, slot_skip, by_fn = [], Counter(), defaultdict(list)
    slot_csv = os.path.join(ROOT, "docs/llm-docs/re/k2/k2-vtable-slots.csv")
    with open(slot_csv, encoding="utf-8", errors="replace") as f:
        rows = [r for r in csv.reader(f) if len(r) == 6 and r[0] and
                not r[0].startswith("#") and r[0] != "class"]
    for cls, slot, k1a, k2a, conf, _k1name in rows:
        if conf != "exact":
            slot_skip["prefix (slot counts differ)"] += 1
            continue
        k1a, k2a, slot = h(k1a), h(k2a), int(slot)
        if k1a not in k1fn:
            slot_skip["K1 function unnamed in KPM db"] += 1
            continue
        if cls not in svt or steam.u32(svt[cls] + 4 * slot) != k2a:
            slot_skip["Steam vtable re-read disagrees"] += 1
            continue
        if cls not in gvt:
            slot_skip["class has no unique GOG vtable"] += 1
            continue
        by_fn[k1fn[k1a]].append((cls, k2a, gog.u32(gvt[cls] + 4 * slot)))
    # A K1 name reaches K2 through every vtable that holds it, including
    # derived classes that inherit it. Where KOTOR 2 overrides in a derived
    # class those rows disagree, so: the defining class's own vtable decides
    # when present, otherwise every row must agree; any split drops the name.
    for (n1, n2, cc, ps), hits in by_fn.items():
        own = [x for x in hits if x[0] == n1] or hits
        if len({(s, g) for _, s, g in own}) != 1:
            slot_skip["vtables disagree on the address"] += 1
            continue
        _, s, g = own[0]
        slot_funcs.append((n1, n2, cc, ps, s, g))

    # Cross-check: where both sources name the same function, GOG must agree.
    mod_gog = {(a, b): g for a, b, _, _, _, g in mod_funcs}
    agree = disagree = 0
    for a, b, _, _, _, g in slot_funcs:
        if (a, b) in mod_gog:
            agree += mod_gog[(a, b)] == g
            disagree += mod_gog[(a, b)] != g
    # Relink sanity: a function's leading bytes survive a relink unless a
    # relocated operand sits inside them.
    pro_same = sum(steam.bytes_at(s, 6) == gog.bytes_at(g, 6)
                   for *_, s, g in slot_funcs + mod_funcs)

    sinks = {}
    for build, src, idx in (("steam", "kotor2_steam_aspyr.db", 4),
                            ("gog", "kotor2_gog_aspyr.db", 5)):
        c = open_copy(src, src)
        sk = Sink(c, build)
        for cls, fn, cc, ps, s, g in mod_funcs:
            sk.add(cls, fn, (s, g)[idx - 4], cc, ps, SRC_MOD, "mod")
        for cls, fn, cc, ps, s, g in slot_funcs:
            sk.add(cls, fn, (s, g)[idx - 4], cc, ps, SRC_SLOT, "vtable-slot")
        vt = svt if build == "steam" else gvt
        have = {r[0] for r in c.execute("select class_name from classes")}
        nvt = 0
        for cls, va in sorted(vt.items()):
            if not re.fullmatch(r"[A-Za-z_]\w*", cls):
                continue  # template instantiations: no stable KPM name
            if cls in have:
                c.execute("update classes set vtable=? where class_name=? and "
                          "vtable is null", (va, cls))
            else:
                c.execute("insert into classes (class_name, size, notes, vtable) "
                          "values (?, null, ?, ?)",
                          (cls, "vtable from RTTI (Voice of the Old Republic)", va))
            nvt += 1
        c.commit()
        sinks[build] = (sk, nvt)

    # ---- KOTOR 1 2004 relink ------------------------------------------------
    rl_path = os.path.join(OUT, "kotor1_relink_2004.db")
    shutil.copyfile(os.path.join(KPM_DB, "kotor1_0_3.db"), rl_path)
    rl = sqlite3.connect(rl_path)
    for t in ("functions", "game_version"):
        rl.execute(f"delete from {t}")
    for i, (sha, ver) in enumerate(RELINK_HASHES, 1):
        rl.execute("insert into game_version (id, sha256_hash, game_name, "
                   "version_string, description, platform) values (?,?,?,?,?,?)",
                   (i, sha, "KOTOR1", ver,
                    "Same BioWare compile as 1.0.3, relinked: code is "
                    "byte-identical but functions moved; .data is unmoved, so "
                    "global pointers and struct offsets equal 1.0.3.", "Windows"))
    rl_sink = Sink(rl, "relink")
    rdata = read_pairs("patches/Accessibility/engine_rebase_rdata.inc")
    for e in k1rep:
        if e["status"] not in ("unique", "unique-ordinal", "hand-resolved"):
            continue
        k1 = h(e["oldAddress"])
        if k1 in k1fn and e.get("newAddress"):
            cls, fn, cc, ps = k1fn[k1]
            rl_sink.add(cls, fn, h(e["newAddress"]), cc, ps, SRC_MOD, "mod")
    rl_vt_kept = 0
    for cid, va in list(rl.execute("select id, vtable from classes where vtable is not null")):
        if va in rdata:
            rl.execute("update classes set vtable=? where id=?", (rdata[va], cid))
            rl_vt_kept += 1
        else:
            rl.execute("update classes set vtable=null where id=?", (cid,))
    rl.commit()

    # ---- Review CSVs ---------------------------------------------------------
    with open(os.path.join(OUT, "k2-functions-added.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["class_name", "function_name", "steam_address",
                    "gog_address", "source"])
        steam_new = dict(((a, b), v) for a, b, v in sinks["steam"][0].c.execute(
            "select class_name, function_name, address from functions where notes in (?,?)",
            (SRC_MOD, SRC_SLOT)))
        gog_new = dict(((a, b), v) for a, b, v in sinks["gog"][0].c.execute(
            "select class_name, function_name, address from functions where notes in (?,?)",
            (SRC_MOD, SRC_SLOT)))
        src_of = {(a, b): "mod" for a, b, *_ in mod_funcs}
        for k in sorted(steam_new):
            w.writerow([k[0], k[1], f"0x{steam_new[k]:08X}",
                        f"0x{gog_new[k]:08X}" if k in gog_new else "",
                        src_of.get(k, "vtable-slot")])
    with open(os.path.join(OUT, "k2-data-addresses.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["our_name", "k1_address", "steam_address", "gog_address"])
        for name, k1, s, g in sorted(mod_data, key=lambda r: r[2]):
            w.writerow([name, f"0x{k1:08X}" if k1 else "", f"0x{s:08X}", f"0x{g:08X}"])
    shutil.copyfile(slot_csv, os.path.join(OUT, "k1-k2-vtable-slots.csv"))
    shutil.copyfile(os.path.join(WORK, "k2-steam-vtables.csv"),
                    os.path.join(OUT, "k2-steam-vtables.csv"))
    shutil.copyfile(os.path.join(WORK, "k2-gog-vtables.csv"),
                    os.path.join(OUT, "k2-gog-vtables.csv"))
    n_off = export_offsets()

    # ---- Report --------------------------------------------------------------
    report += [
        f"mod functions (named via KPM K1 db): {len(mod_funcs)}",
        f"vtable-slot functions accepted: {len(slot_funcs)}; skipped: {dict(slot_skip)}",
        f"GOG cross-check where both sources name a function: agree {agree}, disagree {disagree}",
        f"leading 6 bytes identical Steam vs GOG: {pro_same} of {len(slot_funcs) + len(mod_funcs)}",
    ]
    for build, (sk, nvt) in sinks.items():
        report.append(f"[{build}] added {dict(sk.added)}; vtables written {nvt}; "
                      f"conflicts with KPM rows {len(sk.conflicts)}")
        for cls, fn, theirs, ours, tag in sk.conflicts:
            report.append(f"    CONFLICT {cls}::{fn} KPM 0x{theirs:08X} ours 0x{ours:08X} ({tag})")
    report.append(f"[relink] added {dict(rl_sink.added)}; vtables mapped {rl_vt_kept}")
    report.append(f"offsets exported: {n_off}")
    open(os.path.join(OUT, "export-report.txt"), "w").write("\n".join(report) + "\n")
    print("\n".join(report))


def export_offsets():
    """engine_offsets_fields.h -> CSV. Class context = the comment block above."""
    src = os.path.join(ROOT, "patches/Accessibility/engine_offsets_fields.h")
    decl = re.compile(r"^\s*(?:static\s+)?const\w*\s+[\w:]+\s+(k\w+)\s*=\s*"
                      r"acc::off::(Pick|Same|Todo)\(\s*([^,)]+)(?:,\s*([^)]+))?\)")
    rows, comment, after_decl = [], [], False
    for line in open(src, encoding="utf-8", errors="replace"):
        s = line.strip()
        if s.startswith("//"):
            if after_decl:  # a new block starts; the old one described earlier decls
                comment, after_decl = [], False
            comment.append(s[2:].strip())
            continue
        after_decl = after_decl or bool(decl.match(line))
        m = decl.match(line)
        if m:
            name, kind, a, b = m.groups()
            k2 = {"Pick": b, "Same": a, "Todo": ""}[kind]
            ctx = " ".join(comment)
            cls = re.findall(r"\b(C[A-Z][A-Za-z0-9]+|Gob|Scene)\b", ctx)
            rows.append([name, a.strip(), (k2 or "").strip(),
                         {"Pick": "differs", "Same": "same", "Todo": "K2 unknown"}[kind],
                         cls[0] if cls else "", ctx[:300]])
        elif s:
            comment = []
    with open(os.path.join(OUT, "k1-k2-offsets.csv"), "w", newline="",
              encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["our_name", "k1_value", "k2_value", "status", "class_hint",
                    "context"])
        w.writerows(rows)
    return len(rows)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
