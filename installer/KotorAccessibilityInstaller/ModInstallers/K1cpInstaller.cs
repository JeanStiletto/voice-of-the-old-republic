using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Installs KOTOR 1 Community Patch (K1CP) from the GitHub repo at the
    /// pinned commit SHA in <see cref="Config.K1cpPinnedRef"/>.
    ///
    /// K1CP's <c>.gitattributes</c> tags <c>/tslpatchdata export-ignore</c>, so
    /// GitHub's source tarball/zip strips the directory we need. K1CP also
    /// publishes no GitHub releases (DeadlyStream-only distribution), so there
    /// is no release-asset fallback. We reconstruct <c>tslpatchdata/</c>
    /// file-by-file via the git tree API + raw.githubusercontent.com.
    ///
    /// Steps:
    /// <list type="number">
    ///   <item>List blobs under <c>tslpatchdata/</c> at the pinned commit via git tree API.</item>
    ///   <item>Download each blob via raw.githubusercontent.com into a staging dir,
    ///         with bounded concurrency and per-file retry on transient failure.</item>
    ///   <item>If the install locale has a packaged translation (DE/FR/RU), overlay
    ///         <c>translation_&lt;lang&gt;/append.tlk</c> onto <c>tslpatchdata/append.tlk</c>.
    ///         IT/ES installs: install K1CP in English; the bugfix strings appear in
    ///         English while the rest of the game stays localised (known limitation,
    ///         surfaced on the mod-selection screen footnote).</item>
    ///   <item>Invoke HoloPatcher headless against the staged tslpatchdata.</item>
    ///   <item>Clean up staging.</item>
    /// </list>
    /// </summary>
    public sealed class K1cpInstaller : IModInstaller
    {
        public string Id => "k1cp";
        public string DisplayName => $"KOTOR 1 Community Patch ({Config.K1cpDisplayVersion})";

        public bool IsSelected(ModSelection selection) => selection?.K1cp == true;

        public async Task<ModInstallResult> InstallAsync(ModInstallContext ctx)
        {
            string stagingRoot = null;

            try
            {
                if (string.IsNullOrEmpty(ctx.HoloPatcherExePath) || !File.Exists(ctx.HoloPatcherExePath))
                {
                    return ModInstallResult.Fail(Id,
                        "HoloPatcher.exe not available. Drop the binary at " +
                        "installer/KotorAccessibilityInstaller/Resources/HoloPatcher.exe and rebuild.");
                }

                stagingRoot = Path.Combine(Path.GetTempPath(), $"kotor_acc_k1cp_{Guid.NewGuid():N}");
                string tslpatchdataDir = Path.Combine(stagingRoot, "tslpatchdata");
                Directory.CreateDirectory(tslpatchdataDir);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K1cpDownloading"));
                ctx.Progress?.Invoke(0);

                using var gh = new GitHubClient();
                await FetchTslpatchdataAsync(
                    gh,
                    Config.K1cpRepoOwner,
                    Config.K1cpRepoName,
                    Config.K1cpPinnedRef,
                    tslpatchdataDir,
                    p => ctx.Progress?.Invoke(p * 55 / 100)); // 0..55 covers download

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K1cpStaging"));
                ctx.Progress?.Invoke(55);
                Logger.Info($"K1CP tslpatchdata staged at: {tslpatchdataDir}");

                ApplyLocaleOverlay(tslpatchdataDir, ctx.Locale);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K1cpApplying"));
                ctx.Progress?.Invoke(60);

                var holoResult = await RunHoloPatcherAsync(
                    ctx.HoloPatcherExePath, ctx.GameDir, tslpatchdataDir, ctx.StatusUpdate);

                if (!holoResult.Success)
                {
                    return ModInstallResult.Fail(Id, holoResult.Error);
                }

                ctx.Progress?.Invoke(100);
                Logger.Info($"K1CP install complete (HoloPatcher exit code 0).");
                return ModInstallResult.Ok(Id);
            }
            catch (Exception ex)
            {
                Logger.Error($"K1CP install failed", ex);
                return ModInstallResult.Fail(Id, ex.Message);
            }
            finally
            {
                if (stagingRoot != null && Directory.Exists(stagingRoot))
                {
                    try { Directory.Delete(stagingRoot, recursive: true); }
                    catch (Exception cleanupEx)
                    {
                        Logger.Warning($"Could not clean up K1CP staging dir {stagingRoot}: {cleanupEx.Message}");
                    }
                }
            }
        }

        /// <summary>
        /// Reconstructs <c>tslpatchdata/</c> from the K1CP repo at the pinned
        /// commit. Uses the git tree API to enumerate blobs, then downloads
        /// each one via raw.githubusercontent.com (which, unlike the source
        /// archive, is not subject to <c>.gitattributes export-ignore</c>).
        /// Concurrency-throttled to keep memory + connection count bounded.
        /// </summary>
        private static async Task FetchTslpatchdataAsync(
            GitHubClient gh, string owner, string repo, string commitSha,
            string destDir, Action<int> progress)
        {
            Logger.Info($"K1CP: enumerating tslpatchdata blobs at {owner}/{repo}@{commitSha.Substring(0, 7)}");
            var blobs = await gh.ListTreeBlobsAsync(owner, repo, commitSha, "tslpatchdata");
            if (blobs.Count == 0)
            {
                throw new InvalidOperationException(
                    $"No files found under tslpatchdata/ at {owner}/{repo}@{commitSha}. " +
                    "K1CP repo layout may have changed; bump Config.K1cpPinnedRef.");
            }

            long totalBytes = blobs.Sum(b => b.Size);
            Logger.Info($"K1CP: downloading {blobs.Count} files, {totalBytes / 1024 / 1024} MB total");

            long downloadedBytes = 0;
            int reportedPct = -1;

            // Concurrency cap: 8 is a sweet spot — fast enough to saturate most
            // home connections without slamming raw.githubusercontent.com.
            using var sem = new SemaphoreSlim(8);
            var tasks = blobs.Select(async blob =>
            {
                await sem.WaitAsync();
                try
                {
                    // Blob paths are relative to tslpatchdata/. Mirror the layout
                    // under destDir; create parent dirs as needed.
                    string destPath = Path.Combine(destDir, blob.Path.Replace('/', Path.DirectorySeparatorChar));
                    string parent = Path.GetDirectoryName(destPath);
                    if (!string.IsNullOrEmpty(parent))
                        Directory.CreateDirectory(parent);

                    string rawUrl =
                        $"https://raw.githubusercontent.com/{owner}/{repo}/{commitSha}/tslpatchdata/{blob.Path}";
                    await gh.DownloadRawAsync(rawUrl, destPath);

                    long after = Interlocked.Add(ref downloadedBytes, blob.Size);
                    if (totalBytes > 0 && progress != null)
                    {
                        int pct = (int)((after * 100) / totalBytes);
                        // Coarse rate-limit progress callbacks; cheap interlocked
                        // CAS gate avoids hammering the UI thread on every blob.
                        if (pct != reportedPct &&
                            Interlocked.Exchange(ref reportedPct, pct) != pct)
                        {
                            progress(pct);
                        }
                    }
                }
                finally
                {
                    sem.Release();
                }
            });
            await Task.WhenAll(tasks);
            progress?.Invoke(100);
        }

        private static void ApplyLocaleOverlay(string tslpatchdataDir, GameLocale locale)
        {
            // K1CP ships per-locale append.tlk overlays under
            // tslpatchdata/translation_{german,french,russian}/. The default
            // tslpatchdata/append.tlk is English. To install the localised
            // strings we copy the matching translation's append.tlk over the
            // base before invoking HoloPatcher.
            string translationDir = locale switch
            {
                GameLocale.German => "translation_german",
                GameLocale.French => "translation_french",
                // Russian: vanilla KOTOR doesn't ship a Russian dialog.tlk and our
                // GameLocale enum doesn't currently include Russian, so we can't
                // detect it. K1CP DOES ship two Russian overlays (olegkuz1997 and
                // JayDominus under tslpatchdata/translation_russian/) — wire these
                // up once a Russian user reports.
                _ => null,
            };

            if (translationDir == null)
            {
                if (locale == GameLocale.Italian || locale == GameLocale.Spanish)
                {
                    Logger.Info($"K1CP: no official translation for {locale}; " +
                                "installing English append.tlk (bugfix strings will appear in English).");
                }
                else if (locale == GameLocale.English)
                {
                    Logger.Info("K1CP: English install — using default append.tlk, no overlay needed.");
                }
                else
                {
                    Logger.Warning($"K1CP: unknown locale {locale}; installing English append.tlk.");
                }
                return;
            }

            string overlaySource = Path.Combine(tslpatchdataDir, translationDir, "append.tlk");
            string overlayTarget = Path.Combine(tslpatchdataDir, "append.tlk");

            if (!File.Exists(overlaySource))
            {
                Logger.Warning($"K1CP: expected locale overlay missing at {overlaySource}; " +
                               "falling back to English append.tlk.");
                return;
            }

            File.Copy(overlaySource, overlayTarget, overwrite: true);
            Logger.Info($"K1CP: locale overlay applied — {translationDir}/append.tlk -> tslpatchdata/append.tlk");
        }

        // Forward HoloPatcher stdout lines as status updates at most this often.
        // Lower = more responsive, more screen-reader interruption. Higher = more
        // perceived stall during a verbose install phase.
        private const int HoloPatcherForwardThrottleMs = 2500;

        // Heartbeat tick when HoloPatcher hasn't said anything forwardable.
        // Set just under the forward throttle so an "any progress?" update lands
        // at predictable intervals even when HoloPatcher goes quiet.
        private const int HoloPatcherHeartbeatMs = 5000;

        private static async Task<(bool Success, string Error)> RunHoloPatcherAsync(
            string holoPatcherExe, string gameDir, string tslpatchdataDir,
            Action<string> statusUpdate)
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
                    if (now - Interlocked.Read(ref lastForwardTicks) < HoloPatcherForwardThrottleMs)
                        return;
                    Interlocked.Exchange(ref lastForwardTicks, now);

                    // Cap length so screen readers don't spend 10 seconds on
                    // one update; a leading ellipsis on the right is fine.
                    if (line.Length > 100) line = line.Substring(0, 97) + "...";
                    statusUpdate?.Invoke(InstallerLocale.Format("ModInstall_K1cpProgress_Format", line));
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
                            await Task.Delay(HoloPatcherHeartbeatMs, heartbeatCts.Token);
                            long now = Environment.TickCount64;
                            // Quiet window: emit only if no forward in ~last tick.
                            if (now - Interlocked.Read(ref lastForwardTicks) < HoloPatcherHeartbeatMs - 500)
                                continue;
                            Interlocked.Exchange(ref lastForwardTicks, now);
                            int elapsedSec = (int)((now - heartbeatStarted) / 1000);
                            statusUpdate?.Invoke(InstallerLocale.Format(
                                "ModInstall_K1cpApplyingHeartbeat_Format", elapsedSec));
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
