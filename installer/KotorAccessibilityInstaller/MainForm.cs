using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class MainForm : Form
    {
        private string _gamePath;
        private readonly bool _updateOnly;
        private readonly string _language;
        private readonly ModSelection _modSelection;
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

        public MainForm(string detectedGamePath, bool updateOnly = false, string language = null, ModSelection modSelection = null)
        {
            _gamePath = detectedGamePath;
            _updateOnly = updateOnly;
            _language = language;
            // Null modSelection happens on the update-only path (we don't re-prompt for
            // optional mods on a kpatch update). Treat as "skip optional installs".
            _modSelection = modSelection;
            InitializeComponents();
            Logger.Info($"MainForm initialized (updateOnly: {updateOnly}, language: {language ?? "none"}, modSelection: {modSelection?.ToString() ?? "none"})");
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
                Checked = false
            };

            _readmeCheckBox = new CheckBox
            {
                Text = InstallerLocale.Get("Main_ReadmeCheckBox"),
                Location = new Point(20, 255),
                Size = new Size(400, 25),
                Checked = true
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

            string confirmMessage = InstallerLocale.Format(
                _updateOnly ? "Main_ConfirmUpdate_Format" : "Main_ConfirmInstall_Format", _gamePath);
            string confirmTitle = InstallerLocale.Get(
                _updateOnly ? "Main_ConfirmUpdate_Title" : "Main_ConfirmInstall_Title");

            var result = MessageBox.Show(confirmMessage, confirmTitle, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (result != DialogResult.Yes) return;

            SetControlsEnabled(false);
            _progressBar.Visible = true;
            _progressBar.Value = 0;

            string stagingRoot = null;
            string downloadedKpatch = null;
            string latestVersion = null;
            var installationManager = new InstallationManager(_gamePath);

            using var githubClient = new GitHubClient();

            try
            {
                Logger.Info($"Starting {(_updateOnly ? "update" : "installation")} to: {_gamePath}");

                // Step 1: download .kpatch from GitHub releases
                UpdateStatus(InstallerLocale.Get("Main_StatusDownloading"));
                UpdateProgress(5);

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

                // Step 4: drop Prism speech runtime alongside accessibility.dll
                UpdateStatus(InstallerLocale.Get("Main_StatusCopyingPrism"));
                UpdateProgress(85);
                await Task.Run(() => installationManager.InstallPrismRuntime());

                // Step 4.5: apply community-recommended stability tweaks to swkotor.ini
                // (V-Sync, Frame Buffer, Disable Vertex Buffer Objects, FullScreen).
                // Best-effort: a failure here doesn't roll back the install — the mod
                // still works without these tweaks, the user just doesn't get the
                // stability boost.
                UpdateStatus(InstallerLocale.Get("Main_StatusTweakingIni"));
                UpdateProgress(90);
                var iniResult = await Task.Run(() => SwkotorIniTweaker.ApplyAccessibilityDefaults(_gamePath));
                if (!iniResult.Success)
                {
                    Logger.Warning($"Stability tweaks failed: {iniResult.Error}");
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

                MessageBox.Show(
                    completionMessage,
                    InstallerLocale.Get(_updateOnly ? "Main_CompleteUpdate_Title" : "Main_CompleteInstall_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);

                Logger.AskAndSave();

                if (_readmeCheckBox.Checked) OpenReadme();
                if (_launchCheckBox.Checked) LaunchGame();

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

                if (Logger.AskAndSave(alwaysAsk: true)) Logger.OpenLogFile();

                SetControlsEnabled(true);
                _progressBar.Visible = false;
            }
            finally
            {
                if (downloadedKpatch != null)
                {
                    try { File.Delete(downloadedKpatch); } catch { }
                }
                if (stagingRoot != null)
                    installationManager.CleanupStaging(stagingRoot);
            }
        }

        private void UpdateStatus(string message)
        {
            if (InvokeRequired) { Invoke(new Action(() => UpdateStatus(message))); return; }
            _statusLabel.Text = message;
            _statusLabel.ForeColor = SystemColors.ControlText;
            Logger.Info(message);
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

                Logger.Info($"Launching KOTOR: {exePath}");
                // Launch via Steam URL so Steam's overlay + cloud saves stay wired up,
                // and (importantly) the game inherits the user's non-elevated token
                // rather than the installer's admin token.
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

                Process.Start(new ProcessStartInfo(exePath) { UseShellExecute = true, WorkingDirectory = _gamePath });
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to launch KOTOR", ex);
            }
        }
    }
}
