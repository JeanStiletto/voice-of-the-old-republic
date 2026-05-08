#include "tolk.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "log.h"

namespace tolk {

namespace {

// Tolk's C ABI (from third_party/tolk/include/Tolk.h). All cdecl. We declare
// the function-pointer types here instead of including Tolk.h so the build
// has no header dependency on the upstream tree.
typedef void          (__cdecl* PFN_Tolk_Load)();
typedef void          (__cdecl* PFN_Tolk_Unload)();
typedef bool          (__cdecl* PFN_Tolk_IsLoaded)();
typedef bool          (__cdecl* PFN_Tolk_HasSpeech)();
typedef bool          (__cdecl* PFN_Tolk_Output)(const wchar_t*, bool);
typedef bool          (__cdecl* PFN_Tolk_Silence)();
typedef const wchar_t*(__cdecl* PFN_Tolk_DetectScreenReader)();

HMODULE g_lib       = nullptr;
bool    g_initTried = false;
bool    g_available = false;

PFN_Tolk_Load                pTolk_Load                = nullptr;
PFN_Tolk_Unload              pTolk_Unload              = nullptr;
PFN_Tolk_IsLoaded            pTolk_IsLoaded            = nullptr;
PFN_Tolk_HasSpeech           pTolk_HasSpeech           = nullptr;
PFN_Tolk_Output              pTolk_Output              = nullptr;
PFN_Tolk_Silence             pTolk_Silence             = nullptr;
PFN_Tolk_DetectScreenReader  pTolk_DetectScreenReader  = nullptr;

template <typename T>
bool Resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(g_lib, name));
    if (!fn) {
        acclog::Write("Tolk", "GetProcAddress(%s) failed", name);
        return false;
    }
    return true;
}

}  // namespace

bool Init() {
    if (g_initTried) return g_available;
    g_initTried = true;

    const char* patchDir = acclog::PatchDir();
    if (!patchDir || !*patchDir) {
        acclog::Write("Tolk", "patch dir unknown; aborting init");
        return false;
    }

    // Build absolute paths. Tolk.dll lives next to our DLL; nvdaControllerClient32.dll
    // does too. Tolk_Load() does its own LoadLibrary on the driver client DLLs
    // using a bare name, so we point the DLL search path at our dir for the
    // duration of the call and restore it afterwards.
    char tolkPath[MAX_PATH];
    int written = snprintf(tolkPath, sizeof(tolkPath), "%s\\Tolk.dll", patchDir);
    if (written <= 0 || written >= (int)sizeof(tolkPath)) {
        acclog::Write("Tolk", "tolk.dll path overflow");
        return false;
    }

    char prevDir[MAX_PATH] = {0};
    DWORD prevLen = GetDllDirectoryA(MAX_PATH, prevDir);
    SetDllDirectoryA(patchDir);

    g_lib = LoadLibraryA(tolkPath);
    if (!g_lib) {
        acclog::Write("Tolk", "LoadLibrary(%s) failed err=%lu", tolkPath, GetLastError());
        SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
        return false;
    }

    bool ok =
        Resolve(pTolk_Load,                "Tolk_Load")               &&
        Resolve(pTolk_Unload,              "Tolk_Unload")             &&
        Resolve(pTolk_IsLoaded,            "Tolk_IsLoaded")           &&
        Resolve(pTolk_HasSpeech,           "Tolk_HasSpeech")          &&
        Resolve(pTolk_Output,              "Tolk_Output")             &&
        Resolve(pTolk_Silence,             "Tolk_Silence")            &&
        Resolve(pTolk_DetectScreenReader,  "Tolk_DetectScreenReader");

    if (!ok) {
        FreeLibrary(g_lib);
        g_lib = nullptr;
        SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
        return false;
    }

    pTolk_Load();
    g_available = pTolk_IsLoaded() && pTolk_HasSpeech();

    const wchar_t* name = pTolk_DetectScreenReader();
    if (g_available) {
        // wprintf into ANSI for the log; screen reader names are ASCII in practice.
        char nameA[64] = "?";
        if (name) WideCharToMultiByte(CP_UTF8, 0, name, -1, nameA, sizeof(nameA), nullptr, nullptr);
        acclog::Write("Tolk", "loaded, screen reader = %s", nameA);
    } else {
        acclog::Write("Tolk", "loaded, but no screen reader with speech detected (running silent)");
    }

    SetDllDirectoryA(prevLen > 0 ? prevDir : nullptr);
    return g_available;
}

bool IsAvailable() {
    return g_available;
}

// Self-log every Speak call so the patch log carries one canonical record
// of what NVDA was asked to say, in chronological order. Trace dedups
// consecutive identical strings (the "control 5" placeholder fired 7 times
// in a row case) while still preserving the count via the (repeated Nx)
// flush on change. "[!]" prefix marks interrupt=true so the audit also
// shows when speech preempted in-flight output.
void Speak(const wchar_t* text, bool interrupt) {
    if (!g_available || !text) return;
    char audit[512];
    int conv = WideCharToMultiByte(CP_UTF8, 0, text, -1, audit, sizeof(audit),
                                   nullptr, nullptr);
    if (conv > 0) {
        acclog::Trace("Tolk.spoke", "%s%s", interrupt ? "[!] " : "", audit);
    }
    pTolk_Output(text, interrupt);
}

void Speak(const char* text, bool interrupt) {
    if (!g_available || !text || !*text) return;
    acclog::Trace("Tolk.spoke", "%s%s", interrupt ? "[!] " : "", text);
    wchar_t buf[512];
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    if (n <= 0) return;
    pTolk_Output(buf, interrupt);
}

void Silence() {
    if (!g_available) return;
    acclog::Trace("Tolk.spoke", "(silenced)");
    pTolk_Silence();
}

const wchar_t* ActiveScreenReader() {
    if (!g_available || !pTolk_DetectScreenReader) return L"none";
    const wchar_t* name = pTolk_DetectScreenReader();
    return name ? name : L"none";
}

void Shutdown() {
    if (g_lib && pTolk_Unload) {
        pTolk_Unload();
    }
    g_available = false;
    // Deliberately leak g_lib at process-shutdown — FreeLibrary inside
    // DLL_PROCESS_DETACH risks the loader lock + COM uninit ordering issues.
}

}  // namespace tolk
