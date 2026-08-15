// DLL entry + lazy-init plumbing.
//
// DllMain runs under the loader lock — no LoadLibrary, no COM init, no
// file I/O here. Prism is initialised lazily on the first hook fire;
// that same once-only init detects the user's installed language from
// dialog.tlk and selects the matching strings + combat-anchor tables
// (and speech codepage) before the first utterance — on both games.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "bringup_announce.h"
#include "focus_guard.h"
#include "diag_settings.h"
#include "engine_game.h"
#include "engine_input.h"
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
    // Detect + pin the language before the first utterance. This lives here —
    // the single once-only funnel every speech surface passes through — rather
    // than in OnRulesInit, because OnRulesInit is a KOTOR-1-only hook: KOTOR 2
    // has no rules-init detour, so detection parked there left K2 on the
    // compiled-in German default for every user. Order matters: the narrow-text
    // codepage must be pinned from the detected language before the greeting
    // below, so the very first utterance is already widened correctly. Russian
    // is CP1251 and Polish CP1250 — neither may go through CP_ACP; see
    // prism::SetSpeechCodepage. File I/O is safe at every caller (all speech
    // surfaces fire well past loader lock, same argument as prism::Init's
    // LoadLibrary).
    {
        const acc::strings::Lang lang = DetectLanguageFromTlk();
        acc::strings::SetLanguage(lang);
        prism::SetSpeechCodepage(acc::strings::CodepageFor(lang));
    }
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

// Everything the patch has to stand up once per session, in the order that
// matters. Idempotent and game-agnostic.
//
// It lives in its own function because the two games reach it from different
// places. KOTOR 1 calls it from OnRulesInit, the CSWRules constructor detour.
// KOTOR 2 has no rules-init hook (there is no such entry in
// kotor2.hooks.toml), so it calls it from the first engine tick — see
// core_tick.cpp. Before that call existed, every line below was KOTOR 1 only,
// and several of them are subsystems that KOTOR 2 code *consumes* without ever
// having started: the focus probe is the producer of every
// RequestInputReacquire / RequestInputRelease, so its absence made the tick's
// DrainPendingReacquire a permanent no-op on KOTOR 2 and left the focus-theft
// recovery missing there even after its SetActive constants were resolved.
//
// Everything here is past the loader lock at both call sites, which the file
// I/O and the two worker threads below require.
void EnsureSessionBringup() {
    static bool done = false;
    if (done) return;
    done = true;

    // No-mouse DirectInput crash guard. On KOTOR 2 this has already run from
    // DllMain (the crash can land before the first tick); the call is
    // idempotent, and on KOTOR 1 this is where it installs — after the
    // engine's first successful mouse init, the timing the shipped fix was
    // validated against.
    acc::engine::InstallDirectInputMouseGuard();
    // Keep a failed mouse device from taking the keyboard with it. KOTOR 1
    // installs it HERE rather than from DllMain (where KOTOR 2 does) for the
    // same reason nothing else patches KOTOR 1's .text that early: the Steam
    // build is packed, and at DllMain — which runs from a static import, before
    // the entry point — the bytes we scan for are still encrypted, so the scan
    // would match nothing. Idempotent, so the KOTOR 2 call below is not undone
    // by this one.
    acc::engine::InstallMouseTeardownBlock();
    // Save-thumbnail divide-by-zero guard. Declines on KOTOR 2 by design —
    // see the note at its install site.
    acc::save_guard::InstallSaveScreenshotGuard();
    // Language detection + codepage pinning happen inside
    // EnsurePrismInitialized, before its greeting.
    EnsurePrismInitialized();
    // Baseline snapshot of the game ini + install-root DLLs so every support
    // bundle from now on carries the user's full config without needing a
    // follow-up "what's in your ini?" round-trip. Picks swkotor2.ini on
    // KOTOR 2 — it was already per-game aware, it was simply never called
    // there, which is why KOTOR 2 support bundles carried no config at all.
    acc::diag::settings::LogStartupSnapshot();
    // Build the engine keybinding table (hardcoded command -> scancode -> VK,
    // resolved against the active keyboard layout) so the input hooks can
    // swallow the engine's bare-key action when a modifier-using mod hotkey
    // shadows an engine-bound key. The shadowed gameplay hotkeys are hardcoded
    // in the engine (not in the rebindable Key Mapping screen), so this is a
    // one-time build per session. VksForCode self-heals via a lazy Rebuild if
    // it is asked first, so this call is about doing it once, up front, with
    // the table dumped to the log where support can read it.
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
    // pre-pump-live window. Silent during movies + after pump live. Its phase
    // machine keys on the "SWMovieWindow" and "Render Window" class names,
    // both of which are present in the KOTOR 2 binary.
    acc::bringup_announce::Start();
    // NOTE: update_checker::StartBackgroundCheck() deliberately does NOT fire
    // here. This runs DURING engine bringup — before intro-movie playback
    // completes and before the OpenGL/DirectInput pipeline is settled.
    // Starting WinHTTP I/O here (DNS, WPAD, TLS, thread-pool init,
    // implicit COM apartments) competes with Bink playback for window-
    // foreground / message-loop state and is a leading suspect for
    // intermittent "menu loaded but unresponsive, alt-tab fixes it" /
    // "intro movie plays twice" reports. KOTOR 1 kicks it off from the
    // MainMenu first-sight handler (menus.cpp AnnouncePanelTitle); KOTOR 2's
    // first-sight path is a different handler, so it uses the equivalent
    // past-the-startup-window signal from the tick — see core_tick.cpp.
}

