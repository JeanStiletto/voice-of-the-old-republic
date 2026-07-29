using System.Text;

namespace Kdev;

// ----------------------------------------------------------------------
// Minimal TLK reader, split out of Commands/CombatStringsExtractCommand.cs
// by the Phase-1 structure pass (refactoring candidate 25a). It sits at
// Core/ level for the same reason PeInfo and Signatures do: it is an
// engine-format reader, not command logic, and a second consumer would
// otherwise be tempted to hand-roll one.
//
// Minimal TLK reader. Format: 12-byte header, N x 40-byte String_Data
// table, then a packed string-data blob. Strings are CP-1252 bytes;
// we keep them as raw byte arrays so the C++ emitter can produce
// byte-faithful \xNN escapes.
// ----------------------------------------------------------------------
internal sealed class TlkFile
{
    public int LanguageId { get; private set; }
    public int StringCount { get; private set; }

    private byte[] _file = null!;
    private int[] _offsets = null!;
    private int[] _sizes = null!;
    private int _entriesOffset;

    public static TlkFile Load(string path)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length < 20) throw new InvalidDataException("file too short");
        if (Encoding.ASCII.GetString(bytes, 0, 4) != "TLK ")
            throw new InvalidDataException("not a TLK file (bad magic)");

        var t = new TlkFile { _file = bytes };
        t.LanguageId = BitConverter.ToInt32(bytes, 8);
        t.StringCount = BitConverter.ToInt32(bytes, 12);
        t._entriesOffset = BitConverter.ToInt32(bytes, 16);

        t._offsets = new int[t.StringCount];
        t._sizes = new int[t.StringCount];
        const int headerSize = 20;
        const int entrySize = 40;
        for (int i = 0; i < t.StringCount; i++)
        {
            int p = headerSize + i * entrySize;
            // skip Flags(4) + ResRef(16) + VolVar(4) + PitchVar(4) = 28
            t._offsets[i] = BitConverter.ToInt32(bytes, p + 28);
            t._sizes[i] = BitConverter.ToInt32(bytes, p + 32);
            // SoundLength float at p+36 — ignored.
        }
        return t;
    }

    public byte[]? Get(int strref)
    {
        if (strref < 0 || strref >= StringCount) return null;
        int sz = _sizes[strref];
        if (sz <= 0) return Array.Empty<byte>();
        var result = new byte[sz];
        Array.Copy(_file, _entriesOffset + _offsets[strref], result, 0, sz);
        return result;
    }
}
