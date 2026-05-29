// In-game auto-updater. See update_checker.h for the high-level flow.
//
// Mirrors arena/src/Core/Services/UpdateChecker.cs:
//   - Background HTTP GET against api.github.com on a worker thread,
//     idempotent on repeat calls.
//   - Atomic state flags (checking / check_complete / update_available /
//     downloading / download_complete / download_failed) read from the
//     main-thread Tick; strings written before the flag flips so the
//     reader sees them on flag-acquire.
//   - JSON parsing via small hand-rolled string searches (arena uses
//     regex; we keep behaviour identical with simpler code so the binary
//     stays slim and we don't drag <regex> into the patch DLL).
//   - On download success, writes a .bat to %TEMP% that waits for
//     swkotor.exe to exit, runs the installer with --auto-update under
//     the manifest-driven UAC prompt, then relaunches the game via Steam.
//
// Lessons baked in from arena's iterations:
//   - Cache the release JSON so F5 doesn't re-fetch the API.
//   - Set User-Agent (GitHub returns 403 without one).
//   - Send Accept: application/vnd.github.v3+json.
//   - One-shot announce flag so the "update available" cue doesn't repeat
//     every frame after the background check completes.
//   - Speak then schedule the exit a couple of ticks later — speech is
//     async, so ExitProcess immediately would cut the cue.
//   - The .bat must `start /wait` the installer so the relaunch waits for
//     it to finish; non-elevated batch + manifest-elevated installer keeps
//     the relaunched game running as the user (Steam refuses to launch
//     an elevated child from a non-elevated context, but the relaunch
//     inherits the BAT's non-elevated token regardless of what UAC did
//     to the installer process — verified by arena's hard-won learning
//     on the same shape).

#include "update_checker.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "engine_player.h"
#include "engine_offsets.h"
#include "hotkeys.h"
#include "log.h"
#include "mod_version.h"
#include "prism.h"
#include "strings.h"

#pragma comment(lib, "winhttp.lib")

namespace acc::update_checker {

namespace {

// ----- Configuration --------------------------------------------------------

// Mirrors installer/KotorAccessibilityInstaller/Config.cs ModRepositoryUrl.
// Owner + repo split because WinHTTP needs the host separately from the path.
constexpr const wchar_t* kGitHubHost     = L"api.github.com";
constexpr const wchar_t* kReleasesPath   = L"/repos/JeanStiletto/voice-of-the-old-republic/releases/latest";
constexpr const wchar_t* kUserAgent      = L"VoiceOfTheOldRepublic/UpdateChecker";

// Asset filename to download (matches release.ps1 + installer's
// Path.GetFileName check). The installer is what we hand off to; it'll
// fetch the .kpatch itself via its existing GitHubClient.
constexpr const char*    kInstallerAsset = "KotorAccessibilityInstaller.exe";

// Steam app ID for KOTOR 1 (verified from steamdb.info entry "Star Wars:
// Knights of the Old Republic"). Used in the relaunch step of the .bat.
constexpr const char*    kSteamAppId     = "32370";

// Timeouts in ms. The check is quick (a few hundred bytes); the download
// is the multi-MB installer.
constexpr int kCheckTimeoutMs    = 5000;
constexpr int kDownloadTimeoutMs = 60000;

// Delay between speaking UpdateDownloaded and calling ExitProcess. Two
// seconds covers a typical SAPI/NVDA utterance of the localised cue.
constexpr DWORD kExitGraceMs = 2000;

// ----- Dev test overrides ---------------------------------------------------
// Two opt-in text files in the patch dir let you exercise the full update
// flow (announce → F5 → download → batch → installer → relaunch) without
// touching GitHub or cutting a release. Both files are absent in shipped
// installs (we never bundle them), so production-path is zero-footprint.
//
//   <patch_dir>\update_test_version.txt
//     First line is treated as the latest published version, e.g. "9.9.9".
//     The background check skips HTTP and fakes that as the API response.
//     Compared against kModVersion by the same IsRemoteNewer logic, so
//     the cue only fires when the override is actually higher.
//
//   <patch_dir>\update_test_installer.txt
//     First line is an absolute path to a pre-built installer EXE. F5's
//     download step skips HTTP and CopyFile-s that file to
//     %TEMP%\KotorAccessibilityInstaller.exe — same destination the real
//     download would land. The handoff bat is unaware of the override
//     (it just runs whatever sits at the dest path).
//
// File-based instead of env vars: Windows env-var inheritance through
// Steam is unreliable (Steam doesn't re-read on setx, processes started
// before setx don't see it). The patch dir is always readable from our
// DLL and re-evaluated on every game launch — edit-then-relaunch loop
// works without restarting Steam or fiddling with HKCU\Environment.
//
// Typical local-test recipe:
//   1. `dotnet publish installer/.../KotorAccessibilityInstaller.csproj
//       -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true`
//   2. Write `9.9.9` into <install>\patches\update_test_version.txt
//   3. Write the absolute installer path into <install>\patches\update_test_installer.txt
//   4. kdev launch (or just start the game)
//   5. On title screen the cue fires; F5 runs the local installer.
//
// Cleanup: delete the two files. No other state to roll back.
constexpr const char* kTestVersionFile   = "update_test_version.txt";
constexpr const char* kTestInstallerFile = "update_test_installer.txt";

// Reads the first non-empty line of `<patch_dir>/<filename>` into `out`,
// strips trailing CR/LF/spaces, returns true on success. False if the
// file is missing, empty, or unreadable — same shape as the env-var
// wrapper this replaces so callers stay short.
bool ReadTestFile(const char* filename, char* out, size_t cap) {
    if (cap > 0) out[0] = '\0';
    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) return false;
    char path[MAX_PATH];
    _snprintf_s(path, _TRUNCATE, "%s\\%s", patchDir, filename);
    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") != 0 || !fp) return false;
    char buf[MAX_PATH * 2] = {};
    size_t got = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (got == 0) return false;
    // Take just the first line.
    for (size_t i = 0; i < got; ++i) {
        if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = '\0'; break; }
    }
    // Strip trailing whitespace.
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }
    if (len == 0) return false;
    if (len >= cap) len = cap - 1;
    memcpy(out, buf, len);
    out[len] = '\0';
    return true;
}

