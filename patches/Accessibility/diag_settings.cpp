// Startup snapshot — see diag_settings.h. Runs once from OnRulesInit, after
// loader lock is long past so file I/O + heap alloc are safe.

#include "diag_settings.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"

namespace acc::diag::settings {

namespace {

// 64 KB comfortably holds any single KOTOR ini section (Keymapping, the
// biggest, is ~4 KB) and the full list of section names.
constexpr DWORD kIniBufBytes = 64 * 1024;

bool ResolveInstallRelative(const char* rel, char* out, size_t outCap) {
    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
    if (n == 0 || n >= sizeof(exePath)) return false;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    int written = _snprintf_s(out, outCap, _TRUNCATE, "%s%s", exePath, rel);
    return written > 0;
}

void DumpIni(const char* iniPath) {
    HANDLE heap = GetProcessHeap();
    auto* sections = static_cast<char*>(HeapAlloc(heap, HEAP_ZERO_MEMORY, kIniBufBytes));
    auto* keys     = static_cast<char*>(HeapAlloc(heap, HEAP_ZERO_MEMORY, kIniBufBytes));
    if (!sections || !keys) {
        if (sections) HeapFree(heap, 0, sections);
        if (keys)     HeapFree(heap, 0, keys);
        acclog::Write("Settings.Ini", "heap alloc failed; skipping");
        return;
    }

    DWORD got = GetPrivateProfileSectionNamesA(sections, kIniBufBytes, iniPath);
    if (got == 0) {
        DWORD err = GetLastError();
        acclog::Write("Settings.Ini", "no sections at %s (err=%lu)", iniPath, err);
        HeapFree(heap, 0, sections);
        HeapFree(heap, 0, keys);
        return;
    }

    acclog::Write("Settings.Ini", "begin dump path=%s", iniPath);
    int totalKeys = 0;
    for (const char* sec = sections; *sec != '\0'; sec += strlen(sec) + 1) {
        acclog::Write("Settings.Ini", "[%s]", sec);
        DWORD k = GetPrivateProfileSectionA(sec, keys, kIniBufBytes, iniPath);
        if (k == 0) continue;
        for (const char* kv = keys; *kv != '\0'; kv += strlen(kv) + 1) {
            // Drop ini comments — Windows usually strips these but be defensive.
            if (*kv == ';' || *kv == '#') continue;
            acclog::Write("Settings.Ini", "  %s", kv);
            ++totalKeys;
        }
    }
    acclog::Write("Settings.Ini", "end dump: %d keys", totalKeys);

    HeapFree(heap, 0, sections);
    HeapFree(heap, 0, keys);
}

void ProbeFile(const char* rel) {
    char full[MAX_PATH];
    if (!ResolveInstallRelative(rel, full, sizeof(full))) {
        acclog::Write("Settings.Files", "%s: resolve failed", rel);
        return;
    }
    DWORD attrs = GetFileAttributesA(full);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        acclog::Write("Settings.Files", "%s: absent", rel);
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        acclog::Write("Settings.Files", "%s: present (dir)", rel);
        return;
    }
    HANDLE h = CreateFileA(
        full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER size = {};
    if (h != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(h, &size);
        CloseHandle(h);
    }
    acclog::Write("Settings.Files", "%s: present size=%lld", rel, size.QuadPart);
}

int CountFiles(const char* relDir) {
    char dir[MAX_PATH];
    if (!ResolveInstallRelative(relDir, dir, sizeof(dir))) return -1;
    char pattern[MAX_PATH];
    if (_snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s\\*", dir) <= 0) return -1;
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ++n;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

}  // namespace

void LogStartupSnapshot() {
    static bool fired = false;
    if (fired) return;
    fired = true;

    char iniPath[MAX_PATH];
    if (!ResolveInstallRelative("swkotor.ini", iniPath, sizeof(iniPath))) {
        acclog::Write("Settings.Ini", "could not resolve swkotor.ini path");
    } else {
        DumpIni(iniPath);
    }

    // Audio + input proxy DLLs in the install root. dsoal is the bundled
    // spatial-audio toggle (dsound.dll + dsoal-aldrv.dll), dinput8 is our
    // own loader proxy, mss32 is the Miles audio runtime — its absence or
    // replacement would explain a sudden no-audio report.
    ProbeFile("dsound.dll");
    ProbeFile("dsoal-aldrv.dll");
    ProbeFile("dsoal-alsoft.ini");
    ProbeFile("dinput8.dll");
    ProbeFile("mss32.dll");

    int overrideCount = CountFiles("Override");
    if (overrideCount < 0) {
        acclog::Write("Settings.Files", "Override\\: absent or unreadable");
    } else {
        acclog::Write("Settings.Files", "Override\\: %d files", overrideCount);
    }
}

}  // namespace acc::diag::settings
