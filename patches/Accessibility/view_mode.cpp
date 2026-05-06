#include "view_mode.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")

#include "audio_bus.h"
#include "audio_cues.h"           // NavCue::Wall
#include "camera_announce.h"      // dead-reckoned camera yaw for view-mode
                                  // override of cue-system orientation
#include "engine_area.h"          // AreaObjectIterator, GetCurrentArea,
                                  // GetObjectName, GetObjectPosition,
                                  // SegmentCrossesWalkmesh
#include "engine_options.h"
#include "engine_offsets.h"       // Vector
#include "engine_player.h"
#include "filter_objects.h"       // ObjectMatches
#include "log.h"
#include "spatial_change_detector.h"  // GetCachedWalls
#include "strings.h"
#include "tolk.h"

namespace acc::view_mode {

namespace {

// Cursor motion speed — matches KOTOR's walk pace (≈2.0 m/s). Single
// knob; can move to core_settings later if a separate map-cursor speed
// becomes a thing.
constexpr float kCursorSpeedMps = 2.0f;

// dt cap to prevent teleport-on-stall. Frame stalls (loading screens,
// alt-tab) shouldn't translate into a 30-metre cursor jump on the next
// tick that lands inside a wall.
constexpr float kMaxDtSec = 0.1f;

// Hover-pause object narration debounce. 300 ms matches the long-term
// plan §"Mechanics — view mode": the cursor must rest on an object
// before its name is spoken, so sweeping past objects doesn't fire a
// machine-gun stream of names. Mirrors `turn_announce`'s three-variable
// last_fired / pending / pending_changed_at pattern.
constexpr DWORD kHoverPauseMs = 300;

// Object-narration radius. Plan calls for ~1.0 m so the cursor must
// physically be on top of the object before it speaks; with the
// listener centred at the cursor this is also roughly where the user
// hears the object as "in front of them".
constexpr float kHoverRadiusMeters = 1.0f;

// Wall-collision cue debounce. Driving the cursor straight into a wall
// would otherwise fire a NavCue::Wall every tick — at ~50ms-per-tick
// that's 20 cues/sec into the same ear. 250 ms collapses sustained
// contact to ~4 cues/sec; first contact still fires immediately.
constexpr DWORD kCollisionCueQuietMs = 250;

struct ViewModeState {
    bool   active            = false;
    Vector cursor_pos        = { 0.0f, 0.0f, 0.0f };
    float  cursor_yaw_deg    = 0.0f;   // engine frame, 0° = +X = East,
                                       // CCW positive
    DWORD  last_tick_ms      = 0;       // GetTickCount() last Tick() call
    DWORD  last_collision_ms = 0;       // 0 = no collision yet this session
    // Hover-pause object debounce.
    uint32_t hover_last_spoken     = 0;  // 0 = none spoken this session
    uint32_t hover_pending         = 0;  // 0 = cone clear / no candidate
    DWORD    hover_pending_started = 0;
};

ViewModeState g_state;

// Throwaway listener-override probe (see plan step 1). On the first
// Tick() each second writes a sentinel listener position; the next
// Tick() reads it back to verify the engine doesn't stomp the field
// between our write and the next frame's render. Strip before
// committing once the probe has answered.
//
// 2026-05-06b plan: writes (999, 999, 999), reads it back, logs
// `survived=1` if the field still holds the sentinel — that means our
// per-tick override is sufficient. `survived=0` would force us to
// hook the camera-driven write site instead.
constexpr bool kListenerProbeEnabled = true;
DWORD g_probe_last_emit_ms = 0;
bool  g_probe_pending      = false;
Vector g_probe_sentinel    = { 999.0f, 999.0f, 999.0f };

// Why no Mouse Look forcing / cursor recentring:
//
// User-verified 2026-05-06: in stock KOTOR (regardless of Caps Lock /
// Mouse Look state), A/D rotates the camera only — the character does
// not turn. The character only "snaps" to camera direction at the
// instant W or S is pressed to commit forward motion. So the engine
// already has a native "look around without rotating character"
// primitive — A/D is it. View mode just needs to suppress the
// W/S-driven snap-and-walk so the camera-pan persists.
//
// `SetPlayerInputEnabled(false)` (CSWPlayerControl::SetEnabled @0x6792e0
// per memory `project_player_control_toggle.md`) gates the per-tick
// movement clobber that drives W/S character motion. With it off,
// W/S key presses don't translate the character; A/D still pans the
// camera (camera-pan lives outside the movement-clobber path).
//
// The Phase 4 lay-off 2 probe verified Mouse Look ON makes mouse motion
// drive the camera, but that's a separate primitive — we're using key
// input directly through the engine's existing A/D camera-rotate path,
// not synthesising mouse deltas. So no Mouse Look state to manage,
// no SendInput, no cursor recentring.

void EnterViewMode() {
    // Sustained-disable (armAutoRestore=false): view mode lasts until
    // the user toggles back. Default armAutoRestore=true would auto-
    // restore after 3s — verified 2026-05-06 in patch-20260506-113051.log
    // line 41+44 (W/S regained walkability mid-session).
    if (!acc::engine::SetPlayerInputEnabled(false, /*armAutoRestore=*/false)) {
        acclog::Write(
            "ViewMode: enter REFUSED — SetPlayerInputEnabled(false) "
            "failed (chain unresolved or SEH); skipping toggle");
        return;
    }

    // Initialise the cursor at the player's current position + facing.
    // GetPlayerPosition was already non-null at PollWin32 gate, so a
    // failure here is a chain teardown between the gate and now —
    // bail without entering rather than starting the cursor at (0,0,0).
    Vector pos;
    if (!acc::engine::GetPlayerPosition(pos)) {
        acc::engine::SetPlayerInputEnabled(true);  // undo the disable
        acclog::Write(
            "ViewMode: enter REFUSED — player position unavailable "
            "post-disable; rolled back");
        return;
    }
    g_state.cursor_pos = pos;
    float yaw = 0.0f;
    if (!acc::engine::GetPlayerYawDegrees(yaw)) {
        // Degenerate facing during spawn — start facing +X. First A/D
        // pan or W/S step rebases on the camera yaw read on next tick.
        yaw = 0.0f;
    }
    g_state.cursor_yaw_deg     = yaw;
    g_state.last_tick_ms       = GetTickCount();
    g_state.last_collision_ms  = 0;
    g_state.hover_last_spoken     = 0;
    g_state.hover_pending         = 0;
    g_state.hover_pending_started = 0;

    g_state.active = true;
    tolk::Speak(acc::strings::Get(acc::strings::Id::ViewModeOn),
                /*interrupt=*/true);
    acclog::Write(
        "ViewMode: ENTER cursor=(%.2f,%.2f,%.2f) yaw=%.1f",
        pos.x, pos.y, pos.z, yaw);
}

void ExitViewMode() {
    bool restored = acc::engine::SetPlayerInputEnabled(true);
    g_state.active = false;
    // Don't write the listener on the way out — the engine reclaims the
    // field next frame from the camera, and any further write here
    // would race that update.
    tolk::Speak(acc::strings::Get(acc::strings::Id::ViewModeOff),
                /*interrupt=*/true);
    acclog::Write("ViewMode: EXIT restored=%d", restored ? 1 : 0);
}

void ToggleViewMode() {
    if (g_state.active) ExitViewMode();
    else                EnterViewMode();
}

// Camera-behavior probe (Shift+B). Snapshot the documented chain pointers
// + every byte we currently know about that could plausibly hold Free
// Look / Look About state.
//
// 2026-05-06 outcome: probe + manual Caps Lock test showed no
// CClientOptions bits change in response to Caps Lock. User-blind audio
// test (AltGr heading + A/D press) further confirmed Caps Lock has no
// audible effect — A/D pans camera-only in *both* Caps Lock states.
// Free Look in K1 is therefore either cut, visual-only, or accessed
// through a chain we haven't located yet (Camera::SetBehavior @0x45c230).
// The probe stays in the build for now as a diagnostic — it's cheap and
// might catch other state changes in unrelated future RE work.
void DumpCameraStateProbe() {
    void* clientOptions = acc::engine::GetClientOptions();
    if (!clientOptions) {
        acclog::Write(
            "ViewModeProbe: Shift+B — GetClientOptions returned null "
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
        acclog::Write(
            "ViewModeProbe: Shift+B — SEH fault while reading "
            "CClientOptions @%p; field-by-field dump aborted",
            clientOptions);
        return;
    }

    auto bit = [&](unsigned int mask) -> int {
        return (bitfield & mask) != 0 ? 1 : 0;
    };

    acclog::Write(
        "ViewModeProbe: Shift+B SNAPSHOT options=%p bitfield=0x%08x "
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

// Probe step. Writes (999,999,999) once a second; the *next* Tick() call
// reads the listener back to see whether the engine stomped it. Logs
// the survived/stomped result and the engine value when stomped.
//
// Sequencing: this runs at the *start* of Tick() — so the read it
// performs sees what the engine did between the previous Tick()'s
// listener write (whether sentinel or normal cursor pos) and now,
// which is exactly the question we need answered. We then write the
// real cursor-listener at the end of Tick() (or sentinel if we just
// re-armed the probe). Once we know the answer, the entire probe
// block is stripped and `audio_bus::GetListener` can be removed.
void TickListenerProbe() {
    if (!kListenerProbeEnabled) return;

    DWORD now = GetTickCount();

    if (g_probe_pending) {
        Vector readBack;
        if (acc::audio::GetListener(readBack)) {
            bool survived =
                std::fabs(readBack.x - g_probe_sentinel.x) < 0.5f &&
                std::fabs(readBack.y - g_probe_sentinel.y) < 0.5f &&
                std::fabs(readBack.z - g_probe_sentinel.z) < 0.5f;
            acclog::Write(
                "ViewModeProbe: listener survived=%d engine=(%.2f,%.2f,%.2f) "
                "sentinel=(%.2f,%.2f,%.2f)",
                survived ? 1 : 0,
                readBack.x, readBack.y, readBack.z,
                g_probe_sentinel.x, g_probe_sentinel.y, g_probe_sentinel.z);
        } else {
            acclog::Write(
                "ViewModeProbe: listener read failed (singleton/internal "
                "null); cannot decide");
        }
        g_probe_pending = false;
    }

    if (now - g_probe_last_emit_ms >= 1000) {
        if (acc::audio::SetListener(g_probe_sentinel)) {
            g_probe_pending      = true;
            g_probe_last_emit_ms = now;
            acclog::Write(
                "ViewModeProbe: listener sentinel written "
                "(%.2f,%.2f,%.2f); will read back next tick",
                g_probe_sentinel.x, g_probe_sentinel.y, g_probe_sentinel.z);
        }
    }
}

void StepCursor(float dt) {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    bool w = down('W');
    bool s = down('S');
    if (w == s) return;  // both / neither held → no translation

    // Update yaw from the engine's camera-direction reader before
    // stepping. If camera_announce hasn't anchored yet, fall back to
    // the player's frozen heading (one tick of staleness is fine).
    float yawDeg = g_state.cursor_yaw_deg;
    float cameraYaw = 0.0f;
    if (acc::camera_announce::TryGetCameraEngineYawDegrees(cameraYaw)) {
        yawDeg = cameraYaw;
    }
    g_state.cursor_yaw_deg = yawDeg;

    float yawRad   = yawDeg * 0.017453292519943295f;
    float forwardX = std::cos(yawRad);
    float forwardY = std::sin(yawRad);
    float sign     = w ? +1.0f : -1.0f;
    float dist     = kCursorSpeedMps * dt * sign;

    Vector start = g_state.cursor_pos;
    Vector end   = {
        start.x + forwardX * dist,
        start.y + forwardY * dist,
        start.z };

    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    Vector hit = end;
    bool collided = false;
    if (acc::spatial::change_detector::GetCachedWalls(walls, wallCount) &&
        walls && wallCount > 0) {
        collided = acc::engine::SegmentCrossesWalkmesh(
            walls, wallCount, start, end, hit);
    }

    if (collided) {
        // Step the cursor 5 cm short of the hit point along start→end so
        // we don't sit exactly on the wall (which next tick's segment
        // test would detect again as a fresh crossing on any tiny step).
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
            // z unchanged — room-flat assumption, matches the test.
        } else {
            g_state.cursor_pos = start;  // shouldn't happen given the
                                         // dist != 0 guard above
        }

        DWORD now = GetTickCount();
        if (g_state.last_collision_ms == 0 ||
            now - g_state.last_collision_ms > kCollisionCueQuietMs) {
            acc::audio::PlayCue3D(
                acc::audio::GetNavCueResref(acc::audio::NavCue::Wall),
                hit);
            g_state.last_collision_ms = now;
            acclog::Write(
                "ViewMode: collision at (%.2f,%.2f,%.2f) cursor clamped "
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

void NarrateNearestObject(void* area, const Vector& cursor) {
    if (!area) {
        // Reset hover state when area drops; otherwise a stale handle
        // would compare-equal across area transitions.
        g_state.hover_pending         = 0;
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

    // Track most-recent-observed nearest + when it last changed. Mirrors
    // turn_announce's pending pattern: any rapid sweep keeps changing
    // pending so the stable-window timer keeps resetting and we stay
    // silent until the cursor actually settles.
    if (bestHandle != g_state.hover_pending) {
        g_state.hover_pending         = bestHandle;
        g_state.hover_pending_started = now;
    }

    if (bestHandle == 0) return;                         // nothing in range
    if (bestHandle == g_state.hover_last_spoken) return;  // already spoken
    if (now - g_state.hover_pending_started < kHoverPauseMs) return;

    char name[128] = "";
    if (!acc::engine::GetObjectName(bestObj, name, sizeof(name)) ||
        name[0] == '\0') {
        std::snprintf(name, sizeof(name), "%s",
                      acc::strings::Get(CategoryNameId(bestCat)));
    }

    tolk::Speak(name, /*interrupt=*/true);
    g_state.hover_last_spoken = bestHandle;

    Vector pos = { 0.0f, 0.0f, 0.0f };
    acc::engine::GetObjectPosition(bestObj, pos);
    acclog::Write(
        "ViewMode: hover narrate handle=0x%08x cat=%s name=[%s] "
        "dist=%.2f cursor=(%.2f,%.2f,%.2f) obj=(%.2f,%.2f,%.2f)",
        bestHandle, acc::filter::CategoryName(bestCat), name,
        std::sqrt(bestDistSq),
        cursor.x, cursor.y, cursor.z,
        pos.x, pos.y, pos.z);
}

}  // namespace

bool IsActive() { return g_state.active; }

bool GetEffectiveOrientationYawDegrees(float& out) {
    if (g_state.active) {
        float cameraYaw = 0.0f;
        if (acc::camera_announce::TryGetCameraEngineYawDegrees(cameraYaw)) {
            out = cameraYaw;
            return true;
        }
        // Fall through to player yaw — happens on the very first ticks
        // of view mode if the user toggled in before camera_announce::
        // Tick() ran a single anchoring pass. Acceptable: T2 cone uses
        // character yaw for one tick, then switches to camera yaw.
    }
    return acc::engine::GetPlayerYawDegrees(out);
}

void PollWin32() {
    auto down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    bool b     = down('B');
    bool shift = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);

    static bool s_prevAlone = false;
    static bool s_prevShift = false;

    bool nowAlone = b && !shift;
    bool nowShift = b &&  shift;

    bool risingAlone = nowAlone && !s_prevAlone;
    bool risingShift = nowShift && !s_prevShift;
    s_prevAlone = nowAlone;
    s_prevShift = nowShift;

    if (!risingAlone && !risingShift) return;

    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
    }

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        acclog::Write(
            "ViewMode: B (shift=%d) fired without player loaded; "
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
    if (!g_state.active) return;

    // Foreground gate — same rationale as the cycle / view-mode pollers:
    // don't consume W/S held state when the user is alt-tabbed into a
    // browser typing about the bug they just hit.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) {
            // Don't translate, but still write the listener so the
            // soundscape stays anchored to the cursor.
            acc::audio::SetListener(g_state.cursor_pos);
            return;
        }
    }

    // Step 1 (probe): read back the previous tick's sentinel write
    // before we issue any new listener writes this tick. Strip once
    // the probe answer lands in logs.
    TickListenerProbe();

    DWORD now = GetTickCount();
    DWORD elapsedMs = now - g_state.last_tick_ms;
    g_state.last_tick_ms = now;
    float dt = static_cast<float>(elapsedMs) * 0.001f;
    if (dt > kMaxDtSec) dt = kMaxDtSec;
    if (dt < 0.0f)      dt = 0.0f;

    StepCursor(dt);

    void* area = acc::engine::GetCurrentArea();
    NarrateNearestObject(area, g_state.cursor_pos);

    // Listener override — last write of the tick wins. While the probe
    // is enabled and pending, the sentinel write happens here so the
    // *next* tick's read can verify it survived. Once the probe is
    // stripped this branch becomes the only listener write.
    if (kListenerProbeEnabled && g_probe_pending) {
        // TickListenerProbe has already issued the sentinel write this
        // tick; leave it in place so the engine has a full frame to
        // potentially overwrite it before we read back.
    } else {
        acc::audio::SetListener(g_state.cursor_pos);
    }
}

}  // namespace acc::view_mode
