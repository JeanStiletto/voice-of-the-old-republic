using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// "You picked a game that isn't on this PC" — with store links for it.
    ///
    /// <para>Shown after the game-selection screen, not before. The welcome
    /// wizard used to carry KOTOR 1 store links on a page of its own, which was
    /// wrong twice over: it appeared before the user had said which games they
    /// wanted, and it only ever offered KOTOR 1, so someone here to mod KOTOR 2
    /// was pointed at the wrong game. Asking first and then offering links for
    /// what they actually chose is the order that makes sense.</para>
    ///
    /// <para>Only games that could not be found are listed. If everything the
    /// user selected is already installed, this screen does not appear at all —
    /// a page telling you where to buy something you own is noise, and for a
    /// screen-reader user noise costs real time.</para>
    /// </summary>
    public class GameStoreLinksForm : Form
    {
        /// <summary>False when the user backed out rather than continuing.</summary>
        public bool ProceedWithInstall { get; private set; }

        private readonly IReadOnlyList<GameTarget> _missing;

        /// <summary>
        /// Show the screen for <paramref name="missing"/>, or return true
        /// immediately when nothing is missing.
        /// </summary>
        public static bool ShowIfNeeded(IReadOnlyList<GameTarget> missing)
        {
            if (missing == null || missing.Count == 0) return true;

            var form = new GameStoreLinksForm(missing);
            Application.Run(form);
            return form.ProceedWithInstall;
        }

        private GameStoreLinksForm(IReadOnlyList<GameTarget> missing)
        {
            _missing = missing;
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Text = InstallerLocale.Get("GameLinks_Title");
            Size = new Size(560, 200 + _missing.Count * 90);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = InstallerLocale.Get("GameLinks_Title"),
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 15),
                Size = new Size(500, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            var descriptionLabel = new Label
            {
                Text = InstallerLocale.Get("GameLinks_Description"),
                Location = new Point(20, 50),
                Size = new Size(500, 60),
                TextAlign = ContentAlignment.TopLeft
            };

            Controls.Add(titleLabel);
            Controls.Add(descriptionLabel);

            // One labelled row per missing game, so the two stores are never
            // ambiguous about which game they belong to.
            int y = 120;
            foreach (var target in _missing)
            {
                var gameLabel = new Label
                {
                    Text = InstallerLocale.Format("GameLinks_NotFound_Format", target.DisplayName),
                    Location = new Point(20, y),
                    Size = new Size(500, 22),
                    Font = new Font(Font.FontFamily, 9, FontStyle.Bold)
                };

                var steamButton = new Button
                {
                    Text = InstallerLocale.Format("GameLinks_SteamButton_Format", target.DisplayName),
                    Location = new Point(20, y + 24),
                    Size = new Size(240, 32)
                };
                var steamUrl = target.StorePageSteam;
                steamButton.Click += (s, e) => OpenUrl(steamUrl);

                var gogButton = new Button
                {
                    Text = InstallerLocale.Format("GameLinks_GogButton_Format", target.DisplayName),
                    Location = new Point(270, y + 24),
                    Size = new Size(240, 32)
                };
                var gogUrl = target.StorePageGog;
                gogButton.Click += (s, e) => OpenUrl(gogUrl);

                Controls.Add(gameLabel);
                Controls.Add(steamButton);
                Controls.Add(gogButton);
                y += 90;
            }

            var backButton = new Button
            {
                Text = InstallerLocale.Get("ModdingInfo_BackButton"),
                Location = new Point(20, y + 5),
                Size = new Size(140, 35)
            };
            backButton.Click += (s, e) => Close();

            var continueButton = new Button
            {
                Text = InstallerLocale.Get("ModdingInfo_NextButton"),
                Location = new Point(380, y + 5),
                Size = new Size(140, 35)
            };
            continueButton.Click += (s, e) => { ProceedWithInstall = true; Close(); };

            Controls.Add(backButton);
            Controls.Add(continueButton);

            AcceptButton = continueButton;
            CancelButton = backButton;

            string body = $"{titleLabel.Text}. {descriptionLabel.Text}";
            AccessibleDescription = body;
            continueButton.AccessibleDescription = body;
        }

        private static void OpenUrl(string url)
        {
            try
            {
                Logger.Info($"Opening store page: {url}");
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not open {url}: {ex.Message}");
            }
        }
    }
}
