#include "interact_hotkey.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "cycle_state.h"
#include "engine_area.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_picker.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "guidance_autowalk.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

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

// Resolve the "what does the user want to interact with" target. Cycle
// focus wins if the user has cycled (signal of explicit intent); else
// fall back to engine LastTarget (passive selection's character-relative
// pick). Returns nullptr if neither is available.
//
// outHandle is populated whenever the resolved object has a usable
// engine handle (CGameObject.id +0x4); needed for the
// SetLastClickedOnTarget call.
void* ResolveInteractTarget(uint32_t* outHandle) {
    *outHandle = 0;

    auto& s = acc::cycle::GetState();
    if (s.focusedObj) {
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
            if (*outHandle != 0) return obj;
        }
    }

    // Fallback: read engine LastTarget (passive-selection-driven).
    // LastTarget stores client-side handles (high bit 0x80000000 set);
    // ResolveClientObjectHandle walks to the matching server CSWSObject*,
    // and we re-derive the *server-side* id from CGameObject.id+0x4 — that's
    // the namespace AI-action primitives like AddUseObjectAction operate in.
    // Passing the original client handle silently no-ops the queued action.
    // See memory: project_object_handle_namespaces.md.
    uint32_t handle = ReadLastTargetHandle();
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        return nullptr;
    }
    void* obj = acc::engine::ResolveClientObjectHandle(handle);
    if (!obj) return nullptr;
    *outHandle = acc::engine::GetObjectHandle(obj);
    if (*outHandle == 0) return nullptr;
    return obj;
}

