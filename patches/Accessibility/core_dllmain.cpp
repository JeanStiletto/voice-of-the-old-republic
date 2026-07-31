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

#include "bringup_announce.h"
#include "focus_guard.h"
#include "diag_settings.h"
#include "engine_game.h"
#include "engine_keymap.h"
#include "log.h"
#include "mod_version.h"
#include "prism.h"
#include "save_crash_guard.h"
#include "strings.h"
#include "update_checker.h"
#include "engine_rebase.h"

namespace {

char g_versionSha[128] = "(unset)";

const char* LangName(acc::strings::Lang l) {
    switch (l) {
        case acc::strings::Lang::En: return "English";
        case acc::strings::Lang::De: return "German";
        case acc::strings::Lang::Fr: return "French";
        case acc::strings::Lang::It: return "Italian";
        case acc::strings::Lang::Es: return "Spanish";
        case acc::strings::Lang::Ru: return "Russian";
        case acc::strings::Lang::Pl: return "Polish";
    }
    return "Unknown";
}

// Sample the tlk's string blob and report whether it reads as Windows-1251
// Cyrillic. Needed because community Russian translations do not use a Russian
// LanguageID — Allard 1.72 (the current active one) ships LanguageID=0, the
// English slot, with the strings re-encoded in CP1251. So the declared ID
// cannot tell Russian from English and the bytes have to be inspected.
//
// In CP1251 the Cyrillic block is a solid run at 0xC0-0xFF, so Russian prose
// is overwhelmingly high-byte: measured on Allard's dialog.tlk, 78% of the
// sampled bytes were >= 0xC0. English prose is ~0%. The 20% threshold sits
// far from both, so a mixed tlk (Russian text with long ASCII resrefs) still
// classifies correctly and no plausible Latin-1 text trips it.
//
// The one non-Latin-1 page we also ship for is Polish CP1250, and it was
// measured rather than assumed, because four of its letters (ó ć ę ń) do land
// >= 0xC0: the LEM Polish dialog.tlk reads 1.88%, against German's 1.29% and
// the 20% bar. Polish is safely on the not-Cyrillic side and falls through to
// the LanguageID switch, which recognises it as ID 5.
//
// `entriesOffset` is the tlk header's offset-to-string-entries field.
bool TlkLooksCyrillic(FILE* fp, uint32_t entriesOffset) {
    if (fseek(fp, static_cast<long>(entriesOffset), SEEK_SET) != 0) return false;

    unsigned char sample[64 * 1024];
    size_t got = fread(sample, 1, sizeof(sample), fp);
    // Too small to judge; treat as not-Cyrillic and let the ID switch decide.
    if (got < 512) return false;

    size_t cyrillic = 0;
    for (size_t i = 0; i < got; ++i) {
        if (sample[i] >= 0xC0) ++cyrillic;
    }
    const double ratio = static_cast<double>(cyrillic) / static_cast<double>(got);
    acclog::Write("Lang", "tlk CP1251 probe: %zu/%zu bytes >= 0xC0 (%.1f%%)",
                  cyrillic, got, ratio * 100.0);
    return ratio >= 0.20;
}

// Read the LanguageID from <install>/dialog.tlk to determine the engine locale
// the player has actually installed, so combat-anchor matching + Id::* speech
// route to the right table. Defaults to English on any failure or unrecognised
// LanguageID — the most universal fallback for the broader (non-DE) user base.
// A correctly-installed DE copy still detects LanguageID=2 and routes to
// German; only a genuine detection failure or an unsupported locale lands on
// English.
//
// The CP1251 content probe runs *before* the ID switch and overrides it,
// because a Russian tlk can declare any ID (Allard uses 0). That ordering also
// means a future Russian repack on a different ID is picked up for free.
//
// TLK header layout (20 bytes): "TLK " "V3.0" int32_le LanguageID
// uint32_le StringCount uint32_le OffsetToStringEntries.
// Locale IDs (per kdev `LanguageIdToCode`): 0=En 1=Fr 2=De 3=It 4=Es 5=Pl.
// ID 5 is real BioWare Polish, used by the official LEM localisation — the game
// never shipped Polish on Steam or GoG, but the tlk is first-party and declares
// it honestly, so Polish needs no content probe the way Russian does.
acc::strings::Lang DetectLanguageFromTlk() {
    using L = acc::strings::Lang;

    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
    if (n == 0 || n >= sizeof(exePath)) {
        acclog::Write("Lang", "GetModuleFileName failed; defaulting to English");
        return L::En;
    }

    char* slash = strrchr(exePath, '\\');
    if (!slash) {
        acclog::Write("Lang", "exe path has no backslash (%s); defaulting to English", exePath);
        return L::En;
    }
    size_t tailCap = sizeof(exePath) - static_cast<size_t>(slash + 1 - exePath);
    strncpy_s(slash + 1, tailCap, "dialog.tlk", _TRUNCATE);

    FILE* fp = nullptr;
    if (fopen_s(&fp, exePath, "rb") != 0 || !fp) {
        acclog::Write("Lang", "dialog.tlk not readable at %s; defaulting to English", exePath);
        return L::En;
    }

    unsigned char header[20] = {};
    size_t got = fread(header, 1, sizeof(header), fp);

    if (got < sizeof(header) || memcmp(header, "TLK ", 4) != 0) {
        fclose(fp);
        acclog::Write("Lang", "dialog.tlk bad header (%zu bytes); defaulting to English", got);
        return L::En;
    }

    int32_t langId = 0;
    memcpy(&langId, header + 8, sizeof(langId));
    uint32_t entriesOffset = 0;
    memcpy(&entriesOffset, header + 16, sizeof(entriesOffset));

    // Content probe first — it outranks the declared ID. See TlkLooksCyrillic.
    const bool cyrillic = TlkLooksCyrillic(fp, entriesOffset);
    fclose(fp);

    if (cyrillic) {
        acclog::Write("Lang",
                      "dialog.tlk declares LanguageID=%d but reads as CP1251 "
                      "Cyrillic -> Russian",
                      langId);
        return L::Ru;
    }

    L detected;
    switch (langId) {
        case 0: detected = L::En; break;
        case 1: detected = L::Fr; break;
        case 2: detected = L::De; break;
        case 3: detected = L::It; break;
        case 4: detected = L::Es; break;
        case 5: detected = L::Pl; break;
        default:
            acclog::Write("Lang", "unknown LanguageID=%d; defaulting to English", langId);
            return L::En;
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
        // Localised: this is the first thing every player hears on every
        // launch, so it must follow the configured language like all other
        // spoken strings. It used to be a hardcoded English literal.
        char greeting[128];
        snprintf(greeting, sizeof(greeting),
                 acc::strings::Get(acc::strings::Id::FmtModLoadedVersion),
                 acc::kModVersion);
        prism::Speak(greeting, /*interrupt=*/true);
    }
}

// CExoRawInputInternal::InitializeDirectInputMouse guard (vanilla engine
// crash for users with no mouse). The function dereferences
// `this->direct_input_interface` (+0x1c) at +0xa. On internal Acquire
// failure the engine calls ShutDownDirectInput (nulling +0x1c) but still
// returns 1; the next GetMouseState re-enters and AVs on the NULL.
//
// Earlier attempt at a KPatchManager detour at the function entry broke
// the engine's input pipeline initialisation in a way that focus-loss
// then -regain reset — users had to alt-tab after every launch to wake
// the menu. Likely interaction between the wrapper PUSHAD/PUSHFD/CALL
// overhead and DirectInput's foreground-cooperative-level handshake.
//
// Workaround: install a small inline trampoline *after* the engine's
// first successful mouse init has run, from inside OnRulesInit. By the
// time CSWRules constructs, GetMouseState has already done its initial
// device bring-up. For a with-mouse user our trampoline is silent —
// the function is never called again. For a no-mouse user it intercepts
// the SECOND (AV-prone) call and routes to the engine's own return-0
// epilogue at 0x005e401f.
//
// The trampoline runs the engine's prelude (the 5+2 bytes we displace),
// reads direct_input_interface, and on NULL jumps to the fail epilogue;
// on non-NULL it jumps past the prelude to 0x005e3faa to continue.
//
// Crash report: userlogs/swkotor.exe.58504.dmp.
namespace {

const uintptr_t kInitMouseAddr = acc::addr::R(0x005e3fa0);  // function entry
const uintptr_t kInitMouseContinue = acc::addr::R(0x005e3faa);  // MOV ECX,[EAX]
const uintptr_t kInitMouseFailEpilogue = acc::addr::R(0x005e401f); // POP EDI; XOR EAX,EAX; ...

void InstallMouseGuard() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    constexpr size_t kTrampolineSize = 32;
    void* tramp = VirtualAlloc(
        nullptr, kTrampolineSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        acclog::Write("EngineInput",
            "DirectInput mouse guard install failed: VirtualAlloc returned NULL");
        return;
    }

