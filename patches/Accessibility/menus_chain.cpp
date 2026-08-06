// chain-navigation state and helpers.
//
// here and what stays in menus.cpp. Function bodies are unchanged from
// the original menus.cpp inline definitions; only the namespacing /
// linkage changes.

#include <windows.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_chain.h"
#include "engine_manager.h"
#include "engine_rebase.h"

#include "engine_game.h"     // IsKotor2 — per-game cursor-warp hit-test shims
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_player.h"   // PartyTableIsNPCAvailable / *Selectable /
                              // kPartyRosterSlotCount
#include "engine_reads.h"
#include "log.h"
#include "menus_chargen_attr.h"
#include "menus_chargen_skills.h"
#include "menus_charsheet.h"
#include "menus_credits.h"
#include "menus_equipstats.h"
#include "menus_extract.h"
#include "menus_focus.h"     // WalkAndCaptureOnFirstSight — see RebindChain
#include "menus_internal.h"
#include "menus_inventory.h"
#include "menus_modsettings.h"
#include "menus_pazaakdeck.h"
#include "menus_store.h"
#include "prism.h"
#include "strings.h"
#include "engine_offsets_select.h"

using namespace acc::engine;  // IdentifyPanel, PanelKind, kInput*, etc.

using acc::menus::detail::IsChainNavigable;
using acc::menus::detail::IsClassSelectionIcon;
using acc::menus::detail::GetControlCenter;
using acc::menus::detail::ScaleGuiThresholdPx;

namespace acc::menus::chain {

// ============================================================================
// State definitions (extern decls live in menus_chain.h).
// ============================================================================

ChainEntry g_chain[kMaxChainEntries];
void*      g_chainPanel  = nullptr;
int        g_chainIndex  = 0;
int        g_chainCount  = 0;

void* g_tabbedPanel = nullptr;
int   g_tabsStart   = -1;
int   g_tabsCount   = 0;

int g_equipSlotClickOffsetY  = 0;
int g_classIconClickOffsetX  = 0;

int ComputeTabClickOffsetY(void* panel) {
    if (!panel || g_tabbedPanel != panel || g_tabsCount < 2) return 0;
    int firstTabIdx = -1;
    for (int i = 0; i < g_chainCount; ++i) {
        if (!IsTabButton(g_chain[i].control)) continue;
        if (firstTabIdx < 0) {
            firstTabIdx = i;
        } else {
            int spacing = g_chain[i].cy - g_chain[firstTabIdx].cy;
            return spacing > 0 ? spacing : 0;
        }
    }
    return 0;
}

// IsModalTextPanel was a file-static helper inside menus.cpp; only
// RebindChain calls it, so it lives here now (anonymous namespace).
namespace {
bool IsModalTextPanel(PanelKind k) {
    switch (k) {
    case PanelKind::MessageBoxModal:
    case PanelKind::TutorialBox:
    case PanelKind::AreaTransition:
        return true;
    // StatusSummary deliberately omitted: its body is a label cluster, not a
    // single-row listbox, so this listbox path never applies. RebindChain
    // exposes its visible rows via the dedicated per-kind block below.
    default:
        return false;
    }
}

// Panels[]-membership check, shared by Validate{Tabbed,Chain}Panel and the
// DetectTabsCluster precondition. The save-popup teardown frees the SaveLoad
// panel synchronously when the user commits a save; the next
// OnListBoxSetActiveControl fires on the underlying in-game-menu's tooltip
// listbox while g_currentPanel still points at the freed SaveLoad
// allocation. By then the heap allocator has typically reused that block for
// combat-log strings, so panel+0x20/+0x24 (CExoArrayList data+size) come
// back as ASCII text — the data[0] deref then takes an AV
// (crash analysed 2026-05-29, dump swkotor.exe(1).31228.dmp).
bool IsPanelLive(void* panel) {
    if (!panel) return false;
    constexpr int kCap = 32;
    void* panels[kCap];
    int n = acc::engine::ReadPanelArray(
        acc::engine::GetGuiManager(), panels, kCap);
    for (int i = 0; i < n; ++i) {
        if (panels[i] == panel) return true;
    }
    return false;
}
}  // namespace

// ============================================================================
// Reads.
// ============================================================================

void* ReadPanelActiveControl(void* panel) {
    if (!panel) return nullptr;
    return *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(panel) + kPanelActiveControlOffset);
}

int FindChainEntry(void* control) {
    if (!control) return -1;
    for (int i = 0; i < g_chainCount; ++i) {
        if (g_chain[i].control == control) return i;
    }
    return -1;
}

// Speak guidance when the user activates an InGameLevelUp category that the
// engine has not currently enabled. The wizard enforces sequential leveling:
// exactly one category button carries bit_flags bit 3 (0x8 =
// CSWGuiControl::SetEnabled) at a time. Find it and name it so a blind user
// knows which step to do next; fall back to a generic "not your turn" line if
// none resolves. bit 3 verified against the current step in
// patch-20260608-125730/125909 (current 0x..8f, others 0x..06/0x..04).
void SpeakLevelUpDoStepFirst() {
    for (int i = 0; i < g_chainCount; ++i) {
        void* c = g_chain[i].control;
        if (!c) continue;
        uint32_t bf = 0;
        __try {
            bf = *reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(c) + kControlBitFlagsOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if ((bf & 0x8) == 0) continue;
        char name[128];
        const char* src = acc::menus::extract::FromControl(
            c, name, sizeof(name), g_chainPanel);
        if (src && name[0]) {
            char msg[192];
            snprintf(msg, sizeof(msg),
                     acc::strings::Get(acc::strings::Id::FmtLevelUpDoStepFirst),
                     name);
            prism::Speak(msg, /*interrupt=*/false);
            return;
        }
    }
    prism::Speak(acc::strings::Get(acc::strings::Id::LevelUpStepLocked),
                 /*interrupt=*/false);
}

// ============================================================================
// Tab-cluster detection.
// ============================================================================

bool DetectTabsCluster(void* panel, int& outStart, int& outCount) {
    outStart = -1;
    outCount = 0;
    if (!panel) return false;

    // Guard against a stale g_currentPanel that the engine already freed.
    // See IsPanelLive comment above for the save-popup teardown crash.
    if (!IsPanelLive(panel)) {
        acclog::Write("DetectTabsCluster", "%p not in panels[]; skipping", panel);
        return false;
    }

    // Store is structurally tab-cluster-shaped (listbox @ controls[0],
    // three navigable buttons clumped further down) but the three buttons
    // are Verkaufsliste / Schliess. / Kaufen — distinct action buttons,
    // not page tabs. Flagging them as tabs makes the chain Enter handler
    // route through click-sim (the tab path) instead of FireActivate,
    // which lands the click on whatever overlay sits at the button's
    // y-coordinate — in the wild that misroutes Schliess. into the engine
    // quit-confirm popup.
    if (acc::menus::store::IsStorePanel(panel)) return false;

    // The workbench upgrade panel (upgrade.gui) is the same shape: LB_ITEMS
    // at controls[0] followed by the seven slot buttons (ids 12-18) as a
    // contiguous navigable run. Those are slot pickers, not page tabs.
    // Latching them as tabs routes their Enter through the tab click-sim path
    // (which opens nothing) instead of the chain handler's
    // OnEnterSlot+OnSlotSelected arm. The detection fired the first time only
    // after the user opened a slot, so re-entering any slot after a picker Esc
    // did nothing until the workbench was closed and reopened
    // (patch-20260616-141620.log).
    if (acc::engine::IdentifyPanel(panel) ==
            acc::engine::PanelKind::WorkbenchUpgrade) {
        return false;
    }

    auto* panelList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!panelList->data || panelList->size < 2) return false;

    void* lb = panelList->data[0];
    if (!lb) return false;
    void** vt = *reinterpret_cast<void***>(lb);
    if (reinterpret_cast<uintptr_t>(vt) != kVtableListBox) return false;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    if (!lbList->data || lbList->size <= 0) return false;

    int n = panelList->size > 256 ? 256 : panelList->size;
    int start = -1, end = -1;
    for (int i = 1; i < n; ++i) {
        void* c = panelList->data[i];
        if (c && IsChainNavigable(c)) {
            if (start < 0) start = i;
            end = i;
        } else if (start >= 0) {
            break;
        }
    }
    if (start < 0 || (end - start + 1) < 2) return false;
    outStart = start;
    outCount = end - start + 1;
    return true;
}

