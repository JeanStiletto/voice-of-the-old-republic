#include "passive_narrate.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "audio_bus.h"
#include "audio_cues.h"
#include "combat_query.h"   // BuildTargetCombatBrief enrichment
#include "discovery.h"      // organic-discovery recording
#include "engine_area.h"
#include "engine_player.h"  // kAddrAppManagerPtr + kClientExoAppInternalOffset chain
#include "filter_objects.h"
#include "log.h"
#include "narrated_target.h"
#include "same_name_suffix.h"
#include "spectator_scene.h" // Endar Spire scripted spectator-battle cue
#include "strings.h"
#include "prism.h"

namespace acc::passive_narrate {

// Engine's user-facing target cache. Exposed at namespace scope so the
// extern-"C" OnShowObject handler can update it. Single-threaded, no atomic.
volatile uint32_t s_show_object_handle = 0x7F000000u;

namespace {

// Q/E cycle state. Lives across Tick boundaries; mutated by both the
// input-pipeline edge (RequestQEReannounce) and the engine's downstream
// ShowObject callback (OnEngineShowObject).
//
//   press_active     — a Q/E press is in flight (user-driven or our
//                      synthetic retry). Cleared by OnEngineShowObject
//                      on any resulting ShowObject call, or by Tick's
//                      single-hostile reannounce path.
//   direction_code   — 204 (E forward) / 205 (Q reverse). Saved on
//                      arming so the retry can synthesize the same key.
//   retry_armed      — sentinel landed during press_active; arm Tick
//                      to re-issue the same Q/E next frame.
//   retry_wait       — frames remaining before draining the armed retry.
//                      The engine's candidate halo is rebuilt by
//                      DoPassiveSelection once per frame; waiting one
//                      frame guarantees a fresh list regardless of where
//                      DoPassiveSelection runs relative to our Tick.
//   inside_retry     — Tick is currently executing the synthetic
//                      HandleInputEvent call. Read by IsInSynthesizedQE
//                      so input_pipeline doesn't re-arm press_active for
//                      our own synthesized event, and by OnEngineShowObject
//                      so a second sentinel speaks "Kein Ziel" instead of
//                      arming a third retry.
struct QEState {
    bool press_active   = false;
    int  direction_code = 0;
    bool retry_armed    = false;
    int  retry_wait     = 0;
    bool inside_retry   = false;
};
QEState s_qe;

// Mirrors cycle_input.cpp's mapping. If these get out of sync, lift into
// a shared filter_objects helper.
acc::audio::NavCue ClosedDoorCueForMaterial(void* obj) {
    using N = acc::audio::NavCue;
    switch (acc::engine::GetDoorMaterial(obj)) {
        case acc::engine::DoorMaterial::Wood:  return N::DoorClosedWood;
        case acc::engine::DoorMaterial::Stone: return N::DoorClosedStone;
        case acc::engine::DoorMaterial::Metal: break;
    }
    return N::DoorClosedMetal;
}

// Door cue depends on open_state + material. Other categories ignore obj.
acc::audio::NavCue CueForCategory(acc::filter::CycleCategory c, void* obj) {
    using C = acc::filter::CycleCategory;
    using N = acc::audio::NavCue;
    switch (c) {
        case C::Door:       return acc::engine::IsDoorOpen(obj)
                                    ? N::DoorOpen
                                    : ClosedDoorCueForMaterial(obj);
        case C::Npc:        return N::NpcCreature;
        case C::Container:  return N::ContainerPlaceable;
        case C::Item:       return N::Item;
        case C::Landmark:   return N::Landmark;
        case C::Transition: return N::TransitionExit;
        case C::Count_:     break;
    }
    return N::Item;  // unreachable safety net
}

acc::strings::Id CategoryNameId(acc::filter::CycleCategory c) {
    using C = acc::filter::CycleCategory;
    using S = acc::strings::Id;
    switch (c) {
        case C::Door:       return S::CategoryDoor;
        case C::Npc:        return S::CategoryNpc;
        case C::Container:  return S::CategoryContainer;
        case C::Item:       return S::CategoryItem;
        case C::Landmark:   return S::CategoryLandmark;
        case C::Transition: return S::CategoryTransition;
        case C::Count_:     break;
    }
    return S::CategoryItem;
}

// Returns Count_ for non-nav consumers (combat / dialog target).
acc::filter::CycleCategory ClassifyForNarration(void* obj) {
    using C = acc::filter::CycleCategory;
    for (int i = 0; i < static_cast<int>(C::Count_); ++i) {
        auto c = static_cast<C>(i);
        if (acc::filter::ObjectMatches(obj, c)) return c;
    }
    return C::Count_;
}

// True when obj is one of the active party followers. GetPartyMembers
// returns resolved object handles (NPC slot indices → live creatures), so
// compare against the object's server handle (GetObjectHandle re-derives it
// from the resolved client object). The PC isn't in the follower table, but
// the PC is never a passive-narrate focus (filtered upstream), so followers-
// only is the right set here.
bool IsActivePartyMember(void* obj) {
    if (!obj) return false;
    uint32_t serverHandle = acc::engine::GetObjectHandle(obj);
    if (serverHandle == 0u || serverHandle == 0xFFFFFFFFu) return false;
    uint32_t members[kPartyTableMaxMembers] = {};
    int n = acc::engine::GetPartyMembers(members, kPartyTableMaxMembers);
    for (int i = 0; i < n; ++i) {
        if (members[i] == serverHandle) return true;
    }
    return false;
}

// Resolve → classify → speak → stamp. Used by both the focus-change path
// (OnEngineShowObject delta) and the deferred Q/E re-announce path. The
// two are mutually exclusive by construction — Tick cancels pending if
// OnEngineShowObject sees a real focus change first.
//
// explicitRequest distinguishes a user-driven Q/E cycle from the engine's
// automatic focus-follow. Party members never get the person cue: on the
// auto path they're suppressed entirely (companions trailing the player
// would otherwise spam the cue + name), and on an explicit Q/E request the
// name + status still speak but without the cue.
bool NarrateHandle(uint32_t handle, const char* reason, bool explicitRequest) {
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        return false;
    }

