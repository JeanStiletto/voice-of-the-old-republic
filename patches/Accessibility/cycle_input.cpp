#include "cycle_input.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// user32.lib for GetAsyncKeyState / GetForegroundWindow /
// GetWindowThreadProcessId. The patch build script (create-patch.bat) links
// only sqlite3 by default; pragma keeps the link config local to the TU
// that needs it.
#pragma comment(lib, "user32.lib")

#include "audio_bus.h"
#include "audio_cues.h"
#include "cycle_state.h"
#include "engine_area.h"
#include "engine_compass.h"      // ClockPosition — shared clock-direction math
#include "engine_input.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "guidance_approach.h"  // ArmApproach / IsApproachInFlight — unified
                                // walk-to-act tracker (way-blocked + cancel)
#include "guidance_autowalk.h"
#include "guidance_beacon.h"
#include "guidance_description.h"
#include "guidance_pathfind.h"
#include "hotkeys.h"
#include "log.h"
#include "map_ui_cursor.h"
#include "narrated_target.h"
#include "peek_description.h"
#include "same_name_suffix.h"
#include "state_overrides.h"    // AppendStateLabel — placeable state suffix
#include "strings.h"
#include "prism.h"

namespace acc::cycle_input {

namespace {

// Engine-side shift flag, mutated by manager-level shift up/down events.
// The manager doesn't bake shift into a modifier flag — DirectInput delivers
// each key as its own event — so we track it ourselves. The Win32 polling
// path queries GetAsyncKeyState directly (no need to share this flag).
bool g_engineShiftHeld = false;

// Clock-position relative to player facing, returning 1..12 with 12 =
// directly ahead, 3 = to the right, 6 = directly behind, 9 = to the left.
//
// Player yaw frame: degrees CCW from +X (engine convention via
// GetPlayerYawDegrees). World-frame angle of the (object - player) vector
// is atan2(dy, dx). Their difference is the relative bearing CCW from
// "ahead"; clocks tick CW, so we negate before bucketing into 30° slots.
//
// Returns 12 for the directly-ahead bucket (which would otherwise be 0).
// Shared definition lives in engine_compass (also used by trap_watch).
using acc::engine::ClockPosition;

// Map a CycleCategory to its strings::Id pair (singular name + empty
// message) and its 3D audio cue. Centralises the table so OnCycleItem /
// OnCycleCategory don't each carry duplicate switches.
struct CategoryBindings {
    acc::strings::Id  name;    // singular for prefix / fallback
    acc::strings::Id  empty;   // full pre-formatted "no X in range"
    acc::audio::NavCue cue;    // 3D positional cue played at the focused
                               // object's world position before speech
};

// Refine a placeholder door cue against the actual server-door open_state
// and material. BindingsFor returns DoorOpen as a category-level
// placeholder; the real fire site needs to check open_state (open vs
// closed) and, when closed, the door's material (metal / wood / stone)
// to pick between gui_close and the three dr_{metal,wood,stone}_lock
// samples. Pass-through for any other NavCue so call sites can wrap
// PlayCue3D unconditionally.
acc::audio::NavCue RefineDoorCue(acc::audio::NavCue cue, void* obj) {
    if (cue != acc::audio::NavCue::DoorOpen) return cue;
    if (acc::engine::IsDoorOpen(obj)) return acc::audio::NavCue::DoorOpen;
    switch (acc::engine::GetDoorMaterial(obj)) {
        case acc::engine::DoorMaterial::Wood:  return acc::audio::NavCue::DoorClosedWood;
        case acc::engine::DoorMaterial::Stone: return acc::audio::NavCue::DoorClosedStone;
        case acc::engine::DoorMaterial::Metal: break;
    }
    return acc::audio::NavCue::DoorClosedMetal;
}

CategoryBindings BindingsFor(
        acc::filter::CycleCategory c,
        acc::filter::CycleContext ctx = acc::filter::CycleContext::World) {
    using S = acc::strings::Id;
    using N = acc::audio::NavCue;
    using C = acc::filter::CycleCategory;
    switch (c) {
        case C::Door:
            // DoorOpen is the placeholder; callers must refine via
            // RefineDoorCue() against the focused object's open_state
            // before fire. Empty-state path doesn't use .cue, so the
            // placeholder is harmless there.
            return {S::CategoryDoor,       S::EmptyDoors,       N::DoorOpen};
        case C::Npc:
            return {S::CategoryNpc,        S::EmptyNpcs,        N::NpcCreature};
        case C::Container:
            return {S::CategoryContainer,  S::EmptyContainers,  N::ContainerPlaceable};
        case C::Item:
            return {S::CategoryItem,       S::EmptyItems,       N::Item};
        case C::Landmark:
            // Map context renames to "Map hint" / "Hinweis" — matches the
            // engine's own GetNext/PrevMapNote terminology so the spoken
            // category word lines up with what sighted players see in the
            // InGameMap up/down buttons. World context keeps the broader
            // "Landmark" wording for the unrestricted has_map_note set.
            if (ctx == acc::filter::CycleContext::Map) {
                return {S::CategoryMapHint, S::EmptyMapHints, N::Landmark};
            }
            return {S::CategoryLandmark,   S::EmptyLandmarks,   N::Landmark};
        case C::Transition:
            return {S::CategoryTransition, S::EmptyTransitions, N::TransitionExit};
        case C::Count_:
            break;
    }
    return {S::CategoryItem, S::EmptyAll, N::Item};  // unreachable safety net
}

// Format the "{name}, {clock} o'clock, {metres} metres" payload (or the
// no-clock variant) into outBuf. Returns the bytes written (excluding
// terminator). Bound to acc::strings, so wording / locale lives there.
int FormatItemPayload(const char* name, bool haveYaw, int clock,
                      int metres, char* outBuf, size_t outBufSize) {
    if (metres < 1) metres = 1;
    const char* fmt = acc::strings::Get(haveYaw
        ? acc::strings::Id::FmtAnnounceWithClock
        : acc::strings::Id::FmtAnnounceNoClock);
    int n = haveYaw
        ? std::snprintf(outBuf, outBufSize, fmt, name, clock, metres)
        : std::snprintf(outBuf, outBufSize, fmt, name, metres);
    return n < 0 ? 0 : n;
}

// Resolve a map pin's spoken note text exactly as AnnounceCurrent does,
// so the same-name comparison used for numbering keys off the string the
// user actually hears (note_text, or the "no text" fallback).
void ResolvePinNoteText(void* pin, char* outBuf, size_t bufSize) {
    if (!acc::engine::GetMapPinNoteText(pin, outBuf, bufSize) ||
        outBuf[0] == '\0') {
        std::snprintf(outBuf, bufSize, "%s",
                      acc::strings::Get(acc::strings::Id::MapPinNoText));
    }
}

// Resolve listing entry `idx`'s spoken label, mirroring AnnounceCurrent's
// focused-entry cascade so the same-name comparison used for numbering
// keys off exactly the string the user hears. Returns an empty key when no
// localized name resolves (the spoken text falls back to the category name,
// which carries no per-entry identity to number by).
void ResolveEntryName(const acc::cycle::CategoryListing& listing, int idx,
                      bool mapHint, char* out, size_t size) {
    out[0] = '\0';
    if (listing.isPin[idx]) {
        ResolvePinNoteText(listing.objs[idx], out, size);
        return;
    }
    if (mapHint &&
        acc::engine::GetWaypointMapNote(listing.objs[idx], out, size) &&
        out[0] != '\0') {
        return;
    }
    if (!acc::engine::GetObjectName(listing.objs[idx], out, size) ||
        out[0] == '\0') {
        out[0] = '\0';
    }
}

// Append a north-to-south positional ordinal to a LISTING entry's name when
// two or more entries in the listing share the same spoken label. Designer
// map "hints" (Nordpfad ×4) and duplicate placeables (Fußlocker ×3) repeat
// the same label; the appended number lets the user refer back to a specific
// one.
//
// Listing-relative (ranks within `listing`, not the whole area) — used only
// for the MAP context, where the listing is the full eligible pin/landmark
// set (never discovery-filtered), so listing-relative already equals global.
// It also handles map pins + map-note waypoints, whose spoken label comes
// from pin note_text / GetWaypointMapNote rather than GetObjectName, so the
// area-scanning AppendAreaPositionOrdinal can't substitute here.
//
// World-context statics instead go through acc::narration::AppendDisambiguator
// → AppendAreaPositionOrdinal, which ranks against the WHOLE area (not the
// discovery-filtered listing) so a number can't shift as more peers are
// discovered, and matches the number the Q/E focus path speaks for the same
// object.
void AppendPositionOrdinal(const acc::cycle::CategoryListing& listing,
                           int focusedIndex, bool mapHint, char* name,
                           size_t nameSize) {
    char focusKey[128];
    ResolveEntryName(listing, focusedIndex, mapHint, focusKey,
                     sizeof(focusKey));
    if (focusKey[0] == '\0') return;  // no per-entry label to key on

    // Count how many like-shaped, same-named peers sort north of the
    // focused entry; that rank (1-based, northmost = 1) is its ordinal.
    const bool    focusIsPin = listing.isPin[focusedIndex];
    const Vector& fp         = listing.positions[focusedIndex];
    int rank      = 1;
    int peerCount = 0;
    for (int j = 0; j < listing.count; ++j) {
        if (listing.isPin[j] != focusIsPin) continue;  // compare like with like
        char other[128];
        ResolveEntryName(listing, j, mapHint, other, sizeof(other));
        if (std::strcmp(other, focusKey) != 0) continue;
        ++peerCount;
        if (j != focusedIndex &&
            acc::narration::PositionLess(listing.positions[j], fp)) {
            ++rank;
        }
    }
    if (peerCount < 2) return;  // unique label — no number needed

    size_t curLen = std::strlen(name);
    if (curLen + 5 < nameSize) {
        std::snprintf(name + curLen, nameSize - curLen, " %d", rank);
    }
}

// Speak the locked Pillar 4 payload for whatever the cycle state
// currently focuses. `categoryPrefix` non-null wraps the item with
// "{category}. {item}" via the FmtCategoryItem template; null speaks
// just the item. Distance rounds to whole metres; sub-1m floors to 1
// (avoids "0 metres"). When player yaw is unavailable (degenerate
// facing during spawn / mid-load), the clock segment is dropped.
void AnnounceCurrent(const acc::cycle::CategoryListing& listing,
                     const char* categoryPrefix,
                     acc::filter::CycleContext ctx) {
    auto& s = acc::cycle::GetState(ctx);
    auto bindings = BindingsFor(s.category, ctx);
    const bool mapCtx = (ctx == acc::filter::CycleContext::Map);

    if (!s.focusedObj || s.focusedIndex < 0 ||
        s.focusedIndex >= listing.count) {
        // Empty state — speak the localized "no X in range" phrase. The
        // category-prefix wrapper is intentionally NOT applied here:
        // EmptyDoors etc. are full sentences, not item names. No 3D cue
        // either, since there's no object to localise spatially.
        //
        // Speech path: normal Prism (NVDA primary) in both contexts.
        // SpeakUrgent stays reserved for sustained-key-hold scenarios
        // (map cursor WASD pan); single-press cycle keys don't suffer
        // the typed-character cancel feedback loop in practice.
        const char* msg = acc::strings::Get(bindings.empty);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "-> [%s] (ctx=%s)", msg,
                      mapCtx ? "Map" : "World");
        return;
    }

