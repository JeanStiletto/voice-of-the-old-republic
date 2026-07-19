#include "view_mode.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "user32.lib")

#include "audio_bus.h"
#include "audio_cues.h"
#include "camera_announce.h"
#include "engine_area.h"
#include "engine_manager.h"
#include "engine_panels.h"
#include "engine_options.h"
#include "engine_offsets.h"
#include "engine_player.h"
#include "filter_objects.h"
#include "guidance_autowalk.h"
#include "hotkeys.h"
#include "interact_hotkey.h"
#include "log.h"
#include "narrated_target.h"
#include "spatial_change_detector.h"
#include "strings.h"
#include "prism.h"
#include "transitions.h"
#include "wall_topology.h"

namespace acc::view_mode {

namespace {

constexpr float kCursorSpeedMps = 2.0f;       // matches KOTOR walk pace
constexpr float kMaxDtSec = 0.1f;             // guard against teleport-on-stall
constexpr DWORD kHoverPauseMs = 300;          // settle time before speaking
constexpr float kHoverRadiusMeters = 1.0f;    // cursor must be on the object
constexpr DWORD kCollisionCueQuietMs = 250;   // collapse sustained wall contact

struct ViewModeState {
    bool   active            = false;
    Vector cursor_pos        = { 0.0f, 0.0f, 0.0f };
    float  cursor_yaw_deg    = 0.0f;   // engine frame: 0° = +X, CCW positive
    DWORD  last_tick_ms      = 0;
    DWORD  last_collision_ms = 0;

    // Hover-pause object debounce. Both handle and CSWSObject* are written
    // together so Enter dispatch reads them as a pair without re-resolving.
    uint32_t hover_last_spoken     = 0;
    uint32_t hover_pending         = 0;
    void*    hover_pending_obj     = nullptr;
    DWORD    hover_pending_started = 0;

    // Region-cursor announce. Text-equality dedup so adjacent rooms with
    // identical labels collapse to one announce.
    std::string region_pending_text;
    DWORD region_pending_started_ms  = 0;
    std::string region_last_spoken_text;
};

ViewModeState g_state;

// Single-tick Enter ownership. Set by PollEnter, read-and-cleared by
// interact_hotkey::PollHotkey so the same press can't dispatch twice.
bool g_enter_consumed_this_tick = false;

// Deferred-dispatch state. WalkTo silently no-ops when input has been at 0
// for many ticks — engine needs a fresh 1→0 transition right before AI
// dispatch. Synchronous round-trip in the same tick doesn't work either:
// engine never processes the intermediate enabled=1. So PollEnter exits
// view mode + re-enables input, arms this struct, and Tick fires the
// dispatch on the next tick after the engine has settled.
//
// Hover obj/handle are NOT snapshotted: hover speech stamps the unified
// narrated_target slot, ProcessPendingDispatch reads from there at
// dispatch time. Only the empty-cursor WalkTo path keeps a position
// snapshot (no associated object).
struct PendingDispatch {
    bool     active        = false;
    bool     hasHover      = false;
    Vector   cursor_pos    = {0.0f, 0.0f, 0.0f};
    bool     forceRadial   = false;
    DWORD    armed_at_ms   = 0;
};
PendingDispatch g_pending;

constexpr DWORD kPendingDispatchMinElapsedMs = 16;  // ~1 frame at 60fps

// Listener override note: the per-frame listener write lives in a detour on
// CExoSound::SetListenerPosition (hooks.toml). Doing it from this file's
// Tick would race the engine's own write in UpdateSoundEngine, which runs
// after OnUpdate. The detour substitutes cursor_pos for the engine's
// camera-derived Vector while view mode is active.
//
// No Mouse Look forcing / cursor recentring needed: the camera-rotate keys
// (Y/C under our keymap defaults) pan the camera only, the character only
// snaps on W/S commit. So freezing W/S via SetPlayerInputEnabled(false) is
// enough — the camera keys keep panning natively while W/S and the strafe
// keys (A/D) are frozen, so we can safely reuse A/D to drive the view cursor.

void EnterViewMode() {
    // armAutoRestore=false: view mode lasts until the user toggles back.
    // Default true auto-restores after 3s mid-session.
    if (!acc::engine::SetPlayerInputEnabled(false, /*armAutoRestore=*/false)) {
        acclog::Write("ViewMode", "enter REFUSED — SetPlayerInputEnabled(false) "
            "failed (chain unresolved or SEH); skipping toggle");
        return;
    }

    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) {
        acc::engine::SetPlayerInputEnabled(true);
        acclog::Write("ViewMode", "enter REFUSED — player position unavailable "
            "post-disable; rolled back");
        return;
    }
    g_state.cursor_pos = pos;
    float yaw = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(yaw)) {
        // Degenerate facing during spawn — first cursor step rebases on
        // camera yaw next tick.
        yaw = 0.0f;
    }
    g_state.cursor_yaw_deg     = yaw;
    g_state.last_tick_ms       = GetTickCount();
    g_state.last_collision_ms  = 0;
    g_state.hover_last_spoken     = 0;
    g_state.hover_pending         = 0;
    g_state.hover_pending_obj     = nullptr;
    g_state.hover_pending_started = 0;
    g_state.region_pending_text.clear();
    g_state.region_pending_started_ms   = 0;
    g_state.region_last_spoken_text.clear();

