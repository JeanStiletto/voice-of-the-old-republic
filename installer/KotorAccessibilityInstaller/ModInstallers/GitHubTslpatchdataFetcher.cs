using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller.ModInstallers
{
    /// <summary>
    /// Reconstructs a repo's <c>tslpatchdata/</c> from GitHub at a pinned
    /// commit. Uses the git tree API to enumerate blobs, then downloads each
    /// one via raw.githubusercontent.com — which, unlike the source archive,
    /// is not subject to <c>.gitattributes export-ignore</c> (the K1CP trap;
    /// K2CP has no export-ignore but the same path works for both and keeps
    /// one download story). Concurrency-throttled to keep memory + connection
    /// count bounded.
    ///
    /// Shared by <see cref="K1cpInstaller"/> and <see cref="K2cpInstaller"/>.
    /// </summary>
    internal static class GitHubTslpatchdataFetcher
    {
        public static async Task FetchAsync(
            GitHubClient gh, string owner, string repo, string commitSha,
            string destDir, Action<int> progress, string logPrefix)
        {
            Logger.Info($"{logPrefix}: enumerating tslpatchdata blobs at {owner}/{repo}@{commitSha.Substring(0, 7)}");
            var blobs = await gh.ListTreeBlobsAsync(owner, repo, commitSha, "tslpatchdata");
            if (blobs.Count == 0)
            {
                throw new InvalidOperationException(
                    $"No files found under tslpatchdata/ at {owner}/{repo}@{commitSha}. " +
                    $"{logPrefix} repo layout may have changed; bump the pinned ref in Config.");
            }

            long totalBytes = blobs.Sum(b => b.Size);
            Logger.Info($"{logPrefix}: downloading {blobs.Count} files, {totalBytes / 1024 / 1024} MB total");

            long downloadedBytes = 0;
            int reportedPct = -1;

            // Concurrency cap: 8 is a sweet spot — fast enough to saturate most
            // home connections without slamming raw.githubusercontent.com.
            using var sem = new SemaphoreSlim(8);
            var tasks = blobs.Select(async blob =>
            {
                await sem.WaitAsync();
                try
                {
                    // Blob paths are relative to tslpatchdata/. Mirror the layout
                    // under destDir; create parent dirs as needed.
                    string destPath = Path.Combine(destDir, blob.Path.Replace('/', Path.DirectorySeparatorChar));
                    string parent = Path.GetDirectoryName(destPath);
                    if (!string.IsNullOrEmpty(parent))
                        Directory.CreateDirectory(parent);

                    string rawUrl =
                        $"https://raw.githubusercontent.com/{owner}/{repo}/{commitSha}/tslpatchdata/{blob.Path}";
                    await gh.DownloadRawAsync(rawUrl, destPath);

                    long after = Interlocked.Add(ref downloadedBytes, blob.Size);
                    if (totalBytes > 0 && progress != null)
                    {
                        int pct = (int)((after * 100) / totalBytes);
                        // Coarse rate-limit progress callbacks; cheap interlocked
                        // CAS gate avoids hammering the UI thread on every blob.
                        if (pct != reportedPct &&
                            Interlocked.Exchange(ref reportedPct, pct) != pct)
                        {
                            progress(pct);
                        }
                    }
                }
                finally
                {
                    sem.Release();
                }
            });
            await Task.WhenAll(tasks);
            progress?.Invoke(100);
        }
    }
}