    // 3D positional cue at the focused object's world position. Per the
    // cross-pillar audio vocabulary in audio_cues.h, each Pillar 4
    // category has a designated short cue; the engine's Miles 3D pipeline
    // pans + attenuates it relative to the camera-anchored listener (Q8 +
    // Phase 1 lay-off 4 gate verification). Fired before TTS so the user
    // hears spatial direction first, then the localized name + clock +
    // distance reinforcement.
    const Vector& objPos = listing.positions[s.focusedIndex];
    acc::audio::NavCue cue = RefineDoorCue(bindings.cue, s.focusedObj);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(cue), objPos);

    char name[128] = "";
    // Three name-resolution paths share the "Map hint" announce:
    //  1) User-placed map pin (listing.isPin=true): CSWCMapPin.note_text
    //     (+0x100), populated by map_user_markers::BuildAutoName.
    //  2) Map-context waypoint: GetWaypointMapNote (+0x230) — the engine's
    //     localised map-note text, same string sighted players see.
    //  3) Anything else: GetObjectName (door / NPC / ...). World-context
    //     Landmark + Transition go here too — GetObjectName now folds the
    //     waypoint map-note (+0x230) and the trigger transition_destination
    //     (+0x30c) into its own fallback chain, so those resolve to the
    //     human-readable label ("Zur Oberstadt") instead of the raw tag.
    // The map-context branch below still reads GetWaypointMapNote directly
    // so its same-name numbering keys off the exact map string.
    bool isPin = listing.isPin[s.focusedIndex];
    bool mapHint = mapCtx &&
                   s.category == acc::filter::CycleCategory::Landmark;
    bool nameOk = false;
    if (isPin) {
        nameOk = acc::engine::GetMapPinNoteText(s.focusedObj,
                                                name, sizeof(name)) &&
                 name[0] != '\0';
        if (!nameOk) {
            std::snprintf(name, sizeof(name), "%s",
                          acc::strings::Get(acc::strings::Id::MapPinNoText));
            nameOk = true;
        }
    } else if (mapHint) {
        nameOk = acc::engine::GetWaypointMapNote(s.focusedObj,
                                                 name, sizeof(name)) &&
                 name[0] != '\0';
    }
    if (!nameOk) {
        nameOk = acc::engine::GetObjectName(s.focusedObj,
                                            name, sizeof(name)) &&
                 name[0] != '\0';
    }
    if (!nameOk) {
        // Fall back to the localized category name — at least the user
        // knows the kind even if no localized name resolves.
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(bindings.name));
    }

    // Disambiguate same-name entries with a stable ordinal, so repeated
    // labels ("Nordpfad" ×4, "Kath-Hund" ×3) are individually referable.
    // Two keying strategies by mobility, and they must match what the Q/E
    // focus path speaks for the same object:
    //  - Static objects number by north-to-south world position, so
    //    "Nordpfad 3" is the same marker every visit, save, and player — a
    //    global reference frame independent of entry direction or distance.
    //  - Creatures move; position-ranking would swap their numbers as they
    //    walk. They keep the handle-keyed same-name suffix instead, shared
    //    with combat/passive narration so a creature keeps one number
    //    everywhere.
    // Map context uses the listing-relative AppendPositionOrdinal: its
    // listing is the full eligible set (never discovery-filtered), and it
    // alone resolves pin / map-note labels. World context funnels through
    // acc::narration::AppendDisambiguator — the SAME entry point Q/E uses —
    // which routes creatures to the serial and statics to the whole-area
    // position ordinal (stable as more peers are discovered).
    if (mapCtx) {
        AppendPositionOrdinal(listing, s.focusedIndex, mapHint, name,
                              sizeof(name));
    } else {
        acc::narration::AppendDisambiguator(s.focusedObj, s.category, name,
                                            sizeof(name));
        // Placeable state tag (", gefangen" / ", deaktiviert" …) — same
        // suffix the Q/E narration path appends via GetSpokenName, so an
        // object speaks one consistent state everywhere. Self-gates on the
        // override tag table; no-op for everything else.
        acc::state::AppendStateLabel(s.focusedObj, name, sizeof(name));
    }

    Vector playerPos;
    float yaw = 0.0f;
    bool haveYaw = acc::engine::GetPlayerPosition(playerPos) &&
                   acc::engine::GetPlayerYawDegrees(yaw);
    int clock = 12;
    if (haveYaw) {
        clock = ClockPosition(yaw, objPos.x - playerPos.x,
                              objPos.y - playerPos.y);
    }
    int metres = static_cast<int>(listing.distances[s.focusedIndex] + 0.5f);

    char itemMsg[192];
    FormatItemPayload(name, haveYaw, clock, metres, itemMsg, sizeof(itemMsg));

    char fullMsg[256];
    if (categoryPrefix && categoryPrefix[0] != '\0') {
        std::snprintf(fullMsg, sizeof(fullMsg),
                      acc::strings::Get(acc::strings::Id::FmtCategoryItem),
                      categoryPrefix, itemMsg);
    } else {
        std::snprintf(fullMsg, sizeof(fullMsg), "%s", itemMsg);
    }
    prism::Speak(fullMsg, /*interrupt=*/true);

    // Stamp the unified activation slot — the user just heard the
    // object's name (with distance + clock), so Enter / Shift+- / Ctrl+- /
    // `-` should target it. Pin stamp shape carries the frozen position
    // since pins don't resolve through ResolveServerObjectHandle; game-
    // object stamp re-derives the server handle for the AI primitives.
    uint32_t serverHandle = 0;
    if (isPin) {
        acc::narrated_target::StampMapPin(s.focusedObj, objPos);
    } else {
        serverHandle = acc::engine::GetObjectHandle(s.focusedObj);
        acc::narrated_target::Stamp(s.focusedObj, serverHandle);
    }

    // Map-context: pan the virtual cursor to the focused object so the
    // cursor stays coherent with the cycle (otherwise the cursor would
    // sit at its last WASD position, contradicting the spoken focus).
    // The cursor suppresses its own re-announce when handed a waypoint
    // we just spoke, so no duplicate output. Pins aren't waypoints —
    // pass nullptr so the cursor's waypoint-suppress doesn't latch on
    // an unrelated pointer.
    if (mapCtx) {
        acc::map_ui_cursor::PanToWorld(
            objPos,
            (!isPin && s.category == acc::filter::CycleCategory::Landmark)
                ? s.focusedObj
                : nullptr);
    }

    acclog::Write("Cycle", "-> [%s] obj=%p handle=0x%08x ctx=%s isPin=%d",
                  fullMsg, s.focusedObj, serverHandle,
                  mapCtx ? "Map" : "World", (int)isPin);
}

