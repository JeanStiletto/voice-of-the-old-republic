#include "interact_hotkey.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "combat_query.h"   // Phase 2C — Ö Examine + Phase 2A PC stat read
#include "combat_queue.h"   // Phase 3A — action-queue submenu (Shift+H)
#include "engine_actionbar.h"
#include "engine_area.h"
#include "examine_view.h"   // Phase 2C v2 — navigable Ö examine list
#include "engine_input.h"   // kInputEnter1 / kInputNavUp/Down/Left/Right
#include "engine_levelup.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_picker.h"
#include "engine_player.h"
#include "engine_radial.h"
#include "filter_objects.h"
#include "guidance_approach.h"   // ArmApproach — unified walk-to-act tracker
#include "guidance_autowalk.h"   // UseObject primitive
#include "hotkeys.h"
#include "input_pipeline.h" // NoteOverlayEscClosed — latch the poll-driven
                            // overlay Esc-close so the engine-event consume
                            // guard wins the poll-vs-event race
#include "log.h"
#include "narrated_target.h"
#include "strings.h"
#include "prism.h"
#include "unified_action_menu.h"  // one menu for target + personal actions
#include "view_mode.h"      // IsActive() — Enter is owned by view_mode
                            // while active (lay-off 5)

namespace acc::interact {

namespace {

// Arm the unified approach tracker for an Enter-interact dispatch. The tracker
// (guidance_approach.{h,cpp}) watches the engine's native walk-to-act over the
// walkmesh and announces "way blocked" if it stalls out of range, disarms
// quietly on success (dialog/loot panel opens, or the PC settles within reach),
// and force-restores input on a blocked use-verb. Replaces the old per-file
// g_approach watchdog. `target` is the dispatch object — the live way-blocked
// reference, consistent with what we just acted on.
void ArmInteractApproach(const char* name, void* target, bool inputDisabled,
                         bool isDialog) {
    acc::guidance::ApproachArm arm;
    arm.owner        = acc::guidance::ApproachOwner::Interact;
    std::snprintf(arm.name, sizeof(arm.name), "%s", (name && name[0]) ? name : "?");
    arm.targetObj    = target;
    Vector p{};
    if (target && acc::engine::GetObjectPosition(target, p)) arm.targetPos = p;
    arm.inputDisabled = inputDisabled;
    arm.isDialog      = isDialog;
    arm.speakBlocked  = true;
    acc::guidance::ArmApproach(arm);
}

// True iff the only thing blocking the action menu from opening is the
// in-game menu itself (the Escape menu and its tabs — Inventory, Options,
// Map, Messages, …). The game's own menu hotkeys (J → Messages, M → Map, …)
// switch freely between those screens, so the action-menu openers should
// too: we close the in-game menu back to the world first, then open the menu
// in-world. Message boxes, dialogs, stores and other modal / interaction
// panels stay hard blockers — the engine doesn't let its menu hotkeys switch
// out of those either, and neither do we (the open path refuses over them).
//
// The InGameMenu strip stays the foreground panel while any of its sub-screens
// is drilled (see engine_panels::IsForegroundUiBlocking), so a single fgKind
// check covers every tab.
bool ShouldSwitchFromInGameMenu() {
    acc::engine::UiBlockState blk;
    if (!acc::engine::IsForegroundUiBlocking(&blk)) return false;
    return blk.fgKind == acc::engine::PanelKind::InGameMenu;
}

// Pick the per-kind pre-roll string. Mirrors the cycle/passive_narrate
// kind classification but produces an action verb instead of a label.
acc::strings::Id PreRollFor(acc::filter::CycleCategory c) {
    using C = acc::filter::CycleCategory;
    using S = acc::strings::Id;
    switch (c) {
        case C::Door:       return S::FmtInteractOpen;
        case C::Npc:        return S::FmtInteractTalk;
        case C::Container:  return S::FmtInteractOpen;
        case C::Item:       return S::FmtInteractTake;
        case C::Landmark:   return S::FmtInteractOpen;  // landmark = waypoint
        case C::Transition: return S::FmtInteractOpen;  // transition = doorway
        case C::Count_:     break;
    }
    return S::FmtInteractOpen;
}

// Classify like passive_narrate does — first matching category wins.
// Returns Count_ if the object isn't in any of the six locked categories
// (combat target / dialog target / etc. — we still let the user try
// to interact, just without a localised pre-roll).
acc::filter::CycleCategory ClassifyForInteract(void* obj) {
    using C = acc::filter::CycleCategory;
    for (int i = 0; i < static_cast<int>(C::Count_); ++i) {
        auto c = static_cast<C>(i);
        if (acc::filter::ObjectMatches(obj, c)) return c;
    }
    return C::Count_;
}

// Resolve the "what does the user want to interact with" target.
//
// Unified focus model: the activation target is whatever was last *spoken*
// to the user as a target name. passive_narrate, cycle_input's announce
// path, and view_mode's hover speech all stamp `narrated_target` on a
// successful announcement. This collapses the three previously-independent
// focus channels (cycle_state.focusedObj, engine LastTarget, view-mode
// hover) into a single source of truth keyed on "what did I just hear?".
//
// No fallback: when the slot is empty / stale, the caller treats it as
// "no focus" and speaks GuidanceNoFocus. Falling back to engine LastTarget
// would re-introduce the very inconsistency the unified slot exists to
// remove — engine LastTarget can be set by passive selection / Q/E even
// when the candidate was filtered out (combat target, non-nav kind) and
// never narrated. If the user didn't hear it, Enter shouldn't act on it.
//
// outHandle is populated with the server-side handle (AI-action namespace).
void* ResolveInteractTarget(uint32_t* outHandle) {
    *outHandle = 0;

    acc::narrated_target::Slot slot;
    if (!acc::narrated_target::TryGet(slot)) return nullptr;

    *outHandle = slot.handle;
    acclog::Write("Interact", "target source=narrated (tickStamp=%u) "
        "obj=%p handle=0x%08x",
        slot.tickStamp, slot.obj, slot.handle);
    return slot.obj;
}

// Lay-off-5 refactor (2026-05-06): the post-resolution dispatch flow
// (classify + name + picker + speak + UseObject fallback) is now exposed
// publicly via `acc::interact::DispatchInteract` so view_mode can drive
// the same Enter pipeline with its own target channel (the virtual
// cursor's hover-pause tracker). DispatchInteractImpl keeps the body
// here in the anonymous namespace so it can use the file-internal
// helpers (ClassifyForInteract, PreRollFor, ...); the public symbol
// at the bottom of the file is a thin forwarder.
void DispatchInteractImpl(void* target, uint32_t handle, bool forceRadial);

// PollHotkey's per-press handler. Resolves the target via the
// cycle/LastTarget recency tie-break, then forwards to
// DispatchInteractImpl for the shared dispatch flow. View mode bypasses
// this resolver entirely (its hover channel is the truth) and calls the
// public `acc::interact::DispatchInteract` directly with its own target.
void OnInteract(bool forceRadial) {
    // Map-pin focus: Enter has nothing to dispatch to — pins are
    // destinations, not interactables. Speak the localized hint and
    // redirect to Ctrl+- (beacon) instead of falling into the "no
    // target" silent path. Read the slot directly here because
    // ResolveInteractTarget treats handle=0 as no-target and returns
    // nullptr.
    {
        acc::narrated_target::Slot slot;
        if (acc::narrated_target::TryGet(slot) && slot.isMapPin) {
            const char* msg = acc::strings::Get(
                acc::strings::Id::MapPinInteractHint);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("Interact",
                          "%s -> [%s] (map-pin focus, no interact)",
                          forceRadial ? "Shift+Enter" : "Enter", msg);
            return;
        }
    }

    uint32_t handle = 0;
    void* target = ResolveInteractTarget(&handle);

    if (!target || handle == 0) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Interact", "%s -> [%s] no target",
                      forceRadial ? "Shift+Enter" : "Enter", msg);
        return;
    }

