#include "interact_hotkey.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "actionbar_menu.h"
#include "combat_query.h"   // Phase 2C — Shift+H Examine + Phase 2A PC stat read
#include "combat_queue.h"   // Phase 3A — action-queue submenu (Shift+K)
#include "cycle_state.h"
#include "engine_actionbar.h"
#include "engine_area.h"
#include "engine_input.h"   // kInputEnter1 / kInputNavUp/Down/Left/Right
#include "engine_levelup.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_picker.h"
#include "engine_player.h"
#include "engine_radial.h"
#include "filter_objects.h"
#include "guidance_autowalk.h"
#include "hotkeys.h"
#include "log.h"
#include "passive_narrate.h"
#include "radial_menu.h"
#include "strings.h"
#include "tolk.h"
#include "view_mode.h"      // IsActive() — Enter is owned by view_mode
                            // while active (lay-off 5)

// Engine entry points used by 9b. Kept at file scope for callsite
// brevity, matching engine_manager.h / engine_player.h convention.
//
// CClientExoApp::SetLastClickedOnTarget(ulong) — sets the engine's
// "last clicked target" handle. The setter the engine itself calls
// every time the user clicks an object in 3D world. Verified live
// 2026-05-04 (probe `Probe: LastTarget changed:` lines logged
// 0x80000004 / 0x800000c6 transitions; the read side is the matching
// CClientExoApp::GetLastTarget @0x005edd80).
constexpr uintptr_t kAddrCClientExoAppSetLastClickedOnTarget = 0x005EE200;

// CClientExoAppInternal::HandleMouseClickInWorld(void) — the engine's
// native click-on-3D-world dispatcher. Reads its target from internal
// click state (LastClickedOnTarget + cursor) and enqueues the
// kind-appropriate AI action against the player creature: walk-to +
// open / talk / loot / pick-up. Takes no args (this only).
constexpr uintptr_t kAddrHandleMouseClickInWorld = 0x00620350;

// kClientExoAppInternalOffset (= 0x4) lives in engine_player.h alongside
// the other client-app chain constants — same chain we walk for
// SetPlayerInputEnabled.

