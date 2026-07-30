namespace Kdev;

// ----------------------------------------------------------------------
// Per-file scoring result produced by WavAnalysis and rendered by
// Commands/SoundScoreCommand. Split out alongside the analysis engine by
// the Phase-1 structure pass (refactoring candidate 15).
// ----------------------------------------------------------------------
internal sealed class SoundScore
{
    public required string Path;
    public required string Name;
    public int Rank;
    public int Score;

    public double ElevationCapability;
    public double BearingSharpness;
    public double HfRatio5k;
    public double ElevBandRatio;
    public double Flatness;
    public double CentroidHz;
    public double OnsetsPerSec;
    public double CrestDb;
    public double Seconds;
    public int SampleRate;

    public string FlagString()
    {
        var f = new List<string>(4);
        if (ElevationCapability >= 0.50) f.Add("ELEV");
        if (BearingSharpness >= 0.50) f.Add("SHARP");
        if (Flatness >= 0.25) f.Add("NOISY");
        if (HfRatio5k < 0.10 && OnsetsPerSec < 1.5) f.Add("DRONE");
        return f.Count == 0 ? "-" : string.Join(",", f);
    }
}
