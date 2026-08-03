using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Installs the Unofficial TSLRCM Tweak Pack (Pavijan357, KOTOR 2) — reverts
    /// a handful of TSLRCM restorations that Obsidian most likely cut on purpose.
    ///
    /// Pipeline: DeadlyStream guest scrape (same cookie + csrfKey flow as
    /// TSLRCM), pinned SHA-256 verification, RAR5 extraction via Windows'
    /// built-in <c>tar.exe</c> (libarchive reads RAR5 — no bundled extractor
    /// needed), then one headless HoloPatcher run per component. We never touch
    /// the archive's bundled TSLPatcher exes; each component folder carries its
    /// own <c>changes.ini</c> and is staged as a standalone tslpatchdata dir
    /// (Part 2's <c>changes2.ini</c> is renamed on staging), so the archive's
    /// <c>namespaces.ini</c> multi-component mechanism is bypassed entirely.
    ///
    /// Component set mirrors the archive's namespaces.ini minus the two Extras:
    /// "Trayus Sith Lord Masks" is visual-only and "Gand Warrior's Awareness
    /// Check" is a rebalance — both outside our bundle filters
    /// (docs/installer.md, "KOTOR 2 mod bundle").
    ///
    /// Requires TSLRCM 1.8.3+; the KOTOR 2 flow gates this installer on TSLRCM
    /// presence before it ever runs.
    /// </summary>
    public sealed class TweakPackInstaller : IModInstaller
    {
        public string Id => "tweakpack";
        public string DisplayName => $"Unofficial TSLRCM Tweak Pack ({Config.TweakPackDisplayVersion})";

        public bool IsSelected(ModSelection selection) => selection?.TweakPack == true;

        // Path inside the extracted archive that holds the per-component
        // tslpatchdata folders.
        private const string ArchiveTslpatchdataPath = "Individual component installer/tslpatchdata";

        // (DataPath relative to the tslpatchdata dir, ini name, display name).
        // Mirrors namespaces.ini in the pinned archive; safe to hardcode
        // because the archive is pinned by SHA-256.
        private static readonly (string DataPath, string IniName, string Name)[] Components =
        {
            (@"1-Kaevee_Removal",         "changes.ini",  "Kaevee Removal, Part 1"),
            (@"1-Kaevee_Removal\Part_2",  "changes2.ini", "Kaevee Removal, Part 2"),
            (@"2-Saedhe",                 "changes.ini",  "Saedhe's Head"),
            (@"3-Ravager",                "changes.ini",  "Ravager Mandalore Changes"),
            (@"4-Atton",                  "changes.ini",  "Atton at the End"),
            (@"5-Kreia_Atris_DLG",        "changes.ini",  "Kreia-Atris Dialogue Tweak"),
            (@"6-Mandalore_Trayus",       "changes.ini",  "Trayus Mandalore Conversation"),
        };

        public async Task<ModInstallResult> InstallAsync(ModInstallContext ctx)
        {
            string stagingRoot = null;

            try
            {
                if (string.IsNullOrEmpty(ctx.HoloPatcherExePath) || !File.Exists(ctx.HoloPatcherExePath))
                {
                    return ModInstallResult.Fail(Id, "HoloPatcher.exe not available.");
                }

                stagingRoot = Path.Combine(Path.GetTempPath(), $"kotor_acc_tweak_{Guid.NewGuid():N}");
                Directory.CreateDirectory(stagingRoot);
                string rarPath = Path.Combine(stagingRoot, Config.TweakPackArchiveFileName);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_TweakDownloading"));
                ctx.Progress?.Invoke(0);

                // Automatic acquisition first; if it does not work, the user
                // fetches the archive and we carry on from there. Whichever way
                // it arrives, everything after this point is identical.
                string manualReason = null;
                try
                {
                    using (var ds = new DeadlyStreamClient())
                    {
                        await ds.DownloadFileAsync(Config.TweakPackDownloadPageUrl, rarPath, (done, total) =>
                        {
                            long effectiveTotal = total > 0 ? total : Config.TweakPackArchiveSizeBytes;
                            ctx.Progress?.Invoke((int)Math.Min(15, done * 15 / Math.Max(1, effectiveTotal)));
                        });
                    }

                    string hash = DeadlyStreamClient.ComputeSha256(rarPath);
                    if (!hash.Equals(Config.TweakPackArchiveSha256, StringComparison.OrdinalIgnoreCase))
                    {
                        // Downloaded fine, just not the build we were pinned
                        // against — almost always an upstream update. Different
                        // sentence, same recovery.
                        Logger.Warning($"Tweak Pack hash mismatch: expected {Config.TweakPackArchiveSha256}, got {hash}");
                        manualReason = InstallerLocale.Get("ManualDownload_Reason_Updated");
                    }
                }
                catch (Exception ex)
                {
                    Logger.Warning($"Tweak Pack download failed: {ex.Message}");
                    manualReason = InstallerLocale.Format("ManualDownload_Reason_Failed_Format", ex.Message);
                }

                if (manualReason != null)
                {
                    string supplied = ctx.AskForManualDownload?.Invoke(new ManualDownloadRequest
                    {
                        ModDisplayName = "Unofficial TSLRCM Tweak Pack " + Config.TweakPackDisplayVersion,
                        Reason = manualReason,
                        PageUrl = Config.TweakPackDownloadPageUrl,
                        ExpectedFileName = Config.TweakPackArchiveFileName,
                        Kind = ManualDownloadForm.FileKind.RarArchive,
                        PinnedSha256 = Config.TweakPackArchiveSha256,
                    });

                    if (string.IsNullOrEmpty(supplied))
                        return ModInstallResult.Fail(Id, manualReason);

                    // Copy rather than extract in place: staging is deleted at
                    // the end of this method, and that must never take the
                    // user's own download with it.
                    File.Copy(supplied, rarPath, overwrite: true);
                    Logger.Info($"Tweak Pack: using the user-supplied archive {supplied}");
                }

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_TweakExtracting"));
                ctx.Progress?.Invoke(18);

                string extractDir = Path.Combine(stagingRoot, "extracted");
                Directory.CreateDirectory(extractDir);
                var tarResult = await ExtractRarAsync(rarPath, extractDir);
                if (!tarResult.Success)
                {
                    return ModInstallResult.Fail(Id, tarResult.Error);
                }

                string srcRoot = Path.Combine(extractDir,
                    ArchiveTslpatchdataPath.Replace('/', Path.DirectorySeparatorChar));
                if (!Directory.Exists(srcRoot))
                {
                    return ModInstallResult.Fail(Id,
                        $"Extracted archive is missing '{ArchiveTslpatchdataPath}' — layout may have changed.");
                }

                for (int i = 0; i < Components.Length; i++)
                {
                    var comp = Components[i];
                    ctx.StatusUpdate?.Invoke(InstallerLocale.Format(
                        "ModInstall_TweakComponent_Format", i + 1, Components.Length, comp.Name));

                    // Stage the component as a standalone tslpatchdata dir with
                    // the default ini name HoloPatcher expects.
                    string compStaging = Path.Combine(stagingRoot, $"comp_{i}", "tslpatchdata");
                    CopyDirectory(Path.Combine(srcRoot, comp.DataPath), compStaging);
                    if (!comp.IniName.Equals("changes.ini", StringComparison.OrdinalIgnoreCase))
                    {
                        File.Copy(
                            Path.Combine(compStaging, comp.IniName),
                            Path.Combine(compStaging, "changes.ini"),
                            overwrite: true);
                    }

                    var holoResult = await HoloPatcherRunner.RunAsync(
                        ctx.HoloPatcherExePath, ctx.GameDir, compStaging, ctx.StatusUpdate,
                        "ModInstall_TweakProgress_Format", "ModInstall_TweakHeartbeat_Format");

                    if (!holoResult.Success)
                    {
                        return ModInstallResult.Fail(Id,
                            $"component '{comp.Name}': {holoResult.Error}");
                    }

                    ctx.Progress?.Invoke(20 + (i + 1) * 80 / Components.Length);
                    Logger.Info($"Tweak Pack component OK: {comp.Name}");
                }

                ctx.Progress?.Invoke(100);
                Logger.Info($"Tweak Pack install complete ({Components.Length} components).");
                return ModInstallResult.Ok(Id);
            }
            catch (Exception ex)
            {
                Logger.Error("Tweak Pack install failed", ex);
                return ModInstallResult.Fail(Id, ex.Message);
            }
            finally
            {
                if (stagingRoot != null && Directory.Exists(stagingRoot))
                {
                    try { Directory.Delete(stagingRoot, recursive: true); }
                    catch (Exception cleanupEx)
                    {
                        Logger.Warning($"Could not clean up Tweak Pack staging dir {stagingRoot}: {cleanupEx.Message}");
                    }
                }
            }
        }

        /// <summary>
        /// Extracts a RAR5 archive with Windows' built-in <c>tar.exe</c>
        /// (libarchive, present since Windows 10 1803 and reads RAR5). We shell
        /// out rather than bundling an extractor: 7zr.exe (already bundled)
        /// handles .7z only, and .NET has no RAR support.
        /// </summary>
        private static async Task<(bool Success, string Error)> ExtractRarAsync(string rarPath, string destDir)
        {
            string tarExe = Path.Combine(Environment.SystemDirectory, "tar.exe");
            if (!File.Exists(tarExe))
            {
                return (false, $"tar.exe not found at {tarExe} (requires Windows 10 1803+).");
            }

            var psi = new ProcessStartInfo
            {
                FileName = tarExe,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardError = true,
            };
            psi.ArgumentList.Add("-xf");
            psi.ArgumentList.Add(rarPath);
            psi.ArgumentList.Add("-C");
            psi.ArgumentList.Add(destDir);

            Logger.Info($"Extracting Tweak Pack: {tarExe} -xf \"{rarPath}\" -C \"{destDir}\"");

            using var proc = Process.Start(psi);
            if (proc == null) return (false, "Could not start tar.exe.");
            string stderr = await proc.StandardError.ReadToEndAsync();
            await proc.WaitForExitAsync();

            if (proc.ExitCode != 0)
            {
                return (false, $"tar.exe exited with code {proc.ExitCode}: {stderr.Trim()}");
            }
            return (true, null);
        }

        private static void CopyDirectory(string sourceDir, string destDir)
        {
            Directory.CreateDirectory(destDir);
            foreach (string file in Directory.GetFiles(sourceDir))
                File.Copy(file, Path.Combine(destDir, Path.GetFileName(file)), overwrite: true);
            foreach (string dir in Directory.GetDirectories(sourceDir))
                CopyDirectory(dir, Path.Combine(destDir, Path.GetFileName(dir)));
        }
    }
}
