#include "unified_action_menu.h"

#include <cstdio>
#include <cstring>

#include "combat.h"             // IsPartyInCombat — out-of-combat fire-and-close
#include "combat_diag.h"        // LogPreFire / LogPostFire around dispatch
#include "combat_queue.h"       // ArmUserQueueAdd — attribute the AddAction
#include "engine_actionbar.h"   // personal block read + primitives
#include "engine_area.h"        // ResolveServerObjectHandle, kInvalidObjectId
#include "engine_input.h"       // kInputNav*, kInputEnter*, kInputEsc*,
                                // kInputHome/End, kInputCatFirst/Last
#include "engine_offsets.h"      // kInvalidObjectId
#include "engine_options.h"     // GetActionMenuAutoPause (vanilla pause parity)
#include "engine_panels.h"      // IsForegroundUiBlocking (arm-time panel gate)
#include "engine_picker.h"      // ReanchorRadial (per-press target re-anchor)
#include "engine_player.h"      // SetLeaderQueueModeBit (append-vs-replace)
#include "engine_radial.h"      // target block read + primitives
#include "engine_reads.h"       // ResolveActionDescriptionFromActionId
#include "engine_subscreen.h"   // Begin/EndOverlayPause
#include "hotkeys.h"            // ShiftHeld
#include "log.h"
#include "menus_speak.h"
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
    uint32_t unfoldDeclined = 0;    // last narrated handle whose mid-menu
                                    // target-block unfold found no populated
                                    // rows. Suppresses re-running the populate
                                    // chain for that same handle on every
                                    // subsequent keypress; any other narrated
                                    // handle retries. Reset on disarm.
    bool     pausedOnOpen   = false;// did we BeginOverlayPause when arming? Set
                                    // from the "Action Menu" auto-pause option
                                    // (CClientOptions bit 0x8000) at Arm time so
                                    // close/resume only touch the pause we own.
                                    // Off → menu runs over the live world, the
                                    // vanilla behaviour when that option is
                                    // unset (decompile-confirmed; see the
                                    // auto-pause note in action-menu-and-combat).
    bool     live           = false;// KOTOR 2 pad "live" mode: the menu is the
                                    // controller's persistent action surface,
                                    // not a modal the user opens and closes.
                                    // Two consequences, both in this file:
                                    // it never takes the world pause (whatever
                                    // the auto-pause option says), and firing
                                    // never closes it. See RequestLiveArm.
};
State g;