// ----- State machine --------------------------------------------------------

// Atomics for the cross-thread flags. Strings are written first, the
// flag is stored with release semantics, the Tick reader checks the flag
// with acquire semantics — that ordering guarantees the strings are
// visible once the flag reads true.
std::atomic<bool> g_check_started{false};
std::atomic<bool> g_check_complete{false};
std::atomic<bool> g_update_available{false};
std::atomic<bool> g_announced{false};

std::atomic<bool> g_download_started{false};
std::atomic<bool> g_download_complete{false};
std::atomic<bool> g_download_failed{false};

// Set by Tick once UpdateDownloaded has been spoken; carries the absolute
// tick-clock time at which we should spawn the batch + exit. Zero means
// "no exit pending".
std::atomic<DWORD> g_exit_at_tick{0};

// Strings touched by both worker + main thread. Written by worker BEFORE
// flipping the corresponding flag; read by main AFTER observing the flag.
// Fixed buffers so we don't drag heap allocators across threads.
char g_latest_version[64] = {};
char g_installer_path[MAX_PATH] = {};

// Cached release JSON — populated by the version check, reused by the
// download path so we don't hit the API twice. std::string so it can
// hold the variable-size response without manual buffer math.
std::string g_release_json;

// ----- WinHTTP helpers ------------------------------------------------------

// Open the GitHub host with our User-Agent + redirect policy. Caller closes
// session + connection on completion.
bool OpenGitHubSession(HINTERNET& session, HINTERNET& connection) {
    session = WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        acclog::Write("Update", "WinHttpOpen failed: %lu", GetLastError());
        return false;
    }
    connection = WinHttpConnect(
        session,
        kGitHubHost,
        INTERNET_DEFAULT_HTTPS_PORT,
        0);
    if (!connection) {
        acclog::Write("Update", "WinHttpConnect failed: %lu", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }
    return true;
}

