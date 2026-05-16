using System;
using System.Diagnostics;
using System.Formats.Tar;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Installs KOTOR 1 Community Patch (K1CP) from the GitHub source tarball
    /// at the pinned commit SHA in <see cref="Config.K1cpPinnedRef"/>.
    ///
    /// Steps:
    /// <list type="number">
    ///   <item>Download repo tarball from GitHub (pinned commit SHA).</item>
    ///   <item>Extract <c>tslpatchdata/</c> into a staging dir.</item>
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
            string tarballPath = null;

            try
            {
                if (string.IsNullOrEmpty(ctx.HoloPatcherExePath) || !File.Exists(ctx.HoloPatcherExePath))
                {
                    return ModInstallResult.Fail(Id,
                        "HoloPatcher.exe not available. Drop the binary at " +
                        "installer/KotorAccessibilityInstaller/Resources/HoloPatcher.exe and rebuild.");
                }

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K1cpDownloading"));
                ctx.Progress?.Invoke(5);

                using var gh = new GitHubClient();
                tarballPath = await gh.DownloadRepoTarballAsync(
                    Config.K1cpRepoOwner,
                    Config.K1cpRepoName,
                    Config.K1cpPinnedRef,
                    p => ctx.Progress?.Invoke(5 + (p * 35 / 100)));

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K1cpStaging"));
                ctx.Progress?.Invoke(45);

                stagingRoot = await Task.Run(() => ExtractTarballToStaging(tarballPath));
                string tslpatchdataDir = FindTslpatchdataDir(stagingRoot);
                Logger.Info($"K1CP tslpatchdata staged at: {tslpatchdataDir}");

                ApplyLocaleOverlay(tslpatchdataDir, ctx.Locale);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K1cpApplying"));
                ctx.Progress?.Invoke(60);

                var holoResult = await Task.Run(() =>
                    RunHoloPatcher(ctx.HoloPatcherExePath, ctx.GameDir, tslpatchdataDir));

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
                if (tarballPath != null)
                {
                    try { File.Delete(tarballPath); } catch { /* best-effort */ }
                }
                if (stagingRoot != null)
                {
                    try { Directory.Delete(stagingRoot, recursive: true); }
                    catch (Exception cleanupEx)
                    {
                        Logger.Warning($"Could not clean up K1CP staging dir {stagingRoot}: {cleanupEx.Message}");
                    }
                }
            }
        }

        private static string ExtractTarballToStaging(string tarballPath)
        {
            string stagingRoot = Path.Combine(Path.GetTempPath(), $"kotor_acc_k1cp_{Guid.NewGuid():N}");
            Directory.CreateDirectory(stagingRoot);

            // GitHub tarballs are gzipped. Decompress in-stream then extract via .NET's
            // built-in TarFile (.NET 7+) — avoids pulling in SharpZipLib.
            using (var fs = new FileStream(tarballPath, FileMode.Open, FileAccess.Read))
            using (var gz = new GZipStream(fs, CompressionMode.Decompress))
            {
                TarFile.ExtractToDirectory(gz, stagingRoot, overwriteFiles: true);
            }

            Logger.Info($"Extracted K1CP tarball to: {stagingRoot}");
            return stagingRoot;
        }

        private static string FindTslpatchdataDir(string stagingRoot)
        {
            // GitHub wraps the repo in a top-level directory named
            // "<owner>-<repo>-<short-sha>/". Walk one level down to find it.
            var topDirs = Directory.GetDirectories(stagingRoot);
            if (topDirs.Length == 0)
                throw new InvalidOperationException("K1CP tarball staging is empty — extraction produced no directories");

            // Typically exactly one top-level dir. If multiple, pick the one containing tslpatchdata/.
            foreach (var topDir in topDirs)
            {
                string candidate = Path.Combine(topDir, "tslpatchdata");
                if (Directory.Exists(candidate)) return candidate;
            }

            throw new InvalidOperationException(
                $"K1CP tarball does not contain a tslpatchdata/ folder under any of: " +
                string.Join(", ", topDirs.Select(Path.GetFileName)));
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

        private static (bool Success, string Error) RunHoloPatcher(
            string holoPatcherExe, string gameDir, string tslpatchdataDir)
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
                using var proc = Process.Start(psi);
                if (proc == null)
                    return (false, "Failed to start HoloPatcher.exe (Process.Start returned null)");

                string stdout = proc.StandardOutput.ReadToEnd();
                string stderr = proc.StandardError.ReadToEnd();
                // HoloPatcher installs are typically under a minute; give a generous bound
                // before declaring it hung.
                if (!proc.WaitForExit(milliseconds: 10 * 60 * 1000))
                {
                    try { proc.Kill(entireProcessTree: true); } catch { /* best-effort */ }
                    return (false, "HoloPatcher timed out after 10 minutes; killed.");
                }

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