    DispatchInteractImpl(target, handle, forceRadial);
}

void DispatchInteractImpl(void* target, uint32_t handle, bool forceRadial) {
    if (!target || handle == 0) {
        // Defensive — callers should resolve before dispatching, but if
        // they don't, speak the same fallback OnInteract uses so silence
        // isn't ambiguous (cf. `feedback_never_silence_fallback_announcement`).
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Interact", "DispatchInteract called with no target "
            "(forceRadial=%d) -> [%s]",
            forceRadial ? 1 : 0, msg);
        return;
    }

    char name[128] = "";
    if (!acc::engine::GetObjectName(target, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(acc::strings::Id::CategoryItem));
    }

    auto cat = ClassifyForInteract(target);

    // Diagnostic: log the creature that the dispatched action will run
    // against, so we can correlate "Tab swapped leader" against the
    // engine action that fires.
    void*    leader     = acc::engine::GetPlayerServerCreature();
    uint32_t leaderId   = leader ? acc::engine::GetObjectHandle(leader) : 0u;
    char     leaderName[64] = "?";
    if (leader) {
        acc::engine::GetObjectName(leader, leaderName, sizeof(leaderName));
    }
    Vector leaderPos{};
    bool   haveLeaderPos = acc::engine::GetPlayerPosition(leaderPos);
    if (haveLeaderPos) {
        acclog::Write("Interact", "dispatch creature=%p id=0x%08x name=[%s] "
            "pos=(%.2f,%.2f,%.2f)",
            leader, leaderId, leaderName,
            leaderPos.x, leaderPos.y, leaderPos.z);
    } else {
        acclog::Write("Interact", "dispatch creature=%p id=0x%08x name=[%s] pos=?",
            leader, leaderId, leaderName);
    }

    // Transitions are TRIGGER regions that fire on walk-IN, not on "use". The
    // engine action picker has no verb for a trigger, so the dispatch below would
    // fall through to the UseObject fallback, which queues a walk-to-use the
    // engine can't resolve (no use-node) → the PC never moves → false "Weg
    // versperrt". Walk to the trigger's coordinate instead (engine A*, input left
    // enabled like the cycle coord-walk); crossing into the region fires the
    // transition. Same fix as cycle_input::OnPathfindFocus.
    if (cat == acc::filter::CycleCategory::Transition) {
        Vector tpos{};
        if (acc::engine::GetObjectPosition(target, tpos) &&
            acc::guidance::WalkTo(tpos)) {
            char tmsg[192];
            std::snprintf(tmsg, sizeof(tmsg),
                          acc::strings::Get(PreRollFor(cat)), name);
            prism::Speak(tmsg, /*interrupt=*/true);
            // Coord walk leaves input enabled, so the tracker doesn't own input
            // restore (matches the cycle WalkTo path).
            ArmInteractApproach(name, target, /*inputDisabled=*/false,
                                /*isDialog=*/false);
            acclog::Write("Interact", "%s -> [%s] transition trigger via "
                "WalkTo(coord) target=0x%08x pos=(%.2f,%.2f,%.2f)",
                forceRadial ? "Shift+Enter" : "Enter", tmsg, handle,
                tpos.x, tpos.y, tpos.z);
            return;
        }
        // Couldn't resolve the position / WalkTo faulted — fall through to the
        // normal picker pipeline as a backup.
        acclog::Write("Interact", "transition WalkTo dispatch unavailable — "
            "falling through to picker/UseObject");
    }

    // First: try the engine action picker. It runs the same picker the
    // cursor uses on hover (open / talk / Security / Bash / Disable Trap
    // / …) and dispatches the result through the engine's own click
    // pipeline — no per-kind logic in our patch. See
    // docs/engine-action-picker.md.
    //
    // Pre-roll narration: the picker returns the engine's localised verb
    // (e.g. "Sicherheit") in snap.label. We speak that prefixed to the
    // target name when valid; otherwise we keep the per-category fallback
    // string (which still tells the user *something* happened even when
    // the engine refuses to enumerate actions).
    // Populate the descriptor + open the radial when there's no default action,
    // but do NOT dispatch yet — the dispatch block after the announce picks the
    // primitive per verb (use-equivalent talk/open → robust UseObject; the rest
    // → engine click pipeline).
    acc::picker::ActionSnapshot snap = {};
    acc::picker::Drive(handle, &snap, forceRadial, /*populateOnly=*/true);

    // Radial-opened path: arm the input gate and speak the row+action
    // announce in one call. ArmAfterPopulate handles the speech itself
    // (so we get "Aktionsmenü, Tür. Aktion 1/N: Öffnen" instead of just
    // "Aktionsmenü, Tür" with no follow-up). Falls back to the static
    // pre-roll string when arming fails (no rows populated → menu isn't
    // actually navigable; keeps prior behaviour).
    char msg[192];
    bool radialArmed = false;
    if (snap.radial_opened) {
        // Re-anchor with the descriptor's canonical client target_id, NOT
        // `handle`. In some saves `handle` arrives as a pointer-shaped value
        // in the wrong namespace; SetMainInterfaceTarget tolerates it at arm
        // only because passive narration already set the correct target, but
        // a later re-anchor with it corrupts the engine target (observed:
        // DELTA 0x80000046 -> 0x86dfc420 -> empty menu). snap.target_id is
        // the client id the engine actually populated the menu against.
        uint32_t anchorTarget = snap.target_id ? snap.target_id : handle;
        radialArmed = acc::unified_menu::ArmFromRadial(name, anchorTarget);
        if (!radialArmed) {
            // Radial opened but `target_action` rows are all empty
            // (e.g. door-you-can-only-open: Open lives on the engine's
            // default-action descriptor, never enters any radial row).
            // Tell the user there's nothing in the menu rather than
            // speaking the generic "Aktionsmenü, X" pre-roll that
            // implies a menu they can navigate. Shift+Enter gets a
            // redirect to plain Enter (which dispatches the default
            // action when one exists); plain Enter just reports the
            // empty state — suggesting Enter again would be misleading
            // since that's the press that just landed here.
            // WORKAROUND 2026-05-31: caching the chosen Id into a
            // local `phrase` variable, then calling Get(phrase), produced
            // session-persistent garbage values (observed 81, 145, etc.)
            // under /O2 on some loads — same compiled DLL, different
            // sessions disagreed on the runtime int value of the local,
            // and once a load picked a value it stuck for the whole
            // session. Resolving the format string via Get(literal enum
            // constant) on both arms of the ternary sidesteps it.
            // Phrase-local symptom captured in
            // patch-20260531-150602.log; direct-Get fix verified in
            // patch-20260531-151058.log. Don't fold this back into a
            // single phrase variable without re-verifying across cold
            // sessions.
            const char* fmt = forceRadial
                ? acc::strings::Get(acc::strings::Id::FmtInteractNoActionsRedirect)
                : acc::strings::Get(acc::strings::Id::FmtInteractNoActions);
            std::snprintf(msg, sizeof(msg), fmt, name);
            prism::Speak(msg, /*interrupt=*/true);
        } else {
            // ArmAfterPopulate spoke; build a placeholder for the log line
            // so the existing "engine_label=[…]" diagnostic still has a
            // human-readable msg field.
            std::snprintf(msg, sizeof(msg), "Aktionsmenü(%s)", name);
        }
    } else if (snap.valid && snap.label[0] != '\0') {
        std::snprintf(
            msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtInteractEngine),
            snap.label, name);
        prism::Speak(msg, /*interrupt=*/true);
    } else {
        std::snprintf(
            msg, sizeof(msg),
            acc::strings::Get(PreRollFor(cat)), name);
        prism::Speak(msg, /*interrupt=*/true);
    }

    acclog::Write("Interact", "%s -> [%s] target=%p handle=0x%08x cat=%s "
        "engine_label=[%s] engine_action=0x%x engine_count=%d "
        "radial_opened=%d",
        forceRadial ? "Shift+Enter" : "Enter",
        msg, target, handle,
        cat == acc::filter::CycleCategory::Count_
            ? "(unclassified)"
            : acc::filter::CategoryName(cat),
        snap.label, snap.action_id, snap.count,
        snap.radial_opened ? 1 : 0);

    // Radial already opened by the populate-only Drive (no default action /
    // Shift+Enter / locked door). Nothing to dispatch.
    if (snap.radial_opened) {
        acclog::Write("Interact", "radial opened (no default action) target=0x%08x",
            handle);
        return;
    }

    // Dispatch handle = snap.target_id, the client id the engine actually built
    // the descriptor against (clear the high bit for the server namespace AI
    // actions use). This is the authoritative target — it reflects any engine
    // retarget to a linked object — and equals `handle` in the common case. Fall
    // back to the param handle when the descriptor carried no target id.
    uint32_t dispatchHandle = (snap.valid && snap.target_id)
        ? (snap.target_id & ~0x80000000u)
        : handle;
    const bool handleUsable = dispatchHandle != 0u && dispatchHandle != 1u &&
                              dispatchHandle != 0xFFFFFFFFu;

    acclog::Write("Interact", "dispatch handle=0x%08x (param=0x%08x "
        "snap.target_id=0x%08x) usable=%d",
        dispatchHandle, handle, snap.target_id, handleUsable ? 1 : 0);

    if (snap.valid && handleUsable) {
        // Open/use (0x3f7) → AddUseObjectAction via guidance::UseObject. As a
        // direct server action it robustly walks the leader to use-range over
        // rough terrain and triggers the open — the distant-corpse loot fix.
        // Input-disabled is its proven contract; on success engine_player's
        // queue-watched session restores control, on a blocked stall the approach
        // tracker force-restores it.
        //
        // NOT talk: AddUseObjectAction "uses" an object — a creature isn't used,
        // so it walks-then-does-nothing for dialogue. Talk is handled below.
        if (snap.action_id == 0x3f7) {
            bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);
            bool ok = acc::guidance::UseObject(dispatchHandle);
            if (ok) {
                acclog::Write("Interact", "use-verb dispatched via UseObject "
                    "(action_id=0x%x input_disabled=%d) target=0x%08x",
                    snap.action_id, inputDisabled ? 1 : 0, dispatchHandle);
                ArmInteractApproach(name, target, /*inputDisabled=*/true,
                                    /*isDialog=*/false);
                return;
            }
            // UseObject refused — undo the input-disable and fall through to the
            // engine click pipeline as a backup.
            if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        }

        // Talk (0x3ea) → CSWCCreature::ActionInitiateDialog directly, bypassing
        // HandleMouseClickInWorld's first-click/confirm gate that needed two
        // Enter presses. Input stays ENABLED so the engine walks-then-talks
        // server-side (disabling it suppresses the approach — the distant_npc
        // freeze). The approach tracker covers the genuinely-unreachable case.
        if (snap.action_id == 0x3ea) {
            if (acc::picker::InitiateDialog(dispatchHandle)) {
                acclog::Write("Interact", "dialogue dispatched via "
                    "ActionInitiateDialog target=0x%08x", dispatchHandle);
                ArmInteractApproach(name, target, /*inputDisabled=*/false,
                                    /*isDialog=*/true);
                return;
            }
            // InitiateDialog faulted / client creature unresolved — fall through
            // to the engine click pipeline (HandleMouseClickInWorld) as a backup.
        }

        // Everything else — door/mine/bash, attack, … (and talk only as a backup
        // if ActionInitiateDialog above failed) — → engine click pipeline. This
        // second Drive re-asserts the engine target right before the click
        // (defends against a drifting cursor re-pointing the menu) then runs the
        // click-gate + HandleMouseClickInWorld. IsWalkToActVerb verbs keep input
        // enabled per engine_picker's skip so the native walk-then-act runs;
        // attack stays input-disabled.
        bool dispatched = acc::picker::Drive(dispatchHandle, &snap, forceRadial,
                                             /*populateOnly=*/false);
        if (dispatched) {
            acclog::Write("Interact", "engine picker dispatched action_id=0x%x "
                "label=[%s] target=0x%08x", snap.action_id, snap.label,
                dispatchHandle);
            if (acc::picker::IsWalkToActVerb(snap.action_id)) {
                // Walk-to-act verbs leave input enabled, so the tracker doesn't
                // own input restore here.
                ArmInteractApproach(name, target, /*inputDisabled=*/false,
                                    snap.action_id == 0x3ea);
            }
            return;
        }
        // Fall through to the generic fallback below.
    }

    // Picker had no descriptor (engine has no default action for this
    // leader/target) or the engine dispatch faulted. Fall back to
    // AddUseObjectAction — the right primitive for the simple "walk over and
    // open / talk / pick up" cases that have always worked.
    if (handleUsable) {
        bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);
        bool fallbackOk    = acc::guidance::UseObject(dispatchHandle);
        if (fallbackOk) {
            acclog::Write("Interact", "fallback UseObject dispatched "
                "(input_disabled=%d target=0x%08x) after picker returned "
                "valid=%d count=%d",
                inputDisabled ? 1 : 0, dispatchHandle, snap.valid ? 1 : 0,
                snap.count);
            ArmInteractApproach(name, target, /*inputDisabled=*/true,
                                /*isDialog=*/false);
            return;
        }
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
    }

    char failMsg[192];
    std::snprintf(failMsg, sizeof(failMsg),
                  acc::strings::Get(acc::strings::Id::FmtInteractFailed),
                  name);
    prism::Speak(failMsg, /*interrupt=*/true);
    acclog::Write("Interact", "dispatch FAILED (handleUsable=%d dispatchHandle="
        "0x%08x) -> [%s]", handleUsable ? 1 : 0, dispatchHandle, failMsg);
}

