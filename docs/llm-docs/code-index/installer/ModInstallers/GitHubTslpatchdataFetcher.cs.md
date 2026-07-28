# GitHubTslpatchdataFetcher.cs (83 lines)

Static helper shared by `K1cpInstaller` and `K2cpInstaller` that reconstructs a repo's `tslpatchdata/` folder from GitHub at a pinned commit SHA. Uses the git tree API (`GitHubClient.ListTreeBlobsAsync`) to enumerate blobs under `tslpatchdata/`, then downloads each one individually via `raw.githubusercontent.com` — this sidesteps `.gitattributes export-ignore`, which strips the directory from GitHub's source archive/zip (the K1CP trap). Downloads run with bounded concurrency (`SemaphoreSlim(8)`) and coarse, CAS-gated progress callbacks.

## Declarations (in source order)

- L20 — `internal static class GitHubTslpatchdataFetcher`
- L22 — `public static async Task FetchAsync(GitHubClient gh, string owner, string repo, string commitSha, string destDir, Action<int> progress, string logPrefix)`
  note: throws `InvalidOperationException` if zero blobs found under tslpatchdata/ (signals a repo layout change / stale pinned ref)
- L43 — `using var sem = new SemaphoreSlim(8)` — concurrency cap for parallel downloads
- L44 — `var tasks = blobs.Select(async blob => ...)` — per-blob download task; mirrors blob path under destDir, creates parent dirs, downloads via `gh.DownloadRawAsync`
  note: progress percent uses `Interlocked.Exchange` CAS to avoid redundant UI-thread callbacks per blob
