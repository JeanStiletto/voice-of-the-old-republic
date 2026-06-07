#include "unified_action_menu.h"

#include <cstdio>
#include <cstring>

#include "combat_diag.h"        // LogPreFire / LogPostFire around dispatch
#include "combat_queue.h"       // ReportPrePressDepth / GetPrePressDepth
#include "engine_actionbar.h"   // personal block read + primitives
#include "engine_area.h"        // ResolveServerObjectHandle, kInvalidObjectId
#include "engine_input.h"       // kInputNav*, kInputEnter*, kInputEsc*,
                                // kInputHome/End, kInputCatFirst/Last
#include "engine_offsets.h"      // kInvalidObjectId
#include "engine_panels.h"      // HasActiveDialogPanel
#include "engine_picker.h"      // ReanchorRadial (per-press target re-anchor)
#include "engine_radial.h"      // target block read + primitives
#include "engine_reads.h"       // ResolveActionDescriptionFromActionId
#include "engine_subscreen.h"   // Begin/EndOverlayPause
#include "hotkeys.h"            // ShiftHeld
#include "log.h"
#include "menu_speak.h"
#include "narrated_target.h"
#include "strings.h"
#include "prism.h"

namespace acc::unified_menu {

namespace {

constexpr int kRowCount    = acc::engine_radial::kRowCount;     // 3 target rows
constexpr int kColumnCount = acc::engine_actionbar::kColumnCount; // 6 (0..3 used)
constexpr int kMaxCats     = kRowCount + kColumnCount;

enum class CatKind { Target, Personal };

struct Cat {
    CatKind kind;
    int     slot;   // Target: row 0..2 ; Personal: col 0..3
};

// Per-row / per-column selected-entry shadow, persisted across menu
// sessions AND across the engine's PopulateMenus rebuilds (which reassign
// action_ids). Kept in lock-step with the engine selection by pairing every
// index change with an ApplySelection() (SelectActionInRow / SelectVariant).
// Read by the bare-key announce path so 1..7 reports what the engine fires.
int g_targetSel[kRowCount]      = {0, 0, 0};
int g_personalSel[kColumnCount] = {0, 0, 0, 0, 0, 0};

struct State {
    bool     active       = false;
    Cat      cats[kMaxCats];
    int      catCount     = 0;
    int      curCat       = 0;
    uint32_t targetHandle = 0;     // server/client handle; 0 = personal-only
    bool     creature     = false; // target is a hostile creature → named rows
    char     targetName[64] = "";
};
State g;

int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ---- per-kind primitive dispatch -----------------------------------------

int CountForCat(void* tam, void* mi, const Cat& c) {
    return c.kind == CatKind::Target
        ? acc::engine_radial::RowActionCount(tam, c.slot)
        : acc::engine_actionbar::VariantCount(mi, c.slot);
}

int& ShadowFor(const Cat& c) {
    return c.kind == CatKind::Target ? g_targetSel[c.slot]
                                     : g_personalSel[c.slot];
}

// Stamp the engine's per-slot selection to `idx` so reads + dispatch land on
// the user's chosen entry (target: field1 via SelectActionInRow; personal:
// the +0x1bac selected-action-id via SelectVariant).
void ApplySelection(void* tam, void* mi, const Cat& c, int idx) {
    if (c.kind == CatKind::Target) {
        (void)acc::engine_radial::SelectActionInRow(tam, c.slot, idx);
    } else {
        (void)acc::engine_actionbar::SelectVariant(mi, c.slot, idx);
    }
}

// Read the label of the selected entry. Target reads via the field1-selected
// descriptor (ApplySelection must run first); personal reads by explicit idx.
void ReadLabel(void* tam, void* mi, const Cat& c, int idx,
               char* out, size_t n) {
    out[0] = '\0';
    if (c.kind == CatKind::Target) {
        acc::engine_radial::ReadRowActionLabel(tam, c.slot, out, n);
    } else {
        acc::engine_actionbar::ReadVariantLabel(mi, c.slot, idx, out, n);
    }
}

uint32_t ReadActionId(void* tam, void* mi, const Cat& c, int idx) {
    return c.kind == CatKind::Target
        ? acc::engine_radial::ReadSelectedRowActionId(tam, c.slot)
        : acc::engine_actionbar::ReadVariantActionId(mi, c.slot, idx);
}

bool Dispatch(void* tam, void* mi, const Cat& c) {
    return c.kind == CatKind::Target
        ? acc::engine_radial::DispatchRowAction(tam, c.slot)
        : acc::engine_actionbar::FireSelectedVariant(mi, c.slot);
}

// Localised category name, or nullptr for an unnamed category (target rows
// on a non-creature target — door / placeable / trigger — whose rows carry
// per-object actions like Security / Bash and are announced by label).
const char* CategoryName(const Cat& c) {
    using S = acc::strings::Id;
    if (c.kind == CatKind::Personal) {
        switch (c.slot) {
            case 0: return acc::strings::Get(S::MenuCatSelfPowers);
            case 1: return acc::strings::Get(S::MenuCatMedical);
            case 2: return acc::strings::Get(S::MenuCatMisc);
            case 3: return acc::strings::Get(S::MenuCatExplosives);
            default: return nullptr;
        }
    }
    if (!g.creature) return nullptr;  // door/placeable target → label only
    switch (c.slot) {
        case 0: return acc::strings::Get(S::MenuCatAttacks);
        case 1: return acc::strings::Get(S::MenuCatForcePowers);
        case 2: return acc::strings::Get(S::MenuCatItems);
        default: return nullptr;
    }
}

// ---- category list -------------------------------------------------------

void BuildCategoryList(void* tam, void* mi) {
    g.catCount = 0;
    if (tam) {
        for (int r = 0; r < kRowCount && g.catCount < kMaxCats; ++r) {
            if (acc::engine_radial::RowActionCount(tam, r) > 0) {
                g.cats[g.catCount++] = {CatKind::Target, r};
            }
        }
    }
    if (mi) {
        for (int col = 0; col < kColumnCount && g.catCount < kMaxCats; ++col) {
            if (acc::engine_actionbar::VariantCount(mi, col) > 0) {
                g.cats[g.catCount++] = {CatKind::Personal, col};
            }
        }
    }
}

int LocateCat(CatKind kind, int slot) {
    for (int i = 0; i < g.catCount; ++i) {
        if (g.cats[i].kind == kind && g.cats[i].slot == slot) return i;
    }
    return -1;
}

int FirstPopulatedTargetRow(void* tam) {
    for (int r = 0; r < kRowCount; ++r) {
        if (acc::engine_radial::RowActionCount(tam, r) > 0) return r;
    }
    return -1;
}

// ---- speech --------------------------------------------------------------

// Build the full category announce ("Name: label, N Optionen" / "label, N
// Optionen" for unnamed rows) into `out`. ApplySelection must have run.
void FormatCategory(void* tam, void* mi, const Cat& c, int count, int idx,
                    char* out, size_t n) {
    char label[128] = "";
    ReadLabel(tam, mi, c, idx, label, sizeof(label));
    const char* lbl = label[0] ? label : "?";
    const char* name = CategoryName(c);
    using S = acc::strings::Id;
    if (name && name[0]) {
        if (count > 1) {
            std::snprintf(out, n, acc::strings::Get(S::FmtMenuCatMulti),
                          name, lbl, count);
        } else {
            std::snprintf(out, n, acc::strings::Get(S::FmtMenuCatSingle),
                          name, lbl);
        }
    } else {
        if (count > 1) {
            std::snprintf(out, n, acc::strings::Get(S::FmtMenuPlainMulti),
                          lbl, count);
        } else {
            std::snprintf(out, n, "%s", lbl);
        }
    }
}

// Speak the current category in full. `prefix` (e.g. "Aktionsmenü, X") is
// prepended with ". " when non-null/non-empty (Shift+Enter open only).
void SpeakCategory(void* tam, void* mi, const char* prefix) {
    Cat& c = g.cats[g.curCat];
    int count = CountForCat(tam, mi, c);
    int& sel = ShadowFor(c);
    sel = ClampInt(sel, 0, count > 0 ? count - 1 : 0);
    ApplySelection(tam, mi, c, sel);

    char cat[256] = "";
    FormatCategory(tam, mi, c, count, sel, cat, sizeof(cat));

    char msg[320];
    if (prefix && prefix[0]) {
        std::snprintf(msg, sizeof(msg), "%s. %s", prefix, cat);
    } else {
        std::snprintf(msg, sizeof(msg), "%s", cat);
    }
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("UnifiedMenu", "speak cat=%d kind=%s slot=%d count=%d idx=%d "
        "-> [%s]", g.curCat,
        c.kind == CatKind::Target ? "target" : "personal",
        c.slot, count, sel, msg);
}

// Speak only the current entry label — used after Up/Down/Home/End (the
// category didn't change, so its name would be redundant noise).
void SpeakEntry(void* tam, void* mi) {
    Cat& c = g.cats[g.curCat];
    int& sel = ShadowFor(c);
    char label[128] = "";
    ReadLabel(tam, mi, c, sel, label, sizeof(label));
    acc::menu_speak::SpeakChoice("UnifiedMenu", label,
                                 "entry cat=%d slot=%d idx=%d",
                                 g.curCat, c.slot, sel);
}

// ---- target resolution ---------------------------------------------------

// Server-side handle of the currently narrated target, or 0 if none / map
// pin / stale. Mirrors target_action_menu's old resolver.
uint32_t ResolveNarratedServerHandle() {
    acc::narrated_target::Slot slot{};
    if (!acc::narrated_target::TryGet(slot)) return 0;
    if (slot.isMapPin) return 0;
    if (slot.handle == 0u || slot.handle == kInvalidObjectId) return 0;
    if (!acc::engine::ResolveServerObjectHandle(slot.handle)) return 0;
    return slot.handle;
}

// ---- arm / disarm --------------------------------------------------------

void Arm() {
    if (!g.active) {
        g.active = true;
        acc::engine::BeginOverlayPause();
    }
}

}  // namespace

int PersonalSelection(int col) {
    if (col < 0 || col >= kColumnCount) return 0;
    return g_personalSel[col] < 0 ? 0 : g_personalSel[col];
}

int TargetSelection(int row) {
    if (row < 0 || row >= kRowCount) return 0;
    return g_targetSel[row] < 0 ? 0 : g_targetSel[row];
}

bool IsActive() { return g.active; }

void ForceDisarm(const char* reason) {
    if (!g.active) return;
    acclog::Write("UnifiedMenu", "disarm — reason=%s", reason ? reason : "?");
    g.active = false;
    acc::engine::EndOverlayPause();
    g.catCount = 0;
    g.curCat = 0;
    g.targetHandle = 0;
    g.creature = false;
    g.targetName[0] = '\0';
    // Shadows persist intentionally — keep the user's per-slot variant.
}

bool ArmFromRadial(const char* name, uint32_t targetHandle) {
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        acclog::Write("UnifiedMenu", "ArmFromRadial — TAM unresolved; not arming");
        return false;
    }
    // Preserve Shift+Enter semantics: only arm when a target category is
    // populated. No target rows → return false so the caller speaks the
    // existing "keine Aktionen … Enter" redirect (no regression).
    int firstRow = FirstPopulatedTargetRow(tam);
    if (firstRow < 0) {
        acclog::Write("UnifiedMenu", "ArmFromRadial — no populated target row; "
            "not arming (caller speaks no-actions)");
        return false;
    }
    void* mi = acc::engine_actionbar::ResolveMainInterface();

