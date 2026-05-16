using System;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    public class WelcomeForm : Form
    {
        // KOTOR 1 store pages; both work regardless of regional locale.
        private const string KotorSteamPageUrl = "https://store.steampowered.com/app/32370/";
        private const string KotorGogPageUrl = "https://www.gog.com/en/game/star_wars_knights_of_the_old_republic";

        private ComboBox _languageComboBox;
        private Label _titleLabel;
        private Label _descriptionLabel;
        private Label _versionLabel;
        private Label _languageLabel;
        private Button _nextButton;

        private Label _gameTitleLabel;
        private Label _gameDescriptionLabel;
        private Button _openSteamPageButton;
        private Button _openGogPageButton;
        private Button _backButton;
        private Button _installButton;

        private Panel _page1;
        private Panel _page2;

        public bool ProceedWithInstall { get; private set; } = false;
        public string SelectedLanguage { get; private set; }
        public string LatestModVersion { get; set; }

        public WelcomeForm()
        {
            SelectedLanguage = LanguageDetector.DetectLanguage();
            InitializeComponents();
            ApplyLocale();
            InstallerLocale.OnLanguageChanged += ApplyLocale;
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(520, 360);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _page1 = new Panel { Location = new Point(0, 0), Size = new Size(520, 360), Visible = true };

            _titleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(470, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            _descriptionLabel = new Label
            {
                Location = new Point(20, 60),
                Size = new Size(470, 90),
                TextAlign = ContentAlignment.TopLeft
            };

            _versionLabel = new Label
            {
                Location = new Point(20, 155),
                Size = new Size(470, 20),
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold)
            };

            _languageLabel = new Label { Location = new Point(20, 195), Size = new Size(150, 20) };

            _languageComboBox = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                Location = new Point(175, 192),
                Size = new Size(315, 25)
            };

            foreach (var code in LanguageDetector.SupportedLanguages)
            {
                _languageComboBox.Items.Add(
                    LanguageDetector.DisplayNames.TryGetValue(code, out var name) ? name : code);
            }
            int defaultIndex = Array.IndexOf(LanguageDetector.SupportedLanguages, SelectedLanguage);
            _languageComboBox.SelectedIndex = defaultIndex >= 0 ? defaultIndex : 0;
            _languageComboBox.SelectedIndexChanged += (s, e) =>
            {
                int idx = _languageComboBox.SelectedIndex;
                if (idx >= 0 && idx < LanguageDetector.SupportedLanguages.Length)
                {
                    SelectedLanguage = LanguageDetector.SupportedLanguages[idx];
                    Logger.Info($"User selected language: {SelectedLanguage}");
                    InstallerLocale.SetLanguage(SelectedLanguage);
                }
            };

            _nextButton = new Button { Location = new Point(350, 245), Size = new Size(140, 35) };
            _nextButton.Click += (s, e) => ShowPage2();

            _page1.Controls.AddRange(new Control[]
            {
                _titleLabel, _descriptionLabel, _versionLabel, _languageLabel, _languageComboBox, _nextButton
            });

            // Page 2
            _page2 = new Panel { Location = new Point(0, 0), Size = new Size(520, 360), Visible = false };

            _gameTitleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(470, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            _gameDescriptionLabel = new Label
            {
                Location = new Point(20, 60),
                Size = new Size(470, 110),
                TextAlign = ContentAlignment.TopLeft
            };

            _openSteamPageButton = new Button { Location = new Point(20, 185), Size = new Size(220, 35) };
            _openSteamPageButton.Click += (s, e) =>
            {
                Logger.Info($"Opening KOTOR Steam page: {KotorSteamPageUrl}");
                Process.Start(new ProcessStartInfo(KotorSteamPageUrl) { UseShellExecute = true });
            };

            _openGogPageButton = new Button { Location = new Point(250, 185), Size = new Size(220, 35) };
            _openGogPageButton.Click += (s, e) =>
            {
                Logger.Info($"Opening KOTOR GoG page: {KotorGogPageUrl}");
                Process.Start(new ProcessStartInfo(KotorGogPageUrl) { UseShellExecute = true });
            };

            _backButton = new Button { Location = new Point(20, 245), Size = new Size(140, 35) };
            _backButton.Click += (s, e) => ShowPage1();

            _installButton = new Button { Location = new Point(350, 245), Size = new Size(140, 35) };
            _installButton.Click += (s, e) =>
            {
                ProceedWithInstall = true;
                Close();
            };

            _page2.Controls.AddRange(new Control[]
            {
                _gameTitleLabel, _gameDescriptionLabel, _openSteamPageButton, _openGogPageButton, _backButton, _installButton
            });

            Controls.Add(_page1);
            Controls.Add(_page2);

            FormClosing += (s, e) =>
            {
                if (!ProceedWithInstall && !CancelConfirm.ConfirmCancel(this))
                {
                    e.Cancel = true;
                    return;
                }
                InstallerLocale.OnLanguageChanged -= ApplyLocale;
                if (!ProceedWithInstall) Logger.Info("User closed welcome dialog without proceeding");
            };
        }

        private void ShowPage1()
        {
            _page2.Visible = false;
            _page1.Visible = true;
            UpdateAccessibleDescription();
            _nextButton.Focus();
        }

        private void ShowPage2()
        {
            _page1.Visible = false;
            _page2.Visible = true;
            UpdateAccessibleDescription();
            _installButton.Focus();
        }

        private void ApplyLocale()
        {
            _titleLabel.Text = InstallerLocale.Get("Welcome_Title");
            _descriptionLabel.Text = InstallerLocale.Get("Welcome_Description");
            _languageLabel.Text = InstallerLocale.Get("Welcome_LanguageLabel");
            _nextButton.Text = InstallerLocale.Get("Welcome_NextButton");
            _versionLabel.Text = !string.IsNullOrEmpty(LatestModVersion)
                ? InstallerLocale.Format("Welcome_VersionInfo_Format", LatestModVersion)
                : InstallerLocale.Get("Welcome_VersionInfo_Unknown");

            _gameTitleLabel.Text = InstallerLocale.Get("Welcome_GameTitle");
            _gameDescriptionLabel.Text = InstallerLocale.Get("Welcome_GameDescription");
            _openSteamPageButton.Text = InstallerLocale.Get("Welcome_OpenSteamPage");
            _openGogPageButton.Text = InstallerLocale.Get("Welcome_OpenGogPage");
            _backButton.Text = InstallerLocale.Get("Welcome_BackButton");
            _installButton.Text = InstallerLocale.Get("Welcome_NextButton");

            UpdateAccessibleDescription();
        }

        private void UpdateAccessibleDescription()
        {
            if (_page2 != null && _page2.Visible)
            {
                string body = $"{_gameTitleLabel.Text}. {_gameDescriptionLabel.Text}";
                AccessibleDescription = body;
                if (_installButton != null) _installButton.AccessibleDescription = body;
            }
            else
            {
                string version = _versionLabel != null ? _versionLabel.Text : string.Empty;
                string body = $"{_titleLabel.Text}. {_descriptionLabel.Text} {version}".Trim();
                AccessibleDescription = body;
                if (_nextButton != null) _nextButton.AccessibleDescription = body;
            }
        }
    }
}