// ---- Per-action handlers (shared by both ingestion paths) ----

// Step within the current category and announce the new focused item.
void OnCycleItem(bool prev, acc::filter::CycleContext ctx) {
    acc::cycle::CategoryListing listing;
    acc::cycle::RefreshCurrentListing(listing, ctx);
    if (prev) acc::cycle::CyclePrevItem(listing, ctx);
    else      acc::cycle::CycleNextItem(listing, ctx);
    AnnounceCurrent(listing, /*categoryPrefix=*/nullptr, ctx);
}

// Jump to the first (closest) or last (farthest) item of the current
// category and announce it. Same listing-refresh + announce flow as
// OnCycleItem; only the index target differs (Ctrl+, / Ctrl+.).
void OnCycleEdge(bool last, acc::filter::CycleContext ctx) {
    acc::cycle::CategoryListing listing;
    acc::cycle::RefreshCurrentListing(listing, ctx);
    if (last) acc::cycle::CycleLastItem(listing, ctx);
    else      acc::cycle::CycleFirstItem(listing, ctx);
    AnnounceCurrent(listing, /*categoryPrefix=*/nullptr, ctx);
}

// Step to the next/prev non-empty category and announce
// "{Category}. {closest item name}, {clock}, {metres}". When all six
// categories are empty, speaks the EmptyAll string without a prefix
// (the prefix would be misleading — no category was actually landed on).
void OnCycleCategory(bool prev, acc::filter::CycleContext ctx) {
    acc::cycle::CategoryListing listing;
    bool found = prev
        ? acc::cycle::CyclePrevCategory(listing, ctx)
        : acc::cycle::CycleNextCategory(listing, ctx);
    if (!found) {
        const char* msg = acc::strings::Get(acc::strings::Id::EmptyAll);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "-> [%s] (all empty, ctx=%s)", msg,
                      ctx == acc::filter::CycleContext::Map ? "Map" : "World");
        return;
    }
    auto& s = acc::cycle::GetState(ctx);
    auto bindings = BindingsFor(s.category, ctx);
    AnnounceCurrent(listing, acc::strings::Get(bindings.name), ctx);
}

