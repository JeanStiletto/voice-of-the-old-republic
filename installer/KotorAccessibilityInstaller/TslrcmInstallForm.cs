using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>Result of a <see cref="TslrcmInstallForm"/> run.</summary>
    public enum TslrcmOutcome
    {
        /// <summary>User cancelled the download; nothing to clean up or report.</summary>
        Cancelled,

        /// <summary>The download itself failed — network, or the scrape broke.</summary>
        DownloadFailed,

        /// <summary>
        /// Downloaded fine but the SHA-256 did not match the pin, so it was not
        /// run and was deleted. Kept distinct from <see cref="DownloadFailed"/>
        /// because it means something different and needs a different sentence:
        /// nothing is broken, TSLRCM has almost certainly just shipped an update
        /// this installer predates. <c>FailureReason</c> carries the hash we got.
        /// </summary>
        VerificationFailed,

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
        private Label _eventLogLabel;
        private EventLogView _eventLog;
        private Button _cancelButton;

        private readonly string _k2GamePath;
        /// <summary>
        /// Installer exe supplied by the user via <see cref="ManualDownloadForm"/>,
        /// or null for the normal download path. When set, the download and hash
        /// steps are skipped: the user fetched this file themselves from the
        /// official page, so their judgment stands in for the pin (see that
        /// class for the reasoning).
        /// </summary>
        private readonly string _suppliedInstallerPath;
        private readonly CancellationTokenSource _cts = new CancellationTokenSource();
        private bool _finished;
        private bool _installPhase;
        private int _lastPct = -1;
        private int _lastAnnouncedBucket;

        public TslrcmOutcome Outcome { get; private set; } = TslrcmOutcome.Cancelled;
        public string InstallerPath { get; private set; }
        public string FailureReason { get; private set; }

        /// <summary>
        /// True when Setup succeeded but changed nothing, i.e. TSLRCM was
        /// already there. The install counts as successful either way; the flag
        /// exists so the summary can say "already installed" rather than
        /// claiming a fresh install the user did not get.
        /// </summary>
        public bool AlreadyInstalled { get; private set; }

        /// <param name="k2GamePath">
        /// Detected KOTOR 2 install root, or null when detection failed — then
        /// the form stops after download + verify (<see cref="TslrcmOutcome.DownloadedOnly"/>).
        /// </param>
        /// <param name="suppliedInstallerPath">
        /// A TSLRCM installer the user downloaded themselves. When non-null the
        /// form goes straight to the silent install.
        /// </param>
        public TslrcmInstallForm(string k2GamePath, string suppliedInstallerPath = null)
        {
            _k2GamePath = k2GamePath;
            _suppliedInstallerPath = suppliedInstallerPath;
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
            Size = new Size(560, 385);
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
            _progressBar.AccessibleName = Text;

            _eventLogLabel = new Label
            {
                Text = InstallerLocale.Get("EventLog_Label"),
                Location = new Point(20, 133),
                Size = new Size(510, 18)
            };

            _eventLog = new EventLogView
            {
                Location = new Point(20, 153),
                Size = new Size(510, 125),
                AccessibleName = InstallerLocale.Get("EventLog_Label")
            };

            _cancelButton = new Button
            {
                Location = new Point(390, 290),
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
                _titleLabel, _statusLabel, _progressBar,
                _eventLogLabel, _eventLog, _cancelButton
            });

            CancelButton = _cancelButton;
        }

        private async Task RunAsync()
        {
            string dest = _suppliedInstallerPath
                          ?? Path.Combine(Path.GetTempPath(), Config.TslrcmInstallerFileName);

            if (_suppliedInstallerPath != null)
            {
                // User-supplied: skip download AND hash. They fetched it from
                // the official page themselves, which is the provenance the pin
                // exists to substitute for — see ManualDownloadForm.
                Logger.Info($"TSLRCM: using the user-supplied installer at {_suppliedInstallerPath}");
                InstallerPath = _suppliedInstallerPath;
            }
            else
            {
                try
                {
                    UpdateStatus(InstallerLocale.Get("K2Tslrcm_Downloading"), announce: true);

                    using (var ds = new DeadlyStreamClient())
                    {
                        await ds.DownloadFileAsync(
                            Config.TslrcmDownloadPageUrl, dest, OnProgress, _cts.Token);
                    }

                    UpdateStatus(InstallerLocale.Get("K2Tslrcm_Verifying"), announce: true);
                    string hash = await Task.Run(() => DeadlyStreamClient.ComputeSha256(dest));
                    if (!hash.Equals(Config.TslrcmInstallerSha256, StringComparison.OrdinalIgnoreCase))
                    {
                        // Reported separately from a download failure: this
                        // almost always means TSLRCM shipped an update, not that
                        // anything is broken, and the user gets a different
                        // sentence and a working way forward.
                        Logger.Warning($"TSLRCM hash mismatch: expected {Config.TslrcmInstallerSha256}, got {hash}");
                        FailureReason = hash;
                        TryDelete(dest);
                        Outcome = TslrcmOutcome.VerificationFailed;
                        _finished = true;
                        Close();
                        return;
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
                        announce,
                        logKey: "silent-install");
                }
            }

            if (proc.ExitCode != 0)
            {
                throw new InvalidOperationException(
                    $"TSLRCM setup exited with code {proc.ExitCode} (log: {log}).");
            }

            var tlkAfter = FingerprintDialogTlk();
            if (tlkBefore != tlkAfter)
            {
                Logger.Info($"TSLRCM silent install complete; dialog.tlk {tlkBefore} -> {tlkAfter}");
                return;
            }

            // dialog.tlk did not change. That has two very different causes, and
            // treating them alike reported a perfectly good install as failed:
            //
            //   a) Setup did nothing (the thing this check exists to catch).
            //   b) TSLRCM was ALREADY installed, so Setup rewrote the same file
            //      byte for byte. Re-running the installer over an existing
            //      install is a normal thing to do and it is not an error.
            //
            // Inno's own log distinguishes them: it ends with "Installation
            // process succeeded." only when it actually ran the file entries.
            // That is direct evidence about what Setup did, where the tlk
            // fingerprint is only an inference from one of its side effects, so
            // it wins when the two disagree.
            if (InnoLogReportsSuccess(log))
            {
                Logger.Info("TSLRCM silent install: dialog.tlk unchanged, but Setup's own log reports " +
                            "success — TSLRCM was already installed and has been reinstalled in place.");
                AlreadyInstalled = true;
                return;
            }

            throw new InvalidOperationException(
                "TSLRCM setup exited with code 0 but dialog.tlk is unchanged and its own log does " +
                $"not report success — the install did not run (log: {log}).");
        }

        /// <summary>
        /// Cheap change-detector for the game's dialog.tlk (size + mtime).
        /// TSLRCM always replaces the file, so this distinguishes a real
        /// install from a setup exe that exited 0 without doing anything.
        /// </summary>
        /// <summary>
        /// Whether Inno's log ends with its success line. Read as UTF-8 with a
        /// permissive fallback — the log is Inno's, not ours, and an encoding
        /// surprise must not turn into a failed install.
        /// </summary>
        private static bool InnoLogReportsSuccess(string logPath)
        {
            try
            {
                if (!File.Exists(logPath)) return false;
                string text = File.ReadAllText(logPath);
                return text.Contains("Installation process succeeded", StringComparison.OrdinalIgnoreCase);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not read Setup's log at {logPath}: {ex.Message}");
                return false;
            }
        }

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
                        announce,
                        logKey: "download");
                }));
            }
            catch (InvalidOperationException)
            {
                // Form handle torn down mid-close; nothing to update.
            }
        }

        // logKey groups repeating lines onto ONE row in the event log (see
        // EventLogView). The download reports every percent and the silent
        // install every five seconds, so both pass a key; the one-shot step
        // messages pass none and stand as their own entries.
        //
        // The log is written even when announce is false: not interrupting the
        // user on every percent is the right call for speech, but the history is
        // exactly where that detail belongs.
        private void UpdateStatus(string message, bool announce, string logKey = null)
        {
            if (InvokeRequired)
            {
                Invoke(new Action(() => UpdateStatus(message, announce, logKey)));
                return;
            }
            _statusLabel.Text = message;
            _eventLog?.Report(message, logKey);
            if (!announce) return;

            ScreenReaderAnnouncer.Announce(_statusLabel, message);
        }

        private static void TryDelete(string path)
        {
            try { if (File.Exists(path)) File.Delete(path); }
            catch (Exception ex) { Logger.Warning($"Could not delete {path}: {ex.Message}"); }
        }
    }
}
