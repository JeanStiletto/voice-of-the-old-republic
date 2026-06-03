// chain-navigation state and helpers.
//
// here and what stays in menus.cpp. Function bodies are unchanged from
// the original menus.cpp inline definitions; only the namespacing /
// linkage changes.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_chain.h"

#include "engine_input.h"
#include "engine_manager.h"
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
#include "menus_internal.h"
#include "menus_journal.h"   // IsJournalEntry, SpeakDescription
#include "menus_listbox.h"   // IsWorkbenchUpgradePickerArmed, ArmEquipPicker, ArmWorkbenchUpgradePicker
#include "menus_modsettings.h"
#include "menus_monitors.h"  // AnnounceControl
#include "menus_pazaakdeck.h"
#include "menus_pending.h"   // QueueActivate, IsPending, QueueMoveCursor, Queue{ClickAt,EquipSelect,WorkbenchSlotSelect,StoreItemActivate}
#include "menus_store.h"
#include "peek_description.h" // SpeakItemRowDescription — quest-item Enter
#include "prism.h"
#include "strings.h"

using namespace acc::engine;  // IdentifyPanel, PanelKind, kInput*, etc.

using acc::menus::detail::IsChainNavigable;
using acc::menus::detail::IsClassSelectionIcon;
using acc::menus::detail::GetControlCenter;

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
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return false;
    int n = panelCount > 16 ? 16 : panelCount;
    for (int i = 0; i < n; ++i) {
        if (panelData[i] == panel) return true;
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

    int n = list->size > 256 ? 256 : list->size;
    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c || c == focused) continue;
        if (!IsChainNavigable(c)) continue;

        int cx, cy;
        if (!GetControlCenter(c, cx, cy)) continue;
        if (cy - focusCy > 5 || focusCy - cy > 5) continue;

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

void AppendChainEntry(void* control) {
    if (g_chainCount >= kMaxChainEntries) return;
    if (!IsChainNavigable(control))       return;
    int cx, cy;
    if (!GetControlCenter(control, cx, cy)) return;
    g_chain[g_chainCount++] = { control, cx, cy, /*textOnly=*/false };
}

