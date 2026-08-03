using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class UpdateAvailableForm : Form
    {
        public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close;

        public UpdateAvailableForm(GameTarget target, string installedVersion, string latestVersion,
                                   bool spatialAudioEnabled, string gameStates = null)
        {
            InitializeComponents(target, installedVersion, latestVersion, spatialAudioEnabled, gameStates);
        }

        private void InitializeComponents(GameTarget target, string installedVersion, string latestVersion,
                                          bool spatialAudioEnabled, string gameStates)
        {
            // dsoal is KOTOR 1 only — see InstalledOptionsForm.
            bool offerSpatialAudio = target == GameTarget.Kotor1;

            Text = InstallerLocale.Get("Update_Title") + " — " + target.DisplayName;
            Size = new Size(580, 330);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = InstallerLocale.Get("Update_Heading") + " — " + target.DisplayName,
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(540, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            // Per-game state below the version line, so an update prompt still
            // answers "and what about the other game?" — see
            // Program.DescribeAllGames.
            var versionLabel = new Label
            {
                Text = InstallerLocale.Format("Update_VersionInfo_Format", installedVersion, latestVersion)
                       + (string.IsNullOrEmpty(gameStates)
                            ? string.Empty
                            : "\n\n" + InstallerLocale.Get("GameState_Heading") + "\n" + gameStates),
                Location = new Point(20, 60),
                Size = new Size(540, 130),
                TextAlign = ContentAlignment.TopCenter
            };

            // One row: Update | Full install | [Toggle audio] | Close
            var updateButton = new Button
            {
                Text = InstallerLocale.Get("Update_UpdateButton"),
                Size = new Size(130, 35)
            };
            updateButton.Click += (s, e) => { UserChoice = UpdateChoice.UpdateOnly; Close(); };

            var fullInstallButton = new Button
            {
                Text = InstallerLocale.Get("Update_FullInstallButton"),
                Size = new Size(130, 35)
            };
            fullInstallButton.Click += (s, e) => { UserChoice = UpdateChoice.FullInstall; Close(); };

            string toggleLabel = spatialAudioEnabled
                ? InstallerLocale.Get("SpatialAudio_DisableButton")
                : InstallerLocale.Get("SpatialAudio_EnableButton");
            var toggleButton = new Button
            {
                Text = toggleLabel,
                Size = new Size(140, 35)
            };
            toggleButton.Click += (s, e) => { UserChoice = UpdateChoice.ToggleSpatialAudio; Close(); };

            var closeButton = new Button
            {
                Text = InstallerLocale.Get("Update_CloseButton"),
                Size = new Size(125, 35)
            };
            closeButton.Click += (s, e) => { UserChoice = UpdateChoice.Close; Close(); };

            // Escape closes the dialog; Enter activates the same safe
            // default. Without these, a keyboard-only user had no Escape path.
            AcceptButton = closeButton;
            CancelButton = closeButton;

            var buttons = new List<Button> { updateButton, fullInstallButton };
            if (offerSpatialAudio) buttons.Add(toggleButton);
            buttons.Add(closeButton);

            int x = 20;
            foreach (var b in buttons)
            {
                b.Location = new Point(x, 220);
                x += b.Width + 5;
            }

            Controls.Add(titleLabel);
            Controls.Add(versionLabel);
            foreach (var b in buttons) Controls.Add(b);

            string body = $"{titleLabel.Text}. {versionLabel.Text}";
            AccessibleDescription = body;
            updateButton.AccessibleDescription = body;
        }
    }
}