// Classify a stamped object the same way cycle keys do. Returns Count_
// when the object falls outside the six locked nav categories — callers
// fall back to a generic vocabulary (default-open verb, Item cue) since
// passive_narrate would already have filtered any non-nav classification
// before stamping, so we shouldn't see Count_ in practice.
acc::filter::CycleCategory ClassifyForCycle(void* obj) {
    using C = acc::filter::CycleCategory;
    for (int i = 0; i < static_cast<int>(C::Count_); ++i) {
        auto c = static_cast<C>(i);
        if (acc::filter::ObjectMatches(obj, c)) return c;
    }
    return C::Count_;
}

// Resolve the narrated-target slot into the bundle every dash-family
// handler needs: object, server handle, current world position, current
// localised name, and current category. Returns false (and populates the
// out struct with zeroed fields) when the slot is empty or stale, in
// which case the caller speaks GuidanceNoFocus.
//
// "Current" matters here: cycle-input stamped at announce time, but the
// player may have walked since then, and the object may have moved.
// Always re-read pos / name on use so the activation payload reflects
// the world state at activation time, not stamp time.
struct NarratedActivation {
    void*    obj      = nullptr;
    uint32_t handle   = 0;
    Vector   pos      = {0.0f, 0.0f, 0.0f};
    char     name[128] = {};
    acc::filter::CycleCategory category = acc::filter::CycleCategory::Count_;
    bool     isMapPin = false;
};

bool ResolveNarratedActivation(NarratedActivation& out) {
    out = {};
    acc::narrated_target::Slot slot;
    if (!acc::narrated_target::TryGet(slot)) return false;

    if (slot.isMapPin) {
        // Map-pin path: pos is frozen at stamp time (pins don't move).
        // Name resolves from CSWCMapPin.note_text; fall back to the
        // generic placeholder when missing (user-placed pins always
        // have one assigned, but defended for safety).
        out.obj      = slot.obj;
        out.handle   = 0;
        out.pos      = slot.pos;
        out.category = acc::filter::CycleCategory::Landmark;  // for cue
        out.isMapPin = true;
        if (!acc::engine::GetMapPinNoteText(slot.obj, out.name,
                                            sizeof(out.name)) ||
            out.name[0] == '\0') {
            const char* fallback = acc::strings::Get(
                acc::strings::Id::MapPinNoText);
            std::snprintf(out.name, sizeof(out.name), "%s", fallback);
        }
        return true;
    }

    if (!acc::engine::GetObjectPosition(slot.obj, out.pos)) return false;

    out.obj      = slot.obj;
    out.handle   = slot.handle;
    out.category = ClassifyForCycle(slot.obj);
    out.isMapPin = false;
    if (!acc::engine::GetObjectName(slot.obj, out.name, sizeof(out.name)) ||
        out.name[0] == '\0') {
        // Fall back to the localised category name when the object name
        // is empty — same pattern AnnounceCurrent uses for cycle-stepped
        // items.
        const char* fallback = acc::strings::Get(
            BindingsFor(out.category).name);
        std::snprintf(out.name, sizeof(out.name), "%s", fallback);
    }
    return true;
}

