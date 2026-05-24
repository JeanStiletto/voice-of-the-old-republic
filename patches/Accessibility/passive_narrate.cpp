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

// Map a Pillar 4 cycle category to its 3D audio cue. Mirrors the mapping
// in cycle_input.cpp's BindingsFor — duplicated locally to keep this
// lay-off scoped (the cycle table is a `static` inside cycle_input.cpp's
// anonymous namespace; promoting it requires a refactor that's out of
// scope). If the two get out of sync in the future, factor into a
// shared filter_objects helper.
acc::audio::NavCue CueForCategory(acc::filter::CycleCategory c) {
    using C = acc::filter::CycleCategory;
    using N = acc::audio::NavCue;
    switch (c) {
        case C::Door:       return N::Door;
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
// transition path (delta inside OnEngineShowObject) and the Q/E
// re-announce path. `reason` tags the log channel so post-mortem grep
// can tell which path spoke.
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
    if (!acc::engine::GetObjectName(obj, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(CategoryNameId(cat)));
    }

    if (havePos) {
        acc::audio::PlayCue3D(
            acc::audio::GetNavCueResref(CueForCategory(cat)), pos);
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

    // Self-gate on player-loaded — silent in menus / chargen / area-load.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        if (prev != 0xDEADBEEFu) {
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

void ReannounceCurrentShowObjectTarget() {
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