void AppendChainTextOnly(void* control, void* panel) {
    if (g_chainCount >= kMaxChainEntries) return;
    if (!control) return;
    char tmp[512];
    if (!acc::menus::extract::FromControl(control, tmp, sizeof(tmp), panel)) return;
    int cx, cy;
    if (!GetControlCenter(control, cx, cy)) return;
    g_chain[g_chainCount++] = { control, cx, cy, /*textOnly=*/true };
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

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(panel) + kPanelControlsOffset);
    if (!list->data || list->size <= 0) return;
    int n = list->size > 256 ? 256 : list->size;

    bool modalText = IsModalTextPanel(IdentifyPanel(panel));

    // Panel-aware chain filter: in CSWGuiPortraitCharGen we anchor the chain
    // on the left_arrow alone (announces the current portrait via the
    // PerKind path in menus_extract.cpp's section 9d) and consolidate the
    // right_arrow out of the chain entirely. The user lands on one entry,
    // hears the value, and presses Left/Right to cycle — matching the
    // existing `[◀] value [▶]` UX without needing the head_3d_scene_control
    // (a non-button, IsChainNavigable would reject it) as a chain anchor.
    void* portraitChargenSkip = nullptr;
    {
        void** pVt = *reinterpret_cast<void***>(panel);
        if (reinterpret_cast<uintptr_t>(pVt) ==
                kVtableCSWGuiPortraitCharGen) {
            portraitChargenSkip = reinterpret_cast<unsigned char*>(panel) +
                                  kPortraitRightArrowOffset;
        }
    }

    // Resolve the engine's localized close caption (strref 1582 →
    // "Schliess."/"Close"/…) once per rebind, for the universal
    // close-button filter in isDecorative below. Empty on TLK miss, which
    // disables the filter (fail-open — never hides a real action button).
    char closeCaption[64];
    bool haveCloseCaption =
        LookupTlk(kCloseButtonStrRef, closeCaption, sizeof(closeCaption)) &&
        closeCaption[0] != '\0';

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
    auto isDecorative = [&](void* c) -> bool {
        PanelKind pk = IdentifyPanel(panel);
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(c) + kControlIdOffset);
        // Pazaak deck builder: drop the overlay value/count/title labels and
        // the unaddable (zero-owned) available cards.
        if (acc::menus::pazaakdeck::IsChainDecorative(panel, c)) return true;
        if (pk == PanelKind::InGameCharacter &&
            (cid == 1 || cid == 64 || cid == 65 || cid == 66 || cid == 67)) {
            return true;
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
            if (reinterpret_cast<uintptr_t>(vt) == 0x00756BB8) {
                constexpr size_t kPartyPortraitPartyIdOffset = 0x44c;
                constexpr size_t kPartyPortraitNpcSlotOffset = 0x450;
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
        // WorkbenchUpgrade slot buttons (cid 12..18) that the engine has
        // marked non-interactive (bit_flags & 0x2 == 0). For a 3-slot
        // ranged weapon (saber=3) these are the 4 Kristall positions;
        // for a 4-slot saber/double-shaft (saber=2) they are the 3
        // Aufwertungs positions. Sighted players see them greyed; a
        // keyboard navigator has nothing to do with them — OnSlotSelected
        // gates on is_active so even pressing Enter does nothing useful.
        // Dropping them from the chain lets arrow-down go straight from
        // the last applicable slot to BTN_ASSEMBLE.
        if (pk == PanelKind::WorkbenchUpgrade &&
            cid >= 12 && cid <= 18) {
            uint32_t bf = 0;
            __try {
                bf = *reinterpret_cast<uint32_t*>(
                    reinterpret_cast<unsigned char*>(c) + 0x44);
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
        if (haveCloseCaption &&
            CallDowncast(c, kVtableAsButton) != nullptr) {
            char btnText[64];
            if (ReadButtonText(c, btnText, sizeof(btnText)) &&
                strcmp(btnText, closeCaption) == 0) {
                acclog::Write("Menus.Chain",
                              "filter close button panel=%p ctrl=%p "
                              "text=\"%s\" (TLK %u)",
                              panel, c, btnText, kCloseButtonStrRef);
                return true;
            }
        }
        return false;
    };

    // Store-panel action buttons (Schliess. / Verkaufsliste / Kaufen).
    // These live at fixed struct offsets and we drive them via dedicated
    // hotkeys (Esc to close, G to toggle mode, Enter on item to trade)
    // rather than chain navigation. Walking past the inventory rows into
    // them just adds three dead entries the user has to skip, so filter
    // them out of the chain entirely.
    void* storeCancelBtn  = nullptr;
    void* storeToggleBtn  = nullptr;
    void* storeAcceptBtn  = nullptr;
    if (acc::menus::store::IsStorePanel(panel)) {
        auto* p = reinterpret_cast<unsigned char*>(panel);
        storeCancelBtn = p + kStoreCancelButtonOffset;
        storeToggleBtn = p + kStoreToggleButtonOffset;
        storeAcceptBtn = p + kStoreAcceptButtonOffset;
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
    void* equipPickerLb = nullptr;
    if (IdentifyPanel(panel) == PanelKind::InGameEquip) {
        equipPickerLb = acc::menus::detail::FindControlById(
            panel, kEquipLbItemsId);
    }

    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        if (c == portraitChargenSkip) continue;
        if (c == storeCancelBtn || c == storeToggleBtn ||
            c == storeAcceptBtn) continue;

        if (IsChainNavigable(c)) {
            if (isDecorative(c)) continue;
            AppendChainEntry(c);
            continue;
        }

        // Listbox dispatch:
        //   * size > 1  — recurse one level into button children (sub-dialog
        //     settings list).
        //   * size == 1 in a modal popup — promote the listbox to a text-only
        //     chain entry so arrow keys can land for re-announce.
        //   * size == 1 elsewhere — descriptive label blob; skipped.
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
                continue;
            }
            if (c == equipPickerLb) {
                continue;
            }
            auto* lbList = reinterpret_cast<CExoArrayList*>(
                reinterpret_cast<unsigned char*>(c) + kListBoxControlsOffset);
            if (lbList && lbList->data) {
                if (lbList->size > 1) {
                    int lbN = lbList->size > 256 ? 256 : lbList->size;
                    for (int j = 0; j < lbN; ++j) {
                        AppendChainEntry(lbList->data[j]);
                    }
                } else if (lbList->size == 1 && modalText) {
                    AppendChainTextOnly(c, panel);
                }
            }
        }
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
    if (IdentifyPanel(panel) == PanelKind::InGameCharacter) {
        auto onAnchor = [](void* labelControl, int sortCy,
                           void* userData) -> bool {
            void* p = userData;
            if (g_chainCount >= kMaxChainEntries) return false;
            int cx, cy;
            // cx comes from the real label position so a cursor warp on
            // chain step lands the mouse on the actual on-screen text;
            // cy is overridden to sortCy so the y-sort produces the
            // logical reading order (Klasse → ... → Charisma at cy 1..11)
            // instead of interleaving with real buttons (cy 237+).
            if (!GetControlCenter(labelControl, cx, cy)) {
                cx = 0;
            }
            // Label may be present but empty (mid-frame race during a
            // re-snapshot, or a not-yet-populated field). Skip silently;
            // the row reappears on the next rebind once the label has
            // text.
            char probe[8];
            if (!acc::menus::charsheet::ExtractStatRow(
                    p, labelControl, probe, sizeof(probe))) {
                return true;
            }
            g_chain[g_chainCount++] = {
                labelControl, cx, sortCy, /*textOnly=*/true
            };
            return true;
        };
        acc::menus::charsheet::ForEachStatRowAnchor(panel, onAnchor, panel);
    }

    // Virtual credits row for Inventory + Store. Same shape as the stat-row
    // block above: credits_value_label isn't IsChainNavigable, so without
    // this the user can't reach the gold display the engine renders for
    // sighted players. menus_credits::ForEachCreditsRowAnchor is a no-op
    // for unsupported panel kinds, so we call it unconditionally.
    {
        auto onCreditsAnchor = [](void* labelControl, int sortCy,
                                  void* userData) -> bool {
            void* p = userData;
            if (g_chainCount >= kMaxChainEntries) return false;
            int cx, cy;
            // cx from the on-screen label so a cursor warp on chain step
            // lands the mouse on the visible credits readout; cy is the
            // synthetic sortCy so the entry sorts above real buttons.
            if (!GetControlCenter(labelControl, cx, cy)) {
                cx = 0;
            }
            // Skip when the engine hasn't yet populated the value (gui_
            // string empty mid-frame). Row reappears on the next rebind.
            char probe[8];
            if (!acc::menus::credits::ExtractCreditsRow(
                    p, labelControl, probe, sizeof(probe))) {
                return true;
            }
            g_chain[g_chainCount++] = {
                labelControl, cx, sortCy, /*textOnly=*/true
            };
            return true;
        };
        acc::menus::credits::ForEachCreditsRowAnchor(panel, onCreditsAnchor,
                                                    panel);
    }

    // Virtual wager row for the Pazaak wager popup. Same shape as credits:
    // the maximum_label isn't IsChainNavigable, so without this the user
    // can't reach the wager / max / credits readout. The anchor is a no-op
    // for non-PazaakWager panels.
    {
        auto onWagerAnchor = [](void* labelControl, int sortCy,
                                void* userData) -> bool {
            void* p = userData;
            if (g_chainCount >= kMaxChainEntries) return false;
            int cx, cy;
            if (!GetControlCenter(labelControl, cx, cy)) cx = 0;
            // Skip until the panel fields are readable (wager < 0 / label
            // empty mid-frame). Row reappears on the next rebind.
            char probe[8];
            if (!acc::menus::extract::FromControl(labelControl, probe,
                                                  sizeof(probe), p)) {
                return true;
            }
            g_chain[g_chainCount++] = {
                labelControl, cx, sortCy, /*textOnly=*/true
            };
            return true;
        };
        acc::menus::extract::ForEachWagerRowAnchor(panel, onWagerAnchor, panel);
    }

    // Virtual stat rows for the Equip panel (Vitality, Defense, Attack,
    // Damage). Same shape as the credits anchor — value labels live
    // inline in CSWGuiInGameEquip but aren't IsChainNavigable, so the
    // chain walker would skip them. ForEachEquipStatRowAnchor self-gates
    // on InGameEquip (no-op elsewhere) and emits sortCy values above
    // every real button so stats land at the END of the chain after the
    // slots + Back / Change* buttons.
    {
        auto onEquipStatAnchor = [](void* labelControl, int sortCy,
                                    void* userData) -> bool {
            void* p = userData;
            if (g_chainCount >= kMaxChainEntries) return false;
            int cx, cy;
            if (!GetControlCenter(labelControl, cx, cy)) {
                cx = 0;
            }
            // Skip rows whose value the engine hasn't populated yet
            // (gui_string empty mid-frame). Re-emerges on next rebind
            // once the engine writes the value.
            char probe[8];
            if (!acc::menus::equipstats::ExtractEquipStatRow(
                    p, labelControl, probe, sizeof(probe))) {
                return true;
            }
            g_chain[g_chainCount++] = {
                labelControl, cx, sortCy, /*textOnly=*/true
            };
            return true;
        };
        acc::menus::equipstats::ForEachEquipStatRowAnchor(
            panel, onEquipStatAnchor, panel);
    }

    // Virtual "Mod settings" entry for InGameOptions + MainMenuOptions.
    // Same shape as the credits / stat-row anchors, but the control
    // pointer is a static sentinel (acc::menus::modsettings::GetRoot
    // Anchor) rather than an engine label — we never need to read /
    // dispatch through it as if it were a real CSWGuiControl. Position
    // is synthetic: sortCy=9000 lands the entry at the end of the
    // chain, after every real button on the Optionen strip.
    {
        auto onModSettingsAnchor = [](void* sentinel, int sortCx, int sortCy,
                                      void* /*userData*/) -> bool {
            if (g_chainCount >= kMaxChainEntries) return false;
            g_chain[g_chainCount++] = {
                sentinel, sortCx, sortCy,
                /*textOnly=*/false,
                /*virtualKind=*/kVirtualMod_SettingsRoot
            };
            return true;
        };
        acc::menus::modsettings::ForEachRootAnchor(
            panel, onModSettingsAnchor, panel);
    }

    // Insertion sort by cy ascending. Stable; n^2 is fine for n<=64.
    for (int i = 1; i < g_chainCount; ++i) {
        for (int j = i; j > 0 && g_chain[j].cy < g_chain[j-1].cy; --j) {
            ChainEntry tmp = g_chain[j];
            g_chain[j]   = g_chain[j-1];
            g_chain[j-1] = tmp;
        }
    }

    // Squash cycle-arrow flankers from the chain. Empty-text navigable
    // entries that share a y-row with a NEARBY text-bearing entry are
    // cycle arrows; the user reaches them via Left/Right cycle dispatch
    // on the value-display entry. Lone empty-text entries with no nearby
    // text-bearing same-row neighbour are kept.
    {
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
            constexpr int kSquashDxMax = 80;
            bool sameRowWithText = false;
            for (int j = 0; j < g_chainCount; ++j) {
                if (j == i) continue;
                int dy = g_chain[j].cy - g_chain[i].cy;
                if (dy < 0) dy = -dy;
                if (dy > 5) continue;
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

    // Tab-cluster Y offset is computed on demand at warp/click-sim time via
    // ComputeTabClickOffsetY — see the header comment for the race that made
    // an eager rebind-time computation unreliable.

    // Compute g_equipSlotClickOffsetY for InGameEquip panels.
    g_equipSlotClickOffsetY = 0;
    if (IdentifyPanel(panel) == PanelKind::InGameEquip) {
        int firstSlotIdx = -1;
        int firstSlotY   = 0;
        for (int i = 0; i < g_chainCount; ++i) {
            int cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + kControlIdOffset);
            bool isSlot =
                cid == kEquipBtnHeadId    || cid == kEquipBtnImplantId ||
                cid == kEquipBtnBodyId    || cid == kEquipBtnArmLId    ||
                cid == kEquipBtnArmRId    || cid == kEquipBtnWeapLId   ||
                cid == kEquipBtnWeapRId   || cid == kEquipBtnBeltId    ||
                cid == kEquipBtnHandsId;
            if (!isSlot) continue;
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

    // Compute g_classIconClickOffsetX for the chargen class-selection panel.
    g_classIconClickOffsetX = 0;
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

    acclog::Write("Menus.Chain", "rebind panel=%p count=%d index=%d active=%p "
                  "equipSlotOffsetY=%d classIconOffsetX=%d",
                  panel, g_chainCount, g_chainIndex, active,
                  g_equipSlotClickOffsetY, g_classIconClickOffsetX);
    for (int i = 0; i < g_chainCount; ++i) {
        char text[256];
        const char* src = acc::menus::extract::FromControl(g_chain[i].control,
                                                  text, sizeof(text),
                                                  panel);
        unsigned int isActive =
            *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x4c);
        unsigned int bitFlags =
            *reinterpret_cast<unsigned int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x44);
        acclog::Write("Menus.Chain", "  [%d] %p (%d,%d)%s %s text=\"%s\" is_active=%u bit_flags=0x%x",
                      i, g_chain[i].control, g_chain[i].cx, g_chain[i].cy,
                      g_chain[i].textOnly ? " text-only" : "",
                      src ? src : "?", src ? text : "", isActive, bitFlags);
    }
}

void HandleEnterActivation(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    if (code != kInputEnter1 && code != kInputEnter2) return;
    if (activePanel == nullptr) return;

    // Lazy rebind: previously Enter required a prior arrow press, which
    // stranded engine-pushed modals (StatusSummary after a skill check, the
    // quit-confirm MessageBox, AreaTransition prompts) where the chain was
    // bound to the previous panel. Mirroring HandleNavStep's rebind here lets
    // popups the engine pre-focuses (quit-confirm pre-focused on Abbrechen)
    // activate the focused button on Enter alone. RebindChain anchors
    // g_chainIndex on panel.activeControl when present.
    if (g_chainPanel != activePanel) {
        RebindChain(activePanel);
    }
    if (g_chainPanel != activePanel) return;          // rebind landed elsewhere
    if (g_chainCount <= 0) return;
    if (g_chainIndex < 0 || g_chainIndex >= g_chainCount) return;

    ChainEntry& e = g_chain[g_chainIndex];

    // Virtual chain entries route through their owning module BEFORE any
    // subclass-specific reads below — the entry's `control` field is a
    // sentinel pointer and any vtable / offset dereference would AV.
    // Currently only the mod-settings root entry; new virtual kinds add a
    // case here.
    if (e.virtualKind == kVirtualMod_SettingsRoot) {
        acc::menus::modsettings::OpenSubMenu(g_chainPanel);
        acclog::Write("Menus.Enter", "open mod-settings submenu (parent=%p)",
                      g_chainPanel);
        consumed = true;
        return;
    }

    bool isTabButton = false;
    if (g_tabbedPanel && g_tabsCount >= 2) {
        auto* tlist = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(g_tabbedPanel) + kPanelControlsOffset);
        if (tlist && tlist->data) {
            for (int i = g_tabsStart;
                 i < g_tabsStart + g_tabsCount && i < tlist->size; ++i) {
                if (tlist->data[i] == e.control) { isTabButton = true; break; }
            }
        }
    }

    // Detect equip-screen slot buttons up front. They need the full click
    // pipeline (cursor warp + LMouseDown/Up) to fire the engine's OnSelectSlot
    // — which is what populates LB_ITEMS with items matching the slot. Direct
    // vtable[15] activate on a slot button routes to a different handler
    // (likely OnEnterSlot, the keyboard shortcut path) that pops a "no items"
    // modal instead of populating the picker. Same gate-mismatch shape as
    // Options tab buttons: the mouse path is the only one that triggers the
    // populate.
    bool isEquipSlot = false;
    int  equipSlotCid = 0;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameEquip) {
        equipSlotCid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(e.control) + kControlIdOffset);
        isEquipSlot =
            equipSlotCid == kEquipBtnHeadId    || equipSlotCid == kEquipBtnImplantId ||
            equipSlotCid == kEquipBtnBodyId    || equipSlotCid == kEquipBtnArmLId    ||
            equipSlotCid == kEquipBtnArmRId    || equipSlotCid == kEquipBtnWeapLId   ||
            equipSlotCid == kEquipBtnWeapRId   || equipSlotCid == kEquipBtnBeltId    ||
            equipSlotCid == kEquipBtnHandsId;
    }

    // Workbench upgrade slot buttons (BTN_UPGRADE3X/4X at .gui IDs 12..18 on
    // upgrade.gui). Same shape as equip-screen slot buttons: direct vtable[15]
    // activate doesn't populate LB_ITEMS with the mods compatible with this
    // slot — only the mouse-driven hover+click pipeline does. We don't have
    // an RE'd equivalent of OnEnterSlot/OnSelectSlot for the workbench yet,
    // so the safe path is a full click-sim at the chain entry's extent center
    // (mirrors the tab-button activation pattern).
    bool isWorkbenchUpgradeSlot = false;
    int  workbenchUpgradeSlotCid = 0;
    if (acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::WorkbenchUpgrade) {
        workbenchUpgradeSlotCid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(e.control) + kControlIdOffset);
        isWorkbenchUpgradeSlot =
            workbenchUpgradeSlotCid >= 12 && workbenchUpgradeSlotCid <= 18;
    }

    // Store item row Enter — route to the engine's trade-action handler
    // (OnControlInvAButton / OnControlStoreAButton based on mode) instead of
    // the generic FireActivate. The default vtable[15] event 0x27 path just
    // refreshes the description listbox via OnControlEntered — never actually
    // sells or buys. Action buttons (Verkaufsliste / Schliess. / Kaufen) fall
    // through to the default activate path below; they're plain CSWGuiButton
    // instances, not CSWGuiStoreItemEntry rows.
    bool isStoreItemRow =
        acc::menus::store::IsStorePanel(g_chainPanel) &&
        acc::menus::store::IsStoreItemRow(e.control);

    // Journal quest-row Enter — read the description text. The row's own
    // activate handler is a no-op in the engine; the description text the
    // engine paints next to the list on mouse hover is the only signal a
    // sighted user gets, so we surface it on Enter instead.
    bool isJournalRow =
        acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameJournal &&
        acc::menus::journal::IsJournalEntry(e.control);

    // Quest-item row Enter — the QuestItem sub-screen ("Auftrags-Gegenstände")
    // lists plot items as CSWGuiInGameItemEntry rows with no meaningful
    // activate action (only BTN_BACK does anything). Mirror the journal: read
    // the item's property description on Enter. BTN_BACK is a plain button
    // (not an item row) so it falls through to the generic activate → close.
    bool isQuestItemRow =
        acc::engine::IdentifyPanel(g_chainPanel) ==
            acc::engine::PanelKind::InGameQuestItems &&
        acc::engine::IsInventoryItemRow(e.control);

    if (acc::menus::pending::IsPending()) {
        acclog::Write("Enter", "op already pending; ignoring (target=%p)",
                      e.control);
        consumed = true;
    } else if (isStoreItemRow) {
        acc::menus::pending::QueueStoreItemActivate(g_chainPanel, e.control);
        acclog::Write("Menus.Enter",
                      "store-item-activate panel=%p index=%d target=%p",
                      g_chainPanel, g_chainIndex, e.control);
        consumed = true;
    } else if (isJournalRow) {
        acc::menus::journal::SpeakDescription(g_chainPanel, e.control);
        consumed = true;
    } else if (isQuestItemRow) {
        acc::peek::SpeakItemRowDescription(e.control);
        acclog::Write("Menus.Enter",
                      "quest-item-description panel=%p index=%d target=%p",
                      g_chainPanel, g_chainIndex, e.control);
        consumed = true;
    } else if (e.textOnly) {
        // Modal body text — non-activatable. Re-speak so a user who missed
        // the open-time announce can hear it again. Don't fire vtable[15]
        // (the listbox has no activate handler).
        acc::menus::monitors::AnnounceControl(e.control);
        acclog::Write("Menus.Enter",
                      "re-announce panel=%p index=%d target=%p (text-only)",
                      activePanel, g_chainIndex, e.control);
        consumed = true;
    } else if (isTabButton) {
        int cursorY = e.cy + ComputeTabClickOffsetY(g_chainPanel);
        acc::menus::pending::QueueClickAt(e.cx, cursorY, e.control);
        acclog::Write("Menus.Enter",
                      "click-sim panel=%p index=%d target=%p cursorY=%d (tab)",
                      activePanel, g_chainIndex, e.control, cursorY);
        consumed = true;
    } else if (isEquipSlot) {
        // Bypass click-sim entirely. Calling OnEnterSlot then OnSelectSlot
        // directly invokes the same engine path that mouse-driven hover+click
        // does, but without depending on hit-test landing on the slot button
        // (the labels cover the buttons in z-order — see
        // docs/equip-flow-investigation.md). Deferred to OnUpdate to stay
        // clear of mid-input-dispatch recursion.
        acc::menus::pending::QueueEquipSelect(g_chainPanel, e.control);
        // Arm the picker zone now: OnSelectSlot raises field33_0x4270 |= 1
        // and the user proceeds to LB_ITEMS browsing. Self-clears on panel
        // close, picker Esc, or BTN_EQUIP dispatch.
        acc::menus::listbox::ArmEquipPicker(g_chainPanel);
        acclog::Write("EquipPicker",
                      "armed via direct OnEnterSlot+OnSelectSlot "
                      "(Enter on slot id=%d btn=%p panel=%p)",
                      equipSlotCid, e.control, g_chainPanel);
        consumed = true;
    } else if (isWorkbenchUpgradeSlot) {
        // Click-sim landed on a label (z-order trap); vtable[15] is the
        // keyboard-shortcut path that doesn't populate LB_ITEMS. Both verified
        // in patch-20260525-141557.log and -142247.log. RE'd the workbench
        // slot-pick chain in Lane's gzf — calling CSWGuiUpgrade::OnEnterSlot
        // + OnSlotSelected directly is the engine path that builds the
        // compatible-mods list from CSWPartyTable items + upgrades_2da /
        // upcrystals_2da and AddControls-replaces LB_ITEMS contents.
        acc::menus::pending::QueueWorkbenchSlotSelect(g_chainPanel, e.control);
        acc::menus::listbox::ArmWorkbenchUpgradePicker(g_chainPanel);
        acclog::Write("WorkbenchUpgrade",
                      "armed via direct OnEnterSlot+OnSlotSelected "
                      "(Enter on slot id=%d btn=%p panel=%p)",
                      workbenchUpgradeSlotCid, e.control, g_chainPanel);
        consumed = true;
    } else {
        acc::menus::pending::QueueActivate(e.control);
        // Drill flag is armed centrally inside the OnSwitchToSWInGameGui
        // detour — every path that opens a sub-screen (strip-icon Enter,
        // vanilla M/I/J hotkeys) flows through that one function, so no
        // per-caller arm is needed here.
        acclog::Write("Menus.Enter", "activate panel=%p index=%d target=%p",
                      activePanel, g_chainIndex, e.control);
        consumed = true;
    }
}

