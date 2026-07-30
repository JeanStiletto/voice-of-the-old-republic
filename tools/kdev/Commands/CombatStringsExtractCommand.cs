using System.CommandLine;
using System.CommandLine.Invocation;
using System.Text;

namespace Kdev.Commands;

/// <summary>
/// Extract the combat msg-bus parse anchors for a target locale from
/// that locale's <c>dialog.tlk</c>. Emits a C++ snippet ready to paste
/// into <c>patches/Accessibility/combat_strings.cpp</c> as the new
/// <c>kEn</c> / <c>kFr</c> / etc. table.
///
/// Why this exists
/// ---------------
/// The combat parser in <c>combat.cpp</c> scans engine-emitted log
/// lines for substrings like " ist erfolgreich mit Angriff auf ". On a
/// non-DE install the engine emits the localised equivalents, so every
/// anchor in <c>kDe</c> needs a per-locale twin or the filter degrades
/// to raw-text passthrough.
///
/// The trick: because the Steam <c>swkotor.exe</c> is locale-shared,
/// every combat string lives in <c>dialog.tlk</c> at the same numeric
/// strref across locales. We mapped the DE strrefs once (see
/// <see cref="StrrefMap"/>); for any new locale, the same strrefs
/// produce the localised text mechanically.
///
/// Usage
/// -----
///   kdev combat-strings-extract                  # uses install dialog.tlk
///   kdev combat-strings-extract --lang en        # uses data/dialog-tlk/dialog_en.tlk
///   kdev combat-strings-extract --tlk path.tlk   # explicit file
///   kdev combat-strings-extract --output kEn.txt # to file (else stdout)
///
/// The --lang shorthand reads from a local per-locale TLK cache populated
/// by manual Steam language swaps. See data/dialog-tlk/MANIFEST.txt for
/// the recognised codes and how to add new ones. Useful so we don't have
/// to round-trip Steam every time we want to look at a non-DE locale's
/// strings (combat or otherwise).
///
/// Workflow per locale
/// -------------------
///   1. Steam → KOTOR → Properties → Language → switch to target.
///   2. Wait for the locale's dialog.tlk to download (~5-700 MB).
///   3. <c>kdev combat-strings-extract</c> → save snippet.
///   4. Switch Steam language back to your own.
///   5. Paste the snippet into combat_strings.cpp as a new kXX table.
///   6. Hand-translate the speech-side fields (marked TODO in the snippet).
///   7. Wire up the new Lang enum value (strings.h, strings.cpp,
///      combat_strings.cpp::Get switch).
///   8. Validate with one combat capture in-locale: grep the patch log
///      for `MsgBuf: raw:` and confirm the extracted anchors match.
///
/// Caveats
/// -------
/// Engine-side anchors that the engine glues from multiple strrefs at
/// runtime (the hit/miss verb + " mit Angriff auf " template) are
/// reconstructed from their component strrefs via
/// <see cref="ReconstructHitMissPhrase"/>. If a locale's engine glue
/// order differs from DE, the extracted anchor won't match — verify
/// against a real combat capture before trusting the output.
/// </summary>
public static class CombatStringsExtractCommand
{
    public static Command Build()
    {
        var tlkOpt = new Option<string?>(
            name: "--tlk",
            description: "Path to dialog.tlk. Default: <game install>/dialog.tlk from kdev.toml.");

        var langOpt = new Option<string?>(
            name: "--lang",
            description: "Locale code (en/de/fr/it/es). Resolves to data/dialog-tlk/dialog_<code>.tlk in the project root.");

        var outOpt = new Option<string?>(
            name: "--output",
            description: "Write the C++ snippet to this path. Default: stdout.");

        var cmd = new Command("combat-strings-extract",
            "Extract per-locale combat-parser anchors from a dialog.tlk; emits a kXX MsgStrings table.")
        {
            tlkOpt, langOpt, outOpt,
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            var tlkPath = ctx.ParseResult.GetValueForOption(tlkOpt);
            var lang    = ctx.ParseResult.GetValueForOption(langOpt);
            var outPath = ctx.ParseResult.GetValueForOption(outOpt);
            ctx.ExitCode = Run(tlkPath, lang, outPath);
        });
        return cmd;
    }

    private static int Run(string? tlkPath, string? langCode, string? outPath)
    {
        if (tlkPath != null && langCode != null)
        {
            Console.Error.WriteLine("--tlk and --lang are mutually exclusive. Pick one.");
            return 1;
        }

        KdevConfig? cfg = null;
        try { cfg = KdevConfig.Load(); }
        catch (KdevConfigException) { /* tlkPath may still be explicit */ }

        if (tlkPath == null && langCode != null)
        {
            if (cfg == null)
            {
                Console.Error.WriteLine("--lang requires kdev.toml (resolves to data/dialog-tlk/ at project root).");
                return 1;
            }
            tlkPath = Path.Combine(cfg.ProjectRoot, "data", "dialog-tlk", $"dialog_{langCode}.tlk");
            if (!File.Exists(tlkPath))
            {
                Console.Error.WriteLine($"No cached TLK for --lang {langCode}: expected {tlkPath}");
                Console.Error.WriteLine($"Populate it via: Steam language swap → copy dialog.tlk to that path.");
                return 1;
            }
        }

        if (tlkPath == null)
        {
            if (cfg == null)
            {
                Console.Error.WriteLine("No --tlk / --lang given and kdev.toml not usable.");
                return 1;
            }
            tlkPath = Path.Combine(cfg.GameInstall, "dialog.tlk");
        }

        if (!File.Exists(tlkPath))
        {
            Console.Error.WriteLine($"TLK not found: {tlkPath}");
            return 1;
        }

        TlkFile tlk;
        try
        {
            tlk = TlkFile.Load(tlkPath);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Failed to parse TLK: {ex.Message}");
            return 1;
        }

        var detectedLang = LanguageIdToCode(tlk.LanguageId);
        var snippet = EmitSnippet(tlk, detectedLang, tlkPath);

        if (outPath == null)
        {
            Console.Write(snippet);
        }
        else
        {
            File.WriteAllText(outPath, snippet);
            Console.Error.WriteLine($"Wrote snippet to {outPath} ({snippet.Length} chars).");
        }
        return 0;
    }

    // ------------------------------------------------------------------
    // Strref map. Keyed by anchor field name. Values are either a
    // single strref (verbatim or with a fixed trim rule), or a
    // synthesised value built from multiple strrefs.
    //
    // Discovered in 2026-05-28 session by grepping the DE dialog.tlk
    // for known anchors and dumping the contiguous combat-template
    // block 42030-42395.
    // ------------------------------------------------------------------
    private static class StrrefMap
    {
        // Hit/miss summary template (strref 42042: "<CUSTOM0> <CUSTOM1> mit Angriff auf <CUSTOM2>").
        public const int SummaryTemplate = 42042;
        public const int VerbHit = 42043;   // "ist erfolgreich"
        public const int VerbMiss = 42044;  // "scheitert"

        // Stats-line suffix (strref 42119: "<CUSTOM0> mit <CUSTOM1> gegen Verteidigung <CUSTOM2>. Schaden: <CUSTOM3>.")
        public const int SuffixTemplate = 42119;

        // Feat-use marker (strref 42046: "<CUSTOM0> verwendet").
        public const int FeatMarker = 42046;

        // Block prefixes (clean truncate at "<CUSTOM" or ":").
        public const int PrefixAngriff = 42146;     // "Angriffsstatistik: <CUSTOM0> <CUSTOM1> ="
        public const int PrefixAbwehr = 42149;      // "Abwehrstatistik: <CUSTOM0> = Basis 10"
        public const int PrefixSchaden = 42150;     // "Schadensstatistik: <CUSTOM0> = <CUSTOM1>"
        public const int PrefixBedrohung = 42148;   // "Bedrohungsstatistik: Würfelergebnis ..."

        // Tags.
        public const int TagKritSummary = 1511;     // "Kritischer Treffer!"
        public const int TagAutoHit = 42390;        // "Automatischer Treffer!"
        public const int TagAutoFail = 42391;       // "Automatischer Fehlschlag!"
        public const int KritXPrefix = 42386;       // "Kritischer Treffer x<CUSTOM0> für "

        // Per-component breakdown labels (drop trailing " <CUSTOM0>").
        public const int TokenWuerfel = 42316;      // " Würfelergebnis <CUSTOM0>"  (note leading space)
        public const int TokenGeschMod = 42339;     // "+ Geschicklichkeit-Mod. <CUSTOM0>"
        public const int TokenEntfernung = 42330;   // "+ Entfernungsbonus <CUSTOM0>"
        public const int TokenEffekt = 42332;       // "+ Effektbonus <CUSTOM0>"
        public const int TokenBonusschaden = 42155; // "Bonusschaden <CUSTOM0>"

        // Hand labels.
        public const int HandMain = 42314;          // "Haupthand"

        // ---- Effect / save / damage / kill sequence (results-only rework).
        // Discovered 2026-06-09 by searching the locale TLKs for the DE
        // anchor verbs; all locale-stable (same exe → same strref numbers,
        // localised text). The save line is glued by the engine from three
        // strrefs (type 1374-1376 + result 1392/1393 + template 1406).
        public const int AuswirkungPrefix = 42157;  // "Auswirkungsstatistik:"
        public const int AbsorbReduction = 1455;    // "<C0> : Schadensminderung absorbiert <C1> ..."
                                                    //   (the absorb verb is shared by the reduction +
                                                    //    resistance variants 1455/1456; the simpler
                                                    //    1454 uses a different verb in EN/FR/ES)
        public const int AbilityUse = 32292;        // "<C0> benutzt <C1>."
        public const int DamageActive = 1403;       // "<C0> verletzt <C1>: <C2> Schaden"
        public const int Kill = 1407;               // "<C0> neutralisiert <C1>: <C2> EPs"
        public const int SaveTypeReflex = 1374;     // "Reflex-Rettungswurf"
        public const int SaveTypeFort = 1375;       // "Tapferkeit-Rettungswurf"
        public const int SaveTypeWill = 1376;       // "Willensstärke-Rettungswurf"
        public const int SaveResultSuccess = 1392;  // "Erfolg"
        public const int SaveResultFail = 1393;     // "Misserfolg"
        public const int SaveLineTemplate = 1406;   // "<C0> <C1>. <C2>! ... (Würfelergebnis ...) gegen SK <C6>"
    }

    // ------------------------------------------------------------------
    // Field extraction. One entry per MsgStrings field, in declaration
    // order from combat_strings.h. The Extract delegate receives the
    // TLK and returns the raw bytes (already CP-1252-decoded by
    // TlkFile.GetBytes) that should appear in the C++ literal.
    // ------------------------------------------------------------------
    private record FieldRule(string Name, string Comment, Func<TlkFile, byte[]?> Extract);

    private static readonly FieldRule[] EngineAnchors = new FieldRule[]
    {
        new("phrase_hit",  " ist erfolgreich mit Angriff auf ",
            t => ReconstructHitMissPhrase(t, StrrefMap.SummaryTemplate, StrrefMap.VerbHit)),
        new("phrase_miss", " scheitert mit Angriff auf ",
            t => ReconstructHitMissPhrase(t, StrrefMap.SummaryTemplate, StrrefMap.VerbMiss)),
        new("phrase_mit",  "leading 2 spaces — engine glues suffix to summary with 1 extra space",
            t => SuffixLiteral(t, StrrefMap.SuffixTemplate, "<CUSTOM0>", "<CUSTOM1>", prefixSpaces: 1)),
        new("word_verteidigung", "last word in template gap between <CUSTOM1> and <CUSTOM2>",
            t => LastWordBeforePlaceholder(t.Get(StrrefMap.SuffixTemplate), "<CUSTOM1>", "<CUSTOM2>")),
        new("word_schaden_colon", "last word in template gap between <CUSTOM2> and <CUSTOM3> (keeps trailing ':' if present)",
            t => LastWordBeforePlaceholder(t.Get(StrrefMap.SuffixTemplate), "<CUSTOM2>", "<CUSTOM3>")),
        new("feat_marker", "TLK gives 'verwendet'; engine appends '.' at the end of the line",
            t => AppendByte(StripPlaceholders(t.Get(StrrefMap.FeatMarker)), (byte)'.')),
        new("prefix_angriff", "drop everything from '<CUSTOM' onward",
            t => TrimAt(t.Get(StrrefMap.PrefixAngriff), "<CUSTOM")),
        new("prefix_abwehr", "drop everything from '<CUSTOM' onward",
            t => TrimAt(t.Get(StrrefMap.PrefixAbwehr), "<CUSTOM")),
        new("prefix_schaden", "drop everything from '<CUSTOM' onward",
            t => TrimAt(t.Get(StrrefMap.PrefixSchaden), "<CUSTOM")),
        new("prefix_bedrohung", "keep through ':' only (anchor is the prefix)",
            t => TrimAfter(t.Get(StrrefMap.PrefixBedrohung), ":")),
        new("tag_krit_summary", "verbatim",
            t => t.Get(StrrefMap.TagKritSummary)),
        new("tag_auto_hit", "verbatim",
            t => t.Get(StrrefMap.TagAutoHit)),
        new("tag_auto_fail", "verbatim",
            t => t.Get(StrrefMap.TagAutoFail)),
        new("token_wuerfel", "drop leading space (parser pre-strips) and ' <CUSTOM0>' tail",
            t => TrimAt(TrimLeadingSpaces(t.Get(StrrefMap.TokenWuerfel)), "<CUSTOM0>")),
        new("token_gesch_mod", "drop leading whitespace + '+ ' and trailing '<CUSTOM0>'",
            t => TrimAt(StripLeading(TrimLeadingSpaces(t.Get(StrrefMap.TokenGeschMod)), "+ "), "<CUSTOM0>")),
        new("token_entfernung", "drop leading whitespace + '+ ' and trailing '<CUSTOM0>'",
            t => TrimAt(StripLeading(TrimLeadingSpaces(t.Get(StrrefMap.TokenEntfernung)), "+ "), "<CUSTOM0>")),
        new("token_effekt", "drop leading whitespace + '+ ' and trailing '<CUSTOM0>'",
            t => TrimAt(StripLeading(TrimLeadingSpaces(t.Get(StrrefMap.TokenEffekt)), "+ "), "<CUSTOM0>")),
        new("krit_x_prefix", "drop '<CUSTOM0> für ' tail",
            t => TrimAt(t.Get(StrrefMap.KritXPrefix), "<CUSTOM0>")),
        new("phrase_fuer", "tail of krit-X template: '> für '",
            t => Between(t.Get(StrrefMap.KritXPrefix), ">", null)),
        new("token_bonusschaden", "drop ' <CUSTOM0>' tail",
            t => TrimAt(t.Get(StrrefMap.TokenBonusschaden), " <CUSTOM0>")),
        new("hand_main", "verbatim",
            t => t.Get(StrrefMap.HandMain)),
    };

    // Speech-side fields the engine doesn't supply. Manual translation
    // required per locale; the snippet emits placeholders.
    private static readonly (string Name, string DePlaceholder)[] SpeechLabels = new[]
    {
        ("verb_hit",         "trifft"),
        ("verb_miss",        "verfehlt"),
        ("word_critical",    "kritisch"),
        ("word_angriff",     "Angriff"),
        ("word_vert",        "Vert."),
        ("word_gg",          "gg."),
        ("word_schaden",     "Schaden"),
        ("word_von",         "von"),
        ("word_auto_hit",    "Auto-Treffer."),
        ("word_auto_fail",   "Auto-Fehlschlag."),
    };

    private static readonly (string Name, string DePlaceholder)[] ShortReplacements = new[]
    {
        ("short_wuerfel",    "W"),
        ("short_gesch",      "Gesch "),
        ("short_reichweite", "Reichweite "),
        ("short_effekt",     "Effekt "),
        ("short_bonus",      "Bonus"),
    };

    // ------------------------------------------------------------------
    // Results-only labels + effect/save/damage/kill anchors (the rework
    // layer). A single ordered list because the struct interleaves engine
    // anchors with speech labels: word_failed, [auswirkung..damage anchors],
    // word_resists, word_save_failed, kill_marker. Engine != null means
    // extracted from the TLK; SpeechDe != null means a manual placeholder.
    // ------------------------------------------------------------------
    private record TrailingField(string Name, string Comment, Func<TlkFile, byte[]?>? Engine, string? SpeechDe);

    private static readonly TrailingField[] TrailingFields = new TrailingField[]
    {
        new("word_failed", "miss line label", null, "fehlgeschlagen"),
        new("prefix_auswirkung", "status-echo prefix (42157, verbatim)",
            t => t.Get(StrrefMap.AuswirkungPrefix), null),
        new("absorb_anchor", "absorb verb — last word before <CUSTOM1> in 1455 (covers reduction+resistance variants; not the 1454 energy-shield variant)",
            t => WordBeforePlaceholder(t.Get(StrrefMap.AbsorbReduction), 1), null),
        new("ability_use_marker", "between <CUSTOM0> and <CUSTOM1> in 32292 (spacing preserved)",
            t => BetweenPlaceholders(t.Get(StrrefMap.AbilityUse), 0, 1), null),
        new("save_marker", "save-type common suffix (1374-1376) + type/result separator (1406 gap C1..C2)",
            t => SaveMarker(t), null),
        new("save_success", "result word (1392) + post-result punctuation (1406 gap C2..C3, right-trimmed)",
            t => Concat(t.Get(StrrefMap.SaveResultSuccess), SaveResultPunct(t)), null),
        new("save_fail", "result word (1393) + post-result punctuation (1406 gap C2..C3, right-trimmed)",
            t => Concat(t.Get(StrrefMap.SaveResultFail), SaveResultPunct(t)), null),
        new("damage_marker", "between <CUSTOM0> and <CUSTOM1> in 1403 (spacing preserved)",
            t => BetweenPlaceholders(t.Get(StrrefMap.DamageActive), 0, 1), null),
        new("word_resists", "save-success verb", null, "widersteht"),
        new("word_save_failed", "save-failure tag", null, "misslungen"),
        new("kill_marker", "verb between <CUSTOM0> and <CUSTOM1> in 1407 (trimmed)",
            t => TrimSpaces(BetweenPlaceholders(t.Get(StrrefMap.Kill), 0, 1)), null),
        new("status_ist_marker", "status-echo copula, leading+trailing space (de ' ist ', en ' is ', fr ' est ', it ' \\xE8 ', es ' es ')", null, " ist "),
    };

    // ------------------------------------------------------------------
    // Extraction primitives. All work on raw byte arrays so we never
    // round-trip through UTF-16 and lose CP-1252 fidelity. The TLK
    // reader gives us the raw bytes for each strref; we slice and
    // splice them and emit C++ literals with \xNN escapes for non-ASCII.
    // ------------------------------------------------------------------

    private static byte[]? TrimAt(byte[]? src, string marker)
    {
        if (src == null) return null;
        int idx = IndexOfAscii(src, marker);
        if (idx < 0) return src;
        var result = new byte[idx];
        Array.Copy(src, 0, result, 0, idx);
        return result;
    }

    private static byte[]? TrimAfter(byte[]? src, string marker)
    {
        if (src == null) return null;
        int idx = IndexOfAscii(src, marker);
        if (idx < 0) return src;
        int end = idx + marker.Length;
        var result = new byte[end];
        Array.Copy(src, 0, result, 0, end);
        return result;
    }

    /// <summary>
    /// Extract the "noun phrase locator" from a template gap between two
    /// placeholders. Locale-portable: takes the substring between
    /// <paramref name="afterPlaceholder"/> and <paramref name="beforePlaceholder"/>,
    /// strips leading whitespace and any leading separator punctuation
    /// (".", ","), then returns the LAST whitespace-delimited token plus
    /// the trailing space. This produces:
    ///   DE " gegen Verteidigung " → "Verteidigung "
    ///   EN " vs. Defense "        → "Defense "
    ///   DE ". Schaden: "          → "Schaden: "
    ///   EN " for damage "         → "damage "
    /// All four are valid strstr anchors that locate the following numeric
    /// value the parser atoi()s.
    /// </summary>
    private static byte[]? LastWordBeforePlaceholder(byte[]? template, string afterPlaceholder, string beforePlaceholder)
    {
        if (template == null) return null;
        int a = IndexOfAscii(template, afterPlaceholder);
        if (a < 0) return null;
        a += afterPlaceholder.Length;
        int b = IndexOfAscii(template, beforePlaceholder, a);
        if (b < 0) return null;

        // Find last space within [a, b). The last word is everything after
        // it. Trailing whitespace inside the gap (just before placeholder)
        // is preserved as the anchor's trailing space.
        int trailing = b;
        while (trailing > a && template[trailing - 1] == (byte)' ') trailing--;
        int lastSpace = trailing - 1;
        while (lastSpace > a && template[lastSpace] != (byte)' ') lastSpace--;
        // lastSpace now points at the space before the last word, or == a
        // if there's no internal space (single-word gap).
        int wordStart = (template[lastSpace] == (byte)' ') ? lastSpace + 1 : lastSpace;

        int len = b - wordStart;
        var result = new byte[len];
        Array.Copy(template, wordStart, result, 0, len);
        return result;
    }

    private static byte[]? TrimLeadingSpaces(byte[]? src)
    {
        if (src == null) return null;
        int i = 0;
        while (i < src.Length && src[i] == (byte)' ') i++;
        if (i == 0) return src;
        var result = new byte[src.Length - i];
        Array.Copy(src, i, result, 0, result.Length);
        return result;
    }

    private static byte[]? StripLeading(byte[]? src, string prefix)
    {
        if (src == null) return null;
        if (!StartsWithAscii(src, prefix)) return src;
        var result = new byte[src.Length - prefix.Length];
        Array.Copy(src, prefix.Length, result, 0, result.Length);
        return result;
    }

    private static byte[]? StripPlaceholders(byte[]? src)
    {
        if (src == null) return null;
        // Drop any "<CUSTOMn>" sequence (n = one decimal digit).
        var result = new List<byte>(src.Length);
        for (int i = 0; i < src.Length; i++)
        {
            if (i + 9 <= src.Length &&
                src[i] == (byte)'<' && src[i + 1] == (byte)'C' &&
                src[i + 2] == (byte)'U' && src[i + 3] == (byte)'S' &&
                src[i + 4] == (byte)'T' && src[i + 5] == (byte)'O' &&
                src[i + 6] == (byte)'M' &&
                src[i + 7] >= (byte)'0' && src[i + 7] <= (byte)'9' &&
                src[i + 8] == (byte)'>')
            {
                i += 8;
                continue;
            }
            result.Add(src[i]);
        }
        return result.ToArray();
    }

    private static byte[]? AppendByte(byte[]? src, byte b)
    {
        if (src == null) return null;
        var result = new byte[src.Length + 1];
        Array.Copy(src, result, src.Length);
        result[src.Length] = b;
        return result;
    }

    /// <summary>
    /// Extract a literal substring of a template strref between two
    /// placeholder markers. If <paramref name="start"/> is null, take
    /// from the beginning; if <paramref name="end"/> is null, take to
    /// the end. Optionally pad the result with leading spaces — used
    /// for <c>phrase_mit</c> where the engine emits 2 spaces because
    /// the summary line ends with a space and the suffix starts with
    /// " mit ".
    /// </summary>
    private static byte[]? SuffixLiteral(TlkFile t, int strref, string? start, string? end, int prefixSpaces = 0)
    {
        var src = t.Get(strref);
        if (src == null) return null;
        int from = 0;
        if (start != null)
        {
            int idx = IndexOfAscii(src, start);
            if (idx < 0) return null;
            from = idx + start.Length;
        }
        int to = src.Length;
        if (end != null)
        {
            int idx = IndexOfAscii(src, end, from);
            if (idx < 0) return null;
            to = idx;
        }
        if (to < from) return null;
        var slice = new byte[to - from + prefixSpaces];
        for (int i = 0; i < prefixSpaces; i++) slice[i] = (byte)' ';
        Array.Copy(src, from, slice, prefixSpaces, to - from);
        return slice;
    }

    private static byte[]? Between(byte[]? src, string? start, string? end)
    {
        if (src == null) return null;
        int from = 0;
        if (start != null)
        {
            int idx = IndexOfAscii(src, start);
            if (idx < 0) return null;
            from = idx + start.Length;
        }
        int to = src.Length;
        if (end != null)
        {
            int idx = IndexOfAscii(src, end, from);
            if (idx < 0) return null;
            to = idx;
        }
        if (to < from) return null;
        var result = new byte[to - from];
        Array.Copy(src, from, result, 0, to - from);
        return result;
    }

    /// <summary>
    /// Reconstruct " VERB mit Angriff auf " by substituting the verb
    /// strref into the summary template's CUSTOM1 slot, then extracting
    /// the substring between CUSTOM0 and CUSTOM2 (which leaves leading
    /// and trailing spaces — the anchors strstr() matches against).
    /// </summary>
    private static byte[]? ReconstructHitMissPhrase(TlkFile t, int templateStrref, int verbStrref)
    {
        var template = t.Get(templateStrref);
        var verb = t.Get(verbStrref);
        if (template == null || verb == null) return null;

        // Find CUSTOM0 and CUSTOM2 positions in the template.
        int c0 = IndexOfAscii(template, "<CUSTOM0>");
        int c2 = IndexOfAscii(template, "<CUSTOM2>");
        int c1 = IndexOfAscii(template, "<CUSTOM1>");
        if (c0 < 0 || c1 < 0 || c2 < 0) return null;

        int from = c0 + "<CUSTOM0>".Length;   // start after CUSTOM0
        int to = c2;                          // stop before CUSTOM2

        // Stitch: template[from..c1] + verb + template[c1+|<CUSTOM1>|..to]
        var pre = template.AsSpan(from, c1 - from);
        var post = template.AsSpan(c1 + "<CUSTOM1>".Length,
                                   to - (c1 + "<CUSTOM1>".Length));
        var result = new byte[pre.Length + verb.Length + post.Length];
        pre.CopyTo(result);
        verb.CopyTo(result.AsSpan(pre.Length));
        post.CopyTo(result.AsSpan(pre.Length + verb.Length));
        return result;
    }

    private static int IndexOfAscii(byte[] hay, string needle, int from = 0)
    {
        for (int i = from; i <= hay.Length - needle.Length; i++)
        {
            bool match = true;
            for (int k = 0; k < needle.Length; k++)
            {
                if (hay[i + k] != (byte)needle[k]) { match = false; break; }
            }
            if (match) return i;
        }
        return -1;
    }

    private static bool StartsWithAscii(byte[] hay, string needle)
    {
        if (hay.Length < needle.Length) return false;
        for (int k = 0; k < needle.Length; k++)
            if (hay[k] != (byte)needle[k]) return false;
        return true;
    }

    // ------------------------------------------------------------------
    // Placeholder-tolerant extraction for the effect/save/damage/kill
    // templates. Some locales author the token as "< CUSTOMn>" with an
    // internal space (FR/IT) — the engine normalises it, so we must too.
    // ------------------------------------------------------------------

    // Locate "<" [spaces] "CUSTOMn" ">". Returns (start, endExclusive) or null.
    private static (int start, int end)? FindPlaceholder(byte[] s, int n)
    {
        const string kw = "CUSTOM";
        for (int i = 0; i < s.Length; i++)
        {
            if (s[i] != (byte)'<') continue;
            int j = i + 1;
            while (j < s.Length && s[j] == (byte)' ') j++;
            if (j + kw.Length + 1 >= s.Length) continue;
            bool ok = true;
            for (int k = 0; k < kw.Length; k++)
                if (s[j + k] != (byte)kw[k]) { ok = false; break; }
            if (!ok) continue;
            int d = j + kw.Length;
            if (s[d] != (byte)('0' + n)) continue;
            if (s[d + 1] != (byte)'>') continue;
            return (i, d + 2);
        }
        return null;
    }

    // Text between placeholder n0 (end) and n1 (start), spacing preserved.
    private static byte[]? BetweenPlaceholders(byte[]? s, int n0, int n1)
    {
        if (s == null) return null;
        var a = FindPlaceholder(s, n0);
        var b = FindPlaceholder(s, n1);
        if (a == null || b == null || b.Value.start < a.Value.end) return null;
        int len = b.Value.start - a.Value.end;
        var r = new byte[len];
        Array.Copy(s, a.Value.end, r, 0, len);
        return r;
    }

    // Last whitespace-delimited token before placeholder n (used for the
    // absorb verb that sits immediately before <CUSTOM1>).
    private static byte[]? WordBeforePlaceholder(byte[]? s, int n)
    {
        if (s == null) return null;
        var p = FindPlaceholder(s, n);
        if (p == null) return null;
        int end = p.Value.start;
        while (end > 0 && s[end - 1] == (byte)' ') end--;
        int start = end;
        while (start > 0 && s[start - 1] != (byte)' ') start--;
        if (end <= start) return null;
        var r = new byte[end - start];
        Array.Copy(s, start, r, 0, end - start);
        return r;
    }

    private static byte[]? TrimSpaces(byte[]? s)
    {
        if (s == null) return null;
        int a = 0, b = s.Length;
        while (a < b && s[a] == (byte)' ') a++;
        while (b > a && s[b - 1] == (byte)' ') b--;
        var r = new byte[b - a];
        Array.Copy(s, a, r, 0, b - a);
        return r;
    }

    private static byte[]? Concat(byte[]? a, byte[]? b)
    {
        if (a == null || b == null) return null;
        var r = new byte[a.Length + b.Length];
        Array.Copy(a, r, a.Length);
        Array.Copy(b, 0, r, a.Length, b.Length);
        return r;
    }

    private static byte[] CommonSuffix(byte[] a, byte[] b, byte[] c)
    {
        int m = Math.Min(a.Length, Math.Min(b.Length, c.Length));
        int k = 0;
        while (k < m &&
               a[a.Length - 1 - k] == b[b.Length - 1 - k] &&
               a[a.Length - 1 - k] == c[c.Length - 1 - k]) k++;
        var r = new byte[k];
        Array.Copy(a, a.Length - k, r, 0, k);
        return r;
    }

    // save_marker = longest common suffix of the three save-type strings
    // (e.g. DE "-Rettungswurf", EN " Save") + the template's type→result
    // separator. When the locale has no common suffix (FR/IT/ES, whose
    // distinguishing word is the LAST token) the suffix is empty and the
    // separator alone is the anchor — the parser then walks back one word
    // to recover the type. Both shapes match what the engine renders.
    private static byte[]? SaveMarker(TlkFile t)
    {
        var s1 = t.Get(StrrefMap.SaveTypeReflex);
        var s2 = t.Get(StrrefMap.SaveTypeFort);
        var s3 = t.Get(StrrefMap.SaveTypeWill);
        var sep = BetweenPlaceholders(t.Get(StrrefMap.SaveLineTemplate), 1, 2);
        if (s1 == null || s2 == null || s3 == null || sep == null) return null;
        return Concat(CommonSuffix(s1, s2, s3), sep);
    }

    // Post-result punctuation (the "!" plus locale spacing) from the save
    // template's gap between the result word and the next placeholder.
    // Right-trimmed so the trailing field separator isn't carried in.
    private static byte[]? SaveResultPunct(TlkFile t)
    {
        var sep = BetweenPlaceholders(t.Get(StrrefMap.SaveLineTemplate), 2, 3);
        if (sep == null) return null;
        int b = sep.Length;
        while (b > 0 && sep[b - 1] == (byte)' ') b--;
        var r = new byte[b];
        Array.Copy(sep, r, b);
        return r;
    }

    // ------------------------------------------------------------------
    // C++ string-literal emission. CP-1252 bytes >= 0x80 emit as \xNN
    // hex escapes; insertion of "" separators after a hex escape avoids
    // the C++ "as many hex chars as possible" rule glueing the escape
    // onto a following hex digit.
    // ------------------------------------------------------------------
    private static string CppLiteral(byte[]? bytes)
    {
        if (bytes == null) return "nullptr /* MISSING */";
        var sb = new StringBuilder();
        sb.Append('"');
        bool lastWasHexEscape = false;
        foreach (byte b in bytes)
        {
            bool isHexChar = (b >= (byte)'0' && b <= (byte)'9') ||
                             (b >= (byte)'a' && b <= (byte)'f') ||
                             (b >= (byte)'A' && b <= (byte)'F');
            if (lastWasHexEscape && isHexChar)
            {
                sb.Append("\"\"");  // break the escape early
            }
            lastWasHexEscape = false;

            switch (b)
            {
                case (byte)'\\': sb.Append("\\\\"); break;
                case (byte)'"':  sb.Append("\\\""); break;
                case (byte)'\n': sb.Append("\\n"); break;
                case (byte)'\r': sb.Append("\\r"); break;
                case (byte)'\t': sb.Append("\\t"); break;
                default:
                    if (b >= 0x20 && b <= 0x7E)
                    {
                        sb.Append((char)b);
                    }
                    else
                    {
                        sb.Append("\\x");
                        sb.Append(b.ToString("X2"));
                        lastWasHexEscape = true;
                    }
                    break;
            }
        }
        sb.Append('"');
        return sb.ToString();
    }

    // ------------------------------------------------------------------
    // Snippet emission.
    // ------------------------------------------------------------------
    private static string EmitSnippet(TlkFile tlk, string langCode, string tlkPath)
    {
        var sb = new StringBuilder();
        var tableName = "k" + char.ToUpperInvariant(langCode[0]) + langCode.Substring(1);

        sb.AppendLine($"// === {tableName} table — extracted by `kdev combat-strings-extract` ===");
        sb.AppendLine($"// Source TLK: {tlkPath}");
        sb.AppendLine($"// TLK languageId: {tlk.LanguageId} ({langCode})");
        sb.AppendLine($"// Generated: {DateTime.UtcNow:yyyy-MM-dd HH:mm} UTC");
        sb.AppendLine($"//");
        sb.AppendLine($"// Speech-side fields (verb_hit … short_bonus, plus word_failed/");
        sb.AppendLine($"// word_resists/word_save_failed) are placeholders copied from kDe —");
        sb.AppendLine($"// replace with target-locale translations before use. Engine anchors");
        sb.AppendLine($"// (including the effect/save/damage/kill set) are extracted from the TLK.");
        sb.AppendLine($"// Verify engine-anchor bytes against one in-locale combat capture:");
        sb.AppendLine($"//   grep 'MsgBuf: raw:' <install>/logs/patch-*.log");
        sb.AppendLine();
        sb.AppendLine($"const MsgStrings {tableName} = {{");
        sb.AppendLine($"    // ---- Engine-side parse anchors (extracted from dialog.tlk)");
        foreach (var rule in EngineAnchors)
        {
            var bytes = rule.Extract(tlk);
            var literal = CppLiteral(bytes);
            sb.AppendLine($"    {literal},  // {rule.Name} — {rule.Comment}");
        }
        sb.AppendLine();
        sb.AppendLine($"    // ---- Output-side labels (TODO({langCode}): translate)");
        foreach (var (name, placeholder) in SpeechLabels)
        {
            sb.AppendLine($"    {CppLiteral(EncodeAscii(placeholder))},  // {name} — TODO translate");
        }
        sb.AppendLine();
        sb.AppendLine($"    // ---- Short replacements (TODO({langCode}): translate)");
        foreach (var (name, placeholder) in ShortReplacements)
        {
            sb.AppendLine($"    {CppLiteral(EncodeAscii(placeholder))},  // {name} — TODO translate");
        }
        sb.AppendLine();
        sb.AppendLine($"    // ---- Results-only labels + effect/save/damage/kill anchors");
        foreach (var f in TrailingFields)
        {
            if (f.Engine != null)
            {
                sb.AppendLine($"    {CppLiteral(f.Engine(tlk))},  // {f.Name} — {f.Comment}");
            }
            else
            {
                sb.AppendLine($"    {CppLiteral(EncodeAscii(f.SpeechDe!))},  // {f.Name} — {f.Comment} (TODO({langCode}): translate)");
            }
        }
        sb.AppendLine($"}};");
        sb.AppendLine();
        sb.AppendLine($"// === Wiring checklist ===");
        sb.AppendLine($"// 1. Add Lang::{char.ToUpperInvariant(langCode[0])}{langCode.Substring(1)} to strings.h.");
        sb.AppendLine($"// 2. Add namespace lang_{langCode} + Get(Id) to strings.h / strings_{langCode}.cpp.");
        sb.AppendLine($"// 3. Add `case Lang::{char.ToUpperInvariant(langCode[0])}{langCode.Substring(1)}: return {tableName};`");
        sb.AppendLine($"//    in combat_strings.cpp::Get().");
        sb.AppendLine($"// 4. Add `case Lang::{char.ToUpperInvariant(langCode[0])}{langCode.Substring(1)}: return lang_{langCode}::Get(id);`");
        sb.AppendLine($"//    in strings.cpp::Get().");
        return sb.ToString();
    }

    private static byte[] EncodeAscii(string s)
    {
        // Speech placeholders are kDe text — encode as CP-1252 so the
        // emitter produces the same byte-level literals the existing
        // kDe table uses.
        return Encoding.GetEncoding(1252).GetBytes(s);
    }

    private static string LanguageIdToCode(int languageId) => languageId switch
    {
        0 => "en",
        1 => "fr",
        2 => "de",
        3 => "it",
        4 => "es",
        _ => $"lang{languageId}",
    };
}

