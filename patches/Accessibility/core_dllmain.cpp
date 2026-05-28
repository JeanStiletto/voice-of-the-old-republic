// DLL entry + lazy-init plumbing.
//
// DllMain runs under the loader lock — no LoadLibrary, no COM init, no
// file I/O here. Prism is initialised lazily on the first hook fire.

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "prism.h"

namespace {

// Keep in sync with manifest.toml [patch].version.
constexpr const char* kModVersion = "0.1.0";

char g_versionSha[128] = "(unset)";

}  // namespace

// First hook to fire runs it; subsequent calls are no-ops. Speaks a
// "loaded, version X" greeting so the user knows the patch is active.
void EnsurePrismInitialized() {
    static bool done = false;
    if (done) return;
    done = true;
    if (prism::Init()) {
        char greeting[128];
        snprintf(greeting, sizeof(greeting),
                 "KOTOR accessibility mod loaded, version %s", kModVersion);
        prism::Speak(greeting, /*interrupt=*/true);
    }
}

// CSWRules::CSWRules construction detour (hooks.toml @ 0x00552c9a).
// First fire is the "patch alive" signal and Prism-init trigger.
extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    static bool fired = false;
    if (fired) return;
    fired = true;
    EnsurePrismInitialized();
    acclog::Write("Init", "first CSWRules construction; detour active");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        acclog::Init(hinstDLL);
        DWORD n = GetEnvironmentVariableA(
            "KOTOR_VERSION_SHA", g_versionSha, sizeof(g_versionSha));
        if (n == 0 || n >= sizeof(g_versionSha)) {
            strncpy_s(g_versionSha, "(unset)", _TRUNCATE);
        }
        acclog::Write("Init", "DLL_PROCESS_ATTACH sha=%s", g_versionSha);
        // Prism init deferred — loader lock.
    }
    return TRUE;
}
