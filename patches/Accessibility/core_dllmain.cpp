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

// RE helper: dump runtime-decrypted function bytes for offline disasm.
// swkotor.exe is packed on disk — only the live process has unpacked
// instructions. Removable in one commit.
static void DumpFunctionBytes(const char* tag, uintptr_t va, size_t len) {
    if (len == 0 || len > 0x400) return;  // sanity cap; one log line ~3*len chars
    char hex[0x400 * 3 + 1];
    size_t off = 0;
    __try {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(va);
        for (size_t i = 0; i < len && off + 3 < sizeof(hex); ++i) {
            unsigned b = p[i];
            const char* hexDigits = "0123456789abcdef";
            hex[off++] = hexDigits[(b >> 4) & 0xf];
            hex[off++] = hexDigits[b & 0xf];
            hex[off++] = ' ';
        }
        hex[off] = '\0';
        acclog::Write("Init.REPeek", "%s VA=0x%08zx len=%zu: %s", tag, va, len, hex);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Init.REPeek", "%s VA=0x%08zx FAULT", tag, va);
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

    // Open RE dumps — disposable once their investigations close.
    DumpFunctionBytes("CSWGuiInGameCharacter::ShowLevelUpGUI",
                      0x006b0bb0, 0xf1);
    DumpFunctionBytes("CGuiInGame::ShowLevelUpGUI",
                      0x0062dc00, 0x113);
    DumpFunctionBytes("CGuiInGame::AppendToMsgBuffer",     0x0062b5c0, 80);
    DumpFunctionBytes("CGuiInGame::AppendToDialogBuffer",  0x0062b680, 80);
    DumpFunctionBytes("CGuiInGame::AddFloatyText",         0x0062b080, 32);
    DumpFunctionBytes("CGuiInGame::ShowFlashingStatus",    0x0062b0b0, 32);
    DumpFunctionBytes("CGuiInGame::SetCombatMessage",      0x0062b110, 32);
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