namespace acc::interact {

namespace {

typedef void (__thiscall* PFN_SetLastClickedOnTarget)(void* this_,
                                                     uint32_t handle);
typedef void (__thiscall* PFN_HandleMouseClickInWorld)(void* this_);
typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_);

// Same chain as engine_player's prelude, repeated locally so we don't
// pull in an include cycle for a four-line walk.
void* GetClientExoApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerClientAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetClientExoAppInternal() {
    void* clientApp = GetClientExoApp();
    if (!clientApp) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(clientApp) +
            kClientExoAppInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// CClientExoApp::GetLastTarget — matches the address baked into
// passive_narrate.cpp. We re-read it as the fallback target when the
// user hasn't cycled (cycle_state has no focus).
constexpr uintptr_t kAddrCClientExoAppGetLastTarget = 0x005EDD80;

uint32_t ReadLastTargetHandle() {
    void* exoApp = GetClientExoApp();
    if (!exoApp) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetLastTarget>(
            kAddrCClientExoAppGetLastTarget);
        return fn(exoApp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
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
// Most-recent-event wins between the two independent focus channels:
//   - cycle_state.focusedObj   (mutated by `,`/`.`/Shift+`,`/`.`)
//   - engine LastTarget        (mutated by Q/E, mouse, passive selection)
//
// Without this tie-break, a stale cycle target shadows a fresher Q/E pick:
// e.g. user `,` cycles to a Tür, then Q/E moves engine focus to a Feldkiste,
// presses Enter — Enter would walk to the Tür because cycle_state still
// holds it. Both systems stay live (each finds objects the other can't);
// the timestamp picks whichever the user touched last.
//
// outHandle is populated whenever the resolved object has a usable
// engine handle (CGameObject.id +0x4); needed for the
// SetLastClickedOnTarget call.
void* ResolveInteractTarget(uint32_t* outHandle) {
    *outHandle = 0;

    auto& s = acc::cycle::GetState();

    // Compare ticks via signed difference so wraparound (~49.7d) doesn't
    // flip the order. cycleNewer == true when cycle.mutationTick is at-or-
    // after engine LastTarget's last change — cycle wins ties (it's the
    // explicit nav action; engine LastTarget can be set by passive selection
    // the user didn't initiate).
    unsigned int cycleTick  = s.mutationTick;
    unsigned int engineTick = acc::passive_narrate::LastTargetChangeTick();
    bool cycleNewer = static_cast<int>(cycleTick - engineTick) >= 0;

    if (s.focusedObj && cycleNewer) {
        // RefreshCurrentListing picks up area changes; without it
        // focusedObj could be stale (we haven't cycled keys this tick
        // and the engine moved/destroyed the object). The refresh
        // re-validates by reslotting.
        acc::cycle::CategoryListing listing;
        acc::cycle::RefreshCurrentListing(listing);
        if (s.focusedObj && s.focusedIndex >= 0 &&
            s.focusedIndex < listing.count) {
            void* obj = s.focusedObj;
            *outHandle = acc::engine::GetObjectHandle(obj);
            if (*outHandle != 0) {
                acclog::Write("Interact", "target source=cycle (cycleTick=%u engineTick=%u) "
                    "obj=%p handle=0x%08x",
                    cycleTick, engineTick, obj, *outHandle);
                return obj;
            }
        }
    }

    // Engine LastTarget path — taken when (a) cycle has no focus, or
    // (b) engine LastTarget changed more recently than cycle. LastTarget
    // stores client-side handles (high bit 0x80000000 set);
    // ResolveClientObjectHandle walks to the matching server CSWSObject*,
    // and we re-derive the *server-side* id from CGameObject.id+0x4 — that's
    // the namespace AI-action primitives like AddUseObjectAction operate in.
    // Passing the original client handle silently no-ops the queued action.
    // See memory: project_object_handle_namespaces.md.
    uint32_t handle = ReadLastTargetHandle();
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        // Engine has nothing — last fallback is a stale cycle focus, even
        // when its tick is older than the engine's. Better than dropping
        // the user's interact action entirely.
        if (s.focusedObj) {
            acc::cycle::CategoryListing listing;
            acc::cycle::RefreshCurrentListing(listing);
            if (s.focusedObj && s.focusedIndex >= 0 &&
                s.focusedIndex < listing.count) {
                void* obj = s.focusedObj;
                *outHandle = acc::engine::GetObjectHandle(obj);
                if (*outHandle != 0) {
                    acclog::Write("Interact", "target source=cycle-fallback "
                        "(engine sentinel; cycleTick=%u engineTick=%u) "
                        "obj=%p handle=0x%08x",
                        cycleTick, engineTick, obj, *outHandle);
                    return obj;
                }
            }
        }
        return nullptr;
    }
    void* obj = acc::engine::ResolveClientObjectHandle(handle);
    if (!obj) return nullptr;
    *outHandle = acc::engine::GetObjectHandle(obj);
    if (*outHandle == 0) return nullptr;
    acclog::Write("Interact", "target source=engine (cycleTick=%u engineTick=%u) "
        "lastTarget=0x%08x -> obj=%p handle=0x%08x",
        cycleTick, engineTick, handle, obj, *outHandle);
    return obj;
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
    uint32_t handle = 0;
    void* target = ResolveInteractTarget(&handle);

    if (!target || handle == 0) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
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
        tolk::Speak(msg, /*interrupt=*/true);
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
    acc::picker::ActionSnapshot snap = {};
    bool dispatched = acc::picker::Drive(handle, &snap, forceRadial);

    // Radial-opened path: arm the input gate and speak the row+action
    // announce in one call. ArmAfterPopulate handles the speech itself
    // (so we get "Aktionsmenü, Tür. Aktion 1/N: Öffnen" instead of just
    // "Aktionsmenü, Tür" with no follow-up). Falls back to the static
    // pre-roll string when arming fails (no rows populated → menu isn't
    // actually navigable; keeps prior behaviour).
    char msg[192];
    bool radialArmed = false;
    if (snap.radial_opened) {
        radialArmed = acc::radial_menu::ArmAfterPopulate(name);
        if (!radialArmed) {
            std::snprintf(
                msg, sizeof(msg),
                acc::strings::Get(acc::strings::Id::FmtInteractRadial),
                name);
            tolk::Speak(msg, /*interrupt=*/true);
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
        tolk::Speak(msg, /*interrupt=*/true);
    } else {
        std::snprintf(
            msg, sizeof(msg),
            acc::strings::Get(PreRollFor(cat)), name);
        tolk::Speak(msg, /*interrupt=*/true);
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

    if (dispatched) {
        if (snap.radial_opened) {
            acclog::Write("Interact", "radial opened (no default action) target=0x%08x",
                handle);
        } else {
            acclog::Write("Interact", "engine picker dispatched action_id=0x%x "
                "label=[%s] target=0x%08x",
                snap.action_id, snap.label, handle);
        }
        return;
    }

    // Picker either had no descriptor (engine has no default action for
    // this leader/target) or faulted. Fall back to AddUseObjectAction —
    // it's the right primitive for the simple "walk over and open / talk
    // / pick up" cases that have always worked, and avoids regressing
    // those while the picker is still being shaken down.
    bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);
    bool fallbackOk    = acc::guidance::UseObject(handle);

    if (fallbackOk) {
        acclog::Write("Interact", "fallback UseObject dispatched (input_disabled=%d) "
            "after picker returned valid=%d count=%d",
            inputDisabled ? 1 : 0, snap.valid ? 1 : 0, snap.count);
    } else {
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(
                          acc::strings::Id::FmtInteractFailed),
                      name);
        tolk::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Interact", "dispatch FAILED -> [%s]", failMsg);
    }
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
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("ActionBar", "bare key slot=%d variants=0 -> [%s]",
            slot, msg);
        return;
    }

    // Read the variant at the index the submenu last left us on. Submenu
    // cycle path keeps this index in lock-step with the engine's per-
    // column "currently selected" state via paired CycleNext/Prev calls,
    // so this matches what the engine bare-press fires.
    int idx = acc::actionbar_menu::CurrentSelection(slot);
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
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("ActionBar", "bare key slot=%d variants=%d idx=%d "
            "label=empty -> [%s]",
            slot, nVar, idx, msg);
        return;
    }

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtActionBarFired),
                  label);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("ActionBar", "bare key slot=%d variants=%d idx=%d label=[%s] -> [%s]",
        slot, nVar, idx, label, msg);
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
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("ActionBar", "bare target row=%d count=%d label=[%s] -> [%s]",
            row, count, label, msg);
        return;
    }

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtActionBarFired),
                  label);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write("ActionBar", "bare target row=%d label=[%s] -> [%s]",
        row, label, msg);
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
    bool risingL     = hk::Pressed(hk::Action::LevelUpOpen);
    bool risingS     = hk::Pressed(hk::Action::StatBlockSpeak);
    bool risingEsc   = hk::Pressed(hk::Action::SubmenuEsc);

    // Pressed() already self-gates on foreground; if every action is
    // false we can still need to fall through to combat_query /
    // combat_queue (those self-gate). So don't early-return on no edges.

    Vector unused;
    bool inWorld = acc::engine::GetPlayerPosition(unused);

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
        if (risingOpen1) acc::actionbar_menu::Open(0);
        if (risingOpen2) acc::actionbar_menu::Open(1);
        if (risingOpen3) acc::actionbar_menu::Open(2);
        if (risingOpen4) acc::actionbar_menu::Open(3);

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
            const char* opener = acc::strings::Get(
                acc::strings::Id::LevelUpOpen);
            tolk::Speak(opener, /*interrupt=*/true);
            bool ok = acc::engine_levelup::TriggerLevelUp();
            acclog::Write("Interact", "Shift+L -> [%s] level-up dispatch ok=%d",
                opener, ok ? 1 : 0);
            if (!ok) {
                tolk::Speak(
                    acc::strings::Get(acc::strings::Id::LevelUpFailed),
                    /*interrupt=*/true);
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
    // Skip the announce when our own submenu is active for that column
    // — actionbar_menu's Enter path already speaks "X eingesetzt" via its
    // explicit Tolk call, and the user pressing bare 5 with the submenu
    // open for column 1 would otherwise double-announce. The submenu's
    // Enter consumes via the routing block below, so this only affects
    // the bare-press during submenu-active state, which is an unlikely
    // input pattern but worth handling.
    if (inWorld && !acc::actionbar_menu::IsActive()) {
        if (risingK1) AnnounceBareTargetKey(0);
        if (risingK2) AnnounceBareTargetKey(1);
        if (risingK3) AnnounceBareTargetKey(2);
        if (risingK4) AnnounceBarePersonalKey(0);
        if (risingK5) AnnounceBarePersonalKey(1);
        if (risingK6) AnnounceBarePersonalKey(2);
        if (risingK7) AnnounceBarePersonalKey(3);
    }

    // Combat system, Phase 2C — Shift+H Examine. Self-gated on
    // foreground + in-world inside the helper.
    acc::combat::query::PollWin32Hotkey();

    // Combat system, Phase 3A — Shift+K opens the action-queue submenu.
    // The Open path also self-gates internally; route the submenu's
    // input dispatch below so Up/Down/Enter/Esc reach it while armed.
    acc::combat::queue::PollWin32Hotkey();

    // Combat system, Phase 2A — Shift+S reads the selected-PC full
    // stat block (one-shot speak; no menu state).
    if (inWorld && risingS) {
        acc::combat::query::SpeakSelectedPcStatBlock();
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
        }
        return;
    }

    // Action-bar-active path: route Up/Down/Enter/Esc to the submenu.
    // Esc is included here even though the radial path delegates it to
    // the manager — the action bar is in-world and unbound, so the
    // manager never sees Esc presses while no menu panel is foreground
    // (Esc would normally pop the game menu). Catching it here lets the
    // user back out cleanly without the game-menu side effect.
    if (inWorld && acc::actionbar_menu::IsActive()) {
        if (risingEnter) {
            acc::actionbar_menu::HandleInputEvent(kInputEnter1, /*value=*/1);
        }
        if (risingUp) {
            acc::actionbar_menu::HandleInputEvent(kInputNavUp, 1);
        }
        if (risingDown) {
            acc::actionbar_menu::HandleInputEvent(kInputNavDown, 1);
        }
        if (risingEsc) {
            acc::actionbar_menu::HandleInputEvent(kInputEsc1, 1);
        }
        return;
    }

    // Radial-active path: route Enter + arrows directly to the radial.
    // In-world Enter / arrows bypass CSWGuiManager (engine keymap drops
    // unbound scancodes per memory project_inworld_input_pipeline), so the
    // manager hook in menus.cpp never sees them. We translate the Win32
    // events directly into the radial's logical input vocabulary
    // (kInputEnter1 / kInputNav*). Esc keeps its existing route through
    // the manager hook — Esc IS bound by the engine keymap (pause/options)
    // and reaches the manager normally; routing it here too would
    // double-fire.
    if (inWorld && acc::radial_menu::IsActive()) {
        if (risingEnter) {
            acclog::Write("Interact", "Enter — radial active, dispatching kInputEnter1");
            acc::radial_menu::HandleInputEvent(kInputEnter1, /*value=*/1);
        }
        if (risingUp) {
            acclog::Write("Interact", "Up — radial active, dispatching kInputNavUp");
            acc::radial_menu::HandleInputEvent(kInputNavUp, 1);
        }
        if (risingDown) {
            acclog::Write("Interact", "Down — radial active, dispatching kInputNavDown");
            acc::radial_menu::HandleInputEvent(kInputNavDown, 1);
        }
        if (risingLeft) {
            acclog::Write("Interact", "Left — radial active, dispatching kInputNavLeft");
            acc::radial_menu::HandleInputEvent(kInputNavLeft, 1);
        }
        if (risingRight) {
            acclog::Write("Interact", "Right — radial active, dispatching kInputNavRight");
            acc::radial_menu::HandleInputEvent(kInputNavRight, 1);
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

    OnInteract(forceRadial);
}

}  // namespace acc::interact
