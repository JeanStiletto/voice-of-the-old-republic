#include "tutorial_popup.h"

#include <windows.h>
#include <cstring>

#include "engine_app.h"      // GetServerApp — SetPauseState `this`
#include "engine_panels.h"   // ResolveGuiInGame, IdentifyPanel, PanelKind
#include "log.h"
#include "engine_rebase.h"
#include "engine_offsets_select.h"
#include "engine_game.h"     // IsKotor2 — SetSoundMode signature split

namespace acc::tutorial_popup {

namespace {

// ---- Engine addresses / offsets (RE-confirmed; see trask_popup_recipe.md) ----

// CGuiInGame::ShowTutorialWindow @0x0062f4a0 — mounts tutorial_box for a reason.
// Does NOT check field45_0xb4 (the funnel does), so it works mid-dialogue. Does
// NOT populate the message; we set it ourselves.
// KOTOR 2: 0x007cfb10, the same direct mount — checks the Tutorial-Popups
// gameplay option and the once-shown bit (returns 0 if either refuses, which
// is why clearing the bit first stays mandatory), sets the bit, and AddPanels
// the box at [gui+0xb0]. Crucially it does NOT check the funnel's in-dialogue
// busy flag ([gui+0xcc], checked only by the will-show gate 0x007cf960), so
// it mounts mid-dialogue like the K1 twin. Also like K1 it does not set the
// reason/message. p2..p4 are position/flag defaults: the engine's own idle
// caller (0x0079b700) passes 0x7f000000, 0x7f000000, 0 — mirrored below.
using PFN_ShowTutorial =
    void(__thiscall*)(void* gui, int reason, uint32_t p2, uint32_t p3, uint32_t p4);
const uintptr_t kAddrShowTutorialWindow = acc::addr::Pick(0x0062f4a0, 0x007cfb10);
// The engine's "use the default" sentinel for the mount's p2/p3 on KOTOR 2;
// KOTOR 1 shipped (and was tested) with plain zeros.
constexpr uint32_t kMountDefaultK2 = 0x7f000000u;

// CSWGuiMessageBox::SetMessage(strref) @0x006249d0 — resolves the strref via the
// TLK and sets the box text. (The CExoString overload @0x006271a0 destroys its
// arg, so we use the strref one.)
using PFN_SetMessageStrref = void(__thiscall*)(void* box, uint32_t strref);
const uintptr_t kAddrSetMessageStrref = acc::addr::Pick(0x006249d0, 0x0040FFB0);

// CSWGuiTutorialBox::SetTutorialReason @0x006aa900 — configures the box as a
// TUTORIAL (single "Weiter"/OK prompt, page state) for a row, the way the funnel
// does. Without it the box renders as a generic two-button message box.
// KOTOR 2: 0x00898f60 — writes the row byte (+0x9d8), zeroes the page counter
// (+0x9d9), loads the first "Message_PC%i" page and installs the per-reason OK
// callback; the K2 funnel 0x0079e9d0 reaches it via the thin CGuiInGame
// wrapper 0x007cf9e0.
using PFN_SetTutorialReason = void(__thiscall*)(void* box, int reason);
const uintptr_t kAddrSetTutorialReason = acc::addr::Pick(0x006aa900, 0x00898f60);

// CGuiInGame member: tutorial_box pointer @+0xa0. KOTOR 2: +0xb0, same
// witness as engine_panels' slot table (creator 0x007be4c0 stores the
// CSWGuiTutorialBox ctor 0x00898ca0 result there).
const size_t kGuiTutorialBoxOffset = acc::off::Pick(0xa0, 0xb0);
// CSWGuiTutorialBox source-row byte @+0x994 (we set it to our reason so the
// fingerprint's row lookup lands on an unmapped row; the synthetic flag wins
// regardless, but keeps things tidy). KOTOR 2: +0x9d8 (SetTutorialReason
// writes it, SetNextMessage 0x00899630 reads it back — see tutorial_hints).
const size_t kTutorialBoxRowOffset = acc::off::Pick(0x994, 0x9d8);
// Tutorial once-shown bitfield: byte at CGuiInGame + base + (reason>>3),
// bit (reason & 7). Clear before mounting so it fires every time.
// KOTOR 1 base 0xba8; KOTOR 2 base 0xf88 (bit test 0x007cfa10 reads
// [this + 0xf88 + (reason>>3)] with the same bit scheme).
const size_t kShownBitsBase = acc::off::Pick(0xba8, 0xf88);

// Fixed reason we repurpose for our on-demand popup. 0x2a = 42 = Movement_Keys
// in BOTH games' tutorial.2da (KOTOR 1's last row; KOTOR 2 appends more rows
// after it but keeps 42's label): shown once at game start, never again. Row
// 42 IS mapped to a keyboard hint (the game-start walking popup), but every
// announce path checks SyntheticActive() first and speaks SyntheticHint() when
// we own the box, so the row-keyed hint never fires for our synthetic popups.
constexpr int kSyntheticReason = 0x2a;

// ---- Pause (mirrors engine_subscreen; kept self-contained here — keep
// the three values in sync with engine_subscreen.cpp's copies) ----
using PFN_SetPauseState =
    void(__thiscall*)(void* server, int source_bit, unsigned long on_off);
const uintptr_t kAddrSetPauseState = acc::addr::Pick(0x004ae9a0, 0x0051C760);
// CAUTION: signatures differ per game — K1 internal takes (mode), K2's
// facade takes (mode, flag) with flag 0. See engine_subscreen for the
// witnesses.
using PFN_SetSoundMode = void(__thiscall*)(void* self, int mode);
using PFN_SetSoundModeK2 = void(__thiscall*)(void* self, int mode, int flag);
const uintptr_t kAddrSetSoundMode = acc::addr::Pick(0x005d5e80, 0x0070BC60);
const uintptr_t kAddrExoSoundPtr    = acc::addr::PickGlobal(0x007a39ec, 0x00A1B494);
constexpr unsigned char kPauseBitMenu   = 0x02;

// ---- State ----
// Trask often delivers several rewritten tutorial lines back-to-back before a
// reply break; accumulate their hints (newline-joined) so the popup carries all
// of them, not just the last. Buffers are generous so long combined text is not
// truncated anywhere in the speak path.
char        s_pendingHints[4096] = {0};  // accumulated hints since last break
uint32_t    s_pendingStrref = 0;         // first accumulated line's strref (visible text)
int         s_pendingCount  = 0;
bool        s_active        = false;     // synthetic popup on screen
char        s_activeHintBuf[4096] = {0}; // the combined hint spoken while active
const char* s_activeHint    = nullptr;   // -> s_activeHintBuf while active
bool        s_paused        = false;     // we issued the pause

void SetPause(bool on) {
    void* server = acc::engine::GetServerApp();
    if (!server) return;
    __try {
        reinterpret_cast<PFN_SetPauseState>(kAddrSetPauseState)(
            server, kPauseBitMenu, on ? 1u : 0u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("TutorialPopup", "SEH in SetPause(%d)", on ? 1 : 0);
    }
    if (!on) {
        // Audio resync on unpause (un-mute the mixer), same as the modal-close path.
        __try {
            void* exo = *reinterpret_cast<void**>(kAddrExoSoundPtr);
            if (exo) {
                if (acc::game::IsKotor2()) {
                    reinterpret_cast<PFN_SetSoundModeK2>(kAddrSetSoundMode)(exo, 0, 0);
                } else {
                    reinterpret_cast<PFN_SetSoundMode>(kAddrSetSoundMode)(exo, 0);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("TutorialPopup", "SEH in SetSoundMode(0)");
        }
    }
}

// Is a TutorialBox panel currently in the GUI manager's stack?
bool TutorialBoxPresent() {
    return acc::engine::FindPanelByKind(acc::engine::PanelKind::TutorialBox)
           != nullptr;
}

void FirePopup(uint32_t strref, const char* hint) {
    void* gui = acc::engine::ResolveGuiInGame();
    if (!gui) {
        acclog::Write("TutorialPopup", "fire skipped: ResolveGuiInGame null");
        return;
    }
    // Mark active BEFORE the mount: ShowTutorialWindow / SetMessage synchronously
    // fire the message-listbox SetActive echoes, and the suppression + gates key
    // on SyntheticActive — set it first so the raw mouse text isn't spoken.
    s_active     = true;
    s_activeHint = hint;
    __try {
        auto* g = reinterpret_cast<unsigned char*>(gui);
        // Re-arm: clear the once-shown bit for our reason.
        uint8_t* bits = reinterpret_cast<uint8_t*>(
            g + kShownBitsBase + (kSyntheticReason >> 3));
        *bits &= static_cast<uint8_t>(~(1u << (kSyntheticReason & 7)));
        // Mount the tutorial box.
        const uint32_t p23 = acc::game::IsKotor2() ? kMountDefaultK2 : 0u;
        reinterpret_cast<PFN_ShowTutorial>(kAddrShowTutorialWindow)(
            gui, kSyntheticReason, p23, p23, 0);
        // Point the box at our reason row and set its visible text to the
        // line's original (mouse-worded) strref.
        void* box = *reinterpret_cast<void**>(g + kGuiTutorialBoxOffset);
        if (box) {
            // Configure as a real tutorial (single Weiter/OK prompt) for our
            // reason, then override the visible text with the line's own strref.
            reinterpret_cast<PFN_SetTutorialReason>(kAddrSetTutorialReason)(
                box, kSyntheticReason);
            *reinterpret_cast<uint8_t*>(
                reinterpret_cast<unsigned char*>(box) + kTutorialBoxRowOffset) =
                static_cast<uint8_t>(kSyntheticReason);
            if (strref)
                reinterpret_cast<PFN_SetMessageStrref>(kAddrSetMessageStrref)(box, strref);
        }
        acclog::Write("TutorialPopup",
                      "fired: reason=%d strref=%u box=%p hint=\"%.80s\"",
                      kSyntheticReason, strref, box, hint ? hint : "");
        SetPause(true);
        s_paused = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("TutorialPopup", "SEH fault firing popup");
        s_active = false;  // don't leave the gates stuck on after a failed mount
    }
}

}  // namespace

// True if `hint` is already one of the newline-separated segments accumulated
// this break. Two adjacent Trask lines can carry the same keyboard hint (e.g.
// the camera line and the footlocker line share one object-navigation hint);
// when they land in the same popup we speak it once, not twice.
static bool PendingContainsHint(const char* hint) {
    size_t hlen = std::strlen(hint);
    const char* p = s_pendingHints;
    while (*p) {
        const char* nl = std::strchr(p, '\n');
        size_t seglen = nl ? static_cast<size_t>(nl - p) : std::strlen(p);
        if (seglen == hlen && std::memcmp(p, hint, hlen) == 0) return true;
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

void RecordPendingHint(uint32_t strref, const char* hint) {
    if (!hint || !hint[0]) return;
    if (s_pendingCount > 0 && PendingContainsHint(hint)) {
        acclog::Write("TutorialPopup",
                      "pending dup skipped: strref=%u hint=\"%.60s\"", strref, hint);
        return;
    }
    if (s_pendingCount == 0) {
        s_pendingStrref  = strref;   // first line drives the popup's visible text
        s_pendingHints[0] = '\0';
    } else {
        // Newline between hints so the screen reader pauses between concepts.
        strncat_s(s_pendingHints, sizeof(s_pendingHints), "\n", _TRUNCATE);
    }
    strncat_s(s_pendingHints, sizeof(s_pendingHints), hint, _TRUNCATE);
    ++s_pendingCount;
    acclog::Write("TutorialPopup", "pending #%d recorded: strref=%u hint=\"%.80s\"",
                  s_pendingCount, strref, hint);
}

bool FirePendingAtReplyBreak() {
    if (s_pendingCount == 0) return false;
    if (s_active) return false;  // a popup is already up; don't stack
    // Move the accumulated hints into the active buffer (kept alive for the
    // popup's lifetime; the speak paths read SyntheticHint()).
    strncpy_s(s_activeHintBuf, sizeof(s_activeHintBuf), s_pendingHints, _TRUNCATE);
    uint32_t strref = s_pendingStrref;
    int count = s_pendingCount;
    s_pendingHints[0] = '\0';
    s_pendingStrref   = 0;
    s_pendingCount    = 0;
    acclog::Write("TutorialPopup", "firing %d accumulated hint(s)", count);
    FirePopup(strref, s_activeHintBuf);
    return true;
}

bool SyntheticActive()      { return s_active; }
const char* SyntheticHint() { return s_activeHint; }

void PollDismiss() {
    if (!s_active) return;
    if (TutorialBoxPresent()) return;
    // The box closed (Weiter/OK). Clear state and unpause.
    s_active     = false;
    s_activeHint = nullptr;
    acclog::Write("TutorialPopup", "dismissed (box gone)");
    if (s_paused) {
        SetPause(false);
        s_paused = false;
    }
}

}  // namespace acc::tutorial_popup
