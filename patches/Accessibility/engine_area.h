// Per-area object iteration + room lookup. SEH-guarded; no engine
// re-entry beyond CSWSArea::GetRoom.
//
// CSWSArea:
//   +0x190 game_objects      ulong*  — HANDLE array (not CSWSObject**).
//   +0x194 game_object_count int
//   +0x230 rooms             CSWSRoom[] inline, stride 0x4c
//   +0x25c room_names        CExoString*
//
// CSWSArea::GetRoom @0x4bb600(this, Vector*, int* outRoomIndex /*nullable*/)
//   → CSWSRoom*
//
// CSWSObject base:
//   +0x008 object_kind  uint8 GAME_OBJECT_TYPES
//   +0x090 position     Vector
//
// Handle resolution chain:
//   *kAddrAppManagerPtr → AppManager → +0x4 CClientExoApp* /
//     +0x8 CServerExoApp*. The server path is used for object
//     resolution (CSWSObject::GetArea @0x4cb120 confirms +0x8 via
//     `mov ecx,[eax+0x8]`).
//   CServerExoApp::GetObjectArray @0x004aed70 → CGameObjectArray*.
//   CGameObjectArray::GetGameObject(id, &outPtr) @0x004d8230 → bool.
//
// game_objects[] is an array of handles, not pointers. Initial code
// treated entries as CSWSObject** and got kind-byte garbage; iteration
// now resolves each handle through GetGameObject.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::engine {

// GAME_OBJECT_TYPES at CSWSObject +0x8. Do not reorder — engine values.
enum class GameObjectKind : int {
    Area         = 4,
    Creature     = 5,
    Item         = 6,
    Trigger      = 7,
    Projectile   = 8,
    Placeable    = 9,
    Door         = 10,
    AreaOfEffect = 11,
    Waypoint     = 12,
    Encounter    = 13,
    Store        = 14,
    Sound        = 16,
};

// Convenience wrapper around engine_player::GetPlayerArea.
void* GetCurrentArea();

// -1 on null / fault. Reads ONE byte — the field is uint8 (max 0x10).
// Reading 4 bytes works "by luck" for objects whose three trailing
// bytes happen to be zero; for the rest the high bytes carry adjacent
// field data and the wide read fails every kind comparison.
int GetObjectKind(void* gameObject);

// CGameObject.id @+0x4. Inverse of ResolveServerObjectHandle. 0 on
// null/fault.
uint32_t GetObjectHandle(void* gameObject);

// Two independent handle namespaces. Both return nullptr for invalid
// sentinels (0, 0xFFFFFFFF, kInvalidObjectId 0x7F000000); both SEH-guard.
//
// Server-side: AppManager → CServerExoApp → GetObjectArray →
// GetGameObject. Same chain AreaObjectIterator uses. Handles look like
// 0x000000XX.
void* ResolveServerObjectHandle(uint32_t handle);

// Client-side: AppManager → CClientExoApp → GetGameObject → CSWCObject*,
// then +0xf8 server_object → CSWSObject*. LastTarget handles (high bit
// set, 0x800000XX) need this path; the server-side CGameObjectArray
// won't find them.
void* ResolveClientObjectHandle(uint32_t handle);

// Same first step WITHOUT the +0xf8 chain — returns the CLIENT object
// (CSWCObject* / CSWCCreature*) for a high-bit client handle. Needed by engine
// calls that operate on the client creature directly (e.g. dialogue initiation
// via CSWCCreature::ActionInitiateDialog). nullptr on sentinel / miss / fault.
void* ResolveClientObject(uint32_t handle);

// CSWSObject +0x90. False on null / fault.
bool GetObjectPosition(void* gameObject, Vector& out);

// CSWSArea::GetRoom @0x4bb600. Captures the room index the engine
// resolved (GetRoom's third arg). outIndex = -1 on miss/fault; key into
// room_names[]. nullptr outside any room.
void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex);

