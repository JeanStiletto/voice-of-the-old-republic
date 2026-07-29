using System.CommandLine;
using System.CommandLine.Invocation;
using System.Text;

namespace Kdev.Commands;

/// <summary>
/// Score the extracted WAV corpus for <b>localizability</b> — how well a
/// sound can carry directional information to the ear, especially the
/// hard <i>elevation</i> (up/down) axis used by things like the turret
/// minigame.
///
/// Why this exists
/// ---------------
/// Localizability is a property of the <b>sound</b>, not just its position.
/// The same engine, same 3D position, is sharply locatable with one sound
/// and impossible with another:
///
///   * Left/right (azimuth) survives on almost any sound, because the
///     interaural time/level differences that encode it don't need high
///     frequencies. Panning "just works".
///   * Up/down (elevation) and front/back are decoded from the spectral
///     notch the outer ear (pinna) carves into the sound — and that notch
///     lives HIGH in the spectrum (~5-16 kHz). For the brain to read it,
///     the source must actually contain energy there, and must be broad /
///     flat / noise-like so the directional filtering can be told apart
///     from the source's own spectrum.
///   * A tonal, low, steady engine drone carries none of that. It's the
///     worst-case carrier for elevation. A broadband, high-frequency,
///     transient/pulsed sound (a noise burst, a click train, a chirp,
///     jingling) is the best-case carrier.
///
/// This command measures, per file, the acoustic features that predict
/// localizability, ranks the corpus, writes a human-readable report, and
/// copies the best candidates into a sibling folder so they can be
/// test-heard quickly (each copied filename is prefixed with its rank and
/// score, so the ranking is audible just by tabbing through the folder).
///
/// Metrics (per file, computed from a short-time FFT)
/// --------------------------------------------------
///   * hf-ratio       — fraction of energy above 5 kHz. Elevation needs
///                      energy up here. (Note: the corpus is 22.05 kHz, so
///                      Nyquist is ~11 kHz; the top of the elevation band
///                      is physically absent from these files.)
///   * elev-band      — fraction of energy in the 6-11 kHz pinna-notch band.
///   * flatness       — spectral flatness (Wiener entropy), 0..1. Near 1 =
///                      noise-like/flat (readable directional filtering);
///                      near 0 = tonal/peaky (confounds the cue).
///   * centroid       — spectral centroid in Hz ("brightness").
///   * onsets/sec     — transient density via spectral flux. More onsets =
///                      crisper bearing and repeated directional "looks".
///   * crest          — crest factor in dB (peak/RMS). Higher = peakier /
///                      more transient; lower = steady drone.
///
/// Composite score (0..100)
/// -------------------------
///   elevation-capability = sqrt(hf-norm * flatness-norm)  (BOTH needed)
///   bearing-sharpness    = 0.6*onset-norm + 0.4*crest-norm
///   score = 100 * (0.55*elevation-capability + 0.25*hf-norm
///                  + 0.20*bearing-sharpness)
///
/// The weights lean on elevation because that's the dimension that fails
/// in practice; azimuth already works. Raw metrics are printed alongside
/// so the ranking stays interpretable rather than a black box.
///
/// Usage
/// -----
///   kdev sound-score
///   kdev sound-score --input build/sounds-extracted-full --top 60
///   kdev sound-score --max-seconds 4      # only copy short, loopable cues
///   kdev sound-score --no-copy            # report only, don't copy files
///
/// Defaults (resolved against the project root from kdev.toml):
///   --input    build/sounds-extracted-full
///   --out-dir  build/sound-localizable-best   (sibling of the input)
///   --report   build/sound-score-report.txt   (+ a .csv next to it)
/// </summary>
public static class SoundScoreCommand
{
    public static Command Build()
    {
        var inputOpt = new Option<string?>(
            name: "--input",
            description: "Directory of WAV files to score. Default: build/sounds-extracted-full.");

        var outDirOpt = new Option<string?>(
            name: "--out-dir",
            description: "Folder to copy the best candidates into for test-hearing. Default: build/sound-localizable-best.");

        var reportOpt = new Option<string?>(
            name: "--report",
            description: "Path for the text report (a .csv sibling is also written). Default: build/sound-score-report.txt.");

        var topOpt = new Option<int>(
            name: "--top",
            getDefaultValue: () => 40,
            description: "How many top-ranked files to copy into --out-dir.");

        var maxSecondsOpt = new Option<double>(
            name: "--max-seconds",
            getDefaultValue: () => 0.0,
            description: "When copying, skip files longer than this many seconds (0 = no limit). Targeting cues want short, loopable sounds.");

        var noCopyOpt = new Option<bool>(
            name: "--no-copy",
            getDefaultValue: () => false,
            description: "Write the report only; don't copy any files.");

        var describeOpt = new Option<string?>(
            name: "--describe",
            description: "Instead of scoring the corpus, analyse ONE WAV's loopability: amplitude envelope over time, fade-out detection, steady-region location, pitch drift, and loop-seam quality.");

        var makeLoopOpt = new Option<string?>(
            name: "--make-loop",
            description: "Instead of scoring, build a seamless continuous-loop WAV from ONE file: trim to [--start,--end], crossfade the wrap, peak-normalise, and write to --loop-out.");

        var startOpt = new Option<double>(
            name: "--start",
            getDefaultValue: () => -1.0,
            description: "Loop start in seconds (--make-loop). Default: auto-detect the steady region.");

        var endOpt = new Option<double>(
            name: "--end",
            getDefaultValue: () => -1.0,
            description: "Loop end in seconds (--make-loop). Default: auto-detect the steady region.");

        var crossfadeOpt = new Option<double>(
            name: "--crossfade",
            getDefaultValue: () => 20.0,
            description: "Crossfade length in milliseconds at the loop wrap (--make-loop).");

        var peakDbfsOpt = new Option<double>(
            name: "--peak-dbfs",
            getDefaultValue: () => -1.0,
            description: "Peak-normalise the loop to this dBFS (--make-loop). Use 0 or positive to skip normalising.");

        var flattenOpt = new Option<bool>(
            name: "--flatten",
            getDefaultValue: () => false,
            description: "Divide out the amplitude envelope so the loop is constant-level (--make-loop). Removes the periodic loudness 'pulse' that a decaying source leaves in a loop.");

        var pingPongOpt = new Option<bool>(
            name: "--pingpong",
            getDefaultValue: () => false,
            description: "Append a reversed copy of the loop (forward-then-back), doubling the loop period to weaken audible pattern-repetition. Best for noise-like sounds where reversal isn't perceptible.");

        var loopOutOpt = new Option<string?>(
            name: "--loop-out",
            description: "Output path for --make-loop. Default: build/sound-loops/<name>_loop.wav.");

        var repeatOpt = new Option<string?>(
            name: "--repeat",
            description: "Concatenate a (seamless) WAV back-to-back N times into one file for gapless test-hearing in players that don't loop gaplessly.");

        var repsOpt = new Option<int>(
            name: "--reps",
            getDefaultValue: () => 10,
            description: "Number of repetitions for --repeat.");

        var cmd = new Command("sound-score",
            "Score the WAV corpus for localizability (elevation-carrying ability); rank, report, and copy the best candidates for test-hearing.")
        {
            inputOpt, outDirOpt, reportOpt, topOpt, maxSecondsOpt, noCopyOpt, describeOpt,
            makeLoopOpt, startOpt, endOpt, crossfadeOpt, peakDbfsOpt, flattenOpt, pingPongOpt, loopOutOpt,
            repeatOpt, repsOpt,
        };

        cmd.SetHandler((InvocationContext ctx) =>
        {
            var describe = ctx.ParseResult.GetValueForOption(describeOpt);
            if (describe != null)
            {
                ctx.ExitCode = WavAnalysis.DescribeLoopability(describe);
                return;
            }

            var repeat = ctx.ParseResult.GetValueForOption(repeatOpt);
            if (repeat != null)
            {
                int reps = ctx.ParseResult.GetValueForOption(repsOpt);
                var loopOut = ctx.ParseResult.GetValueForOption(loopOutOpt);
                string outPath = loopOut ?? Path.Combine(
                    Path.GetDirectoryName(Path.GetFullPath(repeat))!,
                    Path.GetFileNameWithoutExtension(repeat) + $"_x{reps}.wav");
                ctx.ExitCode = WavAnalysis.RepeatWav(repeat, outPath, reps);
                return;
            }

            var makeLoop = ctx.ParseResult.GetValueForOption(makeLoopOpt);
            if (makeLoop != null)
            {
                var cfgLoop = KdevConfig.LoadOrPrintErrors(out _);
                string defaultOut = cfgLoop != null
                    ? Path.Combine(cfgLoop.ProjectRoot, "build", "sound-loops",
                        Path.GetFileNameWithoutExtension(makeLoop) + "_loop.wav")
                    : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(makeLoop))!, "sound-loops",
                        Path.GetFileNameWithoutExtension(makeLoop) + "_loop.wav");
                ctx.ExitCode = WavAnalysis.MakeLoop(
                    makeLoop,
                    ctx.ParseResult.GetValueForOption(loopOutOpt) ?? defaultOut,
                    ctx.ParseResult.GetValueForOption(startOpt),
                    ctx.ParseResult.GetValueForOption(endOpt),
                    ctx.ParseResult.GetValueForOption(crossfadeOpt),
                    ctx.ParseResult.GetValueForOption(peakDbfsOpt),
                    ctx.ParseResult.GetValueForOption(flattenOpt),
                    ctx.ParseResult.GetValueForOption(pingPongOpt));
                return;
            }
            var input = ctx.ParseResult.GetValueForOption(inputOpt);
            var outDir = ctx.ParseResult.GetValueForOption(outDirOpt);
            var report = ctx.ParseResult.GetValueForOption(reportOpt);
            var top = ctx.ParseResult.GetValueForOption(topOpt);
            var maxSeconds = ctx.ParseResult.GetValueForOption(maxSecondsOpt);
            var noCopy = ctx.ParseResult.GetValueForOption(noCopyOpt);
            ctx.ExitCode = Run(input, outDir, report, top, maxSeconds, noCopy);
        });
        return cmd;
    }

    private static int Run(string? inputArg, string? outDirArg, string? reportArg,
                           int top, double maxSeconds, bool noCopy)
    {
        var cfg = KdevConfig.LoadOrPrintErrors(out int exit);
        // The command only needs ProjectRoot for path defaults; if kdev.toml
        // is unusable, explicit --input is still allowed.
        string? projectRoot = cfg?.ProjectRoot;

        string input = inputArg
            ?? (projectRoot != null
                ? Path.Combine(projectRoot, "build", "sounds-extracted-full")
                : throw NoConfig("--input"));
        string outDir = outDirArg
            ?? (projectRoot != null
                ? Path.Combine(projectRoot, "build", "sound-localizable-best")
                : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(input))!, "sound-localizable-best"));
        string reportPath = reportArg
            ?? (projectRoot != null
                ? Path.Combine(projectRoot, "build", "sound-score-report.txt")
                : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(input))!, "sound-score-report.txt"));

        if (!Directory.Exists(input))
        {
            Console.Error.WriteLine($"Input directory not found: {input}");
            return 1;
        }

        var wavs = Directory.GetFiles(input, "*.wav", SearchOption.TopDirectoryOnly);
        if (wavs.Length == 0)
        {
            Console.Error.WriteLine($"No .wav files found in {input}");
            return 1;
        }

        Console.Error.WriteLine($"Scoring {wavs.Length} WAV files in {input} ...");

        var results = new List<SoundScore>(wavs.Length);
        int errors = 0;
        int done = 0;
        foreach (var path in wavs)
        {
            try
            {
                var s = WavAnalysis.Score(path);
                if (s != null) results.Add(s);
            }
            catch (Exception ex)
            {
                errors++;
                Console.Error.WriteLine($"  skip {Path.GetFileName(path)}: {ex.Message}");
            }
            if (++done % 250 == 0)
                Console.Error.WriteLine($"  ... {done}/{wavs.Length}");
        }

        if (results.Count == 0)
        {
            Console.Error.WriteLine("No files could be scored.");
            return 1;
        }

        results.Sort((a, b) => b.Score.CompareTo(a.Score));

        // Stamp ranks (1-based) after sorting.
        for (int i = 0; i < results.Count; i++) results[i].Rank = i + 1;

        WriteReport(reportPath, input, results, top, errors);
        WriteCsv(Path.ChangeExtension(reportPath, ".csv"), results);

        int copied = 0;
        if (!noCopy)
            copied = CopyBest(results, outDir, top, maxSeconds);

        // Console summary (linear, screen-reader friendly).
        Console.WriteLine();
        Console.WriteLine($"Scored {results.Count} files ({errors} skipped).");
        var tiers = CountTiers(results);
        Console.WriteLine($"  Excellent (>=70): {tiers.excellent}");
        Console.WriteLine($"  Good (50-70):     {tiers.good}");
        Console.WriteLine($"  Fair (30-50):     {tiers.fair}");
        Console.WriteLine($"  Poor (<30):       {tiers.poor}");
        Console.WriteLine();
        Console.WriteLine($"Report: {reportPath}");
        Console.WriteLine($"CSV:    {Path.ChangeExtension(reportPath, ".csv")}");
        if (!noCopy)
            Console.WriteLine($"Copied {copied} best candidates to: {outDir}");
        Console.WriteLine();
        Console.WriteLine("Top 10:");
        foreach (var s in results.Take(10))
            Console.WriteLine($"  {s.Score,3}  {s.Name}  [{s.FlagString()}]");

        return 0;
    }

    private static KdevConfigException NoConfig(string opt) =>
        new($"kdev.toml not usable and {opt} not given. Pass {opt} explicitly.");

    private static (int excellent, int good, int fair, int poor) CountTiers(List<SoundScore> r)
    {
        int e = 0, g = 0, f = 0, p = 0;
        foreach (var s in r)
        {
            if (s.Score >= 70) e++;
            else if (s.Score >= 50) g++;
            else if (s.Score >= 30) f++;
            else p++;
        }
        return (e, g, f, p);
    }

    private static string Tier(int score) =>
        score >= 70 ? "EXCELLENT" : score >= 50 ? "GOOD" : score >= 30 ? "FAIR" : "POOR";

    // ------------------------------------------------------------------
    // Copy the best candidates. Each copy is renamed
    //   {rank:000}_s{score:000}_{originalname}.wav
    // so an alphabetical listing is rank-ordered and the score is spoken
    // by the screen reader as part of the filename.
    // ------------------------------------------------------------------
    private static int CopyBest(List<SoundScore> results, string outDir, int top, double maxSeconds)
    {
        Directory.CreateDirectory(outDir);
        // Clear previous copies (only our .wav files) so stale ranks don't pile up.
        foreach (var old in Directory.GetFiles(outDir, "*.wav"))
        {
            try { File.Delete(old); } catch { /* best effort */ }
        }

        int copied = 0;
        foreach (var s in results)
        {
            if (copied >= top) break;
            if (maxSeconds > 0 && s.Seconds > maxSeconds) continue;
            var dest = Path.Combine(outDir,
                $"{s.Rank:000}_s{s.Score:000}_{Path.GetFileNameWithoutExtension(s.Name)}.wav");
            try
            {
                File.Copy(s.Path, dest, overwrite: true);
                copied++;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"  copy failed {s.Name}: {ex.Message}");
            }
        }
        return copied;
    }

    // ------------------------------------------------------------------
    // Human-readable report. Linear, no tables (screen-reader friendly):
    // one block per file under tier headings, plus a full one-line-per-file
    // ranking at the end.
    // ------------------------------------------------------------------
    private static void WriteReport(string path, string input, List<SoundScore> results, int top, int errors)
    {
        var sb = new StringBuilder();
        sb.AppendLine("Sound localizability report");
        sb.AppendLine("===========================");
        sb.AppendLine($"Generated: {DateTime.UtcNow:yyyy-MM-dd HH:mm} UTC");
        sb.AppendLine($"Input:     {input}");
        sb.AppendLine($"Scored:    {results.Count} files ({errors} skipped)");
        sb.AppendLine();
        sb.AppendLine("What 'localizability' means here");
        sb.AppendLine("--------------------------------");
        sb.AppendLine("Higher score = the sound carries directional information better,");
        sb.AppendLine("especially the hard up/down (elevation) axis. Left/right works on");
        sb.AppendLine("almost any sound; elevation needs broadband, high-frequency,");
        sb.AppendLine("noise-like, transient content. A tonal low steady drone scores low");
        sb.AppendLine("because the cue physically isn't in the signal.");
        sb.AppendLine();
        sb.AppendLine("Per-file metrics:");
        sb.AppendLine("  score     0..100 composite (see weights below).");
        sb.AppendLine("  elev-cap  elevation capability = sqrt(hf-norm * flatness-norm).");
        sb.AppendLine("  bearing   bearing sharpness = 0.6*onsets + 0.4*crest (azimuth crispness).");
        sb.AppendLine("  hf%       fraction of energy above 5 kHz.");
        sb.AppendLine("  elev%     fraction of energy in the 6-11 kHz pinna-notch band.");
        sb.AppendLine("  flat      spectral flatness 0..1 (1 = noise-like, 0 = tonal).");
        sb.AppendLine("  cent      spectral centroid (brightness) in Hz.");
        sb.AppendLine("  onset/s   transient onsets per second.");
        sb.AppendLine("  crestdB   peak/RMS in dB (high = peaky/transient, low = drone).");
        sb.AppendLine("  flags     ELEV=elevation-capable, SHARP=crisp bearing,");
        sb.AppendLine("            NOISY=flat spectrum, DRONE=poor localizer.");
        sb.AppendLine();
        sb.AppendLine("Note: corpus is 22.05 kHz, so Nyquist is ~11 kHz — the top of the");
        sb.AppendLine("real elevation band (above ~11 kHz) is absent from every file. The");
        sb.AppendLine("best these sounds can do for elevation is the 6-11 kHz slice.");
        sb.AppendLine();

        var tiers = CountTiers(results);
        sb.AppendLine("Summary");
        sb.AppendLine("-------");
        sb.AppendLine($"  EXCELLENT (>=70): {tiers.excellent}");
        sb.AppendLine($"  GOOD (50-70):     {tiers.good}");
        sb.AppendLine($"  FAIR (30-50):     {tiers.fair}");
        sb.AppendLine($"  POOR (<30):       {tiers.poor}");
        sb.AppendLine();
        sb.AppendLine($"The top {top} files are copied to the candidates folder for test-hearing.");
        sb.AppendLine();

        // Detailed blocks for the top N (the ones that get copied / matter most).
        sb.AppendLine("Top candidates (detailed)");
        sb.AppendLine("-------------------------");
        foreach (var s in results.Take(Math.Max(top, 60)))
        {
            sb.AppendLine($"#{s.Rank}  {s.Name}   score {s.Score}  [{Tier(s.Score)}]");
            sb.AppendLine($"    elev-cap {s.ElevationCapability:0.00}   bearing {s.BearingSharpness:0.00}   flags {s.FlagString()}");
            sb.AppendLine($"    hf% {s.HfRatio5k * 100:0.0}   elev% {s.ElevBandRatio * 100:0.0}   flat {s.Flatness:0.000}   cent {s.CentroidHz:0} Hz");
            sb.AppendLine($"    onset/s {s.OnsetsPerSec:0.0}   crestdB {s.CrestDb:0.0}   {s.Seconds:0.00}s   {s.SampleRate} Hz");
            sb.AppendLine();
        }

        // Full ranking, one line per file.
        sb.AppendLine("Full ranking (one line per file)");
        sb.AppendLine("--------------------------------");
        foreach (var s in results)
        {
            sb.AppendLine($"{s.Rank,4}  {s.Score,3}  {Tier(s.Score),-9}  [{s.FlagString(),-18}]  {s.Name}");
        }

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        File.WriteAllText(path, sb.ToString());
    }

    private static void WriteCsv(string path, List<SoundScore> results)
    {
        var sb = new StringBuilder();
        sb.AppendLine("rank,score,tier,name,elev_cap,bearing,hf_ratio_5k,elev_band_6_11k,flatness,centroid_hz,onsets_per_sec,crest_db,seconds,sample_rate,flags");
        foreach (var s in results)
        {
            sb.AppendLine(string.Join(",",
                s.Rank,
                s.Score,
                Tier(s.Score),
                CsvField(s.Name),
                s.ElevationCapability.ToString("0.0000"),
                s.BearingSharpness.ToString("0.0000"),
                s.HfRatio5k.ToString("0.0000"),
                s.ElevBandRatio.ToString("0.0000"),
                s.Flatness.ToString("0.0000"),
                s.CentroidHz.ToString("0"),
                s.OnsetsPerSec.ToString("0.00"),
                s.CrestDb.ToString("0.0"),
                s.Seconds.ToString("0.000"),
                s.SampleRate,
                CsvField(s.FlagString())));
        }
        File.WriteAllText(path, sb.ToString());
    }

    private static string CsvField(string s) =>
        s.Contains(',') || s.Contains('"') ? "\"" + s.Replace("\"", "\"\"") + "\"" : s;
}

