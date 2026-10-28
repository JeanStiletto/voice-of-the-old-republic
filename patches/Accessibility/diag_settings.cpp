// Startup snapshot — see diag_settings.h. Runs once from OnRulesInit, after
// loader lock is long past so file I/O + heap alloc are safe.

#include "diag_settings.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio_bus.h"    // GetSoundInternal — the engine's Miles 3D provider handle
#include "engine_game.h"  // IsKotor2 — per-game ini filename
#include "engine_offsets_select.h"
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

// ---- Audio path ----------------------------------------------------------
// dsoal (the DirectSound -> OpenAL Soft wrapper the installer offers) can only
// spatialize what reaches it as a DirectSound 3D buffer. That depends on the
// 3D provider the game's sound library, Miles, opened: its DirectSound3D and
// EAX providers hand each 3D sound to dsound.dll; its own software providers
// mix 3D sounds down to stereo first, leaving dsoal nothing to place. Nothing
// in the game reports which one it opened, so LogAudioPathOnce does.

// CExoSoundInternal +0xf8 holds the HPROVIDER from AIL_open_3D_provider (the
// Ghidra type database's "3d_provider_handle"; a placeholder-struct offset,
// so LogMilesProvider only trusts it when it equals a handle Miles itself
// enumerates). KOTOR 2's layout is not mapped.
const size_t kSoundInternal3dProviderOff = acc::off::Todo(0xf8);

// Miles (MSS 6) export, __stdcall. HPROENUM / HPROVIDER are 32-bit handles;
// enumeration starts at HPROENUM_FIRST = 0.
typedef int (__stdcall* AilEnumerate3dProvidersFn)(uint32_t* next, uint32_t* provider,
                                                   char** name);

bool ContainsNoCase(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    const size_t n = strlen(needle);
    for (; *hay; ++hay) {
        if (_strnicmp(hay, needle, n) == 0) return true;
    }
    return false;
}

// Reads the engine's provider handle and walks Miles' provider list. SEH
// only: no objects with destructors here (C2712).
void LogMilesProvider(void* soundInternal) {
    HMODULE mss = GetModuleHandleA("mss32.dll");
    if (!mss) {
        acclog::Write("Audio.Path", "mss32.dll not loaded; no Miles provider to report");
        return;
    }
    auto enumerate = reinterpret_cast<AilEnumerate3dProvidersFn>(
        GetProcAddress(mss, "_AIL_enumerate_3D_providers@12"));
    if (!enumerate) {
        acclog::Write("Audio.Path", "mss32.dll has no AIL_enumerate_3D_providers export");
        return;
    }

    uint32_t opened = 0;
    bool     haveOpened = false;
    if (acc::off::Ok(kSoundInternal3dProviderOff)) {
        __try {
            opened = *reinterpret_cast<uint32_t*>(
                reinterpret_cast<char*>(soundInternal) + kSoundInternal3dProviderOff);
            haveOpened = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            haveOpened = false;
        }
    }

    char chosen[128] = {};
    int  count = 0;
    __try {
        uint32_t next = 0, provider = 0;
        char*    name = nullptr;
        while (count < 32 && enumerate(&next, &provider, &name)) {
            const bool isOpened = haveOpened && provider == opened;
            acclog::Write("Audio.Path", "Miles 3D provider %u: %s%s", provider,
                          name ? name : "(no name)", isOpened ? "   <- opened by the game" : "");
            if (isOpened) _snprintf_s(chosen, sizeof(chosen), _TRUNCATE, "%s", name ? name : "");
            ++count;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Audio.Path", "Miles provider enumeration faulted after %d entries", count);
    }

    if (!haveOpened) {
        acclog::Write("Audio.Path", "provider the game opened: not read on this game (layout not mapped)");
        return;
    }
    if (!chosen[0]) {
        acclog::Write("Audio.Path",
                      "provider the game opened: handle %u matches none of the %d listed "
                      "(no 3D provider open, or the offset is wrong)", opened, count);
        return;
    }
    const bool viaDirectSound = ContainsNoCase(chosen, "DirectSound") || ContainsNoCase(chosen, "EAX");
    acclog::Write("Audio.Path", "provider the game opened: %s -> %s", chosen,
                  viaDirectSound
                      ? "3D sounds reach dsound.dll as 3D buffers (dsoal spatializes them when installed)"
                      : "Miles mixes 3D sounds itself; dsoal only receives the stereo result (no HRTF)");
}

// Where dsound.dll was loaded from: the game folder means dsoal, System32
// means Windows' own DirectSound.
void LogDirectSoundModule() {
    HMODULE ds = GetModuleHandleA("dsound.dll");
    if (!ds) {
        acclog::Write("Audio.Path", "dsound.dll not loaded");
    } else {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(ds, path, sizeof(path));
        acclog::Write("Audio.Path", "dsound.dll loaded from %s", path);
    }
    acclog::Write("Audio.Path", "dsoal-aldrv.dll (dsoal's OpenAL Soft) %s",
                  GetModuleHandleA("dsoal-aldrv.dll") ? "loaded" : "not loaded");
}

// The alsoft.ini keys that decide HRTF.
void LogAlsoftConfig() {
    char path[MAX_PATH];
    if (!ResolveInstallRelative("alsoft.ini", path, sizeof(path)) ||
        GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    static const char* const kKeys[] = {"channels", "stereo-mode", "stereo-encoding", "hrtf",
                                        "default-hrtf", "hrtf-mode"};
    for (const char* key : kKeys) {
        char value[128] = {};
        GetPrivateProfileStringA("general", key, "", value, sizeof(value), path);
        if (value[0]) acclog::Write("Audio.Path", "alsoft.ini [general] %s=%s", key, value);
    }
}

}  // namespace

void LogAudioPathOnce() {
    static bool done = false;
    if (done) return;
    void* internal = acc::audio::GetSoundInternal();
    if (!internal) return;  // sound system not up yet
    done = true;
    LogDirectSoundModule();
    LogMilesProvider(internal);
    LogAlsoftConfig();
}

void LogStartupSnapshot() {
    static bool fired = false;
    if (fired) return;
    fired = true;

    const char* iniName =
        acc::game::IsKotor2() ? "swkotor2.ini" : "swkotor.ini";
    char iniPath[MAX_PATH];
    if (!ResolveInstallRelative(iniName, iniPath, sizeof(iniPath))) {
        acclog::Write("Settings.Ini", "could not resolve %s path", iniName);
    } else {
        DumpIni(iniPath);
    }

    // Audio + input proxy DLLs in the install root. dsoal is the bundled
    // spatial-audio toggle (dsound.dll + dsoal-aldrv.dll), dinput8 is our
    // own loader proxy, mss32 is the Miles audio runtime — its absence or
    // replacement would explain a sudden no-audio report.
    ProbeFile("dsound.dll");
    ProbeFile("dsoal-aldrv.dll");
    ProbeFile("alsoft.ini");  // dsoal's OpenAL Soft config (HRTF on/off)
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
