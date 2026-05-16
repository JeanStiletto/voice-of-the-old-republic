using System;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Screen inserted between the welcome wizard and the mod-selection screen.
    /// Informs the user about the components that are ALWAYS installed
    /// (accessibility mod, Prism speech bridge, widescreen patches) — these are
    /// not user-selectable because the mod depends on them for basic functionality.
    /// User-optional mods live in <see cref="ModSelectionForm"/>.
    ///
    /// Uses a single read-only TextBox so screen readers can navigate the content
    /// line-by-line with arrow keys rather than fighting non-focusable Labels.
    /// </summary>
    public class ModdingInfoForm : Form
    {
        private Label _titleLabel;
        private Label _descriptionLabel;
        private TextBox _contentBox;
        private Button _nextButton;
        private Button _backButton;

        public bool ProceedWithInstall { get; private set; } = false;

        public ModdingInfoForm()
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
                if (!ProceedWithInstall) Logger.Info("User closed base-components dialog without proceeding");
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(640, 560);
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

            // Multi-line read-only TextBox: arrow keys + screen-reader cursor
            // can walk every line, unlike a stack of non-focusable Labels.
            _contentBox = new TextBox
            {
                Location = new Point(20, 120),
                Size = new Size(600, 350),
                Multiline = true,
                ReadOnly = true,
                ScrollBars = ScrollBars.Vertical,
                WordWrap = true,
                Font = new Font(FontFamily.GenericSansSerif, 9),
                BackColor = SystemColors.Window
            };

            _backButton = new Button { Location = new Point(20, 485), Size = new Size(140, 35) };
            _backButton.Click += (s, e) => Close();

            _nextButton = new Button { Location = new Point(480, 485), Size = new Size(140, 35) };
            _nextButton.Click += (s, e) =>
            {
                ProceedWithInstall = true;
                Close();
            };

            Controls.AddRange(new Control[] { _titleLabel, _descriptionLabel, _contentBox, _backButton, _nextButton });

            AcceptButton = _nextButton;
            CancelButton = _backButton;
        }

        private void ApplyLocale()
        {
            _titleLabel.Text = InstallerLocale.Get("ModdingInfo_Title");
            _descriptionLabel.Text = InstallerLocale.Get("ModdingInfo_Description");
            _backButton.Text = InstallerLocale.Get("ModdingInfo_BackButton");
            _nextButton.Text = InstallerLocale.Get("ModdingInfo_NextButton");

            _contentBox.Text = BuildContent();

            // Mirror onto AccessibleDescription so NVDA reads the title + intro
            // on dialog open even before focus reaches the content box.
            string body = $"{_titleLabel.Text}. {_descriptionLabel.Text}";
            AccessibleDescription = body;
            _nextButton.AccessibleDescription = body;
            _contentBox.AccessibleName = _titleLabel.Text;
        }

        private static string BuildContent()
        {
            // Read order: accessibility mod → Prism → widescreen. Each section
            // gives a short blurb plus a credit line where relevant.
            var sb = new StringBuilder();

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_AccessibilityHeading"),
                InstallerLocale.Get("ModdingInfo_AccessibilityBody"));

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_PrismHeading"),
                InstallerLocale.Get("ModdingInfo_PrismBody"));

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_WidescreenHeading"),
                InstallerLocale.Get("ModdingInfo_WidescreenBody"));

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_IniTweaksHeading"),
                InstallerLocale.Get("ModdingInfo_IniTweaksBody"));

            return sb.ToString().TrimEnd();
        }

        private static void AppendSection(StringBuilder sb, string heading, string body)
        {
            sb.AppendLine(heading);
            sb.AppendLine(new string('-', Math.Min(heading.Length, 60)));
            sb.AppendLine(body);
            sb.AppendLine();
            sb.AppendLine();
        }
    }
}
