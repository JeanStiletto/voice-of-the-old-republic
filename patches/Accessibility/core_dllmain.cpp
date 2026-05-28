// DLL entry + lazy-init plumbing.
//
// DllMain runs under the loader lock — no LoadLibrary, no COM init, no
// file I/O here. Prism is initialised lazily on the first hook fire,
// after which we also detect the user's installed language from
// dialog.tlk and select the matching strings + combat-anchor tables.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "mod_version.h"
#include "prism.h"
#include "strings.h"
#include "update_checker.h"

namespace {

char g_versionSha[128] = "(unset)";

const char* LangName(acc::strings::Lang l) {
    switch (l) {
        case acc::strings::Lang::En: return "English";
        case acc::strings::Lang::De: return "German";
        case acc::strings::Lang::Fr: return "French";
        case acc::strings::Lang::It: return "Italian";
        case acc::strings::Lang::Es: return "Spanish";
    }
    return "Unknown";
}

// Read the LanguageID byte from <install>/dialog.tlk to determine the
// engine locale the player has actually installed, so combat-anchor
// matching + Id::* speech route to the right table. Defaults to German
// on any failure — same as the historical hardcoded default, so a
// detection failure doesn't break the existing DE install.
//
// TLK header layout (12 bytes): "TLK " "V3.0" int32_le LanguageID.
// Locale IDs (per kdev `LanguageIdToCode`): 0=En 1=Fr 2=De 3=It 4=Es.
acc::strings::Lang DetectLanguageFromTlk() {
    using L = acc::strings::Lang;

    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
    if (n == 0 || n >= sizeof(exePath)) {
        acclog::Write("Lang", "GetModuleFileName failed; defaulting to German");
        return L::De;
    }

    char* slash = strrchr(exePath, '\\');
    if (!slash) {
        acclog::Write("Lang", "exe path has no backslash (%s); defaulting to German", exePath);
        return L::De;
    }
    size_t tailCap = sizeof(exePath) - static_cast<size_t>(slash + 1 - exePath);
    strncpy_s(slash + 1, tailCap, "dialog.tlk", _TRUNCATE);

    FILE* fp = nullptr;
    if (fopen_s(&fp, exePath, "rb") != 0 || !fp) {
        acclog::Write("Lang", "dialog.tlk not readable at %s; defaulting to German", exePath);
        return L::De;
    }

    unsigned char header[12] = {};
    size_t got = fread(header, 1, sizeof(header), fp);
    fclose(fp);

    if (got < sizeof(header) || memcmp(header, "TLK ", 4) != 0) {
        acclog::Write("Lang", "dialog.tlk bad header (%zu bytes); defaulting to German", got);
        return L::De;
    }

    int32_t langId = 0;
    memcpy(&langId, header + 8, sizeof(langId));

    L detected;
    switch (langId) {
        case 0: detected = L::En; break;
        case 1: detected = L::Fr; break;
        case 2: detected = L::De; break;
        case 3: detected = L::It; break;
        case 4: detected = L::Es; break;
        default:
            acclog::Write("Lang", "unknown LanguageID=%d; defaulting to German", langId);
            return L::De;
    }
    acclog::Write("Lang", "detected LanguageID=%d -> %s", langId, LangName(detected));
    return detected;
}

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
                 "KOTOR accessibility mod loaded, version %s", acc::kModVersion);
        prism::Speak(greeting, /*interrupt=*/true);
    }
}

// CSWRules::CSWRules construction detour (hooks.toml @ 0x00552c9a).
// First fire is the "patch alive" signal, Prism-init trigger, and the
// point at which we detect the user's installed language. File I/O is
// safe here (we're well past loader lock by the time CSWRules runs).
extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    static bool fired = false;
    if (fired) return;
    fired = true;
    acc::strings::SetLanguage(DetectLanguageFromTlk());
    EnsurePrismInitialized();
    acc::update_checker::StartBackgroundCheck();
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
