using System;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// KOTOR 2 counterpart of <see cref="ModSelectionForm"/>: shown when the
    /// user checks KOTOR 2 on the game-version screen, after the engine
    /// patches have been applied (their result rides the description text).
    /// Three checkboxes — TSLRCM, K2CP, Tweak Pack — all on by default and
    /// labeled highly recommended.
    ///
    /// TSLRCM is not an <see cref="ModInstallers.IModInstaller"/> (it is an
    /// exe-installer run, not a HoloPatcher payload), so its choice is exposed
    /// as <see cref="InstallTslrcm"/> while the other two ride the shared
    /// <see cref="ModSelection"/> consumed by the coordinator pipeline.
    ///
    /// Each checkbox's <see cref="Control.AccessibleName"/> is composed from
    /// the title + description so a screen reader announces both on focus.
    /// </summary>
    public class Kotor2ModSelectionForm : Form
    {
        private Label _titleLabel;
        private Label _descriptionLabel;

        private CheckBox _tslrcmCheckBox;
        private Label _tslrcmDescription;

        private CheckBox _k2cpCheckBox;
        private Label _k2cpDescription;

        private CheckBox _tweakPackCheckBox;
        private Label _tweakPackDescription;

        private Label _footnoteLabel;

        private Button _backButton;
        private Button _nextButton;

        private readonly string _statusLine;

        public bool ProceedWithInstall { get; private set; } = false;

        /// <summary>User wants TSLRCM downloaded + installed.</summary>
        public bool InstallTslrcm { get; private set; } = true;

        /// <summary>
        /// K2CP / Tweak Pack toggles for the coordinator pipeline. KOTOR 1
        /// groupings are forced false. Ignore unless
        /// <see cref="ProceedWithInstall"/> is true.
        /// </summary>
        public ModSelection Selection { get; private set; }

        /// <param name="statusLine">
        /// Detection + engine-patch result lines composed by the caller;
        /// prepended to the description so the whole preparation step reads as
        /// one linear text.
        /// </param>
        public Kotor2ModSelectionForm(string statusLine)
        {
            _statusLine = statusLine ?? string.Empty;
            InitializeComponents();
            ApplyLocale();
            InstallerLocale.OnLanguageChanged += ApplyLocale;
            FormClosing += (s, e) =>
            {
                if (!ProceedWithInstall && !CancelConfirm.ConfirmCancel(this))
                {
                    e.Cancel = true;
                    return;
                }
                InstallerLocale.OnLanguageChanged -= ApplyLocale;
                if (!ProceedWithInstall) Logger.Info("User closed KOTOR 2 mod-selection dialog without proceeding");
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(660, 600);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 15),
                Size = new Size(620, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            _descriptionLabel = new Label
            {
                Location = new Point(20, 50),
                Size = new Size(620, 120),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- TSLRCM ---
            _tslrcmCheckBox = new CheckBox
            {
                Location = new Point(20, 180),
                Size = new Size(620, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _tslrcmDescription = new Label
            {
                Location = new Point(40, 205),
                Size = new Size(600, 45),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- K2CP ---
            _k2cpCheckBox = new CheckBox
            {
                Location = new Point(20, 255),
                Size = new Size(620, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _k2cpDescription = new Label
            {
                Location = new Point(40, 280),
                Size = new Size(600, 45),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- Tweak Pack ---
            _tweakPackCheckBox = new CheckBox
            {
                Location = new Point(20, 330),
                Size = new Size(620, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _tweakPackDescription = new Label
            {
                Location = new Point(40, 355),
                Size = new Size(600, 45),
                TextAlign = ContentAlignment.TopLeft
            };

            _footnoteLabel = new Label
            {
                Location = new Point(20, 410),
                Size = new Size(620, 85),
                TextAlign = ContentAlignment.TopLeft,
                Font = new Font(FontFamily.GenericSansSerif, 8, FontStyle.Italic)
            };

            _backButton = new Button { Location = new Point(20, 505), Size = new Size(140, 35) };
            _backButton.Click += (s, e) => Close();

            _nextButton = new Button { Location = new Point(500, 505), Size = new Size(140, 35) };
            _nextButton.Click += (s, e) =>
            {
                InstallTslrcm = _tslrcmCheckBox.Checked;
                Selection = new ModSelection
                {
                    K1cp = false,
                    RestoredCutContent = false,
                    CompanionAndSwoopUpgrades = false,
                    K2cp = _k2cpCheckBox.Checked,
                    TweakPack = _tweakPackCheckBox.Checked
                };
                Logger.Info($"KOTOR 2 mod selection: Tslrcm={InstallTslrcm}, {Selection}");
                ProceedWithInstall = true;
                Close();
            };

            Controls.AddRange(new Control[]
            {
                _titleLabel, _descriptionLabel,
                _tslrcmCheckBox, _tslrcmDescription,
                _k2cpCheckBox, _k2cpDescription,
                _tweakPackCheckBox, _tweakPackDescription,
                _footnoteLabel,
                _backButton, _nextButton
            });

            AcceptButton = _nextButton;
            CancelButton = _backButton;
        }

        private void ApplyLocale()
        {
            _titleLabel.Text = InstallerLocale.Get("K2Mods_Title");
            _descriptionLabel.Text = InstallerLocale.Format("K2Mods_Description_Format", _statusLine);

            _tslrcmCheckBox.Text = InstallerLocale.Get("K2Mods_TslrcmCheckbox");
            _tslrcmDescription.Text = InstallerLocale.Get("K2Mods_TslrcmDescription");

            _k2cpCheckBox.Text = InstallerLocale.Get("K2Mods_K2cpCheckbox");
            _k2cpDescription.Text = InstallerLocale.Get("K2Mods_K2cpDescription");

            _tweakPackCheckBox.Text = InstallerLocale.Get("K2Mods_TweakPackCheckbox");
            _tweakPackDescription.Text = InstallerLocale.Get("K2Mods_TweakPackDescription");

            _footnoteLabel.Text = InstallerLocale.Format("K2Mods_Footnote_Format", Config.TslrcmDownloadPageUrl);

            _backButton.Text = InstallerLocale.Get("ModSelection_BackButton");
            _nextButton.Text = InstallerLocale.Get("ModSelection_NextButton");

            _tslrcmCheckBox.AccessibleName = $"{_tslrcmCheckBox.Text}. {_tslrcmDescription.Text}";
            _k2cpCheckBox.AccessibleName = $"{_k2cpCheckBox.Text}. {_k2cpDescription.Text}";
            _tweakPackCheckBox.AccessibleName = $"{_tweakPackCheckBox.Text}. {_tweakPackDescription.Text}";

            string body = $"{_titleLabel.Text}. {_descriptionLabel.Text}";
            AccessibleDescription = body;
            _nextButton.AccessibleDescription = body;
        }
    }
}
