using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Principal;
using System.Text;
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

        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Logger.Info("Voice of the Old Republic Installer starting...");
            Logger.Info($"Running as: {Environment.UserName}");
            Logger.Info($"Is Admin: {GamePathDetector.IsRunningAsAdmin()}");
            Logger.Info($"Arguments: {string.Join(" ", args)}");

            InstallerLocale.Initialize(LanguageDetector.DetectLanguage());

            // Third-party mod pins: embedded copy, then the repo's live copy if
            // it is reachable. Deliberately before any dialog — a stale pin
            // changes what the mod screens can offer. Never throws.
            SourcePins.Initialize();

            if (!GamePathDetector.IsRunningAsAdmin())
            {
                Logger.Error("Not running as administrator");
                MessageBox.Show(
                    InstallerLocale.Get("Program_AdminRequired_Text"),
                    InstallerLocale.Get("Program_AdminRequired_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }

            var running = GamePathDetector.RunningGame();
            if (running != null)
            {
                Logger.Warning($"{running.DisplayName} is currently running");
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
            // Which game the maintenance modes act on. Absent means KOTOR 1,
            // which keeps every uninstall entry written before this flag existed
            // working unchanged.
            GameTarget argTarget = null;

            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i].ToLowerInvariant();
                if (arg == "/uninstall" || arg == "-uninstall" || arg == "--uninstall") uninstallMode = true;
                else if (arg == "/quiet" || arg == "-quiet" || arg == "--quiet" || arg == "/q" || arg == "-q") quietMode = true;
                else if (arg == "/auto-update" || arg == "-auto-update" || arg == "--auto-update") autoUpdateMode = true;
                else if (arg == "--local-kpatch" && i + 1 < args.Length) localKpatchPath = args[++i];
                else if ((arg == "--game" || arg == "-game" || arg == "/game") && i + 1 < args.Length)
                {
                    string id = args[++i];
                    argTarget = GameTarget.FromId(id);
                    if (argTarget == null)
                    {
                        Logger.Error($"--game '{id}' is not a known game id; expected k1 or k2");
                        MessageBox.Show(
                            $"--game '{id}' is not a known game id (expected k1 or k2).",
                            "Installer",
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
                        return;
                    }
                }
                else if (!arg.StartsWith("/") && !arg.StartsWith("-")) pathArg = args[i];
            }

            GameTarget maintenanceTarget = argTarget ?? GameTarget.Kotor1;

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
                Logger.Info($"Running in uninstall mode ({maintenanceTarget.DisplayName})");
                string gamePath = pathArg
                    ?? RegistryManager.GetRegisteredInstallLocation(maintenanceTarget)
                    ?? GamePathDetector.Detect(maintenanceTarget);

                if (string.IsNullOrEmpty(gamePath) || !GamePathDetector.IsValidGamePath(maintenanceTarget, gamePath))
                {
                    if (!quietMode)
                    {
                        MessageBox.Show(
                            InstallerLocale.Get("Program_UninstallError_Text"),
                            InstallerLocale.Get("Program_UninstallError_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
                    }
                    Logger.Error($"Uninstall failed: Could not determine {maintenanceTarget.DisplayName} path");
                    return;
                }

                if (quietMode)
                {
                    UninstallFlow.PerformUninstall(maintenanceTarget, gamePath);
                }
                else
                {
                    Application.Run(new UninstallForm(maintenanceTarget, gamePath));
                }
                return;
            }

            // Install mode.
            //
            // Which game the maintenance dialogs act on: an explicit --game wins
            // (that is how the in-game updater says which copy called it),
            // otherwise the first game that already has the mod. Without that
            // second rule a KOTOR-2-only player was invisible to the installer —
            // it looked only at KOTOR 1, found nothing, and offered a fresh
            // install every single time. Falls back to KOTOR 1 when neither is
            // installed, which is the fresh-install path anyway.
            GameTarget modeTarget = argTarget ?? FirstInstalledTarget() ?? GameTarget.Kotor1;

            string detectedGamePath = pathArg ?? GamePathDetector.Detect(modeTarget)
                ?? (modeTarget == GameTarget.Kotor1 ? GamePathDetector.DefaultGamePath : null);
            bool modExists = detectedGamePath != null && IsModInstalledAt(detectedGamePath);

            // --auto-update: in-game F5 updater handoff. Skips every Welcome /
            // ModSelection / UpdateAvailable / InstalledOptions dialog and runs
            // MainForm directly in headless update-only mode. Exit code is 0 on
            // success, 1 on failure (the caller batch reads it). This path
            // assumes the mod is already installed; the install-only path is
            // out of scope here because the in-game updater only fires when an
            // existing patch DLL has loaded.
            if (autoUpdateMode)
            {
                Logger.Info($"Auto-update mode active — running headless update ({modeTarget.DisplayName})");
                if (!modExists)
                {
                    Logger.Error("--auto-update invoked but no installed mod found at " +
                                 (detectedGamePath ?? "(no game path)"));
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
                Application.Run(new MainForm(modeTarget, detectedGamePath, updateOnly: true, localKpatchPath: localKpatchPath, headless: true));
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
                installedVersion = RegistryManager.GetRegisteredVersion(modeTarget);
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
                bool spatialOn = IsSpatialAudioOn(modeTarget, detectedGamePath);
                var updateForm = new UpdateAvailableForm(modeTarget, installedVersion, latestVersion, spatialOn);
                Application.Run(updateForm);

                switch (updateForm.UserChoice)
                {
                    case UpdateChoice.UpdateOnly:
                        Logger.Info("User chose to update mod only");
                        Application.Run(new MainForm(modeTarget, detectedGamePath, updateOnly: true, language: LanguageDetector.DetectLanguage(), localKpatchPath: localKpatchPath));
                        break;

                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full install");
                        InstallFlow.RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        InstallFlow.ToggleSpatialAudioAndReport(detectedGamePath);
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

                bool spatialOn = IsSpatialAudioOn(modeTarget, detectedGamePath);
                var form = new InstalledOptionsForm(modeTarget, displayVersion, spatialOn);
                Application.Run(form);

                switch (form.UserChoice)
                {
                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full reinstall");
                        InstallFlow.RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        InstallFlow.ToggleSpatialAudioAndReport(detectedGamePath);
                        break;

                    case UpdateChoice.CollectLogs:
                        Logger.Info("User chose to collect logs for beta-test bundle");
                        InstallFlow.CollectLogsAndReport(modeTarget, detectedGamePath);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User closed installed-options dialog");
                        break;
                }
            }
            else
            {
                InstallFlow.RunFullInstallFlow(detectedGamePath, pathArg, latestVersion, localKpatchPath);
            }
        }

        /// <summary>Our runtime DLL sitting in the game folder is what "installed" means.</summary>
        private static bool IsModInstalledAt(string gamePath) =>
            File.Exists(Path.Combine(gamePath, "patches", "accessibility.dll"));

        /// <summary>
        /// First game (KOTOR 1 before KOTOR 2) that has the mod installed, or
        /// null when neither does.
        /// </summary>
        private static GameTarget FirstInstalledTarget()
        {
            foreach (var target in GameTarget.All)
            {
                string path = GamePathDetector.Detect(target);
                if (path != null && IsModInstalledAt(path)) return target;
            }
            return null;
        }

        /// <summary>
        /// dsoal is bundled for KOTOR 1 only, so its state is never queried for
        /// KOTOR 2 — SpatialAudioManager validates a swkotor.exe and would answer
        /// about the wrong game's folder.
        /// </summary>
        private static bool IsSpatialAudioOn(GameTarget target, string gamePath) =>
            target == GameTarget.Kotor1 && gamePath != null && SpatialAudioManager.IsEnabled(gamePath);

        /// <summary>
        /// Welcome → Base-components info → Optional-mods checkboxes → Main install.
        /// Each form can cancel the chain.
        /// </summary>
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
