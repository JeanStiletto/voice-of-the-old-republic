using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace KotorAccessibilityInstaller
{
    /// <summary>
    /// Handles downloading the .kpatch from the mod's GitHub releases.
    /// </summary>
    public class GitHubClient : IDisposable
    {
        private readonly HttpClient _httpClient;

        public GitHubClient()
        {
            _httpClient = new HttpClient();
            _httpClient.DefaultRequestHeaders.Add("User-Agent", "KotorAccessibilityInstaller/1.0");
            _httpClient.Timeout = TimeSpan.FromMinutes(5);
        }

        public async Task<string> GetLatestModVersionAsync(string repoUrl)
        {
            try
            {
                string apiUrl = repoUrl.Replace("github.com", "api.github.com/repos") + "/releases/latest";
                Logger.Info($"Fetching latest mod version from: {apiUrl}");

                var request = new HttpRequestMessage(HttpMethod.Get, apiUrl);
                request.Headers.Add("Accept", "application/vnd.github.v3+json");
                var response = await _httpClient.SendAsync(request);

                if (!response.IsSuccessStatusCode)
                {
                    Logger.Warning($"GitHub API returned {response.StatusCode}");
                    return null;
                }

                string json = await response.Content.ReadAsStringAsync();
                var match = Regex.Match(json, @"""tag_name""\s*:\s*""v?([^""]+)""");
                if (match.Success)
                {
                    string version = match.Groups[1].Value;
                    Logger.Info($"Latest mod version: {version}");
                    return version;
                }

                Logger.Warning("Could not parse version from GitHub API response");
                return null;
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to get mod version", ex);
                return null;
            }
        }

        public async Task<string> DownloadKPatchAsync(string repoUrl, string assetName, Action<int> progress = null)
            => await DownloadReleaseAssetAsync(repoUrl, assetName, progress);

        /// <summary>
        /// Download a named release asset from the latest release of a GitHub
        /// repo. Returns the path of the downloaded file in the system temp dir.
        /// Used for our .kpatch (which tracks the latest release).
        /// For pinned tags, use <see cref="DownloadReleaseAssetByTagAsync"/>.
        /// </summary>
        public Task<string> DownloadReleaseAssetAsync(string repoUrl, string assetName, Action<int> progress = null)
            => DownloadReleaseAssetInternalAsync(repoUrl, releasePathSegment: "latest", assetName, progress);

        /// <summary>
        /// Download a named release asset from a specific tagged release.
        /// Used for HoloPatcher (NickHugi/PyKotor's "latest" release is the
        /// Toolset, not HoloPatcher — we have to pin to the patcher tag).
        /// </summary>
        public Task<string> DownloadReleaseAssetByTagAsync(string repoUrl, string tag, string assetName, Action<int> progress = null)
            => DownloadReleaseAssetInternalAsync(repoUrl, releasePathSegment: $"tags/{Uri.EscapeDataString(tag)}", assetName, progress);

        private async Task<string> DownloadReleaseAssetInternalAsync(string repoUrl, string releasePathSegment, string assetName, Action<int> progress)
        {
            try
            {
                string apiUrl = repoUrl.Replace("github.com", "api.github.com/repos") + "/releases/" + releasePathSegment;
                Logger.Info($"Fetching release info from: {apiUrl}");

                var request = new HttpRequestMessage(HttpMethod.Get, apiUrl);
                request.Headers.Add("Accept", "application/vnd.github.v3+json");
                var response = await _httpClient.SendAsync(request);
                response.EnsureSuccessStatusCode();

                string json = await response.Content.ReadAsStringAsync();
                string pattern = $"\"browser_download_url\"\\s*:\\s*\"([^\"]*{Regex.Escape(assetName)}[^\"]*)\"";
                var match = Regex.Match(json, pattern);

                if (!match.Success)
                    throw new Exception($"Asset '{assetName}' not found in release '{releasePathSegment}' at {repoUrl}.\n\nUpload the file as a release asset and retry.");

                string downloadUrl = match.Groups[1].Value;
                Logger.Info($"Downloading {assetName} from: {downloadUrl}");

                string tempFile = Path.Combine(Path.GetTempPath(), assetName);
                await DownloadFileAsync(downloadUrl, tempFile, progress);
                return tempFile;
            }
            catch (Exception ex)
            {
                Logger.Error($"Failed to download release asset '{assetName}'", ex);
                throw;
            }
        }

        /// <summary>
        /// One entry in a recursive git tree listing — a blob (file) and its
        /// path relative to the queried subtree root.
        /// </summary>
        public sealed class GitBlobEntry
        {
            public string Path { get; init; }
            public string Sha { get; init; }
            public long Size { get; init; }
        }

        /// <summary>
        /// Lists every blob under <paramref name="subdirPath"/> in the repo at
        /// the given commit. Returned paths are relative to <paramref name="subdirPath"/>.
        ///
        /// Used to reconstruct directories that GitHub's source archive omits via
        /// <c>.gitattributes export-ignore</c> (notably K1CP's <c>tslpatchdata/</c>).
        /// Combine with <see cref="DownloadRawAsync"/> against
        /// raw.githubusercontent.com to retrieve file contents — that path is NOT
        /// subject to export-ignore.
        /// </summary>
        public async Task<List<GitBlobEntry>> ListTreeBlobsAsync(
            string owner, string repo, string commitSha, string subdirPath)
        {
            // Walk the commit's root tree to find the subdir's tree SHA.
            string rootSha = await ResolveSubtreeShaAsync(owner, repo, commitSha, subdirPath);

            // Fetch that subtree recursively in one call. truncated=true would
            // indicate >100k entries / >7MB; for K1CP tslpatchdata we're far
            // under, but error clearly if it ever happens so the caller knows
            // to switch strategies.
            string url = $"https://api.github.com/repos/{owner}/{repo}/git/trees/{rootSha}?recursive=1";
            Logger.Info($"Listing recursive tree at {owner}/{repo} sub='{subdirPath}' sha={Shorten(rootSha)}");

            using var request = new HttpRequestMessage(HttpMethod.Get, url);
            request.Headers.Add("Accept", "application/vnd.github.v3+json");
            using var response = await _httpClient.SendAsync(request);
            response.EnsureSuccessStatusCode();

            string json = await response.Content.ReadAsStringAsync();
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            if (root.TryGetProperty("truncated", out var truncEl) && truncEl.GetBoolean())
            {
                throw new InvalidOperationException(
                    $"GitHub tree listing for {owner}/{repo} {subdirPath} was truncated. " +
                    "Caller must switch to a paginated strategy.");
            }

            var blobs = new List<GitBlobEntry>();
            foreach (var entry in root.GetProperty("tree").EnumerateArray())
            {
                if (entry.GetProperty("type").GetString() != "blob") continue;
                blobs.Add(new GitBlobEntry
                {
                    Path = entry.GetProperty("path").GetString(),
                    Sha = entry.TryGetProperty("sha", out var shaEl) ? shaEl.GetString() : null,
                    Size = entry.TryGetProperty("size", out var sizeEl) ? sizeEl.GetInt64() : 0L,
                });
            }
            Logger.Info($"  found {blobs.Count} blobs under '{subdirPath}'");
            return blobs;
        }

        private async Task<string> ResolveSubtreeShaAsync(string owner, string repo, string commitSha, string subdirPath)
        {
            string url = $"https://api.github.com/repos/{owner}/{repo}/git/trees/{commitSha}";
            using var request = new HttpRequestMessage(HttpMethod.Get, url);
            request.Headers.Add("Accept", "application/vnd.github.v3+json");
            using var response = await _httpClient.SendAsync(request);
            response.EnsureSuccessStatusCode();

            string json = await response.Content.ReadAsStringAsync();
            using var doc = JsonDocument.Parse(json);
            foreach (var entry in doc.RootElement.GetProperty("tree").EnumerateArray())
            {
                if (entry.GetProperty("type").GetString() == "tree" &&
                    entry.GetProperty("path").GetString() == subdirPath)
                {
                    return entry.GetProperty("sha").GetString();
                }
            }
            throw new InvalidOperationException(
                $"Subdirectory '{subdirPath}' not found at root of {owner}/{repo}@{Shorten(commitSha)}. " +
                "Upstream repo layout may have changed; bump the pin in Config.");
        }

        /// <summary>
        /// Downloads a single file from a raw URL with retry on transient
        /// (5xx / 429 / network) errors. raw.githubusercontent.com is the
        /// intended host but the helper is host-agnostic.
        ///
        /// Writes atomically — downloads to a .partial file and renames on
        /// success so a process kill mid-download doesn't leave a truncated
        /// file at <paramref name="destPath"/>.
        /// </summary>
        public async Task DownloadRawAsync(string url, string destPath, int maxAttempts = 4)
        {
            string partial = destPath + ".partial";
            Exception last = null;
            for (int attempt = 1; attempt <= maxAttempts; attempt++)
            {
                try
                {
                    using var response = await _httpClient.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
                    if (!response.IsSuccessStatusCode)
                    {
                        bool transient = (int)response.StatusCode >= 500
                                         || response.StatusCode == HttpStatusCode.TooManyRequests
                                         || response.StatusCode == HttpStatusCode.RequestTimeout;
                        if (!transient || attempt == maxAttempts)
                        {
                            response.EnsureSuccessStatusCode();
                        }
                        await Task.Delay(BackoffDelay(attempt));
                        continue;
                    }

                    using (var contentStream = await response.Content.ReadAsStreamAsync())
                    using (var fs = new FileStream(partial, FileMode.Create, FileAccess.Write, FileShare.None, 8192, useAsync: true))
                    {
                        await contentStream.CopyToAsync(fs);
                    }

                    if (File.Exists(destPath))
                    {
                        try { File.Delete(destPath); } catch { /* overwrite-best-effort */ }
                    }
                    File.Move(partial, destPath);
                    return;
                }
                catch (Exception ex) when (
                    ex is HttpRequestException || ex is IOException || ex is TaskCanceledException)
                {
                    last = ex;
                    Logger.Warning($"raw fetch attempt {attempt}/{maxAttempts} failed for {url}: {ex.Message}");
                    if (attempt == maxAttempts) break;
                    await Task.Delay(BackoffDelay(attempt));
                }
                finally
                {
                    if (File.Exists(partial))
                    {
                        try { File.Delete(partial); } catch { /* cleanup best-effort */ }
                    }
                }
            }
            throw new Exception($"Failed to download {url} after {maxAttempts} attempts: {last?.Message}", last);
        }

        private static TimeSpan BackoffDelay(int attempt) =>
            TimeSpan.FromMilliseconds(250 * (1 << (attempt - 1))); // 250 / 500 / 1000 / 2000 ms

        private static string Shorten(string sha) =>
            string.IsNullOrEmpty(sha) || sha.Length < 7 ? sha : sha.Substring(0, 7);

        private async Task DownloadFileAsync(string url, string destinationPath, Action<int> progress = null)
        {
            using (var response = await _httpClient.GetAsync(url, HttpCompletionOption.ResponseHeadersRead))
            {
                response.EnsureSuccessStatusCode();
                long? totalBytes = response.Content.Headers.ContentLength;

                using (var contentStream = await response.Content.ReadAsStreamAsync())
                using (var fileStream = new FileStream(destinationPath, FileMode.Create, FileAccess.Write, FileShare.None, 8192, true))
                {
                    var buffer = new byte[8192];
                    long totalRead = 0;
                    int bytesRead;
                    int lastReportedProgress = -1;

                    while ((bytesRead = await contentStream.ReadAsync(buffer, 0, buffer.Length)) > 0)
                    {
                        await fileStream.WriteAsync(buffer, 0, bytesRead);
                        totalRead += bytesRead;

                        if (totalBytes.HasValue && progress != null)
                        {
                            int currentProgress = (int)((totalRead * 100) / totalBytes.Value);
                            if (currentProgress != lastReportedProgress)
                            {
                                progress(currentProgress);
                                lastReportedProgress = currentProgress;
                            }
                        }
                    }
                }
            }
            Logger.Info($"Download complete: {destinationPath}");
        }

        public void Dispose() => _httpClient?.Dispose();
    }
}
