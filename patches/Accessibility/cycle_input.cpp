#include "cycle_input.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include "engine_input.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "guidance_autowalk.h"
#include "guidance_beacon.h"
#include "guidance_description.h"
#include "guidance_pathfind.h"
#include "hotkeys.h"
#include "log.h"
#include "map_ui_cursor.h"
#include "narrated_target.h"
#include "peek_description.h"
#include "strings.h"
#include "tolk.h"

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
int ClockPosition(float playerYawDeg, float dx, float dy) {
    constexpr float kRadToDeg = 57.29577951308232f;
    float worldAngle = std::atan2(dy, dx) * kRadToDeg;
    float relCcw = worldAngle - playerYawDeg;
    float clockDeg = -relCcw;  // flip to clockwise from +ahead
    while (clockDeg < 0.0f)    clockDeg += 360.0f;
    while (clockDeg >= 360.0f) clockDeg -= 360.0f;
    int hour = static_cast<int>(clockDeg / 30.0f + 0.5f) % 12;
    return hour == 0 ? 12 : hour;
}

// Map a CycleCategory to its strings::Id pair (singular name + empty
// message) and its 3D audio cue. Centralises the table so OnCycleItem /
// OnCycleCategory don't each carry duplicate switches.
struct CategoryBindings {
    acc::strings::Id  name;    // singular for prefix / fallback
    acc::strings::Id  empty;   // full pre-formatted "no X in range"
    acc::audio::NavCue cue;    // 3D positional cue played at the focused
                               // object's world position before speech
};

