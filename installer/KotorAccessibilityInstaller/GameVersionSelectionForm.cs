using System;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Screen between the welcome dialog and the base-components info screen.
    /// Asks which game(s) the installer should target: KOTOR 1, KOTOR 2, or
    /// both. KOTOR 1 defaults to checked (the fully supported path); KOTOR 2
    /// defaults to unchecked while its support is in preparation.
    ///
    /// Next requires at least one box checked; pressing it with neither
    /// selected shows a plain MessageBox (screen-reader announced) instead of
    /// silently disabling the button — a disabled button gives a screen-reader
    /// user no explanation of why the flow stalled.
    ///
    /// Each checkbox's <see cref="Control.AccessibleName"/> is composed from the
    /// title + description so a screen reader announces both on focus (same
    /// pattern as <see cref="ModSelectionForm"/>).
    /// </summary>
    public class GameVersionSelectionForm : Form
    {
        private Label _titleLabel;
        private Label _descriptionLabel;

        private CheckBox _kotor1CheckBox;
        private Label _kotor1Description;

        private CheckBox _kotor2CheckBox;
        private Label _kotor2Description;

        private Button _backButton;
        private Button _nextButton;

        public bool ProceedWithInstall { get; private set; } = false;

        /// <summary>
        /// Selection at time of close. Caller should ignore unless
        /// <see cref="ProceedWithInstall"/> is true.
        /// </summary>
        public GameVersionSelection Selection { get; private set; } = new GameVersionSelection();

        public GameVersionSelectionForm()
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
                if (!ProceedWithInstall) Logger.Info("User closed game-version selection dialog without proceeding");
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(640, 420);
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

            // --- KOTOR 1 ---
            _kotor1CheckBox = new CheckBox
            {
                Location = new Point(20, 120),
                Size = new Size(600, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = true
            };
            _kotor1Description = new Label
            {
                Location = new Point(40, 145),
                Size = new Size(580, 55),
                TextAlign = ContentAlignment.TopLeft
            };

            // --- KOTOR 2 ---
            _kotor2CheckBox = new CheckBox
            {
                Location = new Point(20, 210),
                Size = new Size(600, 25),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold),
                Checked = false
            };
            _kotor2Description = new Label
            {
                Location = new Point(40, 235),
                Size = new Size(580, 70),
                TextAlign = ContentAlignment.TopLeft
            };

            _backButton = new Button { Location = new Point(20, 325), Size = new Size(140, 35) };
            _backButton.Click += (s, e) => Close();

            _nextButton = new Button { Location = new Point(480, 325), Size = new Size(140, 35) };
            _nextButton.Click += (s, e) =>
            {
                if (!_kotor1CheckBox.Checked && !_kotor2CheckBox.Checked)
                {
                    MessageBox.Show(
                        InstallerLocale.Get("GameVersion_NeedOne_Text"),
                        InstallerLocale.Get("GameVersion_NeedOne_Title"),
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Information);
                    return;
                }

                Selection = new GameVersionSelection
                {
                    Kotor1 = _kotor1CheckBox.Checked,
                    Kotor2 = _kotor2CheckBox.Checked
                };
                Logger.Info($"Game version selection: {Selection}");
                ProceedWithInstall = true;
                Close();
            };

            Controls.AddRange(new Control[]
            {
                _titleLabel, _descriptionLabel,
                _kotor1CheckBox, _kotor1Description,
                _kotor2CheckBox, _kotor2Description,
                _backButton, _nextButton
            });

            AcceptButton = _nextButton;
            CancelButton = _backButton;
        }

        private void ApplyLocale()
        {
            _titleLabel.Text = InstallerLocale.Get("GameVersion_Title");
            _descriptionLabel.Text = InstallerLocale.Get("GameVersion_Description");

            _kotor1CheckBox.Text = InstallerLocale.Get("GameVersion_K1Checkbox");
            _kotor1Description.Text = InstallerLocale.Get("GameVersion_K1Description");

            _kotor2CheckBox.Text = InstallerLocale.Get("GameVersion_K2Checkbox");
            _kotor2Description.Text = InstallerLocale.Get("GameVersion_K2Description");

            _backButton.Text = InstallerLocale.Get("Welcome_BackButton");
            _nextButton.Text = InstallerLocale.Get("Welcome_NextButton");

            // Compose AccessibleName so the screen reader announces both the
            // checkbox label AND the description text on focus.
            _kotor1CheckBox.AccessibleName = $"{_kotor1CheckBox.Text}. {_kotor1Description.Text}";
            _kotor2CheckBox.AccessibleName = $"{_kotor2CheckBox.Text}. {_kotor2Description.Text}";

            string body = $"{_titleLabel.Text}. {_descriptionLabel.Text}";
            AccessibleDescription = body;
            _nextButton.AccessibleDescription = body;
        }
    }
}
