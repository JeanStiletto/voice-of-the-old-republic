using System.Text.RegularExpressions;

namespace Kdev;

/// <summary>
/// One engine address the patch depends on, and where it came from.
/// </summary>
/// <param name="Va">Absolute virtual address as written in our sources.</param>
/// <param name="Name">Constant name, or the hook's handler function.</param>
/// <param name="Source">Repo-relative file it was read from.</param>
/// <param name="Line">1-based line number, so a report is clickable.</param>
/// <param name="OriginalBytes">
/// Only set for hooks.toml detours, which record the bytes the patcher
/// verifies before writing. Gives sigscan a free correctness check: the
/// generated signature must agree with the hand-recorded bytes.
/// </param>
public sealed record EngineAddress(
    uint Va,
    string Name,
    string Source,
    int Line,
    byte[]? OriginalBytes = null);

/// <summary>
/// Harvests every hardcoded engine address out of the patch sources.
///
/// Two shapes exist and both matter:
///  * hooks.toml `[[hooks]] address = 0x...` — the 25 detour sites. These fail
///    safe (the patcher's VerifyBytes refuses on a mismatch and the hook goes
///    inert), so a wrong answer here is a dead feature.
///  * address literals in the C++ — engine functions we reinterpret_cast and
///    CALL, plus a handful of globals. These do NOT fail safe: a wrong address
///    calls into the middle of some other function with the wrong calling
///    convention and crashes. That asymmetry is why sigscan reports ambiguity
///    as failure rather than guessing.
///
/// Why the C++ side is a literal sweep and not a declaration matcher
/// -----------------------------------------------------------------
/// It used to be a declaration matcher, keyed on
/// `^(constexpr|const) uintptr_t NAME = 0x...;`. That silently missed every
/// address written in a shape nobody had thought of, and the consequence was
/// not a warning — it was absence. An address the harvester cannot see gets no
/// signature, gets no rebase-table entry, and is never reported as unresolved,
/// so it keeps its reference value on a rebased build and calls into the middle
/// of an unrelated function.
///
/// Twelve addresses were lost that way (found 2026-07-29): `static constexpr
/// uintptr_t` (leading `static`), `constexpr std::uintptr_t` (`std::` prefix),
/// and inline `reinterpret_cast&lt;PFN&gt;(0x...)`, which is not a declaration
/// at all. The wrapped form `= acc::addr::R(0x...);` was invisible too, and that
/// is worse than it sounds: it is the CORRECT form, so regenerating the table
/// would have dropped nearly every address it currently holds.
///
/// So the rule is inverted. Every hex literal in the image's VA range counts as
/// an address unless a marker says otherwise; declaration shapes are matched
/// only to recover a good NAME for the report. A new declaration style can no
/// longer hide. The cost is the occasional false positive — a non-address
/// constant that happens to land in the range — opted out per line with a
/// `kdev-sigscan: ignore` comment or per file with `kdev-sigscan: ignore-file`.
/// A false positive is a noisy report line; a false negative is a crash on
/// somebody else's machine.
/// </summary>
public static class EngineAddresses
{
    /// <summary>
    /// Coarse VA window for the literal sweep. The image spans
    /// 0x00400000..0x008C3000; anything outside cannot be an engine address, and
    /// the bound keeps PE timestamps, bit masks and colour constants out of the
    /// set. Final section classification (.text vs data vs outside image) is
    /// SigScanCommand's job — it has the PE, this does not.
    /// </summary>
    private const uint VaLow = 0x00400000;
    private const uint VaHigh = 0x00900000;

    /// <summary>Opt-out markers, matched against the RAW (uncommented) line.</summary>
    private const string IgnoreLine = "kdev-sigscan: ignore";
    private const string IgnoreFile = "kdev-sigscan: ignore-file";

    // A named address declaration. Permissive on everything that is not the
    // address itself:
    //   [static|inline]* [constexpr|const] [std::]uintptr_t NAME = <value>;
    // where <value> is 0x... or acc::addr::R(0x...) / addr::R(0x...) / R(0x...).
    private static readonly Regex DeclRe = new(
        @"^\s*(?:(?:static|inline)\s+)*(?:constexpr|const)\s+(?:std::)?uintptr_t\s+" +
        @"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*" +
        @"(?:(?:acc::)?(?:addr::)?R\(\s*)?(0x[0-9a-fA-F]+)\s*\)?\s*;",
        RegexOptions.Compiled);