void WalkChildren(const char* label, void* parent, size_t offset,
                  const char* kindName) {
    if (!parent) return;
    // One BlockLog per walk. Line() is the full display (with pointers); Key()
    // is the same content with the volatile heap pointers stripped, so a walk
    // of a panel the engine recreated at a fresh address still hashes equal and
    // folds to a "(repeated Nx)" summary. The block emits-or-folds when `block`
    // leaves scope on any return path. Stable vtable code-addresses stay in the
    // key (they identify the control class).
    acclog::BlockLog block(label);
    if (kindName && *kindName) {
        block.Line("panel=%p kind=%s", parent, kindName);
        block.Key("kind=%s", kindName);
    }

    auto* list = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(parent) + offset);
    if (!list->data || list->size <= 0) {
        block.Line("walk parent=%p children=0", parent);
        block.Key("children=0");
        return;
    }
    int count = list->size;
    if (count > 256) {
        block.Line("walk parent=%p size_oob=%d (capped)", parent, count);
        block.Key("size_oob=%d", count);
        count = 256;
    }
    block.Line("walk parent=%p children=%d", parent, list->size);
    block.Key("children=%d", list->size);
    for (int i = 0; i < count; ++i) {
        void* child = list->data[i];
        if (!child) {
            block.Line("  [%d]=NULL", i);
            block.Key("  [%d]=NULL", i);
            continue;
        }
        int id = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(child) + kControlIdOffset);
        char text[256];
        // Pass `parent` so the perkind fallback resolves correctly when
        // walking InGameMenu's children — the icon labels/buttons have empty
        // CExoString/strref/text_object/gui_string and only resolve via the
        // panel-keyed perkind table.
        const char* source = acc::menus::extract::FromControl(
            child, text, sizeof(text), parent);
        if (source) {
            block.Line("  [%d] %p id=%d src=%s text=\"%s\"",
                       i, child, id, source, text);
            block.Key("  [%d] id=%d src=%s text=\"%s\"", i, id, source, text);
        } else {
            char vtbl[160];
            acc::engine::DumpControlVtable(child, vtbl, sizeof(vtbl));
            block.Line("  [%d] %p id=%d src=none %s",
                       i, child, id, vtbl);
            block.Key("  [%d] id=%d src=none %s", i, id, vtbl);
        }
    }
}

