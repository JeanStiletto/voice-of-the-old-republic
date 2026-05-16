using System;
using System.IO;
using System.IO.Compression;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Downloads HoloPatcher from upstream GitHub at install time so we can drive
    /// TSLPatcher-style mod installs (K1CP, etc.) headlessly.
    ///
    /// Source: <see cref="Config.HoloPatcherRepositoryUrl"/> @ pinned tag
    /// <see cref="Config.HoloPatcherPinnedTag"/>. Upstream ships HoloPatcher as a
    /// zipped folder (<see cref="Config.HoloPatcherAssetName"/>); we extract the
    /// <see cref="Config.HoloPatcherExeName"/> out of it into a fresh temp dir
    /// and return that path.
    ///
    /// License: HoloPatcher is LGPL-3.0; attribution + a link back to upstream
    /// is required. See README and the installer's "About" panel.
    /// </summary>
    public static class HoloPatcherProvider
    {
        /// <summary>
        /// Downloads + extracts HoloPatcher.exe to a fresh temp directory and
        /// returns its path. Returns null + logs a warning on failure (the
        /// caller surfaces this as a per-mod install failure in the summary).
        /// </summary>
        public static async Task<string> DownloadAsync(GitHubClient github, Action<int> progress = null)
        {
            if (github == null) throw new ArgumentNullException(nameof(github));

            string downloadedZip = null;
            try
            {
                Logger.Info($"HoloPatcher source: {Config.HoloPatcherRepositoryUrl} @ {Config.HoloPatcherPinnedTag} " +
                            $"({Config.HoloPatcherDisplayVersion})");

                downloadedZip = await github.DownloadReleaseAssetByTagAsync(
                    Config.HoloPatcherRepositoryUrl,
                    Config.HoloPatcherPinnedTag,
                    Config.HoloPatcherAssetName,
                    progress);

                string targetDir = Path.Combine(Path.GetTempPath(), $"kotor_acc_holopatcher_{Guid.NewGuid():N}");
                Directory.CreateDirectory(targetDir);

                string extractedExe = await Task.Run(() => ExtractHoloPatcherExe(downloadedZip, targetDir));
                Logger.Info($"HoloPatcher staged at: {extractedExe}");
                return extractedExe;
            }
            catch (Exception ex)
            {
                Logger.Warning($"HoloPatcher download failed: {ex.Message}. " +
                               "TSLPatcher-based mods (K1CP, etc.) will fail until the asset is reachable.");
                return null;
            }
            finally
            {
                if (downloadedZip != null)
                {
                    try { File.Delete(downloadedZip); } catch { /* best-effort */ }
                }
            }
        }

        private static string ExtractHoloPatcherExe(string zipPath, string targetDir)
        {
            using var archive = ZipFile.OpenRead(zipPath);
            var entry = archive.GetEntry(Config.HoloPatcherExePathInsideZip)
                ?? throw new InvalidOperationException(
                    $"HoloPatcher.exe not found inside {Path.GetFileName(zipPath)} " +
                    $"at expected path '{Config.HoloPatcherExePathInsideZip}'. " +
                    "Upstream zip layout may have changed — bump Config.HoloPatcherExePathInsideZip.");

            string targetPath = Path.Combine(targetDir, Config.HoloPatcherExeName);
            entry.ExtractToFile(targetPath, overwrite: true);
            return targetPath;
        }

        public static void Cleanup(string holoPatcherExePath)
        {
            if (string.IsNullOrEmpty(holoPatcherExePath)) return;
            try
            {
                string dir = Path.GetDirectoryName(holoPatcherExePath);
                if (Directory.Exists(dir))
                {
                    Directory.Delete(dir, recursive: true);
                    Logger.Info($"Cleaned up HoloPatcher temp dir: {dir}");
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not clean up HoloPatcher temp dir: {ex.Message}");
            }
        }
    }
}
