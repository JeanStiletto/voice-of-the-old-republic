# GitHubClient.cs (433 lines)

HTTP client for downloading the accessibility .kpatch, HoloPatcher, and (via raw git-tree APIs) K1CP's export-ignored tslpatchdata directory from GitHub. Two download paths per asset: a PRIMARY redirect-based path (github.com/.../releases/download/... or /releases/latest, which does NOT count against the unauthenticated 60-req/hour api.github.com rate limit — important for users behind CGNAT/VPN), and an API FALLBACK (api.github.com with Accept: application/octet-stream) used only when the redirect host fails, e.g. during a partial GitHub outage. `ListTreeBlobsAsync` + `DownloadRawAsync` reconstruct directories GitHub's source-archive omits via `.gitattributes export-ignore` by walking the git tree API and pulling file contents from raw.githubusercontent.com (not subject to export-ignore), with atomic `.partial`-then-rename writes and exponential backoff retry on transient errors. Talks to Config (asset/repo names) and Logger.

## Declarations (in source order)

- L16 — `public class GitHubClient : IDisposable`
- L20 — `GitHubClient()` — 5-min HttpClient timeout, custom User-Agent
- L27 — `Task<string> GetLatestModVersionAsync(string repoUrl)` — resolves latest tag, strips leading v/V
- L48 — `Task<string> DownloadKPatchAsync(...)` — thin wrapper over DownloadReleaseAssetAsync
- L57 — `Task<string> DownloadReleaseAssetAsync(string repoUrl, string assetName, Action<int> progress)` — resolves latest tag then downloads
- L78 — `Task<string> DownloadReleaseAssetByTagAsync(...)` — used for HoloPatcher (pinned tag, since "latest" on that repo is the Toolset not the patcher)
- L107 — `Task<string> DownloadTaggedAssetAsync(...)` — direct release-download URL first, falls back to L136 on HttpRequestException/TaskCanceledException/IOException
- L136 — `Task DownloadViaApiAssetAsync(...)` — outage-resilient path via release JSON + asset API url
- L170 — `Task<string> ResolveLatestTagAsync(string repoUrl)` — redirect-first, API fallback
- L185 — `Task<string> ResolveLatestTagViaRedirectAsync(...)` — reads final RequestUri after HttpClient auto-redirect from /releases/latest
- L198 — `Task<string> ResolveLatestTagViaApiAsync(...)`
- L218 — `static string FindAssetApiUrl(string json, string assetName)` — substring match on asset name, returns API url (not browser_download_url)
- L238 — `public sealed class GitBlobEntry { Path, Sha, Size }`
- L255 — `Task<List<GitBlobEntry>> ListTreeBlobsAsync(string owner, string repo, string commitSha, string subdirPath)`
  note: throws if GitHub reports `truncated=true` (>100k entries / >7MB) — caller must switch strategy
- L299 — `Task<string> ResolveSubtreeShaAsync(...)` — walks the commit root tree to find a subdir's tree SHA
- L331 — `Task DownloadRawAsync(string url, string destPath, int maxAttempts = 4)` — retries 5xx/429/408 with backoff, writes via `.partial` + atomic rename
- L385 — `static TimeSpan BackoffDelay(int attempt)` — 250/500/1000/2000 ms
- L394 — `Task DownloadFileAsync(HttpRequestMessage request, string destinationPath, Action<int> progress)` — streaming copy with percent-progress callback
- L430 — `void Dispose()`
