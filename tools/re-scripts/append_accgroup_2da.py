# Python port of installer PriorityGroup2da.AppendAccGroup (2DA V2.b append).
# Usage: append_accgroup.py <in.2da> <out.2da>   (idempotent)
#        append_accgroup.py --verify <source.2da> <expected.2da>
import struct, sys

MAGIC = b"2DA V2.b\n"

# K1 schema (single volume/dist columns, label column empty on every row)
FLAT_ROW_K1 = {
    "priority": "0", "volume": "127", "maxplaying": "4", "interrupt": "1",
    "fadetime": "31337", "maxvolumedist": "10", "minvolumedist": "20",
    "playbackvariance": "0",
}
SPATIAL_ROW_K1 = {
    "priority": "0", "volume": "127", "maxplaying": "4", "interrupt": "1",
    "fadetime": "31338", "maxvolumedist": "1", "minvolumedist": "8",
    "playbackvariance": "0",
}
# K2 schema (Aspyr): volume_pc/volume_xbox + per-platform dist columns,
# label column carries a row name. Same semantics as the K1 rows.
FLAT_ROW_K2 = {
    "label": "Acc_Cue_Flat",
    "priority": "0", "volume_pc": "127", "volume_xbox": "127",
    "maxplaying": "4", "interrupt": "1", "fadetime": "31337",
    "maxvolumedist_pc": "10", "minvolumedist_pc": "20",
    "maxvolumedist_xbox": "10", "minvolumedist_xbox": "20",
    "playbackvariance": "0",
}
SPATIAL_ROW_K2 = {
    "label": "Acc_Cue_Spatial",
    "priority": "0", "volume_pc": "127", "volume_xbox": "127",
    "maxplaying": "4", "interrupt": "1", "fadetime": "31338",
    "maxvolumedist_pc": "1", "minvolumedist_pc": "8",
    "maxvolumedist_xbox": "1", "minvolumedist_xbox": "8",
    "playbackvariance": "0",
}


def acc_rows_for(t):
    if any(c.lower() == "volume_pc" for c in t.cols):
        return [("31337", FLAT_ROW_K2), ("31338", SPATIAL_ROW_K2)]
    return [("31337", FLAT_ROW_K1), ("31338", SPATIAL_ROW_K1)]


class Table:
    pass


def parse(buf):
    if not buf.startswith(MAGIC):
        raise ValueError("not a 2DA V2.b file")
    p = len(MAGIC)
    hdr_end = buf.index(0, p)
    cols = buf[p:hdr_end].decode("ascii").split("\t")
    if cols and cols[-1] == "":
        cols.pop()
    p = hdr_end + 1
    row_count = struct.unpack_from("<i", buf, p)[0]
    p += 4
    labels = []
    for _ in range(row_count):
        t = buf.index(9, p)  # '\t'
        labels.append(buf[p:t].decode("ascii"))
        p = t + 1
    ncells = row_count * len(cols)
    offsets = list(struct.unpack_from("<%dH" % ncells, buf, p))
    p += 2 * ncells
    data_size = struct.unpack_from("<H", buf, p)[0]
    p += 2
    if p + data_size > len(buf):
        raise ValueError("2da data block runs past EOF")
    t = Table()
    t.cols, t.row_count, t.labels = cols, row_count, labels
    t.offsets, t.data = offsets, bytearray(buf[p:p + data_size])
    return t


def build(t):
    out = bytearray(MAGIC)
    out += ("\t".join(t.cols) + "\t").encode("ascii")
    out.append(0)
    out += struct.pack("<i", t.row_count)
    for lbl in t.labels:
        out += lbl.encode("ascii") + b"\t"
    for off in t.offsets:
        out += struct.pack("<H", off)
    out += struct.pack("<H", len(t.data))
    out += t.data
    return bytes(out)


def cell(t, row, col_idx):
    off = t.offsets[row * len(t.cols) + col_idx]
    return t.data[off:t.data.index(0, off)].decode("ascii")


def fadetime_col(t):
    for i, c in enumerate(t.cols):
        if c.lower() == "fadetime":
            return i
    return -1


def has_sentinel(t, fade_col, sentinel):
    return fade_col >= 0 and any(
        cell(t, r, fade_col) == sentinel for r in range(t.row_count))


def append_row(t, row):
    new_idx = t.row_count
    for c in t.cols:
        val = next((v for k, v in row.items() if k == c.lower()), "")
        if c.lower() == "label" and val == "":
            # K1 convention: every row's label cell is the shared empty
            # string at data offset 0 (matches the installer byte-for-byte).
            t.offsets.append(0)
            continue
        off = len(t.data)
        t.data += val.encode("ascii") + b"\x00"
        t.offsets.append(off)
    t.row_count += 1
    t.labels.append(str(new_idx))


def append_accgroup(source):
    t = parse(source)
    fc = fadetime_col(t)
    if fc < 0:
        raise ValueError("no FadeTime column")
    rows = acc_rows_for(t)
    added = False
    for sentinel, row in rows:
        if not has_sentinel(t, fc, sentinel):
            append_row(t, row)
            added = True
    if not added:
        return source
    out = build(t)
    t2 = parse(out)
    fc2 = fadetime_col(t2)
    for sentinel, _ in rows:
        if not has_sentinel(t2, fc2, sentinel):
            raise ValueError("self-check failed")
    return out


if __name__ == "__main__":
    if sys.argv[1] == "--verify":
        src = open(sys.argv[2], "rb").read()
        expected = open(sys.argv[3], "rb").read()
        got = append_accgroup(src)
        print("VERIFY:", "IDENTICAL" if got == expected else
              "MISMATCH (len got=%d expected=%d)" % (len(got), len(expected)))
        sys.exit(0 if got == expected else 1)
    src = open(sys.argv[1], "rb").read()
    out = append_accgroup(src)
    open(sys.argv[2], "wb").write(out)
    print("written %s (%d -> %d bytes%s)" % (
        sys.argv[2], len(src), len(out),
        ", unchanged" if out == src else ""))
