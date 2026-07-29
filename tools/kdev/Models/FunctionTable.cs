using System.Xml;

namespace Kdev.Models;

/// <summary>
/// Address → function-name resolver, built from a Ghidra XML export.
///
/// Stream-parses every &lt;FUNCTION ENTRY_POINT="..." NAME="..."&gt; element and
/// its first &lt;ADDRESS_RANGE START="..." END="..."/&gt; child, builds a sorted
/// array indexed by start address, and binary-searches it on lookup.
///
/// The Ghidra export has ~24K functions in 31MB of XML. End-to-end parse time
/// is around 2–3s on first invocation; data resident is ~1–2MB. No caching —
/// the tool is interactive (one run per crash investigation), and a stale-cache
/// invalidation path would be more code than the parse it saves.
/// </summary>
public sealed class FunctionTable
{
    public readonly record struct Entry(uint Start, uint End, string Name);

    private readonly Entry[] _entries;
    public uint ImageBase { get; }
    public int Count => _entries.Length;

    private FunctionTable(Entry[] entries, uint imageBase)
    {
        _entries = entries;
        ImageBase = imageBase;
    }

    public static FunctionTable LoadFromGhidraXml(string xmlPath)
    {
        var entries = new List<Entry>(capacity: 32768);
        uint imageBase = 0;

        using var reader = XmlReader.Create(xmlPath, new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Ignore,   // skip the inline DTD
            IgnoreComments = true,
            IgnoreWhitespace = true,
        });

        // State carried across element boundaries: when we open a <FUNCTION>
        // we record its entry+name; the next <ADDRESS_RANGE> child supplies
        // the end. A function may have multiple ranges (Ghidra splits some
        // across non-contiguous chunks); we keep the first because that's
        // what overlaps the entry point. Subsequent ranges are dropped.
        uint pendingStart = 0;
        string? pendingName = null;
        bool haveRangeForFunction = false;

        while (reader.Read())
        {
            if (reader.NodeType != XmlNodeType.Element) continue;

            switch (reader.Name)
            {
                case "PROGRAM":
                {
                    var ib = reader.GetAttribute("IMAGE_BASE");
                    if (ib != null && uint.TryParse(ib, System.Globalization.NumberStyles.HexNumber,
                                                    System.Globalization.CultureInfo.InvariantCulture,
                                                    out var parsed))
                    {
                        imageBase = parsed;
                    }
                    break;
                }

                case "FUNCTION":
                {
                    var ep = reader.GetAttribute("ENTRY_POINT");
                    var name = reader.GetAttribute("NAME");
                    if (ep != null && name != null &&
                        uint.TryParse(ep, System.Globalization.NumberStyles.HexNumber,
                                      System.Globalization.CultureInfo.InvariantCulture,
                                      out var startAddr))
                    {
                        pendingStart = startAddr;
                        pendingName = name;
                        haveRangeForFunction = false;
                    }
                    else
                    {
                        pendingName = null;
                    }
                    break;
                }

                case "ADDRESS_RANGE":
                {
                    if (pendingName == null || haveRangeForFunction) break;
                    var endAttr = reader.GetAttribute("END");
                    if (endAttr != null &&
                        uint.TryParse(endAttr, System.Globalization.NumberStyles.HexNumber,
                                      System.Globalization.CultureInfo.InvariantCulture,
                                      out var endAddr))
                    {
                        entries.Add(new Entry(pendingStart, endAddr, pendingName));
                        haveRangeForFunction = true;
                    }
                    break;
                }
            }
        }

        var arr = entries.ToArray();
        Array.Sort(arr, (a, b) => a.Start.CompareTo(b.Start));
        return new FunctionTable(arr, imageBase);
    }

    /// <summary>
    /// Strict containment lookup: returns the entry whose [Start, End] range
    /// contains the address, or null otherwise. Used by code-shape filters
    /// (the ESP-scan) where we only want true matches, not nearby names.
    /// </summary>
    public Entry? Find(uint address)
    {
        var e = FindPreceding(address);
        if (e is null) return null;
        return address <= e.Value.End ? e : null;
    }

    /// <summary>
    /// Closest preceding function — the entry with the largest Start address
    /// &lt;= the query. Used for "closest-function+offset" naming even when
    /// the address falls in a gap (Ghidra often doesn't classify thunks /
    /// alignment padding / data interleaved with code). Distinguished from
    /// Find() by callers that want every code-region address named.
    /// </summary>
    public Entry? FindPreceding(uint address)
    {
        int lo = 0, hi = _entries.Length - 1, best = -1;
        while (lo <= hi)
        {
            int mid = lo + ((hi - lo) >> 1);
            if (_entries[mid].Start <= address) { best = mid; lo = mid + 1; }
            else hi = mid - 1;
        }
        return best < 0 ? null : _entries[best];
    }

    /// <summary>
    /// Resolve an address to "Name+0xN" — falls back to closest-preceding
    /// when not strictly inside a function range, with "+~0xN" to flag the
    /// address as outside the recognized body. Returns "&lt;not in any
    /// function&gt;" only when there's no preceding function at all
    /// (i.e., address below the lowest mapped function).
    /// </summary>
    public string Resolve(uint address)
    {
        var strict = Find(address);
        if (strict is { } s)
        {
            var off = address - s.Start;
            return off == 0 ? s.Name : $"{s.Name}+0x{off:x}";
        }
        var near = FindPreceding(address);
        if (near is null) return "<not in any function>";
        var n = near.Value;
        var nearOff = address - n.Start;
        return $"{n.Name}+~0x{nearOff:x}";  // ~ = approximate, outside recognized range
    }
}
