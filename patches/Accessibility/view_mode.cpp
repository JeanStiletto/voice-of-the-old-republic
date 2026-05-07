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
#include "engine_manager.h"       // kAddrGuiManagerPtr, kMgrModalStack*Offset,
                                  // GetForegroundPanel
#include "engine_panels.h"        // HasActiveDialogPanel, HasActiveSubScreen,
                                  // IdentifyPanel, PanelKind
                                  // GetObjectName, GetObjectPosition,
                                  // SegmentCrossesWalkmesh
#include "engine_options.h"
#include "engine_offsets.h"       // Vector
#include "engine_player.h"
#include "filter_objects.h"       // ObjectMatches
#include "guidance_autowalk.h"    // WalkTo — empty-cursor Enter target
#include "interact_hotkey.h"      // DispatchInteract — Enter on hover target
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
    void*    hover_pending_obj     = nullptr;  // CSWSObject* matching
                                               // hover_pending; captured
                                               // alongside the handle so
                                               // lay-off-5 Enter dispatch
                                               // can hand it to
                                               // DispatchInteract without
                                               // re-resolving.
    DWORD    hover_pending_started = 0;
};

ViewModeState g_state;

// Lay-off-5 single-tick Enter ownership flag. Set by `PollEnter` whenever
// it handles a rising VK_RETURN; read-and-cleared by
// `acc::view_mode::ConsumedEnterThisTick()` from
// `interact_hotkey::PollHotkey` so the same press can't be processed
// twice in one tick. See header doc on the public getter for the why.
bool g_enter_consumed_this_tick = false;

// Lay-off-5 deferred-dispatch state. View-mode-exit autowalk via
// AddMoveToPointAction silently no-ops when SetPlayerInputEnabled has
// been held at 0 for many ticks (verified patch-20260507-064544.log:
// `WalkTo dispatch ret=0x00000001` but `moved=0.00m` at t+3s for every
// dispatch). The cycle_input Shift+- path works because input has been
// settled at 1 for many ticks before WalkTo flips it to 0 — the engine
// needs that 1→0 transition immediately before AI dispatch.
//
// Synchronous round-trip (SetEnabled(true) then SetEnabled(false) in
// the same tick) didn't work either: the engine never actually
// processed the `enabled=1` state. So we exit view mode + re-enable
// input synchronously, arm this pending-dispatch struct, and process
// it on the *next* OnUpdate tick after the engine has had a frame
// to settle into `enabled=1` and we then transition cleanly to 0
// inside the dispatch path.
struct PendingDispatch {
    bool     active        = false;
    bool     hasHover      = false;
    void*    hover_obj     = nullptr;
    uint32_t hover_handle  = 0;
    Vector   cursor_pos    = {0.0f, 0.0f, 0.0f};
    bool     forceRadial   = false;
    DWORD    armed_at_ms   = 0;   // GetTickCount() when armed
};
PendingDispatch g_pending;

// Min elapsed time before processing a pending dispatch. ~1 frame at
// 60fps; ensures at least one engine tick processes the input
// re-enable before we dispatch the AI action. Concrete tick rate
// varies; this is a lower bound rather than a fixed delay.
constexpr DWORD kPendingDispatchMinElapsedMs = 16;

// Listener override path (post-rework, 2026-05-06):
//
// Lay-off 4 first attempt wrote `CExoSound::SetListenerPosition` from
// our OnUpdate `view_mode::Tick` every tick. The probe (sentinel
// `(999,999,999)` written, read back next tick) consistently logged
// `survived=0`: the engine's own per-frame camera-driven listener write
// in `CClientExoAppInternal::UpdateSoundEngine` runs *after* our
// OnUpdate callsite and overwrites the field before any 3D-audio render
// can see our value.
//
// The fix is structural: we detour `CExoSound::SetListenerPosition`
// itself (single xref from UpdateSoundEngine; see hooks.toml entry).
// `OnSetListenerPosition` substitutes the cursor position for the
// engine's Vector when view mode is active and dispatches the inner
// `CExoSoundInternal::SetListenerPosition` directly. The engine's
// per-frame call now acts as our heartbeat — every camera-driven write
// becomes a cursor-driven write while view mode is active. No
// additional per-tick listener call needed from this file.
//
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
    g_state.hover_pending_obj     = nullptr;
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

