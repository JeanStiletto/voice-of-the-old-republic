// Phase 5 lay-off 6 — virtual cursor on the area map UI.
//
// See map_ui_cursor.h for the user model and engine surfaces.
//
// Cursor lives in map-pixel space [0..440] × [0..256]. Movement keys
// translate the cursor in pixel space (with map-frame-relative axes —
// W = "up on the map", S = "down", A = "left", D = "right"; this is a
// 2D scan UI, not a 3D heading). The cursor's world projection comes
// from inverting CSWSAreaMap's pixel transform (4 fields, see
// docs/navsystems-investigation.md §Q4).

#include "map_ui_cursor.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

#include "audio_bus.h"
#include "audio_cue_player.h"
#include "audio_cues.h"
#include "engine_area.h"
#include "engine_manager.h"
#include "engine_panels.h"
#include "engine_player.h"
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::map_ui_cursor {

namespace {

// Movement speed in pixels per second. KOTOR's reference resolution is
// 640×480 and the map render fills ~440×256 of that; a 100 px/s cursor
// crosses the map in ~4.4 s, comparable to the engine walk speed. Tune
// live.
constexpr float kCursorSpeedPx = 100.0f;

// Bounds of the engine's map-pixel coordinate system (per Q4). Inclusive
// upper bound matches the engine's own check `< 0x1b9` / `< 0x101`, so
// the highest legal cursor pixel is (440, 256).
constexpr int kMapPixelMaxX = 440;
constexpr int kMapPixelMaxY = 256;

// dt cap to prevent teleport-on-stall (loading screens, alt-tab).
constexpr float kMaxDtSec = 0.1f;

// Hover-pause debounce for cursor narration. 300 ms matches view-mode's
// pattern so spoken cadence stays consistent across our two cursor
// surfaces.
constexpr DWORD kHoverPauseMs = 300;

// Radius (in map pixels) within which a map-note waypoint counts as
// "under the cursor". Roughly half a 16-px map-pin icon — generous
// enough that the user doesn't have to land pixel-perfect, tight
// enough that hover-pause discrimination still works between adjacent
// notes.
constexpr int kHoverHitRadiusPx = 24;

// Walked-into-edge cue debounce. Sustained edge-hugging would otherwise
// emit a NavCue::Wall every tick.
constexpr DWORD kEdgeCueQuietMs = 250;

// CSWSModule.area_map field offset (field89_0x218 in Lane's typed
// struct — investigation §Q4 + engine decomp of MapHider::Draw which
// reads exactly this offset to acquire the CSWSAreaMap*).
constexpr size_t kModuleAreaMapOffset = 0x218;

// CSWSAreaMap field layout (subset). Full layout in
// docs/navsystems-investigation.md §Q4.
constexpr size_t kAreaMapOrientationOffset      = 0x10;
constexpr size_t kAreaMapWorldUnitsPerXPxOffset = 0x18;
constexpr size_t kAreaMapWorldUnitsPerYPxOffset = 0x1c;
constexpr size_t kAreaMapWorldOriginXOffset     = 0x20;
constexpr size_t kAreaMapWorldOriginYOffset     = 0x24;

// CSWGuiInGameMap.map_hider field offset. Derived from the struct layout
// in swkotor.exe.h (CSWGuiInGameMap line 8548): after panel (CSWGuiPanel)
// + 4 labels + 5 buttons, the embedded CSWGuiMapHider sits at the offset
// the GoG xml confirms via the previous fields. The xml gives:
//   exit_button       @ +0x8ec  (size 0x1c4 → ends 0xab0)
//   up_button         @ +0xab0  (size 0x1c4 → ends 0xc74)
//   down_button       @ +0xc74  (size 0x1c4 → ends 0xe38)
//   map_hider         @ +0xe38  (CSWGuiMapHider, size 0x248)
// Then field11_0x238 inside map_hider holds the waypoint linked list.
constexpr size_t kInGameMapHiderOffset                = 0xe38;
constexpr size_t kMapHiderWaypointListOffset          = 0x238;  // CExoLinkedList

// CExoLinkedList layout (visible in engine_area / from Ghidra): the
// list holds an internal pointer; the internal exposes head/tail and
// nodes that point at the payload. For the map-hider list, the payload
// at each node is a `ulong` — the waypoint OBJECT HANDLE — which the
// engine then resolves via CServerExoApp::GetGameObject. Same access
// shape as CSWSArea.game_objects (handle array, not object pointer
// array).
constexpr size_t kCExoLinkedListInternalOffset = 0x0;
constexpr size_t kCExoLLInternalHeadOffset     = 0x0;
constexpr size_t kCExoLLNodeNextOffset         = 0x4;
constexpr size_t kCExoLLNodePayloadOffset      = 0x8;

// CSWSWaypoint layout offsets (already used elsewhere — re-stated here
// so the cursor code is self-documenting).
constexpr size_t kWaypointPositionOffset = 0x90;
constexpr size_t kWaypointHasMapNoteOff  = 0x22c;
constexpr size_t kWaypointMapNoteLocOff  = 0x230;

// Engine function addresses verified 2026-05-12 against
// k1_win_gog_swkotor.exe.xml.
constexpr uintptr_t kAddrCServerExoAppGetModule        = 0x004ae6b0;
constexpr uintptr_t kAddrCSWSAreaMapIsWorldPointExplored = 0x00579210;

using PFN_CServerExoApp_GetModule = void* (__thiscall*)(void* /*serverApp*/);
using PFN_CSWSAreaMap_IsWorldPointExplored =
    bool (__thiscall*)(void* /*areaMap*/, Vector /*pos*/);

struct CursorState {
    bool   active                  = false;
    float  px                      = 220.0f;   // (kMapPixelMaxX / 2.0f)
    float  py                      = 128.0f;   // (kMapPixelMaxY / 2.0f)
    Vector world                   = {0, 0, 0};
    DWORD  last_tick_ms            = 0;
    DWORD  last_edge_cue_ms        = 0;
    void*  pending_note_waypoint   = nullptr;
    DWORD  pending_note_started_ms = 0;
    void*  last_spoken_waypoint    = nullptr;
    bool   last_announced_unexplored = false;
};

CursorState g_state;

// Foreground-window gate. Same pattern as cycle_input::PollWin32 — only
// consume keys while KOTOR has focus so alt-tabbing doesn't steal
// movement from other apps.
bool IsForegroundProcess() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == GetCurrentProcessId();
}

// The InGameMap sub-screen is pushed onto panels[] *under* the
// CSWGuiInGameMenu strip — GetForegroundPanel returns the strip, not
// the map. Walk panels[] explicitly looking for any InGameMap entry;
// that's how `MonitorPanelContents` finds the sub-screen too.
bool IsMapPanelActive(void** outPanel) {
    if (outPanel) *outPanel = nullptr;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int panelSize = 0;
    void** panelData = nullptr;
    __try {
        panelSize = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
        panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!panelData || panelSize <= 0) return false;
    int n = panelSize > 16 ? 16 : panelSize;
    for (int i = 0; i < n; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        if (acc::engine::IdentifyPanel(p) ==
            acc::engine::PanelKind::InGameMap) {
            if (outPanel) *outPanel = p;
            return true;
        }
    }
    return false;
}

void* GetServerApp() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appManager) +
            kAppManagerServerOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetAreaMap() {
    void* serverApp = GetServerApp();
    if (!serverApp) return nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_CServerExoApp_GetModule>(
            kAddrCServerExoAppGetModule);
        void* module = fn(serverApp);
        if (!module) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(module) + kModuleAreaMapOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool IsWorldPointExplored(void* areaMap, const Vector& pos) {
    if (!areaMap) return false;
    __try {
        auto fn = reinterpret_cast<PFN_CSWSAreaMap_IsWorldPointExplored>(
            kAddrCSWSAreaMapIsWorldPointExplored);
        return fn(areaMap, pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Read CSWSAreaMap pixel-transform fields. Returns false on any read
// fault (SEH-guarded). All four scalars are required to invert; one
// missing → cursor stays dormant this tick.
bool ReadAreaMapTransform(void* areaMap,
                          uint32_t& outOrientation,
                          float& outWuPerPx, float& outWuPerPy,
                          float& outOriginX, float& outOriginY) {
    if (!areaMap) return false;
    __try {
        auto* b = reinterpret_cast<unsigned char*>(areaMap);
        outOrientation = *reinterpret_cast<uint32_t*>(b + kAreaMapOrientationOffset);
        outWuPerPx     = *reinterpret_cast<float*>   (b + kAreaMapWorldUnitsPerXPxOffset);
        outWuPerPy     = *reinterpret_cast<float*>   (b + kAreaMapWorldUnitsPerYPxOffset);
        outOriginX     = *reinterpret_cast<float*>   (b + kAreaMapWorldOriginXOffset);
        outOriginY     = *reinterpret_cast<float*>   (b + kAreaMapWorldOriginYOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Apply the engine's forward rotation per orientation, matching
// CSWSAreaMap::GetMapPixelFromWorldCoord exactly. Decompile confirms:
//   case 1: (xw, yw) -> (-xw, -yw)
//   case 2: (xw, yw) -> (yw,  -xw)
//   case 3: (xw, yw) -> (-yw,  xw)  (case 3 keeps yw as-is, x flipped)
void ApplyOrientationForward(uint32_t orient, float& xw, float& yw) {
    float ox = xw, oy = yw;
    switch (orient) {
        case 0: /* identity */                       break;
        case 1: xw = -ox; yw = -oy;                  break;
        case 2: xw =  oy; yw = -ox;                  break;
        case 3: xw = -oy; yw =  ox;                  break;
        default: /* unknown — leave unmolested */    break;
    }
}

// Undo it.
void ApplyOrientationInverse(uint32_t orient, float& xw, float& yw) {
    float ox = xw, oy = yw;
    switch (orient) {
        case 0: /* identity */                       break;
        case 1: xw = -ox; yw = -oy;                  break;  // self-inverse
        case 2: xw = -oy; yw =  ox;                  break;  // inverse of (y,-x) = (-y, x)
        case 3: xw =  oy; yw = -ox;                  break;  // inverse of (-y, x) = (y, -x)
        default: /* unknown */                       break;
    }
}

bool WorldToPixel(void* areaMap, const Vector& world, float& outPx, float& outPy) {
    uint32_t orient; float wuPx, wuPy, ox, oy;
    if (!ReadAreaMapTransform(areaMap, orient, wuPx, wuPy, ox, oy)) return false;
    if (wuPx == 0.0f || wuPy == 0.0f) return false;
    float xw = world.x, yw = world.y;
    ApplyOrientationForward(orient, xw, yw);
    outPx = (xw - ox) / wuPx + 0.5f;
    outPy = (yw - oy) / wuPy + 0.5f;
    return true;
}

bool PixelToWorld(void* areaMap, float px, float py, float zSeed, Vector& outWorld) {
    uint32_t orient; float wuPx, wuPy, ox, oy;
    if (!ReadAreaMapTransform(areaMap, orient, wuPx, wuPy, ox, oy)) return false;
    float xw = px * wuPx + ox;
    float yw = py * wuPy + oy;
    ApplyOrientationInverse(orient, xw, yw);
    outWorld.x = xw;
    outWorld.y = yw;
    outWorld.z = zSeed;
    return true;
}

// Walk the map-hider's waypoint linked list, find the nearest waypoint
// whose map-pixel projection lands within kHoverHitRadiusPx of the
// cursor AND whose has_map_note != 0 AND whose position
// IsWorldPointExplored == true. Returns the CSWSWaypoint* (server-side
// object) of the best match, or nullptr.
//
// Same filter the engine's GetNext/PrevMapNote apply — we re-do the
// scan rather than reading their cached current-node so we get spatial
// not sequential hit detection.
void* FindNearestExploredMapNote(void* mapPanel, void* areaMap,
                                 float cursorPx, float cursorPy,
                                 int* outScannedCount = nullptr) {
    if (!mapPanel || !areaMap) return nullptr;
    void* mapHider = reinterpret_cast<unsigned char*>(mapPanel) +
                     kInGameMapHiderOffset;
    void* internal = nullptr;
    __try {
        internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(mapHider) +
            kMapHiderWaypointListOffset +
            kCExoLinkedListInternalOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!internal) return nullptr;

    void* serverApp = GetServerApp();
    if (!serverApp) return nullptr;

    void* bestObj = nullptr;
    float bestDist2 = (float)(kHoverHitRadiusPx * kHoverHitRadiusPx);

    void* node = nullptr;
    __try {
        node = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(internal) +
            kCExoLLInternalHeadOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }

    int guard = 0;
    int scanned = 0;
    while (node && guard++ < 256) {
        ++scanned;
        // CExoLinkedList<ulong>: each node's data is a heap-boxed ulong*,
        // so the handle requires two indirections from `node`. Decomp of
        // GetNextMapNote confirms: GetAtPos returns ulong* and the caller
        // does `uVar7 = *puVar2;` to extract the handle value.
        uint32_t handle = 0;
        void* nextNode = nullptr;
        __try {
            void* dataPtr = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kCExoLLNodePayloadOffset);
            if (dataPtr) {
                handle = *reinterpret_cast<uint32_t*>(dataPtr);
            }
            nextNode = *reinterpret_cast<void**>(
                reinterpret_cast<unsigned char*>(node) +
                kCExoLLNodeNextOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (handle == 0) { node = nextNode; continue; }

        void* obj = acc::engine::ResolveServerObjectHandle(handle);
        if (!obj) { node = nextNode; continue; }

        // Spoiler / curation gate — same checks the engine uses
        // internally for GetNext/PrevMapNote.
        int hasNote = 0;
        Vector pos = {0,0,0};
        __try {
            hasNote = *reinterpret_cast<int*>(
                reinterpret_cast<unsigned char*>(obj) +
                kWaypointHasMapNoteOff);
            pos = *reinterpret_cast<Vector*>(
                reinterpret_cast<unsigned char*>(obj) +
                kWaypointPositionOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            node = nextNode; continue;
        }
        if (hasNote == 0) { node = nextNode; continue; }
        if (!IsWorldPointExplored(areaMap, pos)) {
            node = nextNode; continue;
        }

        float ppx, ppy;
        if (!WorldToPixel(areaMap, pos, ppx, ppy)) {
            node = nextNode; continue;
        }
        acclog::Trace("MapCursor.dump",
                      "waypoint #%d handle=0x%x world=(%.2f,%.2f) "
                      "pixel=(%.1f,%.1f)",
                      scanned, handle, pos.x, pos.y, ppx, ppy);
        float dx = ppx - cursorPx;
        float dy = ppy - cursorPy;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestObj = obj;
        }
        node = nextNode;
    }
    if (outScannedCount) *outScannedCount = scanned;
    return bestObj;
}

bool ReadWaypointMapNoteText(void* waypoint, char* outBuf, size_t bufSize) {
    return acc::engine::GetWaypointMapNote(waypoint, outBuf, bufSize);
}

void SeedCursorAtPlayer(void* areaMap) {
    Vector p;
    if (!acc::engine::GetPlayerPosition(p)) return;
    float px, py;
    if (!WorldToPixel(areaMap, p, px, py)) return;
    if (px < 0) px = 0;
    if (px > (float)kMapPixelMaxX) px = (float)kMapPixelMaxX;
    if (py < 0) py = 0;
    if (py > (float)kMapPixelMaxY) py = (float)kMapPixelMaxY;
    g_state.px = px;
    g_state.py = py;
    g_state.world = p;
}

void ResetSessionState() {
    g_state.active                    = false;
    g_state.last_tick_ms              = 0;
    g_state.last_edge_cue_ms          = 0;
    g_state.pending_note_waypoint     = nullptr;
    g_state.pending_note_started_ms   = 0;
    g_state.last_spoken_waypoint      = nullptr;
    g_state.last_announced_unexplored = false;
}

bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

}  // namespace

bool IsActive() { return g_state.active; }

bool TryGetCursorWorldPosition(Vector& out) {
    if (!g_state.active) return false;
    out = g_state.world;
    return true;
}

void Tick() {
    void* mapPanel = nullptr;
    if (!IsForegroundProcess() || !IsMapPanelActive(&mapPanel)) {
        if (g_state.active) {
            acclog::Write("MapCursor", "deactivated — map no longer foreground");
            ResetSessionState();
        }
        return;
    }

    void* areaMap = GetAreaMap();
    if (!areaMap) {
        if (g_state.active) {
            acclog::Write("MapCursor", "deactivated — area_map vanished");
            ResetSessionState();
        }
        return;
    }

    DWORD now = GetTickCount();
    if (!g_state.active) {
        SeedCursorAtPlayer(areaMap);
        PixelToWorld(areaMap, g_state.px, g_state.py, g_state.world.z, g_state.world);
        g_state.active                  = true;
        g_state.last_tick_ms            = now;
        g_state.last_spoken_waypoint    = nullptr;
        g_state.pending_note_waypoint   = nullptr;
        g_state.pending_note_started_ms = 0;
        g_state.last_announced_unexplored = false;
        acclog::Write("MapCursor",
                      "activated — seed pixel=(%.1f,%.1f) world=(%.2f,%.2f,%.2f)",
                      g_state.px, g_state.py,
                      g_state.world.x, g_state.world.y, g_state.world.z);
    }

    // dt
    float dt = (now - g_state.last_tick_ms) * 0.001f;
    g_state.last_tick_ms = now;
    if (dt > kMaxDtSec) dt = kMaxDtSec;
    if (dt < 0.0f) dt = 0.0f;

    // Cursor delta from movement keys. Map-frame axes: W = -py (up on
    // map = lower pixel), S = +py, A = -px, D = +px. Pixel space y
    // grows down per the engine's screen convention.
    float vx = 0.0f, vy = 0.0f;
    bool w = KeyDown('W');
    bool s = KeyDown('S');
    bool a = KeyDown('A');
    bool d = KeyDown('D');
    if (w || s || a || d) {
        acclog::Trace("MapCursor", "input W=%d S=%d A=%d D=%d", w, s, a, d);
    }
    if (w && !s) vy -= 1.0f;
    if (s && !w) vy += 1.0f;
    if (a && !d) vx -= 1.0f;
    if (d && !a) vx += 1.0f;
    if (vx != 0.0f || vy != 0.0f) {
        // Normalise diagonals so combined keys don't 1.4× the speed.
        float mag = std::sqrt(vx * vx + vy * vy);
        vx /= mag; vy /= mag;
    }
    float step = kCursorSpeedPx * dt;
    float newPx = g_state.px + vx * step;
    float newPy = g_state.py + vy * step;

    bool clampedX = false, clampedY = false;
    if (newPx < 0.0f) { newPx = 0.0f; clampedX = true; }
    if (newPx > (float)kMapPixelMaxX) { newPx = (float)kMapPixelMaxX; clampedX = true; }
    if (newPy < 0.0f) { newPy = 0.0f; clampedY = true; }
    if (newPy > (float)kMapPixelMaxY) { newPy = (float)kMapPixelMaxY; clampedY = true; }
    bool moved = (newPx != g_state.px) || (newPy != g_state.py);

    g_state.px = newPx;
    g_state.py = newPy;
    PixelToWorld(areaMap, g_state.px, g_state.py, g_state.world.z, g_state.world);

    // Edge collision cue. Fires only when the user is actively pressing
    // into the map boundary — not on idle clamp.
    if ((clampedX || clampedY) && (vx != 0.0f || vy != 0.0f)) {
        if (g_state.last_edge_cue_ms == 0 ||
            now - g_state.last_edge_cue_ms > kEdgeCueQuietMs) {
            acc::audio::PlayCue3D(
                acc::audio::GetNavCueResref(acc::audio::NavCue::Wall),
                g_state.world);
            g_state.last_edge_cue_ms = now;
        }
    }

    // Hover-pause map-note narration. Three-variable pattern.
    int scannedCount = 0;
    void* hit = FindNearestExploredMapNote(mapPanel, areaMap,
                                           g_state.px, g_state.py,
                                           &scannedCount);
    if (moved) {
        acclog::Trace("MapCursor",
                      "tick pixel=(%.1f,%.1f) world=(%.2f,%.2f) "
                      "scanned=%d hit=%p",
                      g_state.px, g_state.py,
                      g_state.world.x, g_state.world.y,
                      scannedCount, hit);
    }

    if (hit) {
        // Reset unexplored-state announcement if we move onto a note.
        g_state.last_announced_unexplored = false;
        if (hit == g_state.last_spoken_waypoint) {
            // Already spoken — keep silence.
            g_state.pending_note_waypoint = nullptr;
            g_state.pending_note_started_ms = 0;
        } else if (hit == g_state.pending_note_waypoint) {
            if (g_state.pending_note_started_ms != 0 &&
                now - g_state.pending_note_started_ms >= kHoverPauseMs) {
                char text[256];
                if (ReadWaypointMapNoteText(hit, text, sizeof(text)) &&
                    text[0] != '\0') {
                    // NOW-priority via NVDA SSML — survives the typed-
                    // character cancellation that NORMAL-priority
                    // Tolk_Output suffers from while WASD is held.
                    tolk::SpeakUrgent(text);
                    acclog::Write("MapCursor",
                                  "speak note=\"%s\" cursor=(%.1f,%.1f)",
                                  text, g_state.px, g_state.py);
                }
                g_state.last_spoken_waypoint    = hit;
                g_state.pending_note_waypoint   = nullptr;
                g_state.pending_note_started_ms = 0;
            }
        } else {
            g_state.pending_note_waypoint   = hit;
            g_state.pending_note_started_ms = now;
        }
    } else {
        // Cursor not over a map note. Drop any pending. If cursor sits
        // on an unexplored point AND we haven't already announced that,
        // wait the hover-pause and speak it once.
        if (g_state.pending_note_waypoint != nullptr) {
            g_state.pending_note_waypoint   = nullptr;
            g_state.pending_note_started_ms = 0;
        }
        if (g_state.last_spoken_waypoint != nullptr && moved) {
            // We left a note — clear the "already spoken" so re-entering
            // the same note announces again.
            g_state.last_spoken_waypoint = nullptr;
        }
        // Unexplored-area cue. Held silent while user is moving — only
        // fires when cursor settles for a beat over fog-of-war.
        // Implementation deferred to a follow-up; the first cut focuses
        // on note-hover speech to verify the basic geometry path.
    }
}

}  // namespace acc::map_ui_cursor