void HandleNavStep(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    bool navKeyIsHome = (code == kInputHome);
    bool navKeyIsEnd  = (code == kInputEnd);
    bool navKeyIsUp   = (code == kInputNavUp);
    bool navKeyIsDown = (code == kInputNavDown);
    if (!(navKeyIsUp || navKeyIsDown || navKeyIsHome || navKeyIsEnd)) return;
    if (activePanel == nullptr) return;

    if (activePanel != g_chainPanel) {
        RebindChain(activePanel);
    }
    if (g_chainCount == 0) {
        // Foreground panel has no navigable controls. Log so we can see
        // which panels are routing-only (e.g. the recurring 074FE618
        // overlay and the dialog routing target 0FDEE418 observed in
        // the in-game session) and decide whether to add a fallback
        // strategy (walk down the modal stack to the next chain-eligible
        // panel, or surface the panel's content via the title/listbox
        // path). For now: log only, leave the input unconsumed so the
        // engine sees it.
        acc::engine::PanelKind emptyKind = acc::engine::IdentifyPanel(activePanel);
        acclog::Write("Menus.Chain", "empty panel=%p kind=%s has no navigable "
                      "controls; input not consumed",
                      activePanel, acc::engine::PanelKindName(emptyKind));

        // Walk the panel ONCE so we can see what's actually in it.
        // OnSetActiveControl's panel-walk gate (s_lastPanel) doesn't
        // fire on these panels because the engine never sets focus on
        // them. Without a walk we never learn their structure — log-only
        // diagnostics give us nothing actionable.
        static void* s_walkedEmptyPanels[16];
        static int   s_walkedEmptyCount = 0;
        bool walked = false;
        for (int i = 0; i < s_walkedEmptyCount; ++i) {
            if (s_walkedEmptyPanels[i] == activePanel) { walked = true; break; }
        }
        if (!walked && s_walkedEmptyCount < 16) {
            s_walkedEmptyPanels[s_walkedEmptyCount++] = activePanel;
            acclog::Write("Menus.EmptyChain", "walk panel=%p kind=%s",
                          activePanel, acc::engine::PanelKindName(emptyKind));
            WalkChildren("Menus.EmptyChain", activePanel, kPanelControlsOffset);
        }
    }
    if (g_chainCount <= 0) return;

    int newIndex;
    if (navKeyIsHome) {
        newIndex = 0;
    } else if (navKeyIsEnd) {
        newIndex = g_chainCount - 1;
    } else {
        int delta = navKeyIsDown ? +1 : -1;
        newIndex = g_chainIndex + delta;
        if (newIndex < 0)              newIndex = 0;
        if (newIndex >= g_chainCount)  newIndex = g_chainCount - 1;
    }
    g_chainIndex = newIndex;

    ChainEntry& e = g_chain[g_chainIndex];
    // Chargen Fähigkeiten descriptions are long (~10s of speech each) but the
    // user navigates Up/Down faster than they can read. With interrupt=false
    // (our default), each step queues "label, suffix, description" behind the
    // previous step's still-playing description — the user hears descriptions
    // one row behind their focus. Silence any in-flight speech before
    // announcing the new row, so each chain step starts fresh and the
    // just-arrived focus wins the speech channel. No-op on every other panel
    // (their descriptions are short enough to drain naturally).
    if (acc::menus::chargen_skills::IsChargenSkillsPanel(g_chainPanel)) {
        prism::Silence();
    }
    acc::menus::monitors::AnnounceControl(e.control);
    // Mirror chain focus into the chargen Attributes panel's selected_ability
    // so the next Left/Right press routes OnPlusButton / OnMinusButton to the
    // focused ability rather than the default top row (STR). No-op on every
    // other panel.
    acc::menus::chargen_attr::SyncSelectedAbilityFromChainFocus();
    // Same for the chargen Skills panel — different field
    // (selected_skill_index) on a different panel, same mechanism.
    acc::menus::chargen_skills::SyncSelectedSkillFromChainFocus();
    // Per-row info suffixes / descriptions across panels that need them.
    // Each helper no-ops on every panel except its own.
    acc::menus::chargen_attr::AnnounceChainStepSuffix(g_chainPanel, e.control);
    acc::menus::chargen_skills::AnnounceChainStepSuffix(g_chainPanel, e.control);
    acc::menus::chargen_skills::AnnounceChainStepDescription(g_chainPanel, e.control);
    acc::menus::store::AnnounceChainStepSuffix(g_chainPanel, e.control);
    // Inventory rows (CSWGuiInGameInventory / Container loot listbox): append
    // "N Stück" when stack_size > 1. Store rows are deliberately excluded —
    // the store suffix above already speaks "Lager N" / "du besitzt N".
    // Silent on stack_size == 1 so weapons / armour stay quiet.
    if (acc::engine::IsInventoryItemRow(e.control)) {
        int stack = acc::engine::ReadItemRowStackCount(e.control);
        if (stack > 1) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix),
                     acc::strings::Get(acc::strings::Id::FmtItemStackSuffix),
                     stack);
            prism::Speak(suffix, /*interrupt=*/false);
        }
    }
    int cursorX = e.cx;
    int cursorY = e.cy;
    if (!e.textOnly) {
        // Cursor warp + suppress-budget exist to make hover-to-focus work for
        // activatable controls. Text-only entries (modal body listboxes) have
        // no hover semantics worth chasing — skipping keeps the cursor stable
        // on whatever button the user just left, and avoids spurious
        // engine-side SetActiveControl echoes from the listbox under the cursor.
        if (IsTabButton(e.control)) {
            cursorY += ComputeTabClickOffsetY(g_chainPanel);
        }
        if (acc::menus::detail::IsClassSelectionIcon(g_chainPanel, e.control) &&
            g_classIconClickOffsetX > 0) {
            cursorX += g_classIconClickOffsetX;
        }
        // Chargen Attribute / Skills hit-test-shifts-up-one-row compensation.
        // Without it the cursor lands on the row above and the engine's
        // OnEnterPointsButton populates description_listbox for the wrong row.
        {
            int abilityPitch = acc::menus::chargen_attr::RowPitchForCursorWarp(
                g_chainPanel, e.control);
            if (abilityPitch > 0) cursorY += abilityPitch;
        }
        {
            int skillPitch = acc::menus::chargen_skills::RowPitchForCursorWarp(
                g_chainPanel, e.control);
            if (skillPitch > 0) cursorY += skillPitch;
        }
        acc::menus::pending::QueueMoveCursor(cursorX, cursorY, e.control);
        // No explicit suppress needed for the engine-side focus echo:
        // AnnounceControl above primed channel-0 dedup via MarkSpoken, so
        // DrainPendingAnnounce will short-circuit when the cursor-warp's
        // SetActive echo arrives with the same text.
    }
    const char* dirTag =
        navKeyIsHome ? "HOME" :
        navKeyIsEnd  ? "END"  :
        navKeyIsDown ? "DOWN" : "UP";
    acclog::Write("Menus.Chain",
                  "step panel=%p index=%d/%d target=%p center=(%d,%d) cursor=(%d,%d)%s %s",
                  g_chainPanel, g_chainIndex, g_chainCount,
                  e.control, e.cx, e.cy, cursorX, cursorY,
                  e.textOnly ? " text-only" : "",
                  dirTag);
    // Always consume nav keys on a panel with a non-empty chain.
    consumed = true;
}

