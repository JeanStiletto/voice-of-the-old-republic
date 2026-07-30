using System.Text;

namespace Kdev;

/// <summary>
/// Minimal PE32 header reader, shared by <c>kdev dump-text</c> and
/// <c>kdev sigscan</c>.
///
/// Deliberately tiny: we only need the image base, the section table and the
/// link timestamp. Two things it must get right, because both commands depend
/// on them:
///
///  * <see cref="RvaToOffset"/> takes an <paramref name="imageAligned"/> flag.
///    A file on disk stores each section at PointerToRawData; a mapped image
///    stores it at VirtualAddress. Reading a memory dump with file-offset
///    logic silently yields the wrong bytes rather than an error, so the
///    caller must state which it has.
///  * Sections are matched by span = max(VirtualSize, SizeOfRawData). The two
///    disagree on this engine (.text is 0x33B1D0 virtual vs 0x33C000 raw), and
///    using only one of them drops addresses near the boundary.
/// </summary>
public sealed record PeSection(
    string Name,
    uint VirtualAddress,
    uint VirtualSize,
    uint RawSize,
    uint RawPointer);

public sealed class PeInfo
{
    public required uint ImageBase { get; init; }
    public required uint SizeOfImage { get; init; }
    public required uint Timestamp { get; init; }
    public required IReadOnlyList<PeSection> Sections { get; init; }

    public DateTime TimestampUtc => DateTimeOffset.FromUnixTimeSeconds(Timestamp).UtcDateTime;

    /// <summary>
    /// Parse the headers out of <paramref name="buf"/>. Pass
    /// <paramref name="imageBaseOverride"/> when reading a mapped image whose
    /// actual base could differ from the header value (it does not for this
    /// exe — no ASLR — but being explicit keeps the dump self-describing).
    /// </summary>
    public static PeInfo Parse(ReadOnlySpan<byte> buf, ulong? imageBaseOverride = null)
    {
        if (buf.Length < 0x40) throw new InvalidDataException("buffer too small for a DOS header");
        if (buf[0] != (byte)'M' || buf[1] != (byte)'Z')
            throw new InvalidDataException("missing MZ signature");

        int lfanew = BitConverter.ToInt32(buf[0x3C..]);
        if (lfanew <= 0 || lfanew + 0x100 > buf.Length)
            throw new InvalidDataException($"implausible e_lfanew 0x{lfanew:X}");
        if (buf[lfanew] != (byte)'P' || buf[lfanew + 1] != (byte)'E' ||
            buf[lfanew + 2] != 0 || buf[lfanew + 3] != 0)
            throw new InvalidDataException("missing PE signature");

        int coff = lfanew + 4;
        ushort numSections = BitConverter.ToUInt16(buf[(coff + 2)..]);
        uint timestamp = BitConverter.ToUInt32(buf[(coff + 4)..]);
        ushort sizeOpt = BitConverter.ToUInt16(buf[(coff + 16)..]);

        int opt = coff + 20;
        ushort magic = BitConverter.ToUInt16(buf[opt..]);
        if (magic != 0x10B)
            throw new InvalidDataException($"not PE32 (optional header magic 0x{magic:X4})");

        uint headerImageBase = BitConverter.ToUInt32(buf[(opt + 28)..]);
        uint sizeOfImage = BitConverter.ToUInt32(buf[(opt + 56)..]);

        int secStart = opt + sizeOpt;
        var sections = new List<PeSection>(numSections);
        for (int i = 0; i < numSections; i++)
        {
            int s = secStart + (i * 40);
            if (s + 40 > buf.Length) break;
            string name = Encoding.ASCII.GetString(buf.Slice(s, 8)).TrimEnd('\0');
            sections.Add(new PeSection(
                Name: name,
                VirtualSize: BitConverter.ToUInt32(buf[(s + 8)..]),
                VirtualAddress: BitConverter.ToUInt32(buf[(s + 12)..]),
                RawSize: BitConverter.ToUInt32(buf[(s + 16)..]),
                RawPointer: BitConverter.ToUInt32(buf[(s + 20)..])));
        }

        return new PeInfo
        {
            ImageBase = imageBaseOverride.HasValue ? (uint)imageBaseOverride.Value : headerImageBase,
            SizeOfImage = sizeOfImage,
            Timestamp = timestamp,
            Sections = sections,
        };
    }

    public PeSection? SectionForRva(uint rva)
    {
        foreach (var s in Sections)
        {
            uint span = Math.Max(s.VirtualSize, s.RawSize);
            if (rva >= s.VirtualAddress && rva < s.VirtualAddress + span) return s;
        }
        return null;
    }

    public PeSection? SectionNamed(string name) =>
        Sections.FirstOrDefault(s => s.Name == name);

    /// <summary>
    /// Buffer offset for a virtual address. <paramref name="imageAligned"/>
    /// must be true for a memory dump and false for an on-disk file — see the
    /// class remarks. Returns -1 when the address is outside every section, or
    /// (for a file) inside virtual padding with no bytes backing it.
    /// </summary>
    public int VaToOffset(uint va, bool imageAligned)
    {
        if (va < ImageBase) return -1;
        uint rva = va - ImageBase;
        var s = SectionForRva(rva);
        if (s == null) return -1;
        uint delta = rva - s.VirtualAddress;
        if (imageAligned) return (int)(s.VirtualAddress + delta);
        if (delta >= s.RawSize) return -1;
        return (int)(s.RawPointer + delta);
    }

    public uint OffsetToVa(int offset, bool imageAligned)
    {
        if (imageAligned) return (uint)(ImageBase + (uint)offset);
        foreach (var s in Sections)
        {
            if (offset >= s.RawPointer && offset < s.RawPointer + s.RawSize)
                return (uint)(ImageBase + s.VirtualAddress + ((uint)offset - s.RawPointer));
        }
        return 0;
    }
}
