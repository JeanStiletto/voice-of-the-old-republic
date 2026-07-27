using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.Automation;

namespace KotorAccessibilityInstaller
{
    /// <summary>Result of a <see cref="TslrcmInstallForm"/> run.</summary>
    public enum TslrcmOutcome
    {
        /// <summary>User cancelled the download; nothing to clean up or report.</summary>
        Cancelled,

        /// <summary>Download or hash verification failed; installer exe deleted.</summary>
        DownloadFailed,

        /// <summary>
        /// Downloaded + verified, but no KOTOR 2 path was known so no silent
        /// install was attempted. Caller runs the wizard interactively.
        /// </summary>
        DownloadedOnly,

        /// <summary>
        /// Downloaded + verified, silent install attempted and failed. The exe
        /// is kept so the caller can offer the interactive wizard as fallback.
        /// </summary>
        SilentInstallFailed,

        /// <summary>Silent install completed and dialog.tlk changed as expected.</summary>
        SilentInstalled,
    }

    /// <summary>
    /// Downloads the TSLRCM installer exe from DeadlyStream (guest scrape via
    /// <see cref="DeadlyStreamClient"/>), verifies it against the pinned
    /// SHA-256 in <see cref="Config.TslrcmInstallerSha256"/> (fail-closed: a
    /// swapped or upstream-updated file routes the user to the manual browser
    /// download), then — when the KOTOR 2 folder is known — runs the installer
    /// silently with Inno Setup's <c>/VERYSILENT /DIR=...</c>. Silent install
    /// is the default because the TSLRCM wizard is English-only while this
    /// installer speaks the user's language.
    ///
    /// The silent run is verified by fingerprinting the game's
    /// <c>dialog.tlk</c> before and after: TSLRCM always replaces it, so an
    /// exit code 0 with an unchanged tlk is treated as failure rather than
    /// silently reporting success.
    ///
    /// Status changes are spoken via UIA notifications (same pattern as
    /// MainForm.UpdateStatus). Download progress announces every 25 %; the
    /// install phase announces a heartbeat every ~15 s. Cancel is only
    /// honoured during the download — aborting Inno mid-install would leave a
    /// half-written mod install, so the button is disabled for that phase.
    /// </summary>
    public class TslrcmInstallForm : Form
    {
        private Label _titleLabel;
        private Label _statusLabel;
        private ProgressBar _progressBar;
        private Button _cancelButton;

        private readonly string _k2GamePath;
        private readonly CancellationTokenSource _cts = new CancellationTokenSource();
        private bool _finished;
        private bool _installPhase;
        private int _lastPct = -1;
        private int _lastAnnouncedBucket;

        public TslrcmOutcome Outcome { get; private set; } = TslrcmOutcome.Cancelled;
        public string InstallerPath { get; private set; }
        public string FailureReason { get; private set; }

        /// <param name="k2GamePath">
        /// Detected KOTOR 2 install root, or null when detection failed — then
        /// the form stops after download + verify (<see cref="TslrcmOutcome.DownloadedOnly"/>).
        /// </param>
        public TslrcmInstallForm(string k2GamePath)
        {
            _k2GamePath = k2GamePath;
            InitializeComponents();
            Shown += async (s, e) => await RunAsync();
            FormClosing += (s, e) =>
            {
                if (_finished) return;
                if (_installPhase)
                {
                    // Closing mid-install would orphan a half-written TSLRCM
                    // install; refuse until the phase completes.
                    e.Cancel = true;
                    return;
                }
                Logger.Info("TSLRCM window closed; cancelling download");
                _cts.Cancel();
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(560, 220);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 12, FontStyle.Bold),
                Location = new Point(20, 15),
                Size = new Size(510, 28),
                TextAlign = ContentAlignment.MiddleCenter,
                Text = "TSLRCM"
            };

            _statusLabel = new Label
            {
                Location = new Point(20, 55),
                Size = new Size(510, 40),
                TextAlign = ContentAlignment.TopLeft,
                Text = InstallerLocale.Get("K2Tslrcm_Downloading")
            };