// GET a URL split into host/path on a fresh request. Body is appended to
// `out` (already an empty std::string from the caller). Returns true on
// HTTP 200 with body present.
//
// `host` may be null to reuse the existing connection. `path` is the
// resource path including the leading slash and any query string.
bool HttpGetToString(HINTERNET connection, const wchar_t* path,
                     int timeoutMs, std::string& out) {
    HINTERNET req = WinHttpOpenRequest(
        connection,
        L"GET",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req) {
        acclog::Write("Update", "WinHttpOpenRequest failed: %lu", GetLastError());
        return false;
    }

    // Per-handle timeouts: resolve, connect, send, receive.
    WinHttpSetTimeouts(req, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    static const wchar_t kAccept[] =
        L"Accept: application/vnd.github.v3+json\r\n";
    if (!WinHttpSendRequest(req, kAccept, (DWORD)-1L,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        acclog::Write("Update", "WinHttpSendRequest failed: %lu", GetLastError());
        WinHttpCloseHandle(req);
        return false;
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        acclog::Write("Update", "WinHttpReceiveResponse failed: %lu", GetLastError());
        WinHttpCloseHandle(req);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(req,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        acclog::Write("Update", "API returned HTTP %lu", status);
        WinHttpCloseHandle(req);
        return false;
    }

    char buf[4096];
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        DWORD got = 0;
        DWORD toRead = avail < sizeof(buf) ? avail : sizeof(buf);
        if (!WinHttpReadData(req, buf, toRead, &got)) break;
        if (got == 0) break;
        out.append(buf, got);
    }
    WinHttpCloseHandle(req);
    return !out.empty();
}

// GET an arbitrary URL to a file on disk. Handles host + path cracking
// (the asset download URL is on a different host than api.github.com —
// objects.githubusercontent.com or a release-asset host). Follows
// redirects via WinHTTP's default policy.
bool HttpDownloadUrlToFile(const wchar_t* url, const char* destPath, int timeoutMs) {
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[2048] = {};
    uc.lpszHostName  = hostBuf;
    uc.dwHostNameLength = (DWORD)(sizeof(hostBuf) / sizeof(wchar_t)) - 1;
    uc.lpszUrlPath   = pathBuf;
    uc.dwUrlPathLength = (DWORD)(sizeof(pathBuf) / sizeof(wchar_t)) - 1;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) {
        acclog::Write("Update", "WinHttpCrackUrl failed: %lu", GetLastError());
        return false;
    }
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET session = WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        acclog::Write("Update", "download WinHttpOpen failed: %lu", GetLastError());
        return false;
    }
    HINTERNET connection = WinHttpConnect(session, hostBuf, uc.nPort, 0);
    if (!connection) {
        acclog::Write("Update", "download WinHttpConnect failed: %lu", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET req = WinHttpOpenRequest(
        connection,
        L"GET",
        pathBuf,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        acclog::Write("Update", "download WinHttpOpenRequest failed: %lu", GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }
    WinHttpSetTimeouts(req, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {

        DWORD status = 0, statusSize = sizeof(status);
        WinHttpQueryHeaders(req,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            FILE* fp = nullptr;
            if (fopen_s(&fp, destPath, "wb") == 0 && fp) {
                char buf[16384];
                DWORD avail = 0;
                ok = true;
                size_t totalWritten = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    DWORD got = 0;
                    DWORD toRead = avail < sizeof(buf) ? avail : sizeof(buf);
                    if (!WinHttpReadData(req, buf, toRead, &got) || got == 0) {
                        ok = false;
                        break;
                    }
                    if (fwrite(buf, 1, got, fp) != got) {
                        ok = false;
                        break;
                    }
                    totalWritten += got;
                }
                fclose(fp);
                if (ok) {
                    acclog::Write("Update",
                                  "downloaded %zu bytes to %s",
                                  totalWritten, destPath);
                }
            } else {
                acclog::Write("Update", "fopen_s failed for %s", destPath);
            }
        } else {
            acclog::Write("Update", "asset GET returned HTTP %lu", status);
        }
    } else {
        acclog::Write("Update", "asset GET WinHttp send/receive failed: %lu",
                      GetLastError());
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// ----- JSON extraction ------------------------------------------------------
// Hand-rolled to avoid pulling <regex> into the patch DLL. Each helper
// finds `"key" : "value"` and writes value into the caller's buffer.

// Skip whitespace + a single colon. Returns pointer past the colon, or
// nullptr if the next non-whitespace char isn't ':'.
const char* SkipColon(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return nullptr;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

// After SkipColon-ish, find the opening quote of a string value and copy
// up to the closing quote into out (NUL-terminated). Returns the pointer
// past the closing quote, or nullptr on parse failure.
const char* ReadQuotedString(const char* p, char* out, size_t outCap) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '"') return nullptr;
    ++p;
    size_t i = 0;
    while (*p && *p != '"') {
        if (i + 1 < outCap) out[i++] = *p;
        ++p;
    }
    if (i < outCap) out[i] = '\0';
    if (*p != '"') return nullptr;
    return p + 1;
}

// Extract the top-level `"tag_name": "vX.Y.Z"` value. Returns true if
// found; out is written with the leading 'v' stripped (and any " " /
// "-pre" suffix removed, mirroring arena's NormalizeVersion).
bool ExtractTagName(const std::string& json, char* out, size_t outCap) {
    const char* p = strstr(json.c_str(), "\"tag_name\"");
    if (!p) return false;
    p = SkipColon(p + strlen("\"tag_name\""));
    if (!p) return false;
    char raw[128] = {};
    if (!ReadQuotedString(p, raw, sizeof(raw))) return false;
    // Strip leading v/V.
    const char* start = raw;
    if (*start == 'v' || *start == 'V') ++start;
    // Strip pre-release suffix at '-' or whitespace.
    size_t copyLen = strlen(start);
    for (size_t i = 0; i < copyLen; ++i) {
        if (start[i] == '-' || start[i] == ' ') { copyLen = i; break; }
    }
    if (copyLen >= outCap) copyLen = outCap - 1;
    memcpy(out, start, copyLen);
    out[copyLen] = '\0';
    return out[0] != '\0';
}

// Walk every `"browser_download_url": "..."` occurrence and return the
// first URL that contains assetName. WinHTTP needs wide-char URLs, but
// the JSON is bytes from the wire, so we transcode the chosen URL at
// the boundary (the URL is ASCII — GitHub's release-asset URLs always
// are — so a straight widen is safe).
bool ExtractAssetUrl(const std::string& json, const char* assetName,
                     wchar_t* outUrl, size_t outCap) {
    const char* p = json.c_str();
    while (true) {
        p = strstr(p, "\"browser_download_url\"");
        if (!p) return false;
        p = SkipColon(p + strlen("\"browser_download_url\""));
        if (!p) return false;
        char urlBuf[1024] = {};
        const char* next = ReadQuotedString(p, urlBuf, sizeof(urlBuf));
        if (!next) return false;
        p = next;
        if (strstr(urlBuf, assetName) != nullptr) {
            size_t len = strlen(urlBuf);
            if (len >= outCap) len = outCap - 1;
            for (size_t i = 0; i < len; ++i) outUrl[i] = (wchar_t)(unsigned char)urlBuf[i];
            outUrl[len] = L'\0';
            return true;
        }
    }
}

// ----- Version comparison ---------------------------------------------------
// Same shape as arena's NormalizeVersion: parse up to 4 dot-separated
// integers, compare lexicographically.

struct ParsedVersion {
    int parts[4];
};

ParsedVersion ParseVersion(const char* s) {
    ParsedVersion v = {{0, 0, 0, 0}};
    if (!s || !*s) return v;
    // Strip leading v if present (extra defence; ExtractTagName already does this).
    if (*s == 'v' || *s == 'V') ++s;
    int idx = 0;
    while (*s && idx < 4) {
        char* end = nullptr;
        long n = strtol(s, &end, 10);
        v.parts[idx++] = (int)n;
        if (end == s) break;
        s = end;
        if (*s == '.') ++s;
        else break;
    }
    return v;
}

bool IsRemoteNewer(const char* remote, const char* local) {
    ParsedVersion r = ParseVersion(remote);
    ParsedVersion l = ParseVersion(local);
    for (int i = 0; i < 4; ++i) {
        if (r.parts[i] > l.parts[i]) return true;
        if (r.parts[i] < l.parts[i]) return false;
    }
    return false;
}

// ----- Worker threads -------------------------------------------------------

void CheckVersionWorker() {
    // Dev override — pretend the contents of update_test_version.txt is
    // what GitHub reports as the latest release. Lets local-test runs
    // exercise the full F5 flow without touching the API or cutting a
    // real release.
    char testVersion[64] = {};
    if (ReadTestFile(kTestVersionFile, testVersion, sizeof(testVersion))) {
        acclog::Write("Update",
                      "TEST MODE: %s=%s overrides API check",
                      kTestVersionFile, testVersion);
        if (IsRemoteNewer(testVersion, acc::kModVersion)) {
            strncpy_s(g_latest_version, testVersion, _TRUNCATE);
            // No release JSON — the download path's own test-mode override
            // will skip the asset-URL extraction step too.
            g_update_available.store(true, std::memory_order_release);
        } else {
            acclog::Write("Update",
                          "TEST MODE: %s is not newer than installed %s",
                          testVersion, acc::kModVersion);
        }
        g_check_complete.store(true, std::memory_order_release);
        return;
    }

    HINTERNET session = nullptr, connection = nullptr;
    if (!OpenGitHubSession(session, connection)) {
        g_check_complete.store(true, std::memory_order_release);
        return;
    }

    std::string json;
    bool ok = HttpGetToString(connection, kReleasesPath, kCheckTimeoutMs, json);

    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!ok) {
        acclog::Write("Update", "version check failed (no response body)");
        g_check_complete.store(true, std::memory_order_release);
        return;
    }

    char remote[64] = {};
    if (!ExtractTagName(json, remote, sizeof(remote))) {
        acclog::Write("Update", "version check: could not parse tag_name");
        g_check_complete.store(true, std::memory_order_release);
        return;
    }

    if (IsRemoteNewer(remote, acc::kModVersion)) {
        // Write strings BEFORE flipping the flag — readers use acquire
        // on this flag, so anything stored beforehand is visible.
        // Note: HandleF5 reads g_update_available directly (without first
        // checking g_check_complete), so this store must be release; Tick
        // reads g_check_complete with acquire and can use relaxed for the
        // dependent fields.
        strncpy_s(g_latest_version, remote, _TRUNCATE);
        g_release_json = std::move(json);
        g_update_available.store(true, std::memory_order_release);
        acclog::Write("Update", "update available: %s (installed %s)",
                      remote, acc::kModVersion);
    } else {
        acclog::Write("Update", "up to date (installed %s, latest %s)",
                      acc::kModVersion, remote);
    }
    g_check_complete.store(true, std::memory_order_release);
}

void DownloadWorker() {
    char tempDir[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDir) == 0) {
        acclog::Write("Update", "download: GetTempPath failed: %lu", GetLastError());
        g_download_failed.store(true, std::memory_order_relaxed);
        g_download_complete.store(true, std::memory_order_release);
        return;
    }
    char destPath[MAX_PATH] = {};
    _snprintf_s(destPath, _TRUNCATE, "%s%s", tempDir, kInstallerAsset);

    // Dev override — copy a pre-built installer from disk instead of
    // fetching from GitHub. Pairs with update_test_version.txt so the
    // entire flow can be exercised locally with no network at all.
    char testInstaller[MAX_PATH] = {};
    if (ReadTestFile(kTestInstallerFile, testInstaller, sizeof(testInstaller))) {
        acclog::Write("Update",
                      "TEST MODE: %s=%s overrides HTTP download",
                      kTestInstallerFile, testInstaller);
        if (!CopyFileA(testInstaller, destPath, FALSE)) {
            acclog::Write("Update",
                          "TEST MODE: CopyFile failed (%lu) — check the path exists",
                          GetLastError());
            g_download_failed.store(true, std::memory_order_relaxed);
        } else {
            strncpy_s(g_installer_path, destPath, _TRUNCATE);
            acclog::Write("Update", "TEST MODE: copied to %s", destPath);
        }
        g_download_complete.store(true, std::memory_order_release);
        return;
    }

    // If the background-check JSON is still cached, reuse it; otherwise
    // fetch fresh. Arena does the same fallback — caller may have pressed
    // F5 before the background check populated the cache (rare, since F5
    // is gated on _updateAvailable, but the safety net is cheap).
    std::string json;
    if (!g_release_json.empty()) {
        json = g_release_json;
    } else {
        HINTERNET session = nullptr, connection = nullptr;
        if (OpenGitHubSession(session, connection)) {
            HttpGetToString(connection, kReleasesPath, kDownloadTimeoutMs, json);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
        }
    }
    if (json.empty()) {
        acclog::Write("Update", "download: no release JSON available");
        g_download_failed.store(true, std::memory_order_relaxed);
        g_download_complete.store(true, std::memory_order_release);
        return;
    }

    wchar_t assetUrl[1024] = {};
    if (!ExtractAssetUrl(json, kInstallerAsset, assetUrl,
                         sizeof(assetUrl) / sizeof(wchar_t))) {
        acclog::Write("Update", "download: asset '%s' not in release JSON",
                      kInstallerAsset);
        g_download_failed.store(true, std::memory_order_relaxed);
        g_download_complete.store(true, std::memory_order_release);
        return;
    }

    if (!HttpDownloadUrlToFile(assetUrl, destPath, kDownloadTimeoutMs)) {
        acclog::Write("Update", "download: HTTP fetch failed for %s", kInstallerAsset);
        g_download_failed.store(true, std::memory_order_relaxed);
        g_download_complete.store(true, std::memory_order_release);
        return;
    }

    strncpy_s(g_installer_path, destPath, _TRUNCATE);
    g_download_complete.store(true, std::memory_order_release);
    acclog::Write("Update", "download complete: %s", destPath);
}

// ----- Handoff batch + exit -------------------------------------------------

// Build the .bat that takes over after the game exits. See header for
// the design rationale (non-elevated batch + manifest-elevated installer
// + Steam relaunch).
bool WriteHandoffBatch(const char* installerPath, char* batchPathOut, size_t outCap) {
    char tempDir[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDir) == 0) return false;
    _snprintf_s(batchPathOut, outCap, _TRUNCATE,
                "%sKotorAccessibility_AutoUpdate.bat", tempDir);

    FILE* fp = nullptr;
    if (fopen_s(&fp, batchPathOut, "w") != 0 || !fp) {
        acclog::Write("Update", "could not create handoff bat at %s", batchPathOut);
        return false;
    }

    // The bat lives at %TEMP%\KotorAccessibility_AutoUpdate.bat. It:
    //   1) Polls tasklist until swkotor.exe has exited (2 s cycle).
    //   2) start /wait — runs the installer and waits for it to finish.
    //      The installer's app.manifest demands elevation; Windows shows
    //      a UAC prompt at this point. After acceptance the installer
    //      runs the headless `--auto-update` path.
    //   3) Relaunches the game via the Steam protocol so saves go to the
    //      user-token Steam profile, not whatever profile the elevated
    //      installer ran under.
    //   4) Self-deletes via `(goto) 2>nul & del "%~f0"` — the parens
    //      trick lets cmd unmap the script before delete so the deletion
    //      doesn't race with cmd's read-ahead.
    fprintf(fp, "@echo off\r\n");
    fprintf(fp, ":wait\r\n");
    fprintf(fp, "tasklist /fi \"imagename eq swkotor.exe\" 2>nul | find /i \"swkotor.exe\" >nul\r\n");
    fprintf(fp, "if not errorlevel 1 (\r\n");
    fprintf(fp, "    timeout /t 2 /nobreak >nul\r\n");
    fprintf(fp, "    goto wait\r\n");
    fprintf(fp, ")\r\n");
    fprintf(fp, "start \"\" /wait \"%s\" --auto-update\r\n", installerPath);
    fprintf(fp, "start \"\" \"steam://rungameid/%s\"\r\n", kSteamAppId);
    fprintf(fp, "del \"%s\" >nul 2>&1\r\n", installerPath);
    fprintf(fp, "(goto) 2>nul & del \"%%~f0\"\r\n");
    fclose(fp);
    return true;
}

bool SpawnHandoffBatch(const char* batchPath) {
    // Non-elevated, hidden console. The installer self-elevates via its
    // own app.manifest, so we don't pass the runas verb here.
    //
    // Flag choice — `CREATE_NO_WINDOW` only (NOT combined with
    // `DETACHED_PROCESS`). DETACHED_PROCESS strips cmd.exe of any
    // console at all; without one, cmd's `start /wait <exe>` and
    // `start "" "steam://..."` either fail silently or do not wait.
    // CREATE_NO_WINDOW allocates the child a hidden console, which is
    // what `start` actually needs to function.
    char cmdLine[2 * MAX_PATH];
    _snprintf_s(cmdLine, _TRUNCATE, "cmd.exe /c \"%s\"", batchPath);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        acclog::Write("Update", "CreateProcess for handoff bat failed: %lu",
                      GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void LaunchHandoffAndExit() {
    char batchPath[MAX_PATH] = {};
    if (!WriteHandoffBatch(g_installer_path, batchPath, sizeof(batchPath))) {
        acclog::Write("Update", "could not write handoff bat — aborting update");
        return;
    }
    if (!SpawnHandoffBatch(batchPath)) {
        acclog::Write("Update", "could not spawn handoff bat — aborting update");
        return;
    }
    acclog::Write("Update", "handoff bat spawned: %s — exiting game", batchPath);
    // Synchronous exit. The bat is detached, so it survives.
    ExitProcess(0);
}

}  // namespace

// ----- Public API -----------------------------------------------------------

void StartBackgroundCheck() {
    bool expected = false;
    if (!g_check_started.compare_exchange_strong(expected, true)) return;
    acclog::Write("Update", "starting background version check");
    std::thread(CheckVersionWorker).detach();
}

// F5 rising-edge poll + main-menu gate. Refuses the press during active
// gameplay (GetPlayerPosition succeeds), announcing UpdateNotInMenu so
// the user doesn't think the keypress was eaten silently.
void PollF5() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::CheckForUpdate)) return;
    Vector pos;
    if (acc::engine::GetPlayerPosition(pos)) {
        prism::SpeakUrgent(acc::strings::Get(acc::strings::Id::UpdateNotInMenu));
        return;
    }
    HandleF5();
}