    g_state.active = true;
    prism::Speak(acc::strings::Get(acc::strings::Id::ViewModeOn),
                /*interrupt=*/true);
    acclog::Write("ViewMode", "ENTER cursor=(%.2f,%.2f,%.2f) yaw=%.1f",
        pos.x, pos.y, pos.z, yaw);
}

void ExitViewMode() {
    bool restored = acc::engine::SetPlayerInputEnabled(true);
    g_state.active = false;
    // Engine reclaims the listener from the camera next frame; no write here.
    prism::Speak(acc::strings::Get(acc::strings::Id::ViewModeOff),
                /*interrupt=*/true);
    acclog::Write("ViewMode", "EXIT restored=%d", restored ? 1 : 0);
}

void ToggleViewMode() {
    if (g_state.active) ExitViewMode();
    else                EnterViewMode();
}

// Shift+B diagnostic: snapshot CClientOptions to the log. Cheap and
// unintrusive; keeps catching state changes during unrelated RE work.
void DumpCameraStateProbe() {
    void* clientOptions = acc::engine::GetClientOptions();
    if (!clientOptions) {
        acclog::Write("ViewModeProbe", "Shift+B — GetClientOptions returned null "
            "(chain unresolved or SEH); nothing to snapshot");
        return;
    }

    unsigned int bitfield     = 0;
    unsigned int neighbour_4  = 0;
    unsigned int neighbour_c  = 0;
    unsigned int neighbour_10 = 0;
    unsigned int neighbour_14 = 0;
    bool fault = false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(clientOptions);
        bitfield     = *reinterpret_cast<unsigned int*>(base +
                          kClientOptionsBitFieldOffset);
        neighbour_4  = *reinterpret_cast<unsigned int*>(base + 0x4);
        neighbour_c  = *reinterpret_cast<unsigned int*>(base + 0xc);
        neighbour_10 = *reinterpret_cast<unsigned int*>(base + 0x10);
        neighbour_14 = *reinterpret_cast<unsigned int*>(base + 0x14);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault = true;
    }

    if (fault) {
        acclog::Write("ViewModeProbe", "Shift+B — SEH fault while reading "
            "CClientOptions @%p; field-by-field dump aborted",
            clientOptions);
        return;
    }

    auto bit = [&](unsigned int mask) -> int {
        return (bitfield & mask) != 0 ? 1 : 0;
    };

    acclog::Write("ViewModeProbe", "Shift+B SNAPSHOT options=%p bitfield=0x%08x "
        "[auto_level=%d mouse_look=%d autosave=%d minigame_yaxis=%d "
        "combat_movement=%d undocumented_bits=0x%08x] "
        "neighbours @+0x4=0x%08x @+0xc=0x%08x @+0x10=0x%08x "
        "@+0x14=0x%08x view_mode_active=%d",
        clientOptions, bitfield,
        bit(0x01), bit(kClientOptionsMouseLookMask),
        bit(0x04), bit(0x08), bit(0x10),
        bitfield & ~static_cast<unsigned int>(0x1f),
        neighbour_4, neighbour_c, neighbour_10, neighbour_14,
        g_state.active ? 1 : 0);
}

