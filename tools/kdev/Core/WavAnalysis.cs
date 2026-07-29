using System.Text;

namespace Kdev;

// ----------------------------------------------------------------------
// WAV reader + short-time FFT + acoustic feature extraction.
//
// Split out of Commands/SoundScoreCommand.cs by the Phase-1 structure pass
// (refactoring candidate 15). The command half is CLI wiring and report
// writing; this is the engine underneath it, and it never refers back -
// it takes paths and returns numbers.
//
// Self-contained (no audio package): parses RIFF/WAVE PCM (8/16/24/32-bit
// int and 32-bit float), downmixes to mono, and runs a Hann-windowed STFT
// to derive the spectral and temporal features that predict localizability.
// Also carries the loop-crafting toolkit built on the same primitives.
// ----------------------------------------------------------------------
internal static class WavAnalysis
{
    private const int FrameSize = 1024;   // ~46 ms at 22.05 kHz
    private const int HopSize = 512;

    public static SoundScore? Score(string path)
    {
        var (mono, sampleRate) = ReadMono(path);
        if (mono.Length < FrameSize || sampleRate <= 0)
        {
            // Too short / unreadable to analyse meaningfully.
            return new SoundScore
            {
                Path = path,
                Name = System.IO.Path.GetFileName(path),
                Score = 0,
                SampleRate = sampleRate > 0 ? sampleRate : 0,
                Seconds = sampleRate > 0 ? (double)mono.Length / sampleRate : 0,
            };
        }

        double seconds = (double)mono.Length / sampleRate;

        // --- Crest factor over the whole waveform.
        double peak = 0, sumSq = 0;
        for (int i = 0; i < mono.Length; i++)
        {
            double a = Math.Abs(mono[i]);
            if (a > peak) peak = a;
            sumSq += mono[i] * (double)mono[i];
        }
        double rms = Math.Sqrt(sumSq / mono.Length);
        double crestDb = (rms > 1e-9 && peak > 1e-9) ? 20.0 * Math.Log10(peak / rms) : 0.0;

        // --- STFT: average power spectrum + spectral flux for onsets.
        int nBins = FrameSize / 2;                 // skip Nyquist bin
        var avgPower = new double[nBins];
        var prevMag = new double[nBins];
        var window = HannWindow(FrameSize);
        var re = new double[FrameSize];
        var im = new double[FrameSize];

        var fluxSeries = new List<double>();
        int frames = 0;
        bool havePrev = false;

        for (int start = 0; start + FrameSize <= mono.Length; start += HopSize)
        {
            for (int i = 0; i < FrameSize; i++)
            {
                re[i] = mono[start + i] * window[i];
                im[i] = 0.0;
            }
            Fft(re, im);

            double flux = 0;
            for (int k = 0; k < nBins; k++)
            {
                double mag = Math.Sqrt(re[k] * re[k] + im[k] * im[k]);
                double pow = mag * mag;
                avgPower[k] += pow;
                if (havePrev)
                {
                    double d = mag - prevMag[k];
                    if (d > 0) flux += d;
                }
                prevMag[k] = mag;
            }
            if (havePrev) fluxSeries.Add(flux);
            havePrev = true;
            frames++;
        }

        if (frames == 0)
            return new SoundScore { Path = path, Name = System.IO.Path.GetFileName(path), Score = 0, SampleRate = sampleRate, Seconds = seconds };

        for (int k = 0; k < nBins; k++) avgPower[k] /= frames;

        // --- Spectral features from the averaged power spectrum.
        double binHz = (double)sampleRate / FrameSize;
        double totalPow = 0, hfPow = 0, elevPow = 0, centroidNum = 0;
        // Spectral flatness via geometric/arithmetic mean over bins 1..nBins-1.
        double logSum = 0, arithSum = 0;
        int flatCount = 0;
        const double eps = 1e-12;
        for (int k = 1; k < nBins; k++)
        {
            double p = avgPower[k];
            double f = k * binHz;
            totalPow += p;
            centroidNum += f * p;
            if (f >= 5000.0) hfPow += p;
            if (f >= 6000.0 && f <= 11000.0) elevPow += p;
            logSum += Math.Log(p + eps);
            arithSum += p + eps;
            flatCount++;
        }

        double hfRatio = totalPow > 0 ? hfPow / totalPow : 0;
        double elevRatio = totalPow > 0 ? elevPow / totalPow : 0;
        double centroid = totalPow > 0 ? centroidNum / totalPow : 0;
        double geoMean = Math.Exp(logSum / flatCount);
        double ariMean = arithSum / flatCount;
        double flatness = ariMean > 0 ? geoMean / ariMean : 0;

        // --- Onset density via peak-picking on the flux series.
        double onsetsPerSec = OnsetRate(fluxSeries, seconds);

        // --- Normalisations (heuristic; chosen so real broadband cues land
        //     near 1 and drones near 0). See class doc for rationale.
        double hfNorm = Clamp01(hfRatio / 0.30);
        double elevNorm = Clamp01(elevRatio / 0.12);
        double flatNorm = Clamp01(flatness / 0.20);
        double onsetNorm = Clamp01(onsetsPerSec / 6.0);
        double crestNorm = Clamp01((crestDb - 6.0) / 18.0);

        // Elevation needs BOTH high-frequency energy AND a flat-ish spectrum,
        // so combine them multiplicatively (geometric mean). Use the better
        // of the broadband-HF and in-band-elevation measures for the HF leg.
        double hfLeg = Math.Max(hfNorm, elevNorm);
        double elevationCapability = Math.Sqrt(hfLeg * flatNorm);
        double bearingSharpness = 0.6 * onsetNorm + 0.4 * crestNorm;

        double composite = 100.0 * (0.55 * elevationCapability
                                    + 0.25 * hfNorm
                                    + 0.20 * bearingSharpness);

        return new SoundScore
        {
            Path = path,
            Name = System.IO.Path.GetFileName(path),
            Score = (int)Math.Round(Clamp(composite, 0, 100)),
            ElevationCapability = elevationCapability,
            BearingSharpness = bearingSharpness,
            HfRatio5k = hfRatio,
            ElevBandRatio = elevRatio,
            Flatness = flatness,
            CentroidHz = centroid,
            OnsetsPerSec = onsetsPerSec,
            CrestDb = crestDb,
            Seconds = seconds,
            SampleRate = sampleRate,
        };
    }

