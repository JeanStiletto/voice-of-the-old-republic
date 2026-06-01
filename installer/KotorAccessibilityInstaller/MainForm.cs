using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.Automation;
using KotorAccessibilityInstaller.ModInstallers;

namespace KotorAccessibilityInstaller
{
    public class MainForm : Form
    {
        private string _gamePath;
        private readonly bool _updateOnly;
        private readonly bool _headless;
        private readonly string _language;
        private readonly ModSelection _modSelection;
        private readonly string _localKpatchPath;
        private bool _installFinished;

        private Label _titleLabel;
        private Label _statusLabel;
        private Label _pathLabel;
        private TextBox _pathTextBox;
        private Button _browseButton;
        private Button _installButton;
        private Button _cancelButton;
        private ProgressBar _progressBar;
        private CheckBox _launchCheckBox;
        private CheckBox _readmeCheckBox;

        public MainForm(string detectedGamePath, bool updateOnly = false, string language = null, ModSelection modSelection = null, string localKpatchPath = null, bool headless = false)
        {
            _gamePath = detectedGamePath;
            _updateOnly = updateOnly;
            _headless = headless;
            _language = language;
            // Null modSelection happens on the update-only path (we don't re-prompt for
            // optional mods on a kpatch update). Treat as "skip optional installs".
            _modSelection = modSelection;
            _localKpatchPath = localKpatchPath;
            InitializeComponents();
            Logger.Info($"MainForm initialized (updateOnly: {updateOnly}, headless: {headless}, language: {language ?? "none"}, modSelection: {modSelection?.ToString() ?? "none"}, localKpatch: {localKpatchPath ?? "none"})");
        }

        // In headless mode (--auto-update from the in-game updater), auto-trigger
        // the install on Shown so the user doesn't need to click anything. The
        // game-relaunch path is owned by the calling batch script, so the readme
        // / launch checkboxes are forced off in InitializeComponents and the
        // confirm + success dialogs are skipped in InstallButton_Click.
        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (_headless)
            {
                Logger.Info("Headless mode active — auto-triggering install");
                BeginInvoke(new Action(() => InstallButton_Click(this, EventArgs.Empty)));
            }
        }

