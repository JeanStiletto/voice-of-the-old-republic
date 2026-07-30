using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Fetches the localized TSLRCM <c>dialog.tlk</c> from the TSLRCM team's
    /// official per-language Steam Workshop items and copies it into the
    /// KOTOR 2 folder over the English one the DeadlyStream installer left.
    ///
    /// Why this shape: the DeadlyStream TSLRCM 1.8.6 exe is English-only
    /// (verified by extraction — see docs/installer.md), the localized
    /// editions exist ONLY as Workshop items, and those are UGC-depot hosted
    /// (GetPublishedFileDetails returns an empty file_url), so there is no
    /// anonymous direct download. But every affected user owns KOTOR 2 on
    /// Steam by definition, so we automate the community-endorsed route:
    /// open the Workshop page (the user presses Subscribe — the one manual
    /// step, in Steam's own UI), poll the library's
    /// <c>workshop/content/&lt;app&gt;/&lt;item&gt;/</c> folder for
    /// dialog.tlk, wait for the size to go stable, verify the language via
    /// <see cref="GameLocaleDetector"/> (content probe included, so Russian
    /// works despite its English language ID), back up the English tlk as
    /// <c>dialog.tlk.english.bak</c>, and copy the localized one in. The
    /// completion message tells the user to unsubscribe again so the
    /// Workshop copy never coexists with the directory-installed mods.
    ///
    /// MUST run after TSLRCM but BEFORE K2CP / Tweak Pack: those append
    /// strings to dialog.tlk, and replacing the file afterwards would orphan
    /// their strrefs.
    /// </summary>
    public class WorkshopTlkHarvestForm : Form
    {
        // Official TSLRCM language editions on the Steam Workshop (app 208580),
        // maintained by the TSLRCM team; the German item is author-confirmed
        // at 1.8.6 (2025 comment). IDs verified 2026-07-27.
        public static bool TryGetWorkshopItem(GameLocale locale, out string itemId)
        {
            switch (locale)
            {
                case GameLocale.German: itemId = "485551190"; return true;
                case GameLocale.French: itemId = "485553656"; return true;
                case GameLocale.Italian: itemId = "485556965"; return true;
                case GameLocale.Spanish: itemId = "485555217"; return true;
                case GameLocale.Russian: itemId = "2143250983"; return true;
                default: itemId = null; return false;
            }
        }

        // Give the user ample time to find the Subscribe button and Steam time
        // to pull ~335 MB; the Cancel button is live the whole way.
        private static readonly TimeSpan OverallTimeout = TimeSpan.FromMinutes(20);

        private Label _titleLabel;
        private Label _statusLabel;
        private ProgressBar _progressBar;
        private Button _cancelButton;

        private readonly string _k2GamePath;
        private readonly GameLocale _expectedLocale;
        private readonly string _itemId;
        private readonly CancellationTokenSource _cts = new CancellationTokenSource();
        private bool _finished;

        public bool Success { get; private set; }

        /// <summary>Failure detail; null when the user cancelled or timed out silently.</summary>
        public string FailureReason { get; private set; }

        public WorkshopTlkHarvestForm(string k2GamePath, GameLocale expectedLocale, string itemId)
        {
            _k2GamePath = k2GamePath ?? throw new ArgumentNullException(nameof(k2GamePath));
            _expectedLocale = expectedLocale;
            _itemId = itemId ?? throw new ArgumentNullException(nameof(itemId));
            InitializeComponents();
            Shown += async (s, e) => await RunAsync();
            FormClosing += (s, e) =>
            {
                if (_finished) return;
                Logger.Info("Workshop tlk harvest window closed; cancelling");
                _cts.Cancel();
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(560, 200);
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
                Location = new Point(20, 50),
                Size = new Size(510, 55),
                TextAlign = ContentAlignment.TopLeft,
                Text = InstallerLocale.Get("K2Lang_Waiting")
            };

            _progressBar = new ProgressBar
            {
                Location = new Point(20, 110),
                Size = new Size(510, 20),
                Style = ProgressBarStyle.Marquee
            };
            _progressBar.AccessibleName = Text;

            _cancelButton = new Button
            {
                Location = new Point(390, 135),
                Size = new Size(140, 30),
                Text = InstallerLocale.Get("Main_CancelButton")
            };
            _cancelButton.Click += (s, e) =>
            {
                Logger.Info("Workshop tlk harvest cancelled via button");
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
            try
            {
                // <lib>/steamapps/common/<game> -> <lib>/steamapps/workshop/content/<app>/<item>
                string itemDir = Path.GetFullPath(Path.Combine(
                    _k2GamePath, "..", "..", "workshop", "content",
                    Config.Kotor2WorkshopAppId, _itemId));
                Logger.Info($"Workshop tlk harvest: expecting content under {itemDir}");

                string pageUrl = $"steam://url/CommunityFilePage/{_itemId}";
                Logger.Info($"Opening Workshop page: {pageUrl}");
                Process.Start(new ProcessStartInfo { FileName = pageUrl, UseShellExecute = true });

                UpdateStatus(InstallerLocale.Get("K2Lang_Waiting"), announce: true);

                string tlkPath = await WaitForWorkshopTlkAsync(itemDir);
                if (tlkPath == null) return; // cancelled or timed out; reason already set

                UpdateStatus(InstallerLocale.Get("K2Tslrcm_Verifying"), announce: true);

                // Reuse the installer's locale detector on the tlk's own folder:
                // covers the header ID for DE/FR/IT/ES and the CP1251 content
                // probe for Russian (whose tlk declares the English ID).
                var detected = GameLocaleDetector.Detect(Path.GetDirectoryName(tlkPath));
                if (detected != _expectedLocale)
                {
                    FailureReason =
                        $"The Workshop item's dialog.tlk reads as {detected}, expected {_expectedLocale}. " +
                        "Not installed.";
                    Logger.Warning($"Workshop tlk harvest: {FailureReason}");
                    return;
                }

                string gameTlk = Path.Combine(_k2GamePath, "dialog.tlk");
                string backup = Path.Combine(_k2GamePath, "dialog.tlk.english.bak");
                File.Copy(gameTlk, backup, overwrite: true);
                File.Copy(tlkPath, gameTlk, overwrite: true);
                Logger.Info($"Workshop tlk harvest: {detected} dialog.tlk installed " +
                            $"({new FileInfo(gameTlk).Length} bytes); English backup at {backup}");
                Success = true;
            }
            catch (Exception ex)
            {
                Logger.Error("Workshop tlk harvest failed", ex);
                FailureReason = ex.Message;
            }
            finally
            {
                _finished = true;
                Close();
            }
        }

        /// <summary>
        /// Polls the workshop item folder for dialog.tlk until it exists with a
        /// stable size (Steam finished writing it). Returns null on cancel
        /// (FailureReason stays null) or timeout (FailureReason set).
        /// </summary>
        private async Task<string> WaitForWorkshopTlkAsync(string itemDir)
        {
            long started = Environment.TickCount64;
            long lastAnnounce = started;
            long lastSize = -1;
            string candidate = null;

            while (true)
            {
                if (_cts.IsCancellationRequested)
                {
                    Logger.Info("Workshop tlk harvest cancelled while waiting");
                    return null;
                }

                long now = Environment.TickCount64;
                if (now - started > OverallTimeout.TotalMilliseconds)
                {
                    FailureReason = "Timed out waiting for the Steam Workshop download " +
                                    $"({(int)OverallTimeout.TotalMinutes} minutes).";
                    return null;
                }

                try
                {
                    candidate = Directory.Exists(itemDir)
                        ? Directory.EnumerateFiles(itemDir, "dialog.tlk", SearchOption.AllDirectories)
                            .FirstOrDefault()
                        : null;
                }
                catch (Exception ex)
                {
                    Logger.Warning($"Workshop poll error: {ex.Message}");
                    candidate = null;
                }

                if (candidate != null)
                {
                    long size = new FileInfo(candidate).Length;
                    // Two consecutive polls with the same non-trivial size =
                    // Steam is done writing. The vanilla-scale tlk is ~10 MB;
                    // 1 MB screens out a partially-written header.
                    if (size > 1024 * 1024 && size == lastSize)
                    {
                        Logger.Info($"Workshop tlk found: {candidate} ({size} bytes, stable)");
                        return candidate;
                    }
                    lastSize = size;
                }

                if (now - lastAnnounce >= 15000)
                {
                    lastAnnounce = now;
                    int elapsedSec = (int)((now - started) / 1000);
                    UpdateStatus(
                        InstallerLocale.Format("K2Lang_WaitingHeartbeat_Format", elapsedSec),
                        announce: true);
                }

                try { await Task.Delay(2000, _cts.Token); }
                catch (OperationCanceledException) { return null; }
            }
        }

        private void UpdateStatus(string message, bool announce)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action(() => UpdateStatus(message, announce)));
                return;
            }
            _statusLabel.Text = message;
            if (!announce) return;

            ScreenReaderAnnouncer.Announce(_statusLabel, message);
        }
    }
}