void StepCursor(float dt) {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    bool w = down('W');
    bool s = down('S');
    bool d = down('D');  // strafe right
    bool a = down('A');  // strafe left
    float forwardSign = (w ? 1.0f : 0.0f) - (s ? 1.0f : 0.0f);
    float strafeSign  = (d ? 1.0f : 0.0f) - (a ? 1.0f : 0.0f);
    if (forwardSign == 0.0f && strafeSign == 0.0f) return;

    // Pre-step yaw refresh from the engine's camera reader; fall back to
    // the frozen heading until camera_announce has anchored.
    float yawDeg = g_state.cursor_yaw_deg;
    float cameraYaw = 0.0f;
    if (acc::camera_announce::TryGetCameraEngineYawDegrees(cameraYaw)) {
        yawDeg = cameraYaw;
    }
    g_state.cursor_yaw_deg = yawDeg;

    float yawRad   = yawDeg * 0.017453292519943295f;
    float forwardX = std::cos(yawRad);
    float forwardY = std::sin(yawRad);
    // Right perpendicular in CCW-positive yaw: rotate forward 90° clockwise.
    float rightX = std::sin(yawRad);
    float rightY = -std::cos(yawRad);

    float dx = forwardX * forwardSign + rightX * strafeSign;
    float dy = forwardY * forwardSign + rightY * strafeSign;
    // Normalise so diagonals don't move 1.41× faster than cardinals.
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag > 1e-6f) {
        dx /= mag;
        dy /= mag;
    }
    float dist = kCursorSpeedMps * dt;

    Vector start = g_state.cursor_pos;
    Vector end   = {
        start.x + dx * dist,
        start.y + dy * dist,
        start.z };

    // Surface-level collision: cursor stops only where the audio would
    // announce a wall, not on portal-seam fragments the audio absorbs.
    Vector hit = end;
    bool collided = acc::spatial::change_detector::SegmentCrossesSurface(
        start, end, hit);

    if (collided) {
        // Back off 5 cm from the hit so next tick's tiny step doesn't read
        // as a fresh crossing.
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-6f) {
            float backoff = 0.05f;
            float hx = hit.x - start.x;
            float hy = hit.y - start.y;
            float t  = std::sqrt(hx * hx + hy * hy);
            float clamped = t - backoff;
            if (clamped < 0.0f) clamped = 0.0f;
            float u = clamped / len;
            g_state.cursor_pos.x = start.x + dx * u;
            g_state.cursor_pos.y = start.y + dy * u;
            // z unchanged — room-flat assumption matches the test.
        } else {
            g_state.cursor_pos = start;  // unreachable given dist > 0 guard
        }

        DWORD now = GetTickCount();
        if (g_state.last_collision_ms == 0 ||
            now - g_state.last_collision_ms > kCollisionCueQuietMs) {
            acc::audio::PlayCue3D(
                acc::audio::GetNavCueResref(acc::audio::NavCue::Wall),
                hit);
            g_state.last_collision_ms = now;
            acclog::Write("ViewMode", "collision at (%.2f,%.2f,%.2f) cursor clamped "
                "to (%.2f,%.2f,%.2f)",
                hit.x, hit.y, hit.z,
                g_state.cursor_pos.x, g_state.cursor_pos.y,
                g_state.cursor_pos.z);
        }
    } else {
        g_state.cursor_pos = end;
    }
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

