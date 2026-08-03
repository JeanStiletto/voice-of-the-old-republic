#include "intro_skip.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "engine_game.h"
#include "log.h"

namespace acc::intro_skip {

namespace {

// Mirror of installer/GameTarget.cs::IntroMovieFiles — the two lists must
// agree, because the installer's rename IS this module's persisted state.
//
// KOTOR 1: verified against the swkotor.exe string table; only
// PlayMoviesAsync references these three names.
const char* const kIntroFilesK1[] = {
    "biologo.bik",
    "leclogo.bik",
    "legal.bik",
};

// KOTOR 2 has no biologo.bik. Its readable string data references the same
// leclogo and legal stems; ObsidianEnt and Aspyr are launch-time logos by
// every other sign but could not be confirmed in the executable, whose code
// section Steam's wrapper encrypts on disk. Including them is safe either
// way — a movie the engine cannot open is skipped, and both the uninstaller
// and the in-game toggle put every name back.
const char* const kIntroFilesK2[] = {
    "leclogo.bik",
    "legal.bik",
    "ObsidianEnt.bik",
    "Aspyr.bik",
};

constexpr const char* kDisabledSuffix = ".disabled";

// The list for the game we are running in. Entry 0 is the representative
// CurrentState() probes, so it must be a file that game actually ships.
const char* const* IntroFiles(int* countOut) {
    if (acc::game::IsKotor2()) {
        *countOut = sizeof(kIntroFilesK2) / sizeof(kIntroFilesK2[0]);
        return kIntroFilesK2;
    }
    *countOut = sizeof(kIntroFilesK1) / sizeof(kIntroFilesK1[0]);
    return kIntroFilesK1;
}

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
    // and our SetDisabled keep the whole list in lockstep.
    int count = 0;
    const char* const* introFiles = IntroFiles(&count);

    char enabledPath[MAX_PATH];
    char disabledPath[MAX_PATH];
    _snprintf_s(enabledPath,  sizeof(enabledPath),  _TRUNCATE,
                "%s\\%s", moviesDir, introFiles[0]);
    _snprintf_s(disabledPath, sizeof(disabledPath), _TRUNCATE,
                "%s\\%s%s", moviesDir, introFiles[0], kDisabledSuffix);

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

    int introFileCount = 0;
    const char* const* introFiles = IntroFiles(&introFileCount);

    bool allOk = true;
    int renamed = 0;
    int noop = 0;
    for (int i = 0; i < introFileCount; ++i) {
        char enabledPath[MAX_PATH];
        char disabledPath[MAX_PATH];
        _snprintf_s(enabledPath,  sizeof(enabledPath),  _TRUNCATE,
                    "%s\\%s", moviesDir, introFiles[i]);
        _snprintf_s(disabledPath, sizeof(disabledPath), _TRUNCATE,
                    "%s\\%s%s", moviesDir, introFiles[i], kDisabledSuffix);

        const char* from = disable ? enabledPath  : disabledPath;
        const char* to   = disable ? disabledPath : enabledPath;

        if (FileExists(to) && !FileExists(from)) {
            ++noop;
            continue;  // already in the requested state for this file
        }
        if (!FileExists(from)) {
            // Neither name present: the user deleted the .bik, or this
            // distribution never shipped it. Not a failure — the same call
            // the installer's IntroMovieDisabler counts as "missing" rather
            // than an error. Treating it as one would make the in-game
            // toggle report failure on a build that simply has fewer logo
            // movies, even though every file that IS there was renamed.
            acclog::Write("IntroSkip",
                "SetDisabled(%d): %s absent in both forms — skipping",
                disable ? 1 : 0, introFiles[i]);
            ++noop;
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
