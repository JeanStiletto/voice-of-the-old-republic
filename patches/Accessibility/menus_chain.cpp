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
#include "engine_reads.h"
#include "log.h"
#include "menus_chargen_attr.h"
#include "menus_chargen_skills.h"
#include "menus_extract.h"
#include "menus_internal.h"
#include "tolk.h"

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

    for (int i = 0; i < n; ++i) {
        void* c = list->data[i];
        if (!c) continue;
        if (c == portraitChargenSkip) continue;

        if (IsChainNavigable(c)) {
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
