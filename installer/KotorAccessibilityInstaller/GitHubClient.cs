using System;
using System.IO;
using System.Net.Http;
using System.Text.RegularExpressions;
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
        /// Download a GitHub repo's source tarball at the given ref (branch,
        /// tag, or commit SHA). Returns the path of the downloaded .tar.gz in
        /// the system temp dir. Uses the GitHub API endpoint which redirects
        /// to codeload.github.com; HttpClient follows the redirect.
        /// </summary>
        public async Task<string> DownloadRepoTarballAsync(
            string owner,
            string repo,
            string @ref,
            Action<int> progress = null)
        {
            string url = $"https://api.github.com/repos/{owner}/{repo}/tarball/{@ref}";
            Logger.Info($"Downloading repo tarball: {url}");

            string destFilename = $"{owner}-{repo}-{@ref}.tar.gz";
            string tempFile = Path.Combine(Path.GetTempPath(), destFilename);
            await DownloadFileAsync(url, tempFile, progress);
            return tempFile;
        }

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
