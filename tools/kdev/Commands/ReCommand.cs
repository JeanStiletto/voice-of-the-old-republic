using System.CommandLine;
using System.CommandLine.Invocation;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace Kdev.Commands;

/// <summary>
/// Reverse-engineer a whole bounded engine subsystem from Lane's Ghidra DB in
/// one shot — the "decompile-first" playbook, made a command.
///
/// Why this exists
/// ---------------
/// The turret minigame cost ~5 sessions of measure-a-value → infer-a-model →
/// contradict-it-next-session, because each session started from runtime
/// probing instead of reading the engine. The correct model was readable in the
/// decompile the whole time (the subsystem was 155 functions out of 24k; the
/// logic-bearing path ~15). This command turns "reconstruct subsystem X from the
/// engine" from a remembered ritual into one invocation:
///
///   1. Enumerate every function in the matching classes (SARIF, via jq).
///   2. Dump those classes' struct layouts (SARIF).
///   3. Batch-decompile the logic-bearing functions (Ghidra headless), skipping
///      trivial setters/getters/ctors/dtors unless --all.
///   4. Emit a skeleton model doc + the raw decompile, ready to read and fill in.
///
/// See memory `feedback_reconstruct_engine_structures_first` for the principle:
/// runtime measurement is for CONFIRMING a decompile-derived model or reading an
/// exact engine value — never for INFERRING the model.
///
/// Usage
/// -----
///   kdev re "Mini|MGGun|TrackFollower"              # enumerate + structs (fast)
///   kdev re "CSWGuiDialog" --decompile              # + decompile logic-bearing fns
///   kdev re "CSWMiniGame" --decompile --all         # decompile EVERYTHING in the set
///   kdev re "Mini" --out build/re/turret --max-funcs 60
///
/// The pattern is an Oniguruma regex (jq `test()`), matched against each
/// function's namespace (class) AND each datatype's name. Output lands in
/// build/re/&lt;slug&gt;/ (gitignored, regenerable): a &lt;slug&gt;-model.md skeleton to
/// curate into docs/llm-docs/, plus &lt;slug&gt;.decomp.txt with the raw decompile.
///
/// Dependencies (configurable via the [re] section of kdev.toml; defaults match
/// docs/tools.md): the SARIF export, `jq` on PATH, and the Ghidra headless
/// analyzer + tools/ghidra-scripts/Decompile.java.
/// </summary>
public static class ReCommand
{
    private const string DecompMarker = "Decompile.java> ";
    private const string GhidraSuffix = "(GhidraScript)";