// Tier order matches the walking adapter: landmark → friendly room name
// → shape cache. False = no tier resolved; caller stays silent.
bool ResolveCursorRegionLabel(void* area, const Vector& cursor,
                              std::string& outBuf,
                              const char*& outSource) {
    outBuf.clear();
    outSource = "none";

    // 15m matches the walking adapter's enter/exit windows.
    constexpr float kLandmarkRangeM = 15.0f;
    {
        char   landmarkBuf[128] = {0};
        Vector landmarkPos;
        if (acc::transitions::FindLandmarkNear(
                cursor, kLandmarkRangeM,
                landmarkBuf, sizeof(landmarkBuf), landmarkPos) &&
            landmarkBuf[0] != '\0') {
            outBuf = landmarkBuf;
            outSource = "landmark";
            return true;
        }
    }

    int roomIdx = -1;
    acc::engine::GetRoomAtIndexed(area, cursor, roomIdx);
    if (roomIdx >= 0) {
        char nameBuf[128] = {0};
        if (acc::engine::GetRoomDisplayName(area, roomIdx,
                                            nameBuf, sizeof(nameBuf)) &&
            nameBuf[0] != '\0' &&
            !acc::transitions::IsResrefStyleRoomName(nameBuf)) {
            outBuf = nameBuf;
            outSource = "room_name";
            return true;
        }
    }

    std::string shapeBuf;
    int  shapeSig   = 0;
    int  clusterId  = acc::wall_topology::kClusterIdNone;
    if (acc::wall_topology::LookupAt(area, cursor,
                                     shapeBuf,
                                     shapeSig, clusterId) &&
        !shapeBuf.empty()) {
        outBuf = shapeBuf;
        outSource = "shape";
        return true;
    }
    return false;
}

// Per-tick region announce. Text-equality dedup + 300ms hover-pause so
// micro-crossings on a room seam don't fire. World-speech-gated.
void AnnounceCursorRegion(void* area, const Vector& cursor) {
    if (!area) {
        g_state.region_pending_text.clear();
        g_state.region_pending_started_ms = 0;
        return;
    }
    if (acc::transitions::IsWorldSpeechGated()) {
        // Hold pending state — when the gate releases, a fresh same-region
        // classification must not spam.
        return;
    }

    std::string label;
    const char* source = "none";
    bool resolved = ResolveCursorRegionLabel(area, cursor, label, source);
    if (!resolved || label.empty()) {
        g_state.region_pending_text.clear();
        g_state.region_pending_started_ms = 0;
        return;
    }

    if (label == g_state.region_last_spoken_text) {
        // Already announced — silent until a differently-labelled region.
        g_state.region_pending_text.clear();
        g_state.region_pending_started_ms = 0;
        return;
    }

    DWORD now = GetTickCount();
    if (label == g_state.region_pending_text &&
        g_state.region_pending_started_ms != 0) {
        if (now - g_state.region_pending_started_ms >= kHoverPauseMs) {
            prism::SpeakUrgent(label.c_str());
            g_state.region_last_spoken_text = label;
            g_state.region_pending_text.clear();
            g_state.region_pending_started_ms = 0;
            acclog::Write("ViewMode",
                          "region speak src=%s text=\"%s\" cursor=(%.2f,%.2f,%.2f)",
                          source, label.c_str(),
                          cursor.x, cursor.y, cursor.z);
        }
        return;
    }

    // New pending region — arm the timer.
    g_state.region_pending_text = label;
    g_state.region_pending_started_ms = now;
}