CategoryBindings BindingsFor(
        acc::filter::CycleCategory c,
        acc::filter::CycleContext ctx = acc::filter::CycleContext::World) {
    using S = acc::strings::Id;
    using N = acc::audio::NavCue;
    using C = acc::filter::CycleCategory;
    switch (c) {
        case C::Door:
            return {S::CategoryDoor,       S::EmptyDoors,       N::Door};
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
        case C::MapPin:
            // Reuse Landmark cue — map pins ARE the engine's "important
            // location" affordance, same semantic family as map notes.
            // Separate cue can ship later if user testing shows the
            // overlap is confusing.
            return {S::CategoryMapPin,     S::EmptyMapPins,     N::Landmark};
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
        // Speech path: normal Tolk (NVDA primary) in both contexts.
        // SpeakUrgent stays reserved for sustained-key-hold scenarios
        // (map cursor WASD pan); single-press cycle keys don't suffer
        // the typed-character cancel feedback loop in practice.
        const char* msg = acc::strings::Get(bindings.empty);
        tolk::Speak(msg, /*interrupt=*/true);
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
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), objPos);

    char name[128] = "";
    bool isMapPin = (s.category == acc::filter::CycleCategory::MapPin);
    if (isMapPin) {
        // Map pin: read CSWCMapPin.note_text (+0x100). Fall back to the
        // generic "Quest marker" placeholder when the pin carries no
        // inline text or TLK strref — most quest-script pins have one
        // or the other, but defensive empty-name fallback applies.
        if (!acc::engine::GetMapPinNoteText(s.focusedObj, name, sizeof(name))
            || name[0] == '\0') {
            std::snprintf(name, sizeof(name), "%s",
                          acc::strings::Get(acc::strings::Id::MapPinNoText));
        }
    } else {
        if (!acc::engine::GetObjectName(s.focusedObj, name, sizeof(name)) ||
            name[0] == '\0') {
            // Fall back to the localized category name — at least the user
            // knows the kind even if no localized name resolves.
            std::snprintf(name, sizeof(name), "%s",
                          acc::strings::Get(bindings.name));
        }
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
    tolk::Speak(fullMsg, /*interrupt=*/true);

    // Stamp the unified activation slot — the user just heard the
    // object's name (with distance + clock), so Enter / Shift+- / Ctrl+- /
    // `-` should target it. Two stamp shapes:
    //   - Normal game object: Server-side handle is what the AI
    //     primitives need; cycle stores CSWSObject* directly so
    //     GetObjectHandle re-derives.
    //   - Map pin: no server-side handle exists. Stamp with the pin
    //     pointer + frozen position so activation handlers can route
    //     Ctrl+- beacon to the pin's location and reject Shift+-/Enter
    //     with a localized hint.
    uint32_t serverHandle = 0;
    if (isMapPin) {
        acc::narrated_target::StampMapPin(s.focusedObj, objPos);
    } else {
        serverHandle = acc::engine::GetObjectHandle(s.focusedObj);
        acc::narrated_target::Stamp(s.focusedObj, serverHandle);
    }

    // Map-context: pan the virtual cursor to the focused object so the
    // cursor stays coherent with the cycle (otherwise the cursor would
    // sit at its last WASD position, contradicting the spoken focus).
    // The cursor suppresses its own re-announce when handed a waypoint
    // we just spoke, so no duplicate output.
    if (mapCtx) {
        acc::map_ui_cursor::PanToWorld(
            objPos,
            s.category == acc::filter::CycleCategory::Landmark
                ? s.focusedObj
                : nullptr);
    }

    acclog::Write("Cycle", "-> [%s] obj=%p handle=0x%08x ctx=%s mapPin=%d",
                  fullMsg, s.focusedObj, serverHandle,
                  mapCtx ? "Map" : "World", (int)isMapPin);
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
        tolk::Speak(msg, /*interrupt=*/true);
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
        // Map pin path: position is frozen at stamp time (pins don't
        // move), name is the note_text from CSWCMapPin +0x100.
        out.obj      = slot.obj;
        out.handle   = 0;
        out.pos      = slot.pos;
        out.category = acc::filter::CycleCategory::MapPin;
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

// `-` repeats the last narrated target with fresh distance + clock. Reads
// the unified narrated_target slot rather than cycle_state directly — so
// `-` after a passive-narrate announcement re-announces the passive
// target, not whatever cycle was last focused on. Re-stamps the slot so
// the activation tick refreshes (same target, but the user just renewed
// their "this is what I'm thinking about" claim).
void OnAnnounceFocus() {
    NarratedActivation a;
    if (!ResolveNarratedActivation(a)) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "- (repeat) -> [%s] (no narrated target)",
                      msg);
        return;
    }

    auto bindings = BindingsFor(a.category);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), a.pos);

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
    tolk::Speak(msg, /*interrupt=*/true);

    // Re-stamp the slot so the activation tick refreshes. Map pins keep
    // the same frozen pos (pins don't move); game objects re-derive
    // pos on the next TryGet via GetObjectPosition.
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

