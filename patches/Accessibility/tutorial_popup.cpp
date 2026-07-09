#include "tutorial_popup.h"

#include <windows.h>
#include <cstring>

#include "engine_manager.h"  // kAddrGuiManagerPtr, kMgrPanels*
#include "engine_panels.h"   // ResolveGuiInGame, IdentifyPanel, PanelKind
#include "log.h"

namespace acc::tutorial_popup {

namespace {

// ---- Engine addresses / offsets (RE-confirmed; see trask_popup_recipe.md) ----

// CGuiInGame::ShowTutorialWindow @0x0062f4a0 — mounts tutorial_box for a reason.
// Does NOT check field45_0xb4 (the funnel does), so it works mid-dialogue. Does
// NOT populate the message; we set it ourselves.
using PFN_ShowTutorial =
    void(__thiscall*)(void* gui, int reason, uint32_t p2, uint32_t p3, uint32_t p4);
constexpr uintptr_t kAddrShowTutorialWindow = 0x0062f4a0;

// CSWGuiMessageBox::SetMessage(strref) @0x006249d0 — resolves the strref via the
// TLK and sets the box text. (The CExoString overload @0x006271a0 destroys its
// arg, so we use the strref one.)
using PFN_SetMessageStrref = void(__thiscall*)(void* box, uint32_t strref);
constexpr uintptr_t kAddrSetMessageStrref = 0x006249d0;

// CSWGuiTutorialBox::SetTutorialReason @0x006aa900 — configures the box as a
// TUTORIAL (single "Weiter"/OK prompt, page state) for a row, the way the funnel
// does. Without it the box renders as a generic two-button message box.
using PFN_SetTutorialReason = void(__thiscall*)(void* box, int reason);
constexpr uintptr_t kAddrSetTutorialReason = 0x006aa900;

// CGuiInGame member: tutorial_box pointer @+0xa0.
constexpr size_t kGuiTutorialBoxOffset = 0xa0;
// CSWGuiTutorialBox source-row byte @+0x994 (we set it to our reason so the
// fingerprint's row lookup lands on an unmapped row; the synthetic flag wins
// regardless, but keeps things tidy).
constexpr size_t kTutorialBoxRowOffset = 0x994;
// Tutorial once-shown bitfield: byte at CGuiInGame + 0xba8 + (reason>>3),
// bit (reason & 7). Clear before mounting so it fires every time.
constexpr size_t kShownBitsBase = 0xba8;

// Fixed reason we repurpose for our on-demand popup. 0x2a = 42 = Movement_Keys,
// the last tutorial.2da row: shown once at game start, never again. Row 42 IS
// mapped to a keyboard hint (the game-start walking popup), but every announce
// path checks SyntheticActive() first and speaks SyntheticHint() when we own the
// box, so the row-keyed hint never fires for our synthetic popups.
constexpr int kSyntheticReason = 0x2a;

// ---- Pause (mirrors engine_subscreen; kept self-contained here) ----
using PFN_SetPauseState =
    void(__thiscall*)(void* server, int source_bit, unsigned long on_off);
constexpr uintptr_t kAddrSetPauseState  = 0x004ae9a0;
using PFN_SetSoundMode = void(__thiscall*)(void* self, int mode);
constexpr uintptr_t kAddrSetSoundMode   = 0x005d5e80;
constexpr uintptr_t kAddrAppManagerPtr  = 0x007A39FC;
constexpr size_t    kAppManagerServerOff = 0x08;
constexpr uintptr_t kAddrExoSoundPtr    = 0x007a39ec;
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
    __try {
        void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appMgr) return;
        void* server = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) + kAppManagerServerOff);
        if (!server) return;
        reinterpret_cast<PFN_SetPauseState>(kAddrSetPauseState)(
            server, kPauseBitMenu, on ? 1u : 0u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("TutorialPopup", "SEH in SetPause(%d)", on ? 1 : 0);
    }
    if (!on) {
        // Audio resync on unpause (un-mute the mixer), same as the modal-close path.
        __try {
            void* exo = *reinterpret_cast<void**>(kAddrExoSoundPtr);
            if (exo) reinterpret_cast<PFN_SetSoundMode>(kAddrSetSoundMode)(exo, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("TutorialPopup", "SEH in SetSoundMode(0)");
        }
    }
}

// Is a TutorialBox panel currently in the GUI manager's stack?
bool TutorialBoxPresent() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   count = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** data = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!data || count <= 0) return false;
    int n = count > 16 ? 16 : count;
    for (int i = 0; i < n; ++i) {
        void* p = data[i];
        if (p && acc::engine::IdentifyPanel(p) == acc::engine::PanelKind::TutorialBox)
            return true;
    }
    return false;
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
        reinterpret_cast<PFN_ShowTutorial>(kAddrShowTutorialWindow)(
            gui, kSyntheticReason, 0, 0, 0);
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
bool PendingContainsHint(const char* hint) {
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