void Tick() {
    PollF5();

    // One-shot "update available" announce.
    if (g_check_complete.load(std::memory_order_acquire) &&
        g_update_available.load(std::memory_order_relaxed) &&
        !g_announced.exchange(true, std::memory_order_relaxed)) {
        char line[256];
        _snprintf_s(line, _TRUNCATE,
                    acc::strings::Get(acc::strings::Id::FmtUpdateAvailable),
                    g_latest_version);
        prism::SpeakUrgent(line);
    }

    // Download completion (after F5 → spoken cue → schedule exit).
    if (g_download_complete.load(std::memory_order_acquire) &&
        g_exit_at_tick.load(std::memory_order_relaxed) == 0) {
        if (g_download_failed.load(std::memory_order_relaxed)) {
            prism::SpeakUrgent(acc::strings::Get(acc::strings::Id::UpdateFailed));
            // Reset so a second F5 press can retry.
            g_download_complete.store(false, std::memory_order_relaxed);
            g_download_started.store(false, std::memory_order_release);
            g_download_failed.store(false, std::memory_order_relaxed);
        } else {
            prism::SpeakUrgent(acc::strings::Get(acc::strings::Id::UpdateDownloaded));
            // Schedule exit a couple of ticks out so the cue is audible.
            g_exit_at_tick.store(GetTickCount() + kExitGraceMs,
                                 std::memory_order_relaxed);
        }
    }

    // Exit scheduled? Spawn the handoff bat once the grace period elapses.
    DWORD exitAt = g_exit_at_tick.load(std::memory_order_relaxed);
    if (exitAt != 0 && GetTickCount() >= exitAt) {
        g_exit_at_tick.store(0, std::memory_order_relaxed);
        LaunchHandoffAndExit();
    }
}

