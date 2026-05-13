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
#include "log.h"
#include "region_classifier.h"        // shared shape cache + LookupShapeAt
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

// Shape cache and classifier moved to acc::region (region_classifier.h)
// in 2026-05-13's refactor — same code, single source of truth, built
// once on area-enter by transitions::Tick so the cursor inherits a
// ready cache the moment the map opens. Lookups happen via
// acc::region::LookupShapeAt / LookupRoomShape below.

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

        // Ensure the shared shape cache is built for this area. Normally
        // transitions::Tick has already built it on area-enter; this
        // call is the safety net for cases where the map opens before
        // transitions has had a chance to run (e.g. map-button rebound
        // to a moment immediately after area-load) or where the wall
        // cache wasn't ready when transitions tried. BuildCacheForArea
        // is idempotent on the same area pointer.
        if (area) {
            acc::region::BuildCacheForArea(area);
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

    // Resolve the terrain shape via the shared region cache (built once
    // on area-enter by transitions::Tick). Same stability story as the
    // pre-extraction code: one classification per .lyt-room, snap to the
    // nearest cached room on portal-seam / null-mesh misses, so the user
    // gets a deterministic label per region rather than per pixel.
    // Fog-of-war gate applied first — never expose a shape label for a
    // cell the player hasn't revealed.
    bool cursorExplored = IsWorldPointExplored(areaMap, g_state.world);
    char shapeTextLocal[128] = {0};
    int  shapeSigLocal = 0;
    bool haveShape = false;
    if (cursorExplored) {
        void* areaForRoom = acc::engine::GetCurrentArea();
        if (areaForRoom) {
            haveShape = acc::region::LookupShapeAt(
                areaForRoom, g_state.world,
                shapeTextLocal, sizeof(shapeTextLocal),
                shapeSigLocal, /*outRoomIdx=*/nullptr);
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