// Centroid of the middle face in world space. Input for per-room
// walkmesh probes (terrain shape classification etc.).
//
// outFailReason codes: 0=ok, 1=bad args, 2=roomIdx≥roomCount,
// 3=rooms ptr null, 4=surface_mesh null ("void" rooms — skybox/
// placeholders without walkmesh), 5=vertices/faces empty, 6=SEH.
bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld,
                                int* outFailReason = nullptr);

// CSWSArea.name CExoLocString @+0x150 (inline → TLK strref @+0x154);
// falls back to CSWSArea.tag CExoString @+0x158 (modder-assigned).
bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize);

// CSWSArea.room_names[index] — CExoString* array, stride 8. NOT
// localized — these are .lyt-room ids like "m02_03e" / "stunt_01_main".
// The transitions consumer wraps with a "Room:" prefix.
bool GetRoomDisplayName(void* area, int roomIndex,
                        char* outBuf, size_t bufSize);

// Per-kind localized name lookup:
//   Door      CSWSDoor.loc_name           @+0x39c
//   Creature  CSWSCreature.creature_stats @+0xa74 → first_name @+0x14
//   Placeable CSWSPlaceable.loc_name      @+0x228
//   Item      CSWSItem.localized_name     @+0x280
//   Waypoint  CSWSWaypoint.localized_name @+0x238
//   Trigger   CSWSTrigger.localized_name  @+0x228
//
// CExoLocString byte-matches CExoString; ExtractTextOrStrRef tries
// inline then TLK strref at +4. Final fallback CSWSObject.tag @+0x18
// (modder-assigned id).
bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize);

// CSWSObject.tag CExoString @+0x18 — the modder-assigned, locale-INDEPENDENT
// object id (e.g. "tar03_mission031"). Unlike GetObjectName this never
// localizes and never falls back; it's the raw tag or nothing. This is the
// stable identity component used for the discovery-cycling persistence key.
// False on empty / fault; outBuf NUL-terminated on entry.
bool GetObjectTag(void* gameObject, char* outBuf, size_t bufSize);

// CSWSArea.tag CExoString @+0x158 — the area's modder-assigned id. NOTE: in
// practice this is almost always the GFF default "untitled" (modders rarely set
// area Tag), so it is NOT a usable per-area key. Prefer GetCurrentAreaResName.
// False on empty / fault; outBuf NUL-terminated on entry.
bool GetAreaTag(void* area, char* outBuf, size_t bufSize);

// Current module's resource name via CSWSModule::GetModuleResourceName
// @0x004c4b80 — the stable, language-INDEPENDENT module/area resref (e.g.
// "manm26aa"). KOTOR modules are single-area, so this is the canonical per-area
// id (unlike the area GFF Tag, which defaults to "untitled"). Walks the server
// module chain; no `area` arg needed. False on no module / fault; outBuf
// NUL-terminated on entry.
bool GetCurrentAreaResName(char* outBuf, size_t bufSize);

// CServerExoApp::GetLoadFromSaveGame @0x004af050 — the engine's own "I am
// loading a saved game" flag (CServerExoAppInternal.load_from_savegame).
// Set true by CSWGuiSaveLoad::LoadGame / CGuiInGame::DoQuickLoad for the
// duration of any save-game load (main-menu OR in-game OR F9 quickload),
// and held false during a plain save and during normal play. The clean
// signal for suppressing the message-buffer replay + in-game-GUI
// reconstruction burst the engine produces while restoring a save. False
// on no server app / fault.
bool IsLoadingSaveGame();

// Wraps CClientExoApp::GetObjectName(ulong, CExoString*) — the engine's
// universal accessor. Better than GetObjectName(obj,...) from a handle
// because it handles empty first_name with the template + appearance.2da
// + racialtypes.2da fallbacks. Routes client/server handles correctly.
// outBuf NUL-terminated on entry.
bool GetObjectDisplayNameByHandle(uint32_t handle,
                                  char* outBuf, size_t bufSize);

