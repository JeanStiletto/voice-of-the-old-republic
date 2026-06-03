// In-game Fähigkeiten screen — see menus_abilities.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "menus_abilities.h"

#include "engine_input.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "log.h"
#include "menus_extract.h"
#include "strings.h"
#include "prism.h"

using acc::menus::detail::DriveListBoxSelection;
using acc::menus::detail::ListBoxNavOp;
using acc::menus::detail::ListBoxNavResult;

namespace acc::menus::abilities {

namespace {

// The ability_listbox row at the current selection_index. Its `id` field is
// the skill index that OnEnterSkill consumes. SEH-guarded; null on empty list
// or out-of-range selection.
void* SelectedRow(void* panel) {
    void* row = nullptr;
    __try {
        auto* lb = reinterpret_cast<unsigned char*>(panel) + kAbilitiesListBoxOffset;
        short sel = *reinterpret_cast<short*>(lb + kListBoxSelectionIndexOffset);
        auto* rows = reinterpret_cast<CExoArrayList*>(lb + kListBoxControlsOffset);
        if (rows && rows->data && sel >= 0 && sel < rows->size) {
            row = rows->data[sel];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        row = nullptr;
    }
    return row;
}

// CSWGuiInGameAbilities::OnEnterSkill(this, CSWGuiControl* row) — the engine's
// per-skill repaint. Reads row->id as the skill index and rewrites the
// name/rank/bonus/total labels + the description box. Coordinate-free, unlike
// the mouse-driven OnAbilitySelectionChanged (which hit-tests cursor position
// against the CSWGuiSkillFlow chart and crashes via OnEnterPower on the
// powers tab). __thiscall(this, row) → one stack arg; the typedef MUST carry
// it or the callee's `ret 4` corrupts our frame.
void CallOnEnterSkill(void* panel, void* row) {
    if (!row) return;
    typedef void(__thiscall* PFN)(void* this_, void* row);
    __try {
        reinterpret_cast<PFN>(kAddrAbilitiesOnEnterSkill)(panel, row);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Abilities", "OnEnterSkill SEH (panel=%p row=%p)",
                      panel, row);
    }
}

// Feed a panel-internal code to CSWGuiInGameAbilities::HandleInputEvent. Used
// for the tab cycle (0x29) and the Feats/Powers chart row-step (0x31/0x32):
// the engine runs the chart move + OnEnterFeat/OnEnterPower repaint itself, so
// the detail labels AND the description box come back fresh.
void CallPanelInput(void* panel, int code) {
    typedef void(__thiscall* PFN)(void* this_, int code, int val);
    __try {
        reinterpret_cast<PFN>(kAddrAbilitiesHandleInputEvent)(panel, code, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Abilities", "CallPanelInput SEH (panel=%p code=0x%x)",
                      panel, code);
    }
}

// True iff the Feats/Powers chart can step one row in the given direction
// WITHOUT wrapping. The engine's own chart nav wraps top<->bottom; we clamp it
// so it matches the skills listbox. tab: 2 = Feats, 1 = Powers.
bool ChartCanStep(void* panel, int tab, bool down) {
    size_t off = (tab == 2) ? kAbilitiesFeatsChartOffset
                            : kAbilitiesPowersChartOffset;
    bool can = false;
    __try {
        auto* chart = reinterpret_cast<unsigned char*>(panel) + off;
        unsigned char row   = *(chart + kSkillFlowChartRowOffset);
        unsigned char count = *(chart + kSkillFlowChartRowCountOffset);
        if (count == 0) can = false;
        else if (down)  can = row + 1 < count;
        else            can = row > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        can = false;
    }
    return can;
}

// True iff the Powers tab exists for this character (Jedi with powers). The
// engine uses this to skip an empty Powers tab in its own cycle.
bool PowersAvailable(void* panel) {
    typedef int(__thiscall* PFN)(void*);
    int r = 0;
    __try {
        r = reinterpret_cast<PFN>(kAddrAbilitiesDisplayPowers)(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = 0;
    }
    return r != 0;
}

// Switch directly to a specific tab (0 Skills / 1 Powers / 2 Feats). Each
// handler just sets the tab field + UpdateView — safe even for Powers (it does
// NOT call OnEnterPower). Only call with tab 1 when PowersAvailable.
//
// CALLING CONVENTION: despite Ghidra labelling these On*ButtonPressed as
// `(void)`, their SARIF purgeSize is 4 — they `ret 4`, so the typedef MUST
// carry a 4-byte arg (pass a dummy) or the callee pops our return address and
// corrupts the frame. Same trap as OnAbilitySelectionChanged.
void SwitchToTab(void* panel, int tab) {
    uintptr_t addr = (tab == 1) ? kAddrAbilitiesOnPowersButton
                   : (tab == 2) ? kAddrAbilitiesOnFeatsButton
                                : kAddrAbilitiesOnSkillsButton;
    typedef void(__thiscall* PFN)(void* this_, int dummy);
    __try {
        reinterpret_cast<PFN>(addr)(panel, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Abilities", "SwitchToTab SEH (panel=%p tab=%d)",
                      panel, tab);
    }
}

// Ordered list of available tabs: Skills, [Powers if any], Feats.
int BuildAvailableTabs(void* panel, int outTabs[3]) {
    int n = 0;
    outTabs[n++] = 0;                         // Skills (always)
    if (PowersAvailable(panel)) outTabs[n++] = 1;  // Powers (Jedi only)
    outTabs[n++] = 2;                         // Feats (always)
    return n;
}

// Active tab: 0 = Skills, 1 = Powers, 2 = Feats (CGuiInGame.field139_0xbc0).
// -1 on any resolve failure.
int ReadTab() {
    void* gui = acc::engine::ResolveGuiInGame();
    if (!gui) return -1;
    int tab = -1;
    __try {
        tab = *(reinterpret_cast<unsigned char*>(gui) + kGuiInGameAbilitiesTabOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        tab = -1;
    }
    return tab;
}

// Read an inline CSWGuiLabel/CSWGuiButton member at panel+offset. Returns true
// + fills buf on non-empty text. Members are embedded in the panel struct
// (same layout as the chargen ability_labels[] array), so &member == panel+off.
bool ReadLabel(void* panel, size_t offset, char* buf, size_t bufSize) {
    if (!panel || !buf || bufSize < 2) return false;
    buf[0] = '\0';
    void* ctrl = reinterpret_cast<unsigned char*>(panel) + offset;
    return acc::menus::extract::FromControl(ctrl, buf, bufSize, panel) &&
           buf[0] != '\0';
}

// Append ", <labelText> <valueText>" to msg iff the value label is non-empty.
// Label text comes from the engine's own localized control — nothing hardcoded.
// Feat/power tabs leave rank/bonus/total blank → skipped.
void AppendPair(void* panel, size_t labelOff, size_t valueOff,
                char* msg, size_t msgSize) {
    char value[64];
    if (!ReadLabel(panel, valueOff, value, sizeof(value))) return;
    char label[128];
    bool haveLabel = ReadLabel(panel, labelOff, label, sizeof(label));

    size_t used = strnlen(msg, msgSize);
    if (used + 2 >= msgSize) return;
    if (haveLabel) {
        snprintf(msg + used, msgSize - used, ", %s %s", label, value);
    } else {
        snprintf(msg + used, msgSize - used, ", %s", value);
    }
}

// Speak the shown entry: LBL_NAME, plus the rank/bonus/total pairs ONLY on the
// Skills tab. Feats/powers are binary (you have them or not) and their
// OnEnterFeat/OnEnterPower repaint leaves the rank/bonus/total labels at a stale
// "0", so appending them there is spurious noise — skip. Optional `prefix` (the
// tab name) is spoken first. No "N of M" position — just noise next to stats.
void AnnounceDetail(void* panel, const char* prefix, bool withStats) {
    char msg[512] = {0};
    if (prefix && prefix[0]) {
        snprintf(msg, sizeof(msg), "%s. ", prefix);
    }
    size_t base = strnlen(msg, sizeof(msg));
    ReadLabel(panel, kAbilitiesNameLabelOffset, msg + base, sizeof(msg) - base);

    if (withStats) {
        AppendPair(panel, kAbilitiesSkillRankLabelOffset,
                   kAbilitiesRankValueLabelOffset, msg, sizeof(msg));
        AppendPair(panel, kAbilitiesBonusLabelOffset,
                   kAbilitiesBonusValueLabelOffset, msg, sizeof(msg));
        AppendPair(panel, kAbilitiesTotalLabelOffset,
                   kAbilitiesTotalValueLabelOffset, msg, sizeof(msg));
    }

    if (msg[0]) prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Menus.Abilities", "announce msg=\"%s\"", msg);
}

// Speak just the tab name (its button text) — used at the tab level, where the
// user is choosing which tab to drill into.
void SpeakTabName(void* panel, int tab) {
    size_t btnOff = kAbilitiesSkillsButtonOffset;
    if (tab == 1)      btnOff = kAbilitiesPowersButtonOffset;
    else if (tab == 2) btnOff = kAbilitiesFeatsButtonOffset;
    char tabName[128] = {0};
    if (ReadLabel(panel, btnOff, tabName, sizeof(tabName))) {
        prism::Speak(tabName, /*interrupt=*/true);
    }
    acclog::Write("Menus.Abilities", "tab-level tab=%d name=\"%s\"", tab, tabName);
}

// Two-level drill state: false = tab level (Up/Down pick a tab, Enter drills),
// true = list level (Up/Down browse entries, Esc returns to tab level). Reset
// to tab level whenever the panel pointer changes (fresh open).
bool  s_drilled      = false;
void* s_drilledPanel = nullptr;

}  // namespace

bool IsAbilitiesPanel(void* panel) {
    return panel &&
           acc::engine::IdentifyPanel(panel) ==
               acc::engine::PanelKind::InGameAbilities;
}

void RefreshDetail(void* panel) {
    if (!IsAbilitiesPanel(panel)) return;
    // Only the Skills tab needs our repaint; on Feats/Powers the engine's own
    // OnEnterFeat/OnEnterPower already keep LB_DESC fresh.
    if (ReadTab() == 0) CallOnEnterSkill(panel, SelectedRow(panel));
}

// Browse one entry on the current tab's list (drilled state). Skills are driven
// by us (the engine has no keyboard nav for them); Feats/Powers forward to the
// engine's chart nav, pre-clamped so they don't wrap.
void BrowseList(void* panel, int tab, ListBoxNavOp op) {
    if (tab == 0) {
        void* lb = reinterpret_cast<unsigned char*>(panel) + kAbilitiesListBoxOffset;
        ListBoxNavResult r;
        if (DriveListBoxSelection(lb, op, /*minSel=*/0, r)) {
            CallOnEnterSkill(panel, r.row);
            AnnounceDetail(panel, nullptr, /*withStats=*/true);
            acclog::Write("Abilities", "skill nav sel=%d->%d (rows=%d)",
                          r.oldSel, r.newSel, r.rowCount);
        }
        return;
    }
    // Feats/Powers chart. Up/Down step rows via the engine (chart move +
    // OnEnterFeat/OnEnterPower repaint -> fresh name + description). Pre-clamp so
    // it doesn't wrap. Home/End have no chart equivalent. No stats: feats/powers
    // are binary and their repaint leaves rank/bonus/total stale at 0.
    if (op == ListBoxNavOp::StepUp && ChartCanStep(panel, tab, false)) {
        CallPanelInput(panel, kAbilitiesPanelCodeChartUp);
    } else if (op == ListBoxNavOp::StepDown && ChartCanStep(panel, tab, true)) {
        CallPanelInput(panel, kAbilitiesPanelCodeChartDown);
    }
    AnnounceDetail(panel, nullptr, /*withStats=*/false);
}

bool HandleInput(int /*n*/, void* /*thisPtr*/, void* activePanel,
                 int param_1, int param_2, int& outRv) {
    if (!IsAbilitiesPanel(activePanel)) return false;

    // Fresh open -> start at the tab level.
    if (activePanel != s_drilledPanel) {
        s_drilledPanel = activePanel;
        s_drilled = false;
    }
    if (param_2 == 0) return false;  // press edge only

    int tab = ReadTab();

    ListBoxNavOp op;
    bool isNav = true;
    if      (param_1 == kInputNavUp)   op = ListBoxNavOp::StepUp;
    else if (param_1 == kInputNavDown) op = ListBoxNavOp::StepDown;
    else if (param_1 == kInputHome)    op = ListBoxNavOp::JumpFirst;
    else if (param_1 == kInputEnd)     op = ListBoxNavOp::JumpLast;
    else isNav = false;

    bool isEnter = (param_1 == kInputEnter1 || param_1 == kInputEnter2);
    // Esc arrives as kInputEsc2 (0xdf) for a real keypress — matching only
    // kInputEsc1 (0xb4) let it fall through to the engine's own close handler,
    // so the list-level Esc never returned to the tab level.
    bool isEsc   = (param_1 == kInputEsc1 || param_1 == kInputEsc2);

    if (!s_drilled) {
        // ---- Tab level: Up/Down pick a tab (clamped), Enter drills in. ----
        if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
            int tabs[3];
            int nt = BuildAvailableTabs(activePanel, tabs);
            int idx = 0;
            for (int i = 0; i < nt; ++i) if (tabs[i] == tab) { idx = i; break; }
            int ni = idx + (param_1 == kInputNavDown ? 1 : -1);
            if (ni >= 0 && ni < nt) {
                SwitchToTab(activePanel, tabs[ni]);
                SpeakTabName(activePanel, tabs[ni]);
            } else {
                SpeakTabName(activePanel, tab);  // clamp: re-announce current tab
            }
            outRv = 1;
            return true;
        }
        if (isEnter) {
            s_drilled = true;
            AnnounceDetail(activePanel, nullptr, /*withStats=*/tab == 0);
            acclog::Write("Abilities", "drill into tab=%d (panel=%p)",
                          tab, activePanel);
            outRv = 1;
            return true;
        }
        // Esc / Home / End / others: fall through (Esc closes the screen).
        return false;
    }

    // ---- List level: Up/Down browse, Esc returns to the tab level. ----
    if (isNav) {
        BrowseList(activePanel, tab, op);
        outRv = 1;
        return true;
    }
    if (isEnter) {
        AnnounceDetail(activePanel, nullptr, /*withStats=*/tab == 0);
        outRv = 1;
        return true;
    }
    if (isEsc) {
        s_drilled = false;
        SpeakTabName(activePanel, tab);
        acclog::Write("Abilities", "undrill to tab level tab=%d (panel=%p)",
                      tab, activePanel);
        outRv = 1;
        return true;
    }

    return false;  // anything else falls through
}

}  // namespace acc::menus::abilities