    public static Command Build()
    {
        var patternArg = new Argument<string>(
            name: "pattern",
            description: "Regex matched against class/namespace names (and datatype names). " +
                         "E.g. \"Mini|MGGun|TrackFollower\".");

        var decompileOpt = new Option<bool>(
            aliases: new[] { "--decompile", "-d" },
            description: "Also batch-decompile the logic-bearing functions via Ghidra headless (~100s cold).");

        var allOpt = new Option<bool>(
            name: "--all",
            description: "When decompiling, include trivial Set*/Get*/ctor/dtor functions too.");

        var maxFuncsOpt = new Option<int>(
            name: "--max-funcs",
            getDefaultValue: () => 40,
            description: "Cap on how many functions to decompile (safety against huge sets).");

        var outOpt = new Option<string?>(
            name: "--out",
            description: "Output directory. Default: build/re/<slug-of-pattern>/.");

        var jqOpt = new Option<string?>(
            name: "--jq",
            description: "Override the jq executable (else [re].jq from kdev.toml, else \"jq\" on PATH).");

        var cmd = new Command("re",
            "Reconstruct an engine subsystem from the Ghidra DB: enumerate classes, dump structs, decompile the logic-bearing functions, emit a model skeleton.")
        {
            patternArg, decompileOpt, allOpt, maxFuncsOpt, outOpt, jqOpt,
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            var pattern   = ctx.ParseResult.GetValueForArgument(patternArg);
            var decompile = ctx.ParseResult.GetValueForOption(decompileOpt);
            var all       = ctx.ParseResult.GetValueForOption(allOpt);
            var maxFuncs  = ctx.ParseResult.GetValueForOption(maxFuncsOpt);
            var outDir    = ctx.ParseResult.GetValueForOption(outOpt);
            var jqOverride= ctx.ParseResult.GetValueForOption(jqOpt);
            ctx.ExitCode  = Run(pattern, decompile, all, maxFuncs, outDir, jqOverride);
        });
        return cmd;
    }

    // ------------------------------------------------------------------
    // Records parsed out of the jq NDJSON pass.
    // ------------------------------------------------------------------
    private sealed record FnRec(string Loc, string Ns, string Name, string Sig);
    private sealed record FieldRec(int Offset, string Name, string Type);
    private sealed record DtRec(string Name, List<FieldRec> Fields);

    private static int Run(string pattern, bool decompile, bool all, int maxFuncs,
                           string? outDir, string? jqOverride)
    {
        var cfg = KdevConfig.LoadOrPrintErrors(out var exit);
        if (cfg is null) return exit;

        var sarif = cfg.ReSarifPath;
        if (!File.Exists(sarif))
        {
            Console.Error.WriteLine($"ERROR: SARIF export not found: {sarif}");
            Console.Error.WriteLine("Set [re].sarif in kdev.toml or download it (see docs/tools.md / sarif-cookbook.md).");
            return 1;
        }
        var jq = jqOverride ?? cfg.ReJqPath;

        var slug = Slug(pattern);
        var dir = outDir is not null
            ? Path.GetFullPath(outDir)
            : Path.Combine(cfg.BuildOutput, "re", slug);
        Directory.CreateDirectory(dir);

        // -- Step 1+2: enumerate functions + struct layouts in one jq pass. --
        Console.Error.WriteLine($"Querying SARIF for /{pattern}/ (this reads the whole export, ~10s)...");
        List<FnRec> fns;
        List<DtRec> dts;
        try
        {
            (fns, dts) = QuerySarif(jq, sarif, pattern);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"ERROR: SARIF query failed: {ex.Message}");
            Console.Error.WriteLine($"Is jq installed/on PATH? (configured: {jq}). See docs/llm-docs/sarif-cookbook.md.");
            return 1;
        }

        if (fns.Count == 0 && dts.Count == 0)
        {
            Console.Error.WriteLine($"No functions or datatypes matched /{pattern}/. Check the regex (it's matched against class names).");
            return 1;
        }

        // De-dupe functions by entry address (overloads / vtable dupes share none,
        // but defensive). Sort by address for stable output.
        fns = fns
            .GroupBy(f => f.Loc).Select(g => g.First())
            .OrderBy(f => f.Loc, StringComparer.Ordinal).ToList();
        dts = dts.OrderBy(d => d.Name, StringComparer.Ordinal).ToList();

        // -- Step 3: pick the decompile set. --
        var logicBearing = fns.Where(f => all || !IsTrivial(f)).ToList();
        var decompiled = new List<FnRec>();
        string? decompFile = null;
        var truncated = false;

        if (decompile)
        {
            decompiled = logicBearing.Take(maxFuncs).ToList();
            truncated = logicBearing.Count > maxFuncs;
            if (truncated)
                Console.Error.WriteLine(
                    $"NOTE: {logicBearing.Count} logic-bearing functions; decompiling the first {maxFuncs} " +
                    $"(raise with --max-funcs).");

            Console.Error.WriteLine($"Decompiling {decompiled.Count} functions via Ghidra headless (cold ~100s)...");
            string raw;
            try
            {
                raw = RunGhidraDecompile(cfg, decompiled.Select(f => "0x" + f.Loc.TrimStart('0')).ToList());
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"ERROR: Ghidra decompile failed: {ex.Message}");
                Console.Error.WriteLine($"Check [re].ghidra_* in kdev.toml (headless: {cfg.ReGhidraHeadless}).");
                return 1;
            }
            var clean = CleanGhidraOutput(raw);
            decompFile = Path.Combine(dir, $"{slug}.decomp.txt");
            File.WriteAllText(decompFile, clean);
            Console.Error.WriteLine($"Wrote raw decompile: {decompFile}");
        }

        // -- Step 4: emit the model skeleton. --
        var docPath = Path.Combine(dir, $"{slug}-model.md");
        var decompiledLocs = decompiled.Select(f => f.Loc).ToHashSet();
        var doc = EmitDoc(pattern, slug, fns, dts, logicBearing.Count, decompiled.Count,
                          decompile, truncated, maxFuncs, decompFile, decompiledLocs);
        File.WriteAllText(docPath, doc);

        // -- Summary. --
        Console.WriteLine($"Subsystem /{pattern}/:");
        Console.WriteLine($"  classes matched : {fns.Select(f => f.Ns).Distinct().Count()} (+ {dts.Count} struct layouts)");
        Console.WriteLine($"  functions       : {fns.Count} total, {logicBearing.Count} logic-bearing");
        Console.WriteLine($"  decompiled      : {(decompile ? decompiled.Count.ToString() : "0 (pass --decompile)")}");
        Console.WriteLine($"  model skeleton  : {docPath}");
        if (decompFile is not null)
            Console.WriteLine($"  raw decompile   : {decompFile}");
        Console.WriteLine();
        Console.WriteLine("Next: read the model skeleton, fill the MODEL section from the decompile,");
        Console.WriteLine("then curate it into docs/llm-docs/ when the model is confirmed.");
        return 0;
    }

    // ------------------------------------------------------------------
    // SARIF query — one jq pass emitting NDJSON of fn/dt records.
    // ------------------------------------------------------------------
    private static (List<FnRec>, List<DtRec>) QuerySarif(string jq, string sarif, string pattern)
    {
        // Single pass: FUNCTIONS matched on namespace, DATATYPE matched on name.
        const string program = """
            .runs[0].results[]
            | .properties.additionalProperties as $p
            | if .ruleId == "FUNCTIONS" then
                (if (($p.namespace // "") | test($pat)) then
                  {kind:"fn", loc:($p.location // ""), ns:($p.namespace // "Global"),
                   name:($p.name // ""), sig:($p.value // "")}
                else empty end)
              elif .ruleId == "DATATYPE" then
                (if (($p.name // "") | test($pat)) then
                  {kind:"dt", name:($p.name // ""), fields:($p.fields // {})}
                else empty end)
              else empty end
            """;

        var psi = new ProcessStartInfo
        {
            FileName = jq,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        psi.ArgumentList.Add("-c");
        psi.ArgumentList.Add("--arg");
        psi.ArgumentList.Add("pat");
        psi.ArgumentList.Add(pattern);
        psi.ArgumentList.Add(program);
        psi.ArgumentList.Add(sarif);

        using var proc = Process.Start(psi)
            ?? throw new InvalidOperationException($"could not start jq ({jq})");
        var fns = new List<FnRec>();
        var dts = new List<DtRec>();
        string? line;
        while ((line = proc.StandardOutput.ReadLine()) is not null)
        {
            if (line.Length == 0) continue;
            using var doc = JsonDocument.Parse(line);
            var root = doc.RootElement;
            var kind = root.GetProperty("kind").GetString();
            if (kind == "fn")
            {
                fns.Add(new FnRec(
                    root.GetProperty("loc").GetString() ?? "",
                    root.GetProperty("ns").GetString() ?? "Global",
                    root.GetProperty("name").GetString() ?? "",
                    root.GetProperty("sig").GetString() ?? ""));
            }
            else if (kind == "dt")
            {
                var name = root.GetProperty("name").GetString() ?? "";
                var fields = new List<FieldRec>();
                if (root.TryGetProperty("fields", out var fobj) && fobj.ValueKind == JsonValueKind.Object)
                {
                    foreach (var prop in fobj.EnumerateObject())
                    {
                        var v = prop.Value;
                        int offset = v.TryGetProperty("offset", out var o) && o.TryGetInt32(out var oi) ? oi : -1;
                        string type = "?";
                        if (v.TryGetProperty("type", out var t))
                        {
                            if (t.TryGetProperty("name", out var tn) && tn.ValueKind == JsonValueKind.String)
                                type = tn.GetString() ?? "?";
                            else if (t.TryGetProperty("kind", out var tk) && tk.ValueKind == JsonValueKind.String)
                                type = tk.GetString() ?? "?";
                        }
                        fields.Add(new FieldRec(offset, prop.Name, type));
                    }
                }
                fields.Sort((a, b) => a.Offset.CompareTo(b.Offset));
                dts.Add(new DtRec(name, fields));
            }
        }
        var stderr = proc.StandardError.ReadToEnd();
        proc.WaitForExit();
        if (proc.ExitCode != 0)
            throw new InvalidOperationException(
                $"jq exited {proc.ExitCode}: {stderr.Trim()}");
        return (fns, dts);
    }

    // ------------------------------------------------------------------
    // Ghidra headless decompile of a batch of addresses via Decompile.java.
    // ------------------------------------------------------------------
    private static string RunGhidraDecompile(KdevConfig cfg, List<string> addrs)
    {
        if (!File.Exists(cfg.ReGhidraHeadless))
            throw new FileNotFoundException($"Ghidra headless not found: {cfg.ReGhidraHeadless}");

        // analyzeHeadless is a .bat — run it through cmd.exe (same pattern as
        // BuildCommand's vcvars call). Quote every path; append the addresses.
        var sb = new StringBuilder();
        sb.Append("/c \"");
        sb.Append('"').Append(cfg.ReGhidraHeadless).Append('"');
        sb.Append(' ').Append('"').Append(cfg.ReGhidraProjectDir).Append('"');
        sb.Append(' ').Append(cfg.ReGhidraProjectName);
        sb.Append(" -process ").Append(cfg.ReGhidraProgram);
        sb.Append(" -readOnly");
        sb.Append(" -scriptPath ").Append('"').Append(cfg.ReGhidraScripts).Append('"');
        sb.Append(" -postScript Decompile.java");
        foreach (var a in addrs) sb.Append(' ').Append(a);
        sb.Append('"');

        var psi = new ProcessStartInfo
        {
            FileName = "cmd.exe",
            Arguments = sb.ToString(),
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        var outBuf = new StringBuilder();
        using var proc = Process.Start(psi)
            ?? throw new InvalidOperationException("could not start cmd.exe for Ghidra");
        proc.OutputDataReceived += (_, e) => { if (e.Data is not null) outBuf.AppendLine(e.Data); };
        proc.ErrorDataReceived  += (_, e) => { /* Ghidra is chatty on stderr; ignore */ };
        proc.BeginOutputReadLine();
        proc.BeginErrorReadLine();
        proc.WaitForExit();
        var raw = outBuf.ToString();
        if (!raw.Contains("===== DECOMP"))
            throw new InvalidOperationException(
                "Ghidra produced no decompile output (no '===== DECOMP' marker). " +
                "Check the project/program names and that Decompile.java is on the script path.");
        return raw;
    }

    /// <summary>
    /// Strip Ghidra's headless log framing, leaving the decompile.
    ///
    /// Ghidra's script logger only frames the FIRST line of each multi-line
    /// <c>println</c> with "INFO  Decompile.java> ... (GhidraScript)"; the C body
    /// lines come through raw (no prefix). So: skip the analysis preamble up to
    /// the first "===== DECOMP" delimiter, strip the frame where present, keep
    /// raw body lines verbatim, and drop only interleaved INFO/WARN/ERROR log
    /// noise (which never legitimately starts a line of decompiled C).
    /// </summary>
    private static string CleanGhidraOutput(string raw)
    {
        var sb = new StringBuilder();
        var started = false;
        foreach (var rawLine in raw.Split('\n'))
        {
            var line = rawLine.TrimEnd('\r');
            int idx = line.IndexOf(DecompMarker, StringComparison.Ordinal);
            var framed = idx >= 0;

            string payload;
            if (framed)
            {
                payload = line.Substring(idx + DecompMarker.Length).TrimEnd();
                if (payload.EndsWith(GhidraSuffix, StringComparison.Ordinal))
                    payload = payload.Substring(0, payload.Length - GhidraSuffix.Length).TrimEnd();
            }
            else
            {
                payload = line;
            }

            if (!started)
            {
                if (payload.StartsWith("===== DECOMP", StringComparison.Ordinal)) started = true;
                else continue;  // skip analysis/log preamble
            }

            // Drop Ghidra's own log lines interleaved in the body; framed lines
            // (delimiters) are real output and kept.
            if (!framed)
            {
                var t = payload.TrimStart();
                if (t.StartsWith("INFO ", StringComparison.Ordinal) ||
                    t.StartsWith("WARN ", StringComparison.Ordinal) ||
                    t.StartsWith("ERROR ", StringComparison.Ordinal))
                    continue;
            }

            sb.AppendLine(payload);
        }
        return sb.ToString();
    }

    // ------------------------------------------------------------------
    // Classification + slug helpers.
    // ------------------------------------------------------------------
    private static bool IsTrivial(FnRec f)
    {
        var n = f.Name;
        if (n.StartsWith("Set", StringComparison.Ordinal)) return true;
        if (n.StartsWith("Get", StringComparison.Ordinal)) return true;
        if (n.StartsWith("~", StringComparison.Ordinal)) return true;     // dtor
        // Constructor: method name == class (last "::" segment of namespace).
        var cls = f.Ns;
        int sep = cls.LastIndexOf("::", StringComparison.Ordinal);
        if (sep >= 0) cls = cls.Substring(sep + 2);
        if (n == cls) return true;
        return false;
    }

    private static string Slug(string pattern)
    {
        var sb = new StringBuilder();
        foreach (var c in pattern.ToLowerInvariant())
        {
            if (char.IsLetterOrDigit(c)) sb.Append(c);
            else if (sb.Length > 0 && sb[^1] != '-') sb.Append('-');
        }
        var s = sb.ToString().Trim('-');
        if (s.Length == 0) s = "subsystem";
        if (s.Length > 48) s = s.Substring(0, 48).TrimEnd('-');
        return s;
    }

    // ------------------------------------------------------------------
    // Model-skeleton doc emission.
    // ------------------------------------------------------------------
    private static string EmitDoc(
        string pattern, string slug, List<FnRec> fns, List<DtRec> dts,
        int logicCount, int decompiledCount, bool decompiled, bool truncated,
        int maxFuncs, string? decompFile, HashSet<string> decompiledLocs)
    {
        var sb = new StringBuilder();
        var classes = fns.Select(f => f.Ns).Distinct().OrderBy(x => x, StringComparer.Ordinal).ToList();

        sb.AppendLine($"# RE model skeleton — `{pattern}`");
        sb.AppendLine();
        sb.AppendLine($"Generated by `kdev re \"{pattern}\"`. This is a SKELETON: read the decompile,");
        sb.AppendLine("fill the MODEL section, then curate into `docs/llm-docs/` once confirmed.");
        sb.AppendLine("Reminder: measure the live game only to CONFIRM this model or read an exact");
        sb.AppendLine("engine value — never to infer it (memory `feedback_reconstruct_engine_structures_first`).");
        sb.AppendLine();
        sb.AppendLine("## Coverage");
        sb.AppendLine($"- Classes matched: {classes.Count}");
        sb.AppendLine($"- Functions: {fns.Count} total, {logicCount} logic-bearing");
        sb.AppendLine($"- Struct layouts: {dts.Count}");
        if (decompiled)
        {
            sb.AppendLine($"- Decompiled: {decompiledCount}" +
                          (truncated ? $" (capped at --max-funcs {maxFuncs}; raise it for the rest)" : "")
                          + $" → see `{Path.GetFileName(decompFile!)}`");
        }
        else
        {
            sb.AppendLine("- Decompiled: none — re-run with `--decompile` (and `--all` for setters/getters too).");
        }
        sb.AppendLine();

        sb.AppendLine("## MODEL (fill this in from the decompile — input → state → action → effect)");
        sb.AppendLine();
        sb.AppendLine("- **Entry / lifecycle:** which function constructs/loads it, what `.are`/GFF");
        sb.AppendLine("  fields configure it, the per-frame Update/Control entry point.");
        sb.AppendLine("- **Input path:** how player input reaches state (which field it writes, in what units).");
        sb.AppendLine("- **State update:** the integration/tick that mutates that state each frame.");
        sb.AppendLine("- **Action / effect:** how state becomes the observable result (spawn, hit, render).");
        sb.AppendLine("- **The exact runtime quantity to read for accessibility** (a node transform, a");
        sb.AppendLine("  field) — and the one to WRITE for assist, plus when it gets clobbered.");
        sb.AppendLine("- **Gotchas / prior misconceptions:** what a naive runtime measurement would");
        sb.AppendLine("  mislead you into believing, and why the code says otherwise.");
        sb.AppendLine();

        sb.AppendLine("## Function inventory");
        sb.AppendLine("(`*` = decompiled this run. Address is the Ghidra entry point.)");
        sb.AppendLine();
        foreach (var cls in classes)
        {
            sb.AppendLine($"### {cls}");
            foreach (var f in fns.Where(f => f.Ns == cls))
            {
                var mark = decompiledLocs.Contains(f.Loc) ? "* " : "  ";
                var triv = IsTrivial(f) ? " _(trivial)_" : "";
                sb.AppendLine($"- {mark}`{f.Loc}` **{f.Name}** — `{f.Sig}`{triv}");
            }
            sb.AppendLine();
        }

        sb.AppendLine("## Struct layouts");
        sb.AppendLine();
        if (dts.Count == 0)
        {
            sb.AppendLine("_(no datatypes matched the pattern — widen it to include the struct names)_");
            sb.AppendLine();
        }
        foreach (var d in dts)
        {
            sb.AppendLine($"### {d.Name}");
            foreach (var fld in d.Fields)
            {
                var off = fld.Offset >= 0 ? $"+0x{fld.Offset:x} ({fld.Offset})" : "?";
                sb.AppendLine($"- `{off}` {fld.Name} : {fld.Type}");
            }
            sb.AppendLine();
        }

        sb.AppendLine("## Raw decompile");
        if (decompiled)
            sb.AppendLine($"See `{Path.GetFileName(decompFile!)}` (delimited by `===== DECOMP ... =====`).");
        else
            sb.AppendLine("Not generated. Re-run with `--decompile` to produce `" + slug + ".decomp.txt`.");
        sb.AppendLine();
        return sb.ToString();
    }
}
