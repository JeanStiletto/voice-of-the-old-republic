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
#include "engine_reads.h"             // ReadCExoString — waypoint tag log
#include "guidance_pathfind.h"        // path-graph offsets for adjacency
#include "log.h"
#include "spatial_change_detector.h"  // GetCachedWalls
#include "strings.h"
#include "tolk.h"
#include "transitions.h"

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
// "under the cursor". A 16-px map-pin icon plus generous slack —
// landmarks on the in-game map are sparse enough that 36 px overlap
// between adjacent pins is rare even on landmark-dense maps, and the
// hover-pause debounce discriminates anyway (the closer-of-two scan in
// FindNearestExploredMapNote always picks the nearest pin to the
// cursor centre). Tested live: 24 px was hard to pinpoint without
// sighted feedback; 36 px lets the cursor catch landmarks reliably.
constexpr int kHoverHitRadiusPx = 36;

// Walked-into-edge cue debounce. Sustained edge-hugging would otherwise
// emit a cue every tick.
constexpr DWORD kEdgeCueQuietMs = 250;

// Volume scalar for the map-edge collision cue. kAccCueGain (4.0) was
// inaudible in the paused map-UI sub-screen (verified in
// patch-20260512-143327.log: 28 successive PlayCue3D_ok=1 fires with
// zero perceived output). guidance_beacon ran into the same paused-
// context attenuation and uses 8.0×; same level applied here for the
// same reason — UI feedback has to win against pause-mode audio dampening.
constexpr float kEdgeCueGain = 8.0f;

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

// Ambient categories the cursor can sit on when it is NOT directly over
// a discrete map-note waypoint. Mutually exclusive: cursor is either in
// fog of war (Unexplored), or in an explored layout-room that has a
// cached Bioware-authored landmark (Landmark — Tier 1), or in an
// explored layout-room whose CSWSArea.room_names[] entry is a mod-
// supplied human-readable label (RoomName — Tier 2, rare in vanilla),
// or none of the above (None — silent).
//
// Vanilla resref-style room ids (`m02_03e`, `stunt_01_main`, ...) never
// reach RoomName because IsResrefStyleRoomName filters them out — those
// rooms classify as None and the cursor stays quiet over them, by
// design. The cursor surface is "what's HERE" — silence over a chunk
// the engine couldn't name is honest.
enum class AmbientKind {
    None,
    Unexplored,
    Landmark,
    RoomName,
    // Terrain shape synthesised from a 4-direction wall probe. Falls
    // through when no waypoint/landmark/named-room match — gives blind
    // players a sense of corridor vs. room vs. junction the same way a
    // sighted player reads geometry off the minimap render.
    TerrainShape,
};

struct CursorState {
    bool   active                  = false;
    bool   announced_area_name     = false;
    float  px                      = 220.0f;   // (kMapPixelMaxX / 2.0f)
    float  py                      = 128.0f;   // (kMapPixelMaxY / 2.0f)
    Vector world                   = {0, 0, 0};
    DWORD  last_tick_ms            = 0;
    DWORD  last_edge_cue_ms        = 0;

    // Waypoint hover-pause (explicit map-note overlay — Tier 1
    // landmark-as-game-object).
    void*  pending_note_waypoint   = nullptr;
    DWORD  pending_note_started_ms = 0;
    void*  last_spoken_waypoint    = nullptr;

    // Ambient hover-pause (fog-of-war + layout-room labels + terrain
    // shape). A single timer covers all kinds so flipping between e.g.
    // Landmark and RoomName doesn't double-debounce. last_spoken_*
    // latches the most recent ambient announcement; the cursor stays
    // silent while it sits inside the same ambient zone, and re-arms
    // the moment a different kind/room/shape signature is observed.
    AmbientKind pending_ambient_kind         = AmbientKind::None;
    int         pending_ambient_room_idx     = -1;
    DWORD       pending_ambient_started_ms   = 0;
    AmbientKind last_spoken_ambient_kind     = AmbientKind::None;
    int         last_spoken_ambient_room_idx = -1;
    // Text-based dedup overlay: when adjacent rooms produce identical
    // labels ("Kreuzung, Nord, Ost" in three Taris rooms with
    // different signatures), the (kind, sig) comparator alone treats
    // them as distinct and re-announces on every crossing. Storing
    // the last-spoken text and comparing case-sensitively collapses
    // those into one logical region — same text, no re-announce.
    char        last_spoken_ambient_text[128] = {0};

    // TerrainShape de-dupe + speak buffer: the classified description is
    // built at probe time (when we arm the hover-pause), cached on the
    // pending side, and consumed when the timer elapses. Without the
    // pending buffer the speak path would have to re-probe at speak
    // time — fine functionally but more work, and the local geometry
    // can't realistically change in 300 ms anyway. Signature carries
    // kind+quantised dimensions so the existing (kind, room_idx) dedup
    // comparator works unchanged with room_idx repurposed as signature.
    char        pending_shape_text[128]      = {0};
    int         pending_shape_signature      = 0;
};

CursorState g_state;

// Per-room shape cache. Replaces the per-tick walkmesh probe with a
// one-time classification at map activation: each room in the current
// area is sampled at its representative point (middle-face centroid in
// world space) and classified once. The cursor then looks up shape by
// room index — same room, same shape, always, no matter where in the
// room the cursor sits. Trade-off vs. per-tick:
//   - Loses sub-room granularity: a layout-room that contains both a
//     corridor section and a junction collapses to one shape (whichever
//     the centroid sample resolved to).
//   - Gains stability: the user gets a deterministic label per room
//     instead of a different one for every cursor pixel. They explicitly
//     asked for "not position-dependent — calculate once on map open".
//   - Gains performance: zero raycasts in the hot path. Per-area cost
//     is one classification per room at activation (cheap).
// Cache invalidates when the area pointer changes (entering a new
// scene). Cleared on DLL load by zero-initialisation.
struct RoomShapeCache {
    void* area_owner = nullptr;   // CSWSArea* this cache was built for
    bool  built      = false;
    int   room_count = 0;
    static constexpr int kMaxRooms = 64;
    char   text[kMaxRooms][128] = {};
    int    sig[kMaxRooms] = {};
    bool   present[kMaxRooms] = {};  // false = no shape (e.g. unexplored room at build time)
    Vector rep[kMaxRooms] = {};      // world-space centroid stored so cache misses can snap to nearest cached room
};
RoomShapeCache g_room_cache;

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
    g_state.active                        = false;
    g_state.announced_area_name           = false;
    g_state.last_tick_ms                  = 0;
    g_state.last_edge_cue_ms              = 0;
    g_state.pending_note_waypoint         = nullptr;
    g_state.pending_note_started_ms       = 0;
    g_state.last_spoken_waypoint          = nullptr;
    g_state.pending_ambient_kind          = AmbientKind::None;
    g_state.pending_ambient_room_idx      = -1;
    g_state.pending_ambient_started_ms    = 0;
    g_state.last_spoken_ambient_kind      = AmbientKind::None;
    g_state.last_spoken_ambient_room_idx  = -1;
    g_state.last_spoken_ambient_text[0]   = '\0';
    g_state.pending_shape_text[0]         = '\0';
    g_state.pending_shape_signature       = 0;
}

