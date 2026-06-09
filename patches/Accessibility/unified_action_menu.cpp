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
#include "engine_panels.h"      // IsForegroundUiBlocking (arm-time panel gate)
#include "engine_picker.h"      // ReanchorRadial (per-press target re-anchor)
#include "engine_player.h"      // SetLeaderQueueModeBit (append-vs-replace)
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
    bool     active         = false;
    bool     suspended      = false;// a blocking engine panel (MessageBox,
                                    // hotkey-opened sub-screen) sits over the
                                    // menu. We stay armed + keep our state +
                                    // keep our world pause, but stop owning
                                    // input so the panel handles its own keys.
                                    // On the panel's close we resume at the
                                    // same position (parity with native menus
                                    // restoring focus under a dismissed popup).
    Cat      cats[kMaxCats];
    int      catCount       = 0;
    int      curCat         = 0;
    uint32_t targetHandle   = 0;    // server/client handle; 0 = personal-only
    bool     creature       = false;// target is a hostile creature → named rows
    bool     hasTargetBlock = false;// include the 3 target rows in the menu?
    int      reqSlot        = 0;    // category the open shortcut asked for
                                    // (Shift+1..3 row / Shift+4..7 column).
                                    // Held in State, not a local: reading it
                                    // back after the engine-call chain returned
                                    // corrupted values (observed 2026-06-07 —
                                    // the local logged as a pointer/handle while
                                    // globals stayed intact, a stack imbalance
                                    // somewhere in the engine calls). Capturing
                                    // it before the first engine call sidesteps
                                    // the corrupted stack slot.
    char     targetName[64] = "";
};
State g;

