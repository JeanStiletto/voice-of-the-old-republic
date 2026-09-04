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

    // A call into the per-game address layer (engine_rebase.h). These are the
    // ONE shape that carries addresses for BOTH games on a single line, so the
    // plain literal sweep must not see them: it would harvest the KOTOR 2 side
    // as a KOTOR 1 address, build a signature from whatever KOTOR 1 bytes
    // happen to live at that value, and either fail loudly (the pre-2026-09
    // state: every K2 arg reported not-found/build-failed against the relink)
    // or, worse, resolve it to an unrelated function. Matched and consumed
    // BEFORE the sweep; each argument goes to the harvest for its own game.
    //
    // Argument order per engine_rebase.h:
    //   R(k1) Kotor1Only(k1) TodoGlobal(k1) Kotor2Only(k2)
    //   Pick(k1, k2) PickGlobal(k1, k2)
    private static readonly Regex AddrCallRe = new(
        @"(?:acc::)?addr::(R|Pick|PickGlobal|Kotor1Only|Kotor2Only|TodoGlobal)" +
        @"\(\s*(0x[0-9a-fA-F]+|0)\s*(?:,\s*(0x[0-9a-fA-F]+|0)\s*)?\)",
        RegexOptions.Compiled);

    // Recovers the constant name when an addr-layer call sits in a normal
    // declaration, so reports say `kAddrFoo` rather than an "(inline)" snippet.
    private static readonly Regex LineDeclNameRe = new(
        @"^\s*(?:(?:static|inline)\s+)*(?:constexpr|const)\s+(?:std::)?uintptr_t\s+" +
        @"([A-Za-z_][A-Za-z0-9_]*)\s*=",
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

                // Addr-layer calls first: harvest their KOTOR 1 argument, then
                // blank the whole call so the sweep below cannot mistake the
                // KOTOR 2 argument for a KOTOR 1 address. (No addr-layer call
                // in this codebase spans lines; if one ever does, its literals
                // fall through to the sweep and fail LOUDLY as unresolvable —
                // never silently.)
                var addrCalls = AddrCallRe.Matches(line);
                if (addrCalls.Count > 0)
                {
                    var nameMatch = LineDeclNameRe.Match(line);
                    foreach (Match c in addrCalls)
                    {
                        if (c.Groups[1].Value == "Kotor2Only") { continue; }
                        if (!TryVa(c.Groups[2].Value, out uint k1Va)) { continue; }
                        string name = nameMatch.Success
                            ? nameMatch.Groups[1].Value
                            : "(inline) " + Snippet(line);
                        result.Add(new EngineAddress(k1Va, name, rel, i + 1));
                    }
                    line = AddrCallRe.Replace(line, c => new string(' ', c.Length));
                    if (string.IsNullOrWhiteSpace(line)) { continue; }
                }

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

    /// <summary>
    /// The KOTOR 2 half of the harvest: the K2-side argument of every
    /// addr-layer call (`Pick`/`PickGlobal` second argument, `Kotor2Only`
    /// argument) plus every address in kotor2.hooks.toml — hook sites AND
    /// consumed_exit_address values, which are K2 .text addresses in their own
    /// right and were exactly the kind of entry a hook-only count missed.
    ///
    /// No literal catch-all here, deliberately: a bare hex literal cannot be
    /// classified by game, and the invariant that every KOTOR 2 address is
    /// written inside an addr-layer call is enforced from the KOTOR 1 side —
    /// the K1 sweep harvests every unclaimed in-range literal as a K1 address,
    /// and a K2 address swept that way fails the K1 relink resolve loudly.
    /// (Literals above K1's VA ceiling are audited by grep; one was found and
    /// converted, menus_keymap.cpp's kAddrK2SetFilter.)
    /// </summary>
    public static List<EngineAddress> CollectKotor2(string sourceDir, string repoRoot)
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

            bool inBlockComment = false;
            for (int i = 0; i < lines.Length; i++)
            {
                string raw = lines[i];
                string line = StripComments(raw, ref inBlockComment);
                if (string.IsNullOrWhiteSpace(line)) { continue; }
                if (raw.Contains(IgnoreLine, StringComparison.Ordinal)) { continue; }

                var addrCalls = AddrCallRe.Matches(line);
                if (addrCalls.Count == 0) { continue; }
                var nameMatch = LineDeclNameRe.Match(line);
                foreach (Match c in addrCalls)
                {
                    string k2Hex = c.Groups[1].Value switch
                    {
                        "Pick" or "PickGlobal" => c.Groups[3].Success ? c.Groups[3].Value : "",
                        "Kotor2Only" => c.Groups[2].Value,
                        _ => "",  // R / Kotor1Only / TodoGlobal carry no K2 value
                    };
                    if (k2Hex.Length == 0 || !TryVaK2(k2Hex, out uint k2Va)) { continue; }
                    string name = nameMatch.Success
                        ? nameMatch.Groups[1].Value
                        : "(inline) " + Snippet(line);
                    result.Add(new EngineAddress(k2Va, name, rel, i + 1));
                }
            }
        }

        string hooksToml = Path.Combine(sourceDir, "kotor2.hooks.toml");
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
        if (hex.Length < 3) { return false; }  // a literal `0` placeholder arg
        if (!ulong.TryParse(hex.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                            null, out ulong parsed)) { return false; }
        if (parsed < VaLow || parsed >= VaHigh) { return false; }
        va = (uint)parsed;
        return true;
    }

    /// <summary>
    /// KOTOR 2's image is larger: SizeOfImage 0x6B9000 (Steam Aspyr), so VAs
    /// run to 0x00AB9000 — the .data globals around 0x00A1B000 are real and
    /// sit past KOTOR 1's ceiling. Same base, own ceiling.
    /// </summary>
    private const uint VaHighK2 = 0x00AC0000;

    private static bool TryVaK2(string hex, out uint va)
    {
        va = 0;
        if (hex.Length < 3) { return false; }
        if (!ulong.TryParse(hex.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                            null, out ulong parsed)) { return false; }
        if (parsed < VaLow || parsed >= VaHighK2) { return false; }
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
        uint? consumedExit = null;
        string? fn = null;
        byte[]? orig = null;
        int declLine = 0;
        int exitLine = 0;

        void Flush()
        {
            if (addr.HasValue)
                hooks.Add(new EngineAddress(addr.Value, fn ?? "(unnamed hook)", rel, declLine, orig));
            // A consumed exit is a .text address of its own: the wrapper JMPs
            // there on the consume path, so on a rebased build it must be
            // resolved exactly like a hook site. It gets no OriginalBytes —
            // the patcher writes nothing there, so there is nothing to verify
            // against hooks.toml — but it does get a signature and a report row.
            if (consumedExit.HasValue)
                hooks.Add(new EngineAddress(consumedExit.Value,
                    (fn ?? "(unnamed hook)") + " (consumed exit)", rel, exitLine));
            addr = null; consumedExit = null; fn = null; orig = null;
        }

        for (int i = 0; i < lines.Length; i++)
        {
            string t = lines[i].Trim();
            if (t == "[[hooks]]") { Flush(); declLine = i + 1; continue; }
            if (t.StartsWith('#')) continue;

            var a = Regex.Match(t, @"^address\s*=\s*(0x[0-9a-fA-F]+)");
            if (a.Success) { addr = Convert.ToUInt32(a.Groups[1].Value, 16); declLine = i + 1; continue; }

            var ce = Regex.Match(t, @"^consumed_exit_address\s*=\s*(0x[0-9a-fA-F]+)");
            if (ce.Success) { consumedExit = Convert.ToUInt32(ce.Groups[1].Value, 16); exitLine = i + 1; continue; }

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

/// <summary>One `[[hooks.parameters]]` sub-block.</summary>
public sealed record HookParameter(string Source, string Type);

/// <summary>
/// A complete `[[hooks]]` block, every field the patcher consumes. Used by the
/// per-version hooks-file emitter so a generated file carries the FULL hook
/// contract — skip_original_bytes, exclude_from_restore, consumed_exit_address,
/// parameter blocks — rather than a skeleton to be hand-completed. Hand-copying
/// those fields across 26 hooks is exactly where a port oversight would live.
/// </summary>
public sealed record HookBlock(
    uint Address,
    string Type,
    string Function,
    byte[]? OriginalBytes,
    bool SkipOriginalBytes,
    string? ExcludeFromRestore,
    uint? ConsumedExitAddress,
    List<HookParameter> Parameters);

public static class HookFile
{
    /// <summary>
    /// Parse every `[[hooks]]` block of a hooks.toml with full fidelity.
    /// Line-oriented, like the harvester's ParseHooks; `type =` is
    /// disambiguated between hook level and parameter level by tracking which
    /// sub-block is open.
    /// </summary>
    public static List<HookBlock> Parse(string path)
    {
        var blocks = new List<HookBlock>();

        uint? addr = null;
        string type = "detour";
        string? fn = null;
        byte[]? orig = null;
        bool skip = false;
        string? exclude = null;
        uint? consumedExit = null;
        var parameters = new List<HookParameter>();
        string? paramSource = null;
        string? paramType = null;
        bool inParam = false;

        void FlushParam()
        {
            if (inParam && paramSource != null && paramType != null)
                parameters.Add(new HookParameter(paramSource, paramType));
            paramSource = null; paramType = null; inParam = false;
        }

        void FlushHook()
        {
            FlushParam();
            if (addr.HasValue)
                blocks.Add(new HookBlock(addr.Value, type, fn ?? "(unnamed hook)", orig,
                                         skip, exclude, consumedExit, parameters));
            addr = null; type = "detour"; fn = null; orig = null;
            skip = false; exclude = null; consumedExit = null;
            parameters = new List<HookParameter>();
        }

        foreach (string rawLine in File.ReadAllLines(path))
        {
            string t = rawLine.Trim();
            if (t == "[[hooks]]") { FlushHook(); continue; }
            if (t == "[[hooks.parameters]]") { FlushParam(); inParam = true; continue; }
            if (t.StartsWith('#') || t.Length == 0) continue;

            var kv = Regex.Match(t, @"^([a-z_]+)\s*=\s*(.*?)\s*(?:#.*)?$");
            if (!kv.Success) continue;
            string key = kv.Groups[1].Value;
            string val = kv.Groups[2].Value;

            if (inParam)
            {
                if (key == "source") { paramSource = Unquote(val); continue; }
                if (key == "type") { paramType = Unquote(val); continue; }
            }
            switch (key)
            {
                case "address": addr = Convert.ToUInt32(val, 16); break;
                case "type": type = Unquote(val); break;
                case "function": fn = Unquote(val); break;
                case "skip_original_bytes": skip = val.StartsWith("true"); break;
                case "exclude_from_restore": exclude = val; break;
                case "consumed_exit_address": consumedExit = Convert.ToUInt32(val, 16); break;
                case "original_bytes":
                    orig = Regex.Matches(val, @"0x[0-9a-fA-F]{1,2}")
                        .Select(m => (byte)Convert.ToInt32(m.Value, 16))
                        .ToArray();
                    break;
            }
        }
        FlushHook();
        return blocks;
    }

    private static string Unquote(string s) =>
        s.Length >= 2 && s[0] == '"' && s[^1] == '"' ? s[1..^1] : s;
}
