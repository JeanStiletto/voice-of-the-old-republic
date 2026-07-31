"""Find the engine calls that would fault on KOTOR 2, and say which are UNGUARDED.

Why this exists
---------------
Clearing a hook handler's `acc::game::HandlerEnabled()` gate is only safe once
everything the handler reaches is either resolved for KOTOR 2 or fails
gracefully. Auditing that by reading does not scale — a single focus handler
reaches dozens of helpers.

The failure modes are two, and they are not equally bad:

  * An address still on `acc::addr::R()` resolves to **0** on KOTOR 2. Calling
    it jumps to address 0 and faults.
  * An offset still on `acc::off::Todo()` resolves to **0x7BAD0000**. Reading
    through it faults.

Both are RECOVERABLE inside `__try`/`__except`, which is how most engine-reading
code here is written — the read fails, the caller takes its existing fallback,
and KOTOR 2 degrades instead of crashing. Outside SEH, the same fault is a
crash. So the thing worth finding is not "unresolved constants reached" (the
worklist already counts those) but **unresolved constants reached from code that
has no SEH frame around them**.

What it reports, per file:
  * UNGUARDED — a call/read through an unresolved constant with no enclosing
    `__try` and no `acc::addr::Ok()` test. These are the crashes. Fix by
    resolving the constant, adding an `Ok()` guard, or extending SEH.
  * guarded    — same, but inside `__try` or behind `Ok()`. Degrades safely;
    worth knowing about but not blocking.

One benign pattern still reports as UNGUARDED and is expected to: a helper that
only computes `panel + offset` and RETURNS it, without dereferencing
(`InGameMessagesFindLb`, `ExamineFindLb`, `DialogFindRepliesLb`,
and the `descLb` local in menus_listbox). Forming an out-of-range pointer is not
a fault; only reading through it is, and those consumers do carry SEH. Left
visible rather than suppressed, because "hands a poisoned pointer to someone
else" is worth seeing when auditing a NEW consumer.

Usage:
    handler_chain_audit.py <patch-dir> [file.cpp ...]

With no files, audits every .cpp in the directory. Brace matching is textual and
therefore approximate: it does not parse C++, it tracks `__try {` ... `}` depth.
Treat a clean report as "no obvious hole", not as proof.
"""
import os
import re
import sys

# A constant is unresolved for KOTOR 2 if declared with one of these markers.
UNRESOLVED_DECL = re.compile(
    r"\b(k[A-Za-z0-9_]+)\s*=\s*acc::(?:off|addr)::(Todo|R|TodoGlobal)\(")
# ... and resolved with one of these.
RESOLVED_DECL = re.compile(
    r"\b(k[A-Za-z0-9_]+)\s*=\s*acc::(?:off|addr)::"
    r"(Same|Pick|PickGlobal|Kotor1Only)\(")

IDENT = re.compile(r"\b(k[A-Za-z0-9_]+)\b")

# A function DEFINITION whose body contains __try supplies its own SEH frame, so
# passing an unresolved constant into it as an argument is safe — the fault
# happens inside the callee's guard. Without this the report is dominated by
# calls to SafeReadOff / ReadPtr / DumpUshortListSEH and the real holes drown.
#
# NOT every read helper qualifies: engine_reads.cpp's ReadCExoString and ReadU32
# deliberately carry no guard (callers supply it), which is exactly the gap that
# produced the first two findings this tool was written for. So this is detected
# from the source rather than assumed from the name.
FUNC_DEF = re.compile(r"^[A-Za-z_][\w:<>*&\s]*?\b(\w+)\s*\([^;]*\)\s*\{?\s*$")


def guarded_helpers(patch_dir):
    """Names of functions whose own body opens a __try."""
    names = set()
    for fn in sorted(os.listdir(patch_dir)):
        if not fn.endswith(".cpp"):
            continue
        lines = open(os.path.join(patch_dir, fn), encoding="utf-8",
                     errors="replace").read().splitlines()
        current = None
        depth = 0
        for line in lines:
            code = re.sub(r"//.*", "", line)
            m = FUNC_DEF.match(code.rstrip())
            if m and depth == 0:
                current = m.group(1)
            depth += code.count("{") - code.count("}")
            if depth <= 0:
                if not re.search(r"\b__try\b", code):
                    pass
            if current and re.search(r"\b__try\b", code):
                names.add(current)
    # Templates and one-line forwarders that the regex above misses, but whose
    # whole purpose is a guarded read. Confirmed by reading each.
    names.update({"SafeRead", "SafeReadOff", "SafeReadPtr", "SafeReadU32",
                  "SafeReadFloat", "SafeReadVector"})
    return names