    void* obj = acc::engine::ResolveClientObjectHandle(handle);
    if (!obj) {
        acclog::Write("PassiveNarrate",
            "%s: handle 0x%08x failed to resolve, silent",
            reason, handle);
        return false;
    }

    acc::filter::CycleCategory cat = ClassifyForNarration(obj);
    if (cat == acc::filter::CycleCategory::Count_) {
        int kind = acc::engine::GetObjectKind(obj);
        acclog::Write("PassiveNarrate",
            "%s: handle 0x%08x kind=%d not a nav category, silent",
            reason, handle, kind);
        return false;
    }

    // Party members get no person cue, ever. On the automatic focus-follow
    // path they're suppressed entirely (cue + name) so companions trailing
    // the player don't spam narration; on an explicit Q/E request the name +
    // status still speak, just without the cue.
    bool isParty = IsActivePartyMember(obj);
    if (!explicitRequest && isParty) {
        acclog::Write("PassiveNarrate",
            "%s: handle 0x%08x is party member, auto-focus suppressed",
            reason, handle);
        return false;
    }

    Vector pos{};
    bool havePos = acc::engine::GetObjectPosition(obj, pos);

    char name[128] = "";
    if (!acc::narration::GetSpokenName(obj, cat, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(CategoryNameId(cat)));
    }

    if (havePos && !isParty) {
        acc::audio::NavCue cue = CueForCategory(cat, obj);
        const char* resref = acc::audio::GetNavCueResref(cue);
        acc::audio::PlayCue3D(resref, pos);
        acclog::Write("PassiveNarrate",
            "fire cue resref=%s for cat=%d obj=%p pos=(%.2f,%.2f,%.2f)",
            resref, static_cast<int>(cat), obj, pos.x, pos.y, pos.z);
    }

    char enriched[320];
    if (acc::combat::query::BuildTargetCombatBrief(
            obj, name, enriched, sizeof(enriched)) &&
        enriched[0] != '\0') {
        prism::Speak(enriched, /*interrupt=*/true);
    } else {
        prism::Speak(name, /*interrupt=*/true);
    }

    uint32_t serverHandle = acc::engine::GetObjectHandle(obj);
    acc::narrated_target::Stamp(obj, serverHandle);

    // Scoped Endar Spire spectator-battle warning: first time a scripted
    // Republic soldier is narrated this area visit, follow the name with the
    // dramatic "you can't help them, press on" line. No-op everywhere else.
    acc::spectator::OnObjectNarrated(obj);

    // Organic discovery: the player just had this object narrated by focusing
    // it (passive ShowObject or an explicit Q/E). Record it so the discovery-
    // tier cycle can resurface it later. No-op for ineligible objects
    // (items, non-unique creatures). This is the primary discovery channel —
    // doors, named NPCs, containers all flow through here.
    acc::discovery::Record(obj);

