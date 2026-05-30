#include "intro_skip.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "log.h"

namespace acc::intro_skip {

namespace {

// Mirror of installer/IntroMovieDisabler.cs::IntroFiles. Verified
// against the swkotor.exe string table — only PlayMoviesAsync references
// these three names.
const char* const kIntroFiles[] = {
    "biologo.bik",
    "leclogo.bik",
    "legal.bik",
};
constexpr int kIntroFileCount = sizeof(kIntroFiles) / sizeof(kIntroFiles[0]);
constexpr const char* kDisabledSuffix = ".disabled";

// Resolve <game-install>/Movies/ from the running EXE's location.
// swkotor.exe lives in the game root, so dirname(GetModuleFileName(NULL))
// + "\\Movies" gives us the right folder regardless of how the user
// installed (Steam, GoG, custom path).
bool GetMoviesDir(char* out, size_t outSize) {
    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    // Strip trailing filename.
    for (DWORD i = n; i > 0; --i) {
        if (exePath[i - 1] == '\\' || exePath[i - 1] == '/') {
            exePath[i - 1] = '\0';
            break;
        }
    }
    int w = _snprintf_s(out, outSize, _TRUNCATE, "%s\\Movies", exePath);
    return w > 0;
}

bool FileExists(const char* path) {
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

State CurrentState() {
    char moviesDir[MAX_PATH];
    if (!GetMoviesDir(moviesDir, sizeof(moviesDir))) {
        acclog::Write("IntroSkip", "CurrentState: GetMoviesDir failed");
        return State::Unknown;
    }
    // Probe just the first file as the representative; the installer
    // and our SetDisabled keep all three in lockstep.
    char enabledPath[MAX_PATH];
    char disabledPath[MAX_PATH];
    _snprintf_s(enabledPath,  sizeof(enabledPath),  _TRUNCATE,
                "%s\\%s", moviesDir, kIntroFiles[0]);
    _snprintf_s(disabledPath, sizeof(disabledPath), _TRUNCATE,
                "%s\\%s%s", moviesDir, kIntroFiles[0], kDisabledSuffix);

    bool en = FileExists(enabledPath);
    bool di = FileExists(disabledPath);
    if (en && !di) return State::Enabled;
    if (di && !en) return State::Disabled;
    return State::Unknown;
}

bool SetDisabled(bool disable) {
    char moviesDir[MAX_PATH];
    if (!GetMoviesDir(moviesDir, sizeof(moviesDir))) {
        acclog::Write("IntroSkip", "SetDisabled: GetMoviesDir failed");
        return false;
    }

    bool allOk = true;
    int renamed = 0;
    int noop = 0;
    for (int i = 0; i < kIntroFileCount; ++i) {
        char enabledPath[MAX_PATH];
        char disabledPath[MAX_PATH];
        _snprintf_s(enabledPath,  sizeof(enabledPath),  _TRUNCATE,
                    "%s\\%s", moviesDir, kIntroFiles[i]);
        _snprintf_s(disabledPath, sizeof(disabledPath), _TRUNCATE,
                    "%s\\%s%s", moviesDir, kIntroFiles[i], kDisabledSuffix);

        const char* from = disable ? enabledPath  : disabledPath;
        const char* to   = disable ? disabledPath : enabledPath;

        if (FileExists(to) && !FileExists(from)) {
            ++noop;
            continue;  // already in the requested state for this file
        }
        if (!FileExists(from)) {
            acclog::Write("IntroSkip",
                "SetDisabled(%d): source missing for %s — neither name present",
                disable ? 1 : 0, kIntroFiles[i]);
            allOk = false;
            continue;
        }

        if (!MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            acclog::Write("IntroSkip",
                "SetDisabled(%d): MoveFileEx %s -> %s failed err=%lu",
                disable ? 1 : 0, from, to, err);
            allOk = false;
            continue;
        }
        ++renamed;
    }

    acclog::Write("IntroSkip",
        "SetDisabled(%d): renamed=%d noop=%d ok=%d",
        disable ? 1 : 0, renamed, noop, allOk ? 1 : 0);
    return allOk;
}

}  // namespace acc::intro_skip