// Wraps ResolveNarratedActivation with the shared empty-slot fallback used
// by `-` / Shift+- / Ctrl+- / Alt+-. On miss, speaks GuidanceNoFocus and
// logs `<tag> -> [<msg>] (no narrated target)`. Returns true iff a target
// was resolved; callers early-return on false.
bool TryResolveOrAnnounceNoFocus(NarratedActivation& a, const char* logTag) {
    if (ResolveNarratedActivation(a)) return true;
    const char* msg = acc::strings::Get(acc::strings::Id::GuidanceNoFocus);
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Cycle", "%s -> [%s] (no narrated target)", logTag, msg);
    return false;
}

// `-` repeats the last narrated target with fresh distance + clock. Reads
// the unified narrated_target slot rather than cycle_state directly — so
// `-` after a passive-narrate announcement re-announces the passive
// target, not whatever cycle was last focused on. Re-stamps the slot so
// the activation tick refreshes (same target, but the user just renewed
// their "this is what I'm thinking about" claim).
void OnAnnounceFocus() {
    NarratedActivation a;
    if (!TryResolveOrAnnounceNoFocus(a, "- (repeat)")) return;

    auto bindings = BindingsFor(a.category);
    {
        acc::audio::NavCue cue = RefineDoorCue(bindings.cue, a.obj);
        acc::audio::PlayCue3D(acc::audio::GetNavCueResref(cue), a.pos);
    }

    Vector playerPos;
    float yaw = 0.0f;
    bool haveYaw = acc::engine::GetPlayerPosition(playerPos) &&
                   acc::engine::GetPlayerYawDegrees(yaw);
    int clock = 12;
    if (haveYaw) {
        clock = ClockPosition(yaw, a.pos.x - playerPos.x,
                              a.pos.y - playerPos.y);
    }
    float dx = a.pos.x - playerPos.x;
    float dy = a.pos.y - playerPos.y;
    int metres = static_cast<int>(std::sqrt(dx * dx + dy * dy) + 0.5f);

    char msg[192];
    FormatItemPayload(a.name, haveYaw, clock, metres, msg, sizeof(msg));
    prism::Speak(msg, /*interrupt=*/true);

    // Re-stamp the slot so the activation tick refreshes. Map pins keep
    // the same frozen pos (pins don't move); game objects re-derive pos
    // on the next TryGet via GetObjectPosition.
    if (a.isMapPin) {
        acc::narrated_target::StampMapPin(a.obj, a.pos);
    } else {
        acc::narrated_target::Stamp(a.obj, a.handle);
    }
    acclog::Write("Cycle", "- (repeat) -> [%s] obj=%p handle=0x%08x mapPin=%d",
                  msg, a.obj, a.handle, (int)a.isMapPin);
}

// Per-kind pre-roll Id picker. Mirrors interact_hotkey's PreRollFor (kept
// local because that one's in an anon namespace) — the Shift+- autowalk
// speech uses the same vocabulary as Enter ("Sprich mit X", "Öffne X",
// "Hebe X auf") so users hear consistent verbs across the two paths.
acc::strings::Id GuidancePreRollFor(acc::filter::CycleCategory c) {
    using C = acc::filter::CycleCategory;
    using S = acc::strings::Id;
    switch (c) {
        case C::Door:       return S::FmtInteractOpen;
        case C::Npc:        return S::FmtInteractTalk;
        case C::Container:  return S::FmtInteractOpen;
        case C::Item:       return S::FmtInteractTake;
        case C::Landmark:   return S::FmtInteractOpen;
        case C::Transition: return S::FmtInteractOpen;
        case C::Count_:     break;
    }
    return S::FmtInteractOpen;
}

