// KOTOR Accessibility — DLL entry + lazy-init plumbing.
//
// Layer: core/ (high-trust, stable). DllMain runs under the loader lock so
// nothing here may load DLLs, init COM, or open files. The actual screen-
// reader bridge (Tolk) is loaded lazily on the first hook fire — see
// EnsureTolkInitialized below.
//
// This file is the smallest of the patch — it defines the DLL boundary and
// the OnRulesInit infrastructure-test detour. Everything else (engine
// readers, menu accessibility, future nav-system subsystems) lives in
// engine_*.cpp / menus_*.cpp / etc.

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "tolk.h"

namespace {

// Keep in sync with manifest.toml [patch].version. We could parse the manifest
// at runtime, but a single literal is simpler and the build will fail loudly
// if the two ever drift on a release.
constexpr const char* kModVersion = "0.1.0";

char g_versionSha[128] = "(unset)";

}  // namespace

// Lazy Tolk init. First hook to fire runs it; subsequent calls are no-ops.
// Speaks a one-line "loaded, version X" greeting on the first successful init
// so the user knows the patch is active even when no focus events have fired.
//
// Exposed (not in the anonymous namespace) so any TU's hook handler can call
// it from its first-fire path. Defined here because the version literal lives
// here.
void EnsureTolkInitialized() {
    static bool done = false;
    if (done) return;
    done = true;
    if (tolk::Init()) {
        char greeting[128];
        snprintf(greeting, sizeof(greeting),
                 "KOTOR accessibility mod loaded, version %s", kModVersion);
        tolk::Speak(greeting, /*interrupt=*/true);
    }
}

// CSWRules::CSWRules first-construction infrastructure-test detour. Logs
// the first fire as a "patch is alive" signal and ensures Tolk is loaded
// before any focus event hits us. Hook is registered in hooks.toml at
// 0x00552c9a.
extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    static bool fired = false;
    if (fired) return;
    fired = true;
    EnsureTolkInitialized();
    acclog::Write("first CSWRules construction; detour active");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        acclog::Init(hinstDLL);
        DWORD n = GetEnvironmentVariableA(
            "KOTOR_VERSION_SHA", g_versionSha, sizeof(g_versionSha));
        if (n == 0 || n >= sizeof(g_versionSha)) {
            strncpy_s(g_versionSha, "(unset)", _TRUNCATE);
        }
        acclog::Write("DLL_PROCESS_ATTACH sha=%s", g_versionSha);
        // Tolk init is intentionally deferred to first hook fire — see header.
    }
    return TRUE;
}