// Pillar 4 sub-state predicates (refine the kind filter):
//   IsUsablePlaceable    CSWSPlaceable.usable @+0x328 OR
//                        has_inventory @+0x324. Drops scenery placeables.
//   IsLandmarkWaypoint   CSWSWaypoint.has_map_note @+0x228.
//   IsTransitionTrigger  CSWSTrigger.transition_destination @+0x30c
//                        (Vector — non-zero indicates set).
bool IsUsablePlaceable(void* placeable);
bool IsLandmarkWaypoint(void* waypoint);
bool IsTransitionTrigger(void* trigger);

// True only when `gameObject` is a loot container (kind == Placeable AND
// has_inventory != 0) whose CItemRepository currently holds zero items —
// i.e. a chest/footlocker/corpse the player has already emptied (or that
// spawned empty). Self-gates on kind so it's safe to call for any object;
// returns false for creatures, doors, pure usable placeables (switches /
// computer panels with has_inventory == 0), and on any fault. Read live
// at narration time, not cached — looting changes the count.
bool IsEmptyContainer(void* gameObject);

// CSWSDoor.open_state +0x2cc. state >= 1 covers both opening anim and
// fully open. False on null/fault/state==0.
bool IsDoorOpen(void* serverDoor);

// Derived from CSWSDoor.generic_type +0x2a1 via a static 65-entry table
// (genericdoors.2da ⋈ placeableobjsnds.2da on soundapptype). Rows 0-12
// don't set soundapptype; classified by label keyword. Metal is the
// fallback for unknown indices.
enum class DoorMaterial { Metal, Wood, Stone };

// Metal on null / fault / out-of-range.
DoorMaterial GetDoorMaterial(void* serverDoor);

// CSWSWaypoint.map_note_enabled +0x22c. Engine's fog-of-war gate for
// map-note text — disabled until the player discovers via map-pin trigger.
bool IsMapNoteEnabled(void* waypoint);

// AppManager → CServerExoApp → GetModule → CSWSModule.area_map @+0x218.
// Shared by map_ui_cursor (cursor projection + fog) and cycle_state
// (map-context fog filter).
void* GetAreaMap();

// CSWSAreaMap::IsWorldPointExplored @0x00579210. Used to filter map
// cycle output so we don't narrate undiscovered landmarks.
bool IsWorldPointExplored(void* areaMap, const Vector& pos);

// CSWSAreaMap::GetMapRotateCCWFromWorldOrientation @0x00578ed0.
// Engine runs Yaw(orientation) + 0/90/180/270 (per area map's own
// orientation tag). Returns the rotation to apply to the compass-arrow
// sprite (player facing in map-local space, CCW-from-+X like engine
// yaw). Callers convert to compass-frame via EngineYawToCompass.
// Engine returns x87 float10 via ST(0); wrapper casts to float.
bool GetMapRotateCCWFromWorldOrientation(void* areaMap,
                                         const Vector& orientation,
                                         float& outDegCCW);

// Back-pointer at CSWSArea +0x2d0. CSWCArea owns the map-pin array
// (quest objective markers, NWScript-placed pins). Mirror set at
// area-load.
void* GetClientArea(void* serverArea);

int GetMapPinCount(void* clientArea);

// Pointer-array semantics (CSWCMapPin**, 4-byte stride, each slot a
// heap-allocated 0x110-byte pin). Lane's PlaceHolder typing as a
// singular pointer is misleading.
void* GetMapPinAt(void* clientArea, int i);

// CSWCMapPin embeds CSWCObject at +0; position at +0x24 (standard
// CGameObject offset). Z is the live server-written value.
bool GetMapPinPosition(void* mapPin, Vector& out);

// CSWCMapPin.reference_number +0x108 (the engine's GetMapPin lookup key).
// NOT a flag bitfield: for engine map-note pins this is the source
// waypoint's CLIENT object id (set by HandleServerToPlayerMapPinEnabled /
// ...ReferenceNumber), which always carries the 0x80000000 client-namespace
// high bit. So it CANNOT discriminate the mod's saved markers from engine
// note pins — use acc::map_user_markers::IsUserMarkerPin (identity) for
// that. Kept as a raw field accessor; no current caller.
uint32_t GetMapPinFlags(void* mapPin);