void ResetTabbedState() {
    g_tabbedPanel = nullptr;
    g_tabsStart   = -1;
    g_tabsCount   = 0;
}

void RebindChainPreserveIndex(void* panel) {
    int savedIndex = g_chainIndex;
    RebindChain(panel);
    if (g_chainCount <= 0) {
        g_chainIndex = 0;
        return;
    }
    if (savedIndex < 0) savedIndex = 0;
    if (savedIndex >= g_chainCount) savedIndex = g_chainCount - 1;
    g_chainIndex = savedIndex;
}

void InvalidateChain() {
    g_chainPanel = nullptr;
    g_chainIndex = 0;
    g_chainCount = 0;
    // Tabbed-panel state is intentionally NOT reset here. The chain panel and
    // the tabbed panel are orthogonal: closing a sub-panel (Grafik) only
    // needs to invalidate the chain, but used to also wipe g_tabbedPanel for
    // the still-live parent (Options), forcing MaybeDetectTabs to re-latch
    // on every reopen — leaving the click-sim/warp offset stale in the
    // window between rebind and re-detect (patch-20260530-110829.log,
    // line 374-511). ValidateTabbedPanel handles tabbed-panel liveness
    // independently each tick.
}

void ValidateTabbedPanel() {
    if (!g_tabbedPanel) return;
    if (IsPanelLive(g_tabbedPanel)) return;
    acclog::Write("ValidateTabbedPanel", "%p not in panels[]; clearing tabbed-mode state",
                  g_tabbedPanel);
    ResetTabbedState();
}

void ValidateChainPanel() {
    if (!g_chainPanel) return;
    if (IsPanelLive(g_chainPanel)) return;
    acclog::Write("ValidateChainPanel", "%p not in panels[]; invalidating chain",
                  g_chainPanel);
    InvalidateChain();
}

// ============================================================================
// Chain builders + cycle-arrow + button finders.
// ============================================================================

bool IsTabButton(void* control) {
    if (!control || !g_tabbedPanel || g_tabsCount < 2) return false;
    auto* tlist = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
    if (!tlist || !tlist->data) return false;
    for (int i = g_tabsStart;
         i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
        if (tlist->data[i] == control) return true;
    }
    return false;
}

void* FindAdjacentArrow(void* panel, void* focused, bool toRight) {
    if (!panel || !focused) return nullptr;

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;

    int focusCx, focusCy;
    if (!GetControlCenter(focused, focusCx, focusCy)) return nullptr;

    void* best = nullptr;
    int bestDx = 0x7fffffff;

    // Same-row tolerance. The arrows are centred on their value button, so
    // this only has to absorb rounding — but on KOTOR 2 the rounding is
    // multiplied by the GUI stretch along with everything else.
    const int kSameRowDy = ScaleGuiThresholdPx(5);

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == focused) continue;
        if (!IsChainNavigable(c)) continue;

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;
        if (cy - focusCy > kSameRowDy || focusCy - cy > kSameRowDy) continue;

        int dx = toRight ? (cx - focusCx) : (focusCx - cx);
        if (dx <= 0) continue;

        char tmp[64];
        if (acc::menus::extract::FromControl(c, tmp, sizeof(tmp), panel)) continue;

        if (dx < bestDx) {
            bestDx = dx;
            best   = c;
        }
    }
    return best;
}

void* FindCloseButton(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!IsChainNavigable(c)) continue;
        char text[256];
        if (!acc::menus::extract::FromControl(c, text, sizeof(text), panel)) continue;
        if (strncmp(text, "Schliess", 8) == 0 ||
            strncmp(text, "Close",    5) == 0 ||
            strncmp(text, "OK",       2) == 0 ||
            strncmp(text, "Weiter",   6) == 0 ||
            strncmp(text, "Continue", 8) == 0) {
            return c;
        }
    }
    return nullptr;
}

void* FindCancelButton(void* panel) {
    if (!panel) return nullptr;
    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return nullptr;
    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!IsChainNavigable(c)) continue;
        char text[256];
        if (!acc::menus::extract::FromControl(c, text, sizeof(text), panel)) continue;
        if (strncmp(text, "Abbrechen", 9) == 0 ||
            strncmp(text, "Cancel",    6) == 0 ||
            strncmp(text, "Nein",      4) == 0 ||
            strncmp(text, "No",        2) == 0) {
            return c;
        }
    }
    return nullptr;
}