    acclog::Write("PassiveNarrate",
        "%s: 0x%08x cat=%s name=[%s] pos=(%.2f,%.2f,%.2f) havePos=%d serverHandle=0x%08x",
        reason, handle, acc::filter::CategoryName(cat), name,
        pos.x, pos.y, pos.z, havePos ? 1 : 0, serverHandle);
    return true;
}

}  // namespace

void OnEngineShowObject(uint32_t handle) {
    // Unconditional cache update — Q/E re-announce reads the latest even
    // when the delta path below short-circuits.
    s_show_object_handle = handle;

    // ShowObject refreshes per frame; delta-detect to avoid spamming.
    // DEADBEEF = "no announcement yet this DLL load" — suppress the first
    // real handle so we don't speak on save resume.
    static uint32_t s_last_announced = 0xDEADBEEFu;
    if (handle == s_last_announced) return;

    uint32_t prev = s_last_announced;
    s_last_announced = handle;

    // Engine changed focus → cancel pending Q/E reannounce; this path
    // speaks + cues for the new target, a deferred reannounce would
    // stack on top. Captured pending flag drives the sentinel-branch
    // "no target" feedback so every Q/E press is audible.
    bool was_qe_request = s_qe.press_active;
    bool was_retry      = s_qe.inside_retry;
    s_qe.press_active = false;

    // Cursor coords logged for the "character spins on its own" intermittent
    // bug: Steam overlay / touchpad twitch / screen-reader cursor sync can
    // drag the cursor across NPCs and the engine snaps the player to face
    // them via DoPassiveSelection.
    POINT cursor = {0, 0};
    GetCursorPos(&cursor);
    acclog::Write("PassiveNarrate",
        "show-object delta: prev=0x%08x new=0x%08x cursor=(%d,%d)",
        prev, handle, cursor.x, cursor.y);

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        if (was_qe_request) {
            // Safety: SelectNearestObject's sentinel path means the engine
            // just told us "no valid target after LOS pruning" — but our
            // narrated_target slot still points at whatever the previous
            // ShowObject locked. Without clearing it, Enter / Shift+- /
            // Ctrl+- / Alt+- / `-` would all activate against that stale
            // focus while the user just heard "no target" (or, with retry,
            // silence). Clear unconditionally on the sentinel-via-Q/E path.
            acc::narrated_target::Clear();

            if (was_retry) {
                // Synthetic retry also landed on sentinel — the engine's
                // candidate halo really has nothing visible to the player.
                // Speak "Kein Ziel" so the user knows the press wasn't
                // eaten, then disarm so subsequent presses re-enter the
                // first-attempt branch (one fresh retry per user press).
                const char* msg = acc::strings::Get(
                    acc::strings::Id::CycleNoTarget);
                prism::Speak(msg, /*interrupt=*/true);
                acclog::Write("PassiveNarrate",
                    "Q/E retry -> sentinel (0x%08x -> 0x%08x), spoke [%s]",
                    prev, handle, msg);
                s_qe.retry_armed = false;
                s_qe.retry_wait  = 0;
            } else {
                // First sentinel after a user-driven Q/E. The candidate
                // halo was emptied by CanSee LOS pruning on a stale frame;
                // DoPassiveSelection will rebuild it next frame. Defer
                // speech and arm a retry — most of the time the rebuild
                // gives us a real target on the second pass.
                s_qe.retry_armed = true;
                s_qe.retry_wait  = 1;  // skip one Tick to let the halo refresh
                acclog::Write("PassiveNarrate",
                    "Q/E -> sentinel (0x%08x -> 0x%08x), arming retry dir=%d",
                    prev, handle, s_qe.direction_code);
            }
        } else if (prev != 0xDEADBEEFu) {
            acclog::Write("PassiveNarrate",
                "focus lost (0x%08x -> sentinel 0x%08x), silent",
                prev, handle);
        }
        return;
    }

    // Real target — engine found something on Q/E (user-driven or our
    // synthetic retry). Disarm any pending retry so we don't double-issue.
    s_qe.retry_armed = false;
    s_qe.retry_wait  = 0;

    // First non-sentinel after DLL load — suppress (don't speak on save
    // resume). Skipping also skips the narrated_target stamp so activation
    // keys can't act on something we never said.
    if (prev == 0xDEADBEEFu) {
        acclog::Write("PassiveNarrate",
            "first-tick handle=0x%08x, suppressed", handle);
        return;
    }

    // was_qe_request marks a user-driven Q/E cycle that resolved through the
    // ShowObject delta. Pass it as explicitRequest so companions are still
    // narrated when the user cycles to them; the automatic focus-follow path
    // (was_qe_request == false) suppresses them.
    NarrateHandle(handle, "passive", was_qe_request);
}

