"""Report which engine constants a set of source files needs, and which of them
are not yet resolved for KOTOR 2.

This is step 2/3 of the porting method in docs/kotor2-port.md: take a KOTOR 1
subsystem whole, enumerate every address and offset its call graph touches, and
verify them all offline BEFORE running anything. Building reduced KOTOR 2 paths
that skip KOTOR 1 logic was tried and abandoned — omitting a workaround does not
defer its cost, it multiplies it, and every rediscovery costs a test round.

Usage:
    port_worklist.py <patch-dir> <file> [<file> ...]

Files may be given as names ("menus.cpp"); their local #includes are followed so
the whole subsystem is covered rather than just the entry file.
"""
import os
import re
import sys

DECL = re.compile(
    r"\b(k[A-Za-z0-9_]+)\s*=\s*acc::(off|addr)::"
    r"(Todo|Same|Pick|Kotor1Only|PickGlobal|TodoGlobal|R)\(",
)
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)
USE = re.compile(r"\b(k[A-Za-z0-9_]+)\b")

# Markers that still have no KOTOR 2 value.
UNRESOLVED = {"Todo", "R", "TodoGlobal"}


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def declarations(patch_dir):
    """name -> (marker, declaring file)"""
    out = {}
    for fn in os.listdir(patch_dir):
        if not fn.endswith((".h", ".cpp", ".inc")):
            continue
        for m in DECL.finditer(read(os.path.join(patch_dir, fn))):
            out[m.group(1)] = (m.group(3), fn)
    return out


def expand(patch_dir, seeds):
    """The seed files plus their header/implementation siblings.

    Deliberately NOT the transitive include closure. Following includes pulls in
    227 files and 700 constants for the menu subsystem, because a header
    including engine_area.h drags in audio, combat and everything else those
    reference — that is the include graph, not the call graph, and it reports
    work the subsystem does not actually need.

    Widen a subsystem by naming more seed files explicitly. That keeps the
    worklist something a human decided rather than something a transitive
    closure inflated.
    """
    seen = set()
    for fn in seeds:
        for cand in (fn,
                     fn[:-4] + ".h" if fn.endswith(".cpp") else fn[:-2] + ".cpp"):
            if os.path.exists(os.path.join(patch_dir, cand)):
                seen.add(cand)
    return seen


def main(patch_dir, seeds):
    decls = declarations(patch_dir)
    files = expand(patch_dir, seeds)

    used = {}
    for fn in sorted(files):
        for name in set(USE.findall(read(os.path.join(patch_dir, fn)))):
            if name in decls:
                used.setdefault(name, set()).add(fn)

    todo = {n: v for n, v in used.items() if decls[n][0] in UNRESOLVED}
    done = len(used) - len(todo)

    print(f"subsystem: {', '.join(sorted(seeds))}")
    print(f"files in closure: {len(files)}")
    print(f"engine constants used: {len(used)}   resolved for K2: {done}   "
          f"UNRESOLVED: {len(todo)}")
    print()
    by_decl = {}
    for name, users in todo.items():
        by_decl.setdefault(decls[name][1], []).append(name)
    for decl_file in sorted(by_decl):
        names = sorted(by_decl[decl_file])
        print(f"--- {decl_file}  ({len(names)})")
        for n in names:
            print(f"    {decls[n][0]:<11} {n}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
