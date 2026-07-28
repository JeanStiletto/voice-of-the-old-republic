# DeadlyStreamClient.cs (126 lines)

Guest-download client for deadlystream.com (Invision Community forum). No static direct-download URL exists: it scrapes the file page for a per-session `csrfKey` (via cookies + regex), then GETs `?do=download&csrfKey=<key>` and streams the attachment. Deliberately defensive — every failure mode (missing csrfKey, HTML response instead of a file) throws a descriptive error so callers can fall back to handing the user a manual browser-download link. Used to fetch TSLRCM and the Unofficial TSLRCM Tweak Pack. Verified working as a guest on 2026-07-27.

## Declarations (in source order)

- L29 — `public sealed class DeadlyStreamClient : IDisposable`
- L34 — `UserAgent` — browser-like UA string
- L36 — `CsrfKeyRegex` — extracts the csrfKey from file-page HTML
- L41 — `DeadlyStreamClient()` — sets up HttpClient with CookieContainer, 30-min timeout
- L61 — `Task DownloadFileAsync(string filePageUrl, string destPath, Action<long,long> progress, CancellationToken cancel)`
  note: two-step scrape (page GET for csrfKey, then download GET); throws if response Content-Type is HTML (guest downloads disabled)
- L116 — `static string ComputeSha256(string path)` — uppercase hex SHA-256, used to verify every scraped file against Config's pinned hash
- L123 — `void Dispose()`
