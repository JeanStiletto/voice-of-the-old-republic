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
// the last tutorial.2da row: shown once at game start, never again, and NOT in
// our keyboard-hint row map, so nothing else collides with it.
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
uint32_t    s_pendingStrref = 0;
const char* s_pendingHint   = nullptr;
bool        s_active        = false;   // synthetic popup on screen
const char* s_activeHint    = nullptr;
bool        s_paused        = false;   // we issued the pause

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

void RecordPendingHint(uint32_t strref, const char* hint) {
    if (!hint || !hint[0]) return;
    s_pendingStrref = strref;
    s_pendingHint   = hint;
    acclog::Write("TutorialPopup", "pending recorded: strref=%u hint=\"%.80s\"",
                  strref, hint);
}

void FirePendingAtReplyBreak() {
    if (!s_pendingHint) return;
    if (s_active) return;  // a popup is already up; don't stack
    uint32_t strref = s_pendingStrref;
    const char* hint = s_pendingHint;
    s_pendingHint   = nullptr;
    s_pendingStrref = 0;
    FirePopup(strref, hint);
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
