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
    /// <para>HOW THE SWAP WORKS — and why it is not Sku's swap. Sku's updater
    /// renames itself aside and moves the download into its own name, because it
    /// lives at one fixed path that a Start-menu shortcut points at. This EXE has
    /// no fixed home: it is run out of Downloads, out of the game folder (the
    /// uninstaller copy), or out of %TEMP% (the in-game F5 handoff), and at
    /// ~85 MB it is the payload rather than a stub. So instead of overwriting
    /// wherever we happen to be sitting — which needs write access to somebody
    /// else's folder and silently rewrites a file the user downloaded — the
    /// verified download is STAGED in %TEMP% and simply started. The old file
    /// stays where it was and is never touched. The install that follows copies
    /// the NEW EXE into the game folder as the uninstaller (see
    /// <c>InstallationManager.CopyUninstaller</c>), so the one copy that has to
    /// stay current does.</para>
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
        /// Set on the run that follows a successful self-update, so the user can
        /// be told what happened. A restart with no explanation is exactly the
        /// kind of thing a screen-reader user cannot see the cause of.
        /// </summary>
        public static string PostUpdateNotice { get; private set; }

        /// <summary>Version of the running EXE, e.g. "0.8.0".</summary>
        public static string RunningVersion => InstallerVersion.Number();

        private static string RunningExe => Environment.ProcessPath ?? string.Empty;

        /// <summary>
        /// Housekeeping for every run: note a completed self-update, and drop
        /// staged EXEs left in %TEMP% by earlier runs. Also runs when NO update
        /// happened, so a download that was cancelled or died halfway does not
        /// leave 85 MB behind forever.
        /// </summary>
        public static void CleanUp(string[] args)
        {
            if (HasSwitch(args, UpdatedSwitch))
            {
                PostUpdateNotice = InstallerLocale.Format("SelfUpdate_Done_Format", RunningVersion);
                Logger.Info($"Restarted after a self-update; now running {RunningVersion}.");
            }

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
        /// Starts the verified staged EXE and hands it this run's arguments, so a
        /// launch that carried a game path or a --game keeps carrying it across
        /// the restart. The caller returns straight afterwards; nothing was
        /// overwritten, so there is nothing to roll back.
        /// </summary>
        public static void LaunchStaged(string staged, string[] args)
        {
            var forwarded = new StringBuilder();
            foreach (var a in args ?? new string[0])
            {
                if (IsSwitch(a, SkipSwitch) || IsSwitch(a, UpdatedSwitch)) continue;
                forwarded.Append(Quote(a)).Append(' ');
            }
            forwarded.Append(UpdatedSwitch);

            Logger.Info($"Starting the updated installer: {staged} {forwarded}");
            Process.Start(new ProcessStartInfo
            {
                FileName = staged,
                Arguments = forwarded.ToString(),
                UseShellExecute = true,     // keeps the elevation we already hold
                WorkingDirectory = Path.GetDirectoryName(staged) ?? string.Empty,
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
