using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// The fallback that cannot go stale: when we cannot fetch a mod ourselves,
    /// the user fetches it and hands it to us, and the install continues
    /// automatically from there.
    ///
    /// <para><b>Why this exists.</b> Every automatic download here depends on
    /// something outside our control staying put — DeadlyStream's download flow,
    /// a file's hash, the site existing at all. Pins can be refreshed
    /// (<see cref="SourcePins"/>), but only while somebody is maintaining the
    /// repo. This path depends on nothing: as long as the mod can be downloaded
    /// by a human, the installer can still install it. In five years it is
    /// likely the only path that still works.</para>
    ///
    /// <para><b>Why accepting a user-supplied file is not a security
    /// downgrade.</b> The SHA-256 pins exist because we fetch bytes
    /// automatically, from a scraped endpoint, with no human judgment involved —
    /// and then run one of them elevated. When the user downloads the file
    /// themselves from the official page, they have supplied the provenance.
    /// That is the same trust as every manual mod install ever done, and better,
    /// because we drive the rest correctly afterwards. So the pin is never
    /// relaxed for an automatic download, and never demanded of a manual one.
    /// If the file does happen to match the pin, we say so.</para>
    ///
    /// <para><b>Screen reader.</b> The explanation is a focusable read-only
    /// multiline TextBox (the ModdingInfoForm pattern) so it can be read line by
    /// line rather than only as a control name. If a plausible file is already
    /// sitting in Downloads it gets its own button, so the common case is one
    /// keystroke and no file dialog at all.</para>
    /// </summary>
    public class ManualDownloadForm : Form
    {
        /// <summary>Path the user supplied, or null if they skipped.</summary>
        public string SelectedFilePath { get; private set; }

        private readonly string _pageUrl;
        private readonly string _expectedFileName;
        private readonly FileKind _kind;
        private readonly string _pinnedSha256;

        private Label _statusLabel;

        /// <summary>
        /// What the file should be, so a mis-picked file is caught immediately
        /// with a spoken reason rather than failing confusingly several steps
        /// later. This is a mistake check, not a security check — the user
        /// already vouched for the file.
        /// </summary>
        public enum FileKind
        {
            /// <summary>Windows executable — "MZ".</summary>
            Executable,
            /// <summary>RAR archive — "Rar!\x1a\x07" (RAR4 and RAR5 share the prefix).</summary>
            RarArchive,
            /// <summary>Zip archive — "PK\x03\x04".</summary>
            ZipArchive,
        }

        public ManualDownloadForm(
            string modDisplayName,
            string reason,
            string pageUrl,
            string expectedFileName,
            FileKind kind,
            string pinnedSha256)
        {
            _pageUrl = pageUrl;
            _expectedFileName = expectedFileName;
            _kind = kind;
            _pinnedSha256 = pinnedSha256;
            InitializeComponents(modDisplayName, reason);
        }

        private void InitializeComponents(string modDisplayName, string reason)
        {
            Text = InstallerLocale.Format("ManualDownload_Title_Format", modDisplayName);
            Size = new Size(680, 460);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var titleLabel = new Label
            {
                Text = Text,
                Font = new Font(Font.FontFamily, 14, FontStyle.Bold),
                Location = new Point(20, 20),
                Size = new Size(620, 30),
                TextAlign = ContentAlignment.MiddleCenter
            };

            var body = new TextBox
            {
                Multiline = true,
                ReadOnly = true,
                ScrollBars = ScrollBars.Vertical,
                Location = new Point(20, 60),
                Size = new Size(620, 200),
                Text = InstallerLocale.NormalizeLineBreaks(InstallerLocale.Format(
                    "ManualDownload_Body_Format", modDisplayName, reason, _expectedFileName, _pageUrl)),
                TabStop = true
            };
            body.AccessibleName = InstallerLocale.Format("ManualDownload_Title_Format", modDisplayName);

            _statusLabel = new Label
            {
                Location = new Point(20, 268),
                Size = new Size(620, 40),
                TextAlign = ContentAlignment.TopLeft,
                Text = string.Empty
            };

            const int btnY = 320;
            const int btnH = 35;
            var buttons = new List<Button>();

            var openPageButton = new Button
            {
                Text = InstallerLocale.Get("ManualDownload_OpenPageButton"),
                Size = new Size(190, btnH)
            };
            openPageButton.Click += (s, e) => OpenDownloadPage();
            buttons.Add(openPageButton);

            // If something that looks right is already in Downloads, offer it
            // directly — for the common case (user just downloaded it) this
            // turns the whole dialog into one keypress.
            string detected = FindLikelyDownload();
            if (detected != null)
            {
                var useDetectedButton = new Button
                {
                    Text = InstallerLocale.Format("ManualDownload_UseDetectedButton_Format",
                                                  Path.GetFileName(detected)),
                    Size = new Size(250, btnH)
                };
                useDetectedButton.Click += (s, e) => Accept(detected);
                buttons.Add(useDetectedButton);
                AnnounceStatus(InstallerLocale.Format("ManualDownload_Detected_Format",
                                                      Path.GetFileName(detected)));
            }

            var browseButton = new Button
            {
                Text = InstallerLocale.Get("ManualDownload_ChooseFileButton"),
                Size = new Size(190, btnH)
            };
            browseButton.Click += (s, e) => BrowseForFile();
            buttons.Add(browseButton);

            var skipButton = new Button
            {
                Text = InstallerLocale.Get("ManualDownload_SkipButton"),
                Size = new Size(120, btnH)
            };
            skipButton.Click += (s, e) => { SelectedFilePath = null; Close(); };
            buttons.Add(skipButton);

            // Escape skips. Deliberately no AcceptButton: Enter should not pick
            // an action when the actions differ this much in consequence.
            CancelButton = skipButton;

            int x = 20;
            foreach (var b in buttons)
            {
                b.Location = new Point(x, btnY);
                x += b.Width + 10;
            }

            Controls.Add(titleLabel);
            Controls.Add(body);
            Controls.Add(_statusLabel);
            foreach (var b in buttons) Controls.Add(b);

            AccessibleDescription = body.Text;
        }

        private void OpenDownloadPage()
        {
            try
            {
                Logger.Info($"Opening manual download page: {_pageUrl}");
                Process.Start(new ProcessStartInfo(_pageUrl) { UseShellExecute = true });
                AnnounceStatus(InstallerLocale.Get("ManualDownload_PageOpened"));
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not open the download page: {ex.Message}");
                AnnounceStatus(InstallerLocale.Format("ManualDownload_PageOpenFailed_Format", _pageUrl));
            }
        }

        private void BrowseForFile()
        {
            using var dialog = new OpenFileDialog
            {
                Title = InstallerLocale.Get("ManualDownload_ChooseFileButton"),
                Filter = _kind switch
                {
                    FileKind.Executable => "Programs (*.exe)|*.exe|All files (*.*)|*.*",
                    FileKind.ZipArchive => "Archives (*.zip)|*.zip|All files (*.*)|*.*",
                    _ => "Archives (*.rar)|*.rar|All files (*.*)|*.*",
                },
                CheckFileExists = true,
                Multiselect = false,
            };

            string downloads = GetDownloadsDir();
            if (Directory.Exists(downloads)) dialog.InitialDirectory = downloads;

            if (dialog.ShowDialog(this) == DialogResult.OK)
                Accept(dialog.FileName);
        }

        /// <summary>
        /// Validate shape, report what we know about the file, and close with it
        /// if it is usable.
        /// </summary>
        private void Accept(string path)
        {
            try
            {
                var info = new FileInfo(path);
                if (!info.Exists || info.Length == 0)
                {
                    AnnounceStatus(InstallerLocale.Get("ManualDownload_Invalid_Empty"));
                    return;
                }

                if (!HasExpectedMagic(path))
                {
                    // Wrong kind of file — almost always a mis-pick (the .html
                    // download page saved instead of the file, or the wrong
                    // archive). Say so and let them try again.
                    AnnounceStatus(InstallerLocale.Format(
                        "ManualDownload_Invalid_Kind_Format", Path.GetFileName(path)));
                    return;
                }

                // Not required, but worth knowing: if it matches the pin, the
                // user fetched exactly the version we were built against.
                if (!string.IsNullOrEmpty(_pinnedSha256))
                {
                    string hash = DeadlyStreamClient.ComputeSha256(path);
                    bool matches = hash.Equals(_pinnedSha256, StringComparison.OrdinalIgnoreCase);
                    Logger.Info($"Manually supplied {Path.GetFileName(path)}: " +
                                $"sha256={hash} (matches pin: {matches})");
                }

                SelectedFilePath = path;
                Logger.Info($"Using manually supplied file: {path}");
                Close();
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not use the chosen file: {ex.Message}");
                AnnounceStatus(InstallerLocale.Format("ManualDownload_Invalid_Unreadable_Format", ex.Message));
            }
        }

        private bool HasExpectedMagic(string path)
        {
            try
            {
                using var fs = File.OpenRead(path);
                var head = new byte[4];
                if (fs.Read(head, 0, head.Length) < head.Length) return false;

                return _kind switch
                {
                    FileKind.Executable => head[0] == (byte)'M' && head[1] == (byte)'Z',
                    FileKind.RarArchive => head[0] == (byte)'R' && head[1] == (byte)'a' &&
                                           head[2] == (byte)'r' && head[3] == (byte)'!',
                    FileKind.ZipArchive => head[0] == (byte)'P' && head[1] == (byte)'K' &&
                                           head[2] == 0x03 && head[3] == 0x04,
                    _ => true,
                };
            }
            catch { return false; }
        }

        /// <summary>
        /// Newest file in Downloads that plausibly IS the mod: the exact
        /// expected filename first, then anything with the right extension and
        /// the right magic bytes. Returns null when nothing fits, in which case
        /// the dialog simply doesn't offer the shortcut.
        /// </summary>
        private string FindLikelyDownload()
        {
            try
            {
                string downloads = GetDownloadsDir();
                if (!Directory.Exists(downloads)) return null;

                string exact = Path.Combine(downloads, _expectedFileName);
                if (File.Exists(exact) && HasExpectedMagic(exact)) return exact;

                string pattern = _kind switch
                {
                    FileKind.Executable => "*.exe",
                    FileKind.ZipArchive => "*.zip",
                    _ => "*.rar",
                };
                FileInfo best = null;
                foreach (var path in Directory.EnumerateFiles(downloads, pattern))
                {
                    var info = new FileInfo(path);
                    if (info.Length == 0 || !HasExpectedMagic(path)) continue;
                    if (best == null || info.LastWriteTimeUtc > best.LastWriteTimeUtc) best = info;
                }
                return best?.FullName;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not scan Downloads for a manual copy: {ex.Message}");
                return null;
            }
        }

        // .NET's SpecialFolder enum has no Downloads entry; same user-profile
        // fallback LogCollector uses.
        private static string GetDownloadsDir() =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");

        private void AnnounceStatus(string message)
        {
            _statusLabel.Text = message;
            Logger.Info($"[ManualDownload] {message}");
            ScreenReaderAnnouncer.Announce(_statusLabel, message);
        }

        /// <summary>
        /// Show the dialog and return the file the user supplied, or null if
        /// they skipped. Convenience wrapper so callers do not have to remember
        /// the Application.Run pattern the rest of this installer uses.
        /// </summary>
        public static string Ask(
            string modDisplayName,
            string reason,
            string pageUrl,
            string expectedFileName,
            FileKind kind,
            string pinnedSha256)
        {
            var form = new ManualDownloadForm(
                modDisplayName, reason, pageUrl, expectedFileName, kind, pinnedSha256);
            Application.Run(form);
            return form.SelectedFilePath;
        }
    }
}
