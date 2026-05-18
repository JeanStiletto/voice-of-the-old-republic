#include "passive_narrate.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "audio_bus.h"
#include "audio_cues.h"
#include "combat_query.h"   // Phase 2B — BuildTargetCombatBrief enrichment
#include "engine_area.h"
#include "engine_offsets.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "log.h"
#include "narrated_target.h"
#include "strings.h"
#include "tolk.h"

// Address shared with probe_world_hover.h (probe verified the read works).
// Not pulling that header here — the probe is a sibling diagnostic, not a
// dependency. One constexpr re-declaration is cheaper than an include
// graph cycle if the probe is ever deleted.
constexpr uintptr_t kAddrCClientExoAppGetLastTarget = 0x005EDD80;

namespace acc::passive_narrate {

namespace {

typedef uint32_t (__thiscall* PFN_GetLastTarget)(void* this_);

// AppManager → +0x4 → CClientExoApp* — same chain as engine_player's
// internal helper, repeated here because we want the *client* app to call
// client-side methods on, and engine_player's exposed helper returns the
// player's server creature (different downcast path).
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

// Map a Pillar 4 cycle category to its 3D audio cue. Mirrors the mapping
// in cycle_input.cpp's BindingsFor — duplicated locally to keep this
// lay-off scoped (the cycle table is a `static` inside cycle_input.cpp's
// anonymous namespace; promoting it requires a refactor that's out of
// scope for 9a). If the two get out of sync in the future, factor into a
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
// engine-internal LastTarget consumer that the user shouldn't hear about
// as ambient nav narration).
acc::filter::CycleCategory ClassifyForNarration(void* obj) {
    using C = acc::filter::CycleCategory;
    for (int i = 0; i < static_cast<int>(C::Count_); ++i) {
        auto c = static_cast<C>(i);
        if (acc::filter::ObjectMatches(obj, c)) return c;
    }
    return C::Count_;
}

}  // namespace

void Tick() {
    // Self-gate on player-loaded — silent in menus / chargen / area-load.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    // Last-handle cache. DEADBEEF = "first call ever, treat next read as a
    // delta but don't speak — we don't know what the user has been
    // looking at across DLL reload / save load." First real change after
    // reload will speak normally.
    static uint32_t s_lastHandle = 0xDEADBEEFu;

    uint32_t handle = ReadLastTargetHandle();
    if (handle == s_lastHandle) return;

    uint32_t prev = s_lastHandle;
    s_lastHandle = handle;

    // Skip the engine's "no target" sentinel — silence on focus loss.
    // Don't announce "lost focus" / "nothing focused" because the user
    // hears that channel constantly as they walk away from things.
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) {
        // Log only — silent for the user. Useful for post-mortem.
        if (prev != 0xDEADBEEFu) {
            acclog::Write("PassiveNarrate", "focus lost (0x%08x -> sentinel 0x%08x), silent",
                prev, handle);
        }
        return;
    }

    // First-tick suppression — don't speak the very first non-sentinel
    // target after DLL load. The user already knows what they were
    // pointed at when they last saved; speaking on resume is noise.
    // Skipped narrations don't stamp narrated_target either — the user
    // hasn't been told about this object, so it shouldn't claim the
    // activation slot.
    if (prev == 0xDEADBEEFu) {
        acclog::Write("PassiveNarrate", "first-tick handle=0x%08x, suppressed", handle);
        return;
    }

    // LastTarget hands us a *client*-side handle (high-bit-set form like
    // 0x800000XX, observed live 2026-05-04). The client resolver chains
    // through CSWCObject.server_object +0xf8 so we land on a CSWSObject*
    // that the rest of engine_area can read against.
    void* obj = acc::engine::ResolveClientObjectHandle(handle);
    if (!obj) {
        acclog::Write("PassiveNarrate", "handle 0x%08x failed to resolve, silent",
            handle);
        return;
    }

    acc::filter::CycleCategory cat = ClassifyForNarration(obj);
    if (cat == acc::filter::CycleCategory::Count_) {
        // Not a nav-relevant kind — combat target / etc. Silent.
        int kind = acc::engine::GetObjectKind(obj);
        acclog::Write("PassiveNarrate", "handle 0x%08x kind=%d not a nav category, silent",
            handle, kind);
        return;
    }

    Vector pos{};
    bool havePos = acc::engine::GetObjectPosition(obj, pos);

    char name[128] = "";
    if (!acc::engine::GetObjectName(obj, name, sizeof(name)) ||
        name[0] == '\0') {
        // Empty-name fallback: speak the localised category label.
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(CategoryNameId(cat)));
    }

    // 3D cue at the object's position (when we have one). The engine's
    // listener is camera-anchored (Phase 1 lay-off 4 finding) — pan +
    // attenuation track the player view naturally.
    if (havePos) {
        acc::audio::PlayCue3D(
            acc::audio::GetNavCueResref(CueForCategory(cat)), pos);
    }

    // Phase 2B — for Creature kind, enrich the announcement with the
    // combat brief (HP / AC / faction / dead). For other kinds, speech
    // is bare-name only; combat data isn't relevant.
    char enriched[320];
    if (acc::combat::query::BuildTargetCombatBrief(
            obj, name, enriched, sizeof(enriched)) &&
        enriched[0] != '\0') {
        tolk::Speak(enriched, /*interrupt=*/true);
    } else {
        // Speak just the name. Distance / clock are intentionally omitted
        // here — passive narration is ambient, not directional. The cue
        // carries spatial direction via 3D pan; speech carries identity.
        // If the user wants distance/clock, they cycle to the object via
        // `,`/`.` (Pillar 4 active-scan path).
        tolk::Speak(name, /*interrupt=*/true);
    }

    // Stamp the unified activation slot. We speak the bare object name,
    // which means the user "knows" this is what they're hearing about —
    // Enter / Shift+- / Ctrl+- / `-` should now target it. The stamp
    // takes the SERVER-side handle (re-derived via GetObjectHandle),
    // matching the namespace the AI-action primitives use.
    uint32_t serverHandle = acc::engine::GetObjectHandle(obj);
    acc::narrated_target::Stamp(obj, serverHandle);

    acclog::Write("PassiveNarrate", "0x%08x -> 0x%08x cat=%s name=[%s] "
        "pos=(%.2f,%.2f,%.2f) havePos=%d serverHandle=0x%08x",
        prev, handle, acc::filter::CategoryName(cat), name,
        pos.x, pos.y, pos.z, havePos ? 1 : 0, serverHandle);
}

}  // namespace acc::passive_narrate
