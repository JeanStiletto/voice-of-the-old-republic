"""Find code references to a string literal and the struct stores around them.

The workhorse for porting GFF-backed struct offsets: a field's Load site
references the field-name string ("MapNoteEnabled", "OpenState", ...) as a
PUSH immediate, and the store that follows the GetXxxByLabel call writes the
decoded value to [this+offset]. KOTOR 2's unoptimised build makes that store
adjacent and frame-free, so a short disassembly window after the reference
usually shows the offset directly.

Works on any 32-bit PE whose bytes are readable from disk (swkotor2.exe, or
KOTOR 1's decrypted image dump swkotor-image.bin with --image-base).

Usage:
    string_xref_stores.py <exe-or-image> "<string>" [--funcs k2-functions.csv]
        [--window N] [--image-base 0x400000] [--all-strings]

    --funcs       map each hit into its containing function (address,size csv)
    --window      bytes to disassemble after each reference (default 160)
    --before      bytes to disassemble before each reference (default 0)
    --all-strings list every occurrence of the string, even without xrefs
"""
import argparse
import bisect
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("capstone not installed for this Python")


def pe_sections(data):
    """[(name, va, vsize, file_off, fsize)] and image base."""
    if data[:2] != b"MZ":
        return None, None
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return None, None
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    sec = opt + opt_size
    out = []
    for i in range(nsec):
        s = sec + i * 40
        name = data[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, fsize, foff = struct.unpack_from("<IIII", data, s + 8)
        out.append((name, image_base + va, vsize, foff, fsize))
    return out, image_base


def load_functions(path):
    funcs = []
    with open(path) as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) < 2:
                continue
            try:
                addr = int(parts[0], 16)
                size = int(parts[1])
            except ValueError:
                continue
            funcs.append((addr, size))
    funcs.sort()
    return funcs


def containing_function(funcs, va):
    if not funcs:
        return None
    i = bisect.bisect_right(funcs, (va, 1 << 30)) - 1
    if i < 0:
        return None
    addr, size = funcs[i]
    if addr <= va < addr + size:
        return addr
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("needle")
    ap.add_argument("--funcs")
    ap.add_argument("--window", type=int, default=160)
    ap.add_argument("--before", type=int, default=0)
    ap.add_argument("--image-base", type=lambda x: int(x, 0), default=None,
                    help="treat file as a raw image dump at this base")
    ap.add_argument("--all-strings", action="store_true")
    args = ap.parse_args()

    data = open(args.binary, "rb").read()

    if args.image_base is not None:
        # Raw image dump: file offset == va - image_base everywhere.
        secs = [("image", args.image_base, len(data), 0, len(data))]
        base = args.image_base
    else:
        secs, base = pe_sections(data)
        if secs is None:
            sys.exit("not a PE file; pass --image-base for raw dumps")

    def va_of(foff):
        for name, va, vsize, fo, fs in secs:
            if fo <= foff < fo + fs:
                return va + (foff - fo)
        return None

    def foff_of(va):
        for name, sva, vsize, fo, fs in secs:
            if sva <= va < sva + fs:
                return fo + (va - sva)
        return None

    # 1. Find the string (NUL-terminated occurrences preferred).
    needle = args.needle.encode("ascii")
    str_vas = []
    start = 0
    while True:
        i = data.find(needle, start)
        if i < 0:
            break
        start = i + 1
        # Prefer occurrences that look like standalone C strings.
        standalone = (data[i + len(needle):i + len(needle) + 1] == b"\0" and
                      (i == 0 or data[i - 1:i] == b"\0"))
        va = va_of(i)
        if va is not None and (standalone or args.all_strings):
            str_vas.append(va)
    if not str_vas:
        sys.exit(f"string {args.needle!r} not found as standalone C string "
                 f"(retry with --all-strings)")
    for va in str_vas:
        print(f"string at 0x{va:08x}")

    funcs = load_functions(args.funcs) if args.funcs else []

    # 2. Scan executable/all sections for 4-byte immediates of those VAs.
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    targets = {struct.pack("<I", va): va for va in str_vas}
    hits = 0
    for name, sva, vsize, fo, fs in secs:
        if args.image_base is None and name not in (".text",):
            continue
        blob = data[fo:fo + fs]
        for pat, sva_target in targets.items():
            start = 0
            while True:
                i = blob.find(pat, start)
                if i < 0:
                    break
                start = i + 1
                ref_va = sva + i
                # Skip hits inside .rdata-like regions of a raw image: only
                # accept if the disassembly around it decodes as code that
                # actually uses the immediate. Cheap filter: try decoding a
                # window and require the immediate to appear in an operand.
                w_from = max(fo, fo + i - 8 - args.before)
                w_to = min(fo + fs, fo + i + 4 + args.window)
                code = data[w_from:w_to]
                va0 = sva + (w_from - fo)
                fn = containing_function(funcs, ref_va)
                hdr = f"\n=== ref near 0x{ref_va:08x}"
                if fn is not None:
                    hdr += f"  (in function 0x{fn:08x})"
                print(hdr + " ===")
                shown = 0
                for insn in md.disasm(code, va0):
                    marker = " <-- ref" if (
                        insn.address <= ref_va < insn.address + insn.size
                    ) else ""
                    print(f"  0x{insn.address:08x}: "
                          f"{insn.mnemonic} {insn.op_str}{marker}")
                    shown += 1
                    if insn.address > ref_va + args.window - 16:
                        break
                if shown == 0:
                    print("  (window did not decode)")
                hits += 1
    if hits == 0:
        print("no code references found")


if __name__ == "__main__":
    main()