void NarrateNearestObject(void* area, const Vector& cursor) {
    if (!area) {
        // Reset on area drop — stale handle would compare-equal across
        // transitions; stale obj would dangle into freed memory.
        g_state.hover_pending         = 0;
        g_state.hover_pending_obj     = nullptr;
        g_state.hover_pending_started = 0;
        return;
    }

    float radiusSq = kHoverRadiusMeters * kHoverRadiusMeters;
    float bestDistSq = radiusSq + 1.0f;
    void* bestObj = nullptr;
    uint32_t bestHandle = 0;
    acc::filter::CycleCategory bestCat =
        acc::filter::CycleCategory::Count_;

    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        acc::filter::CycleCategory cat =
            acc::filter::CycleCategory::Count_;
        bool matched = false;
        for (int c = 0; c < int(acc::filter::CycleCategory::Count_); ++c) {
            auto cc = static_cast<acc::filter::CycleCategory>(c);
            if (acc::filter::ObjectMatches(obj, cc)) {
                cat = cc;
                matched = true;
                break;
            }
        }
        if (!matched) continue;

        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;
        float dx = pos.x - cursor.x;
        float dy = pos.y - cursor.y;
        float dz = pos.z - cursor.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > radiusSq) continue;
        if (distSq >= bestDistSq) continue;

        bestDistSq = distSq;
        bestObj    = obj;
        bestHandle = acc::engine::GetObjectHandle(obj);
        bestCat    = cat;
    }

    DWORD now = GetTickCount();

    // Reset the pending timer whenever the nearest changes — rapid sweeps
    // keep resetting so we stay silent until the cursor actually settles.
    if (bestHandle != g_state.hover_pending) {
        g_state.hover_pending         = bestHandle;
        g_state.hover_pending_obj     = bestObj;
        g_state.hover_pending_started = now;
    }

    if (bestHandle == 0) return;
    if (bestHandle == g_state.hover_last_spoken) return;
    if (now - g_state.hover_pending_started < kHoverPauseMs) return;

    char name[128] = "";
    if (!acc::engine::GetObjectName(bestObj, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(CategoryNameId(bestCat)));
    }

    prism::Speak(name, /*interrupt=*/true);
    g_state.hover_last_spoken = bestHandle;

    // Unified activation slot — Enter / Shift+Enter / Shift+- / Ctrl+- /
    // `-` all act on whatever the user just heard.
    acc::narrated_target::Stamp(bestObj, bestHandle);

    Vector pos = { 0.0f, 0.0f, 0.0f };
    acc::engine::GetObjectPosition(bestObj, pos);
    acclog::Write("ViewMode", "hover narrate handle=0x%08x cat=%s name=[%s] "
        "dist=%.2f cursor=(%.2f,%.2f,%.2f) obj=(%.2f,%.2f,%.2f)",
        bestHandle, acc::filter::CategoryName(bestCat), name,
        std::sqrt(bestDistSq),
        cursor.x, cursor.y, cursor.z,
        pos.x, pos.y, pos.z);
}

// Enter / Shift+Enter dispatch while view mode is active.
//   hover target present → DispatchInteract(obj, handle, forceRadial)
//   no hover target      → speak pre-roll + WalkTo(cursor_pos)
// View mode exits synchronously before dispatch (autowalk needs an
// unfrozen character). Actual dispatch is deferred via g_pending — see
// PendingDispatch comment for the engine-state reason.
void PollEnter() {
    bool risingPlain  = acc::hotkeys::Pressed(
                            acc::hotkeys::Action::InteractTarget);
    bool risingForce  = acc::hotkeys::Pressed(
                            acc::hotkeys::Action::InteractForceRadial);
    if (!risingPlain && !risingForce) return;

    // Claim this tick's Enter before exiting view mode; interact_hotkey
    // runs later in the same OnUpdate and reads-and-clears this flag.
    g_enter_consumed_this_tick = true;

    bool forceRadial = risingForce;
    const char* keyTag = forceRadial ? "Shift+Enter" : "Enter";

    // Snapshot only view-mode-local state. Hover obj/handle live in the
    // unified narrated_target slot — ProcessPendingDispatch reads them
    // from there at dispatch time.
    Vector cursor_pos = g_state.cursor_pos;
    bool   hasHover   = g_state.hover_pending_obj != nullptr &&
                        g_state.hover_pending != 0;

    g_state.active = false;
    bool restored = acc::engine::SetPlayerInputEnabled(true);
    acclog::Write("ViewMode", "EXIT (deferred dispatch) input_restored=%d hasHover=%d",
        restored ? 1 : 0, hasHover ? 1 : 0);

    g_pending.active        = true;
    g_pending.hasHover      = hasHover;
    g_pending.cursor_pos    = cursor_pos;
    g_pending.forceRadial   = forceRadial;
    g_pending.armed_at_ms   = GetTickCount();

    acclog::Write("ViewMode", "%s -> dispatch armed for next tick "
        "(hasHover=%d cursor=(%.2f,%.2f,%.2f) forceRadial=%d)",
        keyTag, hasHover ? 1 : 0,
        cursor_pos.x, cursor_pos.y, cursor_pos.z,
        forceRadial ? 1 : 0);
}

