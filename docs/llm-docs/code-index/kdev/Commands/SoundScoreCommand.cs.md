# SoundScoreCommand.cs (1348 lines)

`kdev sound-score` — scores an extracted WAV corpus for directional "localizability" (especially the hard elevation/up-down axis, needed for cues like the turret minigame). Computes per-file acoustic features via a self-contained RIFF/WAVE reader + Hann-windowed STFT (no audio package dependency): HF ratio (>5kHz), 6-11kHz pinna-notch elevation band ratio, spectral flatness, centroid, onset rate (spectral flux peak-picking), crest factor. Composite score = `100*(0.55*elevationCapability + 0.25*hfNorm + 0.20*bearingSharpness)` where `elevationCapability = sqrt(hfLeg * flatNorm)` (needs both HF energy and a flat/noise-like spectrum). Ranks the corpus, writes a linear (no-tables) text report + CSV, copies the top N candidates into a folder with rank+score-prefixed filenames for screen-reader-friendly test-hearing. Also hosts loop-crafting subcommands operating on a single WAV: `--describe` (loopability diagnostics: envelope, fade detection, pitch drift, seam quality), `--make-loop` (trim + equal-power crossfade + optional envelope-flatten/ping-pong + peak-normalise), `--repeat` (gapless N-times concatenation). Includes an IMA/DVI ADPCM decoder for the KOTOR blaster SFX that use it. Self-contained — no other kdev classes referenced besides `KdevConfig`.

## Declarations (in source order)

- L78 — `static class SoundScoreCommand`
- L80 — `Command Build()` — `--input`/`--out-dir`/`--report`/`--top`/`--max-seconds`/`--no-copy`, plus loop-crafting options `--describe`/`--make-loop`/`--start`/`--end`/`--crossfade`/`--peak-dbfs`/`--flatten`/`--pingpong`/`--loop-out`/`--repeat`/`--reps`
- L220 — `int Run(...)` — scores every WAV in `--input`, sorts descending by score, stamps ranks, writes report+CSV, copies top N
- L314 — `KdevConfigException NoConfig(string opt)`
- L317 — `(int,int,int,int) CountTiers(List<SoundScore> r)` — EXCELLENT/GOOD/FAIR/POOR bucket counts
- L330 — `string Tier(int score)`
- L339 — `int CopyBest(...)` — copies `{rank:000}_s{score:000}_{name}.wav`, clearing prior copies first
- L373 — `void WriteReport(...)` — linear (no-table) text report: methodology, summary, top-N detailed blocks, full ranking
- L443 — `void WriteCsv(...)`
- L469 — `string CsvField(string s)`
- L476 — `sealed class SoundScore` — per-file result fields + `FlagString()` (ELEV/SHARP/NOISY/DRONE)
- L512 — `static class WavAnalysis` — the whole acoustic engine
  - L517 — `SoundScore? Score(string path)` — crest factor + STFT average power spectrum + spectral flux → composite score
  - L662 — `int DescribeLoopability(string path)` — envelope-per-slice (level+brightness/centroid), attack/fade detection, pitch-drift-via-ZCR, loop-seam quality, wrap-jump outlier check, recommendation
  - L839 — `int MakeLoop(...)` — trims to `[start,end]`, equal-power crossfades the wrap against the tail past `end` (so the join is continuous in the source), optional `FlattenRegion` envelope-flatten, optional peak-normalise, optional ping-pong (forward+reversed, doubles period)
    note: crossfade head starts from the sample just past the loop end, so the wrap is guaranteed continuous in the source signal
  - L946 — `int RepeatWav(string inPath, string outPath, int reps)` — back-to-back concat for gapless test-hearing
  - L983 — `void FlattenRegion(double[] x, int from, int to, int sampleRate)` — divides out a 30ms-windowed RMS envelope, gain clamped [0.5x,2x]
  - L1014 — `(double,double) DetectSteadyRegion(double[] mono, int sampleRate)` — -3dB-threshold segment scan
  - L1037 — `void WriteWavMono16(...)` — 16-bit mono PCM RIFF writer
  - L1066 — `double SliceCentroid(...)`
  - L1089 — `double ZcrHz(...)` — zero-crossing-rate pitch proxy
  - L1099/L1106 — `NextZeroCrossing`/`PrevZeroCrossing`
  - L1113 — `double OnsetRate(List<double> flux, double seconds)` — mean+1σ threshold peak-picking
  - L1133 — `double[] HannWindow(int n)`
  - L1146 — `void Fft(double[] re, double[] im)` — iterative radix-2 Cooley-Tukey, in place
  - L1188 — `(double[],int) ReadMono(string path)` — RIFF/WAVE parser: PCM 8/16/24/32-bit int + 32-bit float + IMA ADPCM (format 17), downmixed to mono
  - L1264 — `double ReadSample(byte[] b, int i, int bits, int format)`
  - L1296-1347 — `ImaIndexTable`/`ImaStepTable` + `double[] DecodeImaAdpcmMono(...)` — standard IMA/DVI ADPCM block decoder
