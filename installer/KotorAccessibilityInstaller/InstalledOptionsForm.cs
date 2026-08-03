using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Shown when the mod is already installed and up to date. Choices:
    /// full reinstall, toggle bundled spatial-audio layer (dsoal) on/off,
    /// collect a beta-test log bundle, or close. Same enum as
    /// UpdateAvailableForm so Program.cs can dispatch uniformly.
    ///
    /// The spatial-audio toggle is offered for KOTOR 1 only — dsoal is not
    /// bundled for KOTOR 2 and its pairing with Aspyr's audio stack is
    /// unverified, so the button is absent rather than present-and-inert.
    /// </summary>
    public class InstalledOptionsForm : Form
    {
        public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close;

        public InstalledOptionsForm(GameTarget target, string installedVersion, bool spatialAudioEnabled)
        {
            InitializeComponents(target, installedVersion, spatialAudioEnabled);
        }

        private void InitializeComponents(GameTarget target, string installedVersion, bool spatialAudioEnabled)
        {
            bool offerSpatialAudio = target == GameTarget.Kotor1;

            Text = InstallerLocale.Get("Program_UpToDate_Title") + " — " + target.DisplayName;
            Size = new Size(700, 300);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = Text,
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(660, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            string audioStateLabel = spatialAudioEnabled
                ? InstallerLocale.Get("SpatialAudio_StateEnabled")
                : InstallerLocale.Get("SpatialAudio_StateDisabled");

            var bodyLabel = new Label
            {
                Text = offerSpatialAudio
                    ? InstallerLocale.Format("InstalledOptions_Body_Format", installedVersion, audioStateLabel)
                    : InstallerLocale.Format("InstalledOptions_BodyNoAudio_Format", installedVersion),
                Location = new Point(20, 60),
                Size = new Size(660, 130),
                TextAlign = ContentAlignment.TopLeft
            };

            const int btnY = 205;
            const int btnH = 35;

            var reinstallButton = new Button
            {
                Text = InstallerLocale.Get("InstalledOptions_ReinstallButton"),
                Size = new Size(140, btnH)
            };
            reinstallButton.Click += (s, e) => { UserChoice = UpdateChoice.FullInstall; Close(); };

            string toggleLabel = spatialAudioEnabled
                ? InstallerLocale.Get("SpatialAudio_DisableButton")
                : InstallerLocale.Get("SpatialAudio_EnableButton");
            var toggleButton = new Button
            {
                Text = toggleLabel,
                Size = new Size(200, btnH)
            };
            toggleButton.Click += (s, e) => { UserChoice = UpdateChoice.ToggleSpatialAudio; Close(); };

            var collectLogsButton = new Button
            {
                Text = InstallerLocale.Get("CollectLogs_Button"),
                Size = new Size(170, btnH)
            };
            collectLogsButton.Click += (s, e) => { UserChoice = UpdateChoice.CollectLogs; Close(); };

            var closeButton = new Button
            {
                Text = InstallerLocale.Get("Update_CloseButton"),
                Size = new Size(120, btnH)
            };
            closeButton.Click += (s, e) => { UserChoice = UpdateChoice.Close; Close(); };

            // Escape closes the dialog; Enter activates the same safe
            // default. Without these, a keyboard-only user had no Escape path.
            AcceptButton = closeButton;
            CancelButton = closeButton;

            // Laid out left to right from the set that actually applies, rather
            // than at fixed coordinates, so dropping the spatial-audio button on
            // KOTOR 2 closes the gap instead of leaving a hole in the tab order's
            // visual counterpart.
            var buttons = new List<Button> { reinstallButton };
            if (offerSpatialAudio) buttons.Add(toggleButton);
            buttons.Add(collectLogsButton);
            buttons.Add(closeButton);

            int x = 20;
            foreach (var b in buttons)
            {
                b.Location = new Point(x, btnY);
                x += b.Width + 10;
            }

            Controls.Add(titleLabel);
            Controls.Add(bodyLabel);
            foreach (var b in buttons) Controls.Add(b);

            string body = $"{titleLabel.Text}. {bodyLabel.Text}";
            AccessibleDescription = body;
            reinstallButton.AccessibleDescription = body;
        }
    }
}
