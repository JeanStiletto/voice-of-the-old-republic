using System;
using System.IO;
using System.Reflection;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Provides HoloPatcher, which drives TSLPatcher-style mod installs (K1CP,
    /// K2CP, the Tweak Pack) headlessly.
    ///
    /// <para><b>Bundled, not downloaded.</b> It used to be fetched from a pinned
    /// GitHub release at install time, which made it the single most fragile
    /// dependency in the whole pipeline — and the only one with no manual
    /// escape hatch. Every other third-party payload can be downloaded by hand
    /// if automation fails; HoloPatcher cannot, because the user never sees it.
    /// If that release ever became unreachable, NO TSLPatcher-based mod would
    /// install, for either game. Worse, we were already on a fallback: the
    /// canonical PyKotor repo re-tagged without attaching binaries, so the pin
    /// pointed at NickHugi's fork.</para>
    ///
    /// <para>Embedding it costs ~11 MB on a ~77 MB installer and removes the
    /// last network dependency from the mod pipeline. Version is whatever was
    /// vendored into <c>Resources/HoloPatcher.exe</c>; bumping it means
    /// replacing that file, which is a deliberate, reviewable act rather than
    /// an upstream change we inherit silently.</para>
    ///
    /// <para>NOTE — must extract outside of <c>%TEMP%</c>:</para>
    /// HoloPatcher v1.60-patcher-beta4 has an explicit guard
    /// (<c>is_running_from_temp</c> in <c>holopatcher/__main__.py</c>) that
    /// rejects the run if <c>sys.executable</c> starts with
    /// <c>tempfile.gettempdir()</c> — i.e. <c>%LOCALAPPDATA%\Temp</c> on Windows.
    /// We stage under <c>%LOCALAPPDATA%\KotorAccessibility\holopatcher\</c>
    /// instead so the guard passes.
    ///
    /// <para>License: the vendored build is HoloPatcher v1.60-patcher-beta4 from
    /// <c>NickHugi/PyKotor</c>, GPL-3.0 (the LICENSE file at that tag is the
    /// plain GPL v3 text — GitHub's repo-level "LGPL-3.0" label describes the
    /// repository's current state, not this tag). Same licence as this project,
    /// and we invoke it as a separate process rather than linking it, so this
    /// is mere aggregation. The licence text ships as
    /// <c>Resources/HoloPatcher-License.txt</c> and the source stays available
    /// at the upstream repo — see <see cref="Config.HoloPatcherRepositoryUrl"/>
    /// and <see cref="Config.HoloPatcherPinnedTag"/>, kept for attribution.</para>
    /// </summary>
    public static class HoloPatcherProvider
    {
        /// <summary>
        /// Staging root for HoloPatcher.exe. Deliberately outside
        /// <c>tempfile.gettempdir()</c> to satisfy HoloPatcher's "no temp dir"
        /// guard (see class docs).
        /// </summary>
        private static string StagingRoot =>
            Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "KotorAccessibility",
                "holopatcher");

        /// <summary>
        /// Writes the bundled HoloPatcher.exe to a fresh staging directory and
        /// returns its path. Returns null + logs a warning on failure (the
        /// caller surfaces this as a per-mod install failure in the summary).
        ///
        /// <para>The <paramref name="progress"/> callback is invoked once at
        /// completion. Extraction of an embedded resource is fast enough that
        /// incremental reporting would only produce a burst of announcements a
        /// screen reader has to read out.</para>
        /// </summary>
        public static async Task<string> ProvideAsync(Action<int> progress = null)
        {
            try
            {
                Logger.Info($"HoloPatcher: using the bundled copy " +
                            $"({Config.HoloPatcherDisplayVersion}, from {Config.HoloPatcherRepositoryUrl} " +
                            $"@ {Config.HoloPatcherPinnedTag})");

                string targetDir = Path.Combine(StagingRoot, Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(targetDir);
                string targetPath = Path.Combine(targetDir, Config.HoloPatcherExeName);

                await Task.Run(() => ExtractBundledExe(targetPath));

                progress?.Invoke(100);
                Logger.Info($"HoloPatcher staged at: {targetPath}");
                return targetPath;
            }
            catch (Exception ex)
            {
                Logger.Warning($"HoloPatcher could not be staged: {ex.Message}. " +
                               "TSLPatcher-based mods (K1CP, K2CP, Tweak Pack) will be reported as failed.");
                return null;
            }
        }

        private static void ExtractBundledExe(string targetPath)
        {
            var assembly = Assembly.GetExecutingAssembly();
            string resourceName = null;
            foreach (var name in assembly.GetManifestResourceNames())
            {
                if (name.EndsWith(Config.HoloPatcherExeName, StringComparison.OrdinalIgnoreCase))
                {
                    resourceName = name;
                    break;
                }
            }
            if (resourceName == null)
                throw new FileNotFoundException(
                    $"Embedded resource not found: {Config.HoloPatcherExeName}");

            using var stream = assembly.GetManifestResourceStream(resourceName)
                ?? throw new InvalidOperationException($"Could not open resource stream: {resourceName}");
            using var file = new FileStream(targetPath, FileMode.Create, FileAccess.Write, FileShare.None);
            stream.CopyTo(file);
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
