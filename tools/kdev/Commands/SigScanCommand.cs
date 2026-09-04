using System.CommandLine;
using System.CommandLine.Invocation;
using System.Text;
using System.Text.Json;

namespace Kdev.Commands;

/// <summary>
/// Rebase our hardcoded engine addresses onto a different build of swkotor.exe.
///
/// Why
/// ---
/// Supporting another executable (the Allard Russian exe first; any future
/// re-pack after) is not a re-reverse-engineering job — the functions are
/// byte-identical, the linker just put them elsewhere. All 25 detour
/// signatures were found in the Allard exe displaced by +96..+512 bytes, with
/// the displacement varying per function. So the work is mechanical: for each
/// address, take a relocation-tolerant signature from a known-good image and
/// find where it landed.
///
/// The two halves
/// --------------
///   kdev sigscan --emit    reference image -> signatures  (needs `dump-text`,
///                          because the Steam exe is SteamStub-encrypted on
///                          disk and only readable decrypted from memory)
///   kdev sigscan --resolve signatures -> addresses in a target exe
///
/// Both run in one invocation when given both inputs.
///
/// Reading the report
/// ------------------
/// Only `unique` is usable. `ambiguous` and `not-found` must be resolved by
/// hand before the target exe can be supported, because most of these
/// addresses are reinterpret_cast to function pointers and CALLED — a wrong
/// answer is a crash inside another function, not a no-op. The 25 hook sites
/// are the safe subset: the patcher verifies their bytes and refuses on a
/// mismatch. That is why this command never guesses a best candidate.
///
/// Usage
/// -----
///   kdev sigscan                                   # both halves, default paths
///   kdev sigscan --target "C:\...\swkotor.exe"     # rebase onto a specific exe
///   kdev sigscan --emit-only                       # just build + audit signatures
/// </summary>
public static class SigScanCommand
{
    public static Command Build()
    {
        var refOpt = new Option<FileInfo?>("--reference",
            "Reference image dump from `kdev dump-text` (default: build/re/imagedump/swkotor-image.bin).");
        var targetOpt = new Option<FileInfo?>("--target",
            "Target swkotor.exe to rebase onto. Omit to only build signatures.");
        var outOpt = new Option<DirectoryInfo?>("--out",
            "Output directory (default: build/re/sigscan).");
        var emitOnlyOpt = new Option<bool>("--emit-only",
            "Build and audit signatures against the reference; skip resolving.");
        var k2Opt = new Option<bool>("--k2",
            "KOTOR 2 mode: harvest the K2-side addresses (Pick/Kotor2Only args + " +
            "kotor2.hooks.toml), reference the clean Steam Aspyr exe, resolve into the " +
            "GOG 2021 relink. Defaults: --reference C:\\Tools\\k2re\\swkotor2.exe, " +
            "--target the GOG Galaxy install.");

        var cmd = new Command("sigscan",
            "Rebase our hardcoded engine addresses onto another swkotor.exe build.")
        {
            refOpt, targetOpt, outOpt, emitOnlyOpt, k2Opt
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            ctx.ExitCode = Run(
                ctx.ParseResult.GetValueForOption(refOpt),
                ctx.ParseResult.GetValueForOption(targetOpt),
                ctx.ParseResult.GetValueForOption(outOpt),
                ctx.ParseResult.GetValueForOption(emitOnlyOpt),
                ctx.ParseResult.GetValueForOption(k2Opt));
        });
        return cmd;
    }

    /// <summary>KOTOR 2 mode default paths. Both are hash-verified clean copies
    /// (docs/kotor2-port.md "GOG build"); overridable with --reference/--target.
    /// On-disk files are fine here — the Aspyr exes are not SteamStub-encrypted,
    /// so no memory dump is needed, unlike KOTOR 1.</summary>
    private const string K2ReferenceDefault = @"C:\Tools\k2re\swkotor2.exe";
    private const string K2TargetDefault =
        @"C:\Program Files\GOG Galaxy\Games\Star Wars - KotOR2\swkotor2.exe";

    internal static int Run(FileInfo? refFile, FileInfo? targetFile, DirectoryInfo? outDir,
        bool emitOnly, bool k2 = false)
    {
        var cfg = KdevConfig.LoadOrPrintErrors(out int cfgExit);
        if (cfg == null) return cfgExit;

        string refPath = refFile?.FullName
            ?? (k2 ? K2ReferenceDefault
                   : Path.Combine(cfg.ProjectRoot, "build", "re", "imagedump", "swkotor-image.bin"));
        string dest = outDir?.FullName
            ?? Path.Combine(cfg.ProjectRoot, "build", "re", k2 ? "sigscan-k2" : "sigscan");
        if (k2 && !emitOnly && targetFile == null) targetFile = new FileInfo(K2TargetDefault);

        if (!File.Exists(refPath))
        {
            Console.Error.WriteLine($"ERROR: reference image not found at {refPath}");
            Console.Error.WriteLine(k2
                ? "KOTOR 2 mode expects the clean Steam Aspyr exe there (docs/kotor2-port.md)."
                : "Start the game, then run: kdev dump-text");
            return 1;
        }
        Directory.CreateDirectory(dest);

        // ---- reference ----
        byte[] refImage = File.ReadAllBytes(refPath);
        PeInfo refPe;
        try { refPe = PeInfo.Parse(refImage); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"ERROR: reference image is not a readable PE: {ex.Message}");
            return 1;
        }
        var refText = refPe.SectionNamed(".text");
        if (refText == null)
        {
            Console.Error.WriteLine("ERROR: reference image has no .text section.");
            return 1;
        }

        // A memory dump stores each section at its VirtualAddress and is
        // exactly SizeOfImage long; an on-disk exe stores them at
        // PointerToRawData. Getting this wrong reads the wrong bytes silently
        // rather than failing, so decide it explicitly and say which was used.
        bool refImageAligned = refImage.Length >= refPe.SizeOfImage;

        Console.WriteLine($"Reference: {refPath}");
        Console.WriteLine($"  image base 0x{refPe.ImageBase:X8}, link timestamp {refPe.TimestampUtc:u}");
        Console.WriteLine($"  layout: {(refImageAligned ? "memory dump (RVA-indexed)" : "on-disk file (raw-offset indexed)")}");
        if (!refImageAligned && refPe.SectionNamed(".bind") != null)
        {
            Console.Error.WriteLine("  WARNING: reference is a SteamStub'd file on disk. Its .text is");
            Console.Error.WriteLine("  ciphertext — signatures built from it are meaningless.");
        }

        // ---- addresses ----
        var addresses = k2
            ? EngineAddresses.CollectKotor2(cfg.AccessibilitySource, cfg.ProjectRoot)
            : EngineAddresses.Collect(cfg.AccessibilitySource, cfg.ProjectRoot);
        // The hand-resolved supplement is the K1 kXrefTable; K2 has no analog
        // yet (anything sigscan cannot place there is resolved by the .rdata /
        // .data cross-reference scripts instead).
        s_handResolved = k2 ? new() : EngineAddresses.CollectHandResolved(cfg.AccessibilitySource);
        // Merge duplicates by address. A hook site and a constexpr constant can
        // name the same address; keep whichever record carries OriginalBytes so
        // the hooks.toml cross-check below still sees all 25 detours rather
        // than only those that happen to sort first.
        var byVa = addresses
            .GroupBy(a => a.Va)
            .Select(g =>
            {
                var withBytes = g.FirstOrDefault(x => x.OriginalBytes != null) ?? g.First();
                return withBytes with
                {
                    Name = string.Join(" / ", g.Select(x => x.Name).Distinct())
                };
            })
            .OrderBy(a => a.Va)
            .ToList();

        int inText = byVa.Count(a => IsIn(refPe, refText, a.Va));
        Console.WriteLine($"  {addresses.Count} references, {byVa.Count} distinct addresses, " +
                          $"{inText} in .text (the rest are data and need no rebasing)");
        Console.WriteLine();

        // ---- build signatures ----
        var records = new List<SigRecord>();
        int built = 0, failedBuild = 0, skippedData = 0;

        foreach (var a in byVa)
        {
            if (!IsIn(refPe, refText, a.Va))
            {
                var sec = refPe.SectionForRva(a.Va - refPe.ImageBase);
                records.Add(new SigRecord(a, null, "data", sec?.Name ?? "(outside image)", null, null));
                skippedData++;
                continue;
            }

            var r = Signatures.Build(refImage, refPe, refImageAligned, a.Va, refText,
                                     maskAllImageRefs: k2);
            if (r.Failure != null)
            {
                // A non-unique pattern is still returned and still useful for
                // the ordinal pass; a decode failure yields nothing.
                records.Add(new SigRecord(a, r.Signature, "build-failed", r.Failure, null, null));
                failedBuild++;
                continue;
            }

            // Free cross-check: for the 25 detours, hooks.toml already records
            // the bytes the patcher verifies. If our generated signature
            // disagrees with those, something is wrong with the reference dump
            // or the decoder, and every other signature is suspect too.
            // Build succeeded, so a signature is always present here; the null
            // case is the decode-failure branch above.
            string? mismatch = r.Signature == null ? null : CheckAgainstOriginalBytes(a, r.Signature);
            records.Add(new SigRecord(a, r.Signature, "ok", mismatch, null, null));
            built++;
        }

        Console.WriteLine($"Signatures: {built} built, {failedBuild} failed, {skippedData} data addresses skipped");

        var hookChecks = records.Where(x => x.Address.OriginalBytes != null).ToList();
        int hookBad = hookChecks.Count(x => x.Note != null && x.Status == "ok");
        Console.WriteLine($"  hooks.toml cross-check: {hookChecks.Count - hookBad}/{hookChecks.Count} " +
                          "generated signatures agree with the recorded original_bytes");
        if (hookBad > 0)
        {
            Console.Error.WriteLine("  WARNING: disagreement means the reference dump or decoder is wrong.");
            foreach (var x in hookChecks.Where(x => x.Note != null && x.Status == "ok"))
                Console.Error.WriteLine($"    0x{x.Address.Va:X8} {x.Address.Name}: {x.Note}");
        }

        if (built > 0)
        {
            var lens = records.Where(x => x.Signature != null).Select(x => x.Signature!.Length).Order().ToList();
            Console.WriteLine($"  length: min {lens.First()}, median {lens[lens.Count / 2]}, max {lens.Last()} bytes");
        }
        Console.WriteLine();

        // ---- resolve ----
        if (!emitOnly && targetFile != null)
        {
            if (!targetFile.Exists)
            {
                Console.Error.WriteLine($"ERROR: target not found: {targetFile.FullName}");
                return 1;
            }
            int rc = Resolve(records, targetFile.FullName, refImage, refPe, refImageAligned, refText);
            WriteReports(dest, records, refPath, targetFile.FullName, k2, cfg.AccessibilitySource);
            // Resolve() owns the verdict once it has run. A signature that
            // failed to build is not a failure if the ordinal pass or the
            // hand-resolved supplement placed the address anyway — what matters
            // is whether anything ended up unmapped, which is exactly what
            // Resolve() returns 7 for.
            return rc;
        }
        else if (!emitOnly && targetFile == null)
        {
            Console.WriteLine("No --target given; skipping the resolve half.");
        }

        WriteReports(dest, records, refPath, targetFile?.FullName, k2, cfg.AccessibilitySource);
        return failedBuild == 0 ? 0 : 1;
    }

    private static bool IsIn(PeInfo pe, PeSection sec, uint va)
    {
        if (va < pe.ImageBase) return false;
        uint rva = va - pe.ImageBase;
        uint span = Math.Max(sec.VirtualSize, sec.RawSize);
        return rva >= sec.VirtualAddress && rva < sec.VirtualAddress + span;
    }

    private static string? CheckAgainstOriginalBytes(EngineAddress a, Signature sig)
    {
        if (a.OriginalBytes == null) return null;
        int n = Math.Min(a.OriginalBytes.Length, sig.Length);
        for (int i = 0; i < n; i++)
        {
            if (!sig.Mask[i]) continue;  // wildcarded operand, nothing to compare
            if (sig.Pattern[i] != a.OriginalBytes[i])
                return $"byte {i}: hooks.toml says {a.OriginalBytes[i]:x2}, reference has {sig.Pattern[i]:x2}";
        }
        return null;
    }

    /// <summary>
    /// Reference-to-target pairs already resolved by hand in engine_rebase.cpp's
    /// kXrefTable. Read once in Run(); consumed by Resolve() so those addresses
    /// are not reported as missing on every run. See
    /// <see cref="EngineAddresses.CollectHandResolved"/>.
    /// </summary>
    private static Dictionary<uint, uint> s_handResolved = new();

    /// <summary>Preceding-context window lengths for the call-site
    /// cross-reference pass, longest (most discriminating) first.</summary>
    private static readonly int[] s_xrefContextLengths = { 16, 14, 12, 10, 8, 6, 5 };

    private static int Resolve(List<SigRecord> records, string targetPath,
        byte[] refImage, PeInfo refPe, bool refImageAligned, PeSection refText)
    {
        byte[] img = File.ReadAllBytes(targetPath);
        PeInfo pe;
        try { pe = PeInfo.Parse(img); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"ERROR: target is not a readable PE: {ex.Message}");
            return 1;
        }
        var text = pe.SectionNamed(".text");
        if (text == null)
        {
            Console.Error.WriteLine("ERROR: target has no .text section.");
            return 1;
        }

        Console.WriteLine($"Target: {targetPath}");
        Console.WriteLine($"  image base 0x{pe.ImageBase:X8}, link timestamp {pe.TimestampUtc:u}");
        if (pe.SectionNamed(".bind") != null)
        {
            Console.Error.WriteLine("  WARNING: target has a .bind section (SteamStub). Its .text on disk is");
            Console.Error.WriteLine("  ciphertext, so nothing will resolve. Dump it from memory instead.");
        }

        int unique = 0, none = 0, ambiguous = 0, ordinal = 0;
        foreach (var rec in records)
        {
            if (rec.Signature == null) continue;

            var hits = new List<uint>();
            Signatures.CountMatches(img, pe, imageAligned: false, text,
                rec.Signature.Pattern, rec.Signature.Mask, limit: 8, hitsOut: hits);

            if (hits.Count == 1)
            {
                rec.Resolved = hits[0];
                rec.Status = "unique";
                unique++;
            }
            else if (hits.Count == 0)
            {
                rec.Status = "not-found";
                none++;
            }
            else
            {
                rec.Status = "ambiguous";
                rec.Candidates = hits;
                ambiguous++;
            }
        }

        // Call-site cross-reference pass — the automated form of the method
        // engine_rebase.cpp's kXrefTable documents by hand: find every
        // `call rel32` to the address in the reference, locate each call site
        // in the target by the bytes PRECEDING it, decode the displacement
        // there, and let the sites vote. Runs BEFORE the ordinal fallback
        // because ordinal matching pairs identical code by address ORDER,
        // which the KOTOR 2 GOG relink's ±4MB module shuffle does not
        // preserve — measured 2026-09-04: the ordinal answer for one of eight
        // identical thunks was refuted 72:1 by exactly this cross-reference.
        // A few false-context votes are expected (near-identical caller code),
        // so the winner needs at least two votes and a ≥90% majority.
        int xrefResolved = 0;
        {
            int rStart = refImageAligned ? (int)refText.VirtualAddress : (int)refText.RawPointer;

            int rLen = Math.Min(
                (int)(refImageAligned ? Math.Max(refText.VirtualSize, refText.RawSize) : refText.RawSize),
                refImage.Length - rStart);
            uint rVaBase = refPe.ImageBase + refText.VirtualAddress;
            int tStart = (int)text.RawPointer;
            int tLen = Math.Min((int)text.RawSize, img.Length - tStart);
            uint tVaBase = pe.ImageBase + text.VirtualAddress;

            foreach (var rec in records.Where(r => r.Status is "ambiguous" or "not-found" or "build-failed").ToList())
            {
                // consumed exits move with their hook; the delta pass below is
                // their resolver (they are jump targets, not call targets).
                if (rec.Address.Name.EndsWith(" (consumed exit)")) continue;

                var votes = new Dictionary<uint, int>();
                int sites = 0;
                for (int i = 0; i + 5 <= rLen; i++)
                {
                    if (refImage[rStart + i] != 0xE8) continue;
                    int rel = BitConverter.ToInt32(refImage, rStart + i + 1);
                    if ((uint)(rVaBase + (uint)i + 5 + rel) != rec.Address.Va) continue;
                    sites++;
                    // Progressively shorter preceding-context windows: long
                    // ones can embed another relocated operand (no hit),
                    // short ones repeat (ambiguous). First length that is
                    // UNIQUE in the target wins.
                    foreach (int ctx in s_xrefContextLengths)
                    {
                        if (i < ctx) continue;
                        var needle = refImage.AsSpan(rStart + i - ctx, ctx);
                        int hit = img.AsSpan(tStart, tLen).IndexOf(needle);
                        if (hit < 0) continue;
                        int again = img.AsSpan(tStart + hit + 1, tLen - hit - 1).IndexOf(needle);
                        if (again >= 0) break;
                        int callOff = tStart + hit + ctx;
                        if (callOff + 5 > tStart + tLen || img[callOff] != 0xE8) break;
                        int grel = BitConverter.ToInt32(img, callOff + 1);
                        uint cand = (uint)(tVaBase + (uint)(callOff - tStart) + 5 + grel);
                        votes[cand] = votes.GetValueOrDefault(cand) + 1;
                        break;
                    }
                }
                if (votes.Count == 0) continue;
                var best = votes.MaxBy(kv => kv.Value);
                int total = votes.Values.Sum();
                if (best.Value < 2 || best.Value * 10 < total * 9) continue;

                if (rec.Status == "ambiguous") { ambiguous--; rec.Candidates = null; }
                else if (rec.Status == "not-found") { none--; }
                rec.Resolved = best.Key;
                rec.Status = "xref";
                rec.Note = $"call-site cross-reference: {best.Value} of {total} located callers " +
                           $"agree ({sites} reference call sites total" +
                           (total > best.Value ? $", {total - best.Value} false-context dissenter(s) discounted" : "") +
                           ").";
                xrefResolved++;
            }
        }

        // Ordinal fallback. Some functions are byte-identical to another for
        // the whole window (duplicate code the linker folded), so no
        // content-based signature can separate them. But if a signature
        // matches the SAME NUMBER of sites in reference and target, the two
        // sets almost certainly correspond in address order, and our address's
        // rank in the reference gives us the target site.
        //
        // This is an inference, not a proof, so results are labelled
        // `unique-ordinal` and never silently folded into `unique`.
        foreach (var rec in records.Where(r => r.Signature != null &&
                                               r.Status is "ambiguous" or "build-failed"))
        {
            var sig = rec.Signature!;
            var refHits = new List<uint>();
            Signatures.CountMatches(refImage, refPe, refImageAligned, refText,
                sig.Pattern, sig.Mask, limit: 16, hitsOut: refHits);

            var tgtHits = new List<uint>();
            Signatures.CountMatches(img, pe, imageAligned: false, text,
                sig.Pattern, sig.Mask, limit: 16, hitsOut: tgtHits);

            if (refHits.Count == 0 || refHits.Count != tgtHits.Count) continue;
            refHits.Sort();
            tgtHits.Sort();
            int rank = refHits.IndexOf(rec.Address.Va);
            if (rank < 0) continue;

            rec.Resolved = tgtHits[rank];
            rec.Status = "unique-ordinal";
            rec.Note = $"{refHits.Count} identical sites in both binaries; matched by address order " +
                       $"(rank {rank + 1} of {refHits.Count}). Inference — confirm before shipping.";
            if (ambiguous > 0 && rec.Candidates != null) { ambiguous--; rec.Candidates = null; }
            else none--;
            ordinal++;
        }

        // Consumed exits that would not resolve by content: a consumed exit is
        // a point INSIDE its hook's function, and the whole function is
        // byte-identical between builds — so it moved by exactly its hook's
        // delta. A short epilogue's signature only becomes unique by growing
        // past the RET into the next function, which is precisely the part a
        // relink reorders, hence "not-found" here. Derive the address from the
        // hook's delta instead, then verify the epilogue bytes themselves
        // (masked, up to and including the RET) at the derived address. This is
        // the same method the 2004-relink port used by hand (see
        // relink2004.hooks.toml's header); automated so a regenerated file
        // cannot silently skip the verification step.
        int deltaDerived = 0;
        foreach (var rec in records.Where(r => r.Status is "not-found" or "ambiguous" &&
                                              r.Address.Name.EndsWith(" (consumed exit)")))
        {
            string fn = rec.Address.Name[..^" (consumed exit)".Length];
            var hook = records.FirstOrDefault(h =>
                h.Address.OriginalBytes != null && h.Resolved.HasValue &&
                h.Address.Name.Split(" / ").Contains(fn));
            if (hook == null) continue;

            long delta = (long)hook.Resolved!.Value - hook.Address.Va;
            uint candidate = (uint)(rec.Address.Va + delta);

            var epi = Signatures.BuildUntilRet(refImage, refPe, refImageAligned,
                                               rec.Address.Va, maskAllImageRefs: true);
            if (epi == null)
            {
                rec.Note = "consumed exit: no RET within 128 bytes of the reference " +
                           "address; cannot delta-verify. Resolve by hand.";
                continue;
            }
            if (!Signatures.MatchesAt(img, pe, imageAligned: false, candidate, epi))
            {
                rec.Note = $"consumed exit: hook delta {delta:+#;-#;0} puts it at " +
                           $"0x{candidate:X8}, but the epilogue bytes there do NOT match. " +
                           "Resolve by hand.";
                continue;
            }

            if (rec.Status == "ambiguous") { ambiguous--; rec.Candidates = null; } else { none--; }
            rec.Resolved = candidate;
            rec.Status = "delta-derived";
            rec.Note = $"consumed exit of {fn}: moved with its function " +
                       $"({delta:+#;-#;0} bytes, same as the hook site); epilogue bytes " +
                       $"({epi.Length}b, {epi.ConcreteBytes} concrete, through the RET) " +
                       "verified at the derived address.";
            deltaDerived++;
        }
        if (deltaDerived > 0)
            Console.WriteLine($"  delta-derived consumed exits: {deltaDerived} (epilogue-byte-verified)");

        // Anything still unresolved that engine_rebase.cpp's kXrefTable already
        // maps by hand is accounted for, not missing. Reclassify before counting
        // so the failure exit below means "an address is genuinely unmapped"
        // rather than "the two permanent hand-resolved ones are still here".
        int handResolved = 0;
        foreach (var rec in records.Where(r => r.Status is "ambiguous" or "not-found"))
        {
            if (!s_handResolved.TryGetValue(rec.Address.Va, out uint target)) { continue; }
            if (rec.Status == "ambiguous") { ambiguous--; } else { none--; }
            rec.Status = "hand-resolved";
            rec.Resolved = target;
            rec.Candidates = null;
            rec.Note = "resolved by call-site cross-reference in engine_rebase.cpp's " +
                       "kXrefTable; sigscan cannot place it by its own bytes.";
            handResolved++;
        }

        Console.WriteLine();
        Console.WriteLine($"  unique    : {unique}");
        if (xrefResolved > 0)
            Console.WriteLine($"  by xref   : {xrefResolved}  (agreeing call-site votes — see report)");
        if (ordinal > 0) Console.WriteLine($"  by ordinal: {ordinal}  (inferred — see report)");
        if (handResolved > 0)
            Console.WriteLine($"  hand-fixed: {handResolved}  (kXrefTable in engine_rebase.cpp)");
        Console.WriteLine($"  ambiguous : {ambiguous}");
        Console.WriteLine($"  not-found : {none}");

        if (unique > 0)
        {
            var deltas = records
                .Where(r => r.Status is "unique" or "unique-ordinal" or "xref")
                .Select(r => (long)r.Resolved!.Value - r.Address.Va)
                .Order()
                .ToList();
            Console.WriteLine($"  displacement: min {deltas.First():+#;-#;0}, " +
                              $"median {deltas[deltas.Count / 2]:+#;-#;0}, " +
                              $"max {deltas.Last():+#;-#;0} bytes");
        }

        if (ambiguous + none > 0)
        {
            Console.WriteLine();
            Console.WriteLine("Unresolved — these MUST be settled by hand before the target is supported.");
            Console.WriteLine("Most of these addresses are called as function pointers, so a wrong");
            Console.WriteLine("answer crashes rather than degrading. See the report for candidates.");
            foreach (var r in records.Where(r => r.Status is "ambiguous" or "not-found").Take(25))
            {
                string extra = r.Candidates == null
                    ? ""
                    : "  candidates: " + string.Join(", ", r.Candidates.Select(c => $"0x{c:X8}"));
                Console.WriteLine($"  {r.Status,-10} 0x{r.Address.Va:X8}  {r.Address.Name}{extra}");
            }
            int shown = Math.Min(25, ambiguous + none);
            if (ambiguous + none > shown)
                Console.WriteLine($"  ... and {ambiguous + none - shown} more (full list in the report)");
        }

        // An unresolved .text address is a FAILURE, not a note. It used to exit
        // 0, which meant the one condition this command exists to detect was
        // also the one condition it stayed quiet about — a table could be
        // regenerated, look fine, and silently omit an address that then kept
        // its reference value on the rebased build. Exit non-zero so a script
        // (or a person skimming) cannot miss it. `--emit-only` runs never reach
        // here, so building signatures alone is still exit 0.
        if (ambiguous + none > 0)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine(
                $"ERROR: {ambiguous + none} of {unique + ordinal + ambiguous + none} .text addresses " +
                "did not resolve uniquely.");
            Console.Error.WriteLine(
                "       engine_rebase_table.inc was still written, but it is INCOMPLETE: every");
            Console.Error.WriteLine(
                "       address listed above is absent from it, and acc::addr::R() returns 0 for");
            Console.Error.WriteLine(
                "       an address it cannot map. Guard those call sites with acc::addr::Ok()");
            Console.Error.WriteLine(
                "       or resolve them by hand (see engine_rebase.cpp's kXrefTable for the");
            Console.Error.WriteLine(
                "       cross-reference technique) before shipping support for this build.");
            return 7;
        }

        return 0;
    }

    private static void WriteReports(string dest, List<SigRecord> records, string refPath,
        string? targetPath, bool k2 = false, string? sourceDir = null)
    {
        string txt = Path.Combine(dest, "sigscan-report.txt");
        string json = Path.Combine(dest, "sigscan-report.json");

        var sb = new StringBuilder();
        sb.AppendLine("# kdev sigscan report");
        sb.AppendLine($"# reference : {refPath}");
        sb.AppendLine($"# target    : {targetPath ?? "(none)"}");
        sb.AppendLine($"# generated : {DateTime.UtcNow:u}");
        sb.AppendLine("#");
        sb.AppendLine("# status    old address  new address  delta  name");
        sb.AppendLine("# '??' in a signature marks an operand that legitimately differs");
        sb.AppendLine("# between builds (relative branch target or absolute code pointer).");
        sb.AppendLine();

        foreach (var r in records.OrderBy(r => r.Address.Va))
        {
            string newAddr = r.Resolved.HasValue ? $"0x{r.Resolved.Value:X8}" : "-";
            string delta = r.Resolved.HasValue
                ? ((long)r.Resolved.Value - r.Address.Va).ToString("+#;-#;0")
                : "-";
            sb.AppendLine($"{r.Status,-11} 0x{r.Address.Va:X8}  {newAddr,-11}  {delta,6}  {r.Address.Name}");
            sb.AppendLine($"            source: {r.Address.Source}:{r.Address.Line}");
            if (r.Signature != null)
                sb.AppendLine($"            sig ({r.Signature.Length}b, {r.Signature.ConcreteBytes} concrete, " +
                              $"{r.Signature.InstructionCount} instr): {r.Signature}");
            if (r.Note != null) sb.AppendLine($"            note: {r.Note}");
            if (r.Candidates != null)
                sb.AppendLine($"            candidates: {string.Join(", ", r.Candidates.Select(c => $"0x{c:X8}"))}");
            sb.AppendLine();
        }
        File.WriteAllText(txt, sb.ToString());

        File.WriteAllText(json, JsonSerializer.Serialize(new
        {
            reference = refPath,
            target = targetPath,
            generatedUtc = DateTime.UtcNow.ToString("u"),
            entries = records.OrderBy(r => r.Address.Va).Select(r => new
            {
                name = r.Address.Name,
                source = $"{r.Address.Source}:{r.Address.Line}",
                oldAddress = $"0x{r.Address.Va:X8}",
                newAddress = r.Resolved.HasValue ? $"0x{r.Resolved.Value:X8}" : null,
                delta = r.Resolved.HasValue ? (long?)((long)r.Resolved.Value - r.Address.Va) : null,
                status = r.Status,
                note = r.Note,
                signature = r.Signature?.ToString(),
                candidates = r.Candidates?.Select(c => $"0x{c:X8}"),
            }),
        }, new JsonSerializerOptions { WriteIndented = true }));

        Console.WriteLine();
        Console.WriteLine($"Wrote {txt}");
        Console.WriteLine($"Wrote {json}");

        if (targetPath == null) return;
        if (k2) EmitK2Artifacts(dest, records, targetPath, sourceDir!);
        else EmitArtifacts(dest, records, targetPath);
    }

    /// <summary>
    /// KOTOR 2 mode artifacts: the Steam-K2 → GOG rebase table consumed by
    /// acc::addr's K2 lookup, and kotor2-gog.hooks.toml. Unlike the K1
    /// emitter's skeleton (parameters copied by hand afterwards), the hooks
    /// file is generated COMPLETE from kotor2.hooks.toml's parsed blocks —
    /// every field including parameters and the six consumed_exit_address
    /// values, with only the two address fields translated. Hand-copying
    /// hook contracts across 26 hooks is precisely where a port oversight
    /// would live, so no step of that remains manual.
    /// </summary>
    private static void EmitK2Artifacts(string dest, List<SigRecord> records,
        string targetPath, string sourceDir)
    {
        var resolvedMap = records
            .Where(r => r.Resolved.HasValue &&
                        r.Status is "unique" or "unique-ordinal" or "xref" or "delta-derived")
            .GroupBy(r => r.Address.Va)
            .ToDictionary(g => g.Key, g => g.First().Resolved!.Value);

        // ---- C++ rebase table (.text only; .rdata/.data come from the
        //      cross-reference scripts and land in their own .inc) ----
        var resolved = records
            .Where(r => r.Resolved.HasValue &&
                        r.Status is "unique" or "unique-ordinal" or "xref" or "delta-derived")
            .OrderBy(r => r.Address.Va)
            .ToList();

        string incPath = Path.Combine(dest, "engine_rebase_k2_table.inc");
        var inc = new StringBuilder();
        inc.AppendLine("// GENERATED by `kdev sigscan --k2` -- do not edit by hand.");
        inc.AppendLine($"// reference: KOTOR 2 Steam (Aspyr 2015); target: {Path.GetFileName(targetPath)} (GOG 2021 relink)");
        inc.AppendLine($"// generated: {DateTime.UtcNow:u}");
        inc.AppendLine("//");
        inc.AppendLine("// Left column: the Steam-K2 address as written at Pick()/Kotor2Only() call");
        inc.AppendLine("// sites. Right column: where the GOG relink put the same bytes. Sorted");
        inc.AppendLine("// ascending so the K2 lookup can binary-search. 'ordinal' entries were");
        inc.AppendLine("// matched by address order among identical code -- see the sigscan report.");
        inc.AppendLine();
        inc.AppendLine("// clang-format off");
        foreach (var r in resolved)
        {
            string kind = r.Status switch
            {
                "unique-ordinal" => "  // ordinal",
                "xref" => "  // xref",
                "delta-derived" => "  // delta-derived",
                _ => "",
            };
            inc.AppendLine($"    {{ 0x{r.Address.Va:X8}, 0x{r.Resolved!.Value:X8} }},  // {r.Address.Name}{kind}");
        }
        inc.AppendLine("// clang-format on");
        File.WriteAllText(incPath, inc.ToString());
        Console.WriteLine($"Wrote {incPath} ({resolved.Count} entries)");

        // ---- kotor2-gog.hooks.toml, complete ----
        string srcHooks = Path.Combine(sourceDir, "kotor2.hooks.toml");
        var blocks = HookFile.Parse(srcHooks);
        var missing = new List<string>();
        foreach (var b in blocks)
        {
            if (!resolvedMap.ContainsKey(b.Address))
                missing.Add($"0x{b.Address:X8} {b.Function}");
            if (b.ConsumedExitAddress.HasValue && !resolvedMap.ContainsKey(b.ConsumedExitAddress.Value))
                missing.Add($"0x{b.ConsumedExitAddress.Value:X8} {b.Function} (consumed exit)");
        }
        if (missing.Count > 0)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine("ERROR: hooks file NOT written — these hook addresses did not resolve:");
            foreach (var m in missing) Console.Error.WriteLine($"  {m}");
            return;
        }

        string sha;
        using (var sha256 = System.Security.Cryptography.SHA256.Create())
        using (var fs = File.OpenRead(targetPath))
            sha = Convert.ToHexString(sha256.ComputeHash(fs));

        string hooksPath = Path.Combine(dest, "kotor2-gog.hooks.toml");
        var hk = new StringBuilder();
        hk.AppendLine("# GENERATED by `kdev sigscan --k2` for the GOG 2021 relink of swkotor2.exe.");
        hk.AppendLine("#");
        hk.AppendLine("# Same hooks as kotor2.hooks.toml, same original_bytes: the GOG release is");
        hk.AppendLine("# the same Aspyr compile re-linked in 2021 -- the code at every hook site is");
        hk.AppendLine("# byte-identical, only its address moved (per-module deltas, both");
        hk.AppendLine("# directions). Addresses AND consumed_exit_address values are translated by");
        hk.AppendLine("# the same signature scan that builds engine_rebase_k2_table.inc; parameters");
        hk.AppendLine("# and flags are carried over verbatim from the parsed source blocks.");
        hk.AppendLine("#");
        hk.AppendLine("# Design rationale for each hook lives in kotor2.hooks.toml. Do not");
        hk.AppendLine("# duplicate it here; keep this file mechanical so it stays regenerable.");
        hk.AppendLine($"# generated: {DateTime.UtcNow:u}");
        hk.AppendLine();
        hk.AppendLine("[metadata]");
        hk.AppendLine("target_versions = [");
        hk.AppendLine($"    \"{sha}\",  # KOTOR 2 GOG (Aspyr, 2021 relink)");
        hk.AppendLine("]");
        foreach (var b in blocks)
        {
            uint newAddr = resolvedMap[b.Address];
            hk.AppendLine();
            hk.AppendLine("[[hooks]]");
            hk.AppendLine($"# {b.Function} -- was 0x{b.Address:x8} ({(long)newAddr - b.Address:+#;-#;0} bytes)");
            hk.AppendLine($"address = 0x{newAddr:x8}");
            hk.AppendLine($"type = \"{b.Type}\"");
            hk.AppendLine($"function = \"{b.Function}\"");
            if (b.OriginalBytes != null)
                hk.AppendLine("original_bytes = [" +
                              string.Join(", ", b.OriginalBytes.Select(x => $"0x{x:x2}")) + "]");
            hk.AppendLine($"skip_original_bytes = {(b.SkipOriginalBytes ? "true" : "false")}");
            hk.AppendLine($"exclude_from_restore = {b.ExcludeFromRestore ?? "[]"}");
            if (b.ConsumedExitAddress.HasValue)
            {
                uint newExit = resolvedMap[b.ConsumedExitAddress.Value];
                hk.AppendLine($"# consumed exit -- was 0x{b.ConsumedExitAddress.Value:x8} " +
                              $"({(long)newExit - b.ConsumedExitAddress.Value:+#;-#;0} bytes)");
                hk.AppendLine($"consumed_exit_address = 0x{newExit:x8}");
            }
            foreach (var p in b.Parameters)
            {
                hk.AppendLine();
                hk.AppendLine("[[hooks.parameters]]");
                hk.AppendLine($"source = \"{p.Source}\"");
                hk.AppendLine($"type = \"{p.Type}\"");
            }
        }
        File.WriteAllText(hooksPath, hk.ToString());
        Console.WriteLine($"Wrote {hooksPath} ({blocks.Count} hooks, complete — no hand-finishing step)");
    }

    /// <summary>
    /// Emit the two things the patch actually consumes: the C++ rebase table
    /// and the per-version hooks file. Generated rather than hand-transcribed —
    /// 214 addresses is far past the point where a typo would be spotted, and a
    /// typo here is a crash inside an unrelated function.
    /// </summary>
    private static void EmitArtifacts(string dest, List<SigRecord> records, string targetPath)
    {
        var resolved = records
            .Where(r => r.Resolved.HasValue &&
                        r.Status is "unique" or "unique-ordinal" or "xref" or "delta-derived")
            .OrderBy(r => r.Address.Va)
            .ToList();

        // ---- C++ rebase table ----
        string incPath = Path.Combine(dest, "engine_rebase_table.inc");
        var inc = new StringBuilder();
        inc.AppendLine("// GENERATED by `kdev sigscan` -- do not edit by hand.");
        inc.AppendLine($"// target: {Path.GetFileName(targetPath)}");
        inc.AppendLine($"// generated: {DateTime.UtcNow:u}");
        inc.AppendLine("//");
        inc.AppendLine("// Sorted ascending by reference address so acc::addr::R can binary-search.");
        inc.AppendLine("// 'ordinal' entries were matched by address order among identical code,");
        inc.AppendLine("// not by unique content -- see the sigscan report.");
        inc.AppendLine();
        inc.AppendLine("// clang-format off");
        foreach (var r in resolved)
        {
            string kind = r.Status == "unique-ordinal" ? "  // ordinal" : "";
            inc.AppendLine($"    {{ 0x{r.Address.Va:X8}, 0x{r.Resolved!.Value:X8} }},  // {r.Address.Name}{kind}");
        }
        inc.AppendLine("// clang-format on");
        File.WriteAllText(incPath, inc.ToString());

        var unresolved = records
            .Where(r => r.Status is "ambiguous" or "not-found" ||
                        (r.Status == "build-failed" && !r.Resolved.HasValue))
            .OrderBy(r => r.Address.Va)
            .ToList();

        // ---- per-version hooks file ----
        var hookRecords = resolved.Where(r => r.Address.OriginalBytes != null).ToList();
        string hooksPath = Path.Combine(dest, "target.hooks.toml");
        var hk = new StringBuilder();
        hk.AppendLine("# GENERATED by `kdev sigscan` -- do not edit by hand.");
        hk.AppendLine($"# target: {Path.GetFileName(targetPath)}");
        hk.AppendLine($"# generated: {DateTime.UtcNow:u}");
        hk.AppendLine("#");
        hk.AppendLine("# Addresses are rebased; original_bytes are unchanged because the code at");
        hk.AppendLine("# each hook site is byte-identical between builds -- only its location moved.");
        hk.AppendLine("# Copy the [[hooks]].parameters blocks across from the base hooks.toml, and");
        hk.AppendLine("# set target_versions below to this executable's SHA-256.");
        hk.AppendLine();
        hk.AppendLine("[metadata]");
        hk.AppendLine("target_versions = [\"<SHA-256 OF THE TARGET EXE>\"]");
        hk.AppendLine();
        foreach (var r in hookRecords)
        {
            hk.AppendLine("[[hooks]]");
            hk.AppendLine($"# was 0x{r.Address.Va:X8} ({(long)r.Resolved!.Value - r.Address.Va:+#;-#;0} bytes)");
            hk.AppendLine($"address = 0x{r.Resolved.Value:x8}");
            hk.AppendLine("type = \"detour\"");
            hk.AppendLine($"function = \"{r.Address.Name}\"");
            hk.AppendLine("original_bytes = [" +
                          string.Join(", ", r.Address.OriginalBytes!.Select(b => $"0x{b:x2}")) + "]");
            hk.AppendLine();
        }
        File.WriteAllText(hooksPath, hk.ToString());

        Console.WriteLine($"Wrote {incPath} ({resolved.Count} entries)");
        Console.WriteLine($"Wrote {hooksPath} ({hookRecords.Count} hooks)");
        if (unresolved.Count > 0)
        {
            Console.Error.WriteLine();
            Console.Error.WriteLine($"NOTE: {unresolved.Count} address(es) are NOT in the table. Guard their");
            Console.Error.WriteLine("call sites or resolve them before shipping support for this exe:");
            foreach (var r in unresolved)
                Console.Error.WriteLine($"  0x{r.Address.Va:X8}  {r.Address.Name}");
        }
    }

    private sealed class SigRecord(
        EngineAddress address, Signature? signature, string status,
        string? note, uint? resolved, List<uint>? candidates)
    {
        public EngineAddress Address { get; } = address;
        public Signature? Signature { get; } = signature;
        public string Status { get; set; } = status;
        public string? Note { get; set; } = note;
        public uint? Resolved { get; set; } = resolved;
        public List<uint>? Candidates { get; set; } = candidates;
    }
}