    auto* p = static_cast<uint8_t*>(tramp);
    // Engine's prelude (the bytes we displace with the JMP at +0).
    *p++ = 0x83; *p++ = 0xec; *p++ = 0x14;  // SUB ESP, 0x14
    *p++ = 0x56;                             // PUSH ESI
    *p++ = 0x57;                             // PUSH EDI
    *p++ = 0x8b; *p++ = 0xf9;                // MOV EDI, ECX (this)
    // NULL check on this->direct_input_interface.
    *p++ = 0x8b; *p++ = 0x47; *p++ = 0x1c;  // MOV EAX, [EDI+0x1c]
    *p++ = 0x85; *p++ = 0xc0;                // TEST EAX, EAX
    *p++ = 0x74; *p++ = 0x05;                // JZ +5 (skip continue-jmp, take fail-jmp)
    // Continue path: jump to 0x005e3faa (MOV ECX, [EAX]).
    {
        uintptr_t from = reinterpret_cast<uintptr_t>(p) + 5;
        int32_t rel = static_cast<int32_t>(kInitMouseContinue - from);
        *p++ = 0xe9;
        memcpy(p, &rel, 4); p += 4;
    }
    // Fail path: jump to 0x005e401f (engine's return-0 epilogue).
    {
        uintptr_t from = reinterpret_cast<uintptr_t>(p) + 5;
        int32_t rel = static_cast<int32_t>(kInitMouseFailEpilogue - from);
        *p++ = 0xe9;
        memcpy(p, &rel, 4); p += 4;
    }