// Shift+- — autowalk the player to the currently-focused target. Hybrid
// dispatch:
//   - A real interactable object → `acc::guidance::UseObject`. The engine
//     walks to use-range and triggers the kind-appropriate callback (door
//     open, container loot, item pickup, NPC dialog, transition cross-load),
//     stopping at a sensible distance and facing the object.
//   - A map pin / waypoint, or any focused target without a usable object
//     handle (a non-interactable target), or an object whose USE the engine
//     refuses → `acc::guidance::WalkTo` straight to the world coordinate.
//
// The WalkTo coordinate path is new (2026-06-13). It used to be the only
// path (pre-2026-05-11) and was abandoned because AddMoveToPointAction
// "no-opped for the leader" — which we now know was a misdiagnosis: the move
// only needs the player's server AI enabled (and player input NOT disabled),
// which WalkTo now does. So the two cases that Shift+- previously refused —
// map pins (redirected to Ctrl+-) and non-interactable targets (spoke a
// failure) — now autowalk to the target's position like everything else.
//
// Empty-state: when no item is focused, speaks GuidanceNoFocus and bails.
//
// Cancel-on-second-press: when an autowalk is in flight, the next press
// cancels it via `acc::guidance::CancelMovement` (ClearAllActions, and it
// restores any AI-level WalkTo raised). Both UseObject (destHint) and WalkTo
// arm in-flight tracking, so the toggle works regardless of which path ran.
void OnPathfindFocus() {
    // Toggle-cancel branch — runs before the focus check so the user
    // can cancel with no focus selected (cycled past the end, focus
    // dropped, but they want to stop the in-flight walk).
    if (acc::guidance::IsApproachInFlight()) {
        bool ok = acc::guidance::CancelMovement();
        if (ok) {
            acc::guidance::CancelApproach();  // clear tracker (no announce)
            // Restore manual control immediately rather than waiting
            // for the 3s auto-restore — the user wants the keyboard
            // back NOW.
            acc::engine::SetPlayerInputEnabled(true);
            const char* msg = acc::strings::Get(
                acc::strings::Id::MovementCancelled);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("Cycle", "Shift+- -> [%s] (cancel path)", msg);
            return;
        }
        // Cancel SEH-faulted — fall through to dispatch so the second
        // press at least does *something*.
        acclog::Write("Cycle", "Shift+- cancel SEH-FAULT, falling through to UseObject");
    }

    NarratedActivation a;
    if (!TryResolveOrAnnounceNoFocus(a, "Shift+-")) return;

    // Per-category 3D cue at the destination — spatial confirmation of WHERE
    // we're walking, same pattern the announce path uses. (RefineDoorCue is a
    // no-op for map pins, which are never doors.)
    auto bindings = BindingsFor(a.category);
    {
        acc::audio::NavCue cue = RefineDoorCue(bindings.cue, a.obj);
        acc::audio::PlayCue3D(acc::audio::GetNavCueResref(cue), a.pos);
    }

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(GuidancePreRollFor(a.category)),
                  a.name);

    // Dispatch. Map pins and handle-less targets have no object to "use" —
    // walk straight to the coordinate. Real objects go through UseObject
    // (walk to use-range + trigger); if the engine refuses the use (a
    // non-interactable object), fall back to a plain coordinate walk so the
    // user still gets there. WalkTo manages its own AI-level + leaves player
    // input enabled, so we must NOT disable input around it; the UseObject
    // path keeps its proven input-disable.
    bool ok;
    const char* path;
    bool  inputDisabledForArm = false;
    void* armObj = nullptr;   // live-pos source for the tracker; coord walks
                              // (map pins) have no usable object → targetPos only
    // Transitions are TRIGGER regions — they fire when the PC walks *into* them,
    // not on "use". UseObject on a trigger queues a walk-to-use the engine can't
    // resolve (no use-node), so the PC never moves and the approach tracker
    // declares a false "Weg versperrt". Route them as a coordinate walk like map
    // pins: the engine A* walks to the trigger's position and crossing into the
    // region fires the transition.
    const bool walkToCoord =
        a.isMapPin || a.handle == 0 ||
        a.category == acc::filter::CycleCategory::Transition;
    if (walkToCoord) {
        ok   = acc::guidance::WalkTo(a.pos);
        path = "WalkTo(coord)";
    } else {
        bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);
        ok = acc::guidance::UseObject(a.handle);
        if (ok) {
            path = "UseObject";
            inputDisabledForArm = inputDisabled;
            armObj = a.obj;
        } else {
            if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
            ok   = acc::guidance::WalkTo(a.pos);
            path = "UseObject->WalkTo";
        }
    }

    if (ok) {
        prism::Speak(msg, /*interrupt=*/true);
        // Arm the unified approach tracker (Cycle-owned, so Shift+- / W can
        // toggle-cancel it): if this walk stalls short of the target instead of
        // arriving, TickApproach announces "way blocked".
        acc::guidance::ApproachArm arm;
        arm.owner         = acc::guidance::ApproachOwner::Cycle;
        std::snprintf(arm.name, sizeof(arm.name), "%s", a.name);
        arm.targetObj     = armObj;
        arm.targetPos     = a.pos;
        arm.inputDisabled = inputDisabledForArm;
        arm.isDialog      = false;
        arm.speakBlocked  = true;
        acc::guidance::ArmApproach(arm);
        acclog::Write("Cycle", "Shift+- -> [%s] via %s obj=%p handle=0x%08x "
                      "pin=%d dest=(%.2f,%.2f,%.2f)",
                      msg, path, a.obj, a.handle, a.isMapPin ? 1 : 0,
                      a.pos.x, a.pos.y, a.pos.z);
    } else {
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(acc::strings::Id::FmtGuidingFailed),
                      a.name);
        prism::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Cycle", "Shift+- -> [%s] all paths FAILED (last=%s) "
                      "obj=%p handle=0x%08x pin=%d",
                      failMsg, path, a.obj, a.handle, a.isMapPin ? 1 : 0);
    }
}