const char* AmbientKindStr(AmbientKind k) {
    switch (k) {
        case AmbientKind::None:         return "none";
        case AmbientKind::Unexplored:   return "unexplored";
        case AmbientKind::Landmark:     return "landmark";
        case AmbientKind::RoomName:     return "room_name";
        case AmbientKind::TerrainShape: return "terrain_shape";
    }
    return "?";
}

// Quantise a world-space distance to nearest metre, capped at 25 m. Used
// to build a stable description signature so the hover-pause de-dupe
// fires when the cursor settles inside a corridor — not when distances
// jitter by 0.05 m as the user nudges the cursor.
int QuantiseMetres(float d) {
    if (d < 0.0f)  d = 0.0f;
    if (d > 25.0f) d = 25.0f;
    return static_cast<int>(d + 0.5f);
}

// Cast one ray from `origin` in `(dx,dy)` world XY direction (unit
// vector), length `kProbeLenWu`, against the cached wall buffer. Returns
// the distance to the nearest wall or `kProbeLenWu` if the ray reaches
// its cap unobstructed. Z is preserved across the segment so
// SegmentCrossesWalkmesh's planar XY test stays consistent with the
// cursor's seeded Z.
constexpr float kProbeLenWu = 25.0f;
float ProbeWall(const acc::engine::WallEdge* walls, int wallCount,
                const Vector& origin, float dx, float dy) {
    Vector b;
    b.x = origin.x + dx * kProbeLenWu;
    b.y = origin.y + dy * kProbeLenWu;
    b.z = origin.z;
    Vector hit;
    if (acc::engine::SegmentCrossesWalkmesh(walls, wallCount,
                                            origin, b, hit)) {
        float ddx = hit.x - origin.x;
        float ddy = hit.y - origin.y;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    return kProbeLenWu;
}

// Classify the local terrain shape at the cursor's world position by
// probing the cached perimeter walls in world N/E/S/W. The four
// distances drive a small decision tree:
//
//   axisNS = dist_N + dist_S    (full extent along N-S)
//   axisEW = dist_E + dist_W
//   minD   = min(dist_N, dist_E, dist_S, dist_W)
//   maxD   = max(...)
//
// Off-path: all four distances ≤ 0.8 m → cursor is sitting on/inside a
//   wall (the walkmesh perimeter surrounds it tightly).
// Open area: both axes ≥ 12 m AND minD ≥ 4 m → mostly empty space.
// Corridor: one axis ≥ 2.2× the other AND the wide axis ≥ 8 m → tight
//   long passage; report orientation + width = narrower axis.
// Dead end: three of the four directions ≤ 2 m, one ≥ 5 m → branch with
//   walls on three sides; opening direction = the long one.
// Junction: everything else (multiple medium-length directions).
//
// Writes the localised description to `outBuf` and a small integer
// "signature" to `outSig` capturing kind + quantised dimensions so the
// hover-pause de-dupe can compare without strcmp.
bool ClassifyTerrainShape(const acc::engine::WallEdge* walls, int wallCount,
                          const Vector& cursor,
                          char* outBuf, size_t bufSize, int& outSig) {
    if (!walls || wallCount <= 0 || !outBuf || bufSize < 8) return false;
    float dN = ProbeWall(walls, wallCount, cursor,  0.0f,  1.0f);
    float dE = ProbeWall(walls, wallCount, cursor,  1.0f,  0.0f);
    float dS = ProbeWall(walls, wallCount, cursor,  0.0f, -1.0f);
    float dW = ProbeWall(walls, wallCount, cursor, -1.0f,  0.0f);

    float axisNS = dN + dS;
    float axisEW = dE + dW;
    float minD = dN;
    if (dE < minD) minD = dE;
    if (dS < minD) minD = dS;
    if (dW < minD) minD = dW;
    float maxD = dN;
    if (dE > maxD) maxD = dE;
    if (dS > maxD) maxD = dS;
    if (dW > maxD) maxD = dW;

    using acc::strings::Id;

    // Build signature: low byte = kind, next bytes = quantised
    // primary/secondary metric. Stable across cursor jitter, cheap to
    // compare.
    auto pack = [](int kind, int a, int b) {
        return (kind & 0xff) | ((a & 0xff) << 8) | ((b & 0xff) << 16);
    };

    // Off-path — cursor over wall / outside walkable region.
    if (maxD <= 0.8f) {
        const char* s = acc::strings::Get(Id::MapCursorOffPath);
        if (s && s[0]) {
            std::snprintf(outBuf, bufSize, "%s", s);
            outSig = pack(1, 0, 0);
            return true;
        }
        return false;
    }

    // Open area — large extents in both axes.
    if (axisNS >= 12.0f && axisEW >= 12.0f && minD >= 4.0f) {
        const char* s = acc::strings::Get(Id::MapCursorOpenArea);
        if (s && s[0]) {
            std::snprintf(outBuf, bufSize, "%s", s);
            outSig = pack(2, QuantiseMetres(axisNS), QuantiseMetres(axisEW));
            return true;
        }
        return false;
    }

    // Corridor — one axis much longer than the other.
    bool corridorNS = (axisNS >= 8.0f) && (axisEW > 0.0f) &&
                      (axisNS >= 2.2f * axisEW);
    bool corridorEW = (axisEW >= 8.0f) && (axisNS > 0.0f) &&
                      (axisEW >= 2.2f * axisNS);
    if (corridorNS || corridorEW) {
        const char* axisStr = acc::strings::Get(
            corridorNS ? Id::AxisNorthSouth : Id::AxisEastWest);
        float width = corridorNS ? axisEW : axisNS;
        const char* fmt = acc::strings::Get(Id::FmtMapCursorCorridor);
        if (fmt && fmt[0] && axisStr && axisStr[0]) {
            std::snprintf(outBuf, bufSize, fmt, axisStr, width);
            outSig = pack(corridorNS ? 3 : 4, QuantiseMetres(width), 0);
            return true;
        }
        return false;
    }

    // Dead end — exactly three directions short (≤ 2 m), one open.
    // Previously also required longLen ≥ 5 m, which leaked single-
    // direction "Kreuzung" announces for 3 m alcoves
    // (patch-20260512-164059.log sig=328198 "Kreuzung, Ost"). Drop that
    // requirement: if 3 of 4 sides are walled in ≤ 2 m, semantically
    // it's a dead-end regardless of how far the one open direction
    // continues. The open distance becomes part of the dedup signature
    // so 3 m vs. 12 m dead-ends still differentiate.
    int shortCount = 0;
    int longIdx = -1;     // 0=N 1=E 2=S 3=W
    float longLen = 0.0f;
    float arr[4] = {dN, dE, dS, dW};
    for (int i = 0; i < 4; ++i) {
        if (arr[i] <= 2.0f) ++shortCount;
        if (arr[i] > longLen) { longLen = arr[i]; longIdx = i; }
    }
    if (shortCount == 3 && longIdx >= 0 && longLen > 2.0f) {
        Id dirIds[4] = { Id::DirNorth, Id::DirEast,
                         Id::DirSouth, Id::DirWest };
        const char* dir = acc::strings::Get(dirIds[longIdx]);
        const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
        if (fmt && fmt[0] && dir && dir[0]) {
            std::snprintf(outBuf, bufSize, fmt, dir);
            outSig = pack(5, longIdx, QuantiseMetres(longLen));
            return true;
        }
        return false;
    }

    // Junction / complex geometry — list the open directions instead
    // of just saying "Kreuzung", so the user knows which way each
    // branch leads.
    //
    // Threshold history: started at 3.0 m to exclude wall-hugging
    // micro-openings, but the user encountered rooms (e.g. room 4 in
    // patch-20260512-160753.log with axes 1×2 m — likely a doorway
    // chamber) where every direction is < 3 m and the announce
    // collapsed to a bare uninformative "Kreuzung". Now at 2.0 m,
    // which catches almost all real walk-throughs.
    //
    // Fallback when even 2.0 m isn't met by any direction: pick the
    // two widest directions and list them anyway. "Kreuzung mit
    // Öffnungen nach Norden, Westen" with a 1 m N and 1.5 m W beats
    // "Kreuzung" silence — the user already inferred junction from
    // the kind, the directions are the actionable part.
    constexpr float kOpeningThresholdM = 2.0f;
    Id dirIds[4] = { Id::DirNorth, Id::DirEast, Id::DirSouth, Id::DirWest };

    auto appendDir = [&](char* dirList, size_t bufSize, size_t& dirLen,
                         int& sigDirMask, int i) {
        const char* dir = acc::strings::Get(dirIds[i]);
        if (!dir || !dir[0]) return;
        if (dirLen > 0 && dirLen + 2 < bufSize) {
            dirList[dirLen++] = ',';
            dirList[dirLen++] = ' ';
            dirList[dirLen]   = '\0';
        }
        int n = std::snprintf(dirList + dirLen,
                              bufSize - dirLen, "%s", dir);
        if (n > 0) dirLen += static_cast<size_t>(n);
        sigDirMask |= (1 << i);
    };

    char dirList[96] = {0};
    size_t dirLen = 0;
    int sigDirMask = 0;
    for (int i = 0; i < 4; ++i) {
        if (arr[i] < kOpeningThresholdM) continue;
        appendDir(dirList, sizeof(dirList), dirLen, sigDirMask, i);
    }

    // No direction met the threshold — pick the two widest as a
    // honest fallback. Sort indices descending by distance.
    if (sigDirMask == 0) {
        int order[4] = {0, 1, 2, 3};
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                if (arr[order[j]] > arr[order[i]]) {
                    int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
                }
            }
        }
        // List the top 2 only if they're non-trivial (≥ 0.5 m — even
        // a closet has SOME extent, and below 0.5 m we're really just
        // measuring numerical noise inside a wall).
        int listed = 0;
        for (int k = 0; k < 4 && listed < 2; ++k) {
            if (arr[order[k]] < 0.5f) break;
            appendDir(dirList, sizeof(dirList), dirLen, sigDirMask,
                      order[k]);
            ++listed;
        }
    }

    if (sigDirMask != 0) {
        const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
        if (fmt && fmt[0]) {
            std::snprintf(outBuf, bufSize, fmt, dirList);
            outSig = pack(6, sigDirMask,
                          QuantiseMetres(axisNS + axisEW));
            return true;
        }
    }
    // Final fallback — bare "Junction". With the top-2 fallback above
    // this should be rare in practice (only when ALL four directions
    // are < 0.5 m, which essentially means "inside a wall"). Keep
    // honest rather than silent.
    const char* s = acc::strings::Get(Id::MapCursorJunction);
    if (s && s[0]) {
        std::snprintf(outBuf, bufSize, "%s", s);
        outSig = pack(6,
                      QuantiseMetres(axisNS),
                      QuantiseMetres(axisEW));
        return true;
    }
    return false;
}