    // ------------------------------------------------------------------
    // Single-file loopability inspector. Answers: where does it fade, is
    // there a steady region to loop, does the pitch drift (evolving sound),
    // and how bad is the loop seam. Output is linear text (no graphics).
    // ------------------------------------------------------------------
    public static int DescribeLoopability(string path)
    {
        if (!File.Exists(path))
        {
            Console.Error.WriteLine($"File not found: {path}");
            return 1;
        }

        double[] mono;
        int sampleRate;
        try { (mono, sampleRate) = ReadMono(path); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Could not read {path}: {ex.Message}");
            return 1;
        }
        if (mono.Length < 16 || sampleRate <= 0)
        {
            Console.Error.WriteLine("File too short to analyse.");
            return 1;
        }

        double seconds = (double)mono.Length / sampleRate;
        double peak = 0, sumSq = 0;
        for (int i = 0; i < mono.Length; i++)
        {
            double a = Math.Abs(mono[i]);
            if (a > peak) peak = a;
            sumSq += mono[i] * mono[i];
        }
        double rmsAll = Math.Sqrt(sumSq / mono.Length);

        Console.WriteLine($"Loopability: {Path.GetFileName(path)}");
        Console.WriteLine($"  duration   {seconds:0.000} s   ({mono.Length} samples @ {sampleRate} Hz)");
        Console.WriteLine($"  peak       {Dbfs(peak):0.0} dBFS");
        Console.WriteLine($"  rms        {Dbfs(rmsAll):0.0} dBFS");
        Console.WriteLine();

        // --- Amplitude envelope over time (segment RMS).
        const int segs = 24;
        var segRms = new double[segs];
        double maxSeg = 0;
        int segLen = Math.Max(1, mono.Length / segs);
        for (int s = 0; s < segs; s++)
        {
            int a = s * segLen;
            int b = (s == segs - 1) ? mono.Length : Math.Min(mono.Length, a + segLen);
            double ss = 0; int n = Math.Max(1, b - a);
            for (int i = a; i < b; i++) ss += mono[i] * mono[i];
            segRms[s] = Math.Sqrt(ss / n);
            if (segRms[s] > maxSeg) maxSeg = segRms[s];
        }

        Console.WriteLine("Envelope per slice — level (volume) and centroid (brightness/timbre):");
        double cenMin = double.MaxValue, cenMax = 0;
        for (int s = 0; s < segs; s++)
        {
            double t0 = (double)s * segLen / sampleRate;
            double pct = maxSeg > 0 ? 100.0 * segRms[s] / maxSeg : 0;
            int center = s * segLen + segLen / 2;
            double cen = SliceCentroid(mono, center, sampleRate);
            if (cen > 0) { if (cen < cenMin) cenMin = cen; if (cen > cenMax) cenMax = cen; }
            Console.WriteLine($"  t={t0,6:0.000}s   level {pct,5:0.0}% ({Dbfs(segRms[s]),5:0.0} dBFS)   brightness {cen,6:0} Hz");
        }
        if (cenMin <= cenMax && cenMin < double.MaxValue)
        {
            double cenSwingPct = cenMax > 0 ? 100.0 * (cenMax - cenMin) / cenMax : 0;
            Console.WriteLine($"  brightness range across loop: {cenMin:0}..{cenMax:0} Hz  ({cenSwingPct:0}% swing)");
            if (cenSwingPct > 12)
                Console.WriteLine("  -> TIMBRE sweeps across the loop: this is the pulsing (a 'wah' that resets each cycle), not volume.");
            else
                Console.WriteLine("  -> brightness is fairly steady; remaining pulse is mostly pattern-repetition (the ear recognising the same texture each loop).");
        }
        Console.WriteLine();

        // --- Attack end / fade onset: first & last slice within -3 dB of the
        //     loudest slice. Everything after the last such slice is the
        //     decaying tail / fade-out.
        double thresh = maxSeg * 0.7079; // -3 dB
        int firstLoud = 0, lastLoud = segs - 1;
        for (int s = 0; s < segs; s++) { if (segRms[s] >= thresh) { firstLoud = s; break; } }
        for (int s = segs - 1; s >= 0; s--) { if (segRms[s] >= thresh) { lastLoud = s; break; } }

        double attackEnd = (double)firstLoud * segLen / sampleRate;
        double fadeOnset = (double)(lastLoud + 1) * segLen / sampleRate;
        double steadyLen = fadeOnset - attackEnd;
        Console.WriteLine($"Attack settles by ~{attackEnd:0.000}s; level holds until ~{fadeOnset:0.000}s.");
        Console.WriteLine($"Tail/fade-out occupies the final ~{seconds - fadeOnset:0.000}s.");
        Console.WriteLine($"Steady (full-level) region: ~{attackEnd:0.000}s .. {fadeOnset:0.000}s  ({steadyLen:0.000}s long).");
        Console.WriteLine();

        // --- Pitch drift across the steady region (zero-crossing rate per
        //     third as a cheap fundamental proxy). A spin-down shows a
        //     falling rate => evolving sound, poor continuous-loop material.
        int sa = (int)(attackEnd * sampleRate);
        int sb = Math.Min(mono.Length, (int)(fadeOnset * sampleRate));
        if (sb - sa > 300)
        {
            int third = (sb - sa) / 3;
            double z0 = ZcrHz(mono, sa, sa + third, sampleRate);
            double z1 = ZcrHz(mono, sa + third, sa + 2 * third, sampleRate);
            double z2 = ZcrHz(mono, sa + 2 * third, sb, sampleRate);
            Console.WriteLine("Pitch proxy (zero-crossing rate) across steady region, by third:");
            Console.WriteLine($"  start {z0,6:0} Hz   mid {z1,6:0} Hz   end {z2,6:0} Hz");
            double drift = z0 > 1 ? (z0 - z2) / z0 : 0;
            if (Math.Abs(drift) > 0.20)
                Console.WriteLine($"  -> EVOLVING ({drift * 100:0}% drift): a continuous loop will sound like repeating sweeps. Better as a pulse.");
            else
                Console.WriteLine($"  -> fairly steady ({drift * 100:0}% drift): continuous looping is viable.");
            Console.WriteLine();
        }

        // --- Loop seam quality. Pick loop start/end at zero crossings inside
        //     the steady region and report the level discontinuity at the wrap.
        if (sb - sa > 64)
        {
            int loopStart = NextZeroCrossing(mono, sa, sb);
            int loopEnd = PrevZeroCrossing(mono, sb, sa);
            double seam = Math.Abs(mono[loopEnd] - mono[loopStart]);
            double seamRel = peak > 0 ? seam / peak : 0;
            Console.WriteLine($"Suggested loop region (zero-crossing aligned): {(double)loopStart / sampleRate:0.000}s .. {(double)loopEnd / sampleRate:0.000}s.");
            Console.WriteLine($"  seam level jump at wrap: {seamRel * 100:0.0}% of peak.");
            if (seamRel > 0.05)
                Console.WriteLine("  -> audible click likely; add a short crossfade (~20-40 ms) at the wrap.");
            else
                Console.WriteLine("  -> seam is small; a brief (~10-20 ms) crossfade makes it click-free.");
            Console.WriteLine();
        }

        // --- Loop-wrap discontinuity check. Treats the file itself as the
        //     loop body: a click at the seam shows up as the first-difference
        //     |x[0]-x[N-1]| being an outlier vs the in-body differences. Also
        //     locates the single largest sample jump anywhere (a transient).
        {
            int n = mono.Length;
            double wrapJump = Math.Abs(mono[0] - mono[n - 1]);
            double maxJump = 0; int maxAt = 0;
            double sumJump = 0;
            for (int i = 1; i < n; i++)
            {
                double d = Math.Abs(mono[i] - mono[i - 1]);
                sumJump += d;
                if (d > maxJump) { maxJump = d; maxAt = i; }
            }
            double meanJump = sumJump / Math.Max(1, n - 1);
            Console.WriteLine("Loop-wrap check (treating this file as the loop body):");
            Console.WriteLine($"  wrap jump (last->first sample): {wrapJump / (peak > 0 ? peak : 1) * 100:0.0}% of peak");
            Console.WriteLine($"  mean in-body sample jump:       {meanJump / (peak > 0 ? peak : 1) * 100:0.0}% of peak");
            Console.WriteLine($"  largest jump anywhere:          {maxJump / (peak > 0 ? peak : 1) * 100:0.0}% of peak at {(double)maxAt / sampleRate:0.000}s");
            if (wrapJump > 6 * meanJump)
                Console.WriteLine("  -> wrap jump is a strong outlier: a click at the seam. Lengthen/adjust the crossfade.");
            else if (maxJump > 8 * meanJump)
                Console.WriteLine($"  -> a transient sits inside the loop at {(double)maxAt / sampleRate:0.000}s (recurs every loop). Move the region to avoid it, or place it at the seam.");
            else
                Console.WriteLine("  -> no strong discontinuity; any audible pulse is level/texture, not a click.");
            Console.WriteLine();
        }

        // --- Recommendation.
        Console.WriteLine("Recommendation:");
        if (steadyLen < 0.15)
            Console.WriteLine("  No usable steady region — don't continuous-loop this. Use it as a re-triggered pulse (fire it fresh each tick; it never reaches the fade).");
        else
            Console.WriteLine($"  Either (A) trim to {attackEnd:0.000}s..{fadeOnset:0.000}s with a short crossfade and loop that, or (B) re-trigger it as a pulse shorter than {fadeOnset:0.000}s so the fade never plays.");
        Console.WriteLine("  The fade-out is only avoided by trimming the loop region or by pulsing — a whole-buffer loop WILL play the fade and then click on wrap.");
        return 0;
    }