void RequestQEReannounce(int directionCode) {
    // Skip when we're mid-flight on a synthetic Q/E — the retry path
    // already set press_active itself, and we don't want our own
    // synthesized event to re-enter the first-attempt branch.
    if (s_qe.inside_retry) return;

    s_qe.press_active   = true;
    s_qe.direction_code = directionCode;
    // Fresh user press resets any leftover retry state from a previous
    // press the user may have abandoned.
    s_qe.retry_armed = false;
    s_qe.retry_wait  = 0;
}

bool IsInSynthesizedQE() {
    return s_qe.inside_retry;
}

void Tick() {
    // ---- Path 1: drain the sentinel-skip retry. ----
    //
    // Sentinel handling armed a retry one or more frames ago; once the
    // wait counter hits zero, re-issue the same Q/E direction through
    // CClientExoAppInternal::HandleInputEvent. The engine's switch-case
    // dispatch will rerun SelectNearestObject against a halo that
    // DoPassiveSelection has rebuilt in the intervening frame, so most
    // sentinels resolve to a real target on this second pass.
    if (s_qe.retry_armed) {
        if (s_qe.retry_wait > 0) {
            --s_qe.retry_wait;
            return;
        }
        s_qe.retry_armed = false;

        // Walk *kAddrAppManagerPtr → CClientExoApp → CClientExoAppInternal.
        // SEH around every deref — this runs on every frame the user
        // taps Q/E and we don't want a stray null to fault the engine.
        void* internal = nullptr;
        __try {
            void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
            if (appMgr) {
                void* exoApp = *reinterpret_cast<void**>(
                    reinterpret_cast<unsigned char*>(appMgr) +
                    kAppManagerClientAppOffset);
                if (exoApp) {
                    internal = *reinterpret_cast<void**>(
                        reinterpret_cast<unsigned char*>(exoApp) +
                        kClientExoAppInternalOffset);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            internal = nullptr;
        }
        if (!internal) {
            acclog::Write("PassiveNarrate",
                "Q/E retry: no client-internal, aborted");
            return;
        }

        // CClientExoAppInternal::HandleInputEvent @ 0x00621210.
        // __thiscall(int param_1, int param_2) — same signature the
        // input-pipeline detour observes. dir 204=E / 205=Q.
        using PFN_HIE = void(__thiscall*)(void*, int, int);
        constexpr uintptr_t kAddrCClientExoAppInternalHandleInputEvent =
            0x00621210;
        auto fn = reinterpret_cast<PFN_HIE>(
            kAddrCClientExoAppInternalHandleInputEvent);

        s_qe.inside_retry  = true;
        s_qe.press_active  = true;  // so OnEngineShowObject treats result as Q/E
        int dir = s_qe.direction_code;

        acclog::Write("PassiveNarrate",
            "Q/E retry: synthesizing dir=%d on internal=%p", dir, internal);

        __try {
            fn(internal, dir, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            acclog::Write("PassiveNarrate",
                "Q/E retry: HandleInputEvent faulted");
        }

        s_qe.inside_retry = false;
        // If the synthetic call didn't reach ShowObject (engine no-op),
        // clear press_active so the reannounce-same path below doesn't
        // pick up a stale handle on the following Tick.
        s_qe.press_active = false;
        return;
    }

    // ---- Path 2: single-hostile-combat reannounce. ----
    //
    // User pressed Q/E with only one valid target in the halo — engine
    // selects the same target, no ShowObject transition, and our delta
    // detector eats the silence. press_active is still set from the
    // input-pipeline edge; re-announce the current focus so the user
    // hears confirmation.
    if (!s_qe.press_active) return;
    s_qe.press_active = false;

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    uint32_t handle = s_show_object_handle;
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) return;

    NarrateHandle(handle, "reannounce", /*explicitRequest=*/true);
}

}  // namespace acc::passive_narrate

// Thin trampoline for the ShowObject detour (hooks.toml @ 0x005f9c8e).
extern "C" __declspec(dllexport)
void __cdecl OnShowObject(void* /*clientObject*/, int handle) {
    acc::passive_narrate::OnEngineShowObject(static_cast<uint32_t>(handle));
}