// Ctrl+- — start an audio beacon to the currently-focused Pillar 4 object
// via our own A* over the static per-area nav graph (engine refuses to
// plot for the leader). Speaks a turn-by-turn route description as a
// sanity-check on the calculated path, then arms the beacon — heartbeat
// cues at each next waypoint, reach cues on arrival.
//
// Toggle-cancel: when a beacon is already armed, the second press
// cancels via `acc::guidance::beacon::CancelBeacon` and speaks the
// localised "Beacon cancelled" phrase.
//
// Independent of Shift+- autowalk — the user can dispatch both in any
// order (engine walks via UseObject, our beacon pulses cues in parallel
// for spatial reinforcement).
void OnBeaconFocus() {
    if (acc::guidance::beacon::IsActive()) {
        acc::guidance::beacon::CancelBeacon();
        const char* msg = acc::strings::Get(
            acc::strings::Id::BeaconCancelled);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] (cancel beacon)", msg);
        return;
    }

    NarratedActivation a;
    if (!TryResolveOrAnnounceNoFocus(a, "Ctrl+-")) return;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        // Should be impossible — caller gated on GetPlayerPosition — but
        // defend so the failure path speaks rather than going silent.
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] (player pos unavailable)", msg);
        return;
    }

    void* area = acc::engine::GetCurrentArea();

    // Per-category 3D cue at the destination — same spatial-confirmation
    // pattern as Shift+-. Plays before any speech so the user hears the
    // category cue → opener → description in audible order.
    auto bindings = BindingsFor(a.category);
    {
        acc::audio::NavCue cue = RefineDoorCue(bindings.cue, a.obj);
        acc::audio::PlayCue3D(acc::audio::GetNavCueResref(cue), a.pos);
    }

    std::vector<Vector> waypoints;
    bool pathOk = acc::guidance::ComputePath(area, playerPos, a.pos, waypoints);

    if (!pathOk || waypoints.empty()) {
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(acc::strings::Id::FmtBeaconNoPath),
                      a.name);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] obj=%p (ComputePath failed)",
                      msg, a.obj);
        return;
    }

    // Speak the opener ("Beacon zu {name}") first, then the turn-by-turn
    // route description. Two separate Prism calls so screen readers can
    // queue them in order without interrupt-mid-sentence behaviour. The
    // opener uses interrupt=true (preempt any in-flight speech); the
    // description uses interrupt=false so it queues behind the opener.
    char opener[192];
    std::snprintf(opener, sizeof(opener),
                  acc::strings::Get(acc::strings::Id::FmtBeaconStarted),
                  a.name);
    prism::Speak(opener, /*interrupt=*/true);

    bool isTransition = (a.category == acc::filter::CycleCategory::Transition);
    acc::guidance::description::Speak(playerPos, waypoints, a.name,
                                      isTransition, /*interrupt=*/false);

    acc::guidance::beacon::StartBeacon(waypoints);

    acclog::Write("Cycle", "Ctrl+- -> [%s] obj=%p waypoints=%zu "
                  "dest=(%.2f,%.2f,%.2f) transition=%d",
                  opener, a.obj, waypoints.size(),
                  a.pos.x, a.pos.y, a.pos.z, isTransition ? 1 : 0);
}

// Alt+- — diagnostic alternate path that bypasses the action queue via
// CSWSCreature::ForceMoveToPoint instead of AddMoveToPointAction. Same
// payload semantics (cue + Prism announce); only the engine entry point
// differs. Used to discriminate "queue contention" from "input-mode /
// flag-bit" failure modes when WalkTo is silently dropped.
//
// Speech is identical to OnPathfindFocus — the user's audible feedback
// is the same so behaviour comparison is purely about whether the
// character actually moves. The log distinguishes via "(force path)" /
// "(queue path)" tags so post-mortem grep finds each path cleanly.
void OnPathfindFocusForce() {
    NarratedActivation a;
    if (!TryResolveOrAnnounceNoFocus(a, "Alt+-")) return;

    // Alt+- is the queue-bypass ForceMoveToPoint diagnostic; for map
    // pins there's no UseObject fallback either. Speak the unsupported
    // phrase and direct the user to Ctrl+- which DOES work for pin
    // coordinates.
    if (a.isMapPin) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::MapPinAltDashUnsupported);
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Alt+- -> [%s] (map-pin unsupported) pin=%p",
                      msg, a.obj);
        return;
    }

    auto bindings = BindingsFor(a.category);
    {
        acc::audio::NavCue cue = RefineDoorCue(bindings.cue, a.obj);
        acc::audio::PlayCue3D(acc::audio::GetNavCueResref(cue), a.pos);
    }

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtGuidingTo),
                  a.name);

    bool ok = acc::guidance::ForceWalkTo(a.pos);
    if (ok) {
        prism::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Alt+- -> [%s] obj=%p "
                      "dest=(%.2f,%.2f,%.2f) (force path)",
                      msg, a.obj,
                      a.pos.x, a.pos.y, a.pos.z);
    } else {
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(acc::strings::Id::FmtGuidingFailed),
                      a.name);
        prism::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Cycle", "Alt+- -> [%s] ForceWalkTo FAILED obj=%p "
                      "dest=(%.2f,%.2f,%.2f)",
                      failMsg, a.obj, a.pos.x, a.pos.y, a.pos.z);
    }
}

}  // namespace

