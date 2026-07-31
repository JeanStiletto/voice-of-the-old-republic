using System.Diagnostics;

namespace Kdev;

/// <summary>
/// Shared helpers for inspecting and terminating the running KOTOR process.
/// </summary>
public static class GameProcess
{
    public const string ExeName = "swkotor.exe";
    public const string ProcessName = "swkotor";
    private const int WaitForExitMs = 5000;

    public sealed record KillSummary(int FoundCount, int KilledCount, int FailedCount)
    {
        public bool AllKilled => FoundCount == KilledCount;
        public bool NothingToKill => FoundCount == 0;
    }

    /// <summary>
    /// Terminates every running instance of the given game and waits for them
    /// to exit. Logs each PID as it is killed. Returns a summary; caller
    /// decides how to report.
    /// </summary>
    /// <param name="processName">
    /// Process name without extension. Defaults to KOTOR 1. Note that
    /// GetProcessesByName matches exactly, so "swkotor" does NOT also find
    /// "swkotor2" — the KOTOR 2 target must pass its own name.
    /// </param>
    public static KillSummary KillAll(string processName = ProcessName)
    {
        var procs = Process.GetProcessesByName(processName);
        if (procs.Length == 0)
        {
            return new KillSummary(0, 0, 0);
        }

        var killed = 0;
        var failed = 0;

        foreach (var proc in procs)
        {
            var pid = proc.Id;
            try
            {
                Console.WriteLine($"  Killing PID {pid}...");
                proc.Kill(entireProcessTree: true);
                if (proc.WaitForExit(WaitForExitMs))
                {
                    Console.WriteLine($"  PID {pid} terminated.");
                    killed++;
                }
                else
                {
                    Console.Error.WriteLine($"  ERROR: PID {pid} did not exit within {WaitForExitMs}ms.");
                    failed++;
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"  ERROR: Failed to kill PID {pid}: {ex.Message}");
                failed++;
            }
            finally
            {
                proc.Dispose();
            }
        }

        return new KillSummary(procs.Length, killed, failed);
    }
}
