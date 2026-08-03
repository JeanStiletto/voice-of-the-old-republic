using System;
using System.Collections.Generic;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;
using KotorAccessibilityInstaller.ModInstallers;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Runs the KOTOR 2 mod pipeline (K2CP → Tweak Pack, see
    /// <see cref="ModInstallerCoordinator.BuildKotor2Pipeline"/>) against the
    /// KOTOR 2 install dir with a progress bar and spoken status updates.
    /// Downloads HoloPatcher first (same provider the KOTOR 1 flow uses).
    ///
    /// The caller is responsible for the TSLRCM-first gate — this form assumes
    /// TSLRCM is already in place. No cancel button: aborting HoloPatcher
    /// mid-write would leave a half-patched Override; each run is bounded by
    /// HoloPatcherRunner's 10-minute timeout, so the form always terminates.
    /// </summary>
    public class Kotor2ModsInstallForm : Form
    {
        private Label _titleLabel;
        private Label _statusLabel;
        private ProgressBar _progressBar;

        private readonly string _k2GamePath;
        private readonly ModSelection _selection;
        private bool _finished;

        /// <summary>Per-mod results, in pipeline order. Empty if the run never started.</summary>
        public List<ModInstallResult> Results { get; private set; } = new List<ModInstallResult>();

        public Kotor2ModsInstallForm(string k2GamePath, ModSelection selection)
        {
            _k2GamePath = k2GamePath ?? throw new ArgumentNullException(nameof(k2GamePath));
            _selection = selection ?? throw new ArgumentNullException(nameof(selection));
            InitializeComponents();
            Shown += async (s, e) => await RunAsync();
            FormClosing += (s, e) =>
            {
                // Refuse to close mid-install (see class docs); the pipeline's
                // timeouts guarantee _finished eventually flips.
                if (!_finished) e.Cancel = true;
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(560, 190);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            ControlBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 12, FontStyle.Bold),
                Location = new Point(20, 15),
                Size = new Size(510, 28),
                TextAlign = ContentAlignment.MiddleCenter,
                Text = InstallerLocale.Get("K2Mods_Title")
            };

            _statusLabel = new Label
            {
                Location = new Point(20, 55),
                Size = new Size(510, 40),
                TextAlign = ContentAlignment.TopLeft,
                Text = InstallerLocale.Get("Main_StatusInstallingMods")
            };

            _progressBar = new ProgressBar
            {
                Location = new Point(20, 100),
                Size = new Size(510, 25),
                Minimum = 0,
                Maximum = 100
            };
            _progressBar.AccessibleName = Text;

            Controls.AddRange(new Control[] { _titleLabel, _statusLabel, _progressBar });
        }

        private async Task RunAsync()
        {
            string holoPatcherPath = null;
            try
            {
                UpdateStatus(InstallerLocale.Get("Main_StatusDownloadingHoloPatcher"));
                holoPatcherPath = await HoloPatcherProvider.ProvideAsync(
                    p => UpdateProgress(p / 10)); // 0..10 covers staging the driver
                // A null holoPatcherPath is not fatal here: each installer
                // reports "HoloPatcher.exe not available" per-mod, which lands
                // in the summary with a clear cause.

                UpdateStatus(InstallerLocale.Get("Main_StatusInstallingMods"));
                Results = await ModInstallerCoordinator.InstallSelectedAsync(
                    ModInstallerCoordinator.BuildKotor2Pipeline(),
                    _selection,
                    _k2GamePath,
                    holoPatcherPath,
                    p => UpdateProgress(10 + p * 90 / 100),
                    msg => UpdateStatus(msg),
                    AskForManualDownload);

                UpdateProgress(100);
            }
            catch (Exception ex)
            {
                Logger.Error("KOTOR 2 mods install failed", ex);
                Results.Add(ModInstallResult.Fail("k2mods", ex.Message));
            }
            finally
            {
                HoloPatcherProvider.Cleanup(holoPatcherPath);
            }

            _finished = true;
            Close();
        }

        /// <summary>
        /// Host side of <see cref="ModInstallContext.AskForManualDownload"/>:
        /// shows the dialog and returns what the user supplied. Marshalled onto
        /// the UI thread because installers call it from their async bodies.
        ///
        /// <para>Shown as a modal child of this form rather than via
        /// Application.Run — this form owns the message loop already, and a
        /// second one would deadlock.</para>
        /// </summary>
        private string AskForManualDownload(ManualDownloadRequest request)
        {
            if (InvokeRequired)
                return (string)Invoke(new Func<ManualDownloadRequest, string>(AskForManualDownload), request);

            using var dialog = new ManualDownloadForm(
                request.ModDisplayName,
                request.Reason,
                request.PageUrl,
                request.ExpectedFileName,
                request.Kind,
                request.PinnedSha256);
            dialog.ShowDialog(this);
            return dialog.SelectedFilePath;
        }

        private void UpdateProgress(int value)
        {
            if (InvokeRequired) { BeginInvoke(new Action(() => UpdateProgress(value))); return; }
            _progressBar.Value = Math.Max(0, Math.Min(100, value));
        }

        private void UpdateStatus(string message)
        {
            if (InvokeRequired) { BeginInvoke(new Action(() => UpdateStatus(message))); return; }
            _statusLabel.Text = message;
            Logger.Info(message);

            ScreenReaderAnnouncer.Announce(_statusLabel, message);
        }
    }
}
