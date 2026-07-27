using System;
using System.IO;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Installs KOTOR 2 Community Patch (K2CP) from the GitHub repo at the
    /// pinned commit SHA in <see cref="Config.K2cpPinnedRef"/>.
    ///
    /// Same GitHub org and same tslpatchdata layout as K1CP, with two
    /// differences that make K2CP the easier of the pair:
    /// <list type="bullet">
    ///   <item>No <c>.gitattributes export-ignore</c> on tslpatchdata (verified
    ///         2026-07-27), so the tree-API + raw.githubusercontent.com fetch is
    ///         belt-and-braces rather than a necessity — we keep it anyway so
    ///         both community patches share one download path.</item>
    ///   <item>No translation subfolders at all: K2CP is English-only upstream.
    ///         Non-English installs receive the bugfix strings in English while
    ///         the rest of the game stays localised (same known limitation as
    ///         K1CP on Italian/Spanish, here for every non-English locale).</item>
    /// </list>
    ///
    /// NOT yet part of any pipeline the installer runs. Two prerequisites gate
    /// activation (see docs/installer.md, "KOTOR 2 mod bundle"):
    /// <list type="number">
    ///   <item>Install-order: community consensus is TSLRCM first, K2CP second.
    ///         TSLRCM has no auto-download path yet, so before invoking this
    ///         installer the KOTOR 2 flow must detect an existing TSLRCM install
    ///         (detection method TBD) or the user must confirm they installed it —
    ///         otherwise we would bake in the wrong order.</item>
    ///   <item>Check K2CP's <c>.lyt</c>/<c>.vis</c> files for LF-only line
    ///         endings before activation. K1CP needed a CRLF normalization pass
    ///         (see <see cref="K1cpInstaller"/>); the K2 engine uses the same
    ///         CRLF-assuming parser family, but whether K2CP ships offending
    ///         files is unverified.</item>
    /// </list>
    /// </summary>
    public sealed class K2cpInstaller : IModInstaller
    {
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
                string tslpatchdataDir = Path.Combine(stagingRoot, "tslpatchdata");
                Directory.CreateDirectory(tslpatchdataDir);

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K2cpDownloading"));
                ctx.Progress?.Invoke(0);

                using var gh = new GitHubClient();
                await GitHubTslpatchdataFetcher.FetchAsync(
                    gh,
                    Config.K2cpRepoOwner,
                    Config.K2cpRepoName,
                    Config.K2cpPinnedRef,
                    tslpatchdataDir,
                    p => ctx.Progress?.Invoke(p * 55 / 100), // 0..55 covers download
                    logPrefix: "K2CP");

                ctx.StatusUpdate?.Invoke(InstallerLocale.Get("ModInstall_K2cpStaging"));
                ctx.Progress?.Invoke(55);
                Logger.Info($"K2CP tslpatchdata staged at: {tslpatchdataDir}");

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
    }
}
