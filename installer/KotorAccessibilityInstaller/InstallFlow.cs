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
    // Install-flow orchestration.
    //
    // Split out of Program.cs by the Phase-1 structure pass (refactoring
    // candidate 17). Program.Main dispatches a mode; these drive it -
    // the KOTOR 1 full install, the KOTOR 2 preparation and TSLRCM
    // sub-flows, and the standalone maintenance actions (log collection,
    // spatial-audio toggle).
    static class InstallFlow
    {
        internal static void RunFullInstallFlow(string gamePath, string pathArgOverride, string latestVersion, string localKpatchPath)
        {
            var welcomeForm = new WelcomeForm { LatestModVersion = latestVersion };
            Application.Run(welcomeForm);
            if (!welcomeForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from welcome dialog");
                return;
            }

            // Which game(s) to target. KOTOR 1 continues into the flow below.
            // KOTOR 2 support is in preparation: the selection is collected and
            // logged, K2cpInstaller exists, but the KOTOR 2 install flow is not
            // wired yet (TSLRCM-first ordering must be resolved first — see
            // docs/installer.md, "KOTOR 2 mod bundle"). Until then a KOTOR 2
            // selection gets a notice with the manual TSLRCM download steps.
            var gameVersionForm = new GameVersionSelectionForm();
            Application.Run(gameVersionForm);
            if (!gameVersionForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from game-version selection");
                return;
            }

            if (gameVersionForm.Selection.Kotor2)
            {
                string k2Path = GamePathDetector.DetectKotor2GamePath();
                Logger.Info($"KOTOR 2 selected; detected install: {k2Path ?? "(not found)"}");
                RunKotor2PreparationFlow(k2Path);
            }

            if (!gameVersionForm.Selection.Kotor1)
            {
                Logger.Info("KOTOR 2 only selected; nothing installable in this version — exiting.");
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

            string resolvedPath = pathArgOverride ?? GamePathDetector.DetectGamePath() ?? gamePath;

            // Detect the game's own language before the optional-mods screen so
            // the Russian notes can ride that screen's footnote instead of
            // adding a step. Content-based for Russian — see GameLocaleDetector.
            GameLocale gameLocale = GameLocaleDetector.Detect(resolvedPath);
            WarnIfRussianTranslationMissing(welcomeForm.SelectedLanguage, gameLocale);

            // Optional mods: K1CP / cut content / companion + swoop. All default on.
            var selectionForm = new ModSelectionForm(gameLocale);
            Application.Run(selectionForm);
            if (!selectionForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from mod-selection screen");
                return;
            }

            // Make Windows Error Reporting capture full minidumps under
            // %LOCALAPPDATA%\CrashDumps next time swkotor.exe faults. Without
            // this, beta testers' "collect logs" zips contain no crash dump.
            // Idempotent + best-effort; failure does not block install.
            WerLocalDumps.Enable();

            Application.Run(new MainForm(resolvedPath, language: welcomeForm.SelectedLanguage, modSelection: selectionForm.Selection, localKpatchPath: localKpatchPath));
        }

        /// <summary>
        /// Runs when the user checks KOTOR 2 on the game-version screen.
        /// Sequence: apply the engine patches (their result rides the
        /// selection form's description), show
        /// <see cref="Kotor2ModSelectionForm"/> (TSLRCM / K2CP / Tweak Pack —
        /// all on by default), install TSLRCM via
        /// <see cref="TslrcmInstallForm"/> (silent Inno; visible wizard as
        /// fallback), then run the K2CP + Tweak Pack pipeline via
        /// <see cref="Kotor2ModsInstallForm"/> — gated on TSLRCM being
        /// installed (this run or detected via <see cref="TslrcmDetector"/>)
        /// so the community-mandated order TSLRCM → K2CP → Tweak Pack always
        /// holds. Ends with one spoken per-mod summary box.
        /// </summary>
        internal static void RunKotor2PreparationFlow(string k2Path)
        {
            string statusLine = k2Path != null
                ? InstallerLocale.Format("K2Prep_Detected_Format", k2Path)
                : InstallerLocale.Get("K2Prep_NotDetected");

            // Engine patches first: fast, local, independent of the mod
            // choices below.
            if (k2Path != null)
            {
                statusLine += "\n\n" + ApplyKotor2EnginePatches(k2Path);
            }

            var selectionForm = new Kotor2ModSelectionForm(statusLine);
            Application.Run(selectionForm);
            if (!selectionForm.ProceedWithInstall)
            {
                Logger.Info("KOTOR 2 mod selection cancelled");
                return;
            }

            // Detect the game's language BEFORE TSLRCM runs: the English-only
            // TSLRCM replaces dialog.tlk, after which the original language is
            // no longer readable from the install.
            GameLocale k2Locale = k2Path != null ? GameLocaleDetector.Detect(k2Path) : GameLocale.Unknown;

            var summary = new StringBuilder();
            summary.AppendLine(InstallerLocale.Get("ModInstall_SummaryHeading"));
            const string tslrcmName = "TSLRCM 1.8.6";

            bool tslrcmPresent;
            if (selectionForm.InstallTslrcm)
            {
                RunTslrcmInstall(k2Path);
                // The detector is the ground truth either way: the silent path
                // sets the registry entry via Inno, and after the visible
                // wizard it is the only signal we have for the outcome.
                tslrcmPresent = TslrcmDetector.IsInstalled();
                summary.AppendLine(tslrcmPresent
                    ? InstallerLocale.Format("ModInstall_SummaryOk_Format", tslrcmName)
                    : InstallerLocale.Format("ModInstall_SummaryFailed_Format", tslrcmName, "(not installed)"));
            }
            else
            {
                tslrcmPresent = TslrcmDetector.IsInstalled();
                summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", tslrcmName));
            }

            // Localized-text harvest — must run AFTER TSLRCM (whose English
            // dialog.tlk it replaces) and BEFORE the K2CP / Tweak Pack
            // pipeline (which appends strings to dialog.tlk; replacing the
            // file afterwards would orphan their strrefs).
            if (selectionForm.InstallTslrcm && tslrcmPresent && k2Path != null &&
                k2Locale != GameLocale.English && k2Locale != GameLocale.Unknown &&
                WorkshopTlkHarvestForm.TryGetWorkshopItem(k2Locale, out string workshopItemId))
            {
                const string harvestName = "dialog.tlk (Workshop)";
                var offer = MessageBox.Show(
                    InstallerLocale.Get("K2Lang_Offer_Text"),
                    InstallerLocale.Get("K2Prep_Title"),
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Question);

                if (offer == DialogResult.Yes)
                {
                    var harvestForm = new WorkshopTlkHarvestForm(k2Path, k2Locale, workshopItemId);
                    Application.Run(harvestForm);

                    if (harvestForm.Success)
                    {
                        MessageBox.Show(
                            InstallerLocale.Get("K2Lang_Done_Text"),
                            InstallerLocale.Get("K2Prep_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Information);
                        summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryOk_Format", harvestName));
                    }
                    else if (harvestForm.FailureReason != null)
                    {
                        MessageBox.Show(
                            InstallerLocale.Format("K2Lang_Failed_Format", harvestForm.FailureReason),
                            InstallerLocale.Get("K2Prep_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Warning);
                        summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryFailed_Format",
                            harvestName, harvestForm.FailureReason));
                    }
                    else
                    {
                        summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", harvestName));
                    }
                }
                else
                {
                    Logger.Info("User declined localized-text harvest");
                    summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", harvestName));
                }
            }

            if (selectionForm.Selection.K2cp || selectionForm.Selection.TweakPack)
            {
                if (k2Path == null || !tslrcmPresent)
                {
                    // Order gate: without a known install dir, or without
                    // TSLRCM in place, installing K2CP / Tweak Pack now would
                    // bake in the wrong order (TSLRCM must come first).
                    if (k2Path != null)
                    {
                        MessageBox.Show(
                            InstallerLocale.Get("K2Mods_NoTslrcm_Text"),
                            InstallerLocale.Get("K2Prep_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Warning);
                    }
                    Logger.Info($"K2CP/Tweak Pack skipped (k2Path={(k2Path ?? "null")}, tslrcmPresent={tslrcmPresent})");
                    if (selectionForm.Selection.K2cp)
                        summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", "k2cp"));
                    if (selectionForm.Selection.TweakPack)
                        summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", "tweakpack"));
                }
                else
                {
                    var installForm = new Kotor2ModsInstallForm(k2Path, selectionForm.Selection);
                    Application.Run(installForm);
                    foreach (var r in installForm.Results)
                    {
                        if (r.Skipped)
                            summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", r.Id));
                        else if (r.Success)
                            summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryOk_Format", r.Id));
                        else
                            summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryFailed_Format", r.Id, r.Error ?? "(no detail)"));
                    }
                }
            }

            // TSLRCM's readme buries this in an English document in the game
            // folder; a one-click install would never surface it. Speak it in
            // the summary: old saves are incompatible, a fresh game is required.
            if (selectionForm.InstallTslrcm && tslrcmPresent)
            {
                summary.AppendLine();
                summary.AppendLine(InstallerLocale.Get("K2Mods_NewGameHint"));
            }

            MessageBox.Show(
                summary.ToString(),
                InstallerLocale.Get("K2Prep_Title"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }

        /// <summary>
        /// TSLRCM step: silent install when the KOTOR 2 folder is known,
        /// visible English wizard as fallback (no folder detected, or the
        /// silent run failed and the user accepts the fallback). The caller
        /// determines success via <see cref="TslrcmDetector"/> afterwards.
        /// </summary>
        private static void RunTslrcmInstall(string k2Path)
        {
            string statusLine = k2Path != null
                ? InstallerLocale.Format("K2Prep_Detected_Format", k2Path)
                : InstallerLocale.Get("K2Prep_NotDetected");

            var form = new TslrcmInstallForm(k2Path);
            Application.Run(form);

            switch (form.Outcome)
            {
                case TslrcmOutcome.SilentInstalled:
                    TryDeleteTempFile(form.InstallerPath);
                    break;

                case TslrcmOutcome.DownloadedOnly:
                    // No KOTOR 2 folder detected — silent /DIR would be a guess,
                    // so let TSLRCM's own wizard do its game detection visibly.
                    RunTslrcmWizardInteractive(form.InstallerPath, statusLine);
                    break;

                case TslrcmOutcome.SilentInstallFailed:
                    var fallback = MessageBox.Show(
                        InstallerLocale.Format("K2Tslrcm_SilentFailed_Format", form.FailureReason),
                        InstallerLocale.Get("K2Prep_Title"),
                        MessageBoxButtons.YesNo,
                        MessageBoxIcon.Warning);
                    if (fallback == DialogResult.Yes)
                        RunTslrcmWizardInteractive(form.InstallerPath, statusLine);
                    else
                        TryDeleteTempFile(form.InstallerPath);
                    break;

                case TslrcmOutcome.DownloadFailed:
                    MessageBox.Show(
                        InstallerLocale.Format("K2Tslrcm_Failed_Format",
                            form.FailureReason, Config.TslrcmDownloadPageUrl),
                        InstallerLocale.Get("K2Prep_Title"),
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Warning);
                    break;

                case TslrcmOutcome.Cancelled:
                default:
                    // User cancelled — the manual link is on the selection
                    // form's footnote, so stay quiet.
                    break;
            }
        }

        /// <summary>
        /// Applies Lane's static engine patches (4 GB memory + borderless
        /// fullscreen) to swkotor2.exe and sets swkotor2.ini to windowed mode
        /// (the borderless patch requires AllowWindowedMode=1 + FullScreen=0,
        /// and windowed is our screen-reader baseline anyway). Returns a
        /// localized one-line result for the preparation dialog. A null result
        /// from the applicator means the exe hash is not a declared build —
        /// already patched by an earlier run, or an unknown variant — which is
        /// reported as a skip, not an error.
        /// </summary>
        private static string ApplyKotor2EnginePatches(string k2Path)
        {
            try
            {
                var manager = new InstallationManager(k2Path);
                var result = manager.ApplyKotor2StaticPatches(out string skipReason);

                if (result == null)
                {
                    return InstallerLocale.Format("K2Patches_Skipped_Format", skipReason);
                }

                foreach (var msg in result.Messages ?? new List<string>())
                    Logger.Info($"  K2 patch: {msg}");

                if (!result.Success)
                {
                    Logger.Error($"KOTOR 2 engine patches failed: {result.Error}");
                    return InstallerLocale.Format("K2Patches_Failed_Format", result.Error);
                }

                var iniResult = SwkotorIniTweaker.ApplyKotor2WindowedDefaults(k2Path);
                if (!iniResult.Success)
                {
                    // Patch bytes landed; only the ini step failed. Report the
                    // partial state rather than calling the whole step failed.
                    Logger.Warning($"swkotor2.ini tweak failed: {iniResult.Error}");
                    return InstallerLocale.Format("K2Patches_Failed_Format",
                        $"swkotor2.ini: {iniResult.Error}");
                }

                Logger.Info("KOTOR 2 engine patches + swkotor2.ini windowed defaults applied.");
                return InstallerLocale.Get("K2Patches_Applied");
            }
            catch (Exception ex)
            {
                Logger.Error("KOTOR 2 engine patch step failed", ex);
                return InstallerLocale.Format("K2Patches_Failed_Format", ex.Message);
            }
        }

        /// <summary>
        /// Visible-wizard fallback: announce the handoff (so an unannounced
        /// English wizard doesn't steal focus from the screen reader), launch
        /// TSLRCM's Inno wizard, block until it exits, delete the temp exe.
        /// </summary>
        private static void RunTslrcmWizardInteractive(string installerExe, string statusLine)
        {
            MessageBox.Show(
                InstallerLocale.Format("K2Tslrcm_WizardIntro_Format", statusLine),
                InstallerLocale.Get("K2Prep_Title"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);

            try
            {
                Logger.Info($"Launching TSLRCM installer: {installerExe}");
                using var proc = Process.Start(new ProcessStartInfo
                {
                    FileName = installerExe,
                    UseShellExecute = true
                });
                proc?.WaitForExit();
                Logger.Info($"TSLRCM installer exited with code {proc?.ExitCode}");
            }
            catch (Exception ex)
            {
                Logger.Error("Could not launch TSLRCM installer", ex);
                MessageBox.Show(
                    InstallerLocale.Format("K2Tslrcm_Failed_Format",
                        ex.Message, Config.TslrcmDownloadPageUrl),
                    InstallerLocale.Get("K2Prep_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Warning);
            }
            finally
            {
                TryDeleteTempFile(installerExe);
            }
        }

        private static void TryDeleteTempFile(string path)
        {
            if (string.IsNullOrEmpty(path)) return;
            try { if (File.Exists(path)) File.Delete(path); }
            catch (Exception ex) { Logger.Warning($"Could not delete temp file {path}: {ex.Message}"); }
        }

        /// <summary>
        /// The user picked Russian for the installer but the game itself is not
        /// translated. Point them at the translation's own source and stop
        /// there: KOTOR has no official Russian release, and the community
        /// translation is not published under a licence that lets us bundle or
        /// mirror it, nor at a location stable enough to download and verify.
        /// The mod still speaks Russian either way, so this is guidance, not an
        /// error, and it must not block the install.
        /// </summary>
        private static void WarnIfRussianTranslationMissing(string installerLanguage, GameLocale gameLocale)
        {
            if (!string.Equals(installerLanguage, "ru", StringComparison.OrdinalIgnoreCase)) return;
            if (gameLocale == GameLocale.Russian) return;

            Logger.Info($"Installer language is Russian but game locale is {gameLocale}; " +
                        "showing translation guidance.");
            MessageBox.Show(
                InstallerLocale.Get("Russian_NotFound_Body"),
                InstallerLocale.Get("Russian_NotFound_Heading"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }

        /// <summary>
        /// Build a beta-test archive (.7z, LZMA2; .zip fallback) in the user's
        /// Downloads folder containing the newest patch log, the newest swkotor
        /// crash dump, the installer log, and a system-info summary. Opens
        /// Explorer with the archive selected so the user can attach it to a
        /// bug report directly.
        /// </summary>
        internal static void CollectLogsAndReport(string gamePath)
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
        internal static void ToggleSpatialAudioAndReport(string gamePath)
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

    }
}
