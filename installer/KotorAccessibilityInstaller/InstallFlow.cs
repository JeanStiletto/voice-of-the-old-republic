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

            // Which game(s) to install into. Both are fully supported; each gets
            // its own MainForm pass at the end of this method.
            var gameVersionForm = new GameVersionSelectionForm();
            Application.Run(gameVersionForm);
            if (!gameVersionForm.ProceedWithInstall)
            {
                Logger.Info("Installation cancelled from game-version selection");
                return;
            }

            // Now that we know which games the user wants, offer store links for
            // any of them that isn't on this PC — and skip the screen entirely
            // when they are all present. Deliberately after the selection: a
            // "where to buy" page shown before the user has chosen a game can
            // only guess which game to advertise, and the old one always guessed
            // KOTOR 1.
            var missingGames = new List<GameTarget>();
            if (gameVersionForm.Selection.Kotor1 && GamePathDetector.Detect(GameTarget.Kotor1) == null)
                missingGames.Add(GameTarget.Kotor1);
            if (gameVersionForm.Selection.Kotor2 && GamePathDetector.Detect(GameTarget.Kotor2) == null)
                missingGames.Add(GameTarget.Kotor2);

            if (!GameStoreLinksForm.ShowIfNeeded(missingGames))
            {
                Logger.Info("Installation cancelled from the game-store-links screen");
                return;
            }

            string k2Path = null;
            if (gameVersionForm.Selection.Kotor2)
            {
                k2Path = GamePathDetector.DetectKotor2GamePath();
                Logger.Info($"KOTOR 2 selected; detected install: {k2Path ?? "(not found)"}");

                // KOTOR 2's game-content mods run BEFORE our own install, not
                // after. TSLRCM is a third-party Inno installer that writes
                // freely into the game folder (and replaces dialog.tlk
                // outright), so anything of ours already sitting there would be
                // at its mercy. KOTOR 1's optional mods have the same ordering,
                // just expressed differently: they run inside MainForm, but
                // still after nothing of ours is at risk — they are
                // HoloPatcher payloads that only touch game data.
                RunKotor2ModFlow(k2Path, welcomeForm.SelectedLanguage);
            }

            string resolvedK1Path = null;
            ModSelection k1Mods = null;

            if (gameVersionForm.Selection.Kotor1)
            {
                // Base-components info: accessibility mod + Prism + widescreen (always installed).
                var infoForm = new ModdingInfoForm();
                Application.Run(infoForm);
                if (!infoForm.ProceedWithInstall)
                {
                    Logger.Info("Installation cancelled from base-components screen");
                    return;
                }

                resolvedK1Path = pathArgOverride ?? GamePathDetector.DetectGamePath() ?? gamePath;

                // Detect the game's own language before the optional-mods screen so
                // the Russian notes can ride that screen's footnote instead of
                // adding a step. Content-based for Russian — see GameLocaleDetector.
                GameLocale gameLocale = GameLocaleDetector.Detect(resolvedK1Path);
                WarnIfGameTranslationMissing(welcomeForm.SelectedLanguage, gameLocale);

                // Optional mods: K1CP / cut content / companion + swoop. All default on.
                var selectionForm = new ModSelectionForm(gameLocale);
                Application.Run(selectionForm);
                if (!selectionForm.ProceedWithInstall)
                {
                    Logger.Info("Installation cancelled from mod-selection screen");
                    return;
                }
                k1Mods = selectionForm.Selection;
            }

            // Make Windows Error Reporting capture minidumps under
            // %LOCALAPPDATA%\CrashDumps next time either game faults. Without
            // this, beta testers' "collect logs" zips contain no crash dump.
            // Idempotent + best-effort; failure does not block install.
            WerLocalDumps.Enable();

            // One MainForm pass per selected game. Sequential rather than a
            // single multi-target form: each pass has its own path to validate,
            // its own progress to announce and its own completion summary, and
            // a screen-reader user needs those separated rather than interleaved.
            // The cost is one extra .kpatch download when both are selected.
            if (resolvedK1Path != null)
            {
                Application.Run(new MainForm(GameTarget.Kotor1, resolvedK1Path,
                    language: welcomeForm.SelectedLanguage, modSelection: k1Mods,
                    localKpatchPath: localKpatchPath));
            }

            if (gameVersionForm.Selection.Kotor2)
            {
                if (k2Path == null)
                {
                    // Nothing to install into. Said plainly rather than silently
                    // skipped — the user explicitly asked for KOTOR 2.
                    MessageBox.Show(
                        InstallerLocale.Get("K2Prep_NotDetected"),
                        InstallerLocale.Get("K2Prep_Title"),
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Warning);
                }
                else
                {
                    Application.Run(new MainForm(GameTarget.Kotor2, k2Path,
                        language: welcomeForm.SelectedLanguage,
                        localKpatchPath: localKpatchPath));
                }
            }
        }

        /// <summary>
        /// KOTOR 2's game-content mods, run before our own install (see the
        /// caller for why that order). Sequence: show
        /// <see cref="Kotor2ModSelectionForm"/> (TSLRCM / K2CP / Tweak Pack —
        /// all on by default), install TSLRCM via
        /// <see cref="TslrcmInstallForm"/> (silent Inno; visible wizard as
        /// fallback), then run the K2CP + Tweak Pack pipeline via
        /// <see cref="Kotor2ModsInstallForm"/> — gated on TSLRCM being
        /// installed (this run or detected via <see cref="TslrcmDetector"/>)
        /// so the community-mandated order TSLRCM → K2CP → Tweak Pack always
        /// holds. Ends with one spoken per-mod summary box.
        ///
        /// The engine patches are NOT applied here. They rewrite swkotor2.exe,
        /// so they install in the same KPatchCore transaction as the
        /// accessibility patch during the MainForm pass — applying them in a
        /// transaction of their own would change the executable hash that the
        /// accessibility patch's own version gate then reads.
        /// </summary>
        /// <param name="installerLanguage">
        /// The language the user picked for the installer itself. Decides which
        /// localized TSLRCM text is offered — see the harvest block for why the
        /// game's own dialog.tlk cannot answer that question.
        /// </param>
        internal static void RunKotor2ModFlow(string k2Path, string installerLanguage)
        {
            string statusLine = k2Path != null
                ? InstallerLocale.Format("K2Prep_Detected_Format", k2Path)
                : InstallerLocale.Get("K2Prep_NotDetected");

            var selectionForm = new Kotor2ModSelectionForm(statusLine);
            Application.Run(selectionForm);
            if (!selectionForm.ProceedWithInstall)
            {
                Logger.Info("KOTOR 2 mod selection cancelled");
                return;
            }

            // Subscribed Workshop items override everything we are about to
            // install. Ask before, not after.
            if (k2Path != null && !ConfirmNoWorkshopContent(k2Path)) return;

            // For the record only. The game's own language stops being a usable
            // signal the moment the English TSLRCM replaces dialog.tlk, so the
            // routing below keys off the installer's language instead — but
            // knowing what the game WAS is worth having in the log.
            if (k2Path != null)
                Logger.Info($"KOTOR 2 text language before TSLRCM: {GameLocaleDetector.Detect(k2Path)}");

            var summary = new StringBuilder();
            summary.AppendLine(InstallerLocale.Get("ModInstall_SummaryHeading"));
            const string tslrcmName = "TSLRCM 1.8.6";

            // WHICH TSLRCM this user gets. Non-English players take the
            // localized Steam Workshop edition; everyone else takes the English
            // DeadlyStream installer.
            //
            // This is not a preference, it is the only correct answer for them.
            // The DeadlyStream exe is English-only and ships a full English
            // dialog.tlk that replaces the player's — turning a German game's
            // entire text English, losing a translation that was already right.
            // The localized editions exist only as Workshop items, ship no text
            // table at all, and embed their new strings in the content files, so
            // they leave the player's table alone. See WorkshopTslrcmForm.
            var tslrcmLocale = GameLocaleDetector.FromInstallerLanguage(installerLanguage);
            bool useLocalizedTslrcm =
                k2Path != null &&
                tslrcmLocale != GameLocale.English && tslrcmLocale != GameLocale.Unknown &&
                WorkshopTslrcmForm.TryGetWorkshopItem(tslrcmLocale, out _);

            bool tslrcmPresent;
            if (selectionForm.InstallTslrcm && useLocalizedTslrcm)
            {
                tslrcmPresent = RunLocalizedTslrcmInstall(k2Path, tslrcmLocale, summary, tslrcmName);
            }
            else if (selectionForm.InstallTslrcm)
            {
                // Three signals, strongest first.
                //
                // 1. The silent install verifies ITSELF: TslrcmInstallForm
                //    checks dialog.tlk and, when that is unchanged, Setup's own
                //    log — so a reinstall over an existing TSLRCM reports
                //    success rather than a scary false failure.
                // 2. Otherwise the uninstall entry, which is all we have after
                //    the visible wizard.
                // 3. If neither fires, ASK. The alternative is to conclude
                //    "not installed" from the absence of a signal we are not
                //    sure exists, and then silently skip K2CP and the Tweak
                //    Pack — the failure that leaves a player with a mod set
                //    they believe is complete and is not.
                var outcome = RunTslrcmInstall(k2Path);

                if (outcome == TslrcmOutcome.SilentInstalled)
                {
                    tslrcmPresent = true;
                    Logger.Info("TSLRCM present: verified by the silent install");
                }
                else if (TslrcmDetector.IsInstalled())
                {
                    tslrcmPresent = true;
                }
                else if (outcome == TslrcmOutcome.DownloadedOnly ||
                         outcome == TslrcmOutcome.SilentInstallFailed)
                {
                    // The user was handed the wizard. Whether they completed it
                    // is something only they know.
                    tslrcmPresent = MessageBox.Show(
                        InstallerLocale.Get("K2Tslrcm_ConfirmInstalled_Text"),
                        InstallerLocale.Get("K2Prep_Title"),
                        MessageBoxButtons.YesNo,
                        MessageBoxIcon.Question) == DialogResult.Yes;
                    Logger.Info($"TSLRCM presence answered by the user: {tslrcmPresent}");
                }
                else
                {
                    tslrcmPresent = false;
                }

                summary.AppendLine(tslrcmPresent
                    ? InstallerLocale.Format(
                        _tslrcmWasAlreadyInstalled ? "ModInstall_SummaryAlready_Format"
                                                   : "ModInstall_SummaryOk_Format", tslrcmName)
                    : InstallerLocale.Format("ModInstall_SummaryFailed_Format", tslrcmName, "(not installed)"));
            }
            else
            {
                tslrcmPresent = TslrcmDetector.IsInstalled();
                summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", tslrcmName));
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
        /// silent run failed and the user accepts the fallback). Returns the
        /// form's outcome so the caller can tell a self-verified silent install
        /// from a wizard handoff whose result only the user knows.
        /// </summary>
        /// <summary>
        /// TSLRCM for a non-English player: the localized Steam Workshop
        /// edition, copied into the game folder.
        ///
        /// <para>The user subscribes in Steam's own UI (the one manual step we
        /// cannot automate — the items are UGC-depot hosted, so there is no
        /// anonymous download), we copy the content out, and we tell them to
        /// unsubscribe. Unsubscribing is what makes this safe: while the item
        /// stays subscribed, the game reads it from a separate folder whose
        /// files hard-conflict with directory-installed mods. Once copied and
        /// unsubscribed, they are ordinary game files that K2CP and the Tweak
        /// Pack patch normally.</para>
        ///
        /// <para>Returns whether TSLRCM ended up installed.</para>
        /// </summary>
        private static bool RunLocalizedTslrcmInstall(
            string k2Path, GameLocale locale, StringBuilder summary, string tslrcmName)
        {
            WorkshopTslrcmForm.TryGetWorkshopItem(locale, out string itemId);
            Logger.Info($"TSLRCM: taking the localized Workshop edition for {locale} (item {itemId})");

            var offer = MessageBox.Show(
                InstallerLocale.Format("K2Lang_Offer_Text", locale.ToString()),
                InstallerLocale.Get("K2Prep_Title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);

            if (offer != DialogResult.Yes)
            {
                // Declining the localized edition means declining TSLRCM. We do
                // NOT quietly fall back to the English one: that would replace
                // their text table with English, which is the outcome this whole
                // path exists to avoid, and doing it as an unrequested fallback
                // would be the worst way to arrive there.
                Logger.Info("User declined the localized TSLRCM; TSLRCM not installed");
                summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", tslrcmName));
                return TslrcmDetector.IsInstalled();
            }

            var form = new WorkshopTslrcmForm(k2Path, locale, itemId);
            Application.Run(form);

            if (form.Success)
            {
                MessageBox.Show(
                    InstallerLocale.Get("K2Lang_Done_Text"),
                    InstallerLocale.Get("K2Prep_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
                summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryOk_Format", tslrcmName));
                return true;
            }

            if (form.FailureReason != null)
            {
                MessageBox.Show(
                    InstallerLocale.Format("K2Lang_Failed_Format", form.FailureReason),
                    InstallerLocale.Get("K2Prep_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Warning);
                summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryFailed_Format",
                    tslrcmName, form.FailureReason));
            }
            else
            {
                summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", tslrcmName));
            }

            // An earlier install may still be there even though this attempt
            // did not complete.
            return TslrcmDetector.IsInstalled();
        }

        /// <summary>
        /// Set by <see cref="RunTslrcmInstall"/> when Setup reported success
        /// without changing anything, i.e. TSLRCM was already there. Only
        /// affects the wording of the summary line.
        /// </summary>
        private static bool _tslrcmWasAlreadyInstalled;

        private static TslrcmOutcome RunTslrcmInstall(string k2Path)
        {
            string statusLine = k2Path != null
                ? InstallerLocale.Format("K2Prep_Detected_Format", k2Path)
                : InstallerLocale.Get("K2Prep_NotDetected");

            var form = new TslrcmInstallForm(k2Path);
            Application.Run(form);
            _tslrcmWasAlreadyInstalled = form.AlreadyInstalled;

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

                case TslrcmOutcome.VerificationFailed:
                    // Not a breakage: the file downloaded fine, it just isn't
                    // the one this installer was built against. Say that, then
                    // let the user fetch it themselves.
                    return OfferManualTslrcm(k2Path, statusLine,
                        InstallerLocale.Get("ManualDownload_Reason_Updated"));

                case TslrcmOutcome.DownloadFailed:
                    return OfferManualTslrcm(k2Path, statusLine,
                        InstallerLocale.Format("ManualDownload_Reason_Failed_Format",
                                               form.FailureReason));

                case TslrcmOutcome.Cancelled:
                default:
                    // User cancelled — the manual link is on the selection
                    // form's footnote, so stay quiet.
                    break;
            }

            return form.Outcome;
        }

        /// <summary>
        /// Automatic acquisition of TSLRCM did not work. Hand the job to the
        /// user — open the download page, take the file they fetched — and then
        /// continue the install exactly as if we had downloaded it ourselves.
        ///
        /// <para>This is the path that still works when the pins are years out
        /// of date or DeadlyStream has changed its download flow entirely, and
        /// it is why a stale pin is an inconvenience rather than a dead end.
        /// Returns the outcome of the resumed install, or the original failure
        /// when the user skips.</para>
        /// </summary>
        private static TslrcmOutcome OfferManualTslrcm(string k2Path, string statusLine, string reason)
        {
            string supplied = ManualDownloadForm.Ask(
                "TSLRCM " + SourcePins.Tslrcm.DisplayVersion,
                reason,
                Config.TslrcmDownloadPageUrl,
                Config.TslrcmInstallerFileName,
                ManualDownloadForm.FileKind.Executable,
                Config.TslrcmInstallerSha256);

            if (supplied == null)
            {
                Logger.Info("User skipped the manual TSLRCM download");
                return TslrcmOutcome.DownloadFailed;
            }

            if (k2Path == null)
            {
                // No install dir to point a silent /DIR at — let TSLRCM's own
                // wizard do its game detection, same as the DownloadedOnly path.
                RunTslrcmWizardInteractive(supplied, statusLine, deleteAfter: false);
                return TslrcmOutcome.DownloadedOnly;
            }

            var form = new TslrcmInstallForm(k2Path, suppliedInstallerPath: supplied);
            Application.Run(form);

            if (form.Outcome == TslrcmOutcome.SilentInstallFailed)
            {
                var fallback = MessageBox.Show(
                    InstallerLocale.Format("K2Tslrcm_SilentFailed_Format", form.FailureReason),
                    InstallerLocale.Get("K2Prep_Title"),
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Warning);
                if (fallback == DialogResult.Yes)
                    RunTslrcmWizardInteractive(supplied, statusLine, deleteAfter: false);
            }

            // The user's own download is never deleted — it is in their
            // Downloads folder and theirs to keep.
            return form.Outcome;
        }

        /// <summary>
        /// Steam Workshop pre-flight. A subscribed Workshop copy of TSLRCM (or
        /// any other Workshop item) installs into a separate content folder that
        /// the game overlays on top of everything else, which breaks the
        /// directory-installed mods we are about to lay down — the community
        /// builds all require unsubscribing first. We cannot unsubscribe on the
        /// user's behalf, so this warns and lets them choose.
        ///
        /// Detection is deliberately shallow: the existence of any subdirectory
        /// under the KOTOR 2 workshop content path. Reading which items they are
        /// would mean parsing Steam's manifests for no gain — any Workshop
        /// content at all is worth the warning.
        ///
        /// Returns false when the user chose to stop.
        /// </summary>
        private static bool ConfirmNoWorkshopContent(string k2Path)
        {
            string workshopDir = FindWorkshopContentDir(k2Path);
            if (workshopDir == null) return true;

            Logger.Warning($"Steam Workshop content present for KOTOR 2: {workshopDir}");
            var choice = MessageBox.Show(
                InstallerLocale.Format("K2Workshop_Warning_Format", workshopDir),
                InstallerLocale.Get("K2Prep_Title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning);

            if (choice != DialogResult.Yes)
            {
                Logger.Info("User stopped at the Steam Workshop warning");
                return false;
            }
            return true;
        }

        /// <summary>
        /// <c>&lt;library&gt;/steamapps/workshop/content/208580</c> if it exists and
        /// holds at least one item, else null. Derived from the game path
        /// (<c>steamapps/common/&lt;game&gt;</c>) so it follows a game installed to a
        /// secondary Steam library rather than assuming the default one.
        /// </summary>
        private static string FindWorkshopContentDir(string k2Path)
        {
            try
            {
                // …/steamapps/common/<game>  ->  …/steamapps
                var common = Directory.GetParent(k2Path);
                var steamapps = common?.Parent;
                if (steamapps == null) return null;

                string workshop = Path.Combine(steamapps.FullName, "workshop", "content",
                                               Config.Kotor2WorkshopAppId);
                if (!Directory.Exists(workshop)) return null;
                return Directory.EnumerateFileSystemEntries(workshop).GetEnumerator().MoveNext()
                    ? workshop
                    : null;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not probe for Workshop content: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Visible-wizard fallback: announce the handoff (so an unannounced
        /// English wizard doesn't steal focus from the screen reader), launch
        /// TSLRCM's Inno wizard, block until it exits, delete the temp exe.
        /// </summary>
        /// <param name="deleteAfter">
        /// False when the exe is the user's own download sitting in their
        /// Downloads folder. Deleting that would be us throwing away a file we
        /// did not create and they may want to keep — or re-use, if they have
        /// to run this again.
        /// </param>
        private static void RunTslrcmWizardInteractive(string installerExe, string statusLine, bool deleteAfter = true)
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
                if (deleteAfter) TryDeleteTempFile(installerExe);
            }
        }

        private static void TryDeleteTempFile(string path)
        {
            if (string.IsNullOrEmpty(path)) return;
            try { if (File.Exists(path)) File.Delete(path); }
            catch (Exception ex) { Logger.Warning($"Could not delete temp file {path}: {ex.Message}"); }
        }

        /// <summary>
        /// The user picked a language for the installer whose game translation
        /// KOTOR never shipped on Steam or GoG, but this install is not
        /// translated to it. Point them at the translation's own source and stop
        /// there — we cannot bundle or mirror either one. Russian's community
        /// translation carries no redistribution licence and no stable download
        /// location; the Polish one is first-party LucasArts content extracted
        /// from the 2004 retail release, which is not ours to hand out at all.
        ///
        /// The mod still speaks the chosen language either way, so this is
        /// guidance, not an error, and it must not block the install.
        /// </summary>
        private static readonly (string Lang, GameLocale Locale, string Key)[] TranslationGuidance =
        {
            ("ru", GameLocale.Russian, "Russian"),
            ("pl", GameLocale.Polish,  "Polish"),
        };

        private static void WarnIfGameTranslationMissing(string installerLanguage, GameLocale gameLocale)
        {
            foreach (var (lang, locale, key) in TranslationGuidance)
            {
                if (!string.Equals(installerLanguage, lang, StringComparison.OrdinalIgnoreCase)) continue;
                if (gameLocale == locale) return;

                Logger.Info($"Installer language is {key} but game locale is {gameLocale}; " +
                            "showing translation guidance.");
                MessageBox.Show(
                    InstallerLocale.Get($"{key}_NotFound_Body"),
                    InstallerLocale.Get($"{key}_NotFound_Heading"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
                return;
            }
        }

        /// <summary>
        /// Build a beta-test archive (.7z, LZMA2; .zip fallback) in the user's
        /// Downloads folder containing the newest patch log, the newest swkotor
        /// crash dump, the installer log, and a system-info summary. Opens
        /// Explorer with the archive selected so the user can attach it to a
        /// bug report directly.
        /// </summary>
        internal static void CollectLogsAndReport(GameTarget target, string gamePath)
        {
            // Make sure WER will actually capture dumps from now on, even if
            // nothing was set up earlier in the install. Idempotent.
            WerLocalDumps.Enable();

            var result = LogCollector.Collect(target, gamePath);
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