// CSWCMapPin.enabled +0xfc. SetMapPinEnabled toggles without removing
// the array slot — filter callers check before surfacing text.
bool IsMapPinEnabled(void* mapPin);

// CSWCMapPin.note_text CExoString @+0x100. Set by the wire packet
// handler after operator_new(0x110).
bool GetMapPinNoteText(void* mapPin, char* outBuf, size_t bufSize);

// Replicates the engine pattern from
// HandleServerToPlayerMapPinReferenceNumber @0x652d60:
//   operator_new(0x110) → CSWCMapPin ctor → field writes (pos +0x24,
//   enabled +0xfc, note_text via CExoString::operator= +0x100, flags
//   +0x108, subtype +0x10c) → CSWCArea::AddMapPin @0x606d90.
//
// referenceNumber keys SetMapPinEnabled / GetMapPin. The engine uses a
// per-player counter from 1; we reserve high-half range to avoid collisions.
//
// `name` is copied into a heap CExoString via the engine's operator= —
// matched to ~CSWCMapPin's free.
//
// In-area only. Persistence (NW_MAP_PIN_*_{N} ScriptVarTable strings)
// is a separate follow-up; for the saved-marker hotkey, in-area is
// acceptable. Pins vanish on area transition.
bool CreateMapPin(void*       clientArea,
                  const Vector& pos,
                  const char* name,
                  uint32_t    referenceNumber,
                  void**      outPin = nullptr);

// CSWSWaypoint.map_note CExoLocString @+0x230 (strref @+0x234).
// BioWare-authored display labels ("Bridge", "Cargo Hold", ...).
bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize);

// Snapshots data pointer + size at construction. The engine rebuilds
// game_objects on area-load, never mid-frame, so single-tick scans are
// safe. Per-Next() resolution via cached CGameObjectArray*. All reads
// SEH-guarded; construction fault yields immediate exhaustion.
class AreaObjectIterator {
public:
    explicit AreaObjectIterator(void* area);

    // Skips 0-handle slots + handles GetGameObject rejects (engine
    // treats some as sentinel during area-unload bookkeeping).
    void* Next();

    int   SnapshotSize() const { return size_; }

private:
    uint32_t* handles_;
    int       size_;
    int       index_;
    void*     objectArray_;  // CGameObjectArray* (server-side master table)
};

}  // namespace acc::engine

// CSWSArea::GetRoom — __thiscall. Third arg is an int* outRoomIndex
// (NULL-passable per PositionWalkable's decomp).
constexpr uintptr_t kAddrCSWSAreaGetRoom = 0x004BB600;

// Handle-resolution chain (server-side master object table).
constexpr size_t    kAppManagerServerOffset      = 0x8;
constexpr uintptr_t kAddrCServerExoAppGetObjectArray = 0x004AED70;
constexpr uintptr_t kAddrCGameObjectArrayGetGameObject = 0x004D8230;

// Client-side resolver: CClientExoApp::GetGameObject(ulong) -> CSWCObject*.
// Direct one-call wrapper around the client-side game object array (the
// inner CGameObjectArray pointer is held inside the CClientExoApp
// instance; we never need to touch it directly because GetGameObject
// hides the array layer). Verified live 2026-05-04 — see
// ResolveClientObjectHandle's docs.
constexpr uintptr_t kAddrCClientExoAppGetGameObject = 0x005ED580;

// Map + fog-of-war chain.
constexpr uintptr_t kAddrCServerExoAppGetModule          = 0x004AE6B0;
constexpr size_t    kModuleAreaMapOffset                 = 0x218;
constexpr uintptr_t kAddrCSWSAreaMapIsWorldPointExplored = 0x00579210;
// __thiscall(this, Vector by value). Returns float10 via ST(0).
// BYTES_PURGED=12 (callee pops 3-float Vector).
constexpr uintptr_t kAddrCSWSAreaMapGetMapRotateCCW       = 0x00578ED0;