// Build (or rebuild) the per-room shape cache for `area`. Walks every
// CSWSRoom in the area, derives a representative point inside it via
// engine::GetRoomRepresentativeWorld, and classifies the room's shape
// once using the cached wall buffer. Stored by room index for O(1)
// lookup in Tick. Rooms whose centroid isn't yet explored are recorded
// as `present=false` — when the cursor pans into them later we still
// fall through to the per-tick probe rather than re-running the build
// (the user already revealed the cell, the cache might just predate
// that reveal). Skipping unexplored rooms also keeps the cache build
// spoiler-correct against future fog reveals.
void BuildRoomShapeCache(void* area, void* areaMap) {
    g_room_cache.built = false;
    g_room_cache.room_count = 0;
    for (int i = 0; i < RoomShapeCache::kMaxRooms; ++i) {
        g_room_cache.text[i][0] = '\0';
        g_room_cache.sig[i]     = 0;
        g_room_cache.present[i] = false;
    }
    g_room_cache.area_owner = area;
    if (!area || !areaMap) return;

    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        acclog::Write("MapCursor",
                      "BuildRoomShapeCache: wall cache not ready — "
                      "cache empty for this activation; per-tick probe "
                      "fallback remains in place");
        return;
    }

    int roomCount = 0;
    __try {
        // kAreaRoomCountOffset is at file scope in engine_area.h (between
        // two acc::engine namespace blocks) — reference unqualified.
        roomCount = static_cast<int>(*reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(area) +
            kAreaRoomCountOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        roomCount = 0;
    }
    if (roomCount > RoomShapeCache::kMaxRooms) {
        acclog::Write("MapCursor",
                      "BuildRoomShapeCache: area reports %d rooms — "
                      "truncating to cache capacity %d",
                      roomCount, RoomShapeCache::kMaxRooms);
        roomCount = RoomShapeCache::kMaxRooms;
    }
    g_room_cache.room_count = roomCount;

    // Classify EVERY room, regardless of current fog-of-war state.
    // Fog-of-war gating happens at lookup-time in Tick (the cache is
    // only consulted when cursorExplored is true), so building shapes
    // for unexplored rooms doesn't leak — but skipping them at build
    // time DOES break the cache: the first revisit to the map after
    // walking through a previously-unexplored room would fall through
    // to the per-tick probe instead of using the cache (which is what
    // happened in patch-20260512-155249.log: area had 17 rooms,
    // populated=0 because every representative centroid landed in
    // fog at build time). The build is cheap (one classification per
    // room) — just do them all.
    int populated = 0;
    for (int r = 0; r < roomCount; ++r) {
        Vector rep;
        if (!acc::engine::GetRoomRepresentativeWorld(area, r, rep)) continue;
        char text[128] = {0};
        int  sig = 0;
        if (ClassifyTerrainShape(walls, wallCount, rep,
                                 text, sizeof(text), sig)) {
            std::snprintf(g_room_cache.text[r],
                          sizeof(g_room_cache.text[r]),
                          "%s", text);
            g_room_cache.sig[r] = sig;
            g_room_cache.present[r] = true;
            g_room_cache.rep[r] = rep;
            ++populated;
            acclog::Write("MapCursor",
                          "room %d shape=\"%s\" sig=%d rep=(%.2f,%.2f,%.2f)",
                          r, text, sig, rep.x, rep.y, rep.z);
        }
    }
    // -------------------------------------------------------------
    // Adjacency-based openings override.
    //
    // The walkmesh-probe shape classifier sees one room at a time and
    // can't perceive doorways into adjacent rooms: a room's walkmesh
    // perimeter has walls flanking every doorway, and from a small
    // room's centroid those flanking walls are within 2 m and read as
    // "no opening that direction". patch-20260512-170401.log §17:28
    // example: Room 9 cached as "Kreuzung, Nord, Ost" while it's
    // actually navigable West-to-Room-0 (a 5 m corridor).
    //
    // Path graph fixes this: K1's per-area navigation graph has edges
    // between path_points that span doorways. If a path connection's
    // two endpoints are in different walkmesh rooms, those rooms are
    // adjacent. Compass direction from A's centroid to B's centroid
    // tells us which way A "opens" toward B.
    //
    // We only override Junction / DeadEnd shapes (sig low byte 5 or
    // 6) — corridor and open-area shapes keep their walkmesh-derived
    // text since the width / axis info still matters and the openings
    // list isn't the primary content there.
    uint64_t adjacency[RoomShapeCache::kMaxRooms] = {0};

    uint32_t pathPointsCount = 0;
    void* pathPointsPtr = nullptr;
    uint32_t pathConnectionsCount = 0;
    void* pathConnectionsPtr = nullptr;
    __try {
        auto* abase = reinterpret_cast<unsigned char*>(area);
        pathPointsCount = *reinterpret_cast<uint32_t*>(
            abase + kAreaPathPointsCountOffset);
        pathPointsPtr = *reinterpret_cast<void**>(
            abase + kAreaPathPointsPtrOffset);
        pathConnectionsCount = *reinterpret_cast<uint32_t*>(
            abase + kAreaPathConnectionsCountOffset);
        pathConnectionsPtr = *reinterpret_cast<void**>(
            abase + kAreaPathConnectionsPtrOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pathPointsCount = 0;
    }

    int adjEdges = 0;
    if (pathPointsCount > 0 && pathPointsPtr &&
        pathConnectionsCount > 0 && pathConnectionsPtr) {
        // Cap point count to a sane upper bound — Endar Spire was 51,
        // larger areas reach ~104 per guidance_pathfind comments.
        constexpr uint32_t kMaxPathPoints = 512;
        if (pathPointsCount > kMaxPathPoints) {
            pathPointsCount = kMaxPathPoints;
        }

        // Resolve every path point's containing room in one pass.
        // Stack budget: 512 ints + 512 uint32s + 512 Vectors ≈ 8 KB.
        // Fine for this build path which runs at most once per area.
        int       pointRoom[kMaxPathPoints];
        uint32_t  pointCsr[kMaxPathPoints];
        auto* pointsBase = reinterpret_cast<unsigned char*>(pathPointsPtr);
        for (uint32_t i = 0; i < pathPointsCount; ++i) {
            pointRoom[i] = -1;
            pointCsr[i]  = 0;
            Vector pos;
            __try {
                pos = *reinterpret_cast<Vector*>(
                    pointsBase + i * kPathPointStride +
                    kPathPointPositionOffset);
                pointCsr[i] = *reinterpret_cast<uint32_t*>(
                    pointsBase + i * kPathPointStride +
                    kPathPointCsrOffset);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            int roomIdx = -1;
            acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
            pointRoom[i] = roomIdx;
        }

        // Walk every (i → connections[k]) directed edge. Since the
        // engine's graph is symmetric, we'd see (i, j) and (j, i) as
        // separate edges; mark adjacency on both endpoints either way.
        auto* connBase = reinterpret_cast<uint32_t*>(pathConnectionsPtr);
        for (uint32_t i = 0; i < pathPointsCount; ++i) {
            int roomA = pointRoom[i];
            if (roomA < 0 || roomA >= RoomShapeCache::kMaxRooms) continue;
            uint32_t start = pointCsr[i];
            uint32_t end = (i + 1 < pathPointsCount)
                ? pointCsr[i + 1] : pathConnectionsCount;
            if (start > pathConnectionsCount) continue;
            if (end > pathConnectionsCount) end = pathConnectionsCount;
            for (uint32_t k = start; k < end; ++k) {
                uint32_t j = 0;
                __try {
                    j = connBase[k];
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    continue;
                }
                if (j >= pathPointsCount) continue;
                int roomB = pointRoom[j];
                if (roomB < 0 || roomB >= RoomShapeCache::kMaxRooms) continue;
                if (roomA == roomB) continue;
                if (!(adjacency[roomA] & (1ULL << roomB))) {
                    adjacency[roomA] |= (1ULL << roomB);
                    ++adjEdges;
                }
            }
        }
    }
    acclog::Write("MapCursor",
                  "Adjacency: pathPoints=%u pathConns=%u edges=%d",
                  pathPointsCount, pathConnectionsCount, adjEdges);

    // For each Junction / DeadEnd room, rebuild text + sig from
    // adjacency. Junction kind = 6; DeadEnd kind = 5. Other kinds
    // (corridor, open area, wall) keep their walkmesh-derived text.
    using acc::strings::Id;
    Id cardinalIds[4] = {
        Id::DirNorth, Id::DirEast, Id::DirSouth, Id::DirWest
    };

    int overrides = 0;
    for (int r = 0; r < roomCount; ++r) {
        if (!g_room_cache.present[r]) continue;
        int kind = g_room_cache.sig[r] & 0xff;
        if (kind != 5 && kind != 6) continue;  // only junction / dead-end

        // Bucket each adjacent room into one of 4 cardinals — pick
        // the dominant axis (largest of |dx|, |dy|). Multiple
        // adjacent rooms in the same direction collapse to one bit
        // in the mask.
        int dirMask = 0;
        int adjCount = 0;
        Vector myRep = g_room_cache.rep[r];
        for (int b = 0; b < RoomShapeCache::kMaxRooms; ++b) {
            if (!(adjacency[r] & (1ULL << b))) continue;
            if (b >= roomCount) continue;
            if (!g_room_cache.present[b]) continue;
            ++adjCount;
            float dx = g_room_cache.rep[b].x - myRep.x;
            float dy = g_room_cache.rep[b].y - myRep.y;
            int dirIdx;
            if (std::fabs(dx) > std::fabs(dy)) {
                dirIdx = (dx > 0.0f) ? 1 /*E*/ : 3 /*W*/;
            } else {
                dirIdx = (dy > 0.0f) ? 0 /*N*/ : 2 /*S*/;
            }
            dirMask |= (1 << dirIdx);
        }

        if (adjCount == 0 || dirMask == 0) continue;  // keep walkmesh text

        // Build "Dir1, Dir2, ..." in the order the bits appear (N,E,S,W).
        char dirList[96] = {0};
        size_t dirLen = 0;
        for (int d = 0; d < 4; ++d) {
            if (!(dirMask & (1 << d))) continue;
            const char* name = acc::strings::Get(cardinalIds[d]);
            if (!name || !name[0]) continue;
            if (dirLen > 0 && dirLen + 2 < sizeof(dirList)) {
                dirList[dirLen++] = ',';
                dirList[dirLen++] = ' ';
                dirList[dirLen]   = '\0';
            }
            int n = std::snprintf(dirList + dirLen,
                                  sizeof(dirList) - dirLen, "%s", name);
            if (n > 0) dirLen += static_cast<size_t>(n);
        }

        // Single adjacency → Sackgasse with that direction; multiple
        // → Kreuzung. This re-classifies based on actual navigability
        // (the walkmesh probe may have labelled a 1-exit room as
        // junction; adjacency knows better).
        if (__popcnt(static_cast<unsigned>(dirMask)) == 1) {
            const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
            if (fmt && fmt[0]) {
                std::snprintf(g_room_cache.text[r],
                              sizeof(g_room_cache.text[r]),
                              fmt, dirList);
                g_room_cache.sig[r] = (5) | ((dirMask & 0xff) << 8);
            }
        } else {
            const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
            if (fmt && fmt[0]) {
                std::snprintf(g_room_cache.text[r],
                              sizeof(g_room_cache.text[r]),
                              fmt, dirList);
                g_room_cache.sig[r] = (6) | ((dirMask & 0xff) << 8) |
                                       ((adjCount & 0xff) << 16);
            }
        }
        ++overrides;
        acclog::Write("MapCursor",
                      "room %d adjacency-override → \"%s\" (adjCount=%d)",
                      r, g_room_cache.text[r], adjCount);
    }
    acclog::Write("MapCursor",
                  "Adjacency-override applied to %d rooms", overrides);

    g_room_cache.built = true;
    acclog::Write("MapCursor",
                  "BuildRoomShapeCache: built area=%p rooms=%d populated=%d",
                  area, roomCount, populated);
}

// Read CSWSWaypoint.Tag for diagnostic logging. CExoString at
// kObjectTagOffset (engine_area.h). Returns false if the read faults
// or the tag is empty.
bool ReadWaypointTag(void* waypoint, char* outBuf, size_t bufSize) {
    if (!waypoint || !outBuf || bufSize < 2) return false;
    __try {
        // kObjectTagOffset lives at global scope in engine_area.h between
        // two acc::engine namespace blocks — reference it directly, not
        // through the namespace.
        return acc::engine::ReadCExoString(
            waypoint, kObjectTagOffset, outBuf, bufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
        g_state.active                        = true;
        g_state.last_tick_ms                  = now;
        g_state.last_spoken_waypoint          = nullptr;
        g_state.pending_note_waypoint         = nullptr;
        g_state.pending_note_started_ms       = 0;
        g_state.pending_ambient_kind          = AmbientKind::None;
        g_state.pending_ambient_room_idx      = -1;
        g_state.pending_ambient_started_ms    = 0;
        g_state.last_spoken_ambient_kind      = AmbientKind::None;
        g_state.last_spoken_ambient_room_idx  = -1;
        g_state.last_spoken_ambient_text[0]   = '\0';
        g_state.pending_shape_text[0]         = '\0';
        g_state.pending_shape_signature       = 0;
        acclog::Write("MapCursor",
                      "activated — seed pixel=(%.1f,%.1f) world=(%.2f,%.2f,%.2f)",
                      g_state.px, g_state.py,
                      g_state.world.x, g_state.world.y, g_state.world.z);

        // Speak the area title once per map-open. This is what a sighted
        // player gets from the green banner ("Taris - Südliche
        // Apartments"); blind players need it announced or they lose the
        // "which map am I looking at" anchor.
        void* area = acc::engine::GetCurrentArea();
        if (!g_state.announced_area_name) {
            if (area) {
                char nameBuf[160] = {0};
                if (acc::engine::GetAreaDisplayName(area, nameBuf,
                                                   sizeof(nameBuf)) &&
                    nameBuf[0] != '\0') {
                    tolk::SpeakUrgent(nameBuf);
                    acclog::Write("MapCursor", "area name=\"%s\"", nameBuf);
                }
            }
            g_state.announced_area_name = true;
        }

        // Build the per-room shape cache once per area. If the area is
        // the same as last build, keep the existing cache (cheap when
        // the user re-opens the map repeatedly in the same scene). If
        // the cache wasn't built last time (wall cache wasn't ready
        // yet, build was skipped), try again — the cache is the user's
        // primary stability mechanism for shape descriptions.
        if (area && (area != g_room_cache.area_owner ||
                     !g_room_cache.built)) {
            BuildRoomShapeCache(area, areaMap);
        }
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
    //
    // Cue choice: NavCue::Collision (gui_invdrop), not NavCue::Wall
    // (as_nt_wtrdrip_09). Wall is a quiet 3D ambient water-drip designed
    // to ride underneath gameplay audio at low gain; the map UI runs in
    // a paused sub-screen where the engine attenuates 3D audio further,
    // and the cue silently inaudibles. gui_invdrop is the GUI-path
    // inventory-drop sound, designed to be heard during menu interaction
    // — fits the "UI boundary, not a worldly wall" semantics.
    //
    // Source position: anchor at the player so PlayCue3D's
    // (camera - character) shift produces listener-to-source = 0,
    // sidestepping max_distance attenuation entirely.
    if ((clampedX || clampedY) && (vx != 0.0f || vy != 0.0f)) {
        if (g_state.last_edge_cue_ms == 0 ||
            now - g_state.last_edge_cue_ms > kEdgeCueQuietMs) {
            Vector cuePos;
            if (!acc::engine::GetPlayerPosition(cuePos)) {
                cuePos = g_state.world;  // best-effort fallback
            }
            const char* cueResref =
                acc::audio::GetNavCueResref(acc::audio::NavCue::Collision);
            bool ok = acc::audio::PlayCue3D(cueResref, cuePos,
                                            kEdgeCueGain);
            acclog::Write("MapCursor",
                          "edge cue clampX=%d clampY=%d resref=%s "
                          "cuePos=(%.2f,%.2f,%.2f) gain=%.1f "
                          "PlayCue3D_ok=%d",
                          (int)clampedX, (int)clampedY, cueResref,
                          cuePos.x, cuePos.y, cuePos.z,
                          kEdgeCueGain, (int)ok);
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

    // Resolve the terrain shape via the per-room cache built at map
    // activation. The room cache trades per-pixel granularity for
    // stability — the user explicitly asked for "calculate once on map
    // open, not position-dependent". Look up by the cursor's current
    // room index; if the room wasn't classified at build time (fog at
    // build, then explored later, or the room sits outside cache
    // capacity), fall through to a live per-tick probe so the user
    // still hears something for newly-revealed rooms.
    bool cursorExplored = IsWorldPointExplored(areaMap, g_state.world);
    char shapeTextLocal[128] = {0};
    int  shapeSigLocal = 0;
    bool haveShape = false;
    if (cursorExplored) {
        int cursorRoomIdx = -1;
        void* areaForRoom = acc::engine::GetCurrentArea();
        if (areaForRoom) {
            acc::engine::GetRoomAtIndexed(areaForRoom, g_state.world,
                                          cursorRoomIdx);
        }
        bool cacheUsable = g_room_cache.built &&
                           g_room_cache.area_owner == areaForRoom;
        if (cacheUsable &&
            cursorRoomIdx >= 0 &&
            cursorRoomIdx < RoomShapeCache::kMaxRooms &&
            g_room_cache.present[cursorRoomIdx]) {
            // Direct cache hit — cursor is inside a cached room.
            std::snprintf(shapeTextLocal, sizeof(shapeTextLocal),
                          "%s", g_room_cache.text[cursorRoomIdx]);
            shapeSigLocal = g_room_cache.sig[cursorRoomIdx];
            haveShape = true;
        } else if (cacheUsable) {
            // Cache miss — either cursorRoomIdx == -1 (cursor on a
            // portal seam between rooms), or the cursor's room failed
            // to populate (rooms 6 and 16 in patch-20260512-164059.log
            // — likely null surface_mesh). The previous behaviour was a
            // live per-tick probe at the cursor's world position, which
            // produced new "Offene Fläche" / "Korridor 4m" / single-
            // direction "Kreuzung" labels at each pixel and destroyed
            // orientation — the user pan back-and-forth in a small
            // area hit 6 different shapes for the same physical region.
            //
            // Instead: snap to the nearest cached room's representative
            // point and reuse its already-cached label. Same room from
            // any nearby cursor position; transitions between cached
            // rooms still announce because the nearest changes.
            float bestDist2 = 1e30f;
            int bestRoom = -1;
            for (int r = 0; r < g_room_cache.room_count; ++r) {
                if (!g_room_cache.present[r]) continue;
                float dx = g_room_cache.rep[r].x - g_state.world.x;
                float dy = g_room_cache.rep[r].y - g_state.world.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < bestDist2) {
                    bestDist2 = d2;
                    bestRoom = r;
                }
            }
            if (bestRoom >= 0) {
                std::snprintf(shapeTextLocal, sizeof(shapeTextLocal),
                              "%s", g_room_cache.text[bestRoom]);
                shapeSigLocal = g_room_cache.sig[bestRoom];
                haveShape = true;
            }
        }
    }

    if (hit) {
        // Cursor sits directly on an explicit map-note waypoint. This
        // overrides ambient announce (fog / room label) — the waypoint
        // is the more specific information and the user is clearly
        // pointing at it.
        //
        // Drop any in-flight ambient pending so the next time we leave
        // this waypoint the ambient timer starts fresh, not stale.
        g_state.pending_ambient_kind       = AmbientKind::None;
        g_state.pending_ambient_room_idx   = -1;
        g_state.pending_ambient_started_ms = 0;
        g_state.pending_shape_text[0]      = '\0';
        g_state.pending_shape_signature    = 0;

        if (hit == g_state.last_spoken_waypoint) {
            // Already spoken — keep silence.
            g_state.pending_note_waypoint = nullptr;
            g_state.pending_note_started_ms = 0;
        } else if (hit == g_state.pending_note_waypoint) {
            if (g_state.pending_note_started_ms != 0 &&
                now - g_state.pending_note_started_ms >= kHoverPauseMs) {
                char text[256] = {0};
                bool haveText = ReadWaypointMapNoteText(hit, text,
                                                        sizeof(text)) &&
                                text[0] != '\0';
                char tagBuf[128] = {0};
                bool haveTag = ReadWaypointTag(hit, tagBuf, sizeof(tagBuf));
                if (!haveText) {
                    // Honest fallback per feedback_never_silence_fallback_-
                    // announcement: an enabled map-note pin with empty
                    // localised text would otherwise vanish for blind
                    // players even though the engine still renders the
                    // coloured dot. Speak the generic POI label so the
                    // marker is at least surfaced. Tag goes to the log
                    // for investigation of which waypoints fall here.
                    const char* poi = acc::strings::Get(
                        acc::strings::Id::MapCursorWaypointPOI);
                    if (poi && poi[0]) {
                        std::snprintf(text, sizeof(text), "%s", poi);
                    }
                }
                if (text[0] != '\0') {
                    // Append the surrounding terrain shape when we have
                    // one — landmark/POI first, shape second, separated
                    // by ". " so screen readers parse two sentences.
                    char combined[384];
                    const char* speakStr = text;
                    if (haveShape && shapeTextLocal[0] != '\0') {
                        std::snprintf(combined, sizeof(combined),
                                      "%s. %s", text, shapeTextLocal);
                        speakStr = combined;
                    }
                    // NOW-priority via NVDA SSML — survives the typed-
                    // character cancellation that NORMAL-priority
                    // Tolk_Output suffers from while WASD is held.
                    tolk::SpeakUrgent(speakStr);
                    acclog::Write("MapCursor",
                                  "speak note=\"%s\" tag=\"%s\" "
                                  "shape=\"%s\" haveText=%d "
                                  "cursor=(%.1f,%.1f)",
                                  text, haveTag ? tagBuf : "",
                                  haveShape ? shapeTextLocal : "",
                                  (int)haveText,
                                  g_state.px, g_state.py);
                }
                g_state.last_spoken_waypoint    = hit;
                g_state.pending_note_waypoint   = nullptr;
                g_state.pending_note_started_ms = 0;
                // Crossing into a waypoint resets ambient latching — if
                // the user then leaves back into the same fog/room they
                // came from, the ambient re-announces (waypoint counts
                // as an "intervening event" that re-arms ambient).
                g_state.last_spoken_ambient_kind     = AmbientKind::None;
                g_state.last_spoken_ambient_room_idx = -1;
                g_state.last_spoken_ambient_text[0]  = '\0';
            }
        } else {
            g_state.pending_note_waypoint   = hit;
            g_state.pending_note_started_ms = now;
        }
    } else {
        // Cursor not over a map note. Drop waypoint pending; classify
        // current ambient and run the unified hover-pause.
        if (g_state.pending_note_waypoint != nullptr) {
            g_state.pending_note_waypoint   = nullptr;
            g_state.pending_note_started_ms = 0;
        }
        if (g_state.last_spoken_waypoint != nullptr && moved) {
            // Left a note — re-arm so coming back to the same note
            // announces again.
            g_state.last_spoken_waypoint = nullptr;
        }

        // ---- Classify the current ambient state at the cursor.
        //
        // Order: fog-of-war wins outright (spoiler guard — never speak
        // a room/landmark name for an unrevealed cell, even though the
        // engine has both readily available). For explored cells, try
        // Tier 1 landmark (Bioware-authored map-note label, localized,
        // sparse), then Tier 2 mod-supplied friendly room name. Vanilla
        // resref-style room ids fall through to None and stay silent.
        AmbientKind currentKind    = AmbientKind::None;
        int         currentRoomIdx = -1;

        bool explored = IsWorldPointExplored(areaMap, g_state.world);
        if (!explored) {
            currentKind = AmbientKind::Unexplored;
        } else {
            void* area = acc::engine::GetCurrentArea();
            int roomIdx = -1;
            if (area) {
                acc::engine::GetRoomAtIndexed(area, g_state.world, roomIdx);
            }
            if (roomIdx >= 0) {
                currentRoomIdx = roomIdx;
                const char* landmark =
                    acc::transitions::GetLandmarkForRoom(roomIdx);
                if (landmark) {
                    currentKind = AmbientKind::Landmark;
                } else if (area) {
                    char roomBuf[128] = {0};
                    if (acc::engine::GetRoomDisplayName(
                            area, roomIdx, roomBuf, sizeof(roomBuf)) &&
                        roomBuf[0] != '\0' &&
                        !acc::transitions::IsResrefStyleRoomName(roomBuf)) {
                        currentKind = AmbientKind::RoomName;
                    }
                }
            }
        }

        // Shape was already probed once at the top of the tick into
        // shapeTextLocal / shapeSigLocal / haveShape. If the higher
        // tiers (Landmark / friendly RoomName) found nothing and we
        // have a shape, promote currentKind to TerrainShape so the
        // hover-pause + speak flow handles it. When a higher tier
        // already won (Landmark, RoomName, Unexplored), we keep
        // currentKind as-is — the shape is appended at speak time as
        // a tail sentence rather than replacing the primary kind.
        if (currentKind == AmbientKind::None && haveShape) {
            currentKind = AmbientKind::TerrainShape;
        }

        // For Unexplored, room_idx is meaningless (fog spans the cell
        // grid, not the layout-room partition) — collapse to -1 so the
        // "same as last spoken" comparison treats all fog cells as one
        // category. For Landmark / RoomName, room_idx is part of the
        // identity (different rooms re-announce). For TerrainShape, the
        // signature encodes kind + quantised metric — same comparator,
        // different meaning — so we slot it into the same field.
        int classifyRoomIdx;
        if (currentKind == AmbientKind::Landmark ||
            currentKind == AmbientKind::RoomName) {
            classifyRoomIdx = currentRoomIdx;
        } else if (currentKind == AmbientKind::TerrainShape) {
            classifyRoomIdx = shapeSigLocal;
        } else {
            classifyRoomIdx = -1;
        }

        // Resolve the text we'd speak for this ambient, eagerly. Used
        // both for the text-based dedup overlay (so adjacent rooms
        // with identical labels collapse to one announce) and as the
        // backing text for the speak path (so we don't have to re-
        // resolve it later). For TerrainShape: shapeTextLocal already
        // built. For Landmark / RoomName / Unexplored: look up here.
        char currentAmbientText[128] = {0};
        switch (currentKind) {
            case AmbientKind::Unexplored: {
                const char* t = acc::strings::Get(
                    acc::strings::Id::MapCursorUnexplored);
                if (t) std::snprintf(currentAmbientText,
                                     sizeof(currentAmbientText), "%s", t);
                break;
            }
            case AmbientKind::Landmark: {
                const char* lm = acc::transitions::GetLandmarkForRoom(
                    currentRoomIdx);
                if (lm) std::snprintf(currentAmbientText,
                                      sizeof(currentAmbientText), "%s", lm);
                break;
            }
            case AmbientKind::RoomName: {
                void* a = acc::engine::GetCurrentArea();
                if (a) acc::engine::GetRoomDisplayName(
                    a, currentRoomIdx,
                    currentAmbientText, sizeof(currentAmbientText));
                break;
            }
            case AmbientKind::TerrainShape:
                std::snprintf(currentAmbientText,
                              sizeof(currentAmbientText), "%s",
                              shapeTextLocal);
                break;
            case AmbientKind::None:
                break;
        }

        // Dedup overlay: text-equality OR (kind, sig) equality.
        // The text path collapses adjacent rooms whose cache labels
        // happen to be identical (multiple "Kreuzung, Nord, Ost"
        // rooms in Taris South Apartments). The sig path stays as a
        // belt-and-braces match for the existing flow.
        bool sameAsLastSpoken =
            currentKind == g_state.last_spoken_ambient_kind &&
            (classifyRoomIdx == g_state.last_spoken_ambient_room_idx ||
             (currentAmbientText[0] != '\0' &&
              strcmp(currentAmbientText,
                     g_state.last_spoken_ambient_text) == 0));
        // sameAsPending uses *kind-only* match for TerrainShape. Strict
        // (kind+signature) match prevented re-announce after a Nebel/
        // waypoint: as the cursor sweeps a curved corridor the quantised
        // width drifts metre-by-metre (8→7→8→6…), each drift looks like
        // a new signature, the 300 ms timer keeps resetting, and no
        // announce ever fires. With kind-only the timer survives
        // sig drift; the current probe's text is refreshed onto pending
        // each tick so the speak path uses up-to-date geometry. Other
        // kinds (Landmark, RoomName) keep strict sig-match — moving
        // between two named rooms should re-announce.
        bool sameAsPending =
            currentKind == g_state.pending_ambient_kind &&
            (currentKind == AmbientKind::TerrainShape ||
             classifyRoomIdx == g_state.pending_ambient_room_idx);

        if (currentKind == AmbientKind::None) {
            // Nothing to say — clear pending. Also clear last_spoken so
            // the next named ambient (fog, landmark, friendly room,
            // terrain shape) gets re-armed. Pre-terrain-shape, vanilla
            // KOTOR areas spent most of their map area in None; with
            // shape probing those now resolve to TerrainShape almost
            // everywhere, so None mostly means "explored cell but wall
            // cache not built yet" or "Unexplored already announced
            // and we're inside the same fog cell".
            g_state.pending_ambient_kind         = AmbientKind::None;
            g_state.pending_ambient_room_idx     = -1;
            g_state.pending_ambient_started_ms   = 0;
            g_state.last_spoken_ambient_kind     = AmbientKind::None;
            g_state.last_spoken_ambient_room_idx = -1;
            g_state.last_spoken_ambient_text[0]  = '\0';
            g_state.pending_shape_text[0]        = '\0';
            g_state.pending_shape_signature      = 0;
        } else if (sameAsLastSpoken) {
            // Already announced this exact ambient zone; stay silent
            // until something else interrupts (waypoint, different
            // room, fog↔explored flip).
            g_state.pending_ambient_kind       = AmbientKind::None;
            g_state.pending_ambient_room_idx   = -1;
            g_state.pending_ambient_started_ms = 0;
        } else if (sameAsPending) {
            // Keep pending text in sync with the current probe so the
            // speak path describes the cursor's *current* corridor /
            // junction / dead-end, not whatever shape happened to be
            // under the cursor 300 ms ago when the timer started. The
            // arm timer is NOT reset — it keeps counting toward speak.
            if (currentKind == AmbientKind::TerrainShape &&
                shapeTextLocal[0] != '\0') {
                std::snprintf(g_state.pending_shape_text,
                              sizeof(g_state.pending_shape_text),
                              "%s", shapeTextLocal);
                g_state.pending_shape_signature = shapeSigLocal;
            }
            if (g_state.pending_ambient_started_ms != 0 &&
                now - g_state.pending_ambient_started_ms >= kHoverPauseMs) {
                // Hover-pause elapsed — resolve the text to speak.
                // RoomName re-reads GetRoomDisplayName at speak time so
                // the buffer's lifetime is tick-local. TerrainShape
                // reads the description we built when arming.
                const char* text = nullptr;
                char roomBuf[128] = {0};
                switch (currentKind) {
                    case AmbientKind::Unexplored:
                        text = acc::strings::Get(
                            acc::strings::Id::MapCursorUnexplored);
                        break;
                    case AmbientKind::Landmark:
                        text = acc::transitions::GetLandmarkForRoom(
                            currentRoomIdx);
                        break;
                    case AmbientKind::RoomName: {
                        void* area = acc::engine::GetCurrentArea();
                        if (area && acc::engine::GetRoomDisplayName(
                                area, currentRoomIdx,
                                roomBuf, sizeof(roomBuf)) &&
                            roomBuf[0] != '\0') {
                            text = roomBuf;
                        }
                        break;
                    }
                    case AmbientKind::TerrainShape:
                        if (g_state.pending_shape_text[0] != '\0') {
                            text = g_state.pending_shape_text;
                        }
                        break;
                    case AmbientKind::None:
                        break;
                }
                if (text && text[0] != '\0') {
                    // Append the shape tail when the primary tier is
                    // Landmark or RoomName (the user explicitly wanted
                    // both spoken — landmark/room first, then shape).
                    // For TerrainShape pure the primary text IS the
                    // shape; for Unexplored we never expose layout.
                    char combined[384];
                    const char* speakStr = text;
                    if (haveShape && shapeTextLocal[0] != '\0' &&
                        (currentKind == AmbientKind::Landmark ||
                         currentKind == AmbientKind::RoomName)) {
                        std::snprintf(combined, sizeof(combined),
                                      "%s. %s", text, shapeTextLocal);
                        speakStr = combined;
                    }
                    // Same Prism+SAPI urgent path the waypoint announce
                    // uses — survives NVDA's typed-character cancel
                    // while the user holds WASD.
                    tolk::SpeakUrgent(speakStr);
                    acclog::Write("MapCursor",
                                  "speak ambient kind=%s key=%d "
                                  "text=\"%s\" shape=\"%s\" "
                                  "cursor=(%.1f,%.1f)",
                                  AmbientKindStr(currentKind),
                                  classifyRoomIdx, text,
                                  haveShape ? shapeTextLocal : "",
                                  g_state.px, g_state.py);
                }
                g_state.last_spoken_ambient_kind     = currentKind;
                g_state.last_spoken_ambient_room_idx = classifyRoomIdx;
                std::snprintf(g_state.last_spoken_ambient_text,
                              sizeof(g_state.last_spoken_ambient_text),
                              "%s", currentAmbientText);
                g_state.pending_ambient_kind         = AmbientKind::None;
                g_state.pending_ambient_room_idx     = -1;
                g_state.pending_ambient_started_ms   = 0;
                g_state.pending_shape_text[0]        = '\0';
                g_state.pending_shape_signature      = 0;
            }
        } else {
            // New ambient category (or new room within
            // Landmark/RoomName, or new shape signature) — arm the
            // hover-pause. For TerrainShape we also stash the rendered
            // description so the speak path doesn't have to re-probe.
            g_state.pending_ambient_kind       = currentKind;
            g_state.pending_ambient_room_idx   = classifyRoomIdx;
            g_state.pending_ambient_started_ms = now;
            if (currentKind == AmbientKind::TerrainShape) {
                std::snprintf(g_state.pending_shape_text,
                              sizeof(g_state.pending_shape_text),
                              "%s", shapeTextLocal);
                g_state.pending_shape_signature = shapeSigLocal;
            } else {
                g_state.pending_shape_text[0]   = '\0';
                g_state.pending_shape_signature = 0;
            }
        }
    }
}

}  // namespace acc::map_ui_cursor
