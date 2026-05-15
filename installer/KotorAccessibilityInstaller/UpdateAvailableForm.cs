using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class UpdateAvailableForm : Form
    {
        public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close;

        public UpdateAvailableForm(string installedVersion, string latestVersion)
        {
            InitializeComponents(installedVersion, latestVersion);
        }

        private void InitializeComponents(string installedVersion, string latestVersion)
        {
            Text = InstallerLocale.Get("Update_Title");
            Size = new Size(470, 260);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = InstallerLocale.Get("Update_Heading"),
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(420, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            var versionLabel = new Label
            {
                Text = InstallerLocale.Format("Update_VersionInfo_Format", installedVersion, latestVersion),
                Location = new Point(20, 60),
                Size = new Size(420, 60),
                TextAlign = ContentAlignment.TopCenter
            };

            var updateButton = new Button
            {
                Text = InstallerLocale.Get("Update_UpdateButton"),
                Location = new Point(30, 140),
                Size = new Size(130, 35)
            };
            updateButton.Click += (s, e) => { UserChoice = UpdateChoice.UpdateOnly; Close(); };

            var fullInstallButton = new Button
            {
                Text = InstallerLocale.Get("Update_FullInstallButton"),
                Location = new Point(170, 140),
                Size = new Size(130, 35)
            };
            fullInstallButton.Click += (s, e) => { UserChoice = UpdateChoice.FullInstall; Close(); };

            var closeButton = new Button
            {
                Text = InstallerLocale.Get("Update_CloseButton"),
                Location = new Point(310, 140),
                Size = new Size(130, 35)
            };
            closeButton.Click += (s, e) => { UserChoice = UpdateChoice.Close; Close(); };

            Controls.AddRange(new Control[] { titleLabel, versionLabel, updateButton, fullInstallButton, closeButton });

            string body = $"{titleLabel.Text}. {versionLabel.Text}";
            AccessibleDescription = body;
            updateButton.AccessibleDescription = body;
        }
    }
}
