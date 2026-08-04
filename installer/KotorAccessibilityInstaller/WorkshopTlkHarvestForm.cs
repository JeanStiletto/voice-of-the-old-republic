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
    /// Fetches the localized TSLRCM <c>dialog.tlk</c> from the TSLRCM team's
    /// official per-language Steam Workshop items and copies it into the
    /// KOTOR 2 folder over the English one the DeadlyStream installer left.
    ///
    /// Why this shape: the DeadlyStream TSLRCM 1.8.6 exe is English-only
    /// (verified by extraction — see docs/installer.md), the localized
    /// editions exist ONLY as Workshop items, and those are UGC-depot hosted
    /// (GetPublishedFileDetails returns an empty file_url), so there is no
    /// anonymous direct download. But every affected user owns KOTOR 2 on
    /// Steam by definition, so we automate the community-endorsed route:
    /// open the Workshop page (the user presses Subscribe — the one manual
    /// step, in Steam's own UI), poll the library's
    /// <c>workshop/content/&lt;app&gt;/&lt;item&gt;/</c> folder for
    /// dialog.tlk, wait for the size to go stable, verify the language via
    /// <see cref="GameLocaleDetector"/> (content probe included, so Russian
    /// works despite its English language ID), back up the English tlk as
    /// <c>dialog.tlk.english.bak</c>, and copy the localized one in. The
    /// completion message tells the user to unsubscribe again so the
    /// Workshop copy never coexists with the directory-installed mods.
    ///
    /// MUST run after TSLRCM but BEFORE K2CP / Tweak Pack: those append
    /// strings to dialog.tlk, and replacing the file afterwards would orphan
    /// their strrefs.
    /// </summary>
    public class WorkshopTlkHarvestForm : Form
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

        /// <summary>Failure detail; null when the user cancelled or timed out silently.</summary>
        public string FailureReason { get; private set; }

        public WorkshopTlkHarvestForm(string k2GamePath, GameLocale expectedLocale, string itemId)
        {
            _k2GamePath = k2GamePath ?? throw new ArgumentNullException(nameof(k2GamePath));
            _expectedLocale = expectedLocale;
            _itemId = itemId ?? throw new ArgumentNullException(nameof(itemId));
            InitializeComponents();
            Shown += async (s, e) => await RunAsync();
            FormClosing += (s, e) =>
            {
                if (_finished) return;
                Logger.Info("Workshop tlk harvest window closed; cancelling");
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
                Logger.Info("Workshop tlk harvest cancelled via button");
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
                Logger.Info($"Workshop tlk harvest: expecting content under {itemDir}");

                OpenWorkshopPage(reopened: false);

                UpdateStatus(InstallerLocale.Get("K2Lang_Waiting"), announce: true);

                string tlkPath = await WaitForWorkshopTlkAsync(itemDir);
                if (tlkPath == null) return; // cancelled or timed out; reason already set

                UpdateStatus(InstallerLocale.Get("K2Tslrcm_Verifying"), announce: true);

                // Reuse the installer's locale detector on the tlk's own folder:
                // covers the header ID for DE/FR/IT/ES and the CP1251 content
                // probe for Russian (whose tlk declares the English ID).
                var detected = GameLocaleDetector.Detect(Path.GetDirectoryName(tlkPath));
                if (detected != _expectedLocale)
                {
                    FailureReason =
                        $"The Workshop item's dialog.tlk reads as {detected}, expected {_expectedLocale}. " +
                        "Not installed.";
                    Logger.Warning($"Workshop tlk harvest: {FailureReason}");
                    return;
                }

                string gameTlk = Path.Combine(_k2GamePath, "dialog.tlk");
                string backup = Path.Combine(_k2GamePath, "dialog.tlk.english.bak");
                File.Copy(gameTlk, backup, overwrite: true);
                File.Copy(tlkPath, gameTlk, overwrite: true);
                Logger.Info($"Workshop tlk harvest: {detected} dialog.tlk installed " +
                            $"({new FileInfo(gameTlk).Length} bytes); English backup at {backup}");
                Success = true;
            }
            catch (Exception ex)
            {
                Logger.Error("Workshop tlk harvest failed", ex);
                FailureReason = ex.Message;
            }
            finally
            {
                _finished = true;
                Close();
            }
        }

        /// <summary>
        /// Polls the workshop item folder for dialog.tlk until it exists with a
        /// stable size (Steam finished writing it). Returns null on cancel
        /// (FailureReason stays null) or timeout (FailureReason set).
        /// </summary>
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

        private async Task<string> WaitForWorkshopTlkAsync(string itemDir)
        {
            long started = Environment.TickCount64;
            long lastAnnounce = started;
            long lastSize = -1;
            string candidate = null;

            while (true)
            {
                if (_cts.IsCancellationRequested)
                {
                    Logger.Info("Workshop tlk harvest cancelled while waiting");
                    return null;
                }

                long now = Environment.TickCount64;
                if (now - started > OverallTimeout.TotalMilliseconds)
                {
                    FailureReason = "Timed out waiting for the Steam Workshop download " +
                                    $"({(int)OverallTimeout.TotalMinutes} minutes).";
                    return null;
                }

                try
                {
                    candidate = Directory.Exists(itemDir)
                        ? Directory.EnumerateFiles(itemDir, "dialog.tlk", SearchOption.AllDirectories)
                            .FirstOrDefault()
                        : null;
                }
                catch (Exception ex)
                {
                    Logger.Warning($"Workshop poll error: {ex.Message}");
                    candidate = null;
                }

                // The item is fully downloaded but carries no dialog.tlk. Steam
                // has done everything it is going to do, so waiting longer is
                // waiting for something that cannot arrive.
                //
                // This is the case the whole feature was designed around and got
                // wrong: the (German) TSLRCM Workshop item ships localized
                // CONTENT — override, modules, streamvoice, lips — and no text
                // table at all. Polling for dialog.tlk therefore hung until the
                // ten-minute timeout with a heartbeat cheerfully counting
                // seconds. Fail immediately and say what was actually found.
                if (candidate == null && ItemLooksFullyDownloaded(itemDir))
                {
                    FailureReason = InstallerLocale.Get("K2Lang_NoTlkInItem");
                    Logger.Warning($"Workshop item {_itemId} is present ({CountFiles(itemDir)} files) " +
                                   "but contains no dialog.tlk; the localized text cannot be taken from it.");
                    return null;
                }

                if (candidate != null)
                {
                    long size = new FileInfo(candidate).Length;
                    // Two consecutive polls with the same non-trivial size =
                    // Steam is done writing. The vanilla-scale tlk is ~10 MB;
                    // 1 MB screens out a partially-written header.
                    if (size > 1024 * 1024 && size == lastSize)
                    {
                        Logger.Info($"Workshop tlk found: {candidate} ({size} bytes, stable)");
                        return candidate;
                    }
                    lastSize = size;
                }

                if (now - lastAnnounce >= 15000)
                {
                    lastAnnounce = now;
                    int elapsedSec = (int)((now - started) / 1000);

                    // Say WHICH kind of waiting this is. Steam records every
                    // subscribed item in appworkshop_<appid>.acf, so we can tell
                    // "Steam has not taken the subscription" apart from "Steam is
                    // downloading". Without that the heartbeat counted seconds
                    // identically in both cases — and a subscription Steam never
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
                catch (OperationCanceledException) { return null; }
            }
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
