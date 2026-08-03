using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class UninstallForm : Form
    {
        private readonly GameTarget _target;
        private readonly string _gamePath;
        private Label _titleLabel;
        private Label _statusLabel;
        private Label _pathLabel;
        private Button _uninstallButton;
        private Button _cancelButton;
        private ProgressBar _progressBar;

        public UninstallForm(GameTarget target, string gamePath)
        {
            _target = target;
            _gamePath = gamePath;
            InitializeComponents();
            Logger.Info($"UninstallForm initialized ({target.DisplayName})");
        }

        private void InitializeComponents()
        {
            // Which game is being removed has to be in the title: with both
            // installed there are two entries in Add/Remove Programs and no
            // other way to tell the resulting dialogs apart.
            Text = InstallerLocale.Get("Uninstall_Title") + " — " + _target.DisplayName;
            Size = new Size(490, 260);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Text = InstallerLocale.Get("Uninstall_Heading") + " — " + _target.DisplayName,
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(440, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            _statusLabel = new Label
            {
                Text = InstallerLocale.Get("Uninstall_Description"),
                Location = new Point(20, 60),
                Size = new Size(440, 50),
                TextAlign = ContentAlignment.TopLeft
            };

            _pathLabel = new Label
            {
                Text = InstallerLocale.Format("Uninstall_PathLabel_Format", _gamePath),
                Location = new Point(20, 115),
                Size = new Size(440, 20),
                ForeColor = SystemColors.GrayText
            };

            _progressBar = new ProgressBar
            {
                Location = new Point(20, 145),
                Size = new Size(440, 25),
                Style = ProgressBarStyle.Continuous,
                Visible = false
            };

            _uninstallButton = new Button
            {
                Text = InstallerLocale.Get("Uninstall_UninstallButton"),
                Location = new Point(280, 180),
                Size = new Size(90, 30)
            };
            _uninstallButton.Click += UninstallButton_Click;

            _cancelButton = new Button
            {
                Text = InstallerLocale.Get("Uninstall_CancelButton"),
                Location = new Point(380, 180),
                Size = new Size(80, 30)
            };
            _cancelButton.Click += (s, e) => Close();

            Controls.AddRange(new Control[]
            {
                _titleLabel, _statusLabel, _pathLabel, _progressBar, _uninstallButton, _cancelButton
            });

            _progressBar.AccessibleName = InstallerLocale.Get("Uninstall_Heading");

            // Escape must close the dialog. Deliberately NOT setting
            // AcceptButton: this dialog's primary action is destructive, and
            // Enter should not be able to start an uninstall from anywhere in
            // the form.
            CancelButton = _cancelButton;

            string body = $"{_titleLabel.Text}. {_statusLabel.Text} {_pathLabel.Text}";
            AccessibleDescription = body;
            _uninstallButton.AccessibleDescription = body;
        }

        private async void UninstallButton_Click(object sender, EventArgs e)
        {
            var result = MessageBox.Show(
                InstallerLocale.Format("Uninstall_Confirm_Format", _target.DisplayName, _target.ExeName),
                InstallerLocale.Get("Uninstall_Confirm_Title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning);

            if (result != DialogResult.Yes) return;

            _uninstallButton.Enabled = false;
            _cancelButton.Enabled = false;
            _progressBar.Visible = true;
            _progressBar.Value = 0;

            try
            {
                UpdateStatus(InstallerLocale.Get("Uninstall_StatusRemoving"));
                _progressBar.Value = 30;

                await Task.Run(() => UninstallFlow.PerformUninstall(_target, _gamePath));

                _progressBar.Value = 100;
                UpdateStatus(InstallerLocale.Get("Uninstall_StatusComplete"));
                Logger.Info("Uninstallation completed successfully");

                MessageBox.Show(
                    InstallerLocale.Get("Uninstall_Complete_Text"),
                    InstallerLocale.Get("Uninstall_Complete_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);

                Logger.AskAndSave();
                Close();
            }
            catch (Exception ex)
            {
                Logger.Error("Uninstallation failed", ex);
                MessageBox.Show(
                    InstallerLocale.Format("Uninstall_Error_Format", ex.Message),
                    InstallerLocale.Get("Uninstall_Error_Title"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);

                if (Logger.AskAndSave(alwaysAsk: true)) Logger.OpenLogFile();

                _uninstallButton.Enabled = true;
                _cancelButton.Enabled = true;
                _progressBar.Visible = false;
            }
        }

        private void UpdateStatus(string message)
        {
            if (InvokeRequired) { Invoke(new Action(() => UpdateStatus(message))); return; }
            _statusLabel.Text = message;
            Logger.Info(message);

            // Without this an uninstall gives a blind user no progress feedback
            // at all — this form was the F5 bug.
            ScreenReaderAnnouncer.Announce(_statusLabel, message);
        }
    }
}