namespace {

// Sentinel for AppendChainEntry's sortCy parameter: sort this entry by its
// own cy, the normal case for a panel-direct control.
constexpr int kSortByOwnCy = INT_MIN;

// `sortCy` overrides only the reading-order key, never the real cy the cursor
// warp uses. Listbox rows pass their listbox's extent top here so the whole
// listbox stays one contiguous block — see the ChainEntry::sortCy comment.
void AppendChainEntry(void* control, int sortCy = kSortByOwnCy) {
    if (g_chainCount >= kMaxChainEntries) return;
    if (!IsChainNavigable(control))       return;
    int cx, cy;
    if (!GetControlCenter(control, cx, cy)) return;
    bool ownCy = (sortCy == kSortByOwnCy);
    g_chain[g_chainCount++] = {
        control, cx, cy,
        ownCy ? cy : sortCy,
        /*textOnly=*/false, /*virtualKind=*/0,
        // Only panel-direct controls (the default sortCy) are ordered
        // geometrically; a caller-supplied sortCy means this is a listbox
        // row whose block order must survive the sort untouched.
        /*geometricOrder=*/ownCy
    };
}

void AppendChainTextOnly(void* control, void* panel) {
    if (g_chainCount >= kMaxChainEntries) return;
    if (!control) return;
    char tmp[512];
    if (!acc::menus::extract::FromControl(control, tmp, sizeof(tmp), panel)) return;
    int cx, cy;
    if (!GetControlCenter(control, cx, cy)) return;
    g_chain[g_chainCount++] = {
        control, cx, cy, cy,
        /*textOnly=*/true, /*virtualKind=*/0, /*geometricOrder=*/true
    };
}

// Panel-aware chain filter: in CSWGuiPortraitCharGen we anchor the chain
// on the left_arrow alone (announces the current portrait via the
// PerKind path in menus_extract.cpp's section 9d) and consolidate the
// right_arrow out of the chain entirely. The user lands on one entry,
// hears the value, and presses Left/Right to cycle — matching the
// existing `[◀] value [▶]` UX without needing the head_3d_scene_control
// (a non-button, IsChainNavigable would reject it) as a chain anchor.
void* ResolvePortraitChargenSkip(void* panel) {
    void* portraitChargenSkip = nullptr;
{
    void** pVt = *reinterpret_cast<void***>(panel);
    if (reinterpret_cast<uintptr_t>(pVt) ==
            kVtableCSWGuiPortraitCharGen) {
        portraitChargenSkip = reinterpret_cast<unsigned char*>(panel) +
                              kPortraitRightArrowOffset;
    }
}
    return portraitChargenSkip;
}

// Store-panel action buttons (Schliess. / Verkaufsliste / Kaufen).
// These live at fixed struct offsets and we drive them via dedicated
// hotkeys (Esc to close, G to toggle mode, Enter on item to trade)
// rather than chain navigation. Walking past the inventory rows into
// them just adds three dead entries the user has to skip, so filter
// them out of the chain entirely.
void ResolveStoreActionButtons(void* panel, void*& storeCancelBtn,
                               void*& storeToggleBtn,
                               void*& storeAcceptBtn) {
if (acc::menus::store::IsStorePanel(panel)) {
    auto* p = reinterpret_cast<unsigned char*>(panel);
    storeCancelBtn = p + kStoreCancelButtonOffset;
    storeToggleBtn = p + kStoreToggleButtonOffset;
    storeAcceptBtn = p + kStoreAcceptButtonOffset;
}
}

// InGameEquip picker listbox (LB_ITEMS, id=5): the equip panel keeps
// the picker's item listbox in panel.controls[] even when the picker
// isn't visually shown — the engine pre-populates it with the body-
// slot candidates at panel open. Letting the chain recurse into its
// children leaks rows like "Brejiks Armband" / "Energieschild" /
// "Sith-Energieschild" into the equip screen between the slot
// buttons. The picker has its own dedicated listbox spec
// (EquipPickerSpec in menus_listbox.cpp) that drives row selection
// when armed; chain nav never needs to step into it.
void* ResolveEquipPickerListBox(void* panel) {
void* equipPickerLb = nullptr;
if (IdentifyPanel(panel) == PanelKind::InGameEquip) {
    equipPickerLb = acc::menus::detail::FindControlById(
        panel, kEquipLbItemsId);
}
    return equipPickerLb;
}

// WorkbenchUpgrade LB_ITEMS (id=0): the compatible-crystal list. Exactly
// the same hazard as the equip picker above — the engine keeps the listbox
// in panel.controls[] (just toggles its visibility via ShowItems), so the
// generic flatten would expose every crystal row, including rows scrolled
// off the visible page, as standalone chain buttons. Those off-page rows
// are un-presented phantoms: their hit-test returns NULL and FireActivate
// on one AVs the engine (lightsabercrystalcrash — a user arrowed onto the
// leaked off-page row after Esc-ing the picker and the game shut down).
// The picker has its own spec (WorkbenchUpgradeSpec) that drives row
// selection + scroll safely while armed; chain nav must never step into it.
void* ResolveWorkbenchUpgradeListBox(void* panel) {
void* workbenchUpgradeLb = nullptr;
if (IdentifyPanel(panel) == PanelKind::WorkbenchUpgrade) {
    workbenchUpgradeLb = acc::menus::detail::FindControlById(
        panel, kWorkbenchUpgradeLbItemsId);
}
    return workbenchUpgradeLb;
}

// Per-kind decorative filter. Identifies non-interactive icon buttons
// that the engine drops in panel.controls[] but the user has no reason
// to focus — IsChainNavigable can't tell them from real buttons (same
// CSWGuiButton vtable). Keyed on (panel kind, .gui id at +0x50) so
// adding a new entry is one line per kind.
//
// Currently registered:
//   InGameCharacter id=1  (btn_3dchar)   — interaction button for the
//     3D character model rotator. Image-only with no caption; mouse-
//     drives the model spin which isn't a screen-reader-useful action.
//     Without the filter it appears as "control 1" in the chain.
//   InGameCharacter id=64/67 (btn_change1/btn_change2) — party-member
//     switch portraits. The engine cycles party leader on Tab, which
//     re-binds the panel to the new leader and announces the name via
//     party_leader_announce — that covers the same gesture and works
//     in-world too, so these portrait buttons are redundant.
//   InGameCharacter id=65/66 (btn_charright/btn_charleft) — pagination
//     arrows over the 9-slot NPC roster. Useless in KOTOR 1: max
//     active party is 3 (PC + 2 NPCs), and the 2 portrait slots already
//     cover both companions.
//   (All charsheet ids above are KOTOR 1; the K2 set — just the moved
//   model rotator — lives in the per-game branch below.)
bool IsDecorativeControl(void* panel, void* c,
                         const char* closeCaption, bool haveCloseCaption) {
    PanelKind pk = IdentifyPanel(panel);
    int cid = *reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
    // Pazaak deck builder: drop the overlay value/count/title labels and
    // the unaddable (zero-owned) available cards.
    if (acc::menus::pazaakdeck::IsChainDecorative(panel, c)) return true;
    // KOTOR 2 gamepad Quick Menu: 22 controls, of which exactly 8 are the
    // menu entries. The other 14 are the icon and background quads — the
    // SAME CSWGuiButton class, and every id is -1, so neither the class nor
    // the .gui id can tell them apart. What does is that they carry no
    // caption by any route: the extractor's whole cascade (CExoString,
    // strref, text object, gui_string, per-kind table) comes back empty, and
    // reading their gui_string faults outright. Without this the chain is
    // 22 entries long and every other press speaks the "control N"
    // placeholder instead of a menu entry.
    if (pk == PanelKind::GamepadQuickMenu) {
        char text[128];
        if (!acc::menus::extract::FromControl(c, text, sizeof(text), panel)) {
            return true;
        }
    }
    // Pazaak wager popup: mask the less/more SpeedButtons (gui ids 4/5).
    // The wager is adjusted with Left/Right (held = auto-repeat) via
    // pazaak::Tick's polled stepper, so these buttons are redundant in the
    // chain — dropping them lets Up/Down step straight from the wager row
    // to Setzen/Beenden.
    if (pk == PanelKind::PazaakWager && (cid == 4 || cid == 5)) return true;
    // Charsheet decorative ids are strictly per game: K2's panel dropped
    // the party portraits/pagination entirely and re-numbered the model
    // rotator (BTN_3DCHAR 1 → 5) — and K1's ids 65/66 are K2's BTN_AUTO /
    // BTN_LEVELUP, real actions that must stay in the chain (mined from K2
    // character_p.gui 2026-08-02; override copy id-identical to gui.bif).
    if (pk == PanelKind::InGameCharacter) {
        if (acc::game::IsKotor2()) {
            if (cid == 5) return true;  // BTN_3DCHAR (model rotator)
        } else if (cid == 1 || cid == 64 || cid == 65 || cid == 66 ||
                   cid == 67) {
            return true;
        }
    }
    // InGameEquip BTN_EQUIP (id=37, "OK"): the OK button is the
    // engine's picker-commit button. The accessibility picker
    // dispatcher (menus_listbox.cpp EquipPickerOnEnter) commits
    // the selected item directly via QueueEquipCommit, so OK has
    // no role in chain nav. When the picker isn't armed the
    // engine renders it as "OK, nicht verfügbar" — landing on
    // that announces a dead-end. Drop it.
    //
    // InGameEquip BTN_BACK (id=36, "Schliess."): Esc closes the
    // panel via the engine's universal modal-close path, so the
    // close button is functionally redundant for keyboard nav.
    // Same reasoning as Store's Schliess./Verkaufsliste/Kaufen
    // filter above — dedicated hotkey replaces chain landing.
    if (pk == PanelKind::InGameEquip &&
        (cid == kEquipBtnEquipId || cid == kEquipBtnBackId)) {
        return true;
    }
    // InGameEquip bottom-row party-cycle buttons. Tab cycles the
    // active leader engine-side; the panel re-binds and
    // party_leader_announce speaks the new name. The portrait slots
    // (change_party_1/2) and pagination arrows (character_left/right)
    // are therefore redundant — drop them from the chain.
    if (pk == PanelKind::InGameEquip) {
        auto* p = reinterpret_cast<unsigned char*>(panel);
        if (c == p + kEquipPanelChangeParty1ButtonOffset ||
            c == p + kEquipPanelChangeParty2ButtonOffset ||
            c == p + kEquipPanelCharacterLeftButtonOffset ||
            c == p + kEquipPanelCharacterRightButtonOffset) {
            return true;
        }
    }
    // InGameLevelUp "Zurück" (button_back) and "Abbrechen"
    // (button_cancel). Both are dead ends for keyboard nav: Zurück
    // only steps the engine's visual category highlight — we already
    // navigate the level-up categories with our own arrow keys — and
    // Abbrechen routes to OnCancelPressed, which the engine gates on a
    // can-cancel flag it only ever sets to 0 (see kLevelUpButton*Offset
    // in engine_offsets.h). An in-game level-up cannot be cancelled in
    // vanilla; Annehmen is the sole exit. Drop both so arrow nav steps
    // only through the actionable category buttons + Annehmen.
    if (pk == PanelKind::InGameLevelUp) {
        auto* p = reinterpret_cast<unsigned char*>(panel);
        if (c == p + kLevelUpButtonBackOffset ||
            c == p + kLevelUpButtonCancelOffset) {
            return true;
        }
    }
    // InGameMap up_button / down_button ("Vorheriger Hinweis" /
    // "Nächster Hinweis"). They step the engine's GetPrevMapNote /
    // GetNextMapNote cycle, which our map cursor supersedes: W/A/S/D
    // sweeps the map and map_ui_cursor speaks every note it crosses, so
    // the engine's own note stepper is a second, weaker way to do the
    // same thing. Dropping both lets arrow nav step straight to
    // Gruppenauswahl / the return button. The per-kind extractor that
    // names them (menus_extract TryInGameMapArrow) stays — it still
    // labels them for the diagnostic walk and for any focus event the
    // engine raises on its own.
    if (pk == PanelKind::InGameMap) {
        auto* p = reinterpret_cast<unsigned char*>(panel);
        if (c == p + kInGameMapUpButtonOffset ||
            c == p + kInGameMapDownButtonOffset) {
            return true;
        }
    }
    // PartySelection "Hinzuf." (BTN_NPC). Enter on a portrait already
    // drives the engine's OnToggled directly, so this button is the
    // mouse flow's redundant second step — landing on it after every
    // portrait just doubles the path to OK.
    if (pk == PanelKind::PartySelection && cid == kPartySelectionAddBtnId) {
        return true;
    }
    // Inventory "Verwenden" (useitem_button). Same redundancy as the
    // PartySelection Hinzuf. above, plus a correctness problem of its own:
    // the button is the engine's gamepad-A shortcut, so it dispatches
    // activate to panel.active_control rather than to our chain focus, and
    // our cursor warp usually fails to land on an item row (the hit test
    // returns NULL for them — "mouseOver after=00000000" on every row warp in
    // patch-20260731-083448.log). Pressing it therefore fires whatever row
    // the engine still considers active, which can be a different item than
    // the one the user is standing on. Enter on the row reaches the same
    // per-item handler (CreateItemEntry AddEvent's OnControlSelected /
    // CantEquip / NotUseable / FullHealth / … on the row itself), directly
    // and unambiguously — so the button is pure hazard for keyboard nav.
    if (pk == PanelKind::InGameInventory &&
        acc::menus::inventory::IsUseItemButton(panel, c)) {
        return true;
    }
    // PartySelection portraits with no currently-selectable
    // companion. The panel renders all 9 roster slots in a fixed
    // 3x3 grid; sighted players see empty / greyed slots, but a
    // blind navigator has nothing actionable on them (the engine
    // refuses Add/OK anyway) and the spoiler rule from
    // menus_extract section 7b means we deliberately don't speak
    // a name. Treating them as decorative drops them from the
    // chain entirely so arrow keys only step through usable picks.
    //
    // The per-portrait flag word at +0x448 is NOT a reliable gate
    // — patch-20260526-120026.log slots 6/7/8 had values
    // 0xfffffff9 / 0x5f484c41 / 0x39000001 (clearly uninitialised
    // heap memory whose low bit randomly happens to be 1).
    // OnPanelAdded apparently only writes that field for some
    // slots, so trusting bit 0 leaks 3 unnamed "control N" entries
    // into the chain. The NPC roster index at +0x450 IS reliable
    // (clean 0..8 in the log), so route the decision through the
    // engine: keep the portrait if the slot is in the active
    // party (partyId >= 0) OR the engine's own GetIsNPCAvailable
    // returns true for that roster index.
    if (pk == PanelKind::PartySelection) {
        void** vt = *reinterpret_cast<void***>(c);
        if (reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiPartySelectionButton) {
            const size_t kPartyPortraitPartyIdOffset = acc::off::Todo(0x44c);
            const size_t kPartyPortraitNpcSlotOffset = acc::off::Todo(0x450);
            int partyId = -1, npcSlot = -1;
            __try {
                auto* base = reinterpret_cast<unsigned char*>(c);
                partyId = *reinterpret_cast<int*>(
                    base + kPartyPortraitPartyIdOffset);
                npcSlot = *reinterpret_cast<int*>(
                    base + kPartyPortraitNpcSlotOffset);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                partyId = -1;
                npcSlot = -1;
            }
            bool inActiveParty = partyId >= 0;
            bool available = (npcSlot >= 0 &&
                              npcSlot < kPartyRosterSlotCount &&
                              PartyTableIsNPCAvailable(npcSlot));
            if (!inActiveParty && !available) return true;
        }
    }
    // WorkbenchUpgrade slot buttons (per-game ids — see
    // IsWorkbenchUpgradeSlotButtonId) that the engine has marked
    // non-interactive (bit_flags & 0x2 == 0). For a 3-slot
    // ranged weapon (saber=3) these are the 4 Kristall positions;
    // for a 4-slot saber/double-shaft (saber=2) they are the 3
    // Aufwertungs positions. Sighted players see them greyed; a
    // keyboard navigator has nothing to do with them — OnSlotSelected
    // gates on is_active so even pressing Enter does nothing useful.
    // Dropping them from the chain lets arrow-down go straight from
    // the last applicable slot to BTN_ASSEMBLE.
    if (pk == PanelKind::WorkbenchUpgrade &&
        acc::engine::IsWorkbenchUpgradeSlotButtonId(cid)) {
        uint32_t bf = 0;
        __try {
            bf = *reinterpret_cast<uint32_t*>(
                reinterpret_cast<unsigned char*>(c) + kControlBitFlagsOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            bf = 0;
        }
        if ((bf & 0x2) == 0) return true;
    }
    // Universal close-button filter (language-agnostic). Every
    // standalone "close/back" button the engine ships across
    // sub-screens — whatever the .gui names it (BTN_EXIT / BTN_BACK /
    // BTN_CANCEL / BTN_Cancel) — renders caption strref 1582. Esc
    // already dismisses each of these panels (HandleEsc →
    // FindCancelButton/FindCloseButton scan panel.controls directly, so
    // they still find it after we drop it from the chain), making the
    // button redundant for keyboard nav. Match the engine's *resolved*
    // 1582 text rather than the +0x174 strref field — the engine
    // renders these captions via gui_string and frequently leaves the
    // strref slot empty, so a text compare is the reliable signal.
    // Gated to plain buttons (AsButton): close buttons are never
    // toggles/sliders, and this keeps ReadButtonText off slider structs.
    // This subsumes the per-panel Store/Equip/Options Schliess. filters
    // above; those stay for their non-1582 siblings (Equip OK, Store
    // Verkaufsliste/Kaufen) and as the Esc-routing anchors.
    //
    // EqualsTrimmed, not strcmp: KOTOR 2 renders this caption as
    // "\x11 Schliessen" — an Aspyr controller-glyph marker, then a pad space,
    // then the word — against a dialog.tlk that says "Schliessen". A
    // byte-exact compare never matched, so the close button survived in every
    // K2 sub-screen chain (patch-20260806-173225.log: `[5] button
    // text="\x11 Schliessen"`, and not one "filter close button" line in the
    // whole session). ReadGuiString takes the 0x11 off, this takes the space
    // off; both halves are needed. KOTOR 1 emits neither, which is why the
    // same code was correct there for months.
    if (haveCloseCaption &&
        CallDowncast(c, kVtableAsButton) != nullptr) {
        char btnText[64];
        if (ReadButtonText(c, btnText, sizeof(btnText)) &&
            EqualsTrimmed(btnText, closeCaption)) {
            acclog::Write("Menus.Chain",
                          "filter close button panel=%p ctrl=%p "
                          "text=\"%s\" (TLK %u)",
                          panel, c, btnText, kCloseButtonStrRef);
            return true;
        }
    }
    return false;
}

    // Listbox dispatch:
    //   * size > 1  — recurse one level into button children (sub-dialog
    //     settings list).
    //   * size == 1 in a modal popup — promote the listbox to a text-only
    //     chain entry so arrow keys can land for re-announce.
    //   * size == 1 elsewhere — descriptive label blob; skipped.
void AppendListBoxChildren(void* panel, void* c, void* equipPickerLb,
                           void* workbenchUpgradeLb, bool modalText) {
    void** vt = *reinterpret_cast<void***>(c);
    if (reinterpret_cast<uintptr_t>(vt) == kVtableListBox) {
        // Store mode filter: the engine keeps BOTH shopitems and
        // invitems listboxes in panel.controls regardless of which
        // is currently visible. Walking the hidden one's children
        // bleeds unreachable rows into the chain — the user nav
        // would land on items they can neither examine nor trade.
        // Skip the listbox entirely if it's the hidden one. The
        // three action buttons live in panel.controls (not in a
        // listbox) so they stay in the chain.
        if (acc::menus::store::IsHiddenStoreListBox(panel, c)) {
            return;
        }
        if (c == equipPickerLb) {
            return;
        }
        if (c == workbenchUpgradeLb) {
            return;
        }
        // The journal's quest list (items_listbox @ panel+0x5c4) must
        // expose its rows even when only ONE quest is active — otherwise
        // the lone entry (the norm in the Endar Spire tutorial, where the
        // sole quest is "Angriff auf die Endar Spire") is unreachable and
        // the user hears no journal content. Its rows carry the dedicated
        // journal-entry Enter handler (isJournalRow below), so descend via
        // AppendChainEntry exactly like the size>1 path. The description
        // listbox (+0x1a4) is a different control and stays skipped.
        const bool isJournalItemsLb =
            IdentifyPanel(panel) == PanelKind::InGameJournal &&
            c == reinterpret_cast<unsigned char*>(panel) +
                     kJournalItemsListBoxOffset;
        // Sort anchor for every row this listbox contributes. Row extents are
        // listbox-local content coordinates that keep climbing one row-pitch
        // at a time whether or not the row is inside the viewport, so they are
        // not comparable with the panel buttons' screen coordinates and must
        // not go into the y-sort. The listbox itself IS a panel-direct child
        // with a screen-absolute extent, so its top places the whole block
        // where the list actually sits — above every control below it, at any
        // row count. Falls back to per-row cy if the extent is degenerate.
        int blockSortCy = kSortByOwnCy;
        {
            int lbCx, lbCy;
            if (GetControlCenter(c, lbCx, lbCy)) {
                blockSortCy = *reinterpret_cast<int*>(
                    reinterpret_cast<unsigned char*>(c) +
                    kControlExtentOffset + sizeof(int));   // extent.top
            }
        }
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(c) + kListBoxControlsOffset);
        if (lbList && lbList->data) {
            if (lbList->size > 1 ||
                (isJournalItemsLb && lbList->size == 1)) {
                // Cap the per-listbox walk at the chain bound — AppendChain
                // Entry self-stops there anyway, so anything past it would
                // spin the loop for nothing. Using kMaxChainEntries (not a
                // separate magic number) keeps the two limits in lockstep,
                // so raising the chain size lifts the item ceiling with it.
                int lbN = lbList->size > kMaxChainEntries
                              ? kMaxChainEntries
                              : lbList->size;
                for (int j = 0; j < lbN; ++j) {
                    AppendChainEntry(lbList->data[j], blockSortCy);
                }
            } else if (lbList->size == 1 && modalText) {
                AppendChainTextOnly(c, panel);
            }
        }
    }
}

// Shared body of the four virtual-row anchors below (credits, charsheet
// stat rows, pazaak wager, equip stats). All four register a label control
// the engine renders for sighted players but that IsChainNavigable rejects,
// so the chain walker would otherwise skip it.
//
// cx comes from the real label position so a cursor warp on chain step
// lands the mouse on the actual on-screen text; cy is overridden to the
// caller's synthetic sortCy so the y-sort produces the logical reading
// order instead of interleaving with the real buttons.
//
// `probe` is the row's own extractor, called only to answer "does this row
// have text yet". A label can be present but empty — a mid-frame race
// during a re-snapshot, or a field the engine has not populated. Those are
// skipped silently and reappear on the next rebind.
typedef bool (*RowProbe)(void* panel, void* labelControl);

bool AppendVirtualRow(void* labelControl, int sortCy, void* panel,
                      RowProbe probe) {
    if (g_chainCount >= kMaxChainEntries) return false;
    int cx, cy;
    if (!GetControlCenter(labelControl, cx, cy)) {
        cx = 0;
    }
    if (!probe(panel, labelControl)) return true;
    g_chain[g_chainCount++] = {
        labelControl, cx, sortCy, sortCy, /*textOnly=*/true
    };
    return true;
}

bool ProbeCreditsRow(void* panel, void* labelControl) {
    char probe[8];
    return acc::menus::credits::ExtractCreditsRow(
        panel, labelControl, probe, sizeof(probe));
}

bool ProbeStatRow(void* panel, void* labelControl) {
    char probe[8];
    return acc::menus::charsheet::ExtractStatRow(
        panel, labelControl, probe, sizeof(probe));
}

bool ProbeWagerRow(void* panel, void* labelControl) {
    char probe[8];
    return acc::menus::extract::FromControl(
        labelControl, probe, sizeof(probe), panel) != nullptr;
}

bool ProbeEquipStatRow(void* panel, void* labelControl) {
    char probe[8];
    return acc::menus::equipstats::ExtractEquipStatRow(
        panel, labelControl, probe, sizeof(probe));
}

// Virtual credits row for Inventory + Store. credits_value_label isn't
// IsChainNavigable, so without this the user can't reach the gold display
// the engine renders for sighted players. ForEachCreditsRowAnchor is a
// no-op for unsupported panel kinds, so we call it unconditionally.
//
// Registered BEFORE the control/listbox walk below — not after, like the
// other per-kind virtual rows — because Inventory's item listbox can
// append 60+ entries and fill the chain to kMaxChainEntries; a credits
// row queued afterwards silently lost its slot to the cap and vanished
// once the player's inventory grew large (Store, with its smaller shop
// listbox, stayed under the cap and kept working). The cy-sort below
// still lands credits at the top via its synthetic sortCy regardless of
// insertion order.
bool OnCreditsAnchor(void* labelControl, int sortCy, void* userData) {
    return AppendVirtualRow(labelControl, sortCy, userData, ProbeCreditsRow);
}

// Per-kind virtual chain entries. For InGameCharacter the panel
// hosts a dense value-label cluster (Klasse, Stufe, Erfahrung, HP,
// FP, six attributes) that the snapshot announce already covers
// but the chain doesn't expose — labels aren't IsChainNavigable.
// ForEachStatRowAnchor visits each value-label control; we register
// it as a text-only chain entry at its real (cx, cy). The y-sort
// below then drops them into top-to-bottom reading order alongside
// the real buttons (Autom. Levelaufst., Levelaufst., bottom-row
// navigation).
//
// Text-only flag means Enter re-announces (calls AnnounceControl
// again) instead of firing vtable[15] — safe for label controls
// that have no activate handler. FromControl routes through
// ExtractStatRow at section 0 so the user hears the composed phrase
// ("Stärke 14, +2") rather than the bare label text ("14").
bool OnStatRowAnchor(void* labelControl, int sortCy, void* userData) {
    return AppendVirtualRow(labelControl, sortCy, userData, ProbeStatRow);
}

// Virtual wager row for the Pazaak wager popup. Same shape as credits:
// the maximum_label isn't IsChainNavigable, so without this the user
// can't reach the wager / max / credits readout. The anchor is a no-op
// for non-PazaakWager panels.
bool OnWagerAnchor(void* labelControl, int sortCy, void* userData) {
    return AppendVirtualRow(labelControl, sortCy, userData, ProbeWagerRow);
}

// Virtual stat rows for the Equip panel (Vitality, Defense, Attack,
// Damage). Same shape as the credits anchor — value labels live
// inline in CSWGuiInGameEquip but aren't IsChainNavigable, so the
// chain walker would skip them. ForEachEquipStatRowAnchor self-gates
// on InGameEquip (no-op elsewhere) and emits sortCy values above
// every real button so stats land at the END of the chain after the
// slots + Back / Change* buttons.
bool OnEquipStatAnchor(void* labelControl, int sortCy, void* userData) {
    return AppendVirtualRow(labelControl, sortCy, userData, ProbeEquipStatRow);
}

// Virtual "Mod settings" entry for InGameOptions + MainMenuOptions.
// Same shape as the credits / stat-row anchors, but the control
// pointer is a static sentinel (acc::menus::modsettings::GetRoot
// Anchor) rather than an engine label — we never need to read /
// dispatch through it as if it were a real CSWGuiControl. Position
// is synthetic: sortCy=9000 lands the entry at the end of the
// chain, after every real button on the Optionen strip.
bool OnModSettingsAnchor(void* sentinel, int sortCx, int sortCy,
                         void* /*userData*/) {
    if (g_chainCount >= kMaxChainEntries) return false;
    g_chain[g_chainCount++] = {
        sentinel, sortCx, sortCy, sortCy,
        /*textOnly=*/false,
        /*virtualKind=*/kVirtualMod_SettingsRoot
    };
    return true;
}

// Per-kind virtual chain entries for StatusSummary — the engine's quest-
// progress / journal-entry info popup. Its body is a cluster of label
// controls, one per notification type (journal entry, credits, XP, DS/LS
// points, items gained/lost, stealth XP), NOT a listbox, so the listbox
// text-only path above never exposed it and the user only reached OK.
// The engine shows exactly the row(s) that apply and leaves the rest as
// hidden templates whose text still reads "<CUSTOM0>"; only the shown
// rows carry the CSWGuiControl visible bit (kControlVisibleBit at +0x44).
// Expose just those as text-only entries so the user can arrow back and
// re-read, and so the substituted value ("Erfahrungspunkte erhalten: 45")
// is what's read rather than the placeholder. The y-sort below drops them
// above OK (cy ~25 vs ~51) into natural reading order.
void AppendStatusSummaryRows(void* panel, CExoArrayList* list, int n) {
if (IdentifyPanel(panel) == PanelKind::StatusSummary) {
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        if (CallDowncast(c, kVtableAsLabel) == nullptr) continue;
        uint32_t bitFlags = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(c) + kControlBitFlagsOffset);
        if ((bitFlags & kControlVisibleBit) == 0) continue;
        AppendChainTextOnly(c, panel);  // self-skips empty text / no extent
    }
}
}

