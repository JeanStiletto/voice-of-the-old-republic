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
        {
            try
            {
                string apiUrl = repoUrl.Replace("github.com", "api.github.com/repos") + "/releases/latest";
                Logger.Info($"Fetching release info from: {apiUrl}");

                var request = new HttpRequestMessage(HttpMethod.Get, apiUrl);
                request.Headers.Add("Accept", "application/vnd.github.v3+json");
                var response = await _httpClient.SendAsync(request);
                response.EnsureSuccessStatusCode();

                string json = await response.Content.ReadAsStringAsync();
                string pattern = $"\"browser_download_url\"\\s*:\\s*\"([^\"]*{Regex.Escape(assetName)}[^\"]*)\"";
                var match = Regex.Match(json, pattern);

                if (!match.Success)
                    throw new Exception($"Asset '{assetName}' not found in latest release.\n\nMake sure the .kpatch is uploaded to the GitHub release.");

                string downloadUrl = match.Groups[1].Value;
                Logger.Info($"Downloading kpatch from: {downloadUrl}");

                string tempFile = Path.Combine(Path.GetTempPath(), assetName);
                await DownloadFileAsync(downloadUrl, tempFile, progress);
                return tempFile;
            }
            catch (Exception ex)
            {
                Logger.Error("Failed to download kpatch", ex);
                throw;
            }
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