// CSWCMapPin allocation chain. operator_new at 0x43e1b0 is matched to
// the _free that CExoString::operator= and ~CSWCMapPin invoke.
constexpr uintptr_t kAddrOperatorNew                = 0x0043E1B0;  // __cdecl(ulong)
constexpr uintptr_t kAddrCSWCMapPinCtor             = 0x00692540;
constexpr uintptr_t kAddrCExoStringAssignFromCString= 0x005E5140;  // __thiscall(CExoString*, char*)
constexpr uintptr_t kAddrCSWCAreaAddMapPin          = 0x00606D90;  // __thiscall(CSWCArea*, pin)

// Server→client back-pointer (CSWSArea ends at +0x2d0 preceded by
// CPathfindInformation* at +0x2cc).
constexpr size_t kAreaClientAreaBackOffset = 0x2d0;

// CSWCArea.map_pins (pointer array; 4-byte stride confirmed via
// AddMapPin / ClearAllMapPins / GetMapPin decomps).
constexpr size_t kClientAreaMapPinsOffset       = 0x1c4;
constexpr size_t kClientAreaMapPinsCountOffset  = 0x1c8;
constexpr size_t kClientAreaMapPinsCapOffset    = 0x1cc;

constexpr size_t kMapPinPositionOffset = 0x24;   // Vector (CGameObject base)
constexpr size_t kMapPinEnabledOffset  = 0xfc;   // int
constexpr size_t kMapPinNoteTextOffset = 0x100;  // CExoString
// Literal (kCExoStringStride is forward-declared below) — every
// CExoString in this header pairs strref at +0x4.
constexpr size_t kMapPinNoteStrrefOffset = kMapPinNoteTextOffset + 0x4;
constexpr size_t kMapPinFlagsOffset    = 0x108;  // uint32 reference-number / quest bitfield
constexpr size_t kMapPinSubtypeOffset  = 0x10c;  // int (1 = user-placed note pin)

// CSWSArea offsets. Lane's SARIF (CSWSArea SIZE=0x2d4).
constexpr size_t kAreaGameObjectsOffset      = 0x190;
constexpr size_t kAreaGameObjectCountOffset  = 0x194;
constexpr size_t kAreaRoomsOffset            = 0x230;  // CSWSRoom* (deref first)
constexpr size_t kAreaNameLocOffset          = 0x150;  // CExoLocString
constexpr size_t kAreaTagOffset              = 0x158;  // CExoString fallback
constexpr size_t kAreaRoomNamesOffset        = 0x25c;  // CExoString*
constexpr size_t kAreaRoomCountOffset        = 0x268;  // ulong
constexpr size_t kCExoStringStride           = 0x8;

constexpr size_t kRoomStride = 0x4c;

// CSWSObject base. kServerObjectPositionOffset (0x90) lives in engine_player.h.
constexpr size_t kObjectKindOffset = 0x8;   // uint8 GAME_OBJECT_TYPES
constexpr size_t kObjectTagOffset  = 0x18;  // CExoString fallback id

// Per-subclass localized-name offsets (CExoLocString unless noted).
constexpr size_t kDoorLocNameOffset            = 0x39c;
constexpr size_t kDoorGenericTypeOffset        = 0x2a1;  // byte → genericdoors.2da row
constexpr size_t kDoorLockedOffset             = 0x2c4;  // undefined4 (bool)
constexpr size_t kDoorOpenStateOffset          = 0x2cc;  // byte
constexpr size_t kDoorDescriptionOffset        = 0x3a4;
constexpr size_t kDoorStaticOffset             = 0x3c0;  // undefined4 (UTD Static flag)
constexpr size_t kDoorTransitionDestOffset     = 0x3c8;
constexpr size_t kCreatureStatsPtrOffset       = 0xa74;  // CSWSCreatureStats*
constexpr size_t kCreatureStatsFirstNameOffset = 0x14;
constexpr size_t kPlaceableLocNameOffset       = 0x228;
constexpr size_t kItemLocNameOffset            = 0x280;
constexpr size_t kWaypointLocNameOffset        = 0x238;
constexpr size_t kTriggerLocNameOffset         = 0x228;

