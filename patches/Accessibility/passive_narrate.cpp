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

// Engine's user-facing target cache. Exposed at namespace scope so the
// extern-"C" OnShowObject handler can update it. Single-threaded, no atomic.
volatile uint32_t s_show_object_handle = 0x7F000000u;

namespace {

bool s_qe_reannounce_pending = false;

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

// Resolve → classify → speak → stamp. Used by both the focus-change path
// (OnEngineShowObject delta) and the deferred Q/E re-announce path. The
// two are mutually exclusive by construction — Tick cancels pending if
// OnEngineShowObject sees a real focus change first.
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
    bool was_qe_request = s_qe_reannounce_pending;
    s_qe_reannounce_pending = false;

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
            // Q/E exhausted its candidate list (every nearby target failed
            // GetGameObject / AsSWCObject / CanSee LOS). Promote silence
            // to spoken feedback so the press isn't perceived as eaten.
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

    // First non-sentinel after DLL load — suppress (don't speak on save
    // resume). Skipping also skips the narrated_target stamp so activation
    // keys can't act on something we never said.
    if (prev == 0xDEADBEEFu) {
        acclog::Write("PassiveNarrate",
            "first-tick handle=0x%08x, suppressed", handle);
        return;
    }

    NarrateHandle(handle, "passive");
}

void RequestQEReannounce() {
    s_qe_reannounce_pending = true;
}

void Tick() {
    if (!s_qe_reannounce_pending) return;
    s_qe_reannounce_pending = false;

    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    uint32_t handle = s_show_object_handle;
    if (handle == 0u || handle == 0xFFFFFFFFu || handle == 0x7F000000u) return;

    NarrateHandle(handle, "reannounce");
}

}  // namespace acc::passive_narrate

// Thin trampoline for the ShowObject detour (hooks.toml @ 0x005f9c8e).
extern "C" __declspec(dllexport)
void __cdecl OnShowObject(void* /*clientObject*/, int handle) {
    acc::passive_narrate::OnEngineShowObject(static_cast<uint32_t>(handle));
}
