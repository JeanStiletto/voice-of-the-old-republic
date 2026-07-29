using System;
using System.Diagnostics;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Drives a headless HoloPatcher install of a staged <c>tslpatchdata/</c>
    /// against a game directory, forwarding throttled stdout lines and a
    /// heartbeat to the status callback. Shared by <see cref="K1cpInstaller"/>
    /// and <see cref="K2cpInstaller"/>; the two locale format keys let each
    /// caller keep its own mod-prefixed status strings.
    /// </summary>
    internal static class HoloPatcherRunner
    {
        // Forward HoloPatcher stdout lines as status updates at most this often.
        // Lower = more responsive, more screen-reader interruption. Higher = more
        // perceived stall during a verbose install phase.
        private const int ForwardThrottleMs = 2500;

        // Heartbeat tick when HoloPatcher hasn't said anything forwardable.
        // Set to double the forward throttle (2500 ms), so a quiet-period
        // "any progress?" update lands at predictable intervals without
        // competing with real forwarded output.
        private const int HeartbeatMs = 5000;

        public static async Task<(bool Success, string Error)> RunAsync(
            string holoPatcherExe, string gameDir, string tslpatchdataDir,
            Action<string> statusUpdate,
            string progressFormatKey, string heartbeatFormatKey)
        {
            // CLI verified against PyKotor master:
            //   HoloPatcher.exe --game-dir <game> --tslpatchdata <dir> --install [--console]
            // --install starts an unattended install and exits when done.
            // We omit --console so the user doesn't see a stray cmd window pop up;
            // HoloPatcher writes its own installlog.txt next to tslpatchdata.
            var psi = new ProcessStartInfo
            {
                FileName = holoPatcherExe,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            psi.ArgumentList.Add("--game-dir");
            psi.ArgumentList.Add(gameDir);
            psi.ArgumentList.Add("--tslpatchdata");
            psi.ArgumentList.Add(tslpatchdataDir);
            psi.ArgumentList.Add("--install");

            Logger.Info($"Invoking HoloPatcher: {holoPatcherExe} --game-dir \"{gameDir}\" --tslpatchdata \"{tslpatchdataDir}\" --install");

            try
            {
                using var proc = new Process { StartInfo = psi };
                var stdoutBuffer = new StringBuilder();
                var stderrBuffer = new StringBuilder();

                // Shared "last forwarded status" timestamp coordinates the
                // stdout-forward path and the heartbeat. Tick64 is monotonic
                // and safe to read/write via Interlocked.
                long lastForwardTicks = 0;

                proc.OutputDataReceived += (s, e) =>
                {
                    if (e.Data == null) return; // null Data signals EOF
                    lock (stdoutBuffer) stdoutBuffer.AppendLine(e.Data);

                    string line = e.Data.Trim();
                    if (line.Length == 0) return;

                    long now = Environment.TickCount64;
                    if (now - Interlocked.Read(ref lastForwardTicks) < ForwardThrottleMs)
                        return;
                    Interlocked.Exchange(ref lastForwardTicks, now);

                    // Cap length so screen readers don't spend 10 seconds on
                    // one update; a leading ellipsis on the right is fine.
                    if (line.Length > 100) line = line.Substring(0, 97) + "...";
                    statusUpdate?.Invoke(InstallerLocale.Format(progressFormatKey, line));
                };

                proc.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data == null) return;
                    lock (stderrBuffer) stderrBuffer.AppendLine(e.Data);
                };

                proc.Start();
                proc.BeginOutputReadLine();
                proc.BeginErrorReadLine();

                // Heartbeat: keep the UI feeling alive even if HoloPatcher
                // goes quiet for a long stretch (or never speaks at all).
                // Fires only when nothing else has updated status recently.
                using var heartbeatCts = new CancellationTokenSource();
                var heartbeatStarted = Environment.TickCount64;
                Task heartbeat = Task.Run(async () =>
                {
                    try
                    {
                        while (!heartbeatCts.IsCancellationRequested)
                        {
                            await Task.Delay(HeartbeatMs, heartbeatCts.Token);
                            long now = Environment.TickCount64;
                            // Quiet window: emit only if no forward in ~last tick.
                            if (now - Interlocked.Read(ref lastForwardTicks) < HeartbeatMs - 500)
                                continue;
                            Interlocked.Exchange(ref lastForwardTicks, now);
                            int elapsedSec = (int)((now - heartbeatStarted) / 1000);
                            statusUpdate?.Invoke(InstallerLocale.Format(
                                heartbeatFormatKey, elapsedSec));
                        }
                    }
                    catch (OperationCanceledException) { /* normal shutdown */ }
                });

                using var timeoutCts = new CancellationTokenSource(TimeSpan.FromMinutes(10));
                try
                {
                    await proc.WaitForExitAsync(timeoutCts.Token);
                }
                catch (OperationCanceledException)
                {
                    try { proc.Kill(entireProcessTree: true); } catch { /* best-effort */ }
                    heartbeatCts.Cancel();
                    try { await heartbeat; } catch { /* swallow */ }
                    return (false, "HoloPatcher timed out after 10 minutes; killed.");
                }

                // Per MS docs: after WaitForExitAsync returns, call WaitForExit()
                // synchronously so any in-flight OutputDataReceived /
                // ErrorDataReceived events flush before we read their buffers.
                proc.WaitForExit();

                heartbeatCts.Cancel();
                try { await heartbeat; } catch { /* swallow */ }

                string stdout, stderr;
                lock (stdoutBuffer) stdout = stdoutBuffer.ToString();
                lock (stderrBuffer) stderr = stderrBuffer.ToString();

                if (!string.IsNullOrWhiteSpace(stdout)) Logger.Info($"HoloPatcher stdout: {stdout.Trim()}");
                if (!string.IsNullOrWhiteSpace(stderr)) Logger.Warning($"HoloPatcher stderr: {stderr.Trim()}");

                if (proc.ExitCode != 0)
                {
                    string detail = !string.IsNullOrWhiteSpace(stderr)
                        ? stderr.Trim()
                        : (!string.IsNullOrWhiteSpace(stdout) ? stdout.Trim() : "(no output)");
                    return (false, $"HoloPatcher exited with code {proc.ExitCode}: {detail}");
                }

                return (true, null);
            }
            catch (Exception ex)
            {
                return (false, $"HoloPatcher invocation failed: {ex.Message}");
            }
        }
    }
}
