// Tastaturbelegung screen — see menus_keymap.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "menus_keymap.h"

#include "engine_input.h"
#include "engine_manager.h"      // IsPanelInManager
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"        // ReadButtonText
#include "log.h"
#include "menus_chain.h"         // RebindChain (refresh after list repopulates)
#include "menus_extract.h"       // FromControl ("{action}: {key}" readout)
#include "menus_internal.h"      // DriveListBoxSelection / ListBoxNav*
#include "menus_pending.h"       // QueueActivate (click-sim for OK/Cancel/Default)
#include "prism.h"
#include "strings.h"

using acc::menus::detail::DriveListBoxSelection;
using acc::menus::detail::ListBoxNavOp;
using acc::menus::detail::ListBoxNavResult;

namespace acc::menus::keymap {

namespace {

// Engine entry points (decompiled, build/re/optkeymappings-keymapbutton).
constexpr uintptr_t kAddrSetCaptureEvent = 0x006ed480;  // (panel, row) arm capture
constexpr uintptr_t kAddrOnFilterMove    = 0x006ed390;  // (panel) → MOVEMENT
constexpr uintptr_t kAddrOnFilterGame    = 0x006ed3e0;  // (panel) → GAME
constexpr uintptr_t kAddrOnFilterMini    = 0x006ed430;  // (panel) → MINIGAME

// CSWGuiInGameOptKeyMappings: field11_0xf2c == 1 while a capture is armed.
constexpr size_t kCaptureActiveOff = 0xf2c;

// Stable .gui control IDs (optkeymapping.gui; locale-independent like the
// equip-slot IDs). Verified in the PanelProbe dump.
constexpr int kIdListBox      = 0;   // LST_EventList
constexpr int kIdDefaultBtn   = 2;   // BTN_Default ("Standard")
constexpr int kIdAcceptBtn    = 3;   // BTN_Accept  ("OK")
constexpr int kIdCancelBtn    = 4;   // BTN_Cancel  ("Abbrechen")
constexpr int kIdFilterMove   = 6;   // BTN_Filter_Move ("Bewegung")
constexpr int kIdFilterGame   = 7;   // BTN_Filter_Game ("Spiel")
constexpr int kIdFilterMini   = 8;   // BTN_Filter_Mini ("Minigames")

// Tab-level entry list, in announced order: the three category tabs, then the
// three dialog buttons (so the confirm/discard/reset actions live in the same
// flat list the user arrows through — there is no separate button row).
enum class EntryKind { Category, Button };
struct TabEntry {
    EntryKind kind;
    int       guiId;       // control to read/activate
    uintptr_t filterFn;    // Category only: OnFilter* to call (else 0)
};
const TabEntry kTabEntries[] = {
    { EntryKind::Category, kIdFilterMove, kAddrOnFilterMove },
    { EntryKind::Category, kIdFilterGame, kAddrOnFilterGame },
    { EntryKind::Category, kIdFilterMini, kAddrOnFilterMini },
    { EntryKind::Button,   kIdAcceptBtn,  0 },
    { EntryKind::Button,   kIdCancelBtn,  0 },
    { EntryKind::Button,   kIdDefaultBtn, 0 },
};
constexpr int kTabEntryCount = sizeof(kTabEntries) / sizeof(kTabEntries[0]);

// Two-level drill state + the row we armed for capture (for the completion
// re-announce in Tick). Reset on a fresh panel open.
bool  s_drilled        = false;
void* s_panel          = nullptr;
int   s_tabCursor      = 0;
void* s_armedRow       = nullptr;
void* s_armedPanel     = nullptr;

void* FindByGuiId(void* panel, int wantId) {
    if (!panel) return nullptr;
    __try {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
        if (!list || !list->data) return nullptr;
        int n = list->size > 64 ? 64 : list->size;
        for (int i = 0; i < n; ++i) {
            void* c = list->data[i];
            if (!c) continue;
            int id = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
            if (id == wantId) return c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

void* SelectedRow(void* panel) {
    void* row = nullptr;
    __try {
        void* lb = FindByGuiId(panel, kIdListBox);
        if (!lb) return nullptr;
        auto* base = reinterpret_cast<unsigned char*>(lb);
        short sel = *reinterpret_cast<short*>(base + kListBoxSelectionIndexOffset);
        auto* rows = reinterpret_cast<CExoArrayList*>(base + kListBoxControlsOffset);
        if (rows && rows->data && sel >= 0 && sel < rows->size) {
            row = rows->data[sel];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        row = nullptr;
    }
    return row;
}

int CaptureActive(void* panel) {
    int v = 0;
    __try {
        v = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(panel) + kCaptureActiveOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

// Speak a control's text (entry name or "{action}: {key}" row readout).
void SpeakControl(void* panel, void* ctrl) {
    if (!ctrl) return;
    char buf[256];
    if (acc::menus::extract::FromControl(ctrl, buf, sizeof(buf), panel) &&
        buf[0] != '\0') {
        prism::Speak(buf, /*interrupt=*/true);
    }
}

void AnnounceTabEntry(void* panel, int idx) {
    if (idx < 0 || idx >= kTabEntryCount) return;
    SpeakControl(panel, FindByGuiId(panel, kTabEntries[idx].guiId));
    acclog::Write("Menus.KeyMap", "tab-level idx=%d id=%d", idx,
                  kTabEntries[idx].guiId);
}

void AnnounceSelectedRow(void* panel) {
    SpeakControl(panel, SelectedRow(panel));
}

// Switch the displayed category by calling OnFilter* directly (synchronous, so
// the listbox is repopulated before we browse it).
//
// CALLING CONVENTION: OnFilterMove/Game/Mini are AddEvent(0x27) click handlers.
// Despite Ghidra labelling them `(this)`-only, their SARIF BYTES_PURGED is 4 —
// they `ret 4` (the dispatcher passes the clicked control). The typedef MUST
// carry that 4-byte arg (pass a dummy) or the callee pops OUR return address
// and the frame is corrupted → crash. Same trap as menus_abilities::SwitchToTab.
void SwitchCategory(void* panel, uintptr_t filterFn) {
    typedef void(__thiscall* PFN)(void* panel, int dummy);
    __try {
        reinterpret_cast<PFN>(filterFn)(panel, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.KeyMap", "SwitchCategory SEH fn=0x%x", (unsigned)filterFn);
    }
}

void ArmCapture(void* panel, void* row) {
    typedef void(__thiscall* PFN)(void* panel, void* row);
    __try {
        reinterpret_cast<PFN>(kAddrSetCaptureEvent)(panel, row);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.KeyMap", "SetCaptureEvent SEH row=%p", row);
    }
}

}  // namespace

bool IsKeyMapPanel(void* panel) {
    return panel &&
           acc::engine::IdentifyPanel(panel) ==
               acc::engine::PanelKind::KeyboardMapping;
}

void Tick() {
    if (!s_armedPanel) return;
    // Panel gone (closed) — drop the pending re-announce.
    if (!acc::engine::IsPanelInManager(s_armedPanel)) {
        s_armedPanel = nullptr;
        s_armedRow   = nullptr;
        return;
    }
    // Capture still armed — keep waiting.
    if (CaptureActive(s_armedPanel) == 1) return;
    // Capture finished: the engine applied (or rejected) the key. Re-read the
    // armed row's "{action}: {key}" so the user hears the result.
    void* row   = s_armedRow;
    void* panel = s_armedPanel;
    s_armedRow   = nullptr;
    s_armedPanel = nullptr;
    SpeakControl(panel, row);
    acclog::Write("Menus.KeyMap", "capture done; re-announced row=%p", row);
}

bool HandleInput(void* activePanel, int param_1, int param_2, int& outRv) {
    if (!IsKeyMapPanel(activePanel)) return false;

    // Fresh open → start at the tab level on the first entry.
    if (activePanel != s_panel) {
        s_panel     = activePanel;
        s_drilled   = false;
        s_tabCursor = 0;
    }

    // While a capture is armed, hand EVERY key to the engine: it grabs the next
    // keypress (the new binding) and treats Esc as cancel. Consuming arrows /
    // Enter here would make those keys unbindable. Own the event so the generic
    // chain handlers can't steal it; rv=0 = not consumed.
    if (CaptureActive(activePanel) == 1) { outRv = 0; return true; }

    if (param_2 == 0) return false;  // press edge only

    const bool isUp    = (param_1 == kInputNavUp);
    const bool isDown  = (param_1 == kInputNavDown);
    const bool isHome  = (param_1 == kInputHome);
    const bool isEnd   = (param_1 == kInputEnd);
    const bool isEnter = (param_1 == kInputEnter1 || param_1 == kInputEnter2);
    const bool isEsc   = (param_1 == kInputEsc1 || param_1 == kInputEsc2);

    if (!s_drilled) {
        // ---- Tab level: arrow across the flat [tabs + OK/Cancel/Default] list.
        if (isUp || isDown || isHome || isEnd) {
            int ni = s_tabCursor;
            if      (isUp)   ni = s_tabCursor - 1;
            else if (isDown) ni = s_tabCursor + 1;
            else if (isHome) ni = 0;
            else             ni = kTabEntryCount - 1;
            if (ni < 0) ni = 0;
            if (ni >= kTabEntryCount) ni = kTabEntryCount - 1;
            s_tabCursor = ni;
            AnnounceTabEntry(activePanel, s_tabCursor);
            outRv = 1;
            return true;
        }
        if (isEnter) {
            const TabEntry& e = kTabEntries[s_tabCursor];
            if (e.kind == EntryKind::Category) {
                SwitchCategory(activePanel, e.filterFn);
                // OnFilter* repopulates the listbox (frees the old row
                // controls). The generic chain still holds those freed
                // pointers, and MonitorFocusedControl derefs g_chain per tick —
                // so refresh the chain immediately or the next tick faults on a
                // dangling row. (This is the step the standalone build had that
                // kept the earlier inline version from crashing.)
                acc::menus::chain::RebindChain(activePanel);
                s_drilled = true;
                ListBoxNavResult r;
                void* lb = FindByGuiId(activePanel, kIdListBox);
                if (lb) DriveListBoxSelection(lb, ListBoxNavOp::JumpFirst, 0, r);
                AnnounceSelectedRow(activePanel);
                acclog::Write("Menus.KeyMap", "drill into category id=%d", e.guiId);
            } else {
                // OK / Abbrechen / Standard — activate via the engine's own
                // click path (deferred click-sim, like every other button
                // activation in the mod). OK/Cancel pop the panel; Standard
                // resets and stays, so re-announce the current tab after.
                void* btn = FindByGuiId(activePanel, e.guiId);
                if (btn) acc::menus::pending::QueueActivate(btn);
                acclog::Write("Menus.KeyMap", "activate button id=%d", e.guiId);
                if (e.guiId == kIdDefaultBtn) {
                    AnnounceTabEntry(activePanel, s_tabCursor);
                } else {
                    s_panel = nullptr;  // closing — re-init on next open
                }
            }
            outRv = 1;
            return true;
        }
        // Esc / others fall through (engine closes the screen).
        return false;
    }

    // ---- List level: browse bindings, Enter arms a rebind, Esc → tab level.
    if (isUp || isDown || isHome || isEnd) {
        ListBoxNavOp op = isUp   ? ListBoxNavOp::StepUp
                        : isDown ? ListBoxNavOp::StepDown
                        : isHome ? ListBoxNavOp::JumpFirst
                                 : ListBoxNavOp::JumpLast;
        void* lb = FindByGuiId(activePanel, kIdListBox);
        ListBoxNavResult r;
        if (lb && DriveListBoxSelection(lb, op, 0, r)) {
            SpeakControl(activePanel, r.row);
        }
        outRv = 1;
        return true;
    }
    if (isEnter) {
        void* row = SelectedRow(activePanel);
        if (!row) { outRv = 1; return true; }
        int fixed = 0;
        __try {
            fixed = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(row) + kKeyMapButtonUnchangeableOff);
        } __except (EXCEPTION_EXECUTE_HANDLER) { fixed = 1; }
        if (fixed != 0) {
            prism::Speak(acc::strings::Get(acc::strings::Id::KeyBindNotChangeable),
                         /*interrupt=*/true);
            outRv = 1;
            return true;
        }
        ArmCapture(activePanel, row);
        s_armedRow   = row;
        s_armedPanel = activePanel;
        char action[128]; action[0] = '\0';
        acc::engine::ReadButtonText(row, action, sizeof(action));
        char prompt[192];
        snprintf(prompt, sizeof(prompt),
                 acc::strings::Get(acc::strings::Id::FmtKeyBindCapture), action);
        prism::Speak(prompt, /*interrupt=*/true);
        acclog::Write("Menus.KeyMap", "capture armed row=%p action=\"%s\"", row, action);
        outRv = 1;
        return true;
    }
    if (isEsc) {
        s_drilled = false;
        AnnounceTabEntry(activePanel, s_tabCursor);
        acclog::Write("Menus.KeyMap", "undrill to tab level cursor=%d", s_tabCursor);
        outRv = 1;
        return true;
    }
    return false;
}

}  // namespace acc::menus::keymap
