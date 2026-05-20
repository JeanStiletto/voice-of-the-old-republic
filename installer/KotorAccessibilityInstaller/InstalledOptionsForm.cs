using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Shown when the mod is already installed and up to date. Three choices:
    /// full reinstall, toggle bundled spatial-audio layer (dsoal) on/off, or
    /// close. Same enum as UpdateAvailableForm so Program.cs can dispatch
    /// uniformly.
    /// </summary>
    public class InstalledOptionsForm : Form
    {
        public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close;

        public InstalledOptionsForm(string installedVersion, bool spatialAudioEnabled)
        {
            InitializeComponents(installedVersion, spatialAudioEnabled);
        }

        private void InitializeComponents(string installedVersion, bool spatialAudioEnabled)
        {
            Text = InstallerLocale.Get("Program_UpToDate_Title");
            Size = new Size(520, 280);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = InstallerLocale.Get("Program_UpToDate_Title"),
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(480, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            string audioStateLabel = spatialAudioEnabled
                ? InstallerLocale.Get("SpatialAudio_StateEnabled")
                : InstallerLocale.Get("SpatialAudio_StateDisabled");

            var bodyLabel = new Label
            {
                Text = InstallerLocale.Format("InstalledOptions_Body_Format", installedVersion, audioStateLabel),
                Location = new Point(20, 60),
                Size = new Size(480, 100),
                TextAlign = ContentAlignment.TopLeft
            };

            var reinstallButton = new Button
            {
                Text = InstallerLocale.Get("InstalledOptions_ReinstallButton"),
                Location = new Point(20, 175),
                Size = new Size(140, 35)
            };
            reinstallButton.Click += (s, e) => { UserChoice = UpdateChoice.FullInstall; Close(); };

            string toggleLabel = spatialAudioEnabled
                ? InstallerLocale.Get("SpatialAudio_DisableButton")
                : InstallerLocale.Get("SpatialAudio_EnableButton");
            var toggleButton = new Button
            {
                Text = toggleLabel,
                Location = new Point(170, 175),
                Size = new Size(180, 35)
            };
            toggleButton.Click += (s, e) => { UserChoice = UpdateChoice.ToggleSpatialAudio; Close(); };

            var closeButton = new Button
            {
                Text = InstallerLocale.Get("Update_CloseButton"),
                Location = new Point(360, 175),
                Size = new Size(125, 35)
            };
            closeButton.Click += (s, e) => { UserChoice = UpdateChoice.Close; Close(); };

            Controls.AddRange(new Control[] { titleLabel, bodyLabel, reinstallButton, toggleButton, closeButton });

            string body = $"{titleLabel.Text}. {bodyLabel.Text}";
            AccessibleDescription = body;
            reinstallButton.AccessibleDescription = body;
        }
    }
}
