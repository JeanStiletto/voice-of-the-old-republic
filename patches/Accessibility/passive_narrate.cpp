#include "passive_narrate.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "audio_bus.h"
#include "audio_cues.h"
#include "combat_query.h"   // BuildTargetCombatBrief enrichment
#include "engine_area.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "log.h"
#include "narrated_target.h"
#include "same_name_suffix.h"
#include "strings.h"
#include "prism.h"

namespace acc::passive_narrate {

// Cache of the engine's user-facing target, populated by OnEngineShowObject
// (driven by the ShowObject detour declared in hooks.toml at 0x005f9c8e).
// ShowObject is called from DoPassiveSelection (mouse-hover auto-target,
// every frame) and SelectNearestObject (Q/E hostile cycle); both
// represent the actual on-screen focus the sighted player sees in the
// red-hilite ring. AI churn (CreateNewAttackActions writing last_target
// every combat round) does NOT touch this — so during combat the cache
// stays stable on the user-selected target while last_target jitters.
//
// Game is single-threaded so volatile is enough; no atomic needed.
// Exposed at namespace scope (not anonymous) so the extern-"C"
// OnShowObject handler at file end can update it.
volatile uint32_t s_show_object_handle = 0x7F000000u;

namespace {

// Pending Q/E reannounce — set by RequestQEReannounce on each Q/E press;
// cleared by OnEngineShowObject if the engine processes the keystroke
// into a focus change (the focus-change path already speaks + plays the
// cue, so reannounce would double-fire). Drained by Tick if still set
// (engine didn't change focus → single-hostile combat case → reannounce).
bool s_qe_reannounce_pending = false;

// Map a Pillar 4 cycle category to its 3D audio cue. Mirrors the mapping
// in cycle_input.cpp's BindingsFor — duplicated locally to keep this
// lay-off scoped (the cycle table is a `static` inside cycle_input.cpp's
// anonymous namespace; promoting it requires a refactor that's out of
// scope). If the two get out of sync in the future, factor into a
// shared filter_objects helper.
// Closed-door material-specific cue. Mirrors RefineDoorCue in
// cycle_input.cpp.
acc::audio::NavCue ClosedDoorCueForMaterial(void* obj) {
    using N = acc::audio::NavCue;
    switch (acc::engine::GetDoorMaterial(obj)) {
        case acc::engine::DoorMaterial::Wood:  return N::DoorClosedWood;
        case acc::engine::DoorMaterial::Stone: return N::DoorClosedStone;
        case acc::engine::DoorMaterial::Metal: break;
    }
    return N::DoorClosedMetal;
}

// Door cue depends on open_state + material — pass obj so we can read
// both. Other categories ignore obj.
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

// Classify an object against the six locked Pillar 4 categories; returns
// Count_ if no category claims it (combat target / dialog target / other
// engine-internal ShowObject consumer that the user shouldn't hear about
// as ambient nav narration).
acc::filter::CycleCategory ClassifyForNarration(void* obj) {
    using C = acc::filter::CycleCategory;
    for (int i = 0; i < static_cast<int>(C::Count_); ++i) {
        auto c = static_cast<C>(i);
        if (acc::filter::ObjectMatches(obj, c)) return c;
    }
    return C::Count_;
}

// Shared resolve → classify → speak → stamp pipeline. Used by both the
// focus-change path (delta inside OnEngineShowObject) and the deferred
// Q/E re-announce path (Tick after engine processed the keystroke
// without changing focus). `reason` tags the log channel so post-mortem
// grep can tell which path spoke.
//
// Both paths fire the 3D cue: focus-change for primary signalling, Q/E
// reannounce as audible "yes, still the same target" confirmation in
// single-hostile combat. The two paths are mutually exclusive by
// construction — the deferred-Tick mechanism cancels a pending Q/E
// reannounce as soon as the engine fires ShowObject for a real focus
// change, so the user can't hear both for the same Q/E press.
//
// Returns true when speech was emitted, false on any short-circuit
// (sentinel handle, resolve fault, kind not in nav categories).
bool NarrateHandle(uint32_t handle, const char* reason) {
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

    Vector pos{};
    bool havePos = acc::engine::GetObjectPosition(obj, pos);

    char name[128] = "";
    if (!acc::narration::GetSpokenName(obj, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(CategoryNameId(cat)));
    }

    if (havePos) {
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

    acclog::Write("PassiveNarrate",
        "%s: 0x%08x cat=%s name=[%s] pos=(%.2f,%.2f,%.2f) havePos=%d serverHandle=0x%08x",
        reason, handle, acc::filter::CategoryName(cat), name,
        pos.x, pos.y, pos.z, havePos ? 1 : 0, serverHandle);
    return true;
}

}  // namespace

void OnEngineShowObject(uint32_t handle) {
    // Cache update is unconditional — Q/E re-announce reads this and
    // must reflect the very latest engine focus, even if delta detection
    // below short-circuits (no transition, sentinel, etc.).
    s_show_object_handle = handle;

    // Delta detection. ShowObject fires per frame as DoPassiveSelection's
    // tail refresh; we'd spam if we announced unconditionally. Sentinel
    // DEADBEEF marks "no announcement yet this DLL load" — first real
    // non-sentinel handle is suppressed so we don't speak on save resume.
    static uint32_t s_last_announced = 0xDEADBEEFu;
    if (handle == s_last_announced) return;

    uint32_t prev = s_last_announced;
    s_last_announced = handle;

    // Engine changed focus this tick → cancel any pending Q/E reannounce.
    // The focus-change path below will speak + play the new target's cue;
    // a deferred reannounce on top would double-fire (same metallic
    // sample stacking on the new cue, which the user perceived as
    // "wrong sound for the object I cycled to").
    //
    // Capture pending state BEFORE clearing — used by the sentinel branch
    // to give Q/E-initiated "no target" feedback. If the engine's
    // SelectNearestObject returned no next target (it has an angular
    // end-of-cycle quirk where the wrap requires a second press), the
    // user pressed Q/E and would otherwise hear silence; we promote it
    // to a short spoken phrase so every press has audible feedback.
    bool was_qe_request = s_qe_reannounce_pending;
    s_qe_reannounce_pending = false;

    // Cursor-position diagnostic for the "character spins on its own"
    // intermittent bug. ShowObject is driven by DoPassiveSelection
    // (mouse-hover auto-target, every frame) — if Windows-side jitter
    // (Steam overlay, notifications, touchpad twitch, screen-reader
    // cursor sync) drags the screen cursor across NPC silhouettes, the
    // engine snaps the player to face whichever target the cursor
    // last grazed. Cursor coords are GetCursorPos screen-space; pair
    // with the immediately-following PassiveNarrate `passive:` /
    // `focus lost` line to see which target the cursor picked.
    POINT cursor = {0, 0};
    GetCursorPos(&cursor);
    acclog::Write("PassiveNarrate",
        "show-object delta: prev=0x%08x new=0x%08x cursor=(%d,%d)",
        prev, handle, cursor.x, cursor.y);

    // Self-gate on player-loaded — silent in menus / chargen / area-load.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        if (was_qe_request) {
            // Q/E press → engine returned sentinel (no-target-found). Speak
            // feedback so the user hears their press registered. They can
            // press Q/E again to wrap the cycle.
            const char* msg = acc::strings::Get(
                acc::strings::Id::CycleNoTarget);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("PassiveNarrate",
                "Q/E -> sentinel (0x%08x -> 0x%08x), spoke [%s]",
                prev, handle, msg);
        } else if (prev != 0xDEADBEEFu) {
            acclog::Write("PassiveNarrate",
                "focus lost (0x%08x -> sentinel 0x%08x), silent",
                prev, handle);
        }
        return;
    }