        private void InitializeComponents()
        {
            Text = InstallerLocale.Get(_updateOnly ? "Main_TitleUpdater" : "Main_TitleInstaller");
            Size = new Size(520, 360);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Text = InstallerLocale.Get(_updateOnly ? "Main_TitleUpdater" : "Main_TitleInstaller"),
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(470, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            _statusLabel = new Label
            {
                Text = InstallerLocale.Get(_updateOnly ? "Main_StatusUpdate" : "Main_StatusInstall"),
                Location = new Point(20, 60),
                Size = new Size(470, 60),
                TextAlign = ContentAlignment.TopLeft
            };

            _pathLabel = new Label
            {
                Text = InstallerLocale.Get("Main_PathLabel"),
                Location = new Point(20, 130),
                Size = new Size(200, 20)
            };

            _pathTextBox = new TextBox
            {
                Text = _gamePath ?? Program.DefaultGamePath,
                Location = new Point(20, 155),
                Size = new Size(370, 25),
                ReadOnly = true
            };

            _browseButton = new Button
            {
                Text = InstallerLocale.Get("Main_BrowseButton"),
                Location = new Point(400, 153),
                Size = new Size(90, 27)
            };
            _browseButton.Click += BrowseButton_Click;

            _progressBar = new ProgressBar
            {
                Location = new Point(20, 195),
                Size = new Size(470, 25),
                Style = ProgressBarStyle.Continuous,
                Visible = false
            };

            _launchCheckBox = new CheckBox
            {
                Text = InstallerLocale.Get("Main_LaunchCheckBox"),
                Location = new Point(20, 230),
                Size = new Size(300, 25),
                // Headless = invoked by in-game updater; the calling batch
                // script relaunches the game, so this checkbox would race
                // against it. Force off.
                Checked = false
            };

            _readmeCheckBox = new CheckBox
            {
                Text = InstallerLocale.Get("Main_ReadmeCheckBox"),
                Location = new Point(20, 255),
                Size = new Size(400, 25),
                // Headless = unattended; don't pop a browser tab during what
                // the user is treating as a transparent restart.
                Checked = !_headless
            };

            _installButton = new Button
            {
                Text = InstallerLocale.Get(_updateOnly ? "Main_UpdateButton" : "Main_InstallButton"),
                Location = new Point(300, 290),
                Size = new Size(90, 30)
            };
            _installButton.Click += InstallButton_Click;

            _cancelButton = new Button
            {
                Text = InstallerLocale.Get("Main_CancelButton"),
                Location = new Point(400, 290),
                Size = new Size(90, 30)
            };
            _cancelButton.Click += (s, e) => Close();

            Controls.AddRange(new Control[]
            {
                _titleLabel, _statusLabel, _pathLabel, _pathTextBox, _browseButton,
                _progressBar, _launchCheckBox, _readmeCheckBox, _installButton, _cancelButton
            });

            ValidatePath();

            string body = $"{_titleLabel.Text}. {_statusLabel.Text}";
            AccessibleDescription = body;
            _installButton.AccessibleDescription = body;

            FormClosing += (s, e) =>
            {
                // Headless = no user to confirm; let close fall through. Errors
                // already MessageBox'd before close, so no info is being lost.
                if (_headless) return;
                if (!_installFinished && !CancelConfirm.ConfirmCancel(this))
                {
                    e.Cancel = true;
                }
            };
        }

        private void BrowseButton_Click(object sender, EventArgs e)
        {
            using var dialog = new FolderBrowserDialog
            {
                Description = InstallerLocale.Get("Main_BrowseDialogDescription"),
                ShowNewFolderButton = false
            };

            if (!string.IsNullOrEmpty(_pathTextBox.Text) && Directory.Exists(_pathTextBox.Text))
                dialog.SelectedPath = _pathTextBox.Text;

            if (dialog.ShowDialog() == DialogResult.OK)
            {
                _pathTextBox.Text = dialog.SelectedPath;
                _gamePath = dialog.SelectedPath;
                Logger.Info($"User selected path: {_gamePath}");
                ValidatePath();
            }
        }

        private void ValidatePath()
        {
            bool isValid = Program.IsValidGamePath(_pathTextBox.Text);
            _installButton.Enabled = isValid;

            if (!isValid && !string.IsNullOrEmpty(_pathTextBox.Text))
            {
                _statusLabel.Text = InstallerLocale.Get("Main_PathNotFound");
                _statusLabel.ForeColor = Color.Red;
            }
            else
            {
                _statusLabel.Text = InstallerLocale.Get(_updateOnly ? "Main_StatusUpdate" : "Main_StatusInstall");
                _statusLabel.ForeColor = SystemColors.ControlText;
            }
        }

        private async void InstallButton_Click(object sender, EventArgs e)
        {
            _gamePath = _pathTextBox.Text;

            // Headless = invoked by in-game updater; user already confirmed by
            // pressing F5 in-game and accepting the UAC prompt. Skip the dialog.
            if (!_headless)
            {
                string confirmMessage = InstallerLocale.Format(
                    _updateOnly ? "Main_ConfirmUpdate_Format" : "Main_ConfirmInstall_Format", _gamePath);
                string confirmTitle = InstallerLocale.Get(
                    _updateOnly ? "Main_ConfirmUpdate_Title" : "Main_ConfirmInstall_Title");

                var result = MessageBox.Show(confirmMessage, confirmTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
                if (result != DialogResult.Yes) return;
            }

            SetControlsEnabled(false);
            _progressBar.Visible = true;
            _progressBar.Value = 0;

            string stagingRoot = null;
            string downloadedKpatch = null;
            string latestVersion = null;
            string holoPatcherPath = null;
            var installationManager = new InstallationManager(_gamePath);

            using var githubClient = new GitHubClient();

            try
            {
                Logger.Info($"Starting {(_updateOnly ? "update" : "installation")} to: {_gamePath}");

                // Step 1: download .kpatch from GitHub releases
                // DEV override: --local-kpatch <path> short-circuits the GitHub
                // fetch and uses a locally-built .kpatch. Useful for testing the
                // installer end-to-end before a real release exists.
                UpdateStatus(InstallerLocale.Get("Main_StatusDownloading"));
                UpdateProgress(5);

                if (_localKpatchPath != null)
                {
                    Logger.Info($"DEV: skipping GitHub download; using local kpatch at {_localKpatchPath}");
                    downloadedKpatch = _localKpatchPath;
                    UpdateProgress(40);
                }
                else
                {
                    try
                    {
                        latestVersion = await githubClient.GetLatestModVersionAsync(Config.ModRepositoryUrl);
                        Logger.Info($"Latest mod version for registry: {latestVersion ?? "unknown"}");
                    }
                    catch (Exception ex)
                    {
                        Logger.Warning($"Could not fetch latest version: {ex.Message}");
                    }

                    try
                    {
                        downloadedKpatch = await githubClient.DownloadKPatchAsync(
                            Config.ModRepositoryUrl,
                            Config.KPatchAssetName,
                            p => UpdateProgress(5 + (p * 35 / 100)));
                    }
                    catch (Exception modEx)
                    {
                        Logger.Error("Failed to download kpatch", modEx);
                        MessageBox.Show(
                            InstallerLocale.Format("Main_ModDownloadFailed_Format", modEx.Message, Config.ModRepositoryUrl),
                            InstallerLocale.Get("Main_ModDownloadFailed_Title"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
                        SetControlsEnabled(true);
                        _progressBar.Visible = false;
                        return;
                    }
                }

                // Step 2: stage bundled patcher runtime + downloaded kpatch
                UpdateStatus(InstallerLocale.Get("Main_StatusStaging"));
                UpdateProgress(45);
                stagingRoot = await Task.Run(() => installationManager.StagePatcherRuntime(downloadedKpatch));

                // Step 3: apply via KPatchCore
                UpdateStatus(InstallerLocale.Get("Main_StatusApplying"));
                UpdateProgress(60);
                var applyResult = await Task.Run(() => installationManager.ApplyKPatch(stagingRoot));

                foreach (var msg in applyResult.Messages)
                    Logger.Info($"  {msg}");

                if (!applyResult.Success)
                {
                    Logger.Error($"KPatchCore install failed: {applyResult.Error}");
                    MessageBox.Show(
                        InstallerLocale.Format("Main_ApplyFailed_Format", applyResult.Error ?? "(no detail)"),
                        InstallerLocale.Get("Main_ApplyFailed_Title"),
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                    SetControlsEnabled(true);
                    _progressBar.Visible = false;
                    return;
                }

                // Step 4: drop the dinput8.dll proxy next to swkotor.exe so
                // the game auto-loads KotorPatcher on launch. Without this
                // the install still works, but the user would need an
                // external launcher (KPatchLauncher / kdev launch) to
                // inject the patcher each session.
                UpdateProgress(83);
                await Task.Run(() => installationManager.InstallLoader());

                // Step 4.1: drop Prism speech runtime alongside accessibility.dll
                UpdateStatus(InstallerLocale.Get("Main_StatusCopyingPrism"));
                UpdateProgress(85);
                await Task.Run(() => installationManager.InstallPrismRuntime());

                // Step 4.4: drop Override-folder WAV assets (currently the
                // swoop accelpad cue). Best-effort: not having Override
                // assets only degrades the affected audio cue, the rest of
                // the mod still functions.
                try
                {
                    await Task.Run(() => installationManager.InstallOverrideAssets());
                }
                catch (Exception ovEx)
                {
                    Logger.Warning($"Could not install Override assets: {ovEx.Message}");
                }

                // Step 4.45: append the mod's dedicated full-volume audio
                // priority group to prioritygroups.2da. Best-effort: if this
                // fails the DLL falls back to a vanilla full-volume group, so
                // cues still play at full — only the dedicated voice-budget /
                // falloff tuning is lost.
                try
                {
                    await Task.Run(() => installationManager.InstallPriorityGroup());
                }
                catch (Exception pgEx)
                {
                    Logger.Warning($"Could not install priority group: {pgEx.Message}");
                }

                // Step 4.5: apply community-recommended stability tweaks to swkotor.ini
                // (V-Sync, Frame Buffer, Disable Vertex Buffer Objects, FullScreen).
                // Best-effort: a failure here doesn't roll back the install — the mod
                // still works without these tweaks, the user just doesn't get the
                // stability boost.
                UpdateStatus(InstallerLocale.Get("Main_StatusTweakingIni"));
                UpdateProgress(87);
                var iniResult = await Task.Run(() => SwkotorIniTweaker.ApplyAccessibilityDefaults(_gamePath));
                if (!iniResult.Success)
                {
                    Logger.Warning($"Stability tweaks failed: {iniResult.Error}");
                }

                // Step 4.6: disable the three launch-time intro movies
                // (biologo / leclogo / legal). Cuts ~10–20 s off cold start
                // AND eliminates the engine bug where alt-tab during an
                // intro causes the entire intro queue to restart, costing
                // blind users 30–60 s of unresponsive-menu time. Mod
                // settings has a runtime toggle if the user wants intros
                // back later. In-game cutscenes are unaffected — see
                // IntroMovieDisabler doc comment for the engine path split.
                UpdateStatus(InstallerLocale.Get("Main_StatusDisablingIntros"));
                UpdateProgress(88);
                var introResult = await Task.Run(() => IntroMovieDisabler.DisableIntros(_gamePath));
                if (!introResult.Success)
                {
                    Logger.Warning($"Intro disable failed: {introResult.Error}");
                }

                // Step 4.7: install user-selected optional mods (K1CP today, more later).
                // _modSelection is null on the update-only path — the coordinator
                // handles that by returning an empty result list and short-circuiting.
                var modResults = new List<ModInstallResult>();
                if (_modSelection != null && AnyOptionalSelected(_modSelection))
                {
                    // HoloPatcher download — only fetch when at least one optional mod
                    // is selected. Same GitHub-release flow as the .kpatch download
                    // above; cached in temp for this install run only.
                    UpdateStatus(InstallerLocale.Get("Main_StatusDownloadingHoloPatcher"));
                    UpdateProgress(88);
                    holoPatcherPath = await HoloPatcherProvider.DownloadAsync(
                        githubClient,
                        p => UpdateProgress(88 + (p * 2 / 100))); // 88..90 slot for HoloPatcher download

                    UpdateStatus(InstallerLocale.Get("Main_StatusInstallingMods"));
                    UpdateProgress(90);
                    modResults = await ModInstallerCoordinator.InstallSelectedAsync(
                        ModInstallerCoordinator.BuildPipeline(),
                        _modSelection,
                        _gamePath,
                        holoPatcherPath,
                        p => UpdateProgress(90 + (p * 4 / 100)), // 90..94 slot for all mods combined
                        UpdateStatus);
                }

                // Step 5: persistent uninstaller + registry
                string uninstallerPath = installationManager.CopyUninstaller();

                UpdateStatus(InstallerLocale.Get("Main_StatusRegistering"));
                UpdateProgress(95);
                string registeredVersion = latestVersion ?? "1.0.0";
                RegistryManager.Register(_gamePath, registeredVersion, uninstallerPath);

                UpdateProgress(100);
                UpdateStatus(InstallerLocale.Get(_updateOnly ? "Main_StatusUpdateComplete" : "Main_StatusInstallComplete"));
                Logger.Info($"{(_updateOnly ? "Update" : "Installation")} completed successfully");

                string completionMessage = InstallerLocale.Get(
                    _updateOnly ? "Main_CompleteUpdate_Text" : "Main_CompleteInstall_Text");
                completionMessage += InstallerLocale.Get(_updateOnly ? "Main_CompleteModUpdated" : "Main_CompleteModInstalled");

                if (modResults.Count > 0)
                {
                    var summary = new StringBuilder();
                    summary.AppendLine();
                    summary.AppendLine();
                    summary.AppendLine(InstallerLocale.Get("ModInstall_SummaryHeading"));
                    foreach (var r in modResults)
                    {
                        if (r.Skipped)
                            summary.AppendLine(InstallerLocale.Format("ModInstall_SummarySkipped_Format", r.Id));
                        else if (r.Success)
                            summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryOk_Format", r.Id));
                        else
                            summary.AppendLine(InstallerLocale.Format("ModInstall_SummaryFailed_Format", r.Id, r.Error ?? "(no detail)"));
                    }
                    completionMessage += summary.ToString();
                }

                // Headless = the in-game updater is waiting for our exit code,
                // not for the user to read a dialog. Flush logs silently and
                // close without the success MessageBox / readme / launch
                // prompts; the calling batch script handles game relaunch.
                if (_headless)
                {
                    Logger.Flush();
                }
                else
                {
                    MessageBox.Show(
                        completionMessage,
                        InstallerLocale.Get(_updateOnly ? "Main_CompleteUpdate_Title" : "Main_CompleteInstall_Title"),
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Information);

                    Logger.AskAndSave();

                    if (_readmeCheckBox.Checked) OpenReadme();
                    if (_launchCheckBox.Checked) LaunchGame();
                }

                _installFinished = true;
                Close();
            }
            catch (Exception ex)
            {
                Logger.Error($"{(_updateOnly ? "Update" : "Installation")} failed", ex);

                MessageBox.Show(
                    InstallerLocale.Format(
                        _updateOnly ? "Main_ErrorUpdate_Format" : "Main_ErrorInstall_Format", ex.Message),
                    InstallerLocale.Get(_updateOnly ? "Main_ErrorUpdate_Title" : "Main_ErrorInstall_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);

                if (_headless)
                {
                    // Surface failure to the calling batch + flush log without
                    // prompting (no user is in front of the screen — the batch
                    // is what catches the exit code).
                    Environment.ExitCode = 1;
                    Logger.Flush();
                    _installFinished = true;
                    Close();
                }
                else
                {
                    if (Logger.AskAndSave(alwaysAsk: true)) Logger.OpenLogFile();

                    SetControlsEnabled(true);
                    _progressBar.Visible = false;
                }
            }
            finally
            {
                // Don't delete the source path when --local-kpatch is in effect —
                // that's the user's locally-built .kpatch, not a temp download.
                if (downloadedKpatch != null && downloadedKpatch != _localKpatchPath)
                {
                    try { File.Delete(downloadedKpatch); } catch { }
                }
                if (stagingRoot != null)
                    installationManager.CleanupStaging(stagingRoot);
                HoloPatcherProvider.Cleanup(holoPatcherPath);
            }
        }

        private void UpdateStatus(string message)
        {
            if (InvokeRequired) { Invoke(new Action(() => UpdateStatus(message))); return; }
            _statusLabel.Text = message;
            _statusLabel.ForeColor = SystemColors.ControlText;
            Logger.Info(message);

            // WinForms Labels are not UIA live regions, so just changing
            // `_statusLabel.Text` is invisible to NVDA / JAWS / Narrator until
            // the user navigates to it. Raise a UIA notification on the label
            // so screen readers speak each update as it lands.
            //
            // MostRecent processing means a fresh update interrupts a pending
            // one — important during the K1CP step where heartbeat + stdout
            // forwards can fire in quick succession.
            try
            {
                _statusLabel.AccessibilityObject?.RaiseAutomationNotification(
                    AutomationNotificationKind.ActionCompleted,
                    AutomationNotificationProcessing.MostRecent,
                    message);
            }
            catch (Exception ex)
            {
                // Older Windows or no UIA at runtime — degrade silently so
                // missing announcements don't take down the install.
                Logger.Warning($"Could not raise automation notification: {ex.Message}");
            }
        }

        private void UpdateProgress(int value)
        {
            if (InvokeRequired) { Invoke(new Action(() => UpdateProgress(value))); return; }
            _progressBar.Value = Math.Min(value, 100);
        }

        private void SetControlsEnabled(bool enabled)
        {
            _browseButton.Enabled = enabled;
            _installButton.Enabled = enabled;
            _pathTextBox.Enabled = enabled;
            _launchCheckBox.Enabled = enabled;
            _readmeCheckBox.Enabled = enabled;
        }

        private static bool AnyOptionalSelected(ModSelection selection) =>
            selection != null && (selection.K1cp || selection.RestoredCutContent || selection.CompanionAndSwoopUpgrades);

        private void OpenReadme()
        {
            try
            {
                string url = (_language == null || _language == "en")
                    ? Config.ModSiteUrl
                    : $"{Config.ModSiteUrl}docs/README.{_language}.html";

                Logger.Info($"Opening README: {url}");
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to open README", ex);
            }
        }

        private void LaunchGame()
        {
            try
            {
                string exePath = Path.Combine(_gamePath, Program.GameExeName);
                if (!File.Exists(exePath)) return;

                // steam://run/32370 launches whatever copy Steam has registered
                // for KOTOR, which is NOT necessarily _gamePath — GoG installs,
                // CD re-packs, manually-relocated Steam folders, and any
                // user-specified custom path are unknown to Steam. Only take
                // the steam:// route when _gamePath matches Steam's registered
                // install (then we get the overlay, cloud saves, and a non-
                // elevated launch). Otherwise launch the patched exe directly.
                bool useSteamUrl = Program.IsSteamPath(_gamePath);
                Logger.Info($"Launching KOTOR: {exePath} (steam-route={useSteamUrl})");

                if (useSteamUrl)
                {
                    var psi = new ProcessStartInfo("steam://run/32370") { UseShellExecute = true };
                    try
                    {
                        Process.Start(psi);
                        return;
                    }
                    catch (Exception steamEx)
                    {
                        Logger.Warning($"Could not launch via steam:// URL ({steamEx.Message}); falling back to swkotor.exe");
                    }
                }

                Process.Start(new ProcessStartInfo(exePath) { UseShellExecute = true, WorkingDirectory = _gamePath });
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to launch KOTOR", ex);
            }
        }
    }
}
