// internal seam between update_checker.cpp and update_checker_http.cpp.
//
// This is NOT public API - update_checker.h stays the surface core_tick
// and the menus use. The Phase-1 structure pass (refactoring candidate 9)
// split the generic "talk to GitHub over WinHTTP and pick fields out of
// the JSON" plumbing away from the update orchestration (worker threads,
// handoff batch, F5 polling), and these are the primitives that cross the
// resulting boundary.
//
// Everything here is release-independent: no update state, no atomics, no
// knowledge of what is being downloaded. The hosts, paths, asset name and
// timeouts are all passed in by the caller in update_checker.cpp.

#pragma once

#include <windows.h>
#include <winhttp.h>

#include <cstddef>
#include <string>

namespace acc::update_checker {

// Open an HTTPS session+connection to `host` with our User-Agent. Caller
// closes session + connection on completion.
bool OpenSession(const wchar_t* host, HINTERNET& session, HINTERNET& connection);

// GET `path` on an open connection, appending the body to `out`.
bool HttpGetToString(HINTERNET connection, const wchar_t* path,
                     int timeoutMs, std::string& out);

// GET `path` without following redirects; yields the Location header.
bool HttpGetRedirectLocation(HINTERNET connection, const wchar_t* path,
                             int timeoutMs, wchar_t* outLoc, size_t outCap);

// Pull the trailing path segment (the release tag) out of a Location URL.
bool ParseTagFromLocation(const wchar_t* loc, char* out, size_t outCap);

// Download `url` to `destPath`. Follows redirects.
bool HttpDownloadUrlToFile(const wchar_t* url, const char* destPath, int timeoutMs);

// "v1.2.3" -> "1.2.3".
void StripTagToVersion(const char* rawTag, char* out, size_t outCap);

// Field extraction from a GitHub release JSON body.
bool ExtractRawTagName(const std::string& json, char* out, size_t outCap);
bool ExtractAssetApiUrl(const std::string& json, const char* assetName,
                        wchar_t* outUrl, size_t outCap);

// Four-part numeric version compare; missing parts read as 0.
bool IsRemoteNewer(const char* remote, const char* local);

}  // namespace acc::update_checker