// Pillar 4 sub-state.
constexpr size_t kPlaceableUsableOffset        = 0x328;  // "Useable" GFF flag
// "HasInventory" GFF flag. Decompiling CSWSPlaceable::LoadPlaceable shows
// ReadFieldBYTE("HasInventory") is stored to +0x324 (the Ghidra struct
// mislabels +0x334 as has_inventory — that field is something else and
// reads 0 even on real loot containers). CSWSPlaceable::OpenInventory
// gates the container-GUI open on this same +0x324 != 0, then derefs
// item_repository, confirming it as the authoritative "is a lootable
// container" flag.
constexpr size_t kPlaceableHasInventoryOffset  = 0x324;
// CSWSPlaceable.item_repository @+0x36c → CItemRepository. The repo's
// live item count sits at +0x10 (items_list @+0xc). Confirmed by
// decompiling CItemRepository::GetItemInRepository / ItemListGetItem /
// CalculateContentsWeight — all loop `i < this->item_count` over
// `items_list[i]`. Reading the count is a single dword load, so the
// emptiness test is O(1) (no list walk, no per-item handle resolve).
constexpr size_t kPlaceableItemRepositoryOffset = 0x36c;
constexpr size_t kItemRepositoryItemCountOffset = 0x10;
constexpr size_t kWaypointHasMapNoteOffset     = 0x228;
constexpr size_t kTriggerTransitionDestOffset  = 0x30c;  // Vector

// BioWare-authored map-note labels (CSWSWaypoint SIZE=0x240).
constexpr size_t kWaypointMapNoteEnabledOffset = 0x22c;
constexpr size_t kWaypointMapNoteLocOffset     = 0x230;

// Walkmesh wall-edge extraction. Pillar 1 foundation.
//
// CSWSArea.rooms @+0x230 → CSWSRoom*, stride 0x4c, count @+0x268.
// CSWSRoom +0x3c → CSWRoomSurfaceMesh*.
// CSWRoomSurfaceMesh:
//   +0x00 CSWCollisionMesh mesh (embedded 0x88 bytes)
//   +0x88 SurfaceMeshAdjacency* adjacencies (face_count entries)
//   +0x8c CExoArrayList<SurfaceMeshEdge> edges
// CSWCollisionMesh:
//   +0x04 world_coords (int, 1 = vertices already world-space)
//   +0x50 vertex_count, +0x54 vertices (Vector*)
//   +0x58 face_count,   +0x60 face_indices (3 ulongs/face)
//   +0x64 materials (ulong* → surfacemat.2da)
// SurfaceMeshAdjacency.indices[3]: -1 = perimeter (WALL); else edge_id,
// face_id = edge_id / 3.
//
// CSWCollisionMesh::LocalToWorld @0x596aa0 __thiscall(this, out, local).
// BYTES_PURGED=8. Short-circuits when world_coords != 0; we always call.

constexpr size_t kRoomSurfaceMeshOffset            = 0x3c;
constexpr size_t kCollisionMeshVerticesOffset      = 0x54;
constexpr size_t kCollisionMeshFaceCountOffset     = 0x58;
constexpr size_t kCollisionMeshFacesOffset         = 0x60;
constexpr size_t kCollisionMeshMaterialsOffset     = 0x64;
constexpr size_t kSurfaceMeshAdjacenciesOffset     = 0x88;

constexpr size_t kWalkmeshFaceStride               = 0xc;   // 3 × ulong

constexpr uintptr_t kAddrCollisionMeshLocalToWorld = 0x00596aa0;