// Reading-order comparator: y first, then x — but the x tiebreak applies
// only when BOTH entries are geometrically ordered (see
// ChainEntry::geometricOrder). Listbox rows and virtual rows share a single
// sortCy by design and keep their insertion order, so mixing an x compare
// into their block would scramble it.
bool ChainEntryPrecedes(const ChainEntry& a, const ChainEntry& b) {
    if (a.sortCy != b.sortCy) return a.sortCy < b.sortCy;
    if (!a.geometricOrder || !b.geometricOrder) return false;
    return a.cx < b.cx;
}

// Insertion sort in reading order (ChainEntryPrecedes). Stable; the n^2 is
// cheap int compares and runs once per rebind (panel open / content change),
// not per tick, so it stays well within budget even at a full
// kMaxChainEntries chain.
//
// Stability is load-bearing, not incidental: every row of one listbox shares
// a single sortCy AND is flagged non-geometric, so the comparator never
// separates them and their relative order is decided purely by the order
// AppendListBoxChildren walked them — i.e. the engine's own row order.
void SortChainBySortCy() {
for (int i = 1; i < g_chainCount; ++i) {
    for (int j = i; j > 0 && ChainEntryPrecedes(g_chain[j], g_chain[j-1]); --j) {
        ChainEntry tmp = g_chain[j];
        g_chain[j]   = g_chain[j-1];
        g_chain[j-1] = tmp;
    }
}
}

