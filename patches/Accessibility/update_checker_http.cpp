// WinHTTP + JSON plumbing behind the update checker.
//
// Split out of update_checker.cpp by the Phase-1 structure pass
// (refactoring candidate 9). update_checker.cpp keeps the orchestration -
// worker threads, the atomic state flags, the handoff batch that swaps the
// DLL on exit, F5 polling and Tick. This file is the release-independent
// transport layer underneath it: open a session, GET a body or a redirect,
// download a file, pick fields out of the response, compare versions.
//
// Moved verbatim. The helpers that only this file uses (SkipColon,
// ReadQuotedString, ParseVersion and the ParsedVersion
// struct) came along and are not declared in update_checker_http.h.

#include "update_checker_http.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "log.h"

namespace acc::update_checker {

namespace {
constexpr const wchar_t* kUserAgent = L"VoiceOfTheOldRepublic/UpdateChecker";
}  // namespace

// ----- WinHTTP helpers ------------------------------------------------------

// Open an HTTPS session+connection to `host` with our User-Agent. Caller
// closes session + connection on completion.
bool OpenSession(const wchar_t* host, HINTERNET& session, HINTERNET& connection) {
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
        host,
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

// GET `path` on `connection` with automatic redirects DISABLED, then copy the
// 3xx response's Location header (the redirect target URL) into `outLoc`.
// Returns true if a Location was captured. Used to read the tag that
// github.com/.../releases/latest redirects to, without touching the
// rate-limited REST API.
bool HttpGetRedirectLocation(HINTERNET connection, const wchar_t* path,
                             int timeoutMs, wchar_t* outLoc, size_t outCap) {
    HINTERNET req = WinHttpOpenRequest(
        connection, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) {
        acclog::Write("Update", "redirect WinHttpOpenRequest failed: %lu", GetLastError());
        return false;
    }
    WinHttpSetTimeouts(req, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    // Disable auto-redirect so the 302 (with its Location header) comes back
    // to us instead of WinHTTP silently following it to the HTML release page.
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));

    bool ok = false;
    if (outCap > 0) outLoc[0] = L'\0';
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD len = (DWORD)(outCap * sizeof(wchar_t));
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                outLoc, &len, WINHTTP_NO_HEADER_INDEX)) {
            ok = (outLoc[0] != L'\0');
        } else {
            acclog::Write("Update", "no Location header on /releases/latest: %lu",
                          GetLastError());
        }
    } else {
        acclog::Write("Update", "redirect GET send/receive failed: %lu", GetLastError());
    }
    WinHttpCloseHandle(req);
    return ok;
}

// Parse the tag out of a release URL like
// https://github.com/<owner>/<repo>/releases/tag/v0.5.3 — copies the trailing
// path segment after "/releases/tag/" into `out` (tags are ASCII). Returns
// false if the marker isn't present.
bool ParseTagFromLocation(const wchar_t* loc, char* out, size_t outCap) {
    static const wchar_t kMarker[] = L"/releases/tag/";
    const wchar_t* p = wcsstr(loc, kMarker);
    if (!p) return false;
    p += (sizeof(kMarker) / sizeof(wchar_t)) - 1;
    size_t i = 0;
    while (*p && *p != L'/' && *p != L'?' && *p != L'#') {
        if (i + 1 < outCap) out[i++] = (char)*p;
        ++p;
    }
    if (i < outCap) out[i] = '\0';
    return out[0] != '\0';
}