// (ExitViewModeQuiet was inlined into PollEnter as part of the lay-off-5
// deferred-dispatch rework — exit lifecycle is now coupled to the
// dispatch arming rather than being a standalone helper.)

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
        // would compare-equal across area transitions, and a stale obj
        // pointer would dangle into a freed CSWSObject.
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

    // Track most-recent-observed nearest + when it last changed. Mirrors
    // turn_announce's pending pattern: any rapid sweep keeps changing
    // pending so the stable-window timer keeps resetting and we stay
    // silent until the cursor actually settles.
    //
    // Capture both the handle (for change detection + DispatchInteract's
    // engine-side seeding) and the CSWSObject* (for DispatchInteract's
    // name resolution + classification). Both are written together so
    // they never go out of sync — Enter dispatch reads them as a pair.
    if (bestHandle != g_state.hover_pending) {
        g_state.hover_pending         = bestHandle;
        g_state.hover_pending_obj     = bestObj;
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

// Lay-off 5 — Enter / Shift+Enter dispatch while view mode is active.
// Edge-detected on VK_RETURN; when it fires:
//
//   - hover target present (cursor's hover-pause tracker has a non-zero
//     handle + obj) → exit view mode quietly, then call
//     `acc::interact::DispatchInteract(obj, handle, forceRadial)`. Same
//     engine-action-picker pipeline Enter / Shift+Enter run outside
//     view mode, so behaviour ("does pressing Enter on a door open
//     it?") is identical between the two modes. forceRadial=true
//     (Shift+Enter) opens the radial action menu so the user can pick
//     from all available actions instead of the engine's default —
//     matches outside-view-mode Shift+Enter semantics.
//
//   - no hover target → exit view mode quietly, speak "Walking to point"
//     / "Gehe zum Punkt", dispatch `acc::guidance::WalkTo(cursor_pos)`.
//     Same path Shift+- uses for the cycle's focused-object case but
//     without a target name. Shift+Enter behaves identically here
//     (no radial to open with no target).
//
// Lifecycle: exit happens BEFORE dispatch so the autowalk runs against
// an unfrozen character (decision (a) from the lay-off 5 plan). Use
// the no-announce ExitViewModeQuiet so the dispatch announce isn't
// preempted by a "View mode off" interrupt.
//
// Foreground gate is inherited from the caller (Tick) — Enter polled
// here only fires on the same tick Tick() decided to run.
//
// Coordination with `interact_hotkey::PollHotkey`: PollHotkey gates its
// own Enter branch on `!view_mode::IsActive()` so the same VK_RETURN
// rising edge can't double-dispatch via both paths.
void PollEnter() {
    static bool s_prevEnter = false;
    bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    bool risingEnter = enter && !s_prevEnter;
    s_prevEnter = enter;
    if (!risingEnter) return;

    // Claim this tick's Enter press before doing any work that exits
    // view mode — interact_hotkey::PollHotkey runs later in the same
    // OnUpdate and reads-and-clears this flag to skip its own Enter
    // branch, preventing the double-dispatch verified in
    // patch-20260506-142103.log.
    g_enter_consumed_this_tick = true;

    bool forceRadial = ((GetAsyncKeyState(VK_SHIFT)  & 0x8000) != 0) ||
                       ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) ||
                       ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0);
    const char* keyTag = forceRadial ? "Shift+Enter" : "Enter";

    // Snapshot hover + cursor — these are about to leave the live
    // ViewModeState as we exit view mode, but we need them when the
    // deferred dispatch fires next tick.
    Vector   cursor_pos   = g_state.cursor_pos;
    void*    hover_obj    = g_state.hover_pending_obj;
    uint32_t hover_handle = g_state.hover_pending;
    bool     hasHover     = hover_obj != nullptr && hover_handle != 0;

    // Exit view mode + re-enable player input synchronously. The actual
    // AI-action dispatch (WalkTo / DispatchInteract) is deferred to the
    // next tick via g_pending so the engine has a real frame to process
    // `enabled=1` before the dispatch path's SetEnabled(false) creates
    // the 1→0 transition the engine needs to fire AI walks.
    g_state.active = false;
    bool restored = acc::engine::SetPlayerInputEnabled(true);
    acclog::Write(
        "ViewMode: EXIT (deferred dispatch) input_restored=%d hasHover=%d",
        restored ? 1 : 0, hasHover ? 1 : 0);

    g_pending.active        = true;
    g_pending.hasHover      = hasHover;
    g_pending.hover_obj     = hover_obj;
    g_pending.hover_handle  = hover_handle;
    g_pending.cursor_pos    = cursor_pos;
    g_pending.forceRadial   = forceRadial;
    g_pending.armed_at_ms   = GetTickCount();

    acclog::Write(
        "ViewMode: %s -> dispatch armed for next tick "
        "(hover_obj=%p handle=0x%08x cursor=(%.2f,%.2f,%.2f) forceRadial=%d)",
        keyTag, hover_obj, hover_handle,
        cursor_pos.x, cursor_pos.y, cursor_pos.z,
        forceRadial ? 1 : 0);
}