// One-shot: the next Arm() in this dispatch is a live (pad) arm. A request
// rather than an Arm() parameter because the pad opens the menu through the
// ordinary entry points — InteractNarratedTarget → ArmFromRadial, or
// OpenPersonal — and threading a flag through that chain would touch the
// keyboard paths for no reason. Consumed by Arm(), and cleared on disarm so a
// declined open (no populated category) cannot leak into a later keyboard one.
bool g_liveRequest = false;

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
            // KOTOR 2 only. Its main interface fills a fifth column the first
            // game does not have — the party-AI combat stance, Aggressive and
            // three siblings (confirmed live: UnifiedMenu.cols read
            // [4]=4 with "Aggressiv" at index 0). KOTOR 1 never populates it,
            // so BuildCategoryList drops it there and this name is unreachable.
            case 4: return acc::strings::Get(S::MenuCatCombatBehaviour);
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
        int n[kColumnCount] = {0};
        for (int col = 0; col < kColumnCount && g.catCount < kMaxCats; ++col) {
            n[col] = acc::engine_actionbar::VariantCount(mi, col);
            if (n[col] > 0) {
                g.cats[g.catCount++] = {CatKind::Personal, col};
            }
        }
        // Column census, deduped. KOTOR 1 populates four personal columns and
        // binds them to keys 4..7; KOTOR 2's pad action menu is documented as
        // SIX, and whether its PC action bar really fills columns 4 and 5 has
        // never been observed. This walk already reads all six, so the answer
        // is free — and it decides whether those two need number keys of their
        // own. Trace collapses it to one line per distinct shape.
        acclog::Trace("UnifiedMenu.cols", "personal variant counts "
                      "[0]=%d [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d",
                      n[0], n[1], n[2], n[3], n[4], n[5]);
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
    acc::menus::speak::SpeakChoice("UnifiedMenu", label,
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

// Vanilla parity: the radial / personal action menus auto-pause the world only
// when the "Action Menu" auto-pause option is on (CClientOptions bit 0x8000) —
// off by default. Confirmed by decompiling OnTargetUpArrowPressed /
// OnActionUpArrowPressed, which gate their SetAutoPaused(1,7) call on that bit.
// On read failure we default to NOT pausing: matches the vanilla out-of-box
// default and never freezes the world for a user who didn't opt in.
bool ActionMenuAutoPauseEnabled() {
    bool on = false;
    return acc::engine::GetActionMenuAutoPause(on) && on;
}

void Arm() {
    g.suspended = false;
    if (!g.active) {
        g.active = true;
        g.live = g_liveRequest;
        g_liveRequest = false;
        // Live mode never pauses. The whole point of the pad's D-pad action
        // surface is that it sits over a running world the way the engine's
        // own does — pausing it would put back exactly the open/pick/close
        // rhythm it exists to remove. So the auto-pause option is not
        // consulted here; it still governs every keyboard open.
        g.pausedOnOpen = !g.live && ActionMenuAutoPauseEnabled();
        if (g.live) {
            acclog::Write("UnifiedMenu", "open LIVE (pad) — no pause");
        }
        if (g.pausedOnOpen) {
            acc::engine::BeginOverlayPause(
                acc::engine::OverlayPauseOwner::UnifiedMenu);
        } else {
            acclog::Write("UnifiedMenu", "open without pause — Action Menu "
                "auto-pause option off");
        }
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
bool IsLive() { return g.active && g.live; }

void RequestLiveArm(bool on) { g_liveRequest = on; }

// Re-speak the current category against the live menus. Used when a stacked
// overlay (the combat queue) closes back onto this menu: the world stayed
// paused (owner-tracked overlay pause), so the user needs to hear they have
// landed back here at the same position. Mirrors the resume re-speak in
// SetForegroundBlocked, minus the suspend bookkeeping. No-op when inactive or
// suspended (a blocking engine panel still owns input in that case).
void ReannounceCurrent() {
    if (!g.active || g.suspended) return;
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();
    CatKind savedKind = g.cats[g.curCat].kind;
    int     savedSlot = g.cats[g.curCat].slot;
    BuildCategoryList(g.hasTargetBlock ? tam : nullptr, mi);
    if (g.catCount == 0) {
        acclog::Write("UnifiedMenu",
                      "reannounce — all categories empty; disarming");
        ForceDisarm("reannounce-empty");
        return;
    }
    int loc = LocateCat(savedKind, savedSlot);
    g.curCat = (loc >= 0) ? loc : ClampInt(g.curCat, 0, g.catCount - 1);
    acclog::Write("UnifiedMenu", "reannounce cat=%d/%d", g.curCat, g.catCount);
    SpeakCategory(tam, mi, /*prefix=*/nullptr);
}

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
    // idempotent, so this is a no-op when the pause survived. Only when we own
    // the pause (Action Menu auto-pause on); otherwise the menu runs live and
    // there is nothing to re-assert.
    if (g.pausedOnOpen)
        acc::engine::BeginOverlayPause(
            acc::engine::OverlayPauseOwner::UnifiedMenu);

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
    acclog::Write("UnifiedMenu", "disarm — reason=%s live=%d",
                  reason ? reason : "?", g.live ? 1 : 0);
    g.active = false;
    g.suspended = false;
    g.live = false;
    g_liveRequest = false;
    if (g.pausedOnOpen)
        acc::engine::EndOverlayPause(
            acc::engine::OverlayPauseOwner::UnifiedMenu);
    g.pausedOnOpen = false;
    g.catCount = 0;
    g.curCat = 0;
    g.targetHandle = 0;
    g.creature = false;
    g.hasTargetBlock = false;
    g.reqSlot = 0;
    g.targetName[0] = '\0';
    g.unfoldDeclined = 0;
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
        // A menu still armed from an earlier open (possibly against a
        // DIFFERENT target) must not survive this refusal: it kept its old
        // categories navigable after "Spalte N ist leer", so the user
        // browsed and fired the OLD target's menu believing it was the new
        // one (the 13:20 Machtbruch-on-Malak in patch-20260717-131859.log).
        // Worse, the g.targetHandle capture above had already clobbered the
        // armed menu's target. Close it outright — matching the "all
        // categories empty" disarms — so the refusal leaves no ghost menu.
        if (g.active) {
            ForceDisarm("open-empty-row");
        } else {
            g.targetHandle = 0;   // not arming — don't leave a stale target
        }
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

// `requested` distinguishes the two intents that share this body. Shift+4..7
// REQUESTS a column: the user named it, so an empty one is worth saying aloud.
// The pad's live mode does not — it is opening "the action menu", not "Force
// Powers", so a notice about a column nobody asked for would be noise.
bool OpenPersonalImpl(int col, bool requested) {
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

    if (!mi) {
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d — main interface "
            "unresolved; not arming", g.reqSlot);
        return false;
    }

    // The requested column may be empty — Shift+4 self-powers on a non-Jedi or
    // a droid, Shift+6 explosives with no grenades. Say so by name, then open
    // anyway and land on the first populated category.
    //
    // Refusing outright was the old rule ("never silently jump to a different
    // category"), and it read as a menu that would not respond: on T3-M4 every
    // open announced "Own Force Powers: empty" and then nothing at all
    // answered the arrow keys, because nothing had opened. The rule's real
    // intent was don't jump SILENTLY, and this notice is what makes it not
    // silent. It rides in as SpeakCategory's prefix rather than as its own
    // utterance, because SpeakCategory speaks with interrupt=true and would
    // otherwise cancel it — the user hears "<column>: empty. <where you
    // landed>" as one line.
    char emptyNotice[160] = "";
    if (requested && acc::engine_actionbar::VariantCount(mi, g.reqSlot) <= 0) {
        Cat reqCat{CatKind::Personal, g.reqSlot};
        const char* name = CategoryName(reqCat);
        if (name && name[0]) {
            std::snprintf(emptyNotice, sizeof(emptyNotice),
                acc::strings::Get(acc::strings::Id::FmtMenuCategoryEmpty), name);
        } else {
            std::snprintf(emptyNotice, sizeof(emptyNotice),
                acc::strings::Get(acc::strings::Id::FmtActionBarColumnEmpty),
                g.reqSlot + 1);
        }
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d empty -> landing on "
            "first populated category [%s]", g.reqSlot, emptyNotice);
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

    g.hasTargetBlock = hasTarget;   // false → no per-press re-anchor
    g.targetHandle   = hasTarget ? handle : 0;
    g.creature       = hasTarget ? DetectCreature(tam, handle) : false;
    g.targetName[0]  = '\0';

    // Build BEFORE arming. With nothing populated anywhere there is no menu to
    // open, and arming first would take a world pause (and consume a live-arm
    // request) only to hand it straight back.
    BuildCategoryList(tam, mi);  // tam==nullptr → personal block only
    if (g.catCount == 0) {
        if (emptyNotice[0]) prism::Speak(emptyNotice, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "OpenPersonal col=%d — no populated "
            "category anywhere; not arming", g.reqSlot);
        return false;
    }

    Arm();

    // Land on the requested column when it is populated; otherwise on the
    // first category there is. Left/Right reach the rest, including the target
    // rows when folded in.
    int loc = LocateCat(CatKind::Personal, g.reqSlot);
    g.curCat = (loc >= 0) ? loc : 0;

    acclog::Write("UnifiedMenu", "ARMED (personal) col=%d hasTarget=%d "
        "target=0x%08x creature=%d cats=%d curCat=%d", g.reqSlot,
        hasTarget ? 1 : 0, g.targetHandle, g.creature ? 1 : 0,
        g.catCount, g.curCat);
    SpeakCategory(tam, mi, emptyNotice[0] ? emptyNotice : nullptr);
    return true;
}

bool OpenPersonal(int col) { return OpenPersonalImpl(col, /*requested=*/true); }

bool OpenAnyPersonal() { return OpenPersonalImpl(0, /*requested=*/false); }

// Steps of HandleInputEvent below. They are file-local because every one of
// them writes `g` and is only meaningful inside a single input dispatch —
// the HARD RULE is that the menu is populated from input_pipeline and
// nowhere else, so none of this may become reachable from OnUpdate or
// PollHotkey.
namespace {

// Esc — close. When we held the world pause, ForceDisarm → EndOverlayPause
// resumes the world and the engine's pause-resume cue ("Pause aufgehoben")
// is the close announcement (no extra phrase — it was redundant and
// misleading: Esc closes the menu, it doesn't cancel; queued actions stay
// queued and run on resume). When the Action Menu auto-pause option is off
// we never paused, so there is no resume cue — speak an explicit close
// confirmation instead so the user hears the menu dismissed.
void CloseFromEsc() {
// Live mode never held a pause, so there is nothing to resume and no engine
// resume cue to serve as the close announcement. Speak the close and leave
// the world exactly as it was — resuming a pause we did not take would undo
// the player's own tactical pause. (Only a keyboard Esc reaches here in live
// mode; the pad's B stays with the engine's cancel.)
if (g.live) {
    ForceDisarm("esc-live");
    prism::Speak(acc::strings::Get(acc::strings::Id::ActionMenuClosed),
                 /*interrupt=*/true);
    return;
}
const bool wasPaused   = g.pausedOnOpen;
const bool outOfCombat = !acc::combat::IsPartyInCombat();
ForceDisarm("esc");
if (outOfCombat) {
    // Native sub-screens unpause the world on Esc; the unified menu now
    // matches that out of combat. ForceDisarm already released our own
    // overlay pause (auto-pause option on) via EndOverlayPause; if the
    // world is STILL paused that is the player's own manual pause, so
    // resume it here — the engine's "Fortgesetzt" resume cue is then the
    // close announcement. When nothing resumed (never paused, or the
    // overlay release already unpaused silently) speak an explicit close
    // confirmation so the close is never silent.
    if (!acc::engine::ResumeWorldIfPaused("unified-esc")) {
        prism::Speak(acc::strings::Get(acc::strings::Id::ActionMenuClosed),
                     /*interrupt=*/true);
    }
} else if (!wasPaused) {
    // In combat: unchanged. Esc keeps the tactical pause exactly as
    // before — ForceDisarm → EndOverlayPause resumes only the pause we
    // owned; the encounter's own pause is untouched.
    prism::Speak(acc::strings::Get(acc::strings::Id::ActionMenuClosed),
                 /*interrupt=*/true);
}
}

// FOLLOW-CYCLING re-anchor: when the narrated target changed while the
// menu sat open (`,`/`.`/Q/E/passive all stamp the same slot), rebuild
// the target rows against the NEW target so the menu always shows that
// target's real options and Enter fires at it. One re-anchor per cycle
// (the handle comparison self-quiesces), so the per-keypress churn that
// originally forced the lazy design doesn't return. The user's selected
// action follows BY IDENTITY: each row's shadow index is re-located to
// the entry with the same action_id, so cycle → Enter casts the same
// power at the next target. A row whose previous action_id is absent on
// the new target marks its selection as not-carried; Enter (only) is
// then consumed as orientation instead of firing something unheard —
// see the Enter case.
// Returns true when the menu was actually re-anchored onto the new target.
bool ReanchorToNewTarget(void* tam, uint32_t narrated,
                         bool rowCarried[kRowCount]) {
    bool targetChanged = false;
    uint32_t prevId[kRowCount];
    for (int r = 0; r < kRowCount; ++r) {
        prevId[r] = acc::engine_radial::ReadRowActionIdAtIndex(
            tam, r, g_targetSel[r]);
    }
    if (acc::picker::ReanchorRadial(narrated)) {
        uint32_t oldHandle = g.targetHandle;
        targetChanged  = true;
        g.targetHandle = narrated;
        g.creature     = DetectCreature(tam, narrated);
        if (!acc::engine_radial::ReadTargetName(
                tam, g.targetName, sizeof(g.targetName))) {
            g.targetName[0] = '\0';
        }
        for (int r = 0; r < kRowCount; ++r) {
            int ni = acc::engine_radial::FindRowIndexByActionId(
                tam, r, prevId[r]);
            rowCarried[r] = (ni >= 0);
            if (ni >= 0) g_targetSel[r] = ni;
        }
        acclog::Write("UnifiedMenu",
            "follow-cycle re-anchor 0x%08x -> 0x%08x creature=%d "
            "name=[%s] carried=%d%d%d", oldHandle, narrated,
            g.creature ? 1 : 0, g.targetName,
            rowCarried[0] ? 1 : 0, rowCarried[1] ? 1 : 0,
            rowCarried[2] ? 1 : 0);
    } else {
        acclog::Write("UnifiedMenu",
            "follow-cycle re-anchor FAILED for 0x%08x — keeping "
            "0x%08x", narrated, g.targetHandle);
    }
    return targetChanged;
}

// UNFOLD: the menu is personal-only (opened without a target, or
// its target was lost) and the user cycled onto something.
// Populate the target rows against it and fold them in — the
// mid-menu equivalent of OpenPersonal's open-time fold-in, which
// could not happen back then because nothing was narrated. Rows
// empty → target has no actions → stay personal-only and don't
// re-run the populate chain for this handle on every keypress.
// Returns true when the target block was folded in.
bool UnfoldTargetBlock(void* tam, uint32_t narrated,
                       bool rowCarried[kRowCount]) {
    bool targetChanged = false;
    if (acc::picker::ReanchorRadial(narrated)) {
        if (FirstPopulatedTargetRow(tam) >= 0) {
            targetChanged    = true;
            g.hasTargetBlock = true;
            g.targetHandle   = narrated;
            g.creature       = DetectCreature(tam, narrated);
            if (!acc::engine_radial::ReadTargetName(
                    tam, g.targetName, sizeof(g.targetName))) {
                g.targetName[0] = '\0';
            }
            g.unfoldDeclined = 0;
            // Nothing in the new rows was ever selected by the user
            // in this menu session — treat all as not-carried so an
            // Enter that somehow lands there orients instead of
            // firing. The user's personal category re-locates and
            // stays carried, so Enter there fires as normal.
            rowCarried[0] = rowCarried[1] = rowCarried[2] = false;
            acclog::Write("UnifiedMenu",
                "follow-cycle unfold target=0x%08x creature=%d "
                "name=[%s]", narrated, g.creature ? 1 : 0,
                g.targetName);
        } else {
            g.unfoldDeclined = narrated;
            acclog::Write("UnifiedMenu",
                "follow-cycle unfold declined 0x%08x — no target "
                "actions", narrated);
        }
    }
    return targetChanged;
}

// LAZY re-anchor: re-anchoring on EVERY keypress broke target-menu
// navigation (the engine's per-press re-derivation churned the rows so
// arrows produced nothing). Instead, only re-anchor to RESTORE the menu
// when the engine has actually drained our target rows out from under us
// (the cursor-coupling case, project_radial_cursor_coupling). In the
// normal paused-overlay case the rows persist, so we skip re-anchor and
// navigate the stable snapshot — exactly like the personal-only menu.
// Skipped when the follow-cycle re-anchor above already rebuilt this
// press (an actionless new target legitimately has zero rows).
void MaybeRestoreDrainedRows(void*& tam, bool targetChanged) {
    if (!targetChanged && g.hasTargetBlock && g.targetHandle != 0 && tam) {
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
}

// Shift + any arrow — describe the selected entry, no movement.
void DescribeSelectedEntry(void* tam, void* mi, const Cat& cur, int sel) {
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
}

// DoTargetAction dispatches at the creature_id baked into the
// matched list descriptor at PopulateMenus time — and while the
// menu sits open the engine re-bakes the lists against its own
// current target (combat reassert / cursor hover), so an Enter
// seconds after arming fired at the wrong object (Machtbruch
// meant for a Gefangener-Jedi container hit Malak, patch-
// 20260717-131859.log). Restamp the whole row with the menu's
// armed target right before dispatch: raw field writes, safe
// from this poll context (unlike RePopulate — phantom-confirm,
// see OpenTarget). Same client-handle convention as input_
// pipeline's PrepareBareDispatchForNarratedTarget.
void RestampTargetRowForDispatch(void* tam, const Cat& cur) {
    if (cur.kind == CatKind::Target && g.targetHandle != 0) {
        uint32_t targetClient = (g.targetHandle & 0x80000000u)
            ? g.targetHandle
            : (g.targetHandle | 0x80000000u);
        (void)acc::engine_radial::RetargetRowActions(
            tam, cur.slot, targetClient);
    }
}

// Force the engine's APPEND path. Both DoPersonalAction and
// DoTargetAction wipe the leader's action queue before dispatching
// unless its combat-mode bit (field200_0x440 bit 0) is set — that
// bit is the native Shift-held "queue" flag. Our synthetic dispatch
// bypasses the engine's shift capture, so without this every Enter
// overwrites the previous queued action (observed 2026-06-08:
// Macht-Tapferkeit then Kurieren left only Kurieren). Set it for
// the dispatch, restore afterward so the creature's real combat
// mode is untouched.
bool DispatchWithQueueAppend(void* tam, void* mi, const Cat& cur) {
    int prevQueueBit = acc::engine::SetLeaderQueueModeBit(1);

    acc::combat_diag::LogPreFire("menu-enter");
    // Attribute the AddAction this dispatch triggers to the user so its
    // detour speaks the "X, Platz N" cue (see ArmUserQueueAdd).
    acc::combat::queue::ArmUserQueueAdd();
    bool ok = Dispatch(tam, mi, cur);
    acc::combat_diag::LogPostFire("menu-enter");

    if (prevQueueBit >= 0) acc::engine::SetLeaderQueueModeBit(prevQueueBit);
    return ok;
}

// What happens to the menu after a successful fire.
//
// Out of combat, the pause state picks the interaction model:
//
//   World PAUSED (the player pressed the pause key, or the
//   Action Menu auto-pause option froze the world on open) →
//   STACK MODE: queue this action and stay armed so several
//   actions can be lined up without re-opening the menu between
//   each, exactly like the in-combat menu. The world stays
//   paused; Esc (or a manual unpause) commits the queue and
//   closes. The "<action>, Platz N" cue from the AddAction hook
//   is the confirmation that the menu stayed open on this entry.
//
//   World RUNNING → fire-and-close, matching the sighted mouse
//   radial (click an action → it runs → the radial closes).
//   Keeping the menu open here just adds an Esc step vanilla
//   never charges, and a lingering live surface has misfired
//   (patch-20260617-215141.log). ForceDisarm is the same close
//   the Esc path runs, and we are in the sanctioned
//   input-dispatch context (HandleInputEvent), so it obeys the
//   HARD RULE (no populate off the poll/Open path).
//
// Combat is the encounter-level truth (IsPartyInCombat), not the
// controlled-leader bit, so Tabbing to a not-yet-engaged member
// mid-fight can't collapse the menu into fire-and-close and
// unpause an active encounter — the confusion that shaped the
// party-in-combat auto-close in combat.cpp.
void ApplyPostFireClosePolicy() {
    // Live mode never closes on a fire. Closing is what a modal does, and the
    // pad's D-pad surface is not one: the user's next press is as likely to be
    // "same action again" or "next category" as it is to be "done". Leaving it
    // armed also keeps the D-pad meaningful — a closed menu would silently
    // hand the next press back to the cycle bindings.
    if (g.live) {
        acclog::Write("UnifiedMenu", "live fire — staying open");
        return;
    }
    if (!acc::combat::IsPartyInCombat()) {
        if (acc::engine::WorldIsPaused()) {
            acclog::Write("UnifiedMenu",
                "out-of-combat fire while paused — staying open "
                "(stack mode); announce via AddAction hook");
            return;
        }
        // Close is silent by design: the action's confirmation was just
        // spoken by the AddAction hook ("<action>, Platz 1" —
        // out-of-combat actions still route through the leader's combat
        // round, and ArmUserQueueAdd above put us in its attribution
        // window).
        acclog::Write("UnifiedMenu",
            "out-of-combat fire (world running) — closing "
            "(fire-and-close)");
        ForceDisarm("fire-out-of-combat");
        return;
    }

    // In combat: stay armed + paused after firing so the user can stack
    // several actions into the engine queue (grenade → force power →
    // attack) without re-pausing between each. The world only resumes —
    // and the queue runs — on Esc (ForceDisarm → EndOverlayPause). The
    // confirmation message ("…, Position N") is the cue that the menu
    // is still open on the same entry; press Enter again to re-queue,
    // or arrow to another category. Selection/category are preserved;
    // the next keypress's BuildCategoryList + LocateCat re-locates on
    // the same slot.
}

// Follow-cycling contract: Enter right after cycling fires
// immediately when the user's selected action exists on the new
// target (selection carried by action_id above). When it does
// NOT — the action is unavailable there, or the whole category
// vanished — firing would dispatch something the user never
// heard. Consume this press as orientation instead: announce
// the new target's menu context; the next Enter fires what was
// just spoken.
bool HandleEnter(void* tam, void* mi, const Cat& cur, int sel,
                 bool targetChanged, bool selectionCarried) {
    if (targetChanged && !selectionCarried) {
        char prefix[160] = "";
        std::snprintf(prefix, sizeof(prefix),
                      acc::strings::Get(
                          acc::strings::Id::FmtInteractRadial),
                      g.targetName);
        acclog::Write("UnifiedMenu", "ENTER after follow-cycle — "
            "selection not carried; announcing instead of firing");
        SpeakCategory(tam, mi, prefix);
        return true;
    }

    ApplySelection(tam, mi, cur, sel);
    char label[128] = "";
    ReadLabel(tam, mi, cur, sel, label, sizeof(label));

    RestampTargetRowForDispatch(tam, cur);
    bool ok = DispatchWithQueueAppend(tam, mi, cur);

    // The queued-action confirmation ("X, Platz N" / "Warteschlange
    // voll") is spoken by the CSWSCombatRound::AddAction detour
    // (queue::OnEngineActionAdded) that Dispatch() triggers — one
    // authoritative cue per real add, shared with the bare-key path.
    // No separate pre/post snapshot here: it raced the queue drain and
    // double-announced against the hook.
    acclog::Write("UnifiedMenu", "ENTER kind=%s slot=%d idx=%d label=[%s] "
        "ok=%d — queued; announce via AddAction hook",
        cur.kind == CatKind::Target ? "target" : "personal",
        cur.slot, sel, label, ok ? 1 : 0);

    ApplyPostFireClosePolicy();
    return true;
}

}  // namespace

// Fire a personal column's selected entry outright, with no menu.
//
// This is what bare 8 does for KOTOR 2's combat-behaviour column, and it is
// deliberately NOT what bare 4..7 do: those keys are the ENGINE's, and the mod
// only announces what the engine fired. Key 8 has no engine action-bar
// binding in either game (both bind 1..9 solely as dialogue-reply keys), so
// nothing fires unless we do it.
//
// On the announcement: speak the label BEFORE dispatching. If the entry turns
// out to be a real combat-round action, the CSWSCombatRound::AddAction detour
// speaks the authoritative "X, Platz N" with interrupt=true and simply
// replaces this line — which is the behaviour bare 4..7 already have. If it is
// a stance toggle that never reaches AddAction, this line is the only
// confirmation there is. Correct either way, without the mod having to know
// which kind of entry it just fired.
bool FirePersonal(int col) {
    if (col < 0 || col >= kColumnCount) return false;
    if (ForegroundPanelBlocks()) return false;

    void* mi = acc::engine_actionbar::ResolveMainInterface();
    if (!mi) {
        acclog::Write("UnifiedMenu", "FirePersonal col=%d — main interface "
            "unresolved", col);
        return false;
    }

    Cat c{CatKind::Personal, col};
    const int count = acc::engine_actionbar::VariantCount(mi, col);
    if (count <= 0) {
        const char* name = CategoryName(c);
        char msg[160];
        if (name && name[0]) {
            std::snprintf(msg, sizeof(msg),
                acc::strings::Get(acc::strings::Id::FmtMenuCategoryEmpty), name);
        } else {
            std::snprintf(msg, sizeof(msg),
                acc::strings::Get(acc::strings::Id::FmtActionBarColumnEmpty),
                col + 1);
        }
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("UnifiedMenu", "FirePersonal col=%d empty -> [%s]",
                      col, msg);
        return false;
    }

    // The shadow is the entry the menu last left the user on, kept in lock-step
    // with the engine's own per-column selection — so this fires what Shift+8
    // would have shown as current.
    int& sel = ShadowFor(c);
    sel = ClampInt(sel, 0, count - 1);
    ApplySelection(/*tam=*/nullptr, mi, c, sel);

    char label[128] = "";
    ReadLabel(/*tam=*/nullptr, mi, c, sel, label, sizeof(label));
    if (label[0]) prism::Speak(label, /*interrupt=*/true);

    const bool ok = DispatchWithQueueAppend(/*tam=*/nullptr, mi, c);
    acclog::Write("UnifiedMenu", "FirePersonal col=%d idx=%d/%d label=[%s] ok=%d",
                  col, sel, count, label, ok ? 1 : 0);
    return ok;
}

bool HandleInputEvent(int code, int value) {
    if (!g.active) return false;
    if (value == 0) return false;

    if (code == kInputEsc1 || code == kInputEsc2) {
        CloseFromEsc();
        return true;
    }

    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    void* mi  = acc::engine_actionbar::ResolveMainInterface();

    // Follow-cycling: the narrated target may have changed while the menu
    // sat open. Re-anchor onto it (menu already had a target block) or
    // unfold one in (menu was personal-only). rowCarried[] records, per
    // target row, whether the user's selected action_id survived the move —
    // it gates Enter into orientation mode below.
    bool targetChanged = false;
    bool selectionCarried = true;
    bool rowCarried[kRowCount] = {true, true, true};
    if (tam) {
        uint32_t narrated = ResolveNarratedServerHandle();
        const bool haveBlock = g.hasTargetBlock && g.targetHandle != 0;
        if (narrated != 0 && haveBlock &&
            (narrated & ~0x80000000u) != (g.targetHandle & ~0x80000000u)) {
            targetChanged = ReanchorToNewTarget(tam, narrated, rowCarried);
        } else if (narrated != 0 && !haveBlock &&
                   narrated != g.unfoldDeclined) {
            targetChanged = UnfoldTargetBlock(tam, narrated, rowCarried);
        }
    }

    MaybeRestoreDrainedRows(tam, targetChanged);

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

    // Selection survives a follow-cycle re-anchor only when the category the
    // user was on still exists AND (for target rows) its action_id was found
    // on the new target. Not-carried gates Enter into orientation mode.
    if (targetChanged) {
        const Cat& c = g.cats[g.curCat];
        selectionCarried = (loc >= 0) &&
            (c.kind != CatKind::Target || rowCarried[c.slot]);
    }

    Cat cur = g.cats[g.curCat];
    int count = CountForCat(tam, mi, cur);
    int& sel = ShadowFor(cur);
    sel = ClampInt(sel, 0, count > 0 ? count - 1 : 0);
    ApplySelection(tam, mi, cur, sel);

    if ((code == kInputNavUp || code == kInputNavDown ||
         code == kInputNavLeft || code == kInputNavRight) &&
        acc::hotkeys::ShiftHeld()) {
        DescribeSelectedEntry(tam, mi, cur, sel);
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
        case kInputEnter2:
            return HandleEnter(tam, mi, cur, sel, targetChanged,
                               selectionCarried);
        default:
            return false;
    }
}

}  // namespace acc::unified_menu
