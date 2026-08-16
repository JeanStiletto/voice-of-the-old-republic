#include "interact_dispatch.h"
#include "interact_internal.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "user32.lib")

#include "combat_queue.h"   // Phase 3A — action-queue submenu (Shift+H)
#include "engine_actionbar.h"
#include "engine_area.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_picker.h"
#include "engine_player.h"
#include "engine_radial.h"
#include "filter_objects.h"
#include "guidance_approach.h"   // ArmApproach — unified walk-to-act tracker
#include "guidance_autowalk.h"   // UseObject primitive
                            // overlay Esc-close so the engine-event consume
                            // guard wins the poll-vs-event race
#include "log.h"
#include "narrated_target.h"
#include "strings.h"
#include "prism.h"
#include "unified_action_menu.h"  // one menu for target + personal actions
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
// `useHandle` is the AddUseObjectAction target, and only the two UseObject
// dispatch sites pass it: it lets the tracker's coordinate-walk retry re-fire
// the use on arrival instead of stranding the player at the object (see
// ApproachArm::useHandle). Verbs with nothing to re-fire leave it 0.
void ArmInteractApproach(const char* name, void* target, bool inputDisabled,
                         bool isDialog, uint32_t useHandle = 0) {
    acc::guidance::ApproachArm arm;
    arm.owner        = acc::guidance::ApproachOwner::Interact;
    std::snprintf(arm.name, sizeof(arm.name), "%s", (name && name[0]) ? name : "?");
    arm.targetObj    = target;
    Vector p{};
    if (target && acc::engine::GetObjectPosition(target, p)) arm.targetPos = p;
    arm.inputDisabled = inputDisabled;
    arm.isDialog      = isDialog;
    arm.speakBlocked  = true;
    arm.useHandle     = useHandle;
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
}  // namespace  (seam: published via interact_internal.h)
bool ShouldSwitchFromInGameMenu() {
    acc::engine::UiBlockState blk;
    if (!acc::engine::IsForegroundUiBlocking(&blk)) return false;
    return blk.fgKind == acc::engine::PanelKind::InGameMenu;
}
namespace {

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
}  // namespace  (seam: published via interact_internal.h)
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
namespace {

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

    // WARNING — do NOT add a tag-keyed "this door is sealed, skip the dispatch"
    // shortcut here, however dead the door looks. In this engine the open
    // ATTEMPT is frequently the story trigger, carried by the door's OnOpen /
    // OnFailToOpen script, and returning before the engine call suppresses it:
    //   - end_door19  (Endar Spire): Plot=1, Locked=1, no reachable key — looks
    //     inert, but OnFailToOpen = k_pend_traskdie1 is the Trask-death
    //     sequence. The attempt makes Trask demand you level up first, then
    //     fires his sacrifice. Sealing it (0a4d3a2) stranded the player at the
    //     one door they must push against to advance.
    //   - end_door10_cut2 (Endar Spire): Locked=0 in both blueprint and GIT,
    //     with OnOpen = k_pend_room5_02 — the script that runs the doomed-
    //     soldier battle and the Sith reinforcement wave. Sealing it (6e76ce1)
    //     meant the door never opened, so the tutorial's fight never started
    //     and end_door16 to the bridge never unlocked.
    // Door guidance now hangs off the engine's own "This object is locked"
    // report instead (endar_softlock::RegisterMsgRule), where a line can only
    // ever speak when the door genuinely refused to open — and never replaces
    // the attempt. Put new door hints there, not here.

