#include "interact_hotkey.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "cycle_state.h"
#include "engine_area.h"
#include "engine_offsets.h"
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
    uint32_t handle = ReadLastTargetHandle();
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        return nullptr;
    }
    void* obj = acc::engine::ResolveClientObjectHandle(handle);
    if (!obj) return nullptr;
    *outHandle = handle;
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
    auto preRollId = PreRollFor(cat);
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(preRollId), name);
    tolk::Speak(msg, /*interrupt=*/true);

    // Pre-fire log — captures intent before the engine call so we can
    // discriminate "engine entry faulted" from "engine entry ran but
    // didn't do anything visible".
    acclog::Write("Interact: Enter -> [%s] target=%p handle=0x%08x cat=%s",
                  msg, target, handle,
                  cat == acc::filter::CycleCategory::Count_
                      ? "(unclassified)"
                      : acc::filter::CategoryName(cat));

    // Disable per-tick player-input movement clobber for the duration of
    // the AI walk-to-then-use that AddUseObjectAction will enqueue. Engine's
    // TickPlayerInputRestore auto-restores after ~3s; on dispatch failure we
    // restore immediately. See project_player_control_toggle.md.
    bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);

    // AddUseObjectAction is the same primitive NWScript's
    // ActionInteractObject calls. Bypasses the engine's two-click
    // hover-then-click pipeline — that path requires hover-state setup
    // (last_target, last_clicked_on_target, hovered_target_at+0x4a4, plus
    // an action descriptor at +0x4c8) which only the cursor-hover system
    // populates. AddUseObjectAction just enqueues ACTION_USEOBJECT (0x28)
    // with the target id, and the engine internally walks-to + uses.
    bool dispatched = acc::guidance::UseObject(handle);

    if (dispatched) {
        acclog::Write("Interact: UseObject dispatched (input_disabled=%d)",
                      inputDisabled ? 1 : 0);
    } else {
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(
                          acc::strings::Id::FmtInteractFailed),
                      name);
        tolk::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Interact: UseObject dispatch FAILED -> [%s]",
                      failMsg);
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

    OnInteract();
}

}  // namespace acc::interact