    // Patch the function entry with JMP rel32 to the trampoline.
    void* entry = reinterpret_cast<void*>(kInitMouseAddr);
    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        acclog::Write("EngineInput",
            "DirectInput mouse guard install failed: VirtualProtect entry");
        return;
    }
    auto* dst = static_cast<uint8_t*>(entry);
    int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(tramp) - (kInitMouseAddr + 5));
    dst[0] = 0xe9;
    memcpy(&dst[1], &rel, 4);
    DWORD ignored = 0;
    VirtualProtect(entry, 5, oldProtect, &ignored);

    FlushInstructionCache(GetCurrentProcess(), entry, 5);
    FlushInstructionCache(GetCurrentProcess(), tramp, kTrampolineSize);

    acclog::Write("EngineInput",
        "DirectInput mouse guard installed (trampoline at %p)", tramp);
}

}  // namespace

// CSWRules::CSWRules construction detour (hooks.toml @ 0x00552c9a).
// First fire is the "patch alive" signal, Prism-init trigger, and the
// point at which we detect the user's installed language. File I/O is
// safe here (we're well past loader lock by the time CSWRules runs).
extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    if (!acc::game::HandlerEnabled()) return;  // KOTOR 2: not ported yet
    static bool fired = false;
    if (fired) return;
    fired = true;
    acclog::BringupMark("rules_init");
    InstallMouseGuard();
    acc::save_guard::InstallSaveScreenshotGuard();
    // Order matters: pin the narrow-text codepage from the detected language
    // before Prism comes up, so the very first utterance (the version greeting
    // in EnsurePrismInitialized) is already widened correctly. Russian is
    // CP1251 and must not go through CP_ACP — see prism::SetSpeechCodepage.
    {
        const acc::strings::Lang lang = DetectLanguageFromTlk();
        acc::strings::SetLanguage(lang);
        prism::SetSpeechCodepage(acc::strings::CodepageFor(lang));
    }
    EnsurePrismInitialized();
    // Baseline snapshot of swkotor.ini + install-root DLLs so every support
    // bundle from now on carries the user's full config without needing a
    // follow-up "what's in your ini?" round-trip.
    acc::diag::settings::LogStartupSnapshot();
    // Build the engine keybinding table (hardcoded command -> scancode -> VK,
    // resolved against the active keyboard layout) so the input hooks can
    // swallow the engine's bare-key action when a modifier-using mod hotkey
    // shadows an engine-bound key. The shadowed gameplay hotkeys are hardcoded
    // in the engine (not in the rebindable Key Mapping screen), so this is a
    // one-time build per session.
    acc::engine_keymap::Rebuild();
    // Apartment probe — see diag_focus.h. prism.dll's SAPI backend
    // calls CoInitializeEx internally; if it picks MTA on the engine's
    // main thread (where the engine's own message loop + DirectInput
    // dispatch live) that conflicts with anything else on the thread
    // that wants STA, and shows up as "fine until first focus loss".
    acc::focus_guard::LogComApartment("post_prism_init");
    // Spin up the focus-probe polling thread now (rather than at
    // MainMenu first-sight) so we catch focus events during intro-movie
    // playback + the SWMovieWindow → Render Window handoff that
    // happens before the main menu shows. Idempotent — re-call from
    // MainMenu first-sight is a no-op.
    acc::focus_guard::StartFocusProbe();
    // Bringup-phase nag — speaks "Game is still loading" once if the
    // user presses an arrow / Enter / Space during the post-intro
    // pre-pump-live window. Silent during movies + after pump live.
    acc::bringup_announce::Start();
    // NOTE: update_checker::StartBackgroundCheck() used to fire here, but
    // OnRulesInit runs DURING engine bringup — before intro-movie playback
    // completes and before the OpenGL/DirectInput pipeline is settled.
    // Starting WinHTTP I/O here (DNS, WPAD, TLS, thread-pool init,
    // implicit COM apartments) competes with Bink playback for window-
    // foreground / message-loop state and is a leading suspect for
    // intermittent "menu loaded but unresponsive, alt-tab fixes it" /
    // "intro movie plays twice" reports. The kick-off has been moved to
    // the first-sight handler for the MainMenu panel (see menus.cpp
    // AnnouncePanelTitle), at which point the engine is demonstrably
    // past its delicate startup window.
    acclog::Write("Init", "first CSWRules construction; detour active");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        acclog::Init(hinstDLL);
        acclog::BringupMark("dll_attach");
        DWORD n = GetEnvironmentVariableA(
            "KOTOR_VERSION_SHA", g_versionSha, sizeof(g_versionSha));
        if (n == 0 || n >= sizeof(g_versionSha)) {
            strncpy_s(g_versionSha, "(unset)", _TRUNCATE);
        }
        acclog::Write("Init", "DLL_PROCESS_ATTACH sha=%s", g_versionSha);
        // Which game/build, logged beside the SHA because they answer the same
        // question from two directions: the SHA is what KotorPatcher matched,
        // this is what we independently detected from the game's PE header. A
        // disagreement between the two lines is a real bug and worth spotting.
        //
        // Safe under loader lock: detection is a walk of the already-mapped
        // image, with no file I/O and no allocation (see engine_game.h). It is
        // deliberately here rather than in the startup snapshot so it appears
        // even when NO hooks are installed — which is exactly the state the
        // KOTOR 2 bring-up runs in.
        acclog::Write("Game.Identity", "title=%s build=%s",
                      acc::game::TitleName(), acc::game::BuildName());
        // Prism init deferred — loader lock.
    }
    return TRUE;
}