    Arm();
    g.targetHandle = targetHandle;
    g.creature = acc::engine_radial::IsCreatureClientTarget(targetHandle);
    std::snprintf(g.targetName, sizeof(g.targetName), "%s",
                  (name && name[0]) ? name : "");

    BuildCategoryList(tam, mi);
    int loc = LocateCat(CatKind::Target, firstRow);
    g.curCat = (loc >= 0) ? loc : 0;

    char prefix[160] = "";
    std::snprintf(prefix, sizeof(prefix),
                  acc::strings::Get(acc::strings::Id::FmtInteractRadial),
                  g.targetName);

    acclog::Write("UnifiedMenu", "ARMED (radial) target=0x%08x creature=%d "
        "cats=%d curCat=%d", targetHandle, g.creature ? 1 : 0,
        g.catCount, g.curCat);
    SpeakCategory(tam, mi, prefix);
    return true;
}

bool OpenTarget(int row) {
    if (row < 0 || row >= kRowCount) return false;
    if (acc::engine::HasActiveDialogPanel()) {
        acclog::Write("UnifiedMenu", "OpenTarget row=%d — dialog panel; not arming",
            row);
        return false;
    }
    uint32_t handle = ResolveNarratedServerHandle();
    if (handle == 0) {
        // No focused target — Shift+1/2/3 are explicit target openers, so
        // say "kein Fokus" rather than dropping into the personal block.
        prism::Speak(acc::strings::Get(acc::strings::Id::GuidanceNoFocus),
                     /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "OpenTarget row=%d — no narrated target", row);
        return false;
    }
    // Refresh both blocks against the focused target.
    (void)acc::picker::ReanchorRadial(handle);
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();
    if (!tam || acc::engine_radial::RowActionCount(tam, row) <= 0) {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(acc::strings::Id::FmtActionBarColumnEmpty),
                      row + 1);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "OpenTarget row=%d empty (target=0x%08x)",
            row, handle);
        return false;
    }

    Arm();
    g.targetHandle = handle;
    g.creature = acc::engine_radial::IsCreatureClientTarget(handle);
    g.targetName[0] = '\0';

    BuildCategoryList(tam, mi);
    int loc = LocateCat(CatKind::Target, row);
    g.curCat = (loc >= 0) ? loc : 0;

    acclog::Write("UnifiedMenu", "ARMED (target) row=%d target=0x%08x creature=%d "
        "cats=%d", row, handle, g.creature ? 1 : 0, g.catCount);
    SpeakCategory(tam, mi, /*prefix=*/nullptr);
    return true;
}

