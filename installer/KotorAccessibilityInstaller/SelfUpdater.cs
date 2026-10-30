using System;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller
{
    /// <summary>What a self-update would install.</summary>
    public class SelfUpdateInfo
    {
        /// <summary>Version offered online, e.g. "0.8.0".</summary>
        public string Version;

        /// <summary>Version of the EXE currently running, e.g. "0.7.9".</summary>
        public string RunningVersion;

        /// <summary>Lower-case hex SHA-256 the published EXE must hash to.</summary>
        public string Sha256;

        /// <summary>Published size in bytes, or 0 when the file did not say.</summary>
        public long SizeBytes;

        /// <summary>Rolling releases/latest/download URL of the new EXE.</summary>
        public string DownloadUrl;

        /// <summary>Published size in whole megabytes, for the prompt.</summary>
        public int SizeMb => SizeBytes > 0 ? (int)Math.Round(SizeBytes / 1024.0 / 1024.0) : 0;
    }

    /// <summary>
    /// Keeps the installer itself current, before it installs anything.
    ///
    /// <para>WHY THIS MATTERS MORE HERE THAN FOR A PURE DOWNLOADER. The .kpatch
    /// is always fetched from the newest release, so an old installer still
    /// installs the newest mod. Everything else it lays down comes out of its own
    /// EXE — KotorPatcher.dll, prism.dll, the dinput8 proxy, the VC runtime,
    /// HoloPatcher, the address database, the Override samples and 2DAs. Running
    /// last release's installer therefore pairs a current .kpatch with stale
    /// bundled binaries, and that mismatch is not something the user can see or
    /// report usefully.</para>
    ///
    /// <para>HOW THE SWAP WORKS. The verified download is staged in %TEMP%, then
    /// swapped into the running EXE's own name: the running file is renamed
    /// aside to "&lt;name&gt;.old" (Windows allows renaming a running image, not
    /// overwriting it), the staged file is moved into its place, and that is
    /// started. The new instance deletes the ".old" once the old one has exited.
    /// This is the rename-and-replace convention single-EXE tools use (rustup,
    /// Sku's updater).</para>
    ///
    /// <para>It replaces rather than merely starting the staged copy because
    /// this EXE has no fixed home — Downloads, the game folder (the uninstaller
    /// copy), wherever the user put it — and the file the user launches IS the
    /// installer to them. Starting a %TEMP% copy and leaving the original alone
    /// meant the next launch was the old version again: it swept the staged
    /// copy, offered the same 88 MB update, and the installer looked to the
    /// user as if it never updated at all (the "not updating to 0.9" report).
    /// When the swap cannot be made — a read-only folder, antivirus holding a
    /// lock — the staged copy is started instead, and it says plainly that the
    /// file the user launched is still the older version.</para>
    ///
    /// <para>WHAT IT REFUSES TO DO. It never runs an unverified binary: the
    /// published SHA-256 must match and the downloaded file's own version
    /// resource must be newer than ours. It never downgrades, and it never
    /// blocks — offline, GitHub down, hash mismatch, antivirus eating the file,
    /// user says no: every path falls through to a normal run with the EXE that
    /// is already here. An installer that cannot update ITSELF must still be able
    /// to install the mod.</para>
    ///
    /// <para>The version is published as a tiny key=value asset next to the EXE
    /// on the same rolling releases/latest/download URL (written by
    /// installer/release.ps1). That is a plain github.com download, NOT
    /// api.github.com, so it cannot hit the 60-request/hour rate limit this
    /// project avoids everywhere else.</para>
    /// </summary>
    public static class SelfUpdater
    {
        /// <summary>Command-line switch that suppresses the whole check.</summary>
        public const string SkipSwitch = "--no-self-update";

        /// <summary>Command-line switch the restarted instance is launched with.</summary>
        public const string UpdatedSwitch = "--self-updated";

        /// <summary>
        /// Staged downloads are named "&lt;prefix&gt;&lt;version&gt;.exe" in %TEMP%.
        /// Deliberately NOT the published asset name: the in-game F5 updater
        /// writes %TEMP%\VoiceOfTheOldRepublicInstaller.exe, and the two must not
        /// fight over one path.
        /// </summary>
        private const string StagedPrefix = "VoiceOfTheOldRepublicInstaller-";

        /// <summary>
        /// Suffix the replaced EXE is renamed to, next to the new one. The new
        /// instance deletes it once the old process has let go of it.
        /// </summary>
        private const string AsideSuffix = ".old";

        /// <summary>
        /// How long a freshly swapped-in instance waits for the old process to
        /// exit so it can delete the ".old" file. The old process returns right
        /// after starting us and only flushes its log on the way out, so this is
        /// normally well under a second; past the limit the file is left for the
        /// next run's sweep.
        /// </summary>
        private static readonly TimeSpan AsideDeleteWait = TimeSpan.FromSeconds(5);

        /// <summary>
        /// Set on the run that follows a successful self-update, so the user can
        /// be told what happened. A restart with no explanation is exactly the
        /// kind of thing a screen-reader user cannot see the cause of.
        /// </summary>
        public static string PostUpdateNotice { get; private set; }

        /// <summary>Version of the running EXE, e.g. "0.8.0".</summary>
        public static string RunningVersion => InstallerVersion.Number();

        private static string RunningExe => Environment.ProcessPath ?? string.Empty;

        /// <summary>
        /// Housekeeping for every run: note a completed self-update, drop the
        /// ".old" EXE a swap left next to us, and drop staged EXEs left in %TEMP%
        /// by earlier runs. Also runs when NO update happened, so a download that
        /// was cancelled or died halfway does not leave 85 MB behind forever.
        /// </summary>
        public static void CleanUp(string[] args)
        {
            bool justUpdated = HasSwitch(args, UpdatedSwitch);
            if (justUpdated)
            {
                // Started from %TEMP% means the swap fell back to the staged
                // copy: the file the user launched was NOT replaced, and saying
                // "updated" without that caveat is how the next launch of the
                // old file came as a surprise.
                bool ranFromStaging = IsStagedPath(RunningExe);
                PostUpdateNotice = InstallerLocale.Format(
                    ranFromStaging ? "SelfUpdate_DoneStaged_Format" : "SelfUpdate_Done_Format", RunningVersion);
                Logger.Info($"Restarted after a self-update; now running {RunningVersion} from " +
                            $"{(ranFromStaging ? "the staged copy (original file not replaced)" : "the original location")}.");
            }

            // Only a fresh restart waits for the old process; any other run
            // just tries once and leaves a still-locked file for later.
            DeleteAsideFile(justUpdated ? AsideDeleteWait : TimeSpan.Zero);

            try
            {
                string running = RunningExe;
                foreach (string file in Directory.GetFiles(Path.GetTempPath(), StagedPrefix + "*.exe"))
                {
                    // Never the one we are executing from: after a self-update
                    // that is exactly this file, and it is in use anyway.
                    if (string.Equals(file, running, StringComparison.OrdinalIgnoreCase)) continue;
                    try
                    {
                        File.Delete(file);
                        Logger.Info($"Removed a staged installer download: {file}");
                    }
                    catch (Exception ex)
                    {
                        Logger.Info($"Staged installer {Path.GetFileName(file)} still in use ({ex.Message}); left in place.");
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not sweep staged installer downloads: {ex.Message}");
            }
        }

        /// <summary>
        /// Whether a self-update check makes sense for this launch at all.
        ///
        /// <para>Skipped for every non-interactive mode. <c>--auto-update</c> is
        /// the in-game F5 handoff, which just downloaded this EXE fresh from the
        /// newest release — there is nothing newer to find, and a dialog would
        /// stall a headless run the player is waiting on. <c>/uninstall</c> and
        /// <c>/quiet</c> are the Add/Remove Programs paths: removing the mod must
        /// not depend on a download, and a quiet run has nobody to ask.</para>
        /// </summary>
        public static bool ShouldCheck(string[] args, bool uninstallMode, bool autoUpdateMode, bool quietMode)
        {
            if (HasSwitch(args, SkipSwitch))
            {
                Logger.Info($"Self-update check suppressed by {SkipSwitch}.");
                return false;
            }

            if (HasSwitch(args, UpdatedSwitch))
                return false;   // we ARE the update; do not immediately look again

            if (uninstallMode || autoUpdateMode || quietMode)
            {
                Logger.Info("Self-update check skipped: non-interactive mode " +
                            $"(uninstall={uninstallMode}, auto-update={autoUpdateMode}, quiet={quietMode}).");
                return false;
            }

            return true;
        }

        /// <summary>
        /// Asks GitHub what the newest installer is. Returns null when there is
        /// nothing to do — no newer version, no published hash, or any failure at
        /// all. Never throws.
        ///
        /// <para><paramref name="runningVersionOverride"/> exists so a dev build
        /// can pose as an older one: the interesting path is the one where an
        /// update IS offered, and on a freshly built EXE that path is otherwise
        /// unreachable.</para>
        /// </summary>
        public static SelfUpdateInfo Check(string runningVersionOverride = null)
        {
            try
            {
                string url = GitHubClient.BuildLatestDownloadUrl(Config.ModRepositoryUrl, Config.InstallerVersionAssetName);
                string text;
                using (var github = new GitHubClient())
                    text = github.DownloadTextAsync(url, TimeSpan.FromSeconds(15))
                                 .GetAwaiter().GetResult();

                if (string.IsNullOrEmpty(text))
                {
                    Logger.Info("No installer version file published; skipping the self-update check.");
                    return null;
                }

                ParseVersionFile(text, out string version, out string sha, out long size);

                if (string.IsNullOrEmpty(version))
                {
                    Logger.Warning($"{Config.InstallerVersionAssetName} carries no version; skipping the self-update.");
                    return null;
                }

                string running = string.IsNullOrEmpty(runningVersionOverride)
                    ? RunningVersion : runningVersionOverride;
                if (!Program.IsNewerVersion(version, running))
                {
                    Logger.Info($"Installer {running} is current (newest published: {version}).");
                    return null;
                }

                if (string.IsNullOrEmpty(sha) || sha.Length != 64)
                {
                    // Deliberately fatal to the update, not merely noted. We are
                    // about to download and RUN an executable; without the hash
                    // there is nothing to check it against.
                    Logger.Warning($"Installer {version} is published without a usable sha256; refusing to self-update.");
                    return null;
                }

                Logger.Info($"Installer self-update available: {running} -> {version} ({size} bytes).");
                return new SelfUpdateInfo
                {
                    Version = version,
                    RunningVersion = running,
                    Sha256 = sha,
                    SizeBytes = size,
                    DownloadUrl = GitHubClient.BuildLatestDownloadUrl(Config.ModRepositoryUrl,
                                                                     Config.InstallerAssetName),
                };
            }
            catch (Exception ex)
            {
                Logger.Warning($"Self-update check failed ({ex.Message}); continuing with the current version.");
                return null;
            }
        }

        /// <summary>
        /// Reads the lines installer/release.ps1 writes:
        /// <code>
        /// version=0.8.0
        /// sha256=&lt;64 hex&gt;
        /// size=87961850
        /// </code>
        /// Unknown keys, blank lines and # comments are ignored, so the format can
        /// gain a field later without an older installer choking on it. Line
        /// endings are not assumed: the file is written on Windows and served by
        /// GitHub, and a stray CR must not end up inside the hash.
        /// </summary>
        internal static void ParseVersionFile(string text, out string version, out string sha, out long size)
        {
            version = null;
            sha = null;
            size = 0;
            if (text == null) return;

            // A byte-order mark would otherwise glue itself to the first key and
            // turn "version" into something no comparison matches — a failure
            // that looks exactly like "no update published".
            text = text.TrimStart('\uFEFF');

            foreach (var raw in text.Split('\n'))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("#")) continue;
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                string key = line.Substring(0, eq).Trim().ToLowerInvariant();
                string val = line.Substring(eq + 1).Trim();
                if (key == "version") version = val;
                else if (key == "sha256") sha = val.ToLowerInvariant();
                else if (key == "size") long.TryParse(val, out size);
            }
        }

        /// <summary>
        /// Downloads the new EXE into %TEMP% and verifies it. Returns the staged
        /// path. Throws on any failure, having cleaned up the partial file — the
        /// caller reports it and carries on with the current version.
        /// </summary>
        public static async Task<string> DownloadAsync(SelfUpdateInfo info,
                                                       Action<int> percent,
                                                       CancellationToken cancel)
        {
            string staged = Path.Combine(Path.GetTempPath(), StagedPrefix + info.Version + ".exe");
            TryDelete(staged);

            try
            {
                using (var github = new GitHubClient())
                    await github.DownloadToPathAsync(info.DownloadUrl, staged, percent, cancel);

                VerifyStagedFile(staged, info);
                return staged;
            }
            catch
            {
                TryDelete(staged);
                throw;
            }
        }

        /// <summary>
        /// Hash and version resource must both check out before the file is
        /// allowed to replace this process.
        /// </summary>
        private static void VerifyStagedFile(string staged, SelfUpdateInfo info)
        {
            string actual = Sha256OfFile(staged);
            if (!string.Equals(actual, info.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                Logger.Error($"Self-update hash mismatch: expected {info.Sha256}, got {actual}.");
                throw new InvalidDataException(InstallerLocale.Get("SelfUpdate_BadHash"));
            }

            string fileVersion;
            try { fileVersion = FileVersionInfo.GetVersionInfo(staged).FileVersion; }
            catch (Exception ex) { throw new InvalidDataException($"No version information: {ex.Message}"); }

            if (string.IsNullOrEmpty(fileVersion))
                throw new InvalidDataException("The downloaded file carries no version information.");

            // Only that it is genuinely newer than us. The announced number is
            // not re-derived from the resource: the resource carries four
            // components (0.8.0.0) and the published number three, and the
            // comparison treats the missing one as zero anyway.
            if (!Program.IsNewerVersion(fileVersion, info.RunningVersion))
                throw new InvalidDataException(
                    $"The downloaded file reports version {fileVersion}, which is not newer than {info.RunningVersion}.");

            Logger.Info($"Staged installer verified: {fileVersion}, sha256 ok.");
        }

        /// <summary>
        /// Swaps the verified staged EXE into the running EXE's place and starts
        /// it; when the swap cannot be made, starts the staged copy instead. The
        /// caller returns straight afterwards. See the class summary for why the
        /// file is replaced rather than left alone.
        /// </summary>
        public static void ReplaceAndLaunch(string staged, string[] args)
        {
            string target = RunningExe;
            Launch(TryReplaceInPlace(staged, target) ? target : staged, args);
        }

        /// <summary>
        /// Renames the running EXE aside and moves the staged one into its name.
        /// Returns false — with the original back in place wherever possible —
        /// when either step fails; the caller then starts the staged copy.
        /// </summary>
        private static bool TryReplaceInPlace(string staged, string target)
        {
            if (string.IsNullOrEmpty(target) || !File.Exists(target) || IsStagedPath(target))
                return false;

            string aside = target + AsideSuffix;
            try
            {
                if (File.Exists(aside)) File.Delete(aside);
                File.Move(target, aside);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not move the running installer aside ({ex.Message}); " +
                               $"starting the update from {staged} instead.");
                return false;
            }

            try
            {
                File.Move(staged, target);
                Logger.Info($"Replaced {target} with the verified update; the previous version is {aside} " +
                            "until the new one deletes it.");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not move the update into {target} ({ex.Message}); " +
                               $"restoring the original and starting the update from {staged} instead.");
                // A cross-volume move that died halfway leaves a partial file
                // in the way of the restore.
                TryDelete(target);
                try { File.Move(aside, target); }
                catch (Exception restoreEx)
                {
                    Logger.Error($"Could not restore the original installer from {aside}", restoreEx);
                }
                return false;
            }
        }

        /// <summary>
        /// Deletes the ".old" EXE next to us, retrying for up to
        /// <paramref name="wait"/> while the process that left it is still
        /// exiting.
        /// </summary>
        private static void DeleteAsideFile(TimeSpan wait)
        {
            string running = RunningExe;
            if (string.IsNullOrEmpty(running)) return;
            string aside = running + AsideSuffix;
            if (!File.Exists(aside)) return;

            var deadline = DateTime.UtcNow + wait;
            while (true)
            {
                try
                {
                    File.Delete(aside);
                    Logger.Info($"Removed the replaced installer: {aside}");
                    return;
                }
                catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
                {
                    if (DateTime.UtcNow >= deadline)
                    {
                        Logger.Info($"Replaced installer {Path.GetFileName(aside)} still in use ({ex.Message}); left for the next run.");
                        return;
                    }
                    Thread.Sleep(250);
                }
            }
        }

        private static bool IsStagedPath(string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            string dir = Path.GetDirectoryName(Path.GetFullPath(path)) ?? string.Empty;
            string temp = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar);
            return string.Equals(dir.TrimEnd(Path.DirectorySeparatorChar), temp, StringComparison.OrdinalIgnoreCase)
                && Path.GetFileName(path).StartsWith(StagedPrefix, StringComparison.OrdinalIgnoreCase);
        }

        /// <summary>
        /// Starts the given EXE and hands it this run's arguments, so a launch
        /// that carried a game path or a --game keeps carrying it across the
        /// restart.
        /// </summary>
        private static void Launch(string exe, string[] args)
        {
            var forwarded = new StringBuilder();
            foreach (var a in args ?? new string[0])
            {
                if (IsSwitch(a, SkipSwitch) || IsSwitch(a, UpdatedSwitch)) continue;
                forwarded.Append(Quote(a)).Append(' ');
            }
            forwarded.Append(UpdatedSwitch);

            Logger.Info($"Starting the updated installer: {exe} {forwarded}");
            Process.Start(new ProcessStartInfo
            {
                FileName = exe,
                Arguments = forwarded.ToString(),
                UseShellExecute = true,     // keeps the elevation we already hold
                WorkingDirectory = Path.GetDirectoryName(exe) ?? string.Empty,
            });
        }

        private static string Quote(string arg) =>
            arg != null && arg.IndexOf(' ') >= 0 && !arg.StartsWith("\"") ? "\"" + arg + "\"" : arg;

        private static bool TryDelete(string path)
        {
            try
            {
                if (path != null && File.Exists(path)) File.Delete(path);
                return true;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not delete {path}: {ex.Message}");
                return false;
            }
        }

        private static string Sha256OfFile(string path)
        {
            using (var sha = SHA256.Create())
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                byte[] hash = sha.ComputeHash(fs);
                var sb = new StringBuilder(hash.Length * 2);
                foreach (byte b in hash) sb.Append(b.ToString("x2"));
                return sb.ToString();
            }
        }

        private static bool HasSwitch(string[] args, string name)
        {
            if (args == null) return false;
            foreach (var a in args)
                if (IsSwitch(a, name)) return true;
            return false;
        }

        private static bool IsSwitch(string arg, string name) =>
            string.Equals(arg, name, StringComparison.OrdinalIgnoreCase);
    }
}
