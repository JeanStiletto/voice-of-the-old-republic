"""Disassemble one or more functions from either game's binary.

Handles both reference formats used by this port:
  * a PE on disk (swkotor2.exe — not SteamStub-encrypted), and
  * a raw decrypted image dump (`kdev dump-text` output) whose accompanying
    .json records the image base (KOTOR 1's Steam exe is encrypted on disk).

Usage:
    disasm.py <exe-or-dump> <addr> [<addr> ...] [--len N] [--funcs functions.csv]

With --funcs, each address is disassembled to the end of its function as the
catalogue records it; otherwise --len bytes (default 0x80).
"""
import argparse
import json
import os
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("capstone not installed")


def load_image(path):
    """Return (va_of_blob_start, blob) covering the executable bytes."""
    data = open(path, "rb").read()
    if data[:2] == b"MZ":
        e = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, e + 6)[0]
        opt = struct.unpack_from("<H", data, e + 20)[0]
        base = struct.unpack_from("<I", data, e + 24 + 28)[0]
        for i in range(nsec):
            s = e + 24 + opt + i * 40
            name = data[s:s + 8].rstrip(b"\0").decode()
            vsize, va, fsize, foff = struct.unpack_from("<IIII", data, s + 8)
            if name == ".text":
                return base + va, data[foff:foff + fsize]
        sys.exit("no .text section")
    # Raw mapped-image dump: the whole image indexed by RVA, base in the json.
    meta = json.load(open(os.path.splitext(path)[0] + ".json"))
    return meta["imageBase"], data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("addrs", nargs="+", type=lambda x: int(x, 0))
    ap.add_argument("--len", dest="length", type=lambda x: int(x, 0), default=0x80)
    ap.add_argument("--funcs")
    args = ap.parse_args()

    va0, blob = load_image(args.binary)
    sizes = {}
    if args.funcs:
        for line in open(args.funcs):
            try:
                a, sz = line.strip().split(",")
                sizes[int(a, 16)] = int(sz)
            except ValueError:
                continue

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for addr in args.addrs:
        n = sizes.get(addr, args.length)
        off = addr - va0
        if off < 0 or off >= len(blob):
            print(f"=== 0x{addr:08x}: outside image ===")
            continue
        print(f"=== 0x{addr:08x} ({n} bytes) ===")
        for ins in md.disasm(blob[off:off + n], addr):
            print(f"  0x{ins.address:08x}  {ins.bytes.hex():<20}  "
                  f"{ins.mnemonic} {ins.op_str}")
        print()


if __name__ == "__main__":
    main()
