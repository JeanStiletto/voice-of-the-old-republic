using System;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Screen between the base-components info screen and the main install form.
    /// Surfaces the three opt-in mod groupings (K1CP, restored cut content,
    /// companion + swoop upgrades) as standalone checkboxes with descriptive labels.
    /// All groupings default to enabled — uncheck to skip.
    ///
    /// Each checkbox's <see cref="Control.AccessibleName"/> is composed from the
    /// title + description so a screen reader announces both on focus.
    /// </summary>
    public class ModSelectionForm : Form
    {
        private Label _titleLabel;
        private Label _descriptionLabel;

        private CheckBox _k1cpCheckBox;
        private Label _k1cpDescription;

        private CheckBox _cutContentCheckBox;
        private Label _cutContentDescription;

        private CheckBox _companionsCheckBox;
        private Label _companionsDescription;

        private Label _footnoteLabel;

        private Button _backButton;
        private Button _nextButton;

        public bool ProceedWithInstall { get; private set; } = false;

        /// <summary>
        /// Selection at time of close. Defaults to all-on; reflects checkbox state when the user
        /// presses Next. Caller should ignore unless <see cref="ProceedWithInstall"/> is true.
        /// </summary>
        public ModSelection Selection { get; private set; } = ModSelection.AllOn();

        public ModSelectionForm()
        {
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
                if (!ProceedWithInstall) Logger.Info("User closed mod-selection dialog without proceeding");
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(640, 600);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 15),
                Size = new Size(600, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            _descriptionLabel = new Label
            {
                Location = new Point(20, 55),
                Size = new Size(600, 55),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- K1CP ---
            _k1cpCheckBox = new CheckBox
            {
                Location = new Point(20, 120),
                Size = new Size(600, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _k1cpDescription = new Label
            {
                Location = new Point(40, 145),
                Size = new Size(580, 55),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- Restored cut content ---
            _cutContentCheckBox = new CheckBox
            {
                Location = new Point(20, 210),
                Size = new Size(600, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _cutContentDescription = new Label
            {
                Location = new Point(40, 235),
                Size = new Size(580, 55),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- Companion balance + Swoop upgrades ---
            _companionsCheckBox = new CheckBox
            {
                Location = new Point(20, 300),
                Size = new Size(600, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _companionsDescription = new Label
            {
                Location = new Point(40, 325),
                Size = new Size(580, 55),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- IT/ES footnote on K1CP ---
            _footnoteLabel = new Label
            {
                Location = new Point(20, 400),
                Size = new Size(600, 75),
                TextAlign = ContentAlignment.TopLeft,
                Font = new Font(FontFamily.GenericSansSerif, 8, FontStyle.Italic)
            };

            _backButton = new Button { Location = new Point(20, 510), Size = new Size(140, 35) };
            _backButton.Click += (s, e) => Close();

            _nextButton = new Button { Location = new Point(480, 510), Size = new Size(140, 35) };
            _nextButton.Click += (s, e) =>
            {
                Selection = new ModSelection
                {
                    K1cp = _k1cpCheckBox.Checked,
                    RestoredCutContent = _cutContentCheckBox.Checked,
                    CompanionAndSwoopUpgrades = _companionsCheckBox.Checked
                };
                Logger.Info($"Mod selection: {Selection}");
                ProceedWithInstall = true;
                Close();
            };

            Controls.AddRange(new Control[]
            {
                _titleLabel, _descriptionLabel,
                _k1cpCheckBox, _k1cpDescription,
                _cutContentCheckBox, _cutContentDescription,
                _companionsCheckBox, _companionsDescription,
                _footnoteLabel,
                _backButton, _nextButton
            });

            AcceptButton = _nextButton;
            CancelButton = _backButton;
        }

        private void ApplyLocale()
        {
            _titleLabel.Text = InstallerLocale.Get("ModSelection_Title");
            _descriptionLabel.Text = InstallerLocale.Get("ModSelection_Description");

            _k1cpCheckBox.Text = InstallerLocale.Get("ModSelection_K1cpCheckbox");
            _k1cpDescription.Text = InstallerLocale.Get("ModSelection_K1cpDescription");

            _cutContentCheckBox.Text = InstallerLocale.Get("ModSelection_CutContentCheckbox");
            _cutContentDescription.Text = InstallerLocale.Get("ModSelection_CutContentDescription");

            _companionsCheckBox.Text = InstallerLocale.Get("ModSelection_CompanionsCheckbox");
            _companionsDescription.Text = InstallerLocale.Get("ModSelection_CompanionsDescription");

            _footnoteLabel.Text = InstallerLocale.Get("ModSelection_FootnoteBody");

            _backButton.Text = InstallerLocale.Get("ModSelection_BackButton");
            _nextButton.Text = InstallerLocale.Get("ModSelection_NextButton");

            // Compose AccessibleName so the screen reader announces both the
            // group name AND the description text on focus — otherwise NVDA
            // only reads the checkbox Text and the user has to navigate
            // separately to the descriptive Label.
            _k1cpCheckBox.AccessibleName = $"{_k1cpCheckBox.Text}. {_k1cpDescription.Text}";
            _cutContentCheckBox.AccessibleName = $"{_cutContentCheckBox.Text}. {_cutContentDescription.Text}";
            _companionsCheckBox.AccessibleName = $"{_companionsCheckBox.Text}. {_companionsDescription.Text}";

            string body = $"{_titleLabel.Text}. {_descriptionLabel.Text}";
            AccessibleDescription = body;
            _nextButton.AccessibleDescription = body;
        }
    }
}