// Runs at the top of Tick BEFORE the IsActive() gate — by design, this
// fires after PollEnter exited view mode synchronously.
void ProcessPendingDispatch() {
    if (!g_pending.active) return;

    DWORD elapsed = GetTickCount() - g_pending.armed_at_ms;
    if (elapsed < kPendingDispatchMinElapsedMs) return;

    // Snapshot and clear before dispatch — the dispatch path may re-enter
    // our hooks; re-entry must not see g_pending as active.
    bool     hasHover    = g_pending.hasHover;
    Vector   cursor_pos  = g_pending.cursor_pos;
    bool     forceRadial = g_pending.forceRadial;
    g_pending.active = false;

    const char* keyTag = forceRadial ? "Shift+Enter" : "Enter";

    if (hasHover) {
        // Read the hover target live: a passive_narrate stamp between
        // PollEnter and now wins — "last narrated wins". Stale slot (rare,
        // object destroyed within the one-tick gap) speaks GuidanceNoFocus
        // instead of dispatching against a zombie pointer.
        acc::narrated_target::Slot slot;
        if (!acc::narrated_target::TryGet(slot)) {
            const char* msg = acc::strings::Get(
                acc::strings::Id::GuidanceNoFocus);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("ViewMode", "%s deferred -> [%s] (hover stale at dispatch) "
                "elapsed=%lums",
                keyTag, msg, static_cast<unsigned long>(elapsed));
            return;
        }
        acclog::Write("ViewMode", "%s deferred -> DispatchInteract obj=%p handle=0x%08x "
            "elapsed=%lums (narrated target)",
            keyTag, slot.obj, slot.handle,
            static_cast<unsigned long>(elapsed));
        acc::interact::DispatchInteract(slot.obj, slot.handle, forceRadial);
        return;
    }

    // Empty cursor — pre-roll then ForceWalkTo (queue-bypass). Plain
    // WalkTo's queue path stays asleep after view mode's sustained
    // SetEnabled(false); ForceMoveToPoint dispatches directly.
    const char* preroll = acc::strings::Get(acc::strings::Id::GuidingToPoint);
    prism::Speak(preroll, /*interrupt=*/true);

    bool ok = acc::guidance::ForceWalkTo(cursor_pos);
    acclog::Write("ViewMode", "%s deferred -> ForceWalkTo cursor=(%.2f,%.2f,%.2f) ok=%d "
        "elapsed=%lums (no hover target)",
        keyTag, cursor_pos.x, cursor_pos.y, cursor_pos.z, ok ? 1 : 0,
        static_cast<unsigned long>(elapsed));
}

}  // namespace

bool IsActive() { return g_state.active; }

bool ConsumedEnterThisTick() {
    bool was = g_enter_consumed_this_tick;
    g_enter_consumed_this_tick = false;
    return was;
}

bool TryGetCursorPosition(Vector& out) {
    if (!g_state.active) return false;
    out = g_state.cursor_pos;
    return true;
}

bool GetEffectiveOrientationYawDegrees(float& out) {
    // T2's front cone tracks where the user is *looking* — the camera
    // facing — in BOTH normal play and view mode. The camera is what the
    // user sweeps when scanning for walls/openings: in third-person the
    // character often stays put while the camera orbits (confirmed in
    // logs — the compass swept full circles while character yaw barely
    // moved). Anchoring the cone to character yaw made turning-in-place
    // produce no cone movement and no cues. Mirrors camera_announce,
    // which uses camera yaw for exactly this reason.
    float cameraYaw = 0.0f;
    if (acc::camera_announce::TryGetCameraEngineYawDegrees(cameraYaw)) {
        out = cameraYaw;
        return true;
    }
    // Camera pose not anchored yet (first ticks before camera_announce::
    // Tick() ran its anchoring pass, or camera unavailable): fall back to
    // character yaw for that tick, then switch to camera yaw.
    return acc::engine::GetPlayerYawDegrees(out);
}