// Squash cycle-arrow flankers from the chain. Empty-text navigable
// entries that share a y-row with a NEARBY text-bearing entry are
// cycle arrows; the user reaches them via Left/Right cycle dispatch
// on the value-display entry. Lone empty-text entries with no nearby
// text-bearing same-row neighbour are kept.
void SquashCycleFlankers(void* panel) {
    // Title-screen Options sub-screens (Advanced Sound EAX, Advanced
    // Graphics AA/texture/anisotropy, Game Settings difficulty) lay their
    // spinner rows out as a centred value button flanked by empty
    // left/right arrow buttons at x=72 / x=288 — ~108 px from the value at
    // x=180, beyond the 80 px reach that catches chargen's tighter
    // spinners. Widen the squash for just these kinds so the redundant
    // arrows (the user cycles the value with Left/Right) drop out of Up/Down
    // nav instead of speaking "control N". Gated by kind so chargen and
    // every other panel keep the conservative 80 px reach.
    //
    // Both reaches, and the same-row tolerance below, are quoted in .gui
    // authoring units — they were measured off KOTOR 1's .gui files — so
    // they go through ScaleGuiThresholdPx to survive KOTOR 2's stretched
    // extents. Without that the chargen steppers (27 authored units apart,
    // 97 at 2880x1800) and the options spinners (108 authored, ~389) both
    // fall outside the reach and announce as "control N".
    const int kSquashDxMax = ScaleGuiThresholdPx(
        acc::engine::IsMainMenuOptionsSubScreen(IdentifyPanel(panel))
            ? 130 : 80);
    const int kSquashDyMax = ScaleGuiThresholdPx(5);
    int writeIdx = 0;
    for (int i = 0; i < g_chainCount; ++i) {
        char tmp[64];
        bool hasText = acc::menus::extract::FromControl(g_chain[i].control,
                                               tmp, sizeof(tmp),
                                               panel) != nullptr;
        if (hasText) {
            g_chain[writeIdx++] = g_chain[i];
            continue;
        }
        bool sameRowWithText = false;
        for (int j = 0; j < g_chainCount; ++j) {
            if (j == i) continue;
            int dy = g_chain[j].cy - g_chain[i].cy;
            if (dy < 0) dy = -dy;
            if (dy > kSquashDyMax) continue;
            int dx = g_chain[j].cx - g_chain[i].cx;
            if (dx < 0) dx = -dx;
            if (dx == 0 || dx > kSquashDxMax) continue;
            char tmp2[64];
            if (acc::menus::extract::FromControl(g_chain[j].control,
                                        tmp2, sizeof(tmp2),
                                        panel) != nullptr) {
                sameRowWithText = true;
                break;
            }
        }
        if (!sameRowWithText) {
            g_chain[writeIdx++] = g_chain[i];
        }
    }
    g_chainCount = writeIdx;
}