            _progressBar = new ProgressBar
            {
                Location = new Point(20, 100),
                Size = new Size(510, 25),
                Minimum = 0,
                Maximum = 100
            };

            _cancelButton = new Button
            {
                Location = new Point(390, 140),
                Size = new Size(140, 35),
                Text = InstallerLocale.Get("Main_CancelButton")
            };
            _cancelButton.Click += (s, e) =>
            {
                Logger.Info("TSLRCM download cancelled via button");
                _cts.Cancel();
            };

            Controls.AddRange(new Control[]
            {
                _titleLabel, _statusLabel, _progressBar, _cancelButton
            });

            CancelButton = _cancelButton;
        }

        private async Task RunAsync()
        {
            string dest = Path.Combine(Path.GetTempPath(), Config.TslrcmInstallerFileName);

            try
            {
                UpdateStatus(InstallerLocale.Get("K2Tslrcm_Downloading"), announce: true);

                using (var ds = new DeadlyStreamClient())
                {
                    await ds.DownloadFileAsync(
                        Config.TslrcmDownloadPageUrl, dest, OnProgress, _cts.Token);
                }

                UpdateStatus(InstallerLocale.Get("K2Tslrcm_Verifying"), announce: true);
                string hash = await Task.Run(() => ComputeSha256(dest));
                if (!hash.Equals(Config.TslrcmInstallerSha256, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        $"Downloaded file failed SHA-256 verification (got {hash}). " +
                        "The upstream file may have been updated or replaced; " +
                        "download manually or wait for an installer update.");
                }
                Logger.Info($"TSLRCM installer downloaded and verified: {dest}");
                InstallerPath = dest;
            }
            catch (OperationCanceledException)
            {
                Logger.Info("TSLRCM download cancelled");
                TryDelete(dest);
                Outcome = TslrcmOutcome.Cancelled;
                _finished = true;
                Close();
                return;
            }
            catch (Exception ex)
            {
                Logger.Error("TSLRCM download failed", ex);
                FailureReason = ex.Message;
                TryDelete(dest);
                Outcome = TslrcmOutcome.DownloadFailed;
                _finished = true;
                Close();
                return;
            }

            if (_k2GamePath == null)
            {
                Outcome = TslrcmOutcome.DownloadedOnly;
                _finished = true;
                Close();
                return;
            }

            try
            {
                await RunSilentInstallAsync(dest);
                Outcome = TslrcmOutcome.SilentInstalled;
            }
            catch (Exception ex)
            {
                // Keep the exe: the caller offers the interactive wizard next.
                Logger.Error("TSLRCM silent install failed", ex);
                FailureReason = ex.Message;
                Outcome = TslrcmOutcome.SilentInstallFailed;
            }

            _finished = true;
            Close();
        }

        private async Task RunSilentInstallAsync(string installerExe)
        {
            _installPhase = true;
            _cancelButton.Enabled = false;
            _progressBar.Style = ProgressBarStyle.Marquee; // duration unknown

            UpdateStatus(
                InstallerLocale.Format("K2Tslrcm_SilentInstalling_Format", _k2GamePath),
                announce: true);

            var tlkBefore = FingerprintDialogTlk();
            string log = Path.Combine(Path.GetTempPath(), "tslrcm_silent_install.log");

            // Standard Inno Setup 5 silent switches. /SP- skips the "This will
            // install..." prompt, /SUPPRESSMSGBOXES auto-answers script message
            // boxes, /DIR overrides the wizard's directory page.
            var psi = new ProcessStartInfo
            {
                FileName = installerExe,
                Arguments =
                    $"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- " +
                    $"/DIR=\"{_k2GamePath}\" /LOG=\"{log}\"",
                UseShellExecute = true
            };
            Logger.Info($"Running TSLRCM silently: {installerExe} {psi.Arguments}");

            using var proc = Process.Start(psi);
            if (proc == null)
                throw new InvalidOperationException("Could not start the TSLRCM installer process.");

            var started = Environment.TickCount64;
            long lastAnnounce = started;
            while (!proc.HasExited)
            {
                await Task.Delay(1000);
                long now = Environment.TickCount64;
                int elapsedSec = (int)((now - started) / 1000);
                bool announce = now - lastAnnounce >= 15000;
                if (announce) lastAnnounce = now;
                if (elapsedSec % 5 == 0 || announce)
                {
                    UpdateStatus(
                        InstallerLocale.Format("K2Tslrcm_SilentHeartbeat_Format", elapsedSec),
                        announce);
                }
            }

            if (proc.ExitCode != 0)
            {
                throw new InvalidOperationException(
                    $"TSLRCM setup exited with code {proc.ExitCode} (log: {log}).");
            }

            var tlkAfter = FingerprintDialogTlk();
            if (tlkBefore == tlkAfter)
            {
                throw new InvalidOperationException(
                    "TSLRCM setup exited with code 0 but dialog.tlk is unchanged — " +
                    $"the install may not have run (log: {log}).");
            }

            Logger.Info($"TSLRCM silent install complete; dialog.tlk {tlkBefore} -> {tlkAfter}");
        }