// GET an asset-download URL to a file on disk. The URL is the
// api.github.com/.../releases/assets/<id> endpoint; sending
// Accept: application/octet-stream makes GitHub 302-redirect to the
// storage backend (release-assets.githubusercontent.com) instead of
// returning JSON metadata. WinHTTP's default redirect policy follows the
// https→https redirect automatically, so the host/path cracked here is
// only the initial api.github.com request.
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

    static const wchar_t kOctetAccept[] =
        L"Accept: application/octet-stream\r\n";
    bool ok = false;
    if (WinHttpSendRequest(req, kOctetAccept, (DWORD)-1L,
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

// Copy a raw release tag (e.g. "v0.5.3") into `out`, stripping the leading
// v/V and any " " / "-pre" suffix — mirrors arena's NormalizeVersion. The
// result is the comparable/displayable version string.
void StripTagToVersion(const char* rawTag, char* out, size_t outCap) {
    const char* start = (rawTag && *rawTag) ? rawTag : "";
    if (*start == 'v' || *start == 'V') ++start;
    size_t copyLen = strlen(start);
    for (size_t i = 0; i < copyLen; ++i) {
        if (start[i] == '-' || start[i] == ' ') { copyLen = i; break; }
    }
    if (copyLen >= outCap) copyLen = outCap - 1;
    memcpy(out, start, copyLen);
    out[copyLen] = '\0';
}

// Extract the top-level `"tag_name"` value verbatim (no stripping) — the exact
// tag the release is published under, needed to build the direct-download URL.
bool ExtractRawTagName(const std::string& json, char* out, size_t outCap) {
    const char* p = strstr(json.c_str(), "\"tag_name\"");
    if (!p) return false;
    p = SkipColon(p + strlen("\"tag_name\""));
    if (!p) return false;
    if (!ReadQuotedString(p, out, outCap)) return false;
    return out[0] != '\0';
}

// Find the asset whose `"name"` equals assetName and return its API
// `"url"` field — the api.github.com/.../releases/assets/<id> endpoint,
// NOT the browser_download_url. Downloading via the API endpoint with
// Accept: application/octet-stream is the path GitHub serves to the gh
// CLI; it redirects to the storage backend (release-assets.github
// usercontent.com) and stays up when the github.com/.../releases/download
// browser endpoint returns 504 during partial GitHub outages.
//
// Each asset object is `{ "url":<api>, "id":.., "node_id":.., "name":..,
// .., "browser_download_url":<browser> }`. We locate the matching
// `"name"`, scan back to that object's opening `{`, then forward to its
// first `"url"` (which precedes "name", so the uploader sub-object's url
// that follows can't be picked up by mistake).
//
// WinHTTP needs wide-char URLs, but the JSON is bytes from the wire, so
// we transcode the chosen URL at the boundary (GitHub's asset API URLs
// are always ASCII, so a straight widen is safe).
bool ExtractAssetApiUrl(const std::string& json, const char* assetName,
                        wchar_t* outUrl, size_t outCap) {
    const char* base = json.c_str();
    const char* p = base;
    while (true) {
        const char* namePos = strstr(p, "\"name\"");
        if (!namePos) return false;
        const char* afterColon = SkipColon(namePos + strlen("\"name\""));
        const char* next = namePos + strlen("\"name\"");
        if (afterColon) {
            char nameBuf[256] = {};
            const char* end = ReadQuotedString(afterColon, nameBuf, sizeof(nameBuf));
            if (end) next = end;
            if (end && strcmp(nameBuf, assetName) == 0) {
                // Scan back to this object's opening brace, then forward to
                // the first "url" key (the asset's own API url).
                const char* brace = namePos;
                while (brace > base && *brace != '{') --brace;
                const char* urlKey = strstr(brace, "\"url\"");
                if (urlKey && urlKey < namePos) {
                    const char* uc = SkipColon(urlKey + strlen("\"url\""));
                    char urlBuf[1024] = {};
                    if (uc && ReadQuotedString(uc, urlBuf, sizeof(urlBuf))) {
                        size_t len = strlen(urlBuf);
                        if (len >= outCap) len = outCap - 1;
                        for (size_t i = 0; i < len; ++i)
                            outUrl[i] = (wchar_t)(unsigned char)urlBuf[i];
                        outUrl[len] = L'\0';
                        return true;
                    }
                }
            }
        }
        p = next;
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
    // Strip leading v if present (extra defence; StripTagToVersion already does this).
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
}  // namespace acc::update_checker