    // The same declaration with the value wrapped onto the next line:
    //     constexpr uintptr_t kAddrRemoveLastAction =
    //         0x004d37b0;  // CSWSCombatRound::RemoveLastAction
    private static readonly Regex PendingDeclRe = new(
        @"^\s*(?:(?:static|inline)\s+)*(?:constexpr|const)\s+(?:std::)?uintptr_t\s+" +
        @"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:(?:acc::)?(?:addr::)?R\(\s*)?$",
        RegexOptions.Compiled);

    private static readonly Regex ContinuationRe = new(
        @"^\s*(0x[0-9a-fA-F]+)\s*\)?\s*;",
        RegexOptions.Compiled);

    private static readonly Regex HexLiteralRe = new(
        @"0x[0-9a-fA-F]+",
        RegexOptions.Compiled);

    /// <summary>
    /// Scan <paramref name="sourceDir"/> (patches/Accessibility) plus its
    /// hooks.toml. Comment text is stripped first: the sources quote addresses
    /// in prose constantly and those are not real dependencies.
    /// </summary>
    public static List<EngineAddress> Collect(string sourceDir, string repoRoot)
    {
        var result = new List<EngineAddress>();

        foreach (var path in Directory.EnumerateFiles(sourceDir, "*.*", SearchOption.AllDirectories)
                     .Where(p => p.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
                                 p.EndsWith(".h", StringComparison.OrdinalIgnoreCase))
                     .OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
        {
            var lines = File.ReadAllLines(path);
            string rel = Path.GetRelativePath(repoRoot, path);
            if (lines.Any(l => l.Contains(IgnoreFile, StringComparison.Ordinal))) { continue; }

            string? pendingName = null;
            bool inBlockComment = false;

            for (int i = 0; i < lines.Length; i++)
            {
                string raw = lines[i];
                string line = StripComments(raw, ref inBlockComment);
                if (string.IsNullOrWhiteSpace(line)) { continue; }
                if (raw.Contains(IgnoreLine, StringComparison.Ordinal)) { pendingName = null; continue; }

                var m = DeclRe.Match(line);
                if (m.Success && TryVa(m.Groups[2].Value, out uint declVa))
                {
                    result.Add(new EngineAddress(declVa, m.Groups[1].Value, rel, i + 1));
                    pendingName = null;
                    continue;
                }

                if (pendingName != null)
                {
                    var c = ContinuationRe.Match(line);
                    if (c.Success && TryVa(c.Groups[1].Value, out uint contVa))
                    {
                        result.Add(new EngineAddress(contVa, pendingName, rel, i + 1));
                        pendingName = null;
                        continue;
                    }
                    pendingName = null;
                }

                var p = PendingDeclRe.Match(line);
                if (p.Success) { pendingName = p.Groups[1].Value; continue; }

                // Catch-all: any in-range literal that is not part of a named
                // declaration. This is the branch that would have caught the
                // twelve — inline reinterpret_cast<PFN>(0x...) and friends.
                foreach (Match h in HexLiteralRe.Matches(line))
                {
                    if (!TryVa(h.Value, out uint va)) { continue; }
                    result.Add(new EngineAddress(va, "(inline) " + Snippet(line), rel, i + 1));
                }
            }
        }

        string hooksToml = Path.Combine(sourceDir, "hooks.toml");
        if (File.Exists(hooksToml))
        {
            result.AddRange(ParseHooks(hooksToml, Path.GetRelativePath(repoRoot, hooksToml)));
        }

        return result;
    }

    // A kXrefTable row in engine_rebase.cpp: { 0xREFERENCE, 0xTARGET },
    private static readonly Regex XrefRowRe = new(
        @"^\s*\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\}\s*,",
        RegexOptions.Compiled);

    /// <summary>
    /// The hand-resolved supplement in <c>engine_rebase.cpp</c>'s
    /// <c>kXrefTable</c>: reference address to target address, for the few
    /// functions sigscan cannot place by their own bytes (identical forwarding
    /// thunks, or a near-twin sharing the whole prologue). They were resolved by
    /// call-site cross-reference instead.
    ///
    /// sigscan needs to know about them for one reason: without it, those
    /// addresses report as unresolved on every single run, forever. A warning
    /// that is always on is a warning nobody reads — and this command's whole
    /// job is to make a genuinely missing address impossible to overlook.
    /// </summary>
    public static Dictionary<uint, uint> CollectHandResolved(string sourceDir)
    {
        var map = new Dictionary<uint, uint>();
        string path = Path.Combine(sourceDir, "engine_rebase.cpp");
        if (!File.Exists(path)) { return map; }

        bool inTable = false;
        bool inBlockComment = false;
        foreach (string raw in File.ReadAllLines(path))
        {
            string line = StripComments(raw, ref inBlockComment);
            if (line.Contains("kXrefTable", StringComparison.Ordinal)) { inTable = true; continue; }
            if (!inTable) { continue; }
            if (line.Contains("};", StringComparison.Ordinal)) { break; }

            var m = XrefRowRe.Match(line);
            if (m.Success &&
                uint.TryParse(m.Groups[1].Value.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                              null, out uint reference) &&
                uint.TryParse(m.Groups[2].Value.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                              null, out uint target))
            {
                map[reference] = target;
            }
        }
        return map;
    }

    /// <summary>
    /// Parse a hex literal and keep it only if it lands in the image's VA
    /// window. Over-long literals (a 64-bit mask, say) overflow the parse and
    /// are rejected rather than throwing.
    /// </summary>
    private static bool TryVa(string hex, out uint va)
    {
        va = 0;
        if (!ulong.TryParse(hex.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                            null, out ulong parsed)) { return false; }
        if (parsed < VaLow || parsed >= VaHigh) { return false; }
        va = (uint)parsed;
        return true;
    }

    /// <summary>
    /// A short one-line rendering of the code an unnamed address came from, so
    /// the sigscan report says something more useful than "(inline)".
    /// </summary>
    private static string Snippet(string code)
    {
        string s = Regex.Replace(code.Trim(), @"\s+", " ");
        return s.Length <= 70 ? s : s[..67] + "...";
    }

    /// <summary>
    /// Blank out // and /* */ comment text so quoted addresses in prose are not
    /// mistaken for dependencies. Not a full C++ lexer — it does not track
    /// string literals — but the patterns we match require a statement shape
    /// that cannot occur inside a string in this codebase.
    /// </summary>
    private static string StripComments(string line, ref bool inBlockComment)
    {
        var sb = new System.Text.StringBuilder(line.Length);
        for (int i = 0; i < line.Length; i++)
        {
            if (inBlockComment)
            {
                if (i + 1 < line.Length && line[i] == '*' && line[i + 1] == '/')
                {
                    inBlockComment = false;
                    i++;
                }
                continue;
            }
            if (i + 1 < line.Length && line[i] == '/' && line[i + 1] == '/') break;
            if (i + 1 < line.Length && line[i] == '/' && line[i + 1] == '*')
            {
                inBlockComment = true;
                i++;
                continue;
            }
            sb.Append(line[i]);
        }
        return sb.ToString();
    }

    private static List<EngineAddress> ParseHooks(string path, string rel)
    {
        var hooks = new List<EngineAddress>();
        var lines = File.ReadAllLines(path);

        uint? addr = null;
        string? fn = null;
        byte[]? orig = null;
        int declLine = 0;

        void Flush()
        {
            if (addr.HasValue)
                hooks.Add(new EngineAddress(addr.Value, fn ?? "(unnamed hook)", rel, declLine, orig));
            addr = null; fn = null; orig = null;
        }

        for (int i = 0; i < lines.Length; i++)
        {
            string t = lines[i].Trim();
            if (t == "[[hooks]]") { Flush(); declLine = i + 1; continue; }
            if (t.StartsWith('#')) continue;

            var a = Regex.Match(t, @"^address\s*=\s*(0x[0-9a-fA-F]+)");
            if (a.Success) { addr = Convert.ToUInt32(a.Groups[1].Value, 16); declLine = i + 1; continue; }

            var f = Regex.Match(t, @"^function\s*=\s*""([^""]+)""");
            if (f.Success) { fn = f.Groups[1].Value; continue; }

            var o = Regex.Match(t, @"^original_bytes\s*=\s*\[(.*)\]");
            if (o.Success)
            {
                orig = o.Groups[1].Value
                    .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                    .Select(s => (byte)Convert.ToInt32(s, 16))
                    .ToArray();
            }
        }
        Flush();
        return hooks;
    }
}