void HandleF5() {
    // Download already in flight — re-affirm the state so the user hears
    // feedback for the keypress.
    if (g_download_started.load(std::memory_order_acquire) &&
        !g_download_complete.load(std::memory_order_acquire)) {
        prism::SpeakUrgent(acc::strings::Get(acc::strings::Id::UpdateDownloading));
        return;
    }

    // No update available (check not yet complete OR remote == local).
    // Acquire pairs with CheckVersionWorker's release on the same flag —
    // ensures g_latest_version + g_release_json writes are visible if the
    // flag was set, though F5 doesn't actually read them on this branch.
    if (!g_update_available.load(std::memory_order_acquire)) {
        char line[128];
        _snprintf_s(line, _TRUNCATE,
                    acc::strings::Get(acc::strings::Id::FmtUpdateNotAvailable),
                    acc::kModVersion);
        prism::SpeakUrgent(line);
        return;
    }

    // Start the download.
    bool expected = false;
    if (!g_download_started.compare_exchange_strong(expected, true)) return;
    g_download_complete.store(false, std::memory_order_relaxed);
    g_download_failed.store(false, std::memory_order_relaxed);
    g_installer_path[0] = '\0';

    prism::SpeakUrgent(acc::strings::Get(acc::strings::Id::UpdateDownloading));
    std::thread(DownloadWorker).detach();
}

}  // namespace acc::update_checker