void HandleLeftRight(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    if (code != kInputNavLeft && code != kInputNavRight) return;
    if (activePanel == nullptr || g_chainPanel != activePanel) return;
    if (g_chainCount <= 0 || g_chainIndex < 0 || g_chainIndex >= g_chainCount) return;

    void* focused = g_chain[g_chainIndex].control;
    bool toRight = (code == kInputNavRight);

    if (acc::engine::IsSlider(focused)) {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Menus.Slider", "%s: op already pending; ignoring",
                          toRight ? "right" : "left");
        } else {
            int sliderCode = toRight ? 500 : 501;
            acc::menus::pending::QueueSliderInput(focused, sliderCode);
            acclog::Write("Menus.Slider", "%s panel=%p focus=%p code=%d",
                          toRight ? "right" : "left",
                          activePanel, focused, sliderCode);
        }
    } else {
        // Panel-aware cycle override: in CSWGuiPortraitCharGen the chain
        // holds left_arrow as the lone anchor (right_arrow is filtered out
        // in RebindChain). FindAdjacentArrow can pick up the right_arrow
        // as a same-row neighbour when going right, but going left there's
        // nothing to the left of x=272 — so we resolve the targets directly
        // from the panel offsets:
        //   Left  → activate left_arrow (cycles -1)
        //   Right → activate right_arrow (cycles +1)
        // Engine's UpdatePortraitButton writes the new resref to
        // creature.portrait, the per-frame focus monitor re-reads, and
        // the diff fires the new "Porträt: …" announcement.
        void* portraitTarget = nullptr;
        {
            void** pVt = *reinterpret_cast<void***>(activePanel);
            if (reinterpret_cast<uintptr_t>(pVt) ==
                    kVtableCSWGuiPortraitCharGen) {
                auto* base = reinterpret_cast<unsigned char*>(activePanel);
                void* leftArrow = base + kPortraitLeftArrowOffset;
                if (focused == leftArrow) {
                    portraitTarget = toRight
                        ? (void*)(base + kPortraitRightArrowOffset)
                        : leftArrow;
                }
            }
        }
        void* neighbor = portraitTarget
            ? portraitTarget
            : FindAdjacentArrow(activePanel, focused, toRight);
        if (neighbor) {
            if (acc::menus::pending::IsPending()) {
                acclog::Write("Menus.Cycle", "%s: op already pending; ignoring",
                              toRight ? "right" : "left");
            } else {
                acc::menus::pending::QueueActivate(neighbor);
                acclog::Write("Menus.Cycle", "%s panel=%p focus=%p neighbor=%p%s",
                              toRight ? "right" : "left",
                              activePanel, focused, neighbor,
                              portraitTarget ? " (portrait-anchor)" : "");
            }
        } else {
            acclog::Write("Menus.Cycle", "%s: no adjacent arrow for focus=%p",
                          toRight ? "right" : "left", focused);
        }
    }
    consumed = true;
}