    // Re-press against the target we are already walking to. The in-flight
    // attempt is by definition not progressing — a progressing one disarms on
    // success — so stacking a second action onto its queue only deepens the
    // churn: patch-20260816-095834.log has the Enter at 10:06:18 landing on the
    // still-armed 10:06:15 approach and driving the pending-action count from 6
    // to 8 with the PC rooted throughout. Tear the stale attempt down, queue
    // included, so this press starts from a clean slate.
    //
    // Same-target only. A press aimed elsewhere is a change of intent, and the
    // fresh ArmApproach already supersedes the old arm. Input is deliberately
    // left as-is: the dispatch below re-disables it anyway, and if this press
    // ends up opening the radial instead, engine_player's queue-watched restore
    // sees the queue we just cleared and hands control back on its own.
    if (acc::guidance::ApproachTarget() == target) {
        acclog::Write("Interact", "re-press on in-flight approach target=%p — "
            "clearing the stale attempt before re-dispatch", target);
        acc::guidance::CancelMovement();
        acc::guidance::CancelApproach();
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

    // Transitions are TRIGGER regions that fire on walk-IN, not on "use", and
    // landmarks are WAYPOINTS — map markers with no physical presence at all.
    // Neither has a use-node, so the engine action picker has no verb for
    // either, and the dispatch below would fall through to the UseObject
    // fallback, which queues a walk-to-use the engine can't resolve → the PC
    // never moves → false "Weg versperrt". Walk to the coordinate instead
    // (engine A*, input left enabled like the cycle coord-walk); for a
    // transition, crossing into the region fires it. Same fix as
    // cycle_input::OnPathfindFocus, which routes both the same way — Shift+-
    // on a landmark walked there correctly all through the beta session while
    // Enter on the same object did not.
    //
    // Landmarks were added here after the beta log showed every one of them
    // taking the picker path and coming back with another object's descriptor
    // (see the Step 3b note in engine_picker). That check now stops the wrong
    // action from being dispatched; this branch is what makes the right thing
    // happen instead of an empty radial.
    if (cat == acc::filter::CycleCategory::Transition ||
        cat == acc::filter::CycleCategory::Landmark) {
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
            acclog::Write("Interact", "%s -> [%s] %s via WalkTo(coord) "
                "target=0x%08x pos=(%.2f,%.2f,%.2f)",
                forceRadial ? "Shift+Enter" : "Enter", tmsg,
                cat == acc::filter::CycleCategory::Landmark
                    ? "landmark waypoint" : "transition trigger",
                handle, tpos.x, tpos.y, tpos.z);
            return;
        }
        // Couldn't resolve the position / WalkTo faulted — fall through to the
        // normal picker pipeline as a backup.
        acclog::Write("Interact", "%s WalkTo dispatch unavailable — "
            "falling through to picker/UseObject",
            cat == acc::filter::CycleCategory::Landmark ? "landmark"
                                                        : "transition");
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
    // kInvalidObjectId belongs in this list and was missing: the engine writes
    // it into a descriptor's target when there is no target, and we dispatched
    // it as if it were an object (patch-20260813-150242 logs the whole
    // sequence three times — "usable=1" on 0x7f000000, then FAILED). The
    // object resolvers in engine_area already treat all four the same way.
    const bool handleUsable = dispatchHandle != 0u && dispatchHandle != 1u &&
                              dispatchHandle != 0xFFFFFFFFu &&
                              dispatchHandle != kInvalidObjectId;

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
                                    /*isDialog=*/false, dispatchHandle);
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
                                /*isDialog=*/false, dispatchHandle);
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
}  // namespace  (seam: published via interact_internal.h)
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
namespace {

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
}  // namespace  (seam: published via interact_internal.h)
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
namespace {

}  // namespace

// Public seam introduced 2026-05-06 (lay-off 5). Thin forwarder into the
// anonymous-namespace implementation so view_mode can drive the same
// dispatch path PollHotkey runs after its own target resolution.
void DispatchInteract(void* target, uint32_t handle, bool forceRadial) {
    DispatchInteractImpl(target, handle, forceRadial);
}

// Public seam for non-keyboard input sources (the KOTOR 2 gamepad's A / X
// buttons). OnInteract is the target-resolution + dispatch half of the Enter
// gesture; the context gate around it lives in the caller, which is why this
// is a forwarder and not a copy of the router's preamble. See the contract
// note in interact_dispatch.h.
void InteractNarratedTarget(bool forceRadial) {
    OnInteract(forceRadial);
}

}  // namespace acc::interact