// Speak "{label} eingesetzt" (or the empty-column phrase) for a bare-press
// of action-bar key 4..7. Call AFTER the foreground/inWorld gate so the
// announcement matches what the engine actually fires.
//
// Slot mapping per the manual: 4→0 (Friendly Force), 5→1 (Medical),
// 6→2 (Misc), 7→3 (Mine). Engine struct has 6 slots; only 4 hotkey-bound.
//
// We don't suppress the engine's own dispatch (no hook on DoPersonalAction)
// — both fire in parallel, the engine processes the keypress through its
// DirectInput handler, we read the column state at announce time. False
// positives are possible (engine refused to fire e.g. action gated on
// is_action=0); we mirror that by speaking the empty-column phrase rather
// than claiming a fire when is_action is 0.
void AnnounceBarePersonalKey(int slot) {
    // Engine-dispatch gate. ReportPrePressDepth runs only when input_pipeline
    // let the engine's bare-key action through this press; a consumed Shift+combo
    // skips it, so GetPrePressDepth (consume-on-read) returns -1. Without this,
    // the shift-release race (PersonalKey rising after Shift lifts on a Shift+4
    // submenu open) would speak a phantom "X, Platz 0" for an action that never
    // fired. Read it first so every early-out below also stays silent on phantoms.
    int preDepth = acc::combat::queue::GetPrePressDepth();
    if (preDepth < 0) {
        acclog::Write("ActionBar", "bare key slot=%d — no engine dispatch this "
            "press (preDepth=-1); skipping phantom announce", slot);
        return;
    }

    void* mi = acc::engine_actionbar::ResolveMainInterface();
    if (!mi) {
        acclog::Write("ActionBar", "bare key slot=%d — main_interface unresolved",
            slot);
        return;
    }

    int nVar = acc::engine_actionbar::VariantCount(mi, slot);
    if (nVar <= 0) {
        // Column unpopulated — engine almost certainly refused the
        // keypress. Speak the same empty phrase the submenu Open path
        // uses so vocabulary stays consistent across the two routes.
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(
                          acc::strings::Id::FmtActionBarColumnEmpty),
                      slot + 1);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("ActionBar", "bare key slot=%d variants=0 -> [%s]",
            slot, msg);
        return;
    }

    // Read the variant at the index the menu last left us on. The menu's
    // cycle path keeps this index in lock-step with the engine's per-
    // column "currently selected" state via paired SelectVariant calls,
    // so this matches what the engine bare-press fires.
    int idx = acc::unified_menu::PersonalSelection(slot);
    if (idx < 0 || idx >= nVar) idx = 0;

    char label[128] = "";
    acc::engine_actionbar::ReadVariantLabel(mi, slot, idx,
                                            label, sizeof(label));
    if (label[0] == '\0') {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(
                          acc::strings::Id::FmtActionBarColumnEmpty),
                      slot + 1);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("ActionBar", "bare key slot=%d variants=%d idx=%d "
            "label=empty -> [%s]",
            slot, nVar, idx, msg);
        return;
    }

    // Non-empty column: the engine queued (or cap-rejected) the action. The
    // authoritative "X, Platz N" / "Warteschlange voll" cue is spoken by the
    // CSWSCombatRound::AddAction detour (queue::OnEngineActionAdded), which is
    // 1:1 with real adds — no rising-edge under-count on key auto-repeat, no
    // pre/post race against the queue drain. This poll path now only keeps the
    // empty-column feedback above; the successful-queue announce moved to the
    // hook. preDepth is still consumed at the top purely as the phantom-press
    // gate (a consumed Shift+combo / dialog key must not even speak "leer").
    acclog::Write("ActionBar", "bare key slot=%d variants=%d idx=%d label=[%s] "
        "pre=%d — queued; announce via AddAction hook",
        slot, nVar, idx, label, preDepth);
}