    // First-tick suppression — first non-sentinel handle after DLL load.
    // The user already knows what they were pointed at on save; speaking
    // on resume is noise. Skipped narrations don't stamp narrated_target
    // either — activation keys shouldn't act on something we never said.
    if (prev == 0xDEADBEEFu) {
        acclog::Write("PassiveNarrate",
            "first-tick handle=0x%08x, suppressed", handle);
        return;
    }

    NarrateHandle(handle, "passive");
}

void RequestQEReannounce() {
    // Set the pending flag. The Tick handler drains it next frame IF the
    // engine didn't fire ShowObject in between (which would clear the
    // flag via OnEngineShowObject's delta path). No state to capture
    // here — Tick reads s_show_object_handle at drain time, which by
    // then reflects whatever the engine settled on for this Q/E press.
    s_qe_reannounce_pending = true;
}

void Tick() {
    if (!s_qe_reannounce_pending) return;
    s_qe_reannounce_pending = false;

    // Self-gate on player-loaded — match OnEngineShowObject so the
    // re-announce doesn't fire in menus / chargen / area transitions.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    uint32_t handle = s_show_object_handle;
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        // No engine-facing target — nothing to re-announce. Silent.
        return;
    }

    NarrateHandle(handle, "reannounce");
}

}  // namespace acc::passive_narrate

// CClientExoAppInternal::ShowObject hook (hooks.toml entry @ 0x005f9c8e).
// Thin extern-"C" trampoline into the namespace-scoped handler — keeps
// the framework's exported-symbol contract while letting the real
// logic live alongside its state.
extern "C" __declspec(dllexport)
void __cdecl OnShowObject(void* /*clientObject*/, int handle) {
    acc::passive_narrate::OnEngineShowObject(static_cast<uint32_t>(handle));
}
