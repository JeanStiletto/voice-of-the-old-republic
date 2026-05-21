// KOTOR Accessibility — DLL entry + lazy-init plumbing.
//
// Layer: core/ (high-trust, stable). DllMain runs under the loader lock so
// nothing here may load DLLs, init COM, or open files. The actual speech
// bridge (Prism) is loaded lazily on the first hook fire — see
// EnsureTolkInitialized below. (Function name is a historical hold-over
// from the pre-migration Tolk-based path; the underlying init is Prism.)
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

// Lazy speech-bridge init. First hook to fire runs it; subsequent calls are no-ops.
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

// One-shot RE helper: dump the runtime-decrypted bytes of a function so we
// can disassemble offline. The on-disk swkotor.exe is packed (random-looking
// bytes in the .text section) — only the live process has the unpacked
// instructions, and we run inside that process. Logged as a single line of
// hex so the patch log becomes the source of truth for any RE work that
// Lane's Ghidra DB doesn't expose decompiled.
//
// Removable in one commit once the targeted RE finishes.
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

// CSWRules::CSWRules first-construction infrastructure-test detour. Logs
// the first fire as a "patch is alive" signal and ensures Tolk is loaded
// before any focus event hits us. Hook is registered in hooks.toml at
// 0x00552c9a.
extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    static bool fired = false;
    if (fired) return;
    fired = true;
    EnsureTolkInitialized();
    acclog::Write("Init", "first CSWRules construction; detour active");

    // RE: dump CSWGuiInGameCharacter::ShowLevelUpGUI (btn_levelup click
    // handler) — currently returns 0 with no panel visible. Need to find
    // the gate. Function range 0x006b0bb0..0x006b0ca1 (0xf1 bytes). Also
    // dump CGuiInGame::ShowLevelUpGUI (0x0062dc00..0x0062dd13, 0x113
    // bytes) since it's the dispatcher we tried first.
    DumpFunctionBytes("CSWGuiInGameCharacter::ShowLevelUpGUI",
                      0x006b0bb0, 0xf1);
    DumpFunctionBytes("CGuiInGame::ShowLevelUpGUI",
                      0x0062dc00, 0x113);

    // Combat-log live narration: SARIF xref analysis (2026-05-21) showed
    // CSWGuiInGameMessages::AddMessages @0x626920 has only ONE caller —
    // ShowDialogEntry, which is on the dialog path, not the combat-log
    // path. patch-20260521-095251.log confirmed: our hook on AddMessages
    // fired zero times during a live fight even though 64 rows appeared
    // in messages_listbox at review-screen open.
    //
    // The real live combat-log surface is CGuiInGame::AppendToMsgBuffer
    // @0x0062b5c0 (185 bytes). Dump it + AppendToDialogBuffer +
    // ShowFlashingStatus + AddFloatyText so we have prologues for all the
    // CGuiInGame-side feedback writers in one pass.
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
        // Speech-bridge init is intentionally deferred to first hook fire — see header.
    }
    return TRUE;
}
