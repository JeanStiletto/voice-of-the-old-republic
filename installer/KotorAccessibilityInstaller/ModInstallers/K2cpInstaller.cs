using System;
using System.IO;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Installs the KOTOR 2 Community Patch (K2CP) from its DeadlyStream release
    /// archive — the same guest scrape as TSLRCM and the Tweak Pack.
    ///
    /// <para><b>NOT from GitHub, and this is the whole story of this class.</b>
    /// It originally fetched <c>tslpatchdata/</c> from the
    /// <c>KOTORCommunityPatches/TSL_Community_Patch</c> repo, mirroring
    /// <see cref="K1cpInstaller"/>, on the documented belief that K2CP's
    /// <c>.gitattributes</c> has no <c>export-ignore</c> and so the repo tree is
    /// complete. The <c>.gitattributes</c> claim is true and the conclusion was
    /// still wrong: <b>the payload is not in that repo at all</b>. At the pinned
    /// commit the whole repository is 13 files, and <c>tslpatchdata/</c> holds
    /// exactly two — <c>changes.ini</c> and <c>info.rtf</c>. The 713 files it
    /// installs (models, modules, 2DAs, scripts) ship only in the DeadlyStream
    /// archive.</para>
    ///
    /// <para>The failure this caused was quiet, which is why it survived: a
    /// TSLPatcher <c>changes.ini</c> both edits files already in the game and
    /// installs new ones from <c>tslpatchdata</c>. The edits applied, the file
    /// installs had nothing to copy, and HoloPatcher reported it as one warning
    /// in a long log. The install looked like it worked and K2CP was, in
    /// practice, mostly not installed.</para>
    ///
    /// <para>The repo is still where the recipe is authored, so the commit
    /// matching the pinned archive release stays recorded in
    /// <c>sources.json</c> for provenance.</para>
    ///
    /// <para>K2CP has no translation subfolders: it is English-only upstream, so
    /// non-English installs receive the bugfix strings in English while the rest
    /// of the game stays localised (same known limitation as K1CP on
    /// Italian/Spanish, here for every non-English locale).</para>
    ///
    /// <para>Still unverified: whether K2CP ships <c>.lyt</c>/<c>.vis</c> files
    /// with LF-only line endings. K1CP needed a CRLF normalisation pass (see
    /// <see cref="K1cpInstaller"/>) and the KOTOR 2 engine uses the same
    /// CRLF-assuming parser family.</para>
    /// </summary>
    public sealed class K2cpInstaller : IModInstaller
    {
        /// <summary>
        /// Folder inside the archive holding the TSLPatcher payload. The archive
        /// also carries INSTALL.exe (TSLPatcher itself) and a readme, both of
        /// which we ignore — HoloPatcher drives the payload directly.
        /// </summary>
        private const string ArchiveTslpatchdataDir = TslpatchdataZip.DirName;

        public string Id => "k2cp";
        public string DisplayName => $"KOTOR 2 Community Patch ({Config.K2cpDisplayVersion})";

        public bool IsSelected(ModSelection selection) => selection?.K2cp == true;

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

                stagingRoot = Path.Combine(Path.GetTempPath(), $"kotor_acc_k2cp_{Guid.NewGuid():N}");
                Directory.CreateDirectory(stagingRoot);
                string zipPath = Path.Combine(stagingRoot, Config.K2cpArchiveFileName);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K2cpDownloading"));
                ctx.Progress?.Invoke(0);

                // Acquisition. The pinned hash is a fast path, not a gate: a
                // mismatch on a DeadlyStream "latest file" download almost
                // always means upstream shipped an update this installer
                // predates, so the archive is judged by the structural checks
                // below instead (trust-DeadlyStream policy — see sources.json).
                // Only a failed download goes to the manual path immediately.
                string manualReason = null;
                bool hashVerified = false;
                try
                {
                    using (var ds = new DeadlyStreamClient())
                    {
                        await ds.DownloadFileAsync(Config.K2cpDownloadPageUrl, zipPath, (done, total) =>
                        {
                            long effectiveTotal = total > 0 ? total : Config.K2cpArchiveSizeBytes;
                            ctx.Progress?.Invoke((int)Math.Min(45, done * 45 / Math.Max(1, effectiveTotal)));
                        });
                    }

                    string hash = DeadlyStreamClient.ComputeSha256(zipPath);
                    hashVerified = hash.Equals(Config.K2cpArchiveSha256, StringComparison.OrdinalIgnoreCase);
                    if (!hashVerified)
                    {
                        Logger.Warning($"K2CP hash differs from the pinned {Config.K2cpDisplayVersion} " +
                                       $"(expected {Config.K2cpArchiveSha256}, got {hash}) — " +
                                       "assuming an upstream update; validating the archive structurally.");
                    }
                }
                catch (Exception ex)
                {
                    Logger.Warning($"K2CP download failed: {ex.Message}");
                    manualReason = InstallerLocale.Format("ManualDownload_Reason_Failed_Format", ex.Message);
                }

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K2cpStaging"));
                ctx.Progress?.Invoke(50);

                // Stage and structurally validate, asking the user for the
                // archive at most once: on entry when the download failed, or
                // as the retry when what we downloaded is not a usable K2CP
                // archive. A validation failure on a hash-VERIFIED archive is
                // ours (a bad pin or a layout assumption gone stale), and a
                // manual download of the same bytes cannot fix it.
                string tslpatchdataDir = null;
                bool askedManual = false;
                for (int attempt = 0; tslpatchdataDir == null; attempt++)
                {
                    if (manualReason != null)
                    {
                        if (askedManual)
                            return ModInstallResult.Fail(Id, manualReason);
                        askedManual = true;

                        string supplied = ctx.AskForManualDownload?.Invoke(new ManualDownloadRequest
                        {
                            ModDisplayName = DisplayName,
                            Reason = manualReason,
                            PageUrl = Config.K2cpDownloadPageUrl,
                            ExpectedFileName = Config.K2cpArchiveFileName,
                            Kind = ManualDownloadForm.FileKind.ZipArchive,
                            PinnedSha256 = Config.K2cpArchiveSha256,
                        });

                        if (string.IsNullOrEmpty(supplied))
                            return ModInstallResult.Fail(Id, manualReason);

                        // Copy in: staging is deleted below and must never take
                        // the user's own download with it.
                        File.Copy(supplied, zipPath, overwrite: true);
                        Logger.Info($"K2CP: using the user-supplied archive {supplied}");
                        hashVerified = DeadlyStreamClient.ComputeSha256(zipPath)
                            .Equals(Config.K2cpArchiveSha256, StringComparison.OrdinalIgnoreCase);
                        manualReason = null;
                    }

                    // Fresh subdir per attempt so a half-extracted first try
                    // cannot leak files into the payload we end up installing.
                    string attemptRoot = Path.Combine(stagingRoot, $"stage{attempt}");
                    Directory.CreateDirectory(attemptRoot);

                    string structuralError = null;
                    try
                    {
                        await Task.Run(() => TslpatchdataZip.Extract(zipPath, attemptRoot, "K2CP"));

                        string candidate = Path.Combine(attemptRoot, ArchiveTslpatchdataDir);
                        if (!Directory.Exists(candidate))
                        {
                            structuralError = $"the archive has no '{ArchiveTslpatchdataDir}' folder";
                        }
                        else
                        {
                            // The bug this class exists to prevent, made loud. A
                            // payload of changes.ini plus a readme installs almost
                            // nothing while still exiting successfully.
                            int payloadCount = Directory.GetFiles(candidate).Length;
                            if (payloadCount < 10)
                            {
                                structuralError = $"the payload has only {payloadCount} file(s) " +
                                                  "where K2CP ships hundreds";
                            }
                            else
                            {
                                tslpatchdataDir = candidate;
                                Logger.Info($"K2CP tslpatchdata staged at {candidate} ({payloadCount} files)");
                            }
                        }
                    }
                    catch (InvalidDataException ex)
                    {
                        structuralError = $"the file is not a readable zip ({ex.Message})";
                    }

                    if (tslpatchdataDir == null)
                    {
                        Logger.Warning($"K2CP archive failed structural validation: {structuralError}");
                        if (hashVerified)
                        {
                            return ModInstallResult.Fail(Id,
                                $"The K2CP archive matches its pin but failed validation: {structuralError}.");
                        }
                        manualReason = InstallerLocale.Format(
                            "ManualDownload_Reason_Failed_Format", structuralError);
                    }
                }

                if (!hashVerified)
                {
                    Logger.Warning("K2CP: installing an archive newer than the tested pin " +
                                   "(structurally valid; trust-DeadlyStream policy).");
                    ctx.StatusUpdate?.Invoke(InstallerLocale.Format(
                        "ModInstall_NewerThanTested_Format", "KOTOR 2 Community Patch"));
                }

                if (ctx.Locale != GameLocale.English)
                {
                    Logger.Info($"K2CP: game locale is {ctx.Locale}; K2CP ships English-only — " +
                                "bugfix strings will appear in English.");
                }

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K2cpApplying"));
                ctx.Progress?.Invoke(60);

                var holoResult = await HoloPatcherRunner.RunAsync(
                    ctx.HoloPatcherExePath, ctx.GameDir, tslpatchdataDir, ctx.StatusUpdate,
                    "ModInstall_K2cpProgress_Format", "ModInstall_K2cpApplyingHeartbeat_Format");

                if (!holoResult.Success)
                {
                    return ModInstallResult.Fail(Id, holoResult.Error);
                }

                ctx.Progress?.Invoke(100);
                Logger.Info("K2CP install complete (HoloPatcher exit code 0).");
                return ModInstallResult.Ok(Id);
            }
            catch (InvalidDataException ex)
            {
                Logger.Error("K2CP archive could not be read", ex);
                return ModInstallResult.Fail(Id, $"The downloaded K2CP archive is not a readable zip: {ex.Message}");
            }
            catch (Exception ex)
            {
                Logger.Error("K2CP install failed", ex);
                return ModInstallResult.Fail(Id, ex.Message);
            }
            finally
            {
                if (stagingRoot != null && Directory.Exists(stagingRoot))
                {
                    try { Directory.Delete(stagingRoot, recursive: true); }
                    catch (Exception cleanupEx)
                    {
                        Logger.Warning($"Could not clean up K2CP staging dir {stagingRoot}: {cleanupEx.Message}");
                    }
                }
            }
        }

        // The zip -> tslpatchdata extraction lives in TslpatchdataZip, shared
        // with ThematicCompanionsInstaller.
    }
}