        /// <summary>
        /// Cheap change-detector for the game's dialog.tlk (size + mtime).
        /// TSLRCM always replaces the file, so this distinguishes a real
        /// install from a setup exe that exited 0 without doing anything.
        /// </summary>
        private string FingerprintDialogTlk()
        {
            try
            {
                var fi = new FileInfo(Path.Combine(_k2GamePath, "dialog.tlk"));
                if (!fi.Exists) return "missing";
                return $"{fi.Length}:{fi.LastWriteTimeUtc.Ticks}";
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not fingerprint dialog.tlk: {ex.Message}");
                return $"error:{Guid.NewGuid():N}"; // never equal — don't fail the install on probe errors
            }
        }

        /// <summary>
        /// Progress callback from the download stream (worker thread; fires per
        /// 64 KB chunk). Coalesce to whole-percent UI updates before marshaling
        /// to the UI thread.
        /// </summary>
        private void OnProgress(long done, long total)
        {
            long effectiveTotal = total > 0 ? total : Config.TslrcmInstallerSizeBytes;
            int pct = (int)Math.Min(100, done * 100 / Math.Max(1, effectiveTotal));
            if (pct == _lastPct) return;
            _lastPct = pct;

            long doneMb = done / (1024 * 1024);
            long totalMb = effectiveTotal / (1024 * 1024);

            try
            {
                BeginInvoke(new Action(() =>
                {
                    _progressBar.Value = pct;
                    // Announce quarters only; the label text still updates every
                    // percent for on-demand reading.
                    int bucket = pct / 25;
                    bool announce = bucket > _lastAnnouncedBucket;
                    if (announce) _lastAnnouncedBucket = bucket;
                    UpdateStatus(
                        InstallerLocale.Format("K2Tslrcm_Progress_Format", doneMb, totalMb),
                        announce);
                }));
            }
            catch (InvalidOperationException)
            {
                // Form handle torn down mid-close; nothing to update.
            }
        }

        private void UpdateStatus(string message, bool announce)
        {
            if (InvokeRequired)
            {
                Invoke(new Action(() => UpdateStatus(message, announce)));
                return;
            }
            _statusLabel.Text = message;
            if (!announce) return;

            // WinForms Labels are not UIA live regions; raise a notification so
            // NVDA / JAWS speak the update without the user navigating to it.
            try
            {
                _statusLabel.AccessibilityObject?.RaiseAutomationNotification(
                    AutomationNotificationKind.ActionCompleted,
                    AutomationNotificationProcessing.MostRecent,
                    message);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not raise automation notification: {ex.Message}");
            }
        }

        private static string ComputeSha256(string path)
        {
            using var sha = SHA256.Create();
            using var fs = File.OpenRead(path);
            return Convert.ToHexString(sha.ComputeHash(fs));
        }

        private static void TryDelete(string path)
        {
            try { if (File.Exists(path)) File.Delete(path); }
            catch (Exception ex) { Logger.Warning($"Could not delete {path}: {ex.Message}"); }
        }
    }
}