namespace acc::engine {

// One perimeter walkmesh edge — a triangle side that has no neighbour
// (`SurfaceMeshAdjacency.indices[e] == -1`) and therefore corresponds to a
// physical wall in the room. Endpoints are in WORLD space (LocalToWorld
// already applied at extraction time, so consumers do not need the room's
// CSWCollisionMesh anymore — the edge is self-contained).
struct WallEdge {
    Vector a;
    Vector b;
    int    room_id;       // CSWSArea-local room index (0..room_count-1)
    int    material_id;   // surfacemat.2da row id for the face this edge bounds
};

// Walks every room in `area`, then every face in each room's
// CSWRoomSurfaceMesh, and emits one WallEdge for each triangle side whose
// adjacency is `-1` (perimeter / wall sentinel). Endpoints are transformed
// into world space via CSWCollisionMesh::LocalToWorld so callers can compute
// distances against the player's world position directly.
//
// Seam filtering: when called with a buffer (outBuf != nullptr), the
// raw per-room scan is followed by a pairwise pass that drops "edges"
// where two different rooms emit the same world-space endpoints. K1
// joins rooms through portals/AABB rather than via triangle adjacency,
// so room-boundary triangles in both rooms carry adjacency=-1 and would
// otherwise appear as walls — even though the engine considers them
// walkable. This pass eliminates those phantom walls. Endpoint match
// uses ~1cm tolerance for LocalToWorld float variance.
//
// outBuf non-null + maxEdges > 0: writes up to min(emitted, maxEdges)
//   post-filter edges; returns post-filter count (REAL walls).
// outBuf null OR maxEdges <= 0: returns pre-filter DISCOVERED count
//   for buffer-sizing telemetry. filtered ≤ discovered.
//
// SEH-guarded per room; faults skip that room. Walkmesh is immutable
// per area-load (doors are separate collision meshes), so cache once.
int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges);

// Same-floor z tolerance for the segment-vs-wall test below (world units
// = metres). A wall edge whose z at the crossing point differs from the
// ray's z by more than this is on another floor and is not a blocker —
// see the 3D guard in SegmentCrossesWalkmesh. 2m clears step/slope edge
// authoring (~2.25m faces dedup elsewhere) while excluding genuine
// inter-deck stacks. Confirmed against the engine nav graph: a 3D-aware
// crosscheck found zero nav-edge/wall crossings across 6 diverse areas
// (wall cache is phantom-free), and every runtime over-block occurred at
// elevated z against ground-floor walls — i.e. pure 2D-projection noise
// this tolerance removes. Shared with wall_topology::LogNavWallCrossings.
constexpr float kWallCrossZToleranceM = 2.0f;

// Segment-vs-perimeter test in the XY plane, with an optional 3D guard: a
// wall is only a blocker when its edge sits within kWallCrossZToleranceM of
// the ray at the crossing point. Without the guard, a wall on a different
// floor whose 2D projection happens to lie on a→b would falsely block
// (the engine's nav graph proves these are not real obstacles). Returns
// closest qualifying hit along a→b (smallest t in [0,1]).
//
// ignoreZ: when true, the z guard is skipped and the test is pure 2D.
// Callers whose endpoints carry no trustworthy z must set this — notably
// the waypoint smoother, which feeds 2D nav-graph nodes (no height field;
// see PathPoint layout in guidance_pathfind.h). A 2D test there fails safe:
// it can only over-block (keep a redundant waypoint), never miss a real
// wall and route through it. The default (false) keeps the guard for the
// room-shape/cursor consumers, where a phantom cross-floor wall would
// instead corrupt a spoken description.
//
// False on: null walls, wallCount<=0, a==b. Degenerate edges (a==b)
// skipped.
//
// Pulls from spatial::change_detector::GetCachedWalls; this is the
// consumer surface, not the build path.
bool SegmentCrossesWalkmesh(const WallEdge* walls,
                            int wallCount,
                            const Vector& a,
                            const Vector& b,
                            Vector& outHitPoint,
                            bool ignoreZ = false);

}  // namespace acc::engine