void OnInteract() {
    uint32_t handle = 0;
    void* target = ResolveInteractTarget(&handle);

    if (!target || handle == 0) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Interact: Enter -> [%s] no target", msg);
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
        acclog::Write(
            "Interact: dispatch creature=%p id=0x%08x name=[%s] "
            "pos=(%.2f,%.2f,%.2f)",
            leader, leaderId, leaderName,
            leaderPos.x, leaderPos.y, leaderPos.z);
    } else {
        acclog::Write(
            "Interact: dispatch creature=%p id=0x%08x name=[%s] pos=?",
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
    bool dispatched = acc::picker::Drive(handle, &snap);

    char msg[192];
    if (snap.radial_opened) {
        std::snprintf(
            msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtInteractRadial),
            name);
    } else if (snap.valid && snap.label[0] != '\0') {
        std::snprintf(
            msg, sizeof(msg),
            acc::strings::Get(acc::strings::Id::FmtInteractEngine),
            snap.label, name);
    } else {
        std::snprintf(
            msg, sizeof(msg),
            acc::strings::Get(PreRollFor(cat)), name);
    }
    tolk::Speak(msg, /*interrupt=*/true);

    acclog::Write(
        "Interact: Enter -> [%s] target=%p handle=0x%08x cat=%s "
        "engine_label=[%s] engine_action=0x%x engine_count=%d "
        "radial_opened=%d",
        msg, target, handle,
        cat == acc::filter::CycleCategory::Count_
            ? "(unclassified)"
            : acc::filter::CategoryName(cat),
        snap.label, snap.action_id, snap.count,
        snap.radial_opened ? 1 : 0);

    if (dispatched) {
        if (snap.radial_opened) {
            acclog::Write(
                "Interact: radial opened (no default action) target=0x%08x",
                handle);
        } else {
            acclog::Write(
                "Interact: engine picker dispatched action_id=0x%x "
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
        acclog::Write(
            "Interact: fallback UseObject dispatched (input_disabled=%d) "
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
        acclog::Write("Interact: dispatch FAILED -> [%s]", failMsg);
    }
}

}  // namespace

void PollHotkey() {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    static bool s_prevEnter = false;
    bool enter = down(VK_RETURN);
    bool risingEnter = enter && !s_prevEnter;
    s_prevEnter = enter;
    if (!risingEnter) return;

    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    // Gate on "no true-blocker panel is foreground". GetPlayerPosition only
    // confirms we're in-world; it doesn't tell us whether a UI panel is
    // routing input. Without this check, Enter pressed inside the loot UI
    // fires both BTN_OK (via menus.cpp's Container handler) AND a stale
    // UseObject on the chest (via this poll).
    //
    // BLACKLIST approach (not whitelist). The engine's panels[] keeps stale
    // entries — closed Options menus, completed Fade overlays, etc. — at
    // the top of the stack for seconds after the user closed them. A
    // whitelist of "in-world overlay" kinds underblocks because those stale
    // entries get reported as fg by GetForegroundPanel. Verified empirically
    // in patch-20260504-105955.log lines 1750+: fg = Fade (12B77D18) for
    // 20+ seconds while the user was walking around and cycling targets.
    //
    // The blacklist names panels that *actually* route Enter to themselves
    // when foreground — loot, shop, examine, conversation, tutorial popup,
    // confirm modal, area-load. If we miss one (Inventory etc. when actually
    // open) Enter will double-fire; that's recoverable. The reverse — over-
    // blocking — leaves Enter dead in the world and is the worse failure
    // mode of the two (already burned us once).
    //
    // Always logs the gate decision so future "Enter did nothing" reports
    // surface here directly without another instrumentation pass.

    // Stack-scan blocker: during dialog reply turns the engine briefly swaps
    // fg to a transient Fade overlay while the actual CSWGuiDialog* panel
    // stays in panels[]. The fg-only blacklist below misses that window —
    // verified in patch-20260505-050419.log lines 2511→2531: BLOCKED on
    // DialogCinematicCopy, then 1s later ALLOW on Fade dispatched
    // [Dialoge end_trask] action 0x3ea on the in-world target. Mirrors the
    // panels[] scan that MonitorDialogReplies already uses; dialog panels
    // do not stay stale (that monitor's disarm path proves it).
    if (acc::engine::HasActiveDialogPanel()) {
        acclog::Write("Interact: Enter gate -- BLOCKED, dialog panel in stack");
        return;
    }

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (mgr) {
        void* fgPanel = acc::engine::GetForegroundPanel(mgr);
        if (fgPanel) {
            acc::engine::PanelKind fgKind = acc::engine::IdentifyPanel(fgPanel);
            bool blocking = false;
            switch (fgKind) {
            case acc::engine::PanelKind::Container:
            case acc::engine::PanelKind::Store:
            case acc::engine::PanelKind::Examine:
            case acc::engine::PanelKind::DialogCinematic:
            case acc::engine::PanelKind::DialogCinematicCopy:
            case acc::engine::PanelKind::DialogComputer:
            case acc::engine::PanelKind::DialogComputerCamera:
            case acc::engine::PanelKind::TutorialBox:
            case acc::engine::PanelKind::MessageBoxModal:
            case acc::engine::PanelKind::StatusSummary:
            case acc::engine::PanelKind::AreaTransition:
            // The in-game icon strip stays foreground while ANY sub-screen
            // (Inventory / Map / Equipment / …) is drilled — see the drill
            // mechanism in menus.cpp. Without this case Enter on a chain
            // target inside a drilled sub-screen would also be picked up
            // here and dispatched as an in-world UseObject (verified
            // 2026-05-04: pressing Enter in equip picker auto-walked the
            // PC to a chest). The strip's own Enter is owned by the chain
            // Enter activate path in menus.cpp; in-world Enter never makes
            // sense while a menu is open.
            case acc::engine::PanelKind::InGameMenu:
                blocking = true;
                break;
            default:
                blocking = false;
                break;
            }
            if (blocking) {
                acclog::Write("Interact: Enter gate -- BLOCKED, fg=%p kind=%s",
                              fgPanel, acc::engine::PanelKindName(fgKind));
                return;
            }
            acclog::Write("Interact: Enter gate -- ALLOW, fg=%p kind=%s",
                          fgPanel, acc::engine::PanelKindName(fgKind));
        }
    }

    OnInteract();
}

}  // namespace acc::interact