// Shift+- — guide the player to the currently-focused Pillar 4 object via
// `acc::guidance::UseObject`. Plays the per-category 3D cue as spatial
// confirmation, then speaks the per-kind pre-roll ("Sprich mit X", "Öffne X",
// "Hebe X auf"). The engine pathfinds + walks the player + triggers the
// kind-appropriate USE callback (door open, container loot, item pickup,
// NPC dialog start, transition cross-load).
//
// Migrated 2026-05-11 from `WalkTo` (AddMoveToPointAction) to `UseObject`
// (AddUseObjectAction) per Phase 5 architectural pivot — the
// AddMoveToPointAction queue silently no-ops for the leader (engine routes
// it through the FollowLeader path). UseObject is engine-proven for the
// leader case; same primitive the Enter interact hotkey falls back to.
//
// Empty-state: when no item is focused, speaks GuidanceNoFocus and bails.
//
// Cancel-on-second-press: when an autowalk is in flight, the next press
// cancels it via `acc::guidance::CancelMovement` (ClearAllActions). The
// `destHint` we pass to UseObject arms in-flight tracking so this toggle
// works for the new path as well.
void OnPathfindFocus() {
    // Toggle-cancel branch — runs before the focus check so the user
    // can cancel with no focus selected (cycled past the end, focus
    // dropped, but they want to stop the in-flight walk).
    if (acc::guidance::IsAutowalkInFlight()) {
        bool ok = acc::guidance::CancelMovement();
        if (ok) {
            // Restore manual control immediately rather than waiting
            // for the 3s auto-restore — the user wants the keyboard
            // back NOW.
            acc::engine::SetPlayerInputEnabled(true);
            const char* msg = acc::strings::Get(
                acc::strings::Id::MovementCancelled);
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write("Cycle", "Shift+- -> [%s] (cancel path)", msg);
            return;
        }
        // Cancel SEH-faulted — fall through to dispatch so the second
        // press at least does *something*.
        acclog::Write("Cycle", "Shift+- cancel SEH-FAULT, falling through to UseObject");
    }

    NarratedActivation a;
    if (!ResolveNarratedActivation(a)) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Shift+- -> [%s] (no narrated target)", msg);
        return;
    }

    // Map pins have no UseObject path — they aren't game objects, so
    // the engine has nothing to walk-to-and-trigger. Redirect the user
    // to Ctrl+- which beacons to the pin's world position. Play the
    // landmark cue at the pin position first so the user still gets
    // spatial confirmation of WHERE the pin is.
    if (a.isMapPin) {
        auto pinBindings = BindingsFor(a.category);
        acc::audio::PlayCue3D(acc::audio::GetNavCueResref(pinBindings.cue),
                              a.pos);
        const char* hint = acc::strings::Get(
            acc::strings::Id::MapPinShiftDashHint);
        tolk::Speak(hint, /*interrupt=*/true);
        acclog::Write("Cycle", "Shift+- -> [%s] (map-pin not autowalkable) "
                      "pin=%p pos=(%.2f,%.2f,%.2f)",
                      hint, a.obj, a.pos.x, a.pos.y, a.pos.z);
        return;
    }

    // Per-category 3D cue at the destination — same spatial-confirmation
    // pattern the announce path uses.
    auto bindings = BindingsFor(a.category);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), a.pos);

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(GuidancePreRollFor(a.category)),
                  a.name);

    // Disable per-tick player-input clobber for the duration of the AI
    // action; engine's TickPlayerInputRestore auto-flips back after ~3s.
    bool inputDisabled = acc::engine::SetPlayerInputEnabled(false);

    bool ok = acc::guidance::UseObject(a.handle, a.pos);
    if (ok) {
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Shift+- -> [%s] obj=%p handle=0x%08x "
                      "dest=(%.2f,%.2f,%.2f) input_disabled=%d",
                      msg, a.obj, a.handle,
                      a.pos.x, a.pos.y, a.pos.z,
                      inputDisabled ? 1 : 0);
    } else {
        if (inputDisabled) acc::engine::SetPlayerInputEnabled(true);
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(acc::strings::Id::FmtInteractFailed),
                      a.name);
        tolk::Speak(failMsg, /*interrupt=*/true);
        acclog::Write("Cycle", "Shift+- -> [%s] UseObject FAILED obj=%p "
                      "handle=0x%08x",
                      failMsg, a.obj, a.handle);
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
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] (cancel beacon)", msg);
        return;
    }

    NarratedActivation a;
    if (!ResolveNarratedActivation(a)) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] (no narrated target)", msg);
        return;
    }

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        // Should be impossible — caller gated on GetPlayerPosition — but
        // defend so the failure path speaks rather than going silent.
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] (player pos unavailable)", msg);
        return;
    }

    void* area = acc::engine::GetCurrentArea();

    // Per-category 3D cue at the destination — same spatial-confirmation
    // pattern as Shift+-. Plays before any speech so the user hears the
    // category cue → opener → description in audible order.
    auto bindings = BindingsFor(a.category);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), a.pos);

    std::vector<Vector> waypoints;
    bool pathOk = acc::guidance::ComputePath(area, playerPos, a.pos, waypoints);

    if (!pathOk || waypoints.empty()) {
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      acc::strings::Get(acc::strings::Id::FmtBeaconNoPath),
                      a.name);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Ctrl+- -> [%s] obj=%p (ComputePath failed)",
                      msg, a.obj);
        return;
    }

    // Speak the opener ("Beacon zu {name}") first, then the turn-by-turn
    // route description. Two separate Tolk calls so screen readers can
    // queue them in order without interrupt-mid-sentence behaviour. The
    // opener uses interrupt=true (preempt any in-flight speech); the
    // description uses interrupt=false so it queues behind the opener.
    char opener[192];
    std::snprintf(opener, sizeof(opener),
                  acc::strings::Get(acc::strings::Id::FmtBeaconStarted),
                  a.name);
    tolk::Speak(opener, /*interrupt=*/true);

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
// payload semantics (cue + Tolk announce); only the engine entry point
// differs. Used to discriminate "queue contention" from "input-mode /
// flag-bit" failure modes when WalkTo is silently dropped.
//
// Speech is identical to OnPathfindFocus — the user's audible feedback
// is the same so behaviour comparison is purely about whether the
// character actually moves. The log distinguishes via "(force path)" /
// "(queue path)" tags so post-mortem grep finds each path cleanly.
void OnPathfindFocusForce() {
    NarratedActivation a;
    if (!ResolveNarratedActivation(a)) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::GuidanceNoFocus);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Alt+- -> [%s] (no narrated target)", msg);
        return;
    }

    // Alt+- is the queue-bypass ForceMoveToPoint diagnostic; it's
    // known-broken for the leader anyway (per memory
    // project_addmovetopoint_leader_broken), and for map pins there's
    // no UseObject fallback either. Speak the unsupported phrase and
    // direct the user to Ctrl+- which DOES work for pin coordinates.
    if (a.isMapPin) {
        const char* msg = acc::strings::Get(
            acc::strings::Id::MapPinAltDashUnsupported);
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Alt+- -> [%s] (map-pin unsupported) pin=%p",
                      msg, a.obj);
        return;
    }

    auto bindings = BindingsFor(a.category);
    acc::audio::PlayCue3D(acc::audio::GetNavCueResref(bindings.cue), a.pos);

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtGuidingTo),
                  a.name);

    bool ok = acc::guidance::ForceWalkTo(a.pos);
    if (ok) {
        tolk::Speak(msg, /*interrupt=*/true);
        acclog::Write("Cycle", "Alt+- -> [%s] obj=%p "
                      "dest=(%.2f,%.2f,%.2f) (force path)",
                      msg, a.obj,
                      a.pos.x, a.pos.y, a.pos.z);
    } else {
        char failMsg[192];
        std::snprintf(failMsg, sizeof(failMsg),
                      acc::strings::Get(acc::strings::Id::FmtGuidingFailed),
                      a.name);
        tolk::Speak(failMsg, /*interrupt=*/true);
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

    if (param_1 == kInputKbComma) {
        if (g_engineShiftHeld) OnCycleCategory(/*prev=*/true,  ctx);
        else                   OnCycleItem    (/*prev=*/true,  ctx);
        return true;
    }
    if (param_1 == kInputKbPeriod) {
        if (g_engineShiftHeld) OnCycleCategory(/*prev=*/false, ctx);
        else                   OnCycleItem    (/*prev=*/false, ctx);
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
    bool risingAnnounce      = hk::Pressed(hk::Action::AnnounceFocus);
    bool risingPathfind      = hk::Pressed(hk::Action::PathfindFocus);
    bool risingPathfindForce = hk::Pressed(hk::Action::PathfindFocusForce);
    bool risingBeacon        = hk::Pressed(hk::Action::BeaconFocus);

    if (!risingCommaItem && !risingCommaCategory &&
        !risingPeriodItem && !risingPeriodCategory &&
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

    if (risingCommaItem)      OnCycleItem    (/*prev=*/true,  ctx);
    if (risingCommaCategory)  OnCycleCategory(/*prev=*/true,  ctx);
    if (risingPeriodItem)     OnCycleItem    (/*prev=*/false, ctx);
    if (risingPeriodCategory) OnCycleCategory(/*prev=*/false, ctx);

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