int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// True when a real engine panel / modal / dialog owns the foreground. Our
// menu has no GUI panel of its own, so it must never arm over a blocking
// panel: the panel owns input + its own pause, and a menu armed underneath
// would double-consume the shared nav keys (the quit-confirm double-speak
// in patch-20260609-111933.log). The matching auto-disarm — for a blocker
// that appears AFTER we armed (e.g. a hotkey-opened sub-screen) — lives in
// interact_hotkey's per-tick poll. Broader than the old
// HasActiveDialogPanel-only gate: also covers MessageBox, TutorialBox,
// Container / Store, and hotkey-opened sub-screens (the InGameMenu strip
// stays foreground while any sub-screen is drilled).
bool ForegroundPanelBlocks() {
    return acc::engine::IsForegroundUiBlocking();
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

// Append ", N Stück" / ", N Ladungen" to an item entry's label, matching the
// inventory / store suffixes. Resolves the CSWSItem behind the entry's
// item-tagged action_id; no-op for non-item entries (attacks, force powers)
// and single non-charged items. Charged items can't stack, so the two
// suffixes never both fire.
void AppendItemQuantity(void* tam, void* mi, const Cat& c, int idx,
                        char* label, size_t n) {
    void* item = acc::engine::ItemFromActionId(ReadActionId(tam, mi, c, idx));
    if (!item) return;
    char suffix[64] = "";
    int charges = acc::engine::ReadItemCharges(item);
    if (charges >= 0) {
        std::snprintf(suffix, sizeof(suffix),
                      acc::strings::Get(acc::strings::Id::FmtItemChargeSuffix),
                      charges);
    } else {
        int stack = acc::engine::ReadItemStack(item);
        if (stack > 1) {
            std::snprintf(suffix, sizeof(suffix),
                          acc::strings::Get(acc::strings::Id::FmtItemStackSuffix),
                          stack);
        }
    }
    if (!suffix[0]) return;
    size_t len = strnlen(label, n);
    if (len < n) std::snprintf(label + len, n - len, ", %s", suffix);
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

// Decide whether the target block's three rows are the hostile-creature
// Attacks / Force-Powers / Items layout (→ named categories) vs a
// door/placeable/trigger's per-object actions (→ announce by label).
//
// We do NOT rely solely on resolving the target's client handle: an
// extended-cycled FAR target often isn't in the client object array, so
// the vtable downcast (IsCreatureClientTarget) returns false even for a
// real creature (observed 2026-06-07, "mangled menu" bug). The robust
// signal is the action content itself — only hostile-creature rows carry
// the tagged action_ids (force power 0x1000…, feat 0x2000…, item 0x4000…);
// doors/placeables expose only small interface-action ids (0x3ea, 0x3f2…,
// 0x404). So: tagged action present in any row ⇒ creature layout.
bool TargetRowsLookHostileCreature(void* tam) {
    if (!tam) return false;
    for (int r = 0; r < kRowCount; ++r) {
        if (acc::engine_radial::RowActionCount(tam, r) <= 0) continue;
        if (acc::engine_radial::ReadSelectedRowActionId(tam, r) >= 0x10000000u) {
            return true;
        }
    }
    return false;
}

// Combined creature signal: trust the clean vtable downcast for near
// targets, fall back to action-content for far / unresolvable ones.
bool DetectCreature(void* tam, uint32_t handle) {
    return acc::engine_radial::IsCreatureClientTarget(handle) ||
           TargetRowsLookHostileCreature(tam);
}

// ---- speech --------------------------------------------------------------

// Build the full category announce ("Name: label, N Optionen" / "label, N
// Optionen" for unnamed rows) into `out`. ApplySelection must have run.
void FormatCategory(void* tam, void* mi, const Cat& c, int count, int idx,
                    char* out, size_t n) {
    char label[128] = "";
    ReadLabel(tam, mi, c, idx, label, sizeof(label));
    AppendItemQuantity(tam, mi, c, idx, label, sizeof(label));
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
    AppendItemQuantity(tam, mi, c, sel, label, sizeof(label));
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
    g.suspended = false;
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
bool IsSuspended() { return g.active && g.suspended; }

// Panel-stack suspend / resume. The menu owns no engine GUI panel, so when
// the engine pushes a blocking panel over it (a MessageBox, a hotkey-opened
// sub-screen) it must stop owning input — otherwise both the panel and the
// menu consume the same nav keys (the quit-confirm double-speak in
// patch-20260609-111933.log). Unlike a disarm, we keep the menu's state and
// our world pause so the user returns to the same category/entry when the
// panel closes — matching how native engine menus restore focus under a
// dismissed popup (the regression the disarm-only first cut caused). Driven
// per-tick by interact_hotkey with the current foreground-blocked state; only
// the edges do work.
void SetForegroundBlocked(bool blocked) {
    if (!g.active) { g.suspended = false; return; }
    if (blocked == g.suspended) return;  // no edge
    g.suspended = blocked;

    if (blocked) {
        acclog::Write("UnifiedMenu", "suspended — blocking panel over menu");
        // Leave our overlay pause held; the blocking panel manages its own
        // pause on top. We re-assert on resume in case its close churned it.
        return;
    }

    // Resume. The closing panel's own cleanup can clear the world pause
    // (TickInputClassReassert's modal-edge unpause runs for us because we are
    // not a sub-screen), so re-assert our overlay pause — BeginOverlayPause is
    // idempotent, so this is a no-op when the pause survived.
    acc::engine::BeginOverlayPause();

    // Rebuild against the live menus (the engine may have re-populated
    // action_lists while the panel was up) and re-locate the cursor on the
    // same category, mirroring HandleInputEvent's entry rebuild. Then re-speak
    // so the user hears they are back in the menu at the same position.
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();
    CatKind savedKind = g.cats[g.curCat].kind;
    int     savedSlot = g.cats[g.curCat].slot;
    BuildCategoryList(g.hasTargetBlock ? tam : nullptr, mi);
    if (g.catCount == 0) {
        // Everything drained while the panel was up — close cleanly rather
        // than resuming onto an empty menu.
        acclog::Write("UnifiedMenu", "resume — all categories empty; disarming");
        ForceDisarm("resume-empty");
        return;
    }
    int loc = LocateCat(savedKind, savedSlot);
    g.curCat = (loc >= 0) ? loc : ClampInt(g.curCat, 0, g.catCount - 1);
    acclog::Write("UnifiedMenu", "resumed — blocking panel closed; re-speaking "
        "cat=%d/%d", g.curCat, g.catCount);
    SpeakCategory(tam, mi, /*prefix=*/nullptr);
}

void ForceDisarm(const char* reason) {
    if (!g.active) return;
    acclog::Write("UnifiedMenu", "disarm — reason=%s", reason ? reason : "?");
    g.active = false;
    g.suspended = false;
    acc::engine::EndOverlayPause();
    g.catCount = 0;
    g.curCat = 0;
    g.targetHandle = 0;
    g.creature = false;
    g.hasTargetBlock = false;
    g.reqSlot = 0;
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
    g.hasTargetBlock = true;
    g.targetHandle = targetHandle;
    g.creature = DetectCreature(tam, targetHandle);
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
    if (ForegroundPanelBlocks()) {
        acclog::Write("UnifiedMenu", "OpenTarget row=%d — foreground panel; not arming",
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
    // Capture handle + requested row in State before any engine call — the
    // engine-call chain corrupts stack locals (see State::reqSlot).
    g.targetHandle = handle;
    g.reqSlot = row;

    // Read the LIVE target-action menu as-is — do NOT re-populate here. The
    // bare-key path in input_pipeline already re-targets + re-populates BOTH
    // blocks against this same target on the Shift+1..3 press (PopulateMenus
    // rebuilds the target rows too), in the engine's own input-dispatch
    // context. Re-populating again from this tick context is what made the
    // engine synthesise a phantom confirm that fired the menu's first entry
    // (see OpenPersonal note + the 06:55 vs 10:44 logs, 2026-06-07).
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();
    if (!tam || acc::engine_radial::RowActionCount(tam, g.reqSlot) <= 0) {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(acc::strings::Id::FmtActionBarColumnEmpty),
                      g.reqSlot + 1);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "OpenTarget row=%d empty (target=0x%08x)",
            g.reqSlot, g.targetHandle);
        g.targetHandle = 0;   // not arming — don't leave a stale target
        return false;
    }

    Arm();
    g.hasTargetBlock = true;
    g.creature = DetectCreature(tam, g.targetHandle);
    g.targetName[0] = '\0';

    BuildCategoryList(tam, mi);
    int loc = LocateCat(CatKind::Target, g.reqSlot);
    g.curCat = (loc >= 0) ? loc : 0;

    acclog::Write("UnifiedMenu", "ARMED (target) row=%d target=0x%08x creature=%d "
        "cats=%d", g.reqSlot, g.targetHandle, g.creature ? 1 : 0, g.catCount);
    SpeakCategory(tam, mi, /*prefix=*/nullptr);
    return true;
}

bool OpenPersonal(int col) {
    if (col < 0 || col >= kColumnCount) return false;
    if (ForegroundPanelBlocks()) {
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d — foreground panel; not arming",
            col);
        return false;
    }
    // Capture the requested column in State before any engine call — the
    // engine-call chain corrupts stack locals (see State::reqSlot).
    g.reqSlot = col;

    // Shift+4..7 is a PERSONAL entry point, but the menu is meant to be
    // TRULY unified: if a hostile target is in focus we fold its three target
    // rows (Angriffe / Machtkräfte / Gegenstände) in alongside the personal
    // columns, so Left/Right crosses the personal⇄target border freely. The
    // cursor still lands on the requested personal column; the target
    // categories sit one Left away — parity with Shift+1..3 / Shift+Enter.
    // With no focused target the menu stays personal-only, so self-buffs
    // (Shift+5 medpac, stims) still work without an enemy.
    //
    // Read the LIVE main-interface + target-action menu as-is — do NOT call
    // PrepareBareDispatch / RePopulateMainInterface here. The bare-key path in
    // input_pipeline already re-targeted + re-populated BOTH blocks against
    // the narrated target on this same Shift+number press, in the engine's own
    // input-dispatch context. Re-populating again from this tick context made
    // the engine synthesise a phantom confirm one tick later that fired the
    // menu's first entry (verified against the 06:55 vs 10:44 logs, 2026-06-07;
    // see the identical note in OpenTarget).
    void* mi = acc::engine_actionbar::ResolveMainInterface();

    // If the REQUESTED column has no entries (e.g. Shift+4 self-powers on a
    // non-Jedi, Shift+6 explosives with no grenades), announce that column
    // by name and don't open — never silently jump to a different category.
    if (!mi || acc::engine_actionbar::VariantCount(mi, g.reqSlot) <= 0) {
        Cat reqCat{CatKind::Personal, g.reqSlot};
        const char* name = CategoryName(reqCat);
        char msg[160];
        if (name && name[0]) {
            std::snprintf(msg, sizeof(msg),
                acc::strings::Get(acc::strings::Id::FmtMenuCategoryEmpty), name);
        } else {
            std::snprintf(msg, sizeof(msg),
                acc::strings::Get(acc::strings::Id::FmtActionBarColumnEmpty),
                g.reqSlot + 1);
        }
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d empty -> [%s]",
            g.reqSlot, msg);
        return false;
    }

    // Fold in the target block when a live hostile target is focused and its
    // rows are populated. Mirrors OpenTarget's resolution, minus the row
    // requirement: here the personal column is the entry point, so an absent /
    // unpopulated target simply means personal-only (tam stays null).
    uint32_t handle = ResolveNarratedServerHandle();
    void* tam = nullptr;
    if (handle != 0) {
        void* t = acc::engine_radial::ResolveTargetActionMenu();
        if (t && FirstPopulatedTargetRow(t) >= 0) tam = t;
    }
    const bool hasTarget = (tam != nullptr);

    Arm();
    g.hasTargetBlock = hasTarget;   // false → no per-press re-anchor
    g.targetHandle   = hasTarget ? handle : 0;
    g.creature       = hasTarget ? DetectCreature(tam, handle) : false;
    g.targetName[0]  = '\0';

    BuildCategoryList(tam, mi);  // tam==nullptr → personal block only

    // Requested column is populated → land on it (Left/Right reach the rest,
    // including the target rows when folded in).
    int loc = LocateCat(CatKind::Personal, g.reqSlot);
    g.curCat = (loc >= 0) ? loc : 0;

    acclog::Write("UnifiedMenu", "ARMED (personal) col=%d hasTarget=%d "
        "target=0x%08x creature=%d cats=%d curCat=%d", g.reqSlot,
        hasTarget ? 1 : 0, g.targetHandle, g.creature ? 1 : 0,
        g.catCount, g.curCat);
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

    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();

    // LAZY re-anchor: re-anchoring on EVERY keypress broke target-menu
    // navigation (the engine's per-press re-derivation churned the rows so
    // arrows produced nothing). Instead, only re-anchor to RESTORE the menu
    // when the engine has actually drained our target rows out from under us
    // (the cursor-coupling case, project_radial_cursor_coupling). In the
    // normal paused-overlay case the rows persist, so we skip re-anchor and
    // navigate the stable snapshot — exactly like the personal-only menu.
    if (g.hasTargetBlock && g.targetHandle != 0 && tam) {
        int t = acc::engine_radial::RowActionCount(tam, 0) +
                acc::engine_radial::RowActionCount(tam, 1) +
                acc::engine_radial::RowActionCount(tam, 2);
        if (t == 0) {
            acclog::Write("UnifiedMenu", "target rows drained — re-anchoring "
                "0x%08x", g.targetHandle);
            if (!acc::picker::ReanchorRadial(g.targetHandle)) {
                g.targetHandle = 0;  // gone for good → keep personal block
            }
            tam = acc::engine_radial::ResolveTargetActionMenu();
        }
    }

    // Rebuild the category list (target rows may have changed on re-anchor)
    // and re-locate the cursor on the same category, clamping if it drained.
    // Personal-only menus pass tam=nullptr so the target rows never get
    // folded back in mid-navigation.
    CatKind savedKind = g.cats[g.curCat].kind;
    int     savedSlot = g.cats[g.curCat].slot;
    BuildCategoryList(g.hasTargetBlock ? tam : nullptr, mi);
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

            // Force the engine's APPEND path. Both DoPersonalAction and
            // DoTargetAction wipe the leader's action queue before dispatching
            // unless its combat-mode bit (field200_0x440 bit 0) is set — that
            // bit is the native Shift-held "queue" flag. Our synthetic dispatch
            // bypasses the engine's shift capture, so without this every Enter
            // overwrites the previous queued action (observed 2026-06-08:
            // Macht-Tapferkeit then Kurieren left only Kurieren). Set it for
            // the dispatch, restore afterward so the creature's real combat
            // mode is untouched.
            int prevQueueBit = acc::engine::SetLeaderQueueModeBit(1);

            acc::combat::queue::ReportPrePressDepth();
            acc::combat_diag::LogPreFire("menu-enter");
            bool ok = Dispatch(tam, mi, cur);
            acc::combat_diag::LogPostFire("menu-enter");

            if (prevQueueBit >= 0) acc::engine::SetLeaderQueueModeBit(prevQueueBit);

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

            // Stay armed + paused after firing so the user can stack several
            // actions into the engine queue (grenade → force power → attack)
            // without re-pausing between each. The world only resumes — and
            // the queue runs — on Esc (ForceDisarm → EndOverlayPause). The
            // confirmation message ("…, Position N") is the cue that the menu
            // is still open on the same entry; press Enter again to re-queue,
            // or arrow to another category. Selection/category are preserved;
            // the next keypress's BuildCategoryList + LocateCat re-locates on
            // the same slot.
            return true;
        }
        default:
            return false;
    }
}

}  // namespace acc::unified_menu
