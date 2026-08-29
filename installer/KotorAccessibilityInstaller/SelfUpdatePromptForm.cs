using System;
using System.Drawing;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>What happened on the self-update screen.</summary>
    public enum SelfUpdateChoice
    {
        /// <summary>Carry on with the EXE that is running.</summary>
        Skip,

        /// <summary>The new EXE is downloaded and verified; the caller must start it.</summary>
        Restart,
    }

    /// <summary>
    /// Offers the installer's own update, and carries it out.
    ///
    /// <para>It comes BEFORE every other screen on purpose. A self-update ends in
    /// a restart, so asking later would mean discarding choices the user had
    /// already made, and a window that vanishes and comes back mid-install is
    /// exactly the sort of thing that cannot be followed by ear. It is also the
    /// cheapest possible check — a three-line text file — so a user with nothing
    /// to update pays almost nothing for it.</para>
    ///
    /// <para>The recommendation is stated, not enforced. "Continue with the
    /// current version" stays available and stays a real answer — a failed
    /// self-update must never be the reason somebody cannot install the mod — but
    /// the update is the default button, holds the focus, and the body text says
    /// plainly why it is the better choice and how large the download is.</para>
    ///
    /// <para>The download runs on this window rather than in a form of its own: a
    /// second window would cost the user a focus change in each direction.
    /// Progress is announced at ten-percent steps rather than every tick, which
    /// at 85 MB would be a wall of speech nobody can talk over.</para>
    /// </summary>
    public class SelfUpdatePromptForm : Form
    {
        public SelfUpdateChoice Choice { get; private set; } = SelfUpdateChoice.Skip;

        /// <summary>Verified staged EXE, set once <see cref="Choice"/> is Restart.</summary>
        public string StagedExe { get; private set; }

        private readonly SelfUpdateInfo _info;
        private readonly CancellationTokenSource _cancel = new CancellationTokenSource();
        private bool _working;

        private Label _titleLabel;
        private Label _infoLabel;
        private Label _statusLabel;
        private Button _updateButton;
        private Button _skipButton;

        public SelfUpdatePromptForm(SelfUpdateInfo info)
        {
            _info = info;
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Text = InstallerLocale.Get("SelfUpdate_Title") + " " + InstallerVersion.Display();
            Size = new Size(600, 400);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Text = InstallerLocale.Get("SelfUpdate_Heading"),
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(545, 30),
            };

            _infoLabel = new Label
            {
                Text = InstallerLocale.Format("SelfUpdate_Body_Format",
                                              _info.RunningVersion, _info.Version, _info.SizeMb),
                Location = new Point(20, 58),
                Size = new Size(545, 150),
            };

            // Empty until something is happening. A label that says "ready"
            // before the user has done anything is one more line to read past.
            _statusLabel = new Label
            {
                Text = string.Empty,
                Location = new Point(20, 212),
                Size = new Size(545, 40),
                AccessibleName = InstallerLocale.Get("SelfUpdate_StatusAcc"),
            };

            int y = 262, w = 545, h = 38, gap = 8, x = 20;

            _updateButton = new Button
            {
                Text = InstallerLocale.Get("SelfUpdate_UpdateButton"),
                Location = new Point(x, y),
                Size = new Size(w, h),
                AccessibleName = InstallerLocale.Format("SelfUpdate_UpdateAcc_Format", _info.Version),
            };
            _updateButton.Click += OnUpdate;
            y += h + gap;

            _skipButton = new Button
            {
                Text = InstallerLocale.Get("SelfUpdate_SkipButton"),
                Location = new Point(x, y),
                Size = new Size(w, h),
                AccessibleName = InstallerLocale.Get("SelfUpdate_SkipAcc"),
            };
            _skipButton.Click += OnSkip;

            Controls.AddRange(new Control[] { _titleLabel, _infoLabel, _statusLabel, _updateButton, _skipButton });

            AcceptButton = _updateButton;
            CancelButton = _skipButton;
            ActiveControl = _updateButton;

            string announce = $"{_titleLabel.Text}. {_infoLabel.Text}";
            AccessibleDescription = announce;
            _updateButton.AccessibleDescription = announce;
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            ScreenReaderAnnouncer.Announce(this, $"{_titleLabel.Text}. {_infoLabel.Text}");
        }

        private void OnSkip(object sender, EventArgs e)
        {
            // Doubles as the way out of a download that is hanging: the button
            // keeps its place and its meaning, and the run continues on the
            // version that is already here.
            if (_working)
            {
                Logger.Info("User cancelled the installer self-update download.");
                _cancel.Cancel();
                return;
            }

            Logger.Info($"User skipped the installer self-update to {_info.Version}.");
            Choice = SelfUpdateChoice.Skip;
            Close();
        }

        private async void OnUpdate(object sender, EventArgs e)
        {
            if (_working) return;
            _working = true;
            _updateButton.Enabled = false;
            _skipButton.Text = InstallerLocale.Get("SelfUpdate_CancelButton");
            _skipButton.AccessibleName = InstallerLocale.Get("SelfUpdate_CancelAcc");
            SetStatus(InstallerLocale.Get("SelfUpdate_Working"), announce: true);

            try
            {
                int lastAnnounced = -1;
                string staged = await SelfUpdater.DownloadAsync(_info, pct =>
                {
                    // Marshalled back onto the UI thread: the progress callback
                    // fires from the download's IO continuations, and a UIA
                    // notification raised off-thread reaches nobody.
                    if (IsDisposed || !IsHandleCreated) return;
                    BeginInvoke((Action)(() =>
                    {
                        int step = pct / 10 * 10;
                        bool speak = step > lastAnnounced;
                        if (speak) lastAnnounced = step;
                        SetStatus(InstallerLocale.Format("SelfUpdate_Progress_Format", pct), speak);
                    }));
                }, _cancel.Token);

                SetStatus(InstallerLocale.Format("SelfUpdate_Restarting_Format", _info.Version), announce: true);
                StagedExe = staged;
                Choice = SelfUpdateChoice.Restart;

                // Let the announcement actually leave the building before the
                // window closes and the process ends behind it.
                await Task.Delay(700);
                Close();
                return;
            }
            catch (OperationCanceledException)
            {
                Logger.Info("Installer self-update cancelled by the user.");
                // Clear the flag FIRST: OnFormClosing holds the window open
                // while a transfer is live, and this is the transfer having
                // finished unwinding. Leaving it set would refuse our own Close.
                _working = false;
                Choice = SelfUpdateChoice.Skip;
                Close();
                return;
            }
            catch (Exception ex)
            {
                Logger.Error("Installer self-update failed", ex);
                _working = false;
                _updateButton.Enabled = true;
                _skipButton.Text = InstallerLocale.Get("SelfUpdate_SkipButton");
                _skipButton.AccessibleName = InstallerLocale.Get("SelfUpdate_SkipAcc");
                SetStatus(string.Empty, announce: false);

                // A box, not just an announcement: this is a failure the user may
                // want to read twice, and an announcement cannot be re-read. It
                // also says what still works, because nothing about this failure
                // stops the mod itself from being installed.
                MessageBox.Show(this,
                    InstallerLocale.Format("SelfUpdate_Failed_Format", ex.Message, _info.RunningVersion),
                    InstallerLocale.Get("SelfUpdate_FailedTitle"),
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);

                ActiveControl = _skipButton;
            }
        }

        private void SetStatus(string text, bool announce)
        {
            _statusLabel.Text = text;
            if (announce && !string.IsNullOrEmpty(text))
                ScreenReaderAnnouncer.Announce(this, text);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            // Closing mid-download is a cancel, not a crash. Hold the window open
            // for the moment the transfer needs to unwind (cancellation is
            // honoured per buffer, so that is milliseconds) and let the cancelled
            // path close it. Closing here instead would let the caller walk on
            // while a continuation still had a half-written file in hand.
            if (_working && Choice != SelfUpdateChoice.Restart)
            {
                _cancel.Cancel();
                Choice = SelfUpdateChoice.Skip;
                e.Cancel = true;
                return;
            }
            base.OnFormClosing(e);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _cancel.Dispose();
            base.Dispose(disposing);
        }
    }
}