// Row pitch between InGameEquip slot buttons, used to compensate the
// simulated click position. Zero when the panel is not InGameEquip or
// fewer than two slots made it into the chain.
void ComputeEquipSlotClickOffset(void* panel) {
g_equipSlotClickOffsetY = 0;
if (IdentifyPanel(panel) == PanelKind::InGameEquip) {
    int firstSlotIdx = -1;
    int firstSlotY   = 0;
    for (int i = 0; i < g_chainCount; ++i) {
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(g_chain[i].control) + kControlIdOffset);
        if (!IsEquipSlotButtonId(cid)) continue;
        if (firstSlotIdx < 0) {
            firstSlotIdx = i;
            firstSlotY   = g_chain[i].cy;
        } else if (g_chain[i].cy != firstSlotY) {
            int spacing = g_chain[i].cy - firstSlotY;
            if (spacing < 0) spacing = -spacing;
            g_equipSlotClickOffsetY = spacing;
            break;
        }
    }
}
}

// Column pitch between chargen class-selection icons, same purpose as
// ComputeEquipSlotClickOffset above.
//
// KOTOR 1 ONLY. The compensation exists because KOTOR 1's classsel.gui
// hit-test resolves one column to the RIGHT of each icon's own extent:
// warping to BTN_SEL2's centre (x=189) hovers BTN_SEL1, so the warp has to
// aim at x=276 to land on BTN_SEL2 (patch-20260731-140902.log: every
// MoveMouseToPosition on that panel is centre + 87 and every one reports
// `after == target`). KOTOR 2 has no such shift — the same log line on K2
// reads `MoveMouseToPosition(1257,769) target=<SEL2> after=<SEL3>`, i.e.
// the K1 compensation overshoots by exactly one column, which is why class
// selection there skipped icons, needed repeated presses, and left the
// per-icon class-label cache populated for whichever icon the cursor
// actually hit (so the focused one announced as silence).
void ComputeClassIconClickOffset(void* panel) {
g_classIconClickOffsetX = 0;
if (acc::game::IsKotor2()) return;
{
    void** pVt = panel ? *reinterpret_cast<void***>(panel) : nullptr;
    if (reinterpret_cast<uintptr_t>(pVt) ==
            kVtableCSWGuiClassSelection) {
        int firstIconIdx = -1;
        for (int i = 0; i < g_chainCount; ++i) {
            if (!IsClassSelectionIcon(panel, g_chain[i].control)) continue;
            if (firstIconIdx < 0) {
                firstIconIdx = i;
            } else if (g_chain[i].cy == g_chain[firstIconIdx].cy) {
                int spacing = g_chain[i].cx - g_chain[firstIconIdx].cx;
                if (spacing < 0) spacing = -spacing;
                if (spacing > 0) g_classIconClickOffsetX = spacing;
                break;
            }
        }
    }
}
}

