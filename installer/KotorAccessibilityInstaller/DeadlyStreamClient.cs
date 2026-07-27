using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Guest download from deadlystream.com (an Invision Community site).
    /// DeadlyStream serves file downloads to guests without an account, but
    /// there is no static direct URL: the download endpoint requires the
    /// session cookie plus the per-session <c>csrfKey</c> embedded in the file
    /// page's HTML. So this is a two-step scrape: GET the file page (collect
    /// cookies, extract the csrfKey), then GET
    /// <c>?do=download&amp;csrfKey=&lt;key&gt;</c> and stream the attachment.
    ///
    /// Verified working as a guest on 2026-07-27 (TSLRCM file 578: HTTP 200
    /// with Content-Disposition). Deliberately defensive: this depends on the
    /// site's markup and could break on any forum-software upgrade, so every
    /// step throws a descriptive error and callers fall back to handing the
    /// user the page URL for a manual browser download. The long-term plan
    /// stays a permission-based mirror (docs/installer.md, "KOTOR 2 mod
    /// bundle"); this client exists so users are not blocked until then.
    /// </summary>
    public sealed class DeadlyStreamClient : IDisposable
    {
        // Browser-like UA. The site currently serves guests regardless of UA,
        // but a bare HttpClient UA is the first thing bot rules key on. We are
        // not hiding — the rate is one file per install run.
        private const string UserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)";

        private static readonly Regex CsrfKeyRegex =
            new Regex(@"csrfKey=([0-9a-f]{16,64})", RegexOptions.Compiled);

        private readonly HttpClient _http;

        public DeadlyStreamClient()
        {
            var handler = new HttpClientHandler
            {
                CookieContainer = new CookieContainer(),
                UseCookies = true,
                AllowAutoRedirect = true,
            };
            _http = new HttpClient(handler);
            _http.DefaultRequestHeaders.Add("User-Agent", UserAgent);
            // Covers a 138 MB file on a slow connection; cancellation is the
            // user-facing abort path, the timeout is just the safety net.
            _http.Timeout = TimeSpan.FromMinutes(30);
        }

        /// <summary>
        /// Download a DeadlyStream file to <paramref name="destPath"/>.
        /// <paramref name="progress"/> reports (downloadedBytes, totalBytes);
        /// totalBytes is -1 when the server sends no Content-Length.
        /// </summary>
        public async Task DownloadFileAsync(
            string filePageUrl, string destPath,
            Action<long, long> progress = null,
            CancellationToken cancel = default)
        {
            Logger.Info($"DeadlyStream: fetching file page {filePageUrl}");
            string page = await _http.GetStringAsync(filePageUrl, cancel);

            var m = CsrfKeyRegex.Match(page);
            if (!m.Success)
            {
                throw new InvalidOperationException(
                    "Could not find a csrfKey on the DeadlyStream file page — " +
                    "the site layout may have changed.");
            }
            string csrfKey = m.Groups[1].Value;

            string downloadUrl = $"{filePageUrl.TrimEnd('/')}/?do=download&csrfKey={csrfKey}";
            Logger.Info("DeadlyStream: requesting download (guest session)");

            using var resp = await _http.GetAsync(
                downloadUrl, HttpCompletionOption.ResponseHeadersRead, cancel);
            resp.EnsureSuccessStatusCode();

            string mediaType = resp.Content.Headers.ContentType?.MediaType ?? "";
            if (mediaType.Contains("html"))
            {
                throw new InvalidOperationException(
                    "DeadlyStream returned a web page instead of the file — " +
                    "guest downloads may have been disabled. Use the manual link.");
            }

            long total = resp.Content.Headers.ContentLength ?? -1;
            long done = 0;

            using var src = await resp.Content.ReadAsStreamAsync(cancel);
            using var dst = new FileStream(
                destPath, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16);
            var buffer = new byte[1 << 16];
            int read;
            while ((read = await src.ReadAsync(buffer.AsMemory(), cancel)) > 0)
            {
                await dst.WriteAsync(buffer.AsMemory(0, read), cancel);
                done += read;
                progress?.Invoke(done, total);
            }

            Logger.Info($"DeadlyStream: downloaded {done} bytes to {destPath}");
        }

        public void Dispose() => _http.Dispose();
    }
}