bool OpenPersonal(int col) {
    if (col < 0 || col >= kColumnCount) return false;
    if (acc::engine::HasActiveDialogPanel()) {
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d — dialog panel; not arming",
            col);
        return false;
    }
    // Refresh personal lists (and, if a target is focused, the target block
    // too so Left can cross into it). With no target, prep against the
    // invalid sentinel so self-buff items still resolve.
    uint32_t handle = ResolveNarratedServerHandle();
    if (handle != 0) {
        (void)acc::picker::ReanchorRadial(handle);
    } else {
        (void)acc::engine_actionbar::PrepareBareDispatch(kInvalidObjectId);
    }
    void* mi  = acc::engine_actionbar::ResolveMainInterface();
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!mi || acc::engine_actionbar::VariantCount(mi, col) <= 0) {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(acc::strings::Id::FmtActionBarColumnEmpty),
                      col + 1);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d empty", col);
        return false;
    }

    Arm();
    g.targetHandle = handle;  // 0 when no target → personal-only, no re-anchor
    g.creature = (handle != 0) &&
                 acc::engine_radial::IsCreatureClientTarget(handle);
    g.targetName[0] = '\0';

    BuildCategoryList(tam, mi);
    int loc = LocateCat(CatKind::Personal, col);
    g.curCat = (loc >= 0) ? loc : 0;

    acclog::Write("UnifiedMenu", "ARMED (personal) col=%d target=0x%08x cats=%d",
        col, handle, g.catCount);
    SpeakCategory(tam, mi, /*prefix=*/nullptr);
    return true;
}

