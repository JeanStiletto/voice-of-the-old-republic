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
            // Whatever happens — success, cancel at any dialog, or an
            // unhandled throw — the log ends up on disk. Main has a dozen
            // early returns, and relying on each one to remember to flush is
            // how a run that only produced warnings ended up leaving nothing
            // behind to diagnose.
            try { RunMain(args); }
            finally
            {
                // Order matters: sweep first so the log records what it removed.
                InstallationManager.SweepAbandonedStagingDirs();
                Logger.Flush();
            }
        }

        private static void RunMain(string[] args)
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
                    InstallerLocale.Format("Program_GameRunning_Text_Format", running.DisplayName),
                    InstallerLocale.Format("Program_GameRunning_Title_Format", running.DisplayName),
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
                else if (arg == "--dev-gog-only")
                {
                    GamePathDetector.SimulateGogOnly = true;
                    Logger.Info("DEV: --dev-gog-only active — game detection ignores the registered " +
                                "install and all Steam sources, leaving only GOG's registry key");
                }
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
            // Which game the headless and fresh-install paths act on: an
            // explicit --game wins (that is how the in-game updater says which
            // copy called it), otherwise the first game that already has the
            // mod. Without that second rule a KOTOR-2-only player was invisible
            // to the installer — it looked only at KOTOR 1, found nothing, and
            // offered a fresh install every single time. Falls back to KOTOR 1
            // when neither is installed, which is the fresh-install path anyway.
            //
            // Deliberately NOT what the maintenance dialogs act on — see
            // installedGames below.
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
            //
            // Single-game by design, and correctly so: the handoff batch passes
            // --game for the copy the player pressed F5 in, and updating the
            // other game behind their back while they wait to be put back into
            // this one is not what they asked for.
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

            // EVERY installed copy of the mod, not just one.
            //
            // The maintenance dialogs used to follow modeTarget, which resolves
            // to KOTOR 1 whenever KOTOR 1 has the mod. With both games modded
            // that meant "Update mod only" updated KOTOR 1 and silently left
            // KOTOR 2 behind — and on the next run KOTOR 1 was current, so the
            // installer reported "up to date" and offered no update button at
            // all. KOTOR 2 could then only be brought forward by a full
            // reinstall, which is exactly what testers reported. An explicit
            // --game still narrows this to one entry, so scripted invocations
            // are unchanged.
            var installedGames = FindInstalledGames(argTarget, pathArg);
            var staleGames = new List<InstalledGame>();
            foreach (var game in installedGames)
            {
                Logger.Info($"{game.Target.DisplayName}: installed version (from registry): {game.Version ?? "unknown"}");

                // An unknown installed version stays out of the stale set:
                // there is nothing to compare against, and offering an update
                // on a guess would re-run the whole apply for no stated reason.
                if (game.Version != null && latestVersion != null && IsNewerVersion(latestVersion, game.Version))
                    staleGames.Add(game);
            }
            Logger.Info($"Update available for: {(staleGames.Count > 0 ? JoinGameNames(staleGames) : "(none)")}");

            // dsoal is bundled for KOTOR 1 only, so the toggle is resolved from
            // the KOTOR 1 install specifically rather than from whichever game
            // happens to lead the maintenance set.
            string k1ModPath = InstalledPathOf(installedGames, GameTarget.Kotor1);
            bool offerSpatialAudio = k1ModPath != null;
            bool spatialOn = offerSpatialAudio && SpatialAudioManager.IsEnabled(k1ModPath);

            // The full-install flow uses its game-path argument for one thing:
            // prefilling the KOTOR 1 install screen. It therefore gets a KOTOR 1
            // folder or nothing — handing it whichever game led the maintenance
            // set prefilled the KOTOR 2 folder into a screen whose own
            // validation then rejected it, with no explanation on offer.
            string kotor1Path = k1ModPath
                ?? (modeTarget == GameTarget.Kotor1 ? detectedGamePath : null);

            if (staleGames.Count > 0)
            {
                Logger.Info($"Showing update dialog ({JoinGameNames(staleGames)})");
                var updateForm = new UpdateAvailableForm(JoinGameNames(staleGames), offerSpatialAudio,
                                                         staleGames[0].Version, latestVersion, spatialOn,
                                                         DescribeAllGames());
                Application.Run(updateForm);

                switch (updateForm.UserChoice)
                {
                    case UpdateChoice.UpdateOnly:
                        Logger.Info($"User chose to update mod only ({JoinGameNames(staleGames)})");
                        // One MainForm pass per stale game, sequential rather
                        // than one multi-target form — the same shape and the
                        // same reasoning as the full-install flow: each pass has
                        // its own path to validate, its own progress to announce
                        // and its own completion summary, and a screen-reader
                        // user needs those separated rather than interleaved.
                        foreach (var game in staleGames)
                        {
                            Logger.Info($"Updating {game.Target.DisplayName} at {game.GamePath}");
                            Application.Run(new MainForm(game.Target, game.GamePath, updateOnly: true,
                                language: LanguageDetector.DetectLanguage(),
                                localKpatchPath: localKpatchPath));
                        }
                        break;

                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full install");
                        InstallFlow.RunFullInstallFlow(kotor1Path, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        InstallFlow.ToggleSpatialAudioAndReport(k1ModPath);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User cancelled from update dialog");
                        break;
                }
            }
            else if (installedGames.Count > 0)
            {
                var primary = installedGames[0];
                Logger.Info($"Mod is up to date ({JoinGameNames(installedGames)})");

                var form = new InstalledOptionsForm(offerSpatialAudio, TrimVersion(primary.Version ?? "?"),
                                                    spatialOn, DescribeAllGames());
                Application.Run(form);

                switch (form.UserChoice)
                {
                    case UpdateChoice.FullInstall:
                        Logger.Info("User chose full reinstall");
                        InstallFlow.RunFullInstallFlow(kotor1Path, pathArg, latestVersion, localKpatchPath);
                        break;

                    case UpdateChoice.ToggleSpatialAudio:
                        Logger.Info("User chose to toggle spatial audio");
                        InstallFlow.ToggleSpatialAudioAndReport(k1ModPath);
                        break;

                    case UpdateChoice.CollectLogs:
                        Logger.Info("User chose to collect logs for beta-test bundle");
                        // LogCollector covers every installed game; the target
                        // and path only seed its detection.
                        InstallFlow.CollectLogsAndReport(primary.Target, primary.GamePath);
                        break;

                    case UpdateChoice.Close:
                    default:
                        Logger.Info("User closed installed-options dialog");
                        break;
                }
            }
            else
            {
                InstallFlow.RunFullInstallFlow(kotor1Path, pathArg, latestVersion, localKpatchPath);
            }
        }

        /// <summary>
        /// One line per game for the maintenance dialogs: installed and at which
        /// version, present but not modded, or not found at all.
        ///
        /// <para>These dialogs used to describe a single game — whichever one
        /// <c>modeTarget</c> resolved to — so with both games on the machine the
        /// other one was simply invisible, and a KOTOR 2 owner could not tell
        /// whether the mod was in it. Every game gets a line now, including the
        /// ones with nothing to say, because "KOTOR 2: not installed" is the
        /// answer the user is looking for just as often as a version number.</para>
        /// </summary>
        private static string DescribeAllGames()
        {
            var sb = new StringBuilder();
            foreach (var target in GameTarget.All)
            {
                string path = GamePathDetector.Detect(target);
                string state;

                if (path == null)
                {
                    state = InstallerLocale.Get("GameState_GameNotFound");
                }
                else if (!IsModInstalledAt(path))
                {
                    state = InstallerLocale.Get("GameState_ModNotInstalled");
                }
                else
                {
                    string version = RegistryManager.GetRegisteredVersion(target);
                    state = version != null
                        ? InstallerLocale.Format("GameState_ModInstalled_Format", TrimVersion(version))
                        : InstallerLocale.Get("GameState_ModInstalledUnknownVersion");
                }

                sb.AppendLine(InstallerLocale.Format("GameState_Line_Format", target.DisplayName, state));
            }
            return sb.ToString().TrimEnd();
        }

        /// <summary>Drop trailing ".0" groups so 0.6.4.0 reads as 0.6.4.</summary>
        private static string TrimVersion(string version)
        {
            while (version.EndsWith(".0") && version.IndexOf('.') != version.LastIndexOf('.'))
                version = version.Substring(0, version.Length - 2);
            return version;
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
        /// One installed copy of the mod: which game, where, and at which
        /// version. <see cref="Version"/> is the registry's DisplayVersion and
        /// is null when that entry is missing, which a hand-copied or
        /// pre-registry install can be.
        /// </summary>
        private sealed class InstalledGame
        {
            public GameTarget Target;
            public string GamePath;
            public string Version;
        }

        /// <summary>
        /// Every game that currently has the mod, KOTOR 1 first.
        ///
        /// <para>This — not a single target — is what the maintenance dialogs
        /// act on. Resolving to one game (the first installed) is what left a
        /// modded KOTOR 2 a version behind indefinitely: KOTOR 1 always won
        /// that race, so the update path never had the second install in hand
        /// and only a full reinstall could bring KOTOR 2 forward.</para>
        /// </summary>
        /// <param name="only">
        /// Restrict to this game (an explicit <c>--game</c>), or null for all.
        /// </param>
        /// <param name="pathOverride">
        /// A folder named on the command line. It is offered to every candidate
        /// and accepted only by the game whose executable it actually holds, so
        /// it can never attach one game's folder to the other game's entry.
        /// </param>
        private static List<InstalledGame> FindInstalledGames(GameTarget only, string pathOverride)
        {
            var games = new List<InstalledGame>();
            foreach (var target in GameTarget.All)
            {
                if (only != null && target != only) continue;

                string path = pathOverride ?? GamePathDetector.Detect(target);
                if (!GamePathDetector.IsValidGamePath(target, path)) continue;
                if (!IsModInstalledAt(path)) continue;

                games.Add(new InstalledGame
                {
                    Target = target,
                    GamePath = path,
                    Version = RegistryManager.GetRegisteredVersion(target),
                });
            }
            return games;
        }

        /// <summary>That game's folder among <paramref name="games"/>, or null.</summary>
        private static string InstalledPathOf(List<InstalledGame> games, GameTarget target)
        {
            foreach (var game in games)
                if (game.Target == target) return game.GamePath;
            return null;
        }

        /// <summary>
        /// "KOTOR 1" or "KOTOR 1, KOTOR 2" — for the headings and log lines that
        /// have to name a set rather than one game. Comma-separated rather than
        /// joined with a symbol: a screen reader reads the comma as the pause it
        /// is, where "+" is read out as a word mid-title.
        /// </summary>
        private static string JoinGameNames(List<InstalledGame> games)
        {
            var sb = new StringBuilder();
            foreach (var game in games)
            {
                if (sb.Length > 0) sb.Append(", ");
                sb.Append(game.Target.DisplayName);
            }
            return sb.ToString();
        }

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
