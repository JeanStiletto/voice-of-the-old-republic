using System;
using System.IO;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Installs Thematic Companions (Sniggles &amp; JCarter426) — one sibling mod
    /// per game, driven by the same class because they differ only in their pin
    /// and their install-option index.
    ///
    /// <para><b>What it changes.</b> Companion attributes, skills, feats, powers
    /// and starting equipment, so each companion carries level-appropriate
    /// bonuses instead of the stat-lines KOTOR 2 inherited wholesale from KOTOR
    /// 1. Both mods are pure GFF field edits on <c>p_*.utc</c>: their
    /// <c>changes.ini</c> has an empty <c>[TLKList]</c>, <c>[InstallList]</c>,
    /// <c>[2DAList]</c> and <c>[CompileList]</c>. Nothing is written to
    /// <c>dialog.tlk</c> and no scripts are compiled, which is why this installs
    /// identically on every locale — the one bundled mod with no English
    /// bleed-through caveat at all.</para>
    ///
    /// <para><b>Why opt-in and off by default.</b> It is the only bundled mod
    /// that changes game balance rather than fixing something broken, and a
    /// strict-vanilla baseline is itself an accessibility feature: it keeps a
    /// player's experience comparable to every guide and forum answer written
    /// about the game. Both mod-selection screens offer it unchecked, per game,
    /// so the KOTOR 1 and KOTOR 2 decisions stay independent.</para>
    ///
    /// <para><b>Ordering.</b> Runs last in both pipelines, after K1CP / K2CP.
    /// TSLPatcher GFF edits are field-level, so landing last means the community
    /// patches' fixes to the same creature files are preserved and only the
    /// specific stat fields this mod names are overwritten.</para>
    ///
    /// <para><b>Acquisition.</b> The authors publish GitHub releases, so there is
    /// no DeadlyStream scrape and nothing is redistributed: we fetch the
    /// author's own asset at a pinned tag and verify its hash. The mod is
    /// CC BY-NC licensed, which download-at-install-time sidesteps entirely.
    /// There is deliberately no manual-download fallback (same call as K1CP):
    /// GitHub being unreachable is rare, and an optional balance mod is not
    /// worth an extra dialog in the middle of an install.</para>
    /// </summary>
    public sealed class ThematicCompanionsInstaller : IModInstaller
    {
        private readonly GameTarget _game;

        private ThematicCompanionsInstaller(GameTarget game) => _game = game;

        public static ThematicCompanionsInstaller ForKotor1() => new(GameTarget.Kotor1);
        public static ThematicCompanionsInstaller ForKotor2() => new(GameTarget.Kotor2);

        private SourcePins.ReleasePin Pin =>
            _game == GameTarget.Kotor1 ? Config.ThematicCompanionsK1 : Config.ThematicCompanionsK2;

        /// <summary>
        /// Which <c>namespaces.ini</c> entry to install, or null when the mod has
        /// none. The KOTOR 1 mod ships a bare <c>changes.ini</c>. The KOTOR 2 one
        /// offers two options and we take index 0, "Standard" — stats and
        /// starting equipment. Index 1 additionally rewrites one late-game
        /// encounter to be markedly harder, which is not something to hand a
        /// blind first-time player without asking.
        /// </summary>
        private int? NamespaceOptionIndex => _game == GameTarget.Kotor1 ? null : 0;

        public string Id => _game == GameTarget.Kotor1
            ? "thematic-companions-k1"
            : "thematic-companions-k2";

        public string DisplayName => _game == GameTarget.Kotor1
            ? $"Thematic KOTOR Companions ({Pin?.DisplayVersion})"
            : $"Thematic KOTOR 2 Companions ({Pin?.DisplayVersion})";

        public bool IsSelected(ModSelection selection) => _game == GameTarget.Kotor1
            ? selection?.ThematicCompanionsK1 == true
            : selection?.ThematicCompanionsK2 == true;

        public async Task<ModInstallResult> InstallAsync(ModInstallContext ctx)
        {
            string stagingRoot = null;
            string downloadPath = null;

            try
            {
                if (string.IsNullOrEmpty(ctx.HoloPatcherExePath) || !File.Exists(ctx.HoloPatcherExePath))
                {
                    return ModInstallResult.Fail(Id,
                        "HoloPatcher.exe not available. Drop the binary at " +
                        "installer/KotorAccessibilityInstaller/Resources/HoloPatcher.exe and rebuild.");
                }

                var pin = Pin;
                if (pin == null)
                {
                    // Only reachable if sources.json lost the entry, which the
                    // embedded-copy fallback in SourcePins is designed to prevent.
                    return ModInstallResult.Fail(Id,
                        $"No source pin for {Id} in sources.json; skipping rather than guessing a version.");
                }

                stagingRoot = Path.Combine(Path.GetTempPath(), $"kotor_acc_{Id}_{Guid.NewGuid():N}");
                Directory.CreateDirectory(stagingRoot);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_ThematicDownloading"));
                ctx.Progress?.Invoke(0);

                using (var gh = new GitHubClient())
                {
                    downloadPath = await gh.DownloadReleaseAssetByTagAsync(
                        pin.RepoUrl, pin.Tag, pin.AssetName,
                        p => ctx.Progress?.Invoke(Math.Min(45, p * 45 / 100)));
                }

                // Fail-closed on a hash mismatch. A tagged release asset should
                // never change; if it has, something is wrong enough that
                // silently installing it is the wrong response, and the mod is
                // optional so skipping it costs the user nothing but the mod.
                string hash = DeadlyStreamClient.ComputeSha256(downloadPath);
                if (!hash.Equals(pin.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    Logger.Warning($"{Id}: hash mismatch — expected {pin.Sha256}, got {hash}");
                    return ModInstallResult.Fail(Id,
                        $"The downloaded {DisplayName} archive does not match its pinned checksum; " +
                        "it was not installed. Re-verify and bump the pin in sources.json.");
                }

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_ThematicStaging"));
                ctx.Progress?.Invoke(50);

                string tslpatchdataDir = Path.Combine(stagingRoot, TslpatchdataZip.DirName);
                string zipPath = downloadPath;
                await Task.Run(() => TslpatchdataZip.Extract(zipPath, stagingRoot, Id));

                if (!Directory.Exists(tslpatchdataDir))
                {
                    return ModInstallResult.Fail(Id,
                        $"The {DisplayName} archive has no '{TslpatchdataZip.DirName}' folder — its layout has changed.");
                }

                // The payload is a handful of .utc files plus changes.ini. A
                // near-empty folder would still let HoloPatcher exit 0 having
                // done almost nothing (the K2CP trap), so check before running.
                int payloadCount = Directory.GetFiles(tslpatchdataDir).Length;
                Logger.Info($"{Id}: tslpatchdata staged at {tslpatchdataDir} ({payloadCount} files)");
                if (payloadCount < 3)
                {
                    return ModInstallResult.Fail(Id,
                        $"The {DisplayName} payload has only {payloadCount} file(s); " +
                        "refusing to run a patch that would install almost nothing.");
                }

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_ThematicApplying"));
                ctx.Progress?.Invoke(60);

                var holoResult = await HoloPatcherRunner.RunAsync(
                    ctx.HoloPatcherExePath, ctx.GameDir, tslpatchdataDir, ctx.StatusUpdate,
                    "ModInstall_ThematicProgress_Format", "ModInstall_ThematicApplyingHeartbeat_Format",
                    NamespaceOptionIndex);

                if (!holoResult.Success)
                {
                    return ModInstallResult.Fail(Id, holoResult.Error);
                }

                ctx.Progress?.Invoke(100);
                Logger.Info($"{Id}: install complete (HoloPatcher exit code 0). " +
                            "Note: takes full effect on a NEW game only.");
                return ModInstallResult.Ok(Id);
            }
            catch (InvalidDataException ex)
            {
                Logger.Error($"{Id}: archive could not be read", ex);
                return ModInstallResult.Fail(Id, $"The downloaded archive is not a readable zip: {ex.Message}");
            }
            catch (Exception ex)
            {
                Logger.Error($"{Id}: install failed", ex);
                return ModInstallResult.Fail(Id, ex.Message);
            }
            finally
            {
                // GitHubClient downloads into the system temp dir, not our
                // staging root, so it needs its own cleanup.
                if (downloadPath != null && File.Exists(downloadPath))
                {
                    try { File.Delete(downloadPath); }
                    catch (Exception cleanupEx)
                    {
                        Logger.Warning($"{Id}: could not delete {downloadPath}: {cleanupEx.Message}");
                    }
                }

                if (stagingRoot != null && Directory.Exists(stagingRoot))
                {
                    try { Directory.Delete(stagingRoot, recursive: true); }
                    catch (Exception cleanupEx)
                    {
                        Logger.Warning($"{Id}: could not clean up staging dir {stagingRoot}: {cleanupEx.Message}");
                    }
                }
            }
        }
    }
}
