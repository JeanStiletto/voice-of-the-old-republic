using System;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Screen inserted between the welcome wizard and the main install form.
    /// Surfaces the "Mods to bundle" section from docs/installer.md in a
    /// screen-reader-friendly form (linear list, group headings, no tables).
    ///
    /// Uses a single read-only TextBox so screen readers can navigate the
    /// content line-by-line with arrow keys rather than fighting non-focusable
    /// Labels.
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
                InstallerLocale.OnLanguageChanged -= ApplyLocale;
                if (!ProceedWithInstall) Logger.Info("User closed modding-info dialog without proceeding");
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
            // Read order matches the section flow in docs/installer.md: bundle,
            // exclusions, optional add-ons, l10n footnote.
            var sb = new StringBuilder();

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_BundleHeading"),
                InstallerLocale.Get("ModdingInfo_BundleBody"));

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_FiltersHeading"),
                InstallerLocale.Get("ModdingInfo_FiltersBody"));

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_OptionalHeading"),
                InstallerLocale.Get("ModdingInfo_OptionalBody"));

            AppendSection(sb,
                InstallerLocale.Get("ModdingInfo_FootnoteHeading"),
                InstallerLocale.Get("ModdingInfo_FootnoteBody"));

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