void PollWin32() {
    bool risingAlone = acc::hotkeys::Pressed(
                           acc::hotkeys::Action::ViewModeToggle);
    bool risingShift = acc::hotkeys::Pressed(
                           acc::hotkeys::Action::CameraStateProbe);
    if (!risingAlone && !risingShift) return;
    bool shift = risingShift;

    // UI-claim gate (stricter than interact_hotkey's Enter gate). Toggling
    // view mode flips engine input state; doing it while any UI claims
    // input corrupts the engine gate (post-modal-close movement lockup).
    // Scan panels[] for dialog + sub-screens (stale-Fade defeats fg-only
    // checks), modal_stack, and a foreground-kind blacklist.
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    const char* blockReason = nullptr;
    if (acc::engine::HasActiveDialogPanel()) {
        blockReason = "dialog panel in stack";
    } else if (acc::engine::HasActiveSubScreen()) {
        blockReason = "sub-screen drilled";
    } else if (mgr) {
        auto* mgrBase = reinterpret_cast<unsigned char*>(mgr);
        int modalSize = *reinterpret_cast<int*>(
            mgrBase + kMgrModalStackSizeOffset);
        if (modalSize > 0) {
            blockReason = "modal popup up";
        } else {
            void* fgPanel = acc::engine::GetForegroundPanel(mgr);
            if (fgPanel) {
                switch (acc::engine::IdentifyPanel(fgPanel)) {
                case acc::engine::PanelKind::Container:
                case acc::engine::PanelKind::Store:
                case acc::engine::PanelKind::Examine:
                case acc::engine::PanelKind::TutorialBox:
                case acc::engine::PanelKind::MessageBoxModal:
                case acc::engine::PanelKind::StatusSummary:
                case acc::engine::PanelKind::SkillInfoBox:
                case acc::engine::PanelKind::ControllerLossBox:
                case acc::engine::PanelKind::SoloModeQuery:
                case acc::engine::PanelKind::PartySelection:
                case acc::engine::PanelKind::AreaTransition:
                case acc::engine::PanelKind::InGameMenu:
                    blockReason = "UI panel foreground";
                    break;
                default:
                    break;
                }
            }
        }
    }
    if (blockReason) {
        acclog::Write("ViewMode", "B (shift=%d) blocked — %s",
                      shift ? 1 : 0, blockReason);
        return;
    }

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write("ViewMode", "B (shift=%d) fired without player loaded; "
            "skipping", shift ? 1 : 0);
        return;
    }

    if (risingShift) {
        DumpCameraStateProbe();
        return;
    }

    if (risingAlone) {
        ToggleViewMode();
    }
}

void Tick() {
    // Before the active-gate — dispatch fires after view mode has exited.
    ProcessPendingDispatch();

    if (!g_state.active) return;

    // Foreground gate: don't consume W/S when the user is alt-tabbed out.
    // The listener detour keeps anchoring to cursor_pos either way, so the
    // soundscape stays put.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    DWORD now = GetTickCount();
    DWORD elapsedMs = now - g_state.last_tick_ms;
    g_state.last_tick_ms = now;
    float dt = static_cast<float>(elapsedMs) * 0.001f;
    if (dt > kMaxDtSec) dt = kMaxDtSec;
    if (dt < 0.0f)      dt = 0.0f;

    StepCursor(dt);

    void* area = acc::engine::GetCurrentArea();
    NarrateNearestObject(area, g_state.cursor_pos);
    // After Narrate so an object directly under the cursor still wins the
    // audio bus; both can fire in the same tick on independent completions.
    AnnounceCursorRegion(area, g_state.cursor_pos);
    // After Narrate so Enter reads the hover state computed this tick.
    PollEnter();
}

}  // namespace acc::view_mode
