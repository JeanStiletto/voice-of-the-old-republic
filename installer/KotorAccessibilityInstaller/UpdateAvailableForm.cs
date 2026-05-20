using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class UpdateAvailableForm : Form
    {
        public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close;

        public UpdateAvailableForm(string installedVersion, string latestVersion, bool spatialAudioEnabled)
        {
            InitializeComponents(installedVersion, latestVersion, spatialAudioEnabled);
        }

        private void InitializeComponents(string installedVersion, string latestVersion, bool spatialAudioEnabled)
        {
            Text = InstallerLocale.Get("Update_Title");
            Size = new Size(580, 260);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = InstallerLocale.Get("Update_Heading"),
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(540, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            var versionLabel = new Label
            {
                Text = InstallerLocale.Format("Update_VersionInfo_Format", installedVersion, latestVersion),
                Location = new Point(20, 60),
                Size = new Size(540, 60),
                TextAlign = ContentAlignment.TopCenter
            };

            // Four buttons across one row: Update | Full install | Toggle audio | Close
            var updateButton = new Button
            {
                Text = InstallerLocale.Get("Update_UpdateButton"),
                Location = new Point(20, 150),
                Size = new Size(130, 35)
            };
            updateButton.Click += (s, e) => { UserChoice = UpdateChoice.UpdateOnly; Close(); };

            var fullInstallButton = new Button
            {
                Text = InstallerLocale.Get("Update_FullInstallButton"),
                Location = new Point(155, 150),
                Size = new Size(130, 35)
            };
            fullInstallButton.Click += (s, e) => { UserChoice = UpdateChoice.FullInstall; Close(); };

            string toggleLabel = spatialAudioEnabled
                ? InstallerLocale.Get("SpatialAudio_DisableButton")
                : InstallerLocale.Get("SpatialAudio_EnableButton");
            var toggleButton = new Button
            {
                Text = toggleLabel,
                Location = new Point(290, 150),
                Size = new Size(140, 35)
            };
            toggleButton.Click += (s, e) => { UserChoice = UpdateChoice.ToggleSpatialAudio; Close(); };

            var closeButton = new Button
            {
                Text = InstallerLocale.Get("Update_CloseButton"),
                Location = new Point(435, 150),
                Size = new Size(125, 35)
            };
            closeButton.Click += (s, e) => { UserChoice = UpdateChoice.Close; Close(); };

            Controls.AddRange(new Control[] { titleLabel, versionLabel, updateButton, fullInstallButton, toggleButton, closeButton });

            string body = $"{titleLabel.Text}. {versionLabel.Text}";
            AccessibleDescription = body;
            updateButton.AccessibleDescription = body;
        }
    }
}