    // ------------------------------------------------------------------
    // Build a seamless continuous-loop WAV. Trims to [startSec, endSec],
    // then hides the wrap by overlap-adding the C samples that FOLLOW the
    // loop end onto the loop's first C samples with an equal-power
    // crossfade. Because the blended head begins from the sample just past
    // the loop end (x[start+M]) and the loop's last sample is x[start+M-1],
    // the wrap x[start+M-1] -> x[start+M] is continuous in the source — so
    // the loop is click-free. Output is 16-bit mono PCM.
    // ------------------------------------------------------------------
    public static int MakeLoop(string inPath, string outPath, double startSec,
                               double endSec, double crossfadeMs, double peakDbfs,
                               bool flatten = false, bool pingPong = false)
    {
        if (!File.Exists(inPath))
        {
            Console.Error.WriteLine($"File not found: {inPath}");
            return 1;
        }

        double[] x;
        int sampleRate;
        try { (x, sampleRate) = ReadMono(inPath); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Could not read {inPath}: {ex.Message}");
            return 1;
        }

        // Auto-detect the steady region if start/end were not given.
        if (startSec < 0 || endSec < 0)
        {
            var (a, b) = DetectSteadyRegion(x, sampleRate);
            if (startSec < 0) startSec = a;
            if (endSec < 0) endSec = b;
            Console.Error.WriteLine($"Auto steady region: {startSec:0.000}s .. {endSec:0.000}s");
        }

        int start = (int)Math.Round(startSec * sampleRate);
        int end = (int)Math.Round(endSec * sampleRate);
        start = Math.Clamp(start, 0, x.Length - 2);
        end = Math.Clamp(end, start + 2, x.Length);
        int m = end - start;                       // audible loop length
        int c = (int)Math.Round(crossfadeMs / 1000.0 * sampleRate);
        c = Math.Max(0, Math.Min(c, m / 2));       // crossfade can't exceed half the loop

        // Tail must exist after `end` to crossfade against. If not, shrink C.
        int availTail = x.Length - end;
        if (c > availTail) c = Math.Max(0, availTail);

        // Flatten the amplitude envelope across the region actually used
        // (loop body + the crossfade tail past `end`) so a decaying source
        // doesn't leave a periodic loudness pulse in the loop.
        if (flatten)
            FlattenRegion(x, start, Math.Min(x.Length, end + c), sampleRate);

        var outBuf = new double[m];
        // Body first (so any non-crossfaded region is a straight copy).
        for (int i = 0; i < m; i++) outBuf[i] = x[start + i];
        // Crossfade the first C samples: head (fade in) + tail-beyond-end (fade out).
        for (int i = 0; i < c; i++)
        {
            double t = (double)i / c;              // 0..1
            double gIn = Math.Sin(0.5 * Math.PI * t);   // equal-power in
            double gOut = Math.Cos(0.5 * Math.PI * t);  // equal-power out
            double head = x[start + i];
            double tail = x[end + i];              // sample just past the loop end
            outBuf[i] = head * gIn + tail * gOut;
        }

        // Peak-normalise (skip if target >= 0).
        double appliedGainDb = 0;
        if (peakDbfs < 0)
        {
            double pk = 0;
            for (int i = 0; i < m; i++) { double a = Math.Abs(outBuf[i]); if (a > pk) pk = a; }
            if (pk > 1e-9)
            {
                double target = Math.Pow(10.0, peakDbfs / 20.0);
                double g = target / pk;
                appliedGainDb = 20.0 * Math.Log10(g);
                for (int i = 0; i < m; i++) outBuf[i] *= g;
            }
        }

        // Ping-pong: append a reversed copy so the loop plays forward then
        // back, doubling the period and breaking the exact repeat. The
        // forward->reverse and reverse->forward joins land on a duplicated
        // sample, which is inaudible.
        if (pingPong)
        {
            var pp = new double[m * 2];
            Array.Copy(outBuf, 0, pp, 0, m);
            for (int i = 0; i < m; i++) pp[m + i] = outBuf[m - 1 - i];
            outBuf = pp;
            m *= 2;
        }

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath))!);
        WriteWavMono16(outPath, outBuf, sampleRate);

        Console.WriteLine($"Wrote loop: {outPath}");
        Console.WriteLine($"  source     {Path.GetFileName(inPath)}");
        Console.WriteLine($"  region     {startSec:0.000}s .. {endSec:0.000}s   ({(double)m / sampleRate:0.000}s loop{(pingPong ? ", ping-pong" : "")})");
        Console.WriteLine($"  crossfade  {(double)c / sampleRate * 1000.0:0.0} ms (equal-power)");
        Console.WriteLine($"  flatten    {(flatten ? "on (constant-level)" : "off")}");
        Console.WriteLine($"  pingpong   {(pingPong ? "on (forward+reversed)" : "off")}");
        Console.WriteLine($"  normalise  {(peakDbfs < 0 ? $"to {peakDbfs:0.0} dBFS (applied {appliedGainDb:+0.0;-0.0} dB)" : "skipped")}");
        Console.WriteLine($"  format     16-bit mono PCM @ {sampleRate} Hz");
        return 0;
    }

    // ------------------------------------------------------------------
    // Concatenate a WAV back-to-back N times. For a file that already loops
    // seamlessly, this reproduces the exact wrap at each join, so the result
    // is gapless — useful for players that re-buffer (and thus gap) on repeat.
    // ------------------------------------------------------------------
    public static int RepeatWav(string inPath, string outPath, int reps)
    {
        if (!File.Exists(inPath))
        {
            Console.Error.WriteLine($"File not found: {inPath}");
            return 1;
        }
        if (reps < 1) reps = 1;

        double[] x;
        int sampleRate;
        try { (x, sampleRate) = ReadMono(inPath); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Could not read {inPath}: {ex.Message}");
            return 1;
        }

        var outBuf = new double[x.Length * reps];
        for (int r = 0; r < reps; r++)
            Array.Copy(x, 0, outBuf, r * x.Length, x.Length);

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath))!);
        WriteWavMono16(outPath, outBuf, sampleRate);

        Console.WriteLine($"Wrote {reps}x repeat: {outPath}");
        Console.WriteLine($"  source   {Path.GetFileName(inPath)}  ({(double)x.Length / sampleRate:0.000}s)");
        Console.WriteLine($"  total    {(double)outBuf.Length / sampleRate:0.000}s   16-bit mono PCM @ {sampleRate} Hz");
        return 0;
    }

    // ------------------------------------------------------------------
    // Flatten the amplitude envelope of x over [from, to) to constant level
    // by dividing out a smoothed (30 ms) RMS envelope. Gain is clamped to
    // [0.5x, 2x] so near-silent dips aren't blown up into noise. Target
    // level is the median window RMS, so the overall loudness is preserved.
    // ------------------------------------------------------------------
    private static void FlattenRegion(double[] x, int from, int to, int sampleRate)
    {
        int n = to - from;
        if (n < 256) return;
        int w = Math.Max(64, (int)(0.030 * sampleRate)); // 30 ms window

        var pre = new double[n + 1];
        for (int i = 0; i < n; i++) pre[i + 1] = pre[i] + x[from + i] * x[from + i];

        var env = new double[n];
        for (int i = 0; i < n; i++)
        {
            int a = Math.Max(0, i - w / 2);
            int b = Math.Min(n, i + w / 2);
            double meanSq = (pre[b] - pre[a]) / Math.Max(1, b - a);
            env[i] = Math.Sqrt(meanSq);
        }

        var sorted = (double[])env.Clone();
        Array.Sort(sorted);
        double target = sorted[n / 2]; // median window RMS
        if (target < 1e-9) return;

        for (int i = 0; i < n; i++)
        {
            double g = target / (env[i] + 1e-9);
            if (g < 0.5) g = 0.5; else if (g > 2.0) g = 2.0;
            x[from + i] *= g;
        }
    }

    private static (double startSec, double endSec) DetectSteadyRegion(double[] mono, int sampleRate)
    {
        const int segs = 24;
        var segRms = new double[segs];
        double maxSeg = 0;
        int segLen = Math.Max(1, mono.Length / segs);
        for (int s = 0; s < segs; s++)
        {
            int a = s * segLen;
            int b = (s == segs - 1) ? mono.Length : Math.Min(mono.Length, a + segLen);
            double ss = 0; int n = Math.Max(1, b - a);
            for (int i = a; i < b; i++) ss += mono[i] * mono[i];
            segRms[s] = Math.Sqrt(ss / n);
            if (segRms[s] > maxSeg) maxSeg = segRms[s];
        }
        double thresh = maxSeg * 0.7079; // -3 dB
        int firstLoud = 0, lastLoud = segs - 1;
        for (int s = 0; s < segs; s++) { if (segRms[s] >= thresh) { firstLoud = s; break; } }
        for (int s = segs - 1; s >= 0; s--) { if (segRms[s] >= thresh) { lastLoud = s; break; } }
        return ((double)firstLoud * segLen / sampleRate,
                (double)(lastLoud + 1) * segLen / sampleRate);
    }

    private static void WriteWavMono16(string path, double[] mono, int sampleRate)
    {
        int dataBytes = mono.Length * 2;
        using var fs = new FileStream(path, FileMode.Create, FileAccess.Write);
        using var w = new BinaryWriter(fs);
        // RIFF header
        w.Write(new[] { 'R', 'I', 'F', 'F' });
        w.Write(36 + dataBytes);
        w.Write(new[] { 'W', 'A', 'V', 'E' });
        // fmt chunk
        w.Write(new[] { 'f', 'm', 't', ' ' });
        w.Write(16);                       // chunk size
        w.Write((ushort)1);                // PCM
        w.Write((ushort)1);                // mono
        w.Write(sampleRate);
        w.Write(sampleRate * 2);           // byte rate
        w.Write((ushort)2);                // block align
        w.Write((ushort)16);               // bits/sample
        // data chunk
        w.Write(new[] { 'd', 'a', 't', 'a' });
        w.Write(dataBytes);
        foreach (var s in mono)
        {
            double v = s < -1.0 ? -1.0 : s > 1.0 ? 1.0 : s;
            short i16 = (short)Math.Round(v * 32767.0);
            w.Write(i16);
        }
    }

    private static double SliceCentroid(double[] x, int center, int sampleRate)
    {
        const int n = 1024;
        if (x.Length < n) return 0;
        int start = Math.Max(0, Math.Min(x.Length - n, center - n / 2));
        var re = new double[n];
        var im = new double[n];
        var win = HannWindow(n);
        for (int i = 0; i < n; i++) { re[i] = x[start + i] * win[i]; im[i] = 0; }
        Fft(re, im);
        double binHz = (double)sampleRate / n;
        double num = 0, den = 0;
        for (int k = 1; k < n / 2; k++)
        {
            double mag = Math.Sqrt(re[k] * re[k] + im[k] * im[k]);
            num += k * binHz * mag;
            den += mag;
        }
        return den > 0 ? num / den : 0;
    }

    private static double Dbfs(double lin) => lin > 1e-9 ? 20.0 * Math.Log10(lin) : -120.0;

    private static double ZcrHz(double[] x, int a, int b, int sampleRate)
    {
        if (b - a < 2) return 0;
        int crossings = 0;
        for (int i = a + 1; i < b; i++)
            if ((x[i - 1] < 0 && x[i] >= 0) || (x[i - 1] >= 0 && x[i] < 0)) crossings++;
        double dur = (double)(b - a) / sampleRate;
        return dur > 0 ? crossings / (2.0 * dur) : 0; // two crossings per cycle
    }

    private static int NextZeroCrossing(double[] x, int from, int limit)
    {
        for (int i = Math.Max(1, from); i < limit; i++)
            if (x[i - 1] < 0 && x[i] >= 0) return i;
        return from;
    }

    private static int PrevZeroCrossing(double[] x, int from, int lowLimit)
    {
        for (int i = Math.Min(x.Length - 1, from) - 1; i > lowLimit; i--)
            if (x[i - 1] < 0 && x[i] >= 0) return i;
        return Math.Min(x.Length - 1, from);
    }

    private static double OnsetRate(List<double> flux, double seconds)
    {
        if (flux.Count < 3 || seconds <= 0) return 0;
        double mean = 0;
        foreach (var v in flux) mean += v;
        mean /= flux.Count;
        double var = 0;
        foreach (var v in flux) { double d = v - mean; var += d * d; }
        double std = Math.Sqrt(var / flux.Count);
        double thresh = mean + 1.0 * std;

        int peaks = 0;
        for (int i = 1; i < flux.Count - 1; i++)
        {
            if (flux[i] > thresh && flux[i] >= flux[i - 1] && flux[i] > flux[i + 1])
                peaks++;
        }
        return peaks / seconds;
    }

    private static double[] HannWindow(int n)
    {
        var w = new double[n];
        for (int i = 0; i < n; i++)
            w[i] = 0.5 * (1.0 - Math.Cos(2.0 * Math.PI * i / (n - 1)));
        return w;
    }

    private static double Clamp01(double v) => v < 0 ? 0 : v > 1 ? 1 : v;
    private static double Clamp(double v, double lo, double hi) => v < lo ? lo : v > hi ? hi : v;

    // --- Iterative radix-2 Cooley-Tukey FFT (in place). FrameSize is a
    //     power of two, so no zero-padding is needed.
    private static void Fft(double[] re, double[] im)
    {
        int n = re.Length;
        // Bit-reversal permutation.
        for (int i = 1, j = 0; i < n; i++)
        {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j)
            {
                (re[i], re[j]) = (re[j], re[i]);
                (im[i], im[j]) = (im[j], im[i]);
            }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            double ang = -2.0 * Math.PI / len;
            double wRe = Math.Cos(ang), wIm = Math.Sin(ang);
            for (int i = 0; i < n; i += len)
            {
                double curRe = 1.0, curIm = 0.0;
                for (int k = 0; k < len / 2; k++)
                {
                    int a = i + k, b = i + k + len / 2;
                    double tRe = re[b] * curRe - im[b] * curIm;
                    double tIm = re[b] * curIm + im[b] * curRe;
                    re[b] = re[a] - tRe;
                    im[b] = im[a] - tIm;
                    re[a] += tRe;
                    im[a] += tIm;
                    double nRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = nRe;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Minimal RIFF/WAVE reader → mono double[] in [-1, 1].
    // ------------------------------------------------------------------
    private static (double[] mono, int sampleRate) ReadMono(string path)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length < 44 ||
            bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'F' || bytes[3] != 'F' ||
            bytes[8] != 'W' || bytes[9] != 'A' || bytes[10] != 'V' || bytes[11] != 'E')
            throw new InvalidDataException("not a RIFF/WAVE file");

        int audioFormat = 0, channels = 0, sampleRate = 0, bitsPerSample = 0, blockAlign = 0;
        int dataOffset = -1, dataLength = 0;

        int p = 12;
        while (p + 8 <= bytes.Length)
        {
            string id = Encoding.ASCII.GetString(bytes, p, 4);
            int size = BitConverter.ToInt32(bytes, p + 4);
            int body = p + 8;
            if (size < 0 || body + size > bytes.Length) size = bytes.Length - body;

            if (id == "fmt ")
            {
                audioFormat = BitConverter.ToUInt16(bytes, body);
                channels = BitConverter.ToUInt16(bytes, body + 2);
                sampleRate = BitConverter.ToInt32(bytes, body + 4);
                blockAlign = BitConverter.ToUInt16(bytes, body + 12);
                bitsPerSample = BitConverter.ToUInt16(bytes, body + 14);
                if (audioFormat == 0xFFFE && size >= 24) // WAVE_FORMAT_EXTENSIBLE
                    audioFormat = BitConverter.ToUInt16(bytes, body + 24); // SubFormat tag
            }
            else if (id == "data")
            {
                dataOffset = body;
                dataLength = size;
            }
            p = body + size + (size & 1); // chunks are word-aligned
            if (dataOffset >= 0 && audioFormat != 0) { /* keep scanning is fine */ }
        }

        if (dataOffset < 0 || channels <= 0 || sampleRate <= 0)
            throw new InvalidDataException("missing fmt/data chunk");

        // IMA ADPCM (format 17): a few KOTOR SFX (ship blasters) are 4-bit
        // compressed. Decode to PCM before feature extraction so they aren't
        // silently dropped — blaster sounds are exactly the broadband
        // transients we want to surface.
        if (audioFormat == 17)
        {
            if (channels != 1)
                throw new InvalidDataException("multi-channel IMA ADPCM not supported");
            var pcm = DecodeImaAdpcmMono(bytes, dataOffset, dataLength, blockAlign);
            return (pcm, sampleRate);
        }

        if (bitsPerSample <= 0)
            throw new InvalidDataException("missing bits/sample");

        int bytesPerSample = bitsPerSample / 8;
        int frameStride = bytesPerSample * channels;
        if (frameStride <= 0) throw new InvalidDataException("bad frame stride");
        int frameCount = dataLength / frameStride;
        var mono = new double[frameCount];

        for (int f = 0; f < frameCount; f++)
        {
            int baseIdx = dataOffset + f * frameStride;
            double sum = 0;
            for (int c = 0; c < channels; c++)
            {
                int si = baseIdx + c * bytesPerSample;
                sum += ReadSample(bytes, si, bitsPerSample, audioFormat);
            }
            mono[f] = sum / channels;
        }
        return (mono, sampleRate);
    }

    private static double ReadSample(byte[] b, int i, int bits, int format)
    {
        // format 3 = IEEE float; otherwise integer PCM.
        if (format == 3)
        {
            if (bits == 32) return BitConverter.ToSingle(b, i);
            if (bits == 64) return BitConverter.ToDouble(b, i);
        }
        switch (bits)
        {
            case 8:  // unsigned
                return (b[i] - 128) / 128.0;
            case 16:
                return BitConverter.ToInt16(b, i) / 32768.0;
            case 24:
            {
                int v = b[i] | (b[i + 1] << 8) | (b[i + 2] << 16);
                if ((v & 0x800000) != 0) v |= unchecked((int)0xFF000000);
                return v / 8388608.0;
            }
            case 32:
                return BitConverter.ToInt32(b, i) / 2147483648.0;
            default:
                throw new InvalidDataException($"unsupported bits/sample: {bits}");
        }
    }

    // ------------------------------------------------------------------
    // IMA / DVI ADPCM mono decoder. Each block of `blockAlign` bytes
    // begins with a 4-byte header (predictor int16, step index byte,
    // reserved byte), followed by 4-bit nibbles (low nibble first).
    // ------------------------------------------------------------------
    private static readonly int[] ImaIndexTable =
        { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

    private static readonly int[] ImaStepTable =
    {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
        41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
        190, 210, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767,
    };

    private static double[] DecodeImaAdpcmMono(byte[] b, int dataOffset, int dataLength, int blockAlign)
    {
        if (blockAlign < 5) throw new InvalidDataException("bad ADPCM block align");
        var outSamples = new List<double>(dataLength * 2);
        int end = dataOffset + dataLength;

        for (int blockStart = dataOffset; blockStart + 4 <= end; blockStart += blockAlign)
        {
            int predictor = BitConverter.ToInt16(b, blockStart);
            int index = b[blockStart + 2];
            if (index < 0) index = 0; else if (index > 88) index = 88;

            outSamples.Add(predictor / 32768.0); // initial header sample

            int blockEnd = Math.Min(blockStart + blockAlign, end);
            for (int i = blockStart + 4; i < blockEnd; i++)
            {
                byte octet = b[i];
                for (int half = 0; half < 2; half++)
                {
                    int nibble = half == 0 ? (octet & 0x0F) : (octet >> 4);
                    int step = ImaStepTable[index];
                    int diff = step >> 3;
                    if ((nibble & 1) != 0) diff += step >> 2;
                    if ((nibble & 2) != 0) diff += step >> 1;
                    if ((nibble & 4) != 0) diff += step;
                    if ((nibble & 8) != 0) predictor -= diff; else predictor += diff;
                    if (predictor > 32767) predictor = 32767;
                    else if (predictor < -32768) predictor = -32768;
                    index += ImaIndexTable[nibble];
                    if (index < 0) index = 0; else if (index > 88) index = 88;
                    outSamples.Add(predictor / 32768.0);
                }
            }
        }
        return outSamples.ToArray();
    }
}
