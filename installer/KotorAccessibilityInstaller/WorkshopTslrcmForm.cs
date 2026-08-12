using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Installs the LOCALIZED TSLRCM for non-English players, by copying the
    /// TSLRCM team's official per-language Steam Workshop item into the game
    /// folder — taking the player's <c>dialog.tlk</c> from wherever it is
    /// actually right, which for all but one locale means leaving it alone.
    ///
    /// <para><b>Where a localized text table comes from.</b> Not from TSLRCM.
    /// The German, French, Italian and Spanish Workshop items contain no text
    /// table at all (verified against the German item — 677 files, not one
    /// <c>.tlk</c> among them). Those locales already have a vanilla localized
    /// <c>dialog.tlk</c> from their own Steam language depot, and the
    /// translation teams embedded TSLRCM's new player-visible strings directly
    /// in the <c>.dlg</c> files (28 German strings across 10 companion
    /// dialogs) so that nothing has to be appended to it. Measured: de, fr, it
    /// and es all carry 136,329 strings, vanilla's own count, and the dev's
    /// TSLRCM-modified German install carries exactly the same — TSLRCM never
    /// touched it.</para>
    ///
    /// <para><b>Russian is the exception, and needs the opposite.</b>
    /// LucasArts never shipped a Russian KOTOR 2, so there is no vanilla
    /// Russian table to defer to; the Russian team translated the whole thing
    /// themselves and their item DOES ship a <c>dialog.tlk</c> (136,375
    /// strings, Windows-1251). Skipping it — which a blanket "never touch the
    /// tlk" rule did — left Russian players with translated content files and
    /// an English text table for everything else. See
    /// <see cref="InstallItemTextTable"/>.</para>
    ///
    /// <para><b>Why the English installer is wrong for these users.</b> The
    /// DeadlyStream TSLRCM 1.8.6 exe is English-only and ships a full English
    /// <c>dialog.tlk</c> that replaces the player's. On a German copy that
    /// turns the entire game's text English — a straight loss, since the
    /// German text was already correct.</para>
    ///
    /// <para><b>Why copying out is safe, when subscribing is not.</b> The
    /// DeadlyStream thread "Why not to use the Steam Workshop" documents the
    /// problems: Workshop mods live in separate folders read at startup, load
    /// order follows subscription order and gets randomised, and a Workshop
    /// <c>.2da</c> and an Override <c>.2da</c> cancel each other out entirely
    /// instead of merging. Every one of those failures needs the content to
    /// STAY subscribed. Copying the files into the game folder and
    /// unsubscribing turns them into ordinary Override/Modules files, which
    /// TSLPatcher-based mods (K2CP, the Tweak Pack) then patch normally — and
    /// they append their strings to the player's own localized table. That
    /// thread does not discuss copying out, so this is not a route it endorses;
    /// it is one where none of the problems it lists survive.</para>
    ///
    /// <para><b>Known gap, accepted.</b> The localized items omit ~10 files the
    /// English one installs: two English VO splices, the launcher skin, and 7
    /// <c>g_i_trapkit*.uti</c> item files. The trap-kit files differ from
    /// vanilla by exactly 3 bytes — a description StrRef re-pointed at
    /// corrected text in TSLRCM's English tlk (vanilla overstates the mines'
    /// damage and save DC). Shipping them without that tlk would point players
    /// at StrRefs their table lacks, so the omission is forced, not careless. A
    /// localized player keeps vanilla's wrong mine descriptions — exactly what
    /// they had unmodded, so no regression.</para>
    ///
    /// <para>MUST run before K2CP / Tweak Pack, which patch these files.</para>
    /// </summary>
    public class WorkshopTslrcmForm : Form
    {
        // Official TSLRCM language editions on the Steam Workshop (app 208580),
        // maintained by the TSLRCM team; the German item is author-confirmed
        // at 1.8.6 (2025 comment). IDs verified 2026-07-27.
        public static bool TryGetWorkshopItem(GameLocale locale, out string itemId)
        {
            switch (locale)
            {
                case GameLocale.German: itemId = "485551190"; return true;
                case GameLocale.French: itemId = "485553656"; return true;
                case GameLocale.Italian: itemId = "485556965"; return true;
                case GameLocale.Spanish: itemId = "485555217"; return true;
                case GameLocale.Russian: itemId = "2143250983"; return true;
                default: itemId = null; return false;
            }
        }

        /// <summary>
        /// Whether this locale's Workshop item carries its own
        /// <c>dialog.tlk</c>. Russian alone does, for the reason given in the
        /// class summary.
        ///
        /// <para>This decides only what we PROMISE the user before the
        /// download — "your game's text stays as it is" versus "this brings
        /// the Russian text with it". The install itself follows what is
        /// actually in the item, so a wrong answer here misphrases one
        /// sentence rather than installing the wrong thing.</para>
        /// </summary>
        public static bool ItemShipsTextTable(GameLocale locale) => locale == GameLocale.Russian;

        // Give the user ample time to find the Subscribe button and Steam time
        // to pull ~335 MB; the Cancel button is live the whole way.
        private static readonly TimeSpan OverallTimeout = TimeSpan.FromMinutes(20);

        private Label _titleLabel;
        private Label _statusLabel;
        private ProgressBar _progressBar;
        private Button _cancelButton;
        private Button _reopenButton;

        private readonly string _k2GamePath;
        private readonly GameLocale _expectedLocale;
        private readonly string _itemId;
        private readonly CancellationTokenSource _cts = new CancellationTokenSource();
        private bool _finished;

        public bool Success { get; private set; }

        /// <summary>
        /// Whether the item's own <c>dialog.tlk</c> was installed over the
        /// game's. Only the Russian item ships one; the caller uses this to
        /// tell the player their text table changed.
        /// </summary>
        public bool TextTableInstalled { get; private set; }

        /// <summary>Failure detail; null when the user cancelled or timed out silently.</summary>
        public string FailureReason { get; private set; }

        public WorkshopTslrcmForm(string k2GamePath, GameLocale expectedLocale, string itemId)
        {
            _k2GamePath = k2GamePath ?? throw new ArgumentNullException(nameof(k2GamePath));
            _expectedLocale = expectedLocale;
            _itemId = itemId ?? throw new ArgumentNullException(nameof(itemId));
            InitializeComponents();
            Shown += async (s, e) => await RunAsync();
            FormClosing += (s, e) =>
            {
                if (_finished) return;
                Logger.Info("Workshop TSLRCM window closed; cancelling");
                _cts.Cancel();
            };
        }

        private void InitializeComponents()
        {
            Text = Config.DisplayName;
            Size = new Size(560, 200);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            _titleLabel = new Label
            {
                Font = new Font(Font.FontFamily, 12, FontStyle.Bold),
                Location = new Point(20, 15),
                Size = new Size(510, 28),
                TextAlign = ContentAlignment.MiddleCenter,
                Text = "TSLRCM"
            };

            _statusLabel = new Label
            {
                Location = new Point(20, 50),
                Size = new Size(510, 55),
                TextAlign = ContentAlignment.TopLeft,
                Text = InstallerLocale.Get("K2Lang_Waiting")
            };

            _progressBar = new ProgressBar
            {
                Location = new Point(20, 110),
                Size = new Size(510, 20),
                Style = ProgressBarStyle.Marquee
            };
            _progressBar.AccessibleName = Text;

            // Reopening the Workshop page has to be possible at any time. The
            // page is opened once automatically, but Steam can land on the wrong
            // view, or the user can close it — and until this existed, that left
            // them staring at a form waiting forever for a subscription they had
            // no way to reach.
            _reopenButton = new Button
            {
                Location = new Point(20, 135),
                Size = new Size(260, 30),
                Text = InstallerLocale.Get("K2Lang_ReopenPageButton")
            };
            _reopenButton.Click += (s, e) => OpenWorkshopPage(reopened: true);

            _cancelButton = new Button
            {
                Location = new Point(390, 135),
                Size = new Size(140, 30),
                Text = InstallerLocale.Get("Main_CancelButton")
            };
            _cancelButton.Click += (s, e) =>
            {
                Logger.Info("Workshop TSLRCM install cancelled via button");
                _cts.Cancel();
            };

            Controls.AddRange(new Control[]
            {
                _titleLabel, _statusLabel, _progressBar, _reopenButton, _cancelButton
            });

            CancelButton = _cancelButton;
        }

        /// <summary>
        /// Open the item's Workshop page in Steam. Called once at start and
        /// again whenever the user presses the reopen button.
        /// </summary>
        private void OpenWorkshopPage(bool reopened)
        {
            string pageUrl = $"steam://url/CommunityFilePage/{_itemId}";
            try
            {
                Logger.Info($"{(reopened ? "Reopening" : "Opening")} Workshop page: {pageUrl}");
                Process.Start(new ProcessStartInfo { FileName = pageUrl, UseShellExecute = true });

                // Steam takes the foreground and buries this window behind its
                // own. For a sighted user that is a nuisance; for a screen
                // reader it means the dialog explaining what to do next, and the
                // button to reopen the page, simply are not there any more —
                // reported as "the Workshop page was open but the dialog waiting
                // on it was not". Claw focus back once Steam has settled.
                ReclaimForeground();

                if (reopened)
                    UpdateStatus(InstallerLocale.Get("K2Lang_Waiting"), announce: true);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not open the Workshop page: {ex.Message}");
                UpdateStatus(InstallerLocale.Format("K2Lang_PageOpenFailed_Format", pageUrl), announce: true);
            }
        }

        /// <summary>
        /// Bring this window back in front of Steam, shortly after Steam has
        /// been asked to open a page.
        ///
        /// <para>Windows only lets the foreground process hand focus over, and
        /// Steam is not going to. Briefly setting TopMost is the standard way
        /// for an installer to get its own dialog back in front; it is dropped
        /// again immediately so the window does not sit permanently above
        /// everything the user might switch to.</para>
        ///
        /// <para>Delayed rather than immediate: Steam raises its window a moment
        /// after the protocol handler returns, so activating right away just
        /// loses the race.</para>
        /// </summary>
        private void ReclaimForeground()
        {
            var timer = new System.Windows.Forms.Timer { Interval = 1200 };
            timer.Tick += (s, e) =>
            {
                timer.Stop();
                timer.Dispose();
                try
                {
                    if (IsDisposed || Disposing) return;
                    TopMost = true;
                    Activate();
                    BringToFront();
                    TopMost = false;
                    // Re-announce so a screen reader picks the window up again;
                    // regaining focus silently would leave the user unsure which
                    // window they are in.
                    ScreenReaderAnnouncer.Announce(_statusLabel, _statusLabel.Text);
                }
                catch (Exception ex)
                {
                    Logger.Warning($"Could not bring the Workshop dialog back to the front: {ex.Message}");
                }
            };
            timer.Start();
        }

        private async Task RunAsync()
        {
            try
            {
                // <lib>/steamapps/common/<game> -> <lib>/steamapps/workshop/content/<app>/<item>
                string itemDir = Path.GetFullPath(Path.Combine(
                    _k2GamePath, "..", "..", "workshop", "content",
                    Config.Kotor2WorkshopAppId, _itemId));
                Logger.Info($"Workshop TSLRCM: expecting content under {itemDir}");

                OpenWorkshopPage(reopened: false);

                UpdateStatus(InstallerLocale.Get("K2Lang_Waiting"), announce: true);

                if (!await WaitForItemDownloadAsync(itemDir))
                    return; // cancelled or timed out; reason already set

                UpdateStatus(InstallerLocale.Get("K2Lang_Copying"), announce: true);
                int copied = await Task.Run(() => CopyItemIntoGame(itemDir));

                if (copied == 0)
                {
                    FailureReason = InstallerLocale.Get("K2Lang_NothingCopied");
                    Logger.Warning($"Workshop TSLRCM: item at {itemDir} yielded no installable content");
                    return;
                }

                TextTableInstalled = await Task.Run(() => InstallItemTextTable(itemDir));

                Logger.Info($"Workshop TSLRCM ({_expectedLocale}): copied {copied} files into {_k2GamePath}; " +
                            (TextTableInstalled ? "the item's own dialog.tlk installed"
                                                : "the game's own dialog.tlk kept"));
                Success = true;
            }
            catch (Exception ex)
            {
                Logger.Error("Workshop TSLRCM install failed", ex);
                FailureReason = ex.Message;
            }
            finally
            {
                _finished = true;
                Close();
            }
        }

        /// <summary>
        /// Whether Steam's own workshop bookkeeping mentions this item yet.
        ///
        /// <para>Steam keeps <c>steamapps/workshop/appworkshop_&lt;appid&gt;.acf</c>
        /// with a <c>WorkshopItemsInstalled</c> block listing every subscribed
        /// item id. A subscription Steam has accepted and started puts the id in
        /// there; a Subscribe press that never reached the client leaves the
        /// block empty. That is the difference between "wait, it is coming" and
        /// "nothing is going to happen", and it is not visible from the content
        /// folder — which stays absent in both cases.</para>
        ///
        /// <para>Deliberately a substring test rather than a KeyValues parse: we
        /// only need to know whether the id is mentioned, and a parser for
        /// Valve's format would be a lot of surface area for a heartbeat
        /// message. Errs toward "Steam has it" on any read failure, so a locked
        /// or unreadable file never produces a false accusation.</para>
        /// </summary>
        private bool SteamKnowsWorkshopItem()
        {
            try
            {
                // <lib>/steamapps/common/<game> -> <lib>/steamapps
                var steamapps = Directory.GetParent(_k2GamePath)?.Parent;
                if (steamapps == null) return true;

                string acf = Path.Combine(steamapps.FullName, "workshop",
                                          $"appworkshop_{Config.Kotor2WorkshopAppId}.acf");
                if (!File.Exists(acf)) return false;

                return File.ReadAllText(acf).Contains(_itemId, StringComparison.Ordinal);
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not read Steam's workshop manifest: {ex.Message}");
                return true;
            }
        }

        /// <summary>
        /// Whether Steam has finished with this item: its folder exists, Steam's
        /// manifest lists it, and the bytes on disk match the size the manifest
        /// claims. The size comparison is what makes this safe to act on — a
        /// half-downloaded item is smaller, so we never call a download finished
        /// while it is still arriving.
        /// </summary>
        private bool ItemLooksFullyDownloaded(string itemDir)
        {
            try
            {
                if (!Directory.Exists(itemDir)) return false;
                if (!SteamKnowsWorkshopItem()) return false;

                long declared = DeclaredItemSize();
                if (declared <= 0) return false;

                long onDisk = 0;
                foreach (var f in Directory.EnumerateFiles(itemDir, "*", SearchOption.AllDirectories))
                    onDisk += new FileInfo(f).Length;

                return onDisk >= declared;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not size up the Workshop item: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// The item's <c>size</c> from Steam's manifest, or 0 if not stated.
        /// Read with a narrow regex rather than a KeyValues parser: one number
        /// out of a file we do not own does not justify one.
        /// </summary>
        private long DeclaredItemSize()
        {
            try
            {
                var steamapps = Directory.GetParent(_k2GamePath)?.Parent;
                if (steamapps == null) return 0;

                string acf = Path.Combine(steamapps.FullName, "workshop",
                                          $"appworkshop_{Config.Kotor2WorkshopAppId}.acf");
                if (!File.Exists(acf)) return 0;

                // "485551190" { "size" "335023091" ... }
                var match = System.Text.RegularExpressions.Regex.Match(
                    File.ReadAllText(acf),
                    "\"" + System.Text.RegularExpressions.Regex.Escape(_itemId) +
                    "\"\\s*\\{[^}]*?\"size\"\\s*\"(\\d+)\"",
                    System.Text.RegularExpressions.RegexOptions.Singleline);

                return match.Success && long.TryParse(match.Groups[1].Value, out long size) ? size : 0;
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not read the Workshop item's declared size: {ex.Message}");
                return 0;
            }
        }

        private static int CountFiles(string dir)
        {
            try { return Directory.GetFiles(dir, "*", SearchOption.AllDirectories).Length; }
            catch { return -1; }
        }

        /// <summary>
        /// Wait until Steam has the item fully on disk. True when it is ready,
        /// false when the user cancelled or the wait timed out.
        ///
        /// <para>"Ready" is the manifest listing the item AND the bytes on disk
        /// matching the size it declares — not merely the folder existing.
        /// Steam creates the folder early and fills it progressively, so a
        /// presence check alone would start copying a half-downloaded mod.</para>
        /// </summary>
        private async Task<bool> WaitForItemDownloadAsync(string itemDir)
        {
            long started = Environment.TickCount64;
            long lastAnnounce = started;

            while (true)
            {
                if (_cts.IsCancellationRequested)
                {
                    Logger.Info("Workshop TSLRCM install cancelled while waiting");
                    return false;
                }

                long now = Environment.TickCount64;
                if (now - started > OverallTimeout.TotalMilliseconds)
                {
                    FailureReason = $"Timed out waiting for the Steam Workshop download " +
                                    $"({(int)OverallTimeout.TotalMinutes} minutes).";
                    return false;
                }

                if (ItemLooksFullyDownloaded(itemDir))
                {
                    Logger.Info($"Workshop item {_itemId} fully downloaded: " +
                                $"{CountFiles(itemDir)} files at {itemDir}");
                    return true;
                }

                if (now - lastAnnounce >= 15000)
                {
                    lastAnnounce = now;
                    int elapsedSec = (int)((now - started) / 1000);

                    // Say WHICH kind of waiting this is. Steam records every
                    // subscribed item in appworkshop_<appid>.acf, so we can tell
                    // "Steam has not taken the subscription" apart from "Steam is
                    // downloading". Without that the heartbeat counted seconds
                    // identically in both cases -- and a subscription Steam never
                    // acted on looked exactly like a slow 335 MB download, with
                    // nothing to do but wait for a timeout that would never
                    // resolve.
                    bool steamHasItem = SteamKnowsWorkshopItem();
                    UpdateStatus(
                        InstallerLocale.Format(
                            steamHasItem ? "K2Lang_WaitingHeartbeat_Format"
                                         : "K2Lang_NotSubscribedHeartbeat_Format",
                            elapsedSec),
                        announce: true);
                }

                try { await Task.Delay(2000, _cts.Token); }
                catch (OperationCanceledException) { return false; }
            }
        }

        /// <summary>
        /// Folders inside the Workshop item that are game content, mapped to
        /// where they belong in the install. Everything else in the item -- the
        /// readmes, and a stray "modders resource" note the uploader left in --
        /// is not content and is skipped.
        ///
        /// <para>Verified complete against the English installer's own Inno log:
        /// per-folder counts match exactly (modules 87, lips 9, movies 3,
        /// override 471 vs 477). The item carries its voice-over subfolders
        /// inside streamvoice, so recursion covers them.</para>
        /// </summary>
        private static readonly string[] ContentFolders =
        {
            "override", "modules", "lips", "movies", "streammusic", "streamvoice",
        };

        /// <summary>
        /// Copy the item's content folders into the game, overwriting. Returns
        /// the number of files copied.
        ///
        /// <para>A <c>.tlk</c> inside a content folder is skipped: the engine
        /// reads its text table from the game root, so a copy in Override
        /// could only be a stray. The one text table that matters sits at the
        /// item root and is handled by <see cref="InstallItemTextTable"/>.</para>
        ///
        /// <para>Destination folder names come from the INSTALL, not the item.
        /// The item spells them lowercase; a real install may not, and creating
        /// a second "override" next to an existing "Override" would put half the
        /// mod somewhere the game does not read. Windows is case-insensitive so
        /// this is belt-and-braces, but the cost is one directory lookup.</para>
        /// </summary>
        private int CopyItemIntoGame(string itemDir)
        {
            int copied = 0;

            foreach (var folder in ContentFolders)
            {
                string src = Path.Combine(itemDir, folder);
                if (!Directory.Exists(src)) continue;

                string dest = ExistingGameFolder(folder);
                Directory.CreateDirectory(dest);

                foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
                {
                    if (Path.GetExtension(file).Equals(".tlk", StringComparison.OrdinalIgnoreCase))
                    {
                        Logger.Info($"Skipping {folder}/{Path.GetFileName(file)} -- a text table only " +
                                    "counts at the game root");
                        continue;
                    }

                    string relative = file.Substring(src.Length).TrimStart(Path.DirectorySeparatorChar,
                                                                           Path.AltDirectorySeparatorChar);
                    string target = Path.Combine(dest, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    File.Copy(file, target, overwrite: true);
                    copied++;
                }

                Logger.Info($"Workshop TSLRCM: {folder} -> {dest}");
            }

            return copied;
        }

        /// <summary>
        /// Install the item's own <c>dialog.tlk</c> when it ships one, keeping
        /// a backup of the game's. Returns whether a table was installed.
        ///
        /// <para>Driven by what is IN the item rather than by the locale: a
        /// localized item's text table is by definition that locale's own, so
        /// where one exists it is the right file for this player. In practice
        /// that is Russian only — see <see cref="ItemShipsTextTable"/> — and
        /// the item root is where it sits, outside the content folders
        /// <see cref="CopyItemIntoGame"/> walks.</para>
        ///
        /// <para>The backup is written once and never overwritten. Running the
        /// installer twice would otherwise back the Russian table up over the
        /// pristine original, which is the copy worth keeping.</para>
        ///
        /// <para>Copy errors are deliberately NOT swallowed — same as the
        /// content copy. Silently returning false here would leave a Russian
        /// player with translated content, an English text table, and a
        /// success message saying otherwise.</para>
        /// </summary>
        private bool InstallItemTextTable(string itemDir)
        {
            string src = Path.Combine(itemDir, "dialog.tlk");
            if (!File.Exists(src))
            {
                if (ItemShipsTextTable(_expectedLocale))
                {
                    Logger.Warning($"Workshop item {_itemId} ships no dialog.tlk although " +
                                   $"{_expectedLocale} expects one; the game's own text table stays");
                }
                return false;
            }

            string dest = Path.Combine(_k2GamePath, "dialog.tlk");
            string backup = dest + ".pre-tslrcm.bak";

            if (File.Exists(dest) && !File.Exists(backup))
            {
                File.Copy(dest, backup);
                Logger.Info($"Backed the game's text table up as {Path.GetFileName(backup)}");
            }

            File.Copy(src, dest, overwrite: true);
            Logger.Info($"Installed the item's dialog.tlk ({new FileInfo(src).Length} bytes) " +
                        $"as the {_expectedLocale} text table");
            return true;
        }

        /// <summary>
        /// The install's own spelling of a content folder, or the item's
        /// spelling when the install has no such folder yet.
        /// </summary>
        private string ExistingGameFolder(string folderName)
        {
            try
            {
                foreach (var dir in Directory.EnumerateDirectories(_k2GamePath))
                {
                    if (Path.GetFileName(dir).Equals(folderName, StringComparison.OrdinalIgnoreCase))
                        return dir;
                }
            }
            catch (Exception ex)
            {
                Logger.Warning($"Could not scan the game folder for '{folderName}': {ex.Message}");
            }
            return Path.Combine(_k2GamePath, folderName);
        }

        private void UpdateStatus(string message, bool announce)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action(() => UpdateStatus(message, announce)));
                return;
            }
            _statusLabel.Text = message;
            if (!announce) return;

            ScreenReaderAnnouncer.Announce(_statusLabel, message);
        }
    }
}