// Per-rebind chain dump. One line for the rebind plus one per entry;
// this is the trace we read back when a control goes missing from
// arrow navigation or announces the wrong text.
void LogChainContents(void* panel, void* active) {
acclog::Write("Menus.Chain", "rebind panel=%p count=%d index=%d active=%p "
              "equipSlotOffsetY=%d classIconOffsetX=%d",
              panel, g_chainCount, g_chainIndex, active,
              g_equipSlotClickOffsetY, g_classIconClickOffsetX);
for (int i = 0; i < g_chainCount; ++i) {
    char text[256];
    const char* src = acc::menus::extract::FromControl(g_chain[i].control,
                                              text, sizeof(text),
                                              panel);
    // Per-game offsets, not literals: on KOTOR 2 both fields sit +4 and the
    // hardcoded KOTOR 1 values printed a neighbouring dword, which made
    // every K2 chain dump look like garbage flags (bit_flags=0x10,
    // is_active=434) and sent one investigation chasing a phantom.
    unsigned int isActive =
        *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(g_chain[i].control) +
            kControlIsActiveOffset);
    unsigned int bitFlags =
        *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(g_chain[i].control) +
            kControlBitFlagsOffset);
    // sortCy is only printed when it differs from cy — i.e. for virtual rows
    // and listbox blocks — so the common case stays as terse as before.
    char sortNote[32];
    sortNote[0] = '\0';
    if (g_chain[i].sortCy != g_chain[i].cy) {
        snprintf(sortNote, sizeof(sortNote), " sort=%d", g_chain[i].sortCy);
    }
    acclog::Write("Menus.Chain", "  [%d] %p (%d,%d)%s%s %s text=\"%s\" is_active=%u bit_flags=0x%x",
                  i, g_chain[i].control, g_chain[i].cx, g_chain[i].cy,
                  sortNote,
                  g_chain[i].textOnly ? " text-only" : "",
                  src ? src : "?", src ? text : "", isActive, bitFlags);
}
}

}  // namespace

