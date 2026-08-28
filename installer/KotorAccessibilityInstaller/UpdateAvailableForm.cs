using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class UpdateAvailableForm : Form
    {
        public UpdateChoice UserChoice { get; private set; } = UpdateChoice.Close;

        /// <param name="gameNames">
        /// The game (or games) this update will be applied to, already joined
        /// for reading — see <c>Program.JoinGameNames</c>. A set rather than one
        /// name because an update run covers every installed game that is behind
        /// the published release, not just whichever one the installer looked at
        /// first.
        /// </param>
        /// <param name="offerSpatialAudio">
        /// Whether to show the dsoal toggle. Decided by the caller from the
        /// KOTOR 1 install specifically — dsoal is bundled for KOTOR 1 only, so
        /// the button is absent rather than present-and-inert everywhere else.
        /// </param>
        public UpdateAvailableForm(string gameNames, bool offerSpatialAudio, string installedVersion,
                                   string latestVersion, bool spatialAudioEnabled, string gameStates = null)
        {
            InitializeComponents(gameNames, offerSpatialAudio, installedVersion, latestVersion,
                                 spatialAudioEnabled, gameStates);
        }

        private void InitializeComponents(string gameNames, bool offerSpatialAudio, string installedVersion,
                                          string latestVersion, bool spatialAudioEnabled, string gameStates)
        {
            Text = InstallerLocale.Get("Update_Title") + " — " + gameNames;
            Size = new Size(580, 330);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = InstallerLocale.Get("Update_Heading") + " — " + gameNames,
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
