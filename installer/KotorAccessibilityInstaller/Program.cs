using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Principal;
using System.Windows.Forms;
using Microsoft.Win32;

namespace KotorAccessibilityInstaller
{
    public enum UpdateChoice
    {
        Close,
        UpdateOnly,
        FullInstall,
        ToggleSpatialAudio,
        CollectLogs
    }

    static class Program
    {
        public const string GameExeName = "swkotor.exe";

        // Steam install — the only supported distribution. GoG/Aspyr are out of scope
        // for the first installer pass; users can run kdev manually if needed.
        public static readonly string DefaultGamePath =
            @"C:\Program Files (x86)\Steam\steamapps\common\swkotor";

        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Logger.Info("Voice of the Old Republic Installer starting...");
            Logger.Info($"Running as: {Environment.UserName}");
            Logger.Info($"Is Admin: {IsRunningAsAdmin()}");
            Logger.Info($"Arguments: {string.Join(" ", args)}");

            InstallerLocale.Initialize(LanguageDetector.DetectLanguage());

            if (!IsRunningAsAdmin())
            {
                Logger.Error("Not running as administrator");
                MessageBox.Show(
                    InstallerLocale.Get("Program_AdminRequired_Text"),
                    InstallerLocale.Get("Program_AdminRequired_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }

            if (IsGameRunning())
            {
                Logger.Warning("KOTOR is currently running");
                MessageBox.Show(
                    InstallerLocale.Get("Program_GameRunning_Text"),
                    InstallerLocale.Get("Program_GameRunning_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Warning);
                return;
            }

            bool uninstallMode = false;
            bool quietMode = false;
            bool autoUpdateMode = false;
            string pathArg = null;
            string localKpatchPath = null;

            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i].ToLowerInvariant();
                if (arg == "/uninstall" || arg == "-uninstall" || arg == "--uninstall") uninstallMode = true;
                else if (arg == "/quiet" || arg == "-quiet" || arg == "--quiet" || arg == "/q" || arg == "-q") quietMode = true;
                else if (arg == "/auto-update" || arg == "-auto-update" || arg == "--auto-update") autoUpdateMode = true;
                else if (arg == "--local-kpatch" && i + 1 < args.Length) localKpatchPath = args[++i];
                else if (!arg.StartsWith("/") && !arg.StartsWith("-")) pathArg = args[i];
            }

            if (localKpatchPath != null)
            {
                if (!File.Exists(localKpatchPath))
                {
                    Logger.Error($"--local-kpatch path does not exist: {localKpatchPath}");
                    MessageBox.Show(
                        $"--local-kpatch path does not exist:\n{localKpatchPath}",
                        "Installer (dev)",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                    return;
                }
                Logger.Info($"DEV: --local-kpatch override active; using {localKpatchPath} instead of GitHub release");
            }

            if (uninstallMode)
            {
                Logger.Info("Running in uninstall mode");
                string gamePath = pathArg ?? RegistryManager.GetRegisteredInstallLocation() ?? DetectGamePath();

                if (string.IsNullOrEmpty(gamePath) || !IsValidGamePath(gamePath))
                {
                    if (!quietMode)
                    {
                        MessageBox.Show(
                            InstallerLocale.Get("Program_UninstallError_Text"),
                            InstallerLocale.Get("Program_UninstallError_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
                    }
                    Logger.Error("Uninstall failed: Could not determine KOTOR path");
                    return;
                }

                if (quietMode)
                {
                    PerformUninstall(gamePath);
                }
                else
                {
                    Application.Run(new UninstallForm(gamePath));
                }
                return;
            }

            // Install mode
            string detectedGamePath = pathArg ?? DetectGamePath() ?? DefaultGamePath;
            string installedModPath = Path.Combine(detectedGamePath, "patches", "accessibility.dll");
            bool modExists = File.Exists(installedModPath);

            // --auto-update: in-game F5 updater handoff. Skips every Welcome /
            // ModSelection / UpdateAvailable / InstalledOptions dialog and runs
            // MainForm directly in headless update-only mode. Exit code is 0 on
            // success, 1 on failure (the caller batch reads it). This path
            // assumes the mod is already installed; the install-only path is
            // out of scope here because the in-game updater only fires when an
            // existing patch DLL has loaded.
            if (autoUpdateMode)
            {
                Logger.Info("Auto-update mode active — running headless update");
                if (!modExists)
                {
                    Logger.Error($"--auto-update invoked but no installed mod found at {installedModPath}");
                    Environment.ExitCode = 1;
                    return;
                }
                // Re-apply install-time settings the running installer ships
                // with. Without this any installer-side config change between
                // versions (WER dump flags, registry tweaks, future helpers)
                // would only land on a manual reinstall, since the auto-update
                // skips the welcome / mod-selection path that normally calls
                // these. Idempotent + best-effort — failure does not block the
                // update.
                WerLocalDumps.Enable();
                Application.Run(new MainForm(detectedGamePath, updateOnly: true, localKpatchPath: localKpatchPath, headless: true));
                return;
            }
            bool updateAvailable = false;
            string installedVersion = null;
            string latestVersion = null;

            try
            {
                using (var client = new GitHubClient())
                {
                    latestVersion = client.GetLatestModVersionAsync(Config.ModRepositoryUrl).GetAwaiter().GetResult();
                    Logger.Info($"Latest version: {latestVersion}");
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not check for updates: {ex.Message}");
            }

            if (modExists)
            {
                installedVersion = RegistryManager.GetRegisteredVersion();
                Logger.Info($"Installed version (from registry): {installedVersion ?? "unknown"}");

                if (installedVersion != null && latestVersion != null)
                {
                    updateAvailable = IsNewerVersion(latestVersion, installedVersion);
                    Logger.Info($"Update available: {updateAvailable}");
                }
            }

            if (modExists && updateAvailable)
            {
                Logger.Info("Showing update dialog");
                bool spatialOn = SpatialAudioManager.IsEnabled(detectedGamePath);
                var updateForm = new UpdateAvailableForm(installedVersion, latestVersion, spatialOn);
                Application.Run(updateForm);

                switch (updateForm.UserChoice)
                {
                    case UpdateChoice.UpdateOnly:
                        Logger.Info("User chose to update mod only");
                        Application.Run(new MainForm(detectedGamePath, updateOnly: true, language: LanguageDetector.DetectLanguage(), localKpatchPath: localKpatchPath));
                        break;

                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full install");
                        RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        ToggleSpatialAudioAndReport(detectedGamePath);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User cancelled from update dialog");
                        break;
                }
            }
            else if (modExists)
            {
                Logger.Info($"Mod is up to date (installed: {installedVersion ?? "unknown"})");
                string displayVersion = installedVersion ?? "?";
                while (displayVersion.EndsWith(".0") && displayVersion.IndexOf('.') != displayVersion.LastIndexOf('.'))
                    displayVersion = displayVersion.Substring(0, displayVersion.Length - 2);

                bool spatialOn = SpatialAudioManager.IsEnabled(detectedGamePath);
                var form = new InstalledOptionsForm(displayVersion, spatialOn);
                Application.Run(form);

                switch (form.UserChoice)
                {
                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full reinstall");
                        RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        ToggleSpatialAudioAndReport(detectedGamePath);
                        break;

                    case UpdateChoice.CollectLogs:
                        Logger.Info("User chose to collect logs for beta-test bundle");
                        CollectLogsAndReport(detectedGamePath);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User closed installed-options dialog");
                        break;
                }
            }
            else
            {
                RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
            }
        }

        /// <summary>
        /// Welcome → Base-components info → Optional-mods checkboxes → Main install.
        /// Each form can cancel the chain.
        /// </summary>
        private static void RunFullInstallFlow(string gamePath, string pathArgOverride, string latestVersion, string localKpatchPath)
        {
            var welcomeForm = new WelcomeForm { LatestModVersion = latestVersion };
            Application.Run(welcomeForm);
            if (!welcomeForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from welcome dialog");
                return;
            }

            // Base-components info: accessibility mod + Prism + widescreen (always installed).
            var infoForm = new ModdingInfoForm();
            Application.Run(infoForm);
            if (!infoForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from base-components screen");
                return;
            }

            // Optional mods: K1CP / cut content / companion + swoop. All default on.
            var selectionForm = new ModSelectionForm();
            Application.Run(selectionForm);
            if (!selectionForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from mod-selection screen");
                return;
            }

            string resolvedPath = pathArgOverride ?? DetectGamePath() ?? gamePath;

            // Make Windows Error Reporting capture full minidumps under
            // %LOCALAPPDATA%\CrashDumps next time swkotor.exe faults. Without
            // this, beta testers' "collect logs" zips contain no crash dump.
            // Idempotent + best-effort; failure does not block install.
            WerLocalDumps.Enable();

            Application.Run(new MainForm(resolvedPath, language: welcomeForm.SelectedLanguage, modSelection: selectionForm.Selection, localKpatchPath: localKpatchPath));
        }

        /// <summary>
        /// Build a beta-test archive (.7z, LZMA2; .zip fallback) in the user's
        /// Downloads folder containing the newest patch log, the newest swkotor
        /// crash dump, the installer log, and a system-info summary. Opens
        /// Explorer with the archive selected so the user can attach it to a
        /// bug report directly.
        /// </summary>
        private static void CollectLogsAndReport(string gamePath)
        {
            // Make sure WER will actually capture dumps from now on, even if
            // nothing was set up earlier in the install. Idempotent.
            WerLocalDumps.Enable();

            var result = LogCollector.Collect(gamePath);
            if (!result.Success)
            {
                MessageBox.Show(
                    InstallerLocale.Format("CollectLogs_Error_Format", result.Error ?? "(unknown)"),
                    InstallerLocale.Get("CollectLogs_Error_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }

            string message = InstallerLocale.Format(
                "CollectLogs_Success_Format",
                result.ArchivePath,
                result.LogCount,
                result.DumpCount);
            MessageBox.Show(
                message,
                InstallerLocale.Get("CollectLogs_Success_Title"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);

            LogCollector.RevealInExplorer(result.ArchivePath);
        }

        /// <summary>
        /// Flip dsoal on or off based on its current state, then show a
        /// MessageBox so the screen reader announces the new state and reminds
        /// the user to restart the game.
        /// </summary>
        private static void ToggleSpatialAudioAndReport(string gamePath)
        {
            bool wasEnabled = SpatialAudioManager.IsEnabled(gamePath);
            var result = wasEnabled
                ? SpatialAudioManager.Disable(gamePath)
                : SpatialAudioManager.Enable(gamePath);

            if (!result.Success)
            {
                MessageBox.Show(
                    InstallerLocale.Format("SpatialAudio_Error_Format", result.Error ?? "(unknown)"),
                    InstallerLocale.Get("SpatialAudio_Error_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }

            string messageKey = result.NowEnabled
                ? "SpatialAudio_EnabledMessage"
                : "SpatialAudio_DisabledMessage";
            MessageBox.Show(
                InstallerLocale.Get(messageKey),
                InstallerLocale.Get("SpatialAudio_Result_Title"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }

        public static void PerformUninstall(string gamePath)
        {
            Logger.Info($"Uninstalling from: {gamePath}");

            try
            {
                // No restore: install no longer creates swkotor.exe.backup.* files.
                // The uninstall confirmation text tells the user to verify game
                // files via Steam or reinstall from GoG to get vanilla back.

                // Remove patcher runtime files from game root.
                string[] runtimeFiles =
                {
                    "KotorPatcher.dll",
                    "sqlite3.dll",
                    "addresses.db",
                    "patch_config.toml",
                    "dinput8.dll"
                };
                foreach (var name in runtimeFiles)
                {
                    string p = Path.Combine(gamePath, name);
                    if (File.Exists(p))
                    {
                        Logger.Info($"Removing {name}...");
                        try { File.Delete(p); } catch (Exception ex) { Logger.Warning($"Could not delete {name}: {ex.Message}"); }
                    }
                }

                // Remove the patches/ folder we put accessibility.dll + prism.dll into.
                string patchesDir = Path.Combine(gamePath, "patches");
                if (Directory.Exists(patchesDir))
                {
                    Logger.Info($"Removing patches folder: {patchesDir}");
                    try { Directory.Delete(patchesDir, recursive: true); }
                    catch (Exception ex) { Logger.Warning($"Could not delete patches/: {ex.Message}"); }
                }

                // Remove the Override assets we shipped (currently the
                // swoop accelpad WAV). Surgical per-file delete — never
                // touch the Override folder itself or any file we didn't
                // ship (users routinely drop their own mods in there).
                string overrideDir = Path.Combine(gamePath, "Override");
                foreach (var assetName in InstallationManager.OverrideAssetNames)
                {
                    string assetPath = Path.Combine(overrideDir, assetName);
                    if (File.Exists(assetPath))
                    {
                        Logger.Info($"Removing Override asset {assetName}...");
                        try { File.Delete(assetPath); }
                        catch (Exception ex) { Logger.Warning($"Could not delete {assetName}: {ex.Message}"); }
                    }
                }

                // Restore the intro movies we renamed during install. Returns
                // biologo / leclogo / legal .bik files to their vanilla names
                // so a vanilla launch plays the BioWare / LucasArts / legal
                // splash like a fresh install.
                Logger.Info("Restoring intro movies...");
                var introResult = IntroMovieDisabler.RestoreIntros(gamePath);
                if (!introResult.Success)
                {
                    Logger.Warning($"Intro restore failed: {introResult.Error}");
                }

                RegistryManager.Unregister();
                ScheduleUninstallerSelfDelete(gamePath);

                Logger.Info("Uninstallation complete");
                Logger.Flush();
            }
            catch (Exception ex)
            {
                Logger.Error("Uninstallation failed", ex);
                Logger.Flush();
                throw;
            }
        }

        private static void ScheduleUninstallerSelfDelete(string gamePath)
        {
            try
            {
                string uninstallerPath = Path.Combine(gamePath, Config.UninstallerExeName);
                if (!File.Exists(uninstallerPath)) return;

                Logger.Info($"Scheduling deletion of uninstaller: {uninstallerPath}");

                // ping (not timeout) — see docs/installer.md / arena IMPLEMENTATION.md
                // gotcha note: timeout refuses to run without an interactive console.
                var psi = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = $"/c ping 127.0.0.1 -n 5 -w 1000 >nul & del /f /q \"{uninstallerPath}\"",
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    WindowStyle = ProcessWindowStyle.Hidden
                };
                Process.Start(psi);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not schedule uninstaller deletion: {ex.Message}");
            }
        }

        private static bool IsRunningAsAdmin()
        {
            try
            {
                using var identity = WindowsIdentity.GetCurrent();
                var principal = new WindowsPrincipal(identity);
                return principal.IsInRole(WindowsBuiltInRole.Administrator);
            }
            catch { return false; }
        }

        private static bool IsGameRunning()
        {
            try
            {
                var processes = Process.GetProcessesByName("swkotor");
                return processes.Length > 0;
            }
            catch { return false; }
        }

        /// <summary>
        /// Attempts to detect KOTOR's install path. Order:
        /// 1. Registry (previously registered by us)
        /// 2. Steam's per-app registry key (HKLM ...\Steam App 32370)
        /// 3. Common Steam location under Program Files (x86)
        /// 4. CommonObjectives — Default Steam library path
        /// </summary>
        public static string DetectGamePath()
        {
            string registered = RegistryManager.GetRegisteredInstallLocation();
            if (IsValidGamePath(registered)) return registered;

            string steamReg = TryReadSteamAppInstallPath();
            if (IsValidGamePath(steamReg)) return steamReg;

            if (IsValidGamePath(DefaultGamePath)) return DefaultGamePath;

            // Fallback: scan ProgramFiles + ProgramFilesX86 for a swkotor folder.
            foreach (var root in new[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
            })
            {
                if (string.IsNullOrEmpty(root)) continue;
                string candidate = Path.Combine(root, "Steam", "steamapps", "common", "swkotor");
                if (IsValidGamePath(candidate)) return candidate;
            }

            return null;
        }

        private static string TryReadSteamAppInstallPath()
        {
            try
            {
                // KOTOR 1 Steam App ID = 32370. Steam writes InstallLocation here.
                using var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32)
                    .OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 32370");
                return key?.GetValue("InstallLocation") as string;
            }
            catch { return null; }
        }

        public static bool IsValidGamePath(string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            return File.Exists(Path.Combine(path, GameExeName));
        }

        /// <summary>
        /// True iff <paramref name="gamePath"/> is the install path Steam has
        /// registered for KOTOR (App ID 32370). Used by the post-install auto-
        /// launch to decide between <c>steam://run/32370</c> (preserves Steam
        /// overlay + cloud saves + non-elevated token) and a direct exe launch.
        ///
        /// Returns false for GoG copies, CD re-packs, manually-relocated Steam
        /// installs Steam doesn't know about, and any user-specified custom
        /// path — in those cases <c>steam://run/32370</c> would either silently
        /// no-op or launch a different copy than the one we just patched.
        /// </summary>
        public static bool IsSteamPath(string gamePath)
        {
            string steamRegistered = TryReadSteamAppInstallPath();
            if (string.IsNullOrEmpty(steamRegistered) || string.IsNullOrEmpty(gamePath))
                return false;
            return string.Equals(NormalizePath(steamRegistered), NormalizePath(gamePath),
                                 StringComparison.OrdinalIgnoreCase);
        }

        private static string NormalizePath(string p)
        {
            if (string.IsNullOrEmpty(p)) return p;
            try
            {
                return Path.GetFullPath(p)
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            }
            catch { return p; }
        }

        internal static bool IsNewerVersion(string latestVersion, string installedVersion)
        {
            try
            {
                var latest = NormalizeVersion(latestVersion);
                var installed = NormalizeVersion(installedVersion);
                Logger.Info($"Version comparison: latest={latest}, installed={installed}");
                return latest > installed;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Version comparison failed: {ex.Message}");
                return !string.Equals(latestVersion, installedVersion, StringComparison.OrdinalIgnoreCase);
            }
        }

        internal static Version NormalizeVersion(string version)
        {
            if (string.IsNullOrEmpty(version)) return new Version(0, 0, 0, 0);
            version = version.TrimStart('v', 'V');

            int dashIndex = version.IndexOf('-');
            if (dashIndex > 0) version = version.Substring(0, dashIndex);

            int spaceIndex = version.IndexOf(' ');
            if (spaceIndex > 0) version = version.Substring(0, spaceIndex);

            string[] parts = version.Trim().Split('.');
            int major = parts.Length > 0 && int.TryParse(parts[0], out int m) ? m : 0;
            int minor = parts.Length > 1 && int.TryParse(parts[1], out int n) ? n : 0;
            int build = parts.Length > 2 && int.TryParse(parts[2], out int b) ? b : 0;
            int revision = parts.Length > 3 && int.TryParse(parts[3], out int r) ? r : 0;

            return new Version(major, minor, build, revision);
        }
    }
}