// ============================================================================
// RebindChain — the heart of chain navigation. Walks panel.controls,
// recurses into sub-dialog listboxes, sorts by visual y, squashes
// cycle-arrow flankers, computes click-offset compensations, anchors the
// cursor on the engine's current activeControl.
// ============================================================================

void RebindChain(void* panel) {
    g_chainPanel  = panel;
    g_chainIndex  = 0;
    g_chainCount  = 0;
    if (!panel) return;

    // First-sight walk + cycle-category capture, normally driven by
    // OnSetActiveControl. A panel can reach the user's foreground without
    // the engine ever firing SetActiveControl on it — KOTOR 2 pushes every
    // chargen wizard step (abchrgen_p, skchrgen_p, portcust_p) straight out
    // of the parent step-list's button handler, so the walk never ran and
    // the routing layer logged "fg=<step> current=<parent>" for the whole
    // visit. The visible cost was the Attribute / Fähigkeiten rows speaking
    // a bare "8" instead of "Stärke, 8": CaptureLabels lives at the end of
    // this walk and binds ability_labels[i] onto ability_buttons[i].
    //
    // Placed ahead of the control walk below so the capture is in the cache
    // before this rebind's own FromControl calls (squash + chain dump) read
    // it. Idempotent — WalkAndCaptureOnFirstSight self-guards on a
    // last-panel latch, so on KOTOR 1 (where SetActiveControl already ran
    // for this panel) it is an immediate no-op.
    acc::menus::focus::WalkAndCaptureOnFirstSight(panel);

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;

    bool  modalText           = IsModalTextPanel(IdentifyPanel(panel));
    void* portraitChargenSkip = ResolvePortraitChargenSkip(panel);
    void* equipPickerLb       = ResolveEquipPickerListBox(panel);
    void* workbenchUpgradeLb  = ResolveWorkbenchUpgradeListBox(panel);

    void* storeCancelBtn = nullptr;
    void* storeToggleBtn = nullptr;
    void* storeAcceptBtn = nullptr;
    ResolveStoreActionButtons(panel, storeCancelBtn, storeToggleBtn,
                              storeAcceptBtn);

    // Resolve the engine's localized close caption (strref 1582 →
    // "Schliess."/"Close"/…) once per rebind, for the universal
    // close-button filter in isDecorative below. Empty on TLK miss, which
    // disables the filter (fail-open — never hides a real action button).
    char closeCaption[64];
    bool haveCloseCaption =
        LookupTlk(kCloseButtonStrRef, closeCaption, sizeof(closeCaption)) &&
        closeCaption[0] != '\0';

    // Credits row FIRST, before the control/listbox walk: Inventory's item
    // listbox can append 60+ entries and fill the chain to kMaxChainEntries,
    // and a credits row queued afterwards silently lost its slot to the cap.
    // The cy-sort below still lands it at the top via its synthetic sortCy.
    acc::menus::credits::ForEachCreditsRowAnchor(panel, OnCreditsAnchor, panel);

    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        if (c == portraitChargenSkip) continue;
        if (c == storeCancelBtn || c == storeToggleBtn ||
            c == storeAcceptBtn) continue;

        if (IsChainNavigable(c)) {
            if (IsDecorativeControl(panel, c, closeCaption, haveCloseCaption)) {
                continue;
            }
            AppendChainEntry(c);
            continue;
        }
        AppendListBoxChildren(panel, c, equipPickerLb, workbenchUpgradeLb,
                              modalText);
    }

    // The remaining virtual rows. Each ForEach*Anchor is a no-op on panel
    // kinds it does not own, except the charsheet one which is gated here.
    if (IdentifyPanel(panel) == PanelKind::InGameCharacter) {
        acc::menus::charsheet::ForEachStatRowAnchor(panel, OnStatRowAnchor,
                                                    panel);
    }
    AppendStatusSummaryRows(panel, list, n);
    acc::menus::extract::ForEachWagerRowAnchor(panel, OnWagerAnchor, panel);
    acc::menus::equipstats::ForEachEquipStatRowAnchor(
        panel, OnEquipStatAnchor, panel);
    acc::menus::modsettings::ForEachRootAnchor(panel, OnModSettingsAnchor,
                                               panel);

    SortChainBySortCy();
    SquashCycleFlankers(panel);

    // Tab-cluster Y offset is computed on demand at warp/click-sim time via
    // ComputeTabClickOffsetY — see the header comment for the race that made
    // an eager rebind-time computation unreliable.
    ComputeEquipSlotClickOffset(panel);
    ComputeClassIconClickOffset(panel);

    // Anchor at active.
    void* active = ReadPanelActiveControl(panel);
    int   idx    = FindChainEntry(active);
    g_chainIndex = (idx >= 0) ? idx : 0;

    // Mirror initial chain focus into the chargen Attributes panel's
    // selected_ability so a Left/Right press immediately after the panel
    // opens (without first stepping with Up/Down) modifies the right
    // ability. No-op on every other panel. The chain-step handler in
    // menus.cpp keeps the field in sync as the user navigates.
    acc::menus::chargen_attr::SyncSelectedAbilityFromChainFocus();
    // Same on the Skills panel.
    acc::menus::chargen_skills::SyncSelectedSkillFromChainFocus();

    LogChainContents(panel, active);
}

}  // namespace acc::menus::chain

// True iff cid is one of the equip screen's slot buttons. Declared in
// menus_internal.h at global scope (like the id constants themselves); the
// ids resolve per game, and the K2-only second-weapon-set pair is -1 on K1
// so the extra compares are inert there.
bool IsEquipSlotButtonId(int cid) {
    return cid == kEquipBtnHeadId    || cid == kEquipBtnImplantId ||
           cid == kEquipBtnBodyId    || cid == kEquipBtnArmLId    ||
           cid == kEquipBtnArmRId    || cid == kEquipBtnWeapLId   ||
           cid == kEquipBtnWeapRId   || cid == kEquipBtnBeltId    ||
           cid == kEquipBtnHandsId   || cid == kEquipBtnWeapL2Id  ||
           cid == kEquipBtnWeapR2Id;
}
