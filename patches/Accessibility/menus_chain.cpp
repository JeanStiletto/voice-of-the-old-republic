// KOTOR Accessibility — chain-navigation state and helpers.
//
// Step 5 of the menus.cpp refactor. See menus_chain.h for what lives
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
#include "menus_store.h"
#include "prism.h"

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

int g_tabClickOffsetY        = 0;
int g_equipSlotClickOffsetY  = 0;
int g_classIconClickOffsetX  = 0;

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
    ResetTabbedState();
}

void ValidateTabbedPanel() {
    if (!g_tabbedPanel) return;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            if (panelData[i] == g_tabbedPanel) return;
        }
    }
    acclog::Write("ValidateTabbedPanel", "%p not in panels[]; clearing tabbed-mode state",
                  g_tabbedPanel);
    ResetTabbedState();
}

void ValidateChainPanel() {
    if (!g_chainPanel) return;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (panelData && panelCount > 0) {
        int n = panelCount > 16 ? 16 : panelCount;
        for (int i = 0; i < n; ++i) {
            if (panelData[i] == g_chainPanel) return;
        }
    }
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
    //   InGameCharacter id=64 (btn_change1) and id=67 (btn_change2) —
    //     portrait crossfade slots. The OnSwitchLeft/Right handlers mutate
    //     these as decoration during a party-member cycle; the user clicks
    //     btn_charleft (id=66) or btn_charright (id=65) to trigger the
    //     cycle, not these. Skipping them removes two "control 64" /
    //     "control 67" dead-ends from the chain.
    auto isDecorative = [&](void* c) -> bool {
        PanelKind pk = IdentifyPanel(panel);
        int cid = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(c) + 0x50);
        // cid 1 (btn_3dchar) is image-only model rotator; skip.
        // cid 64/67 (btn_change1/2) were previously skipped as "decorative
        // crossfade slots" — that was wrong. They ARE the actual party-
        // member switch targets: clicking one calls OnChangeCharacter
        // which switches the displayed character. The change "crossfade"
        // is a side-effect of the switch (the portraits update to show
        // the OTHER 2 NPCs after the swap), not the purpose of the
        // buttons. Re-exposed in the chain; per-kind labels in
        // menus_extract.cpp section 9e wire the speech.
        // cid 65/66 (btn_charright/btn_charleft) — pagination arrows
        // over the 9-slot NPC roster. Useless in KOTOR 1: max active
        // party is 3 (PC + 2 NPCs), and the 2 portrait slots already
        // cover both companions, so the arrows have nothing to advance to.
        if (pk == PanelKind::InGameCharacter &&
            (cid == 1 || cid == 65 || cid == 66)) {
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
        // InGameEquip character_left/right arrows: pagination over the
        // 9-slot NPC roster, same as InGameCharacter cid 65/66 above.
        // Max party in KOTOR 1 is 3, so the 2 change_party portraits
        // already cover both companions and the arrows have nothing to
        // advance to — skip.
        if (pk == PanelKind::InGameEquip) {
            auto* p = reinterpret_cast<unsigned char*>(panel);
            if (c == p + kEquipPanelCharacterLeftButtonOffset ||
                c == p + kEquipPanelCharacterRightButtonOffset) {
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
        // Source of truth is bit 0 of the per-portrait flag word at
        // +0x448: OnPanelAdded sets it only when GetIsNPCAvailable AND
        // GetNPCSelectability both pass for the slot — i.e. the
        // engine's own "Add" enable gate. We deliberately don't go
        // through the party-table thiscalls here so we stay portable
        // across saves where the table chain might not be settled.
        if (pk == PanelKind::PartySelection) {
            void** vt = *reinterpret_cast<void***>(c);
            if (reinterpret_cast<uintptr_t>(vt) == 0x00756BB8) {
                constexpr size_t kPartyPortraitFlagsOffset = 0x448;
                int flags = 0;
                __try {
                    flags = *reinterpret_cast<int*>(
                        reinterpret_cast<unsigned char*>(c) +
                        kPartyPortraitFlagsOffset);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    flags = 0;
                }
                if ((flags & 1) == 0) return true;
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

    // Compute g_tabClickOffsetY from adjacent tab entries' visual spacing.
    g_tabClickOffsetY = 0;
    if (g_tabbedPanel == panel && g_tabsCount >= 2) {
        int firstTabIdx = -1;
        for (int i = 0; i < g_chainCount; ++i) {
            if (!IsTabButton(g_chain[i].control)) continue;
            if (firstTabIdx < 0) {
                firstTabIdx = i;
            } else {
                int spacing = g_chain[i].cy - g_chain[firstTabIdx].cy;
                if (spacing > 0) g_tabClickOffsetY = spacing;
                break;
            }
        }
    }

    // Compute g_equipSlotClickOffsetY for InGameEquip panels.
    g_equipSlotClickOffsetY = 0;
    if (IdentifyPanel(panel) == PanelKind::InGameEquip) {
        int firstSlotIdx = -1;
        int firstSlotY   = 0;
        for (int i = 0; i < g_chainCount; ++i) {
            int cid = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(g_chain[i].control) + 0x50);
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
                  "tabOffsetY=%d equipSlotOffsetY=%d classIconOffsetX=%d",
                  panel, g_chainCount, g_chainIndex, active,
                  g_tabClickOffsetY, g_equipSlotClickOffsetY,
                  g_classIconClickOffsetX);
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

}  // namespace acc::menus::chain