// Speak "{label} eingesetzt" for a bare-press of target-action key 1..3.
// Same pattern as AnnounceBarePersonalKey but reads target_actions[row]
// from the embedded CSWGuiTargetActionMenu (which the radial-menu path
// already wraps via engine_radial::ReadRowActionLabel).
//
// Row mapping per the manual: 1→row 0 (leftmost), 2→row 1 (centre),
// 3→row 2 (rightmost). The radial uses the same indices (kRowCount=3).
//
// The radial may be empty when no target is actively cycled (the engine
// only populates target_actions[] after a passive-selection or click).
// In that case RowActionCount==0 → empty phrase; matches the personal-
// column empty path.
void AnnounceBareTargetKey(int row) {
    // Engine-dispatch gate — see AnnounceBarePersonalKey. -1 means this press
    // didn't fire a bare engine action (consumed Shift+combo); stay silent.
    int preDepth = acc::combat::queue::GetPrePressDepth();
    if (preDepth < 0) {
        acclog::Write("ActionBar", "bare target row=%d — no engine dispatch this "
            "press (preDepth=-1); skipping phantom announce", row);
        return;
    }

    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        acclog::Write("ActionBar", "bare target row=%d — TAM unresolved",
            row);
        return;
    }

    int count = acc::engine_radial::RowActionCount(tam, row);
    char label[128] = "";
    acc::engine_radial::ReadRowActionLabel(tam, row, label, sizeof(label));

    if (count <= 0 || label[0] == '\0') {
        // Same empty-column phrase shape — keeps the announce vocabulary
        // consistent across both action-bar groups. Use row+1 for the
        // user-visible 1..3 indexing the manual documents.
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(
                          acc::strings::Id::FmtActionBarColumnEmpty),
                      row + 1);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("ActionBar", "bare target row=%d count=%d label=[%s] -> [%s]",
            row, count, label, msg);
        return;
    }

    // Non-empty row: the authoritative "X, Platz N" / "Warteschlange voll"
    // cue is spoken by the CSWSCombatRound::AddAction detour
    // (queue::OnEngineActionAdded) — see AnnounceBarePersonalKey. This poll
    // path keeps only the empty-row feedback above. preDepth stays consumed at
    // the top as the phantom-press gate.
    acclog::Write("ActionBar", "bare target row=%d label=[%s] "
        "pre=%d — queued; announce via AddAction hook", row, label, preDepth);
}

}  // namespace