// CSWRules::CSWRules construction detour (hooks.toml @ 0x00552c9a).
// First fire is the "patch alive" signal and Prism-init trigger (which
// also detects the user's installed language). File I/O is safe here
// (we're well past loader lock by the time CSWRules runs).
extern "C" void __cdecl OnRulesInit(void* /*rulesThis*/) {
    // KOTOR 2 never reaches this: it has no rules-init hook to install. The
    // gate stays as a belt-and-braces assertion of that.
    if (!acc::game::HandlerEnabled()) return;
    static bool fired = false;
    if (fired) return;
    fired = true;
    acclog::BringupMark("rules_init");
    EnsureSessionBringup();
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
        // No-mouse crash guard, KOTOR 2 only. It has to go here for the same
        // reason the identity line above does: KOTOR 2 has no rules-init hook,
        // and this must be in place even in a session where no hook ever fires.
        // The guard cannot be deferred to the first hook that does fire — the
        // crash lands on the first main-loop frame that updates the mouse,
        // which on a no-mouse machine can precede any menu panel we hook.
        //
        // Loader-lock safe: one VirtualAlloc, one VirtualProtect, a memcpy and
        // a FlushInstructionCache. No LoadLibrary, no COM, nothing re-entrant.
        //
        // Ordering is a non-issue. The trampoline only diverges when the
        // engine's DirectInput interface is already NULL, which is a state the
        // engine reaches on its own and never recovers from — so installing
        // before the engine's own DirectInput bring-up is harmless: a
        // with-mouse player's interface is non-NULL and the guard falls
        // straight through to the original code.
        if (acc::game::IsKotor2()) {
            acc::engine::InstallDirectInputMouseGuard();
            // Same reasoning, one step upstream: the guard keeps a torn-down
            // DirectInput from crashing us, this keeps the mouse from tearing
            // it down at all. It has to be as early as the guard, because the
            // teardown it blocks happens on the same first mouse-polling frame
            // the crash used to. KOTOR 2's image is not packed, so the
            // verification scan reads real bytes here.
            acc::engine::InstallMouseTeardownBlock();
        }
        // Prism init deferred — loader lock.
    }
    return TRUE;
}
