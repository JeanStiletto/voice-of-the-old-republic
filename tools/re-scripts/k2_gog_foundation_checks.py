"""Foundation checks for the KOTOR 2 Steam->GOG address port.

Steam reference: C:\\Tools\\k2re\\swkotor2.exe        (clean, 6A52...)
GOG target:      GOG Galaxy install swkotor2.exe      (clean, 777B...)
"""
import re
import sqlite3
import struct
import sys
from collections import Counter

STEAM = r"C:\Tools\k2re\swkotor2.exe"
GOG = r"C:\Program Files\GOG Galaxy\Games\Star Wars - KotOR2\swkotor2.exe"
DBDIR = r"C:\Users\fabia\Dev\kotor\third_party\Kotor-Patch-Manager\AddressDatabases"
HOOKS = r"C:\Users\fabia\Dev\kotor\patches\Accessibility\kotor2.hooks.toml"
SRCDIR = r"C:\Users\fabia\Dev\kotor\patches\Accessibility"


class Pe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        assert d[pe:pe + 4] == b"PE\0\0"
        (self.machine, nsec, self.timestamp) = struct.unpack_from("<HHI", d, pe + 4)
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        self.image_base = struct.unpack_from("<I", d, opt + 28)[0]
        self.sections = []
        sec = opt + opt_size
        for i in range(nsec):
            name = d[sec:sec + 8].rstrip(b"\0").decode()
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", d, sec + 8)
            self.sections.append((name, va, vsize, rawptr, rawsize))
            sec += 40

    def va2off(self, va):
        rva = va - self.image_base
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if sva <= rva < sva + max(vsize, rawsize):
                return rawptr + (rva - sva)
        return None

    def read(self, va, n):
        off = self.va2off(va)
        if off is None:
            return None
        return self.data[off:off + n]

    def text_range(self):
        for name, sva, vsize, rawptr, rawsize in self.sections:
            if name == ".text":
                return rawptr, rawptr + rawsize, sva
        return None


steam = Pe(STEAM)
gog = Pe(GOG)

print("== PE identity ==")
for label, pe in (("steam", steam), ("gog  ", gog)):
    print(f"{label}: timestamp {pe.timestamp:#010x}  base {pe.image_base:#x}  "
          f"sections {[s[0] for s in pe.sections]}")

print()
print("== dinput8 import (string presence in image) ==")
for label, pe in (("steam", steam), ("gog  ", gog)):
    print(f"{label}: dinput8.dll {'FOUND' if b'dinput8.dll' in pe.data or b'DINPUT8.dll' in pe.data else 'MISSING'}")

# --- Lane's function pairs: byte identity within functions -------------------
print()
print("== Lane's 48 function pairs: 64-byte window compare ==")


def load(nm):
    con = sqlite3.connect(rf"{DBDIR}\kotor2_{nm}_aspyr.db")
    f = {(c, fn): int(a, 16) if isinstance(a, str) else a
         for c, fn, a in con.execute("select class_name,function_name,address from functions")}
    con.close()
    return f


sf, gf = load("steam"), load("gog")
identical = operand_only = worse = 0
worst = []
for k in sorted(sf.keys() & gf.keys()):
    a, b = steam.read(sf[k], 64), gog.read(gf[k], 64)
    if a is None or b is None:
        print(f"  UNREADABLE {k}")
        continue
    diffs = [i for i in range(64) if a[i] != b[i]]
    if not diffs:
        identical += 1
    elif len(diffs) <= 16:
        operand_only += 1
    else:
        worse += 1
        worst.append((k, len(diffs)))
print(f"  identical in 64 bytes: {identical}")
print(f"  1-16 differing bytes (operand-level): {operand_only}")
print(f"  more than 16 differing: {worse}  {worst[:5]}")

# --- Our 26 hooks: search original_bytes in GOG .text ------------------------
print()
print("== kotor2.hooks.toml hook sites searched in GOG .text ==")
text = open(HOOKS, encoding="utf-8").read()
hooks = []
for m in re.finditer(r"\[\[hooks\]\](.*?)(?=\[\[hooks\]\]|\Z)", text, re.S):
    body = m.group(1)
    am = re.search(r"^address = (0x[0-9A-Fa-f]+)", body, re.M)
    bm = re.search(r"original_bytes = \[([^\]]*)\]", body, re.S)
    if not am:
        continue
    addr = int(am.group(1), 16)
    obytes = bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{1,2}", bm.group(1))) if bm else b""
    hooks.append((addr, obytes))
lo, hi, _ = gog.text_range()
gtext = gog.data[lo:hi]
uniq = multi = absent = 0
for addr, ob in hooks:
    if not ob:
        print(f"  hook {addr:#010x}: no original_bytes (dll-only?)")
        continue
    n = gtext.count(ob)
    if n == 1:
        uniq += 1
    elif n == 0:
        absent += 1
        print(f"  hook {addr:#010x} ({len(ob)}B): NOT FOUND in GOG .text")
    else:
        multi += 1
        print(f"  hook {addr:#010x} ({len(ob)}B): {n} matches (needs wider signature)")
print(f"  summary: unique {uniq}, multiple {multi}, absent {absent}, total-with-bytes "
      f"{uniq + multi + absent}")

# --- Sample of Pick()/Kotor2Only() K2 addresses ------------------------------
print()
print("== Pick/Kotor2Only K2-side constants: 32-byte window search in GOG ==")
import os
consts = set()
for root, _, files in os.walk(SRCDIR):
    for fn in files:
        if not fn.endswith((".cpp", ".h")):
            continue
        s = open(os.path.join(root, fn), encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"Pick\(\s*0x[0-9A-Fa-f]+\s*,\s*(0x[0-9A-Fa-f]+)", s):
            consts.add(int(m.group(1), 16))
        for m in re.finditer(r"Kotor2Only\(\s*(0x[0-9A-Fa-f]+)", s):
            consts.add(int(m.group(1), 16))
consts = sorted(c for c in consts if c > 0x400000)
print(f"  distinct K2 constants harvested from source: {len(consts)}")
res = Counter()
absent_list = []
for va in consts:
    w = steam.read(va, 32)
    if w is None:
        res["unreadable"] += 1
        continue
    n = gtext.count(w)
    if n == 1:
        res["unique"] += 1
    elif n == 0:
        res["absent"] += 1
        absent_list.append(va)
    else:
        res["multiple"] += 1
print(f"  32-byte raw window: {dict(res)}")
# retry absent with operand masking: compare only bytes unlikely to be operands
# cheap retry: shorter 12-byte window for the absent ones
res2 = Counter()
for va in absent_list:
    w = steam.read(va, 12)
    n = gtext.count(w)
    res2["unique" if n == 1 else ("absent" if n == 0 else "multiple")] += 1
print(f"  absent ones retried with 12-byte window: {dict(res2)}")