void HandleEsc(void* activePanel, int code, int val, bool& consumed) {
    if (val == 0) return;
    if (code != kInputEsc1 && code != kInputEsc2) return;

    // Store-specific Esc: route to cancel_button (Schliess.) directly.
    // The store isn't in IsModalPopupPanel (it's the foreground modal,
    // not a popup on top), and the chain doesn't include the cancel
    // button anymore (we filter it out so it doesn't clutter Up/Down
    // nav), so without this Esc would no-op on the store.
    if (acc::menus::store::IsStorePanel(activePanel)) {
        if (acc::menus::store::CloseFromEsc()) consumed = true;
    }

    // Workbench upgrade panel Esc: route to BTN_BACK (id 28, "Abbrechen")
    // directly. Same shape as the store branch above — the upgrade.gui
    // panel is the foreground modal (not a popup on top), so the generic
    // Esc gate below (IsModalPopupPanel / g_tabbedPanel / escIsOptionsSub)
    // doesn't fire. We also can't rely on FindCancelButton landing on
    // BTN_BACK reliably here (see kWorkbenchUpgradeSpec comments).
    // While the picker is armed the spec's onEsc disarms; this branch
    // only catches Esc when the picker is NOT armed (user on a slot
    // button, BTN_ASSEMBLE, or BTN_BACK).
    if (!consumed && activePanel != nullptr &&
        acc::engine::IdentifyPanel(activePanel) ==
            acc::engine::PanelKind::WorkbenchUpgrade &&
        !acc::menus::listbox::IsWorkbenchUpgradePickerArmed())
    {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Esc", "WorkbenchUpgrade — op already pending; ignoring");
            consumed = true;
        } else {
            constexpr int kWorkbenchUpgradeBtnBack = 28;
            void* back = acc::menus::detail::FindControlById(
                activePanel, kWorkbenchUpgradeBtnBack);
            if (back) {
                acc::menus::pending::QueueActivate(back);
                acclog::Write("Esc",
                              "WorkbenchUpgrade -> BTN_BACK panel=%p target=%p",
                              activePanel, back);
                consumed = true;
            } else {
                acclog::Write("Esc",
                              "WorkbenchUpgrade -- BTN_BACK not found on panel=%p",
                              activePanel);
            }
        }
    }

    // InGameOptions sub-screen override: the parent strip's controls[0] is
    // a button (Spiel laden), not a listbox, so DetectTabsCluster never
    // latches and `g_tabbedPanel` stays null — the tabbed-parent arm above
    // misses every in-game Options sub-screen. The foreground may also be
    // a HUD layer rather than the sub-screen itself, depending on overlay
    // ordering, so activePanel isn't a reliable target either.
    //
    // `g_chainPanel` is the right discriminator: SetActiveControl re-binds
    // the chain to the heap-allocated sub-screen on entry, so it points at
    // Spieleinstellungen / Grafik / Sound / Auto-Pause / Feedback /
    // Tastenbelegung / Mauseinstellungen for the lifetime of that screen,
    // and FindCloseButton on it resolves Schliess. reliably. The
    // IsInGameOptionsSubScreen helper already excludes the parent strip.
    void* escTargetPanel = activePanel;
    bool  escIsOptionsSub = false;
    if (g_chainPanel != nullptr &&
        acc::engine::IsInGameOptionsSubScreen(g_chainPanel))
    {
        escTargetPanel = g_chainPanel;
        escIsOptionsSub = true;
    }

    if (escTargetPanel != nullptr &&
        ((g_tabbedPanel != nullptr && escTargetPanel != g_tabbedPanel) ||
         acc::engine::IsModalPopupPanel(
             acc::engine::IdentifyPanel(escTargetPanel)) ||
         escIsOptionsSub))
    {
        if (acc::menus::pending::IsPending()) {
            acclog::Write("Esc", "op already pending; ignoring");
            consumed = true;
        } else {
            // Probe order matters: confirm-style popups (OK + Abbrechen,
            // Yes + No, …) carry BOTH a cancel-intent button AND the
            // affirmative that FindCloseButton matches as "OK". Esc is a
            // back-out gesture, never a confirm — try Abbrechen/Cancel
            // first so the quit-confirm and save-overwrite-style dialogs
            // route Esc to the safe choice. Single-button info popups
            // (StatusSummary's lone Schliess, AreaTransition's Weiter)
            // have no cancel button, so the FindCloseButton fallback
            // handles them.
            void* cancelBtn = FindCancelButton(escTargetPanel);
            void* tgt = cancelBtn ? cancelBtn : FindCloseButton(escTargetPanel);
            if (tgt) {
                acc::menus::pending::QueueActivate(tgt);
                // InGameOptions sub-screens: the close fires a deferred
                // destroy — the engine keeps the panel in panels[] across
                // the FireActivate dispatch (ValidateChainPanel finds it,
                // chain stays), then frees the panel + children at end
                // of tick. Between those two ticks, MonitorFocusedControl
                // walks g_chain[g_chainIndex].control, dereferences a
                // freed button, and FromControl's SEH-caught AV interacts
                // with /GS to fastfail. Confirmed by crash dump TID 16116:
                // ESI matched the chain entry the user had last navigated
                // to before pressing Esc.
                //
                // ValidateChainPanel can't help (panel still in panels[]
                // when it runs), and chain[10] nulling only covers
                // Schliess. itself — the other 11 entries are equally
                // dead. Invalidate the whole chain here; the Schliess.
                // pointer is already captured by QueueActivate and the
                // next SetActiveControl rebuilds against whatever the
                // engine refocuses on.
                if (escIsOptionsSub) InvalidateChain();
                acclog::Write("Menus.Esc",
                              "%s panel=%p kind=%s target=%p%s",
                              cancelBtn ? "cancel" : "close",
                              escTargetPanel,
                              acc::engine::PanelKindName(
                                  acc::engine::IdentifyPanel(escTargetPanel)),
                              tgt,
                              escIsOptionsSub
                                  ? " (InGameOptions sub-screen)" : "");
                consumed = true;
            } else {
                acclog::Write("Menus.Esc",
                              "sub-dialog panel=%p kind=%s but no cancel/close "
                              "button found; passing through",
                              escTargetPanel,
                              acc::engine::PanelKindName(
                                  acc::engine::IdentifyPanel(escTargetPanel)));
            }
        }
    }
}

}  // namespace acc::menus::chain