bool HandleInputEvent(int code, int value) {
    if (!g.active) return false;
    if (value == 0) return false;

    // Esc — close (ForceDisarm unpauses the world).
    if (code == kInputEsc1 || code == kInputEsc2) {
        prism::Speak(acc::strings::Get(acc::strings::Id::ActionBarCancelled),
                     /*interrupt=*/true);
        ForceDisarm("esc");
        return true;
    }

    // Re-anchor the target block against our cached target each press so a
    // drifting cursor can't re-point/empty it (project_radial_cursor_coupling).
    // Personal-only menus (targetHandle==0) need no re-anchor.
    if (g.targetHandle != 0) {
        if (!acc::picker::ReanchorRadial(g.targetHandle)) {
            acclog::Write("UnifiedMenu", "reanchor failed (target=0x%08x) — "
                "dropping target block, keeping personal", g.targetHandle);
            g.targetHandle = 0;
        }
    }
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();

    // Rebuild the category list (target rows may have changed on re-anchor)
    // and re-locate the cursor on the same category, clamping if it drained.
    CatKind savedKind = g.cats[g.curCat].kind;
    int     savedSlot = g.cats[g.curCat].slot;
    BuildCategoryList(tam, mi);
    if (g.catCount == 0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(acc::strings::Id::FmtInteractNoActions),
                      g.targetName);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "all categories empty after reanchor; disarming");
        ForceDisarm("empty");
        return true;
    }
    int loc = LocateCat(savedKind, savedSlot);
    g.curCat = (loc >= 0) ? loc : ClampInt(g.curCat, 0, g.catCount - 1);

    Cat cur = g.cats[g.curCat];
    int count = CountForCat(tam, mi, cur);
    int& sel = ShadowFor(cur);
    sel = ClampInt(sel, 0, count > 0 ? count - 1 : 0);
    ApplySelection(tam, mi, cur, sel);

    // Shift + any arrow — describe the selected entry, no movement.
    if ((code == kInputNavUp || code == kInputNavDown ||
         code == kInputNavLeft || code == kInputNavRight) &&
        acc::hotkeys::ShiftHeld()) {
        uint32_t actionId = ReadActionId(tam, mi, cur, sel);
        char text[8192];
        if (actionId &&
            acc::engine::ResolveActionDescriptionFromActionId(
                actionId, text, sizeof(text))) {
            prism::Speak(text, /*interrupt=*/true);
            acclog::Write("UnifiedMenu", "Shift+nav action_id=0x%x desc=\"%s\"",
                actionId, text);
        } else {
            prism::Speak(acc::strings::Get(acc::strings::Id::NoTooltipAvailable),
                         /*interrupt=*/true);
            acclog::Write("UnifiedMenu", "Shift+nav action_id=0x%x no desc",
                actionId);
        }
        return true;
    }

    switch (code) {
        case kInputNavLeft:
        case kInputNavRight: {
            int dir = (code == kInputNavRight) ? +1 : -1;
            int nx = g.curCat + dir;
            if (nx < 0 || nx >= g.catCount) {
                // Clamp — re-speak the current category as the edge cue.
                acclog::Write("UnifiedMenu", "%s at edge cat=%d/%d",
                    dir > 0 ? "Right" : "Left", g.curCat, g.catCount);
                SpeakCategory(tam, mi, /*prefix=*/nullptr);
                return true;
            }
            g.curCat = nx;
            SpeakCategory(tam, mi, /*prefix=*/nullptr);
            return true;
        }
        case kInputCatFirst:
        case kInputCatLast: {
            g.curCat = (code == kInputCatFirst) ? 0 : g.catCount - 1;
            SpeakCategory(tam, mi, /*prefix=*/nullptr);
            return true;
        }
        case kInputNavUp:
        case kInputNavDown: {
            // Up = previous entry (toward Home), Down = next (toward End).
            // Clamp at both ends; the repeated label is the edge cue.
            if (count > 1) {
                int dir = (code == kInputNavDown) ? +1 : -1;
                sel = ClampInt(sel + dir, 0, count - 1);
                ApplySelection(tam, mi, cur, sel);
            }
            SpeakEntry(tam, mi);
            return true;
        }
        case kInputHome:
        case kInputEnd: {
            if (count > 0) {
                sel = (code == kInputHome) ? 0 : count - 1;
                ApplySelection(tam, mi, cur, sel);
            }
            SpeakEntry(tam, mi);
            return true;
        }
        case kInputEnter1:
        case kInputEnter2: {
            ApplySelection(tam, mi, cur, sel);
            char label[128] = "";
            ReadLabel(tam, mi, cur, sel, label, sizeof(label));

            acc::combat::queue::ReportPrePressDepth();
            acc::combat_diag::LogPreFire("menu-enter");
            bool ok = Dispatch(tam, mi, cur);
            acc::combat_diag::LogPostFire("menu-enter");

            int preDepth = acc::combat::queue::GetPrePressDepth();
            const bool capHit  = (preDepth >= 4);
            const int  slotNum = preDepth + 1;
            char msg[192];
            if (capHit) {
                std::snprintf(msg, sizeof(msg),
                              acc::strings::Get(acc::strings::Id::FmtFireQueueFull),
                              label[0] ? label : "?");
            } else {
                std::snprintf(msg, sizeof(msg),
                              acc::strings::Get(acc::strings::Id::FmtFireAtPosition),
                              label[0] ? label : "?", slotNum);
            }
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("UnifiedMenu", "ENTER kind=%s slot=%d idx=%d label=[%s] "
                "ok=%d pre=%d slot=%d capHit=%d -> [%s]",
                cur.kind == CatKind::Target ? "target" : "personal",
                cur.slot, sel, label, ok ? 1 : 0, preDepth, slotNum,
                capHit ? 1 : 0, msg);

            ForceDisarm("enter");
            return true;
        }
        default:
            return false;
    }
}

}  // namespace acc::unified_menu