// Public seam introduced 2026-05-06 (lay-off 5). Thin forwarder into the
// anonymous-namespace implementation so view_mode can drive the same
// dispatch path PollHotkey runs after its own target resolution.
void DispatchInteract(void* target, uint32_t handle, bool forceRadial) {
    DispatchInteractImpl(target, handle, forceRadial);
}

void PollHotkey() {
    // Every rising-edge below comes from the central hotkey registry —
    // see hotkeys.h / hotkeys.cpp for the binding table. The registry
    // tracks per-Action `last` state internally so this function no
    // longer carries its own `s_prev*` statics.
    namespace hk = acc::hotkeys;

    bool risingEnterPlain = hk::Pressed(hk::Action::InteractTarget);
    bool risingEnterForce = hk::Pressed(hk::Action::InteractForceRadial);
    bool risingEnter      = risingEnterPlain || risingEnterForce;
    bool risingUp    = hk::Pressed(hk::Action::NavUp);
    bool risingDown  = hk::Pressed(hk::Action::NavDown);
    bool risingLeft  = hk::Pressed(hk::Action::NavLeft);
    bool risingRight = hk::Pressed(hk::Action::NavRight);
    bool risingHome  = hk::Pressed(hk::Action::NavHome);
    bool risingEnd   = hk::Pressed(hk::Action::NavEnd);
    bool risingK1    = hk::Pressed(hk::Action::TargetKey1);
    bool risingK2    = hk::Pressed(hk::Action::TargetKey2);
    bool risingK3    = hk::Pressed(hk::Action::TargetKey3);
    bool risingK4    = hk::Pressed(hk::Action::PersonalKey1);
    bool risingK5    = hk::Pressed(hk::Action::PersonalKey2);
    bool risingK6    = hk::Pressed(hk::Action::PersonalKey3);
    bool risingK7    = hk::Pressed(hk::Action::PersonalKey4);
    bool risingOpen1 = hk::Pressed(hk::Action::ActionBarOpen1);
    bool risingOpen2 = hk::Pressed(hk::Action::ActionBarOpen2);
    bool risingOpen3 = hk::Pressed(hk::Action::ActionBarOpen3);
    bool risingOpen4 = hk::Pressed(hk::Action::ActionBarOpen4);
    bool risingOpenT1 = hk::Pressed(hk::Action::TargetActionOpen1);
    bool risingOpenT2 = hk::Pressed(hk::Action::TargetActionOpen2);
    bool risingOpenT3 = hk::Pressed(hk::Action::TargetActionOpen3);
    bool risingL     = hk::Pressed(hk::Action::LevelUpOpen);
    bool risingEsc   = hk::Pressed(hk::Action::SubmenuEsc);

    // Pressed() already self-gates on foreground; if every action is
    // false we can still need to fall through to combat_query /
    // combat_queue (those self-gate). So don't early-return on no edges.

    // Menu-switch parity. The game's built-in menu hotkeys (J, I, M, …) switch
    // directly from one in-game menu screen to another. The action-menu openers
    // (Shift+Enter / Shift+1..7) now do the same: pressing one while the in-game
    // menu is open closes that menu back to the world first — the very close
    // every tab's own Escape runs (HideSWInGameGui(0) to drop the strip +
    // drilled sub-screen and unpause, then SetInputClass(0,1) to restore
    // in-world input; see CloseInGameMenuToWorld) — so the open logic below and
    // the Enter-dispatch gate then run in-world and arm the menu this same tick.
    // GetPlayerPosition reads the player server object (not the GUI status) so it
    // stays true across the close, and the one-shot opener edge isn't lost. Only
    // the in-game menu is switchable; message boxes / dialogs / stores stay hard
    // blockers where the openers still refuse — parity with the engine, whose
    // menu hotkeys don't switch out of those either.
    const bool openerPressed =
        risingEnterForce ||
        risingOpen1 || risingOpen2 || risingOpen3 || risingOpen4 ||
        risingOpenT1 || risingOpenT2 || risingOpenT3;
    if (openerPressed && ShouldSwitchFromInGameMenu()) {
        acclog::Write("Interact",
            "action-menu opener over in-game menu — closing to world (switch)");
        acc::engine::CloseInGameMenuToWorld();
    }

    Vector unused;
    bool inWorld = acc::engine::GetPlayerPosition(unused);

    // Panel-stack integration for the unified action menu. The menu holds a
    // world pause and routes nav / Enter / Esc through this Win32 poll
    // WITHOUT owning an engine GUI panel. When the engine pushes a real
    // blocking panel over the armed menu — a hotkey-opened sub-screen (Map,
    // Journal, …) or a MessageBox (quit-confirm, save-overwrite) — that panel
    // takes the foreground and input, yet the menu would otherwise stay armed
    // and keep consuming the same arrow / Enter keys, so the modal AND our
    // menu both react to one keypress (patch-20260609-111933.log — quit-confirm
    // nav double-spoke "Abbrechen" / "OK" alongside UnifiedMenu entries).
    //
    // Notify the menu of the current foreground-blocked state every tick. It
    // SUSPENDS (stops owning input, keeps its state + pause) while a blocker is
    // up and RESUMES at the same position when the blocker closes — matching
    // how native engine menus restore focus under a dismissed popup, rather
    // than closing outright. Same IsForegroundUiBlocking predicate the Enter-
    // dispatch gate below uses, so suspend/resume and Enter gating stay
    // consistent. The arm-time half of the gate lives in the menu's Open*
    // entry points (they refuse to arm over a blocker).
    acc::unified_menu::SetForegroundBlocked(
        acc::engine::IsForegroundUiBlocking());

    // Action-bar submenu — Shift+4..Shift+7 opens the column's variant
    // submenu (drives column up_button/down_button widgets via vtable[15]
    // activate). Tested before the radial-active block because the action
    // bar lives independently from the radial: a user could in principle
    // open the action-bar submenu while the radial is still armed. We
    // keep the routes distinct by always letting Shift+N take precedence —
    // pressing it while in the radial closes the radial gate (action-bar
    // open path doesn't disarm radial; the radial's own Tick() handles
    // the next disarm via "rows-empty"). Bare 4..7 fall straight through
    // to the engine-native fast-fire path.
    if (inWorld) {
        // Menu-switch: a Shift+number opener pressed while the combat queue
        // (Shift+H) is open switches cleanly to the action menu — close the
        // queue first so it stops shadowing input, then the OpenPersonal /
        // OpenTarget calls below arm / re-point the unified menu this same tick.
        // The owner-tracked overlay pause keeps the world frozen across the
        // switch: the queue's EndOverlayPause clears only its own owner bit, and
        // the menu re-holds (or already holds) the pause in the same input frame,
        // so no world time passes. Mirrors the game's own J / I / M hotkeys
        // switching between screens — the queue, like those, has no engine panel
        // to defer to, so this precedence is purely ours to set.
        const bool numberOpener =
            risingOpen1 || risingOpen2 || risingOpen3 || risingOpen4 ||
            risingOpenT1 || risingOpenT2 || risingOpenT3;
        if (numberOpener && acc::combat::queue::IsActive()) {
            acclog::Write("Interact",
                "Shift+number over combat queue — closing queue, switching to "
                "action menu");
            acc::combat::queue::ForceDisarm("switch-to-action-menu");
        }

        // Slot mapping is LINEAR — key N drives column N-4:
        //   key 4 / Shift+4 → slot 0  Friendly Force
        //   key 5 / Shift+5 → slot 1  Medical
        //   key 6 / Shift+6 → slot 2  Misc (Sonstiges)
        //   key 7 / Shift+7 → slot 3  Explosives (Sprengstoffe)
        // This matches the engine's own DoPersonalAction dispatch, proven
        // from a clean seabed log (patch-20260615-010243): bare 6 fired the
        // Schallgenerator in Sonstiges while bare 7 hit the empty Explosives
        // column. The earlier "engine swaps 6↔7" belief was wrong — it had
        // been read off our own announce, not a real `benutzt` line — and it
        // left the announce/menu pointing at the opposite column from what
        // the engine actually fired (press 7 for Sonstiges, get Explosives).
        if (risingOpen1) acc::unified_menu::OpenPersonal(0);
        if (risingOpen2) acc::unified_menu::OpenPersonal(1);
        if (risingOpen3) acc::unified_menu::OpenPersonal(2);
        if (risingOpen4) acc::unified_menu::OpenPersonal(3);

        // Shift+1..3 — open the unified menu on a target-action row. Direct
        // row mapping (1→row 0, 2→row 1, 3→row 2); the engine routes target
        // keys linearly via DoTargetAction.
        if (risingOpenT1) acc::unified_menu::OpenTarget(0);
        if (risingOpenT2) acc::unified_menu::OpenTarget(1);
        if (risingOpenT3) acc::unified_menu::OpenTarget(2);

        // Shift+L — open the engine's level-up panel directly
        // (CGuiInGame::ShowLevelUpGUI). First-version escape hatch for
        // the tutorial level: navigating into the Charakterblatt and
        // hitting btn_levelup is the vanilla path, but currently the
        // user's chain-walker Enter on the InGameAbilities Powers tab
        // crashes (CSWGuiInGameAbilities::OnEnterPower null deref) —
        // see logs/swkotor.exe.7848.dmp. Bypassing navigation via the
        // engine surface lets the user reach the level-up panel
        // regardless of which screen they're on. The level-up panel
        // itself enumerates as a normal CSWGuiPanel so the existing
        // chain walker handles its child controls once it opens.
        if (risingL) {
            // Dedupe: ShowLevelUpGUI allocates a fresh CSWGuiLevelUpPanel
            // on every dispatch with no engine-side "already open" check,
            // so key-repeat / fast double-tap stacks duplicate modals on
            // CSWGuiManager.modal_stack that the user can't unwind (Esc
            // only pops one at a time; each underlying instance still
            // owns the foreground). See patch-20260530-112606.log — twelve
            // Shift+L presses in four seconds pushed panels.size 3 → 25.
            if (acc::engine::HasActiveLevelUpPanel()) {
                prism::Speak(
                    acc::strings::Get(acc::strings::Id::LevelUpAlreadyOpen),
                    /*interrupt=*/true);
                acclog::Write("Interact",
                    "Shift+L -> already-open guard, skipping dispatch");
            } else if (!acc::engine_levelup::PlayerCanLevelUp()) {
                // Respect the engine's btn_levelup enabled state: the leader
                // hasn't earned the next level (or is level-capped). Without
                // this the forced level_up_mode=1 opened the wizard anyway,
                // letting the player level up endlessly.
                prism::Speak(
                    acc::strings::Get(acc::strings::Id::LevelUpNotReady),
                    /*interrupt=*/true);
                acclog::Write("Interact",
                    "Shift+L -> CanLevelUp=0, refusing (not enough XP / capped)");
            } else {
                const char* opener = acc::strings::Get(
                    acc::strings::Id::LevelUpOpen);
                prism::Speak(opener, /*interrupt=*/true);
                bool ok = acc::engine_levelup::TriggerLevelUp();
                acclog::Write("Interact", "Shift+L -> [%s] level-up dispatch ok=%d",
                    opener, ok ? 1 : 0);
                if (!ok) {
                    prism::Speak(
                        acc::strings::Get(acc::strings::Id::LevelUpFailed),
                        /*interrupt=*/true);
                }
            }
        }
    }

    // Bare 1..3 (target action menu) and bare 4..7 (player action bar)
    // announce path. The engine fires the action through its DirectInput
    // handler regardless of what we do; this branch only adds the
    // screen-reader announcement so the user knows what they fired
    // ("Medikit eingesetzt", "Sicherheit eingesetzt"). No engine-side
    // suppression — both paths run in parallel, same arrangement as
    // passive_narrate alongside Q/E target cycles.
    //
    // We don't gate on the panel-blocker / dialog-panel check used by
    // Enter dispatch. The engine itself filters bare 1..7 in those
    // contexts (e.g. inside a menu screen) — when it doesn't fire, our
    // announce reads a stale label or speaks the empty-column phrase,
    // both of which are recoverable. Matching the gate exactly would
    // require tracking the engine's own input-mode flags, which we don't
    // currently expose.
    //
    // We deliberately do NOT gate this on unified_menu::IsActive(). The
    // unified action menu stays open as a persistent, paused queueing
    // surface: a common workflow is to open it, fire one action via Enter,
    // then spam bare 1 a few times to stack default attacks before Esc'ing
    // out (and repeat per party member). Those bare presses reach the engine
    // dispatch unconditionally (input_pipeline's bare-key prep has no menu
    // gate) and queue normally — but the menu only speaks on Enter, never on
    // number keys, so without announcing here the queued action lands
    // silently. There's no double-announce risk: the menu's HandleInputEvent
    // is never fed number keys (interact_hotkey forwards only Enter / arrows /
    // Home / End / Esc to it), so this poll path is the sole announcer for
    // bare 1..7 whether or not the menu is open.
    //
    // Dialog gate: when a dialog reply listbox is foreground the number
    // keys belong to the reply selection, not the action bar. The engine's
    // combat dispatch is suppressed in that context (see input_pipeline.cpp
    // OnClientHandleInputEvent's matching HasActiveDialogPanel guard), so
    // announcing "X eingesetzt" here would be a phantom cue for an action
    // that never fired. Skip the announce while a dialog owns the keys.
    if (inWorld && !acc::engine::HasActiveDialogPanel()) {
        if (risingK1) AnnounceBareTargetKey(0);
        if (risingK2) AnnounceBareTargetKey(1);
        if (risingK3) AnnounceBareTargetKey(2);
        // Linear slot mapping (key N → column N-4) — matches the engine's
        // bare-key dispatch; see the Open mapping block above.
        if (risingK4) AnnounceBarePersonalKey(0);
        if (risingK5) AnnounceBarePersonalKey(1);
        if (risingK6) AnnounceBarePersonalKey(2);
        if (risingK7) AnnounceBarePersonalKey(3);
    }

    // Combat system, Phase 2C — Ö opens the navigable examine view
    // (synthetic in-DLL listbox). Toggle: pressing again while open closes.
    acc::examine_view::PollWin32Hotkey();

    // Combat system, Phase 3A — Shift+H opens the action-queue submenu.
    // The Open path also self-gates internally; route the submenu's
    // input dispatch below so Up/Down/Enter/Esc reach it while armed.
    acc::combat::queue::PollWin32Hotkey();

    // Bare H — quick HP / effects / equipped-weapon readout for the
    // currently-controlled leader. Self-gates on player-loaded and
    // UI-block (matching Tab leader-announce).
    acc::combat::query::PollWin32SelfStatusHotkey();

    // Examine view input routing — runs FIRST so an open examine view
    // wins arrow / Enter / Esc keys over any other in-world consumer.
    if (inWorld && acc::examine_view::IsActive()) {
        if (risingEnter) {
            acc::examine_view::HandleInputEvent(kInputEnter1, /*value=*/1);
        }
        if (risingUp) {
            acc::examine_view::HandleInputEvent(kInputNavUp, 1);
        }
        if (risingDown) {
            acc::examine_view::HandleInputEvent(kInputNavDown, 1);
        }
        if (risingEsc) {
            acc::examine_view::HandleInputEvent(kInputEsc1, 1);
            acc::input::NoteOverlayEscClosed();
        }
        return;
    }

    // Combat-queue submenu input routing — runs BEFORE actionbar so the
    // queue submenu wins ties (it's a more recent context). Mirrors the
    // actionbar route: Up/Down/Enter/Esc are translated into engine
    // logical input codes and dispatched at the gate handler.
    if (inWorld && acc::combat::queue::IsActive()) {
        if (risingEnter) {
            acc::combat::queue::HandleInputEvent(kInputEnter1, /*value=*/1);
        }
        if (risingUp) {
            acc::combat::queue::HandleInputEvent(kInputNavUp, 1);
        }
        if (risingDown) {
            acc::combat::queue::HandleInputEvent(kInputNavDown, 1);
        }
        if (risingEsc) {
            acc::combat::queue::HandleInputEvent(kInputEsc1, 1);
            acc::input::NoteOverlayEscClosed();
        }
        return;
    }

    // Unified action menu (Shift+Enter / Shift+1..7) — route every nav +
    // dispatch key here while it's armed. In-world Enter / arrows / Home /
    // End bypass CSWGuiManager (the engine keymap drops these unbound
    // scancodes per memory project_inworld_input_pipeline), so we translate
    // the Win32 edges directly into the menu's logical vocabulary. Esc is
    // caught here too (the menu pauses the world via an overlay hold; the
    // engine-pause-menu open is separately suppressed in input_pipeline).
    // Ctrl+Home / Ctrl+End map to the category-jump codes; plain Home / End
    // jump within the current category.
    if (inWorld && acc::unified_menu::IsActive() &&
        !acc::unified_menu::IsSuspended()) {
        const bool ctrl = acc::hotkeys::CtrlHeld();
        if (risingEnter) acc::unified_menu::HandleInputEvent(kInputEnter1, 1);
        if (risingUp)    acc::unified_menu::HandleInputEvent(kInputNavUp, 1);
        if (risingDown)  acc::unified_menu::HandleInputEvent(kInputNavDown, 1);
        if (risingLeft)  acc::unified_menu::HandleInputEvent(kInputNavLeft, 1);
        if (risingRight) acc::unified_menu::HandleInputEvent(kInputNavRight, 1);
        if (risingHome)  acc::unified_menu::HandleInputEvent(
                             ctrl ? kInputCatFirst : kInputHome, 1);
        if (risingEnd)   acc::unified_menu::HandleInputEvent(
                             ctrl ? kInputCatLast : kInputEnd, 1);
        if (risingEsc) {
            acc::unified_menu::HandleInputEvent(kInputEsc1, 1);
            acc::input::NoteOverlayEscClosed();
        }
        return;
    }

    // Non-radial path: Enter (with optional Shift) drives interact.
    if (!risingEnter) return;
    if (!inWorld) return;

    // View mode owns Enter / Shift+Enter routing while active — its hover
    // channel is the truth for what should be acted on, not cycle_state /
    // engine LastTarget. View_mode::Tick polls VK_RETURN itself and
    // dispatches into `DispatchInteract` (or `WalkTo` for empty cursor)
    // earlier in the OnUpdate ordering.
    //
    // Two cases to skip:
    //  1. View mode currently active (rare for Enter to reach here, but
    //     possible if PollEnter's foreground / active gates dropped
    //     somehow).
    //  2. View mode handed Enter off this tick (PollEnter exited view
    //     mode before dispatching, so IsActive() is now false even
    //     though view_mode owns this press). ConsumedEnterThisTick auto-
    //     clears so the flag can't outlive the tick. Verified failure
    //     mode if not gated: WalkTo dispatched then immediately
    //     preempted by OnInteract's stale-LastTarget Dialog action
    //     (patch-20260506-142103.log).
    if (acc::view_mode::IsActive() || acc::view_mode::ConsumedEnterThisTick()) {
        acclog::Write("Interact", "Enter rising — view mode owns this press, "
            "deferring to view_mode::Tick");
        return;
    }

    // The registry split the rising-edge into plain Enter vs Shift+Enter
    // up front (Action::InteractTarget vs Action::InteractForceRadial),
    // so `risingEnterForce` is authoritative — no need to re-poll Shift.
    bool forceRadial = risingEnterForce;
    const char* keyTag = forceRadial ? "Shift+Enter" : "Enter";

    // Gate on "no true-blocker panel is foreground". GetPlayerPosition only
    // confirms we're in-world; it doesn't tell us whether a UI panel is
    // routing input. Shared predicate IsForegroundUiBlocking() lives in
    // engine_panels — same blacklist used by party_leader_announce's Tab
    // gate so the two stay in sync.
    acc::engine::UiBlockState ui;
    if (acc::engine::IsForegroundUiBlocking(&ui)) {
        switch (ui.reason) {
        case acc::engine::UiBlockReason::DialogInStack:
            acclog::Write("Interact",
                          "%s gate -- BLOCKED, dialog panel in stack",
                          keyTag);
            break;
        case acc::engine::UiBlockReason::ForegroundModal:
            acclog::Write("Interact",
                          "%s gate -- BLOCKED, fg=%p kind=%s "
                          "(modal_stack[%d] top)",
                          keyTag, ui.fgPanel,
                          acc::engine::PanelKindName(ui.fgKind),
                          ui.modalStackTop);
            break;
        case acc::engine::UiBlockReason::ForegroundBlockingKind:
            acclog::Write("Interact",
                          "%s gate -- BLOCKED, fg=%p kind=%s",
                          keyTag, ui.fgPanel,
                          acc::engine::PanelKindName(ui.fgKind));
            break;
        default:
            break;
        }
        return;
    }
    if (ui.fgPanel) {
        acclog::Write("Interact", "%s gate -- ALLOW, fg=%p kind=%s",
                      keyTag, ui.fgPanel,
                      acc::engine::PanelKindName(ui.fgKind));
    }

    // Swallow the Enter that just confirmed a save-name editbox. That popup is
    // foreground but classifies as kind=Unknown, so the blocking gate above
    // lets Enter through; the editbox monitor (menus.TickMonitors, earlier this
    // same tick) set the latch when it saw the confirm Enter. Without this the
    // single confirm Enter also queues an ActionInitiateDialog on the narrated
    // target, which fires when the world unpauses on menu-exit. Self-expiring +
    // single-shot, so a genuine later Enter is unaffected.
    if (acc::input::ConsumeEditboxSubmitLatch()) {
        acclog::Write("Interact", "%s swallowed -- editbox submit closed a modal "
                      "this tick (save-name confirm); not dispatching interact",
                      keyTag);
        return;
    }

    OnInteract(forceRadial);
}

}  // namespace acc::interact