// Process any pending dispatch armed by a prior PollEnter. Runs at the
// top of Tick BEFORE the IsActive() gate so it fires even when view
// mode is no longer active (which is precisely when it's needed —
// PollEnter exits view mode synchronously and arms this for the next
// tick). One-frame minimum delay enforced via kPendingDispatchMinElapsedMs.
void ProcessPendingDispatch() {
    if (!g_pending.active) return;

    DWORD elapsed = GetTickCount() - g_pending.armed_at_ms;
    if (elapsed < kPendingDispatchMinElapsedMs) return;

    // Snapshot then clear before dispatch — the dispatch path may itself
    // re-enter our hooks (e.g. picker::Drive can fire other monitors)
    // and we don't want re-entry to see g_pending as still active.
    bool     hasHover     = g_pending.hasHover;
    void*    hover_obj    = g_pending.hover_obj;
    uint32_t hover_handle = g_pending.hover_handle;
    Vector   cursor_pos   = g_pending.cursor_pos;
    bool     forceRadial  = g_pending.forceRadial;
    g_pending.active = false;

    const char* keyTag = forceRadial ? "Shift+Enter" : "Enter";

    if (hasHover) {
        acclog::Write(
            "ViewMode: %s deferred -> DispatchInteract obj=%p handle=0x%08x "
            "elapsed=%lums (hover target)",
            keyTag, hover_obj, hover_handle,
            static_cast<unsigned long>(elapsed));
        acc::interact::DispatchInteract(hover_obj, hover_handle, forceRadial);
        return;
    }

    // Empty cursor — raw walk-to-position. Speak the localised pre-roll
    // before WalkTo (cf. `feedback_never_silence_fallback_announcement`).
    const char* preroll = acc::strings::Get(acc::strings::Id::GuidingToPoint);
    tolk::Speak(preroll, /*interrupt=*/true);

    // Diagnostic 2026-05-07: try ForceWalkTo (queue-bypass) instead of
    // WalkTo. patch-20260507-083116.log proved the per-creature action
    // queue is innocent — clearing it before WalkTo still produced
    // moved=0.00m (stuck) on 4.07m and 5.99m dispatches. ForceWalkTo
    // uses CSWSCreature::ForceMoveToPoint, a different engine entry
    // point that doesn't enqueue. If the player walks via Force here
    // but not via WalkTo, the queue-processing path is asleep after
    // view-mode's sustained SetEnabled(false). If Force also fails,
    // the player creature itself is in a state that blocks movement
    // dispatch from any engine surface, and we need to look at what
    // view mode does to the creature's control state beyond input.
    bool ok = acc::guidance::ForceWalkTo(cursor_pos);
    acclog::Write(
        "ViewMode: %s deferred -> ForceWalkTo cursor=(%.2f,%.2f,%.2f) ok=%d "
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

    // UI-claim gate. Mirror of (and stricter than) interact_hotkey's Enter
    // gate. Toggling view mode flips engine-level CSWPlayerControl input
    // state via SetPlayerInputEnabled — doing that while any UI panel
    // claims input corrupts the engine's gate (post-modal-close lockup
    // observed in patch-20260507-203839.log line 20905+: ViewMode toggled
    // inside InGameOptions, then quit-confirm Cancel left movement dead).
    //
    // Strict version: panels[] scan for sub-screens AND for dialog panels
    // (stale-Fade-as-fg defeats fg-only checks), modal_stack non-empty,
    // and a foreground-kind blacklist for the remaining UI panels that
    // claim Enter directly. Differs from the Enter gate in that the Enter
    // gate accepts double-fire as recoverable; for view mode the failure
    // is engine-state corruption, so we lean toward block.
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
        acclog::Write("ViewMode: B (shift=%d) blocked — %s",
                      shift ? 1 : 0, blockReason);
        return;
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
    // Process any deferred dispatch from a prior PollEnter BEFORE the
    // active-gate. The dispatch fires after view mode has exited, so
    // gating this on g_state.active would never let it run.
    ProcessPendingDispatch();

    if (!g_state.active) return;

    // Foreground gate — same rationale as the cycle / view-mode pollers:
    // don't consume W/S held state when the user is alt-tabbed into a
    // browser typing about the bug they just hit. Even when foreground-
    // gated out, the OnSetListenerPosition hook keeps writing
    // g_state.cursor_pos every frame, so the soundscape stays anchored
    // to wherever the cursor was when focus was lost.
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

    // Lay-off 5 — Enter / Shift+Enter routing. Polled inside Tick (which
    // is already foreground- and active-gated) so we don't repeat the
    // gates. Runs AFTER NarrateNearestObject so the hover state read
    // here reflects the cursor position computed this same tick — no
    // one-tick lag between "cursor moved over door" and "Enter dispatches
    // to that door". PollEnter may exit view mode and dispatch — fields
    // we touched above (cursor_pos, hover_*) get reset on next entry.
    PollEnter();

    // No per-tick SetListener call here — the OnSetListenerPosition
    // detour at 0x5d5df0 substitutes the cursor position into the
    // engine's own per-frame listener write, which runs after this
    // OnUpdate path. That's the whole point of the rework: stop
    // racing the engine, intercept its own write site instead.
}

}  // namespace acc::view_mode