bool TryHandleEvent(int param_1, int param_2) {
    // Shift state tracking — fires on both press and release, never consumed.
    // Notify peek_description on the release edge so its block cursor resets
    // (so the next Shift+arrow starts at block 0 again).
    if (param_1 == kInputKbLeftShift || param_1 == kInputKbRightShift) {
        bool wasHeld = g_engineShiftHeld;
        g_engineShiftHeld = (param_2 != 0);
        if (wasHeld && !g_engineShiftHeld) {
            acc::peek::OnShiftReleased();
        }
        return false;
    }

    // Press only beyond this point.
    if (param_2 == 0) return false;

    // Filter to the three codes we care about before any further work.
    if (param_1 != kInputKbComma  &&
        param_1 != kInputKbPeriod &&
        param_1 != kInputKbAnnounce) {
        return false;
    }

    // Gate on "is the player actually in-game?". GetPlayerPosition returns
    // false on menus / chargen pre-spawn / area-load mid-flight, in which
    // case we let the key pass through to its normal engine handler.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return false;

    // Map sub-screen captures the cycle keys when it's foreground (under
    // the InGameMenu strip — same scan pattern map_ui_cursor uses). World
    // and Map cycle states are independent; the user can return to
    // in-world cycle exactly where they left off after closing the map.
    acc::filter::CycleContext ctx =
        acc::engine::HasActiveMapPanel()
            ? acc::filter::CycleContext::Map
            : acc::filter::CycleContext::World;

    if (param_1 == kInputKbComma || param_1 == kInputKbPeriod) {
        // In-world `,`/`.` are always live: by default they drive the
        // DISCOVERY tier (only objects the player has organically had
        // narrated), and the "Extended cycling" mod setting widens the same
        // keys to everything in the area. That discovery-vs-extended filter
        // lives in cycle_state::BuildCategoryListing — here we always
        // dispatch. The `-` family (announce / autowalk / beacon) maps to
        // kInputKbAnnounce below and is unaffected.
        const bool prev = (param_1 == kInputKbComma);
        // Ctrl+, / Ctrl+. jump to the first / last item of the current
        // category. Ctrl wins over Shift (matches the dash-family
        // Ctrl>Alt>Shift precedence). The engine latches Shift for us
        // (g_engineShiftHeld) but not Ctrl, so read OS-level Ctrl state
        // directly — same source PollWin32's binding match uses.
        const bool ctrl = acc::hotkeys::CtrlHeld();
        if (ctrl)              OnCycleEdge    (/*last=*/!prev, ctx);
        else if (g_engineShiftHeld) OnCycleCategory(prev, ctx);
        else                   OnCycleItem    (prev, ctx);
        // Suppress PollWin32 from re-dispatching the same press. In Map
        // context the engine routes `,`/`.` through the manager hook
        // AND PollWin32 still sees the OS-level press → without claim
        // the user hears each press dispatched twice. ClaimRisingEdge
        // (not Consume) is required because the manager input fires
        // between EndTick and BeginTick — see the InteractTarget claim
        // in menus.cpp::OnHandleInputEvent for the same pattern.
        if (ctrl) {
            acc::hotkeys::ClaimRisingEdge(
                prev ? acc::hotkeys::Action::CycleItemFirst
                     : acc::hotkeys::Action::CycleItemLast);
        } else if (g_engineShiftHeld) {
            acc::hotkeys::ClaimRisingEdge(
                prev ? acc::hotkeys::Action::CycleCategoryPrev
                     : acc::hotkeys::Action::CycleCategoryNext);
        } else {
            acc::hotkeys::ClaimRisingEdge(
                prev ? acc::hotkeys::Action::CycleItemPrev
                     : acc::hotkeys::Action::CycleItemNext);
        }
        return true;
    }
    if (param_1 == kInputKbAnnounce) {
        if (g_engineShiftHeld) OnPathfindFocus();
        else                   OnAnnounceFocus();
        return true;
    }
    return false;
}

void PollWin32() {
    // All bindings live in `hotkeys.cpp`. Modifier-precedence at the
    // announce / autowalk / force / beacon key is encoded via per-Action
    // `modsRequired` + `modsForbidden`, so each Action::* below is
    // mutually exclusive given the current keyboard state.
    namespace hk = acc::hotkeys;

    bool risingCommaItem     = hk::Pressed(hk::Action::CycleItemPrev);
    bool risingCommaCategory = hk::Pressed(hk::Action::CycleCategoryPrev);
    bool risingPeriodItem    = hk::Pressed(hk::Action::CycleItemNext);
    bool risingPeriodCategory= hk::Pressed(hk::Action::CycleCategoryNext);
    bool risingCommaFirst    = hk::Pressed(hk::Action::CycleItemFirst);
    bool risingPeriodLast    = hk::Pressed(hk::Action::CycleItemLast);
    bool risingAnnounce      = hk::Pressed(hk::Action::AnnounceFocus);
    bool risingPathfind      = hk::Pressed(hk::Action::PathfindFocus);
    bool risingPathfindForce = hk::Pressed(hk::Action::PathfindFocusForce);
    bool risingBeacon        = hk::Pressed(hk::Action::BeaconFocus);

    if (!risingCommaItem && !risingCommaCategory &&
        !risingPeriodItem && !risingPeriodCategory &&
        !risingCommaFirst && !risingPeriodLast &&
        !risingAnnounce && !risingPathfind &&
        !risingPathfindForce && !risingBeacon) return;

    // Gate on in-world. In menus / chargen / pre-spawn this short-circuits
    // before any cycle effect. (Foreground gate already applied by Pressed().)
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    // Same context split as TryHandleEvent — map foreground routes cycle
    // keys to the map-cycle state singleton, otherwise world.
    acc::filter::CycleContext ctx =
        acc::engine::HasActiveMapPanel()
            ? acc::filter::CycleContext::Map
            : acc::filter::CycleContext::World;

    // In-world `,`/`.` (item + category, both shift variants) are always
    // live now: by default they drive the DISCOVERY tier; the "Extended
    // cycling" mod setting widens the listing to everything. The
    // discovery-vs-extended filter lives in cycle_state::BuildCategoryListing,
    // so input always dispatches here. Map context was always live and stays
    // so. The `-` family below stays unaffected.
    {
        if (risingCommaItem)      OnCycleItem    (/*prev=*/true,  ctx);
        if (risingCommaCategory)  OnCycleCategory(/*prev=*/true,  ctx);
        if (risingPeriodItem)     OnCycleItem    (/*prev=*/false, ctx);
        if (risingPeriodCategory) OnCycleCategory(/*prev=*/false, ctx);
        if (risingCommaFirst)     OnCycleEdge    (/*last=*/false, ctx);
        if (risingPeriodLast)     OnCycleEdge    (/*last=*/true,  ctx);
    }

    // Precedence: Ctrl > Alt > Shift > bare. The Action bindings encode
    // this via mutually-exclusive modifier masks, so at most one of these
    // four can be true on a given tick. Beacon wins ties (Ctrl+Shift+-
    // routes to beacon, not autowalk), then ForceWalkTo (Alt — diagnostic
    // queue-bypass kept for future companion-NPC nudges), then autowalk
    // via UseObject (Shift), then bare repeat-announce.
    if (risingBeacon)             OnBeaconFocus();
    else if (risingPathfindForce) OnPathfindFocusForce();
    else if (risingPathfind)      OnPathfindFocus();
    else if (risingAnnounce)      OnAnnounceFocus();
}

}  // namespace acc::cycle_input