def collect_constants(patch_dir):
    """name -> 'unresolved' | 'resolved', across the whole patch."""
    state = {}
    for fn in sorted(os.listdir(patch_dir)):
        if not fn.endswith((".h", ".cpp")):
            continue
        text = open(os.path.join(patch_dir, fn), encoding="utf-8",
                    errors="replace").read()
        for m in RESOLVED_DECL.finditer(text):
            state[m.group(1)] = "resolved"
        for m in UNRESOLVED_DECL.finditer(text):
            state[m.group(1)] = "unresolved"
    return state


def seh_depth_map(lines):
    """For each line index, whether it sits inside a `__try` block.

    Textual and approximate: on seeing `__try` we start counting braces and stay
    'inside' until the matching close. Good enough to separate "wrapped in the
    file's SEH convention" from "bare", which is the distinction that matters.
    """
    inside = [False] * len(lines)
    depth = 0
    brace = 0
    for i, line in enumerate(lines):
        stripped = re.sub(r"//.*", "", line)
        if depth == 0 and re.search(r"\b__try\b", stripped):
            depth = 1
            brace = 0
        if depth:
            inside[i] = True
            brace += stripped.count("{") - stripped.count("}")
            # A __try's body closes when braces return to zero after opening.
            if brace <= 0 and "{" in "".join(
                    re.sub(r"//.*", "", l) for l in lines[:i + 1][-40:]):
                if brace < 0 or (brace == 0 and "}" in stripped):
                    depth = 0
    return inside


def audit(patch_dir, files):
    state = collect_constants(patch_dir)
    helpers = guarded_helpers(patch_dir)
    helper_call = re.compile(
        r"\b(" + "|".join(sorted(map(re.escape, helpers))) + r")\s*[<(]") \
        if helpers else None
    findings = []
    for fn in files:
        path = os.path.join(patch_dir, fn)
        if not os.path.exists(path):
            continue
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        inside = seh_depth_map(lines)
        # Which constants does this file test with Ok()?
        okguarded = set(
            re.findall(r"acc::addr::Ok\(\s*(k[A-Za-z0-9_]+)", "\n".join(lines)))
        for i, line in enumerate(lines):
            code = re.sub(r"//.*", "", line)
            if not code.strip():
                continue
            # Skip the declaration lines themselves.
            if UNRESOLVED_DECL.search(code) or RESOLVED_DECL.search(code):
                continue
            for name in IDENT.findall(code):
                if state.get(name) != "unresolved":
                    continue
                if name in okguarded:
                    kind = "guarded(Ok)"
                elif inside[i]:
                    kind = "guarded(SEH)"
                elif helper_call and helper_call.search(
                        " ".join(re.sub(r"//.*", "", l)
                                 for l in lines[max(0, i - 2):i + 1])):
                    # Passed into a helper that opens its own __try. The window
                    # covers the two preceding lines because these calls are
                    # routinely wrapped, leaving the constant on a continuation
                    # line with the callee name above it.
                    kind = "guarded(callee)"
                else:
                    kind = "UNGUARDED"
                findings.append((kind, fn, i + 1, name, line.strip()[:88]))
    return findings


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    patch_dir = sys.argv[1]
    files = sys.argv[2:] or [f for f in sorted(os.listdir(patch_dir))
                             if f.endswith(".cpp")]
    findings = audit(patch_dir, files)
    unguarded = [f for f in findings if f[0] == "UNGUARDED"]
    guarded = [f for f in findings if f[0] != "UNGUARDED"]
    print(f"# unresolved-constant uses: {len(findings)}   "
          f"UNGUARDED: {len(unguarded)}   guarded: {len(guarded)}")
    cur = None
    for kind, fn, ln, name, src in unguarded:
        if fn != cur:
            print(f"\n--- {fn}")
            cur = fn
        print(f"  {ln:5d}  {name:44s} {src}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
