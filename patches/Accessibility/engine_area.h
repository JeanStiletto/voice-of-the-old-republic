// Per-area object iteration + room lookup.
//
// Layer: engine/ (pure read-side helpers, SEH-guarded; no engine re-entry
// beyond the one wrapped CSWSArea::GetRoom call needed for room-resolution).
//
// Phase 2 lay-off 1: object-list + room-lookup slice. Foundation for the
// Phase 2 consumers (Pillar 4 cycle, Pillar 2 room-transition announcements,
// guidance/autowalk target picking). No consumer wired up in this lay-off;
// per-type name resolution + room-name reading are intentionally deferred to
// their respective consumer lay-offs (Pillar 4 cycle / Pillar 2 transitions)
// where runtime verification can lock the layout.
//
// Engineering basis (investigation Q5 + lay-off 4 corrections, all
// CONFIRMED via DumpBytes 2026-05-04):
//
//   CSWSArea
//     +0x190 game_objects      // ulong* — array of object HANDLES (NOT
//                              //          CSWSObject** as initially read).
//                              //          Confirmed via swkotor.exe.h:
//                              //          `ulong *game_objects;`.
//     +0x194 game_object_count // int
//     +0x230 rooms             // inline CSWSRoom[] (stride 0x4c per GetRoom decomp)
//     +0x25c room_names        // CExoString* (layout untested at lay-off 1)
//
//   CSWSArea::GetRoom @0x4bb600 — point-in-room query
//     Signature: CSWSRoom* (CSWSArea*, Vector*, int* /*outRoomIndex, may be NULL*/)
//
//   CSWSObject (every game-object subclass inherits this base)
//     +0x008 object_kind  // uint32 — GAME_OBJECT_TYPES enum (per Q5)
//     +0x090 position     // Vector (server-side authoritative; matches engine_player)
//
// Handle resolution chain (confirmed by decompiling CSWSObject::GetArea
// @0x4cb120; bytes captured 2026-05-04):
//
//   *kAddrAppManagerPtr     → AppManager*
//     AppManager + 0x4      → CClientExoApp* (already used in engine_player)
//     AppManager + 0x8      → CServerExoApp* (new, used here for object
//                             resolution; verified via `mov ecx,[eax+0x8]`
//                             at GetArea+0x10).
//   CServerExoApp::GetObjectArray() @0x004aed70 → CGameObjectArray*
//   CGameObjectArray::GetGameObject(id, &outPtr) @0x004d8230 → bool
//     Out-pointer is a CGameObject* (which downcasts to CSWSObject* for
//     server-side arrays).
//
// Lay-off 4 correction: the initial implementation treated game_objects[]
// as CSWSObject** and dereferenced each entry as a pointer; in practice
// the array holds 32-bit handles and the dereference produced garbage,
// so every `GetObjectKind` read returned a kind outside the 5/6/7/9/10/12
// set we filter on. Verified by patch-20260503-224102.log: snapshotSize=219,
// scanned=219, every kind bucket=0. Switched to handle-based iteration —
// each Next() call resolves one handle through the engine's GetGameObject
// table.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::engine {

// GAME_OBJECT_TYPES enum values, per investigation Q5. Stored at
// CSWSObject +0x8. Listed values are the ones the Pillar 4 cycle filters on
// (per docs/navsystem-longterm-plan.md §"Categories"); other values exist in
// the engine but are not surfaced here because they're not player-facing
// (PROJECTILE, AREAOFEFFECT, AREA) or are folded into other categories
// (STORE → Container/Placeable per the plan).
//
// Numeric values match the engine — do not reorder.
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

// Resolves the player's current area via engine_player::GetPlayerArea().
// Convenience wrapper so Phase 2 consumers don't all have to reach into
// engine_player + their own SEH frame.
//
// Returns nullptr in the same cases GetPlayerArea() does (main menu, area
// transitions, early DLL attach).
void* GetCurrentArea();

// Returns the GAME_OBJECT_TYPES tag at obj +0x8, or -1 if obj is null /
// the read faults. Compare against GameObjectKind values.
int GetObjectKind(void* gameObject);

// Returns the engine-side handle (CGameObject.id @+0x4) for an object.
// This is the inverse of ResolveServerObjectHandle / ResolveClientObjectHandle:
// given a pointer obtained via either resolution path (or via the area
// iterator), recover the ulong handle the engine itself uses to refer to
// the object on its action queue, in LastTarget, etc. 0 on null / fault.
uint32_t GetObjectHandle(void* gameObject);

// Resolve a 32-bit object handle to a CSWSObject*. The engine has two
// independent handle namespaces — server-side (the array CSWSArea's
// game_objects[] holds, and the action queue's object-id slots use) and
// client-side (CClientExoApp::LastTarget, the in-world hover/passive-
// selection target). Use the variant that matches the handle source.
//
// Both return nullptr for invalid sentinels (`0`, `0xFFFFFFFF`,
// `kInvalidObjectId 0x7F000000`) and for any handle the engine reports
// as missing. Both SEH-guard every dereference. Caller treats the
// returned pointer opaquely (same downcast rules as the
// AreaObjectIterator yield).
//
// Server-side path: AppManager → CServerExoApp → GetObjectArray →
// CGameObjectArray::GetGameObject. Same chain AreaObjectIterator uses
// internally; exposed here for the "I have one server handle" case.
// Server-side handles in observed traffic look like 0x000000XX (low
// bits index into the server array).
void* ResolveServerObjectHandle(uint32_t handle);

// Client-side path: AppManager → CClientExoApp → GetGameObject(handle)
// returns a CSWCObject* (client-side counterpart). We chain through
// `CSWCObject.server_object @+0xf8` to recover the server-side
// CSWSObject* our other helpers (GetObjectKind, GetObjectName,
// GetObjectPosition) expect. Client-side handles in observed traffic
// look like 0x800000XX (high bit set; engine packs a side flag).
//
// Verified live 2026-05-04 (`patch-20260504-065345.log`): the
// LastTarget handles 0x80000004 / 0x800000c6 / 0x80000047 fail
// resolution through the server-side CGameObjectArray — they need this
// path.
void* ResolveClientObjectHandle(uint32_t handle);

// Server-side world position read (CSWSObject layout, +0x90). Returns false
// if obj is null or the read faults.
bool GetObjectPosition(void* gameObject, Vector& out);

// Wraps CSWSArea::GetRoom @0x4bb600. Returns the CSWSRoom* containing pos,
// or nullptr if the position is outside any room or any pointer in the
// chain faults. Caller treats the room pointer opaquely — internal layout
// (stride 0x4c) is not surfaced until a Phase 2 consumer needs it.
void* GetRoomAt(void* area, const Vector& pos);

// Same as GetRoomAt but also captures the room index the engine resolved
// (CSWSArea::GetRoom's third arg is `int* outRoomIndex`, NULL-passable).
// Index is -1 on miss / fault; usable as the lookup key for room_names[].
// Returns the CSWSRoom* (or nullptr) just like GetRoomAt.
void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex);

// Compute a "representative" point inside the room (used as input to
// per-room walkmesh probes — terrain shape classification, etc.). The
// returned point is the centroid of the middle face of the room's
// walkmesh in world space (LocalToWorld already applied), which sits
// firmly inside the walkable surface and far from boundary edges in
// the common case. Returns false on null area, out-of-range roomIdx,
// missing surface_mesh, or any read fault (SEH-guarded internally).
//
// Optional `outFailReason` captures why derivation failed (when the
// return value is false). 0 means success; non-zero codes:
//   1  bad args (area null or roomIdx < 0)
//   2  roomIdx >= roomCount
//   3  rooms array pointer null
//   4  surface_mesh null on the room — most likely cause of K1 "void"
//      rooms (skybox helpers / placeholders) which have no walkmesh
//   5  vertices/faceIndices null or faceCount == 0
//   6  SEH fault during any read
// Pass nullptr if the caller doesn't care.
bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld,
                                int* outFailReason = nullptr);

// Player-facing display name for an area. Reads CSWSArea.name
// (CExoLocString at +0x150) — tries the inline string first, falls back
// to a TLK lookup of the strref at +0x154; if both empty, falls back to
// CSWSArea.tag (CExoString at +0x158, modder-assigned identifier).
//
// Returns false on null area, every fallback empty, or any read fault
// (SEH-guarded internally). outBuf must be at least bufSize bytes; on
// success a null-terminated string is written.
bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize);

// Read the i-th room name from CSWSArea.room_names[index]. The engine
// stores room_names as a `CExoString*` array (stride 8 — char* +
// uint32 length per CExoString) indexed by the same room index that
// GetRoomAtIndexed yields and that GetRoom's third arg captures.
//
// Note: room names are NOT localized — they are .lyt-room identifiers
// like "m02_03e" or "stunt_01_main". The transitions consumer wraps
// the result with a localized "Room:" prefix to keep meaning clear.
//
// Returns false on null area, out-of-range index (negative or >=
// room_count at +0x268), null array pointer, or any read fault. outBuf
// must be at least bufSize bytes; on success a null-terminated string
// is written.
bool GetRoomDisplayName(void* area, int roomIndex,
                        char* outBuf, size_t bufSize);

// Resolve the player-facing localized name for any game object whose kind
// is one of the six Pillar 4 categories (Door, Creature, Placeable, Item,
// Waypoint, Trigger). The chain is per-kind:
//
//   Door        — CSWSDoor.loc_name           @+0x39c (CExoLocString)
//   Creature    — CSWSCreature.creature_stats @+0xa74 → first_name @+0x14
//   Placeable   — CSWSPlaceable.loc_name      @+0x228
//   Item        — CSWSItem.localized_name     @+0x280
//   Waypoint    — CSWSWaypoint.localized_name @+0x238
//   Trigger     — CSWSTrigger.localized_name  @+0x228
//
// CExoLocString layout matches CExoString at the byte level (8 bytes, ptr+
// uint32) — engine_reads::ExtractTextOrStrRef handles both: tries the inline
// string first, falls back to a TLK lookup of the strref at +4. If both are
// empty, falls back to CSWSObject.tag @+0x18 (CExoString — modder-assigned
// stable id), per the investigation Q7 "name-resolution pipeline" guidance.
//
// outBuf must point to a buffer of at least bufSize bytes; on success a
// null-terminated string is written. Returns false if the kind isn't one we
// resolve, every layer of the chain produces empty text, or any read faults
// (SEH-guarded internally).
bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize);

// Localized display name lookup by object handle. Wraps the engine's
// universal accessor `CClientExoApp::GetObjectName(ulong, CExoString*)`
// — which returns the same name the in-game UI shows for any object
// kind (creatures, items, doors, ...). Better than calling
// GetObjectName(gameObject, ...) when working from a handle because
// the engine's own resolution chain handles the cases where
// `first_name` is empty (template FirstName + appearance.2da
// displayname + racialtypes.2da fallbacks).
//
// `handle` accepts both client-side (high bit set) and server-side
// (low bit) handles — the engine routes either correctly.
//
// Returns true on non-empty result; false on engine accessor failure
// or empty name. outBuf is always NUL-terminated on entry (outBuf[0]
// = '\0' even on failure path).
bool GetObjectDisplayNameByHandle(uint32_t handle,
                                  char* outBuf, size_t bufSize);

// Pillar 4 sub-state predicates. The category-kind filter alone over-includes
// — these refine to the player-relevant subset locked in the plan §"Categories":
//
//   IsUsablePlaceable    — CSWSPlaceable.usable        @+0x328 (bool)
//                       OR CSWSPlaceable.has_inventory @+0x334 (bool).
//                          Filters out scenery placeables that the player
//                          can't interact with.
//   IsLandmarkWaypoint   — CSWSWaypoint.has_map_note   @+0x228 (bool).
//                          Engine's own "important location" curation.
//   IsTransitionTrigger  — CSWSTrigger.transition_destination @+0x30c
//                          (Vector — non-zero indicates set).
//
// All return false on null / fault. Callers should already have confirmed
// the object's kind matches before invoking the corresponding predicate.
bool IsUsablePlaceable(void* placeable);
bool IsLandmarkWaypoint(void* waypoint);
bool IsTransitionTrigger(void* trigger);

// Reads CSWSWaypoint.map_note_enabled (+0x22c). True if this waypoint's
// map note is currently visible on the in-game map (engine's fog-of-war
// model — disabled until the player discovers it via map-pin trigger).
// Use to gate any feature that surfaces map-note text so we don't spoil
// locations the player hasn't yet seen on the map.
//
// Returns false on null / fault. Caller should already have confirmed
// the object is a Waypoint kind via GetObjectKind.
bool IsMapNoteEnabled(void* waypoint);

// Resolve the per-area CSWSAreaMap* that owns the fog-of-war bitfield +
// pixel transform. Reached via AppManager → CServerExoApp →
// GetModule() → CSWSModule.area_map (+0x218). Returns nullptr on any
// null link (DLL attach, between modules, no current game) or SEH
// fault. Shared by map_ui_cursor (cursor projection + fog read) and
// cycle_state (map-context fog-of-war filter).
void* GetAreaMap();

// Wraps CSWSAreaMap::IsWorldPointExplored @0x00579210 — the engine's
// fog-of-war read. Returns true iff `pos` lies in a map cell the player
// has revealed (and the map cell exists). False on null areaMap, off-
// map positions, or SEH fault. Used to filter map-context cycle output
// so we never narrate landmarks the player hasn't seen yet.
bool IsWorldPointExplored(void* areaMap, const Vector& pos);

// Reads CSWSWaypoint.map_note (+0x230, CExoLocString). The Bioware-
// authored display label (e.g. "Bridge", "Cargo Hold", "Brücke",
// "Frachtraum") that the in-game map shows. CExoLocString shape matches
// CExoString at +0x230 + the strref at +0x234, so engine_reads's
// `ExtractTextOrStrRef` resolves both the inline c_string path and the
// TLK-strref fallback path.
//
// Returns false on null / empty resolution / fault. outBuf must be at
// least bufSize bytes; on success a null-terminated string is written.
bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize);

// Iterator over CSWSArea.game_objects[]. Constructed once per scan; Next()
// returns successive CSWSObject* values until exhausted (returns nullptr).
//
// Snapshot semantics: the data pointer + size are captured at construction
// time. If the engine resizes game_objects mid-scan the iterator will read
// stale handles — in practice the array is rebuilt on area-load, never
// mid-frame, so a single-OnUpdate-tick scan is safe.
//
// Handles are resolved per-Next() through the cached CGameObjectArray*
// captured at construction. The resolution call is one engine __thiscall
// + one out-pointer read per object, all SEH-guarded.
//
// All field reads are SEH-guarded; if the area pointer or the server-side
// object array faults at construction the iterator yields nothing (Next()
// returns nullptr immediately).
class AreaObjectIterator {
public:
    explicit AreaObjectIterator(void* area);

    // Returns the next CSWSObject*, or nullptr when exhausted / on resolution
    // failure. Skips array slots whose handle is 0 (sentinel) or whose
    // GetGameObject lookup returns false (engine treats some handles as
    // invalid sentinels — happens during area unload bookkeeping).
    void* Next();

    // Snapshot of the array length captured at construction. Used by the
    // cycle diagnostic log so callers can see "scanned N objects, matched M".
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

// Per-area map-and-fog-of-war singleton chain.
//   CServerExoApp::GetModule @0x004ae6b0 → CSWSModule*
//   CSWSModule.area_map (+0x218)         → CSWSAreaMap*
//   CSWSAreaMap::IsWorldPointExplored @0x00579210 — fog-of-war read.
// See navsystems-investigation.md §Q4 for the full layout.
constexpr uintptr_t kAddrCServerExoAppGetModule          = 0x004AE6B0;
constexpr size_t    kModuleAreaMapOffset                 = 0x218;
constexpr uintptr_t kAddrCSWSAreaMapIsWorldPointExplored = 0x00579210;

// CSWSArea field offsets. game_objects + rooms verified live (lay-off 1+4);
// name + tag + room_count from Lane's SARIF DATATYPE entry for CSWSArea
// (k1_win_gog_swkotor.exe.xml line 13428, SIZE=0x2d4) — same DB the rooms +
// game_objects offsets came from. Per memory:
// project_ghidra_gog_steam_bytes_match the Steam+GoG layouts agree.
constexpr size_t kAreaGameObjectsOffset      = 0x190;
constexpr size_t kAreaGameObjectCountOffset  = 0x194;
constexpr size_t kAreaRoomsOffset            = 0x230;  // CSWSRoom* (pointer to inline-stride buffer; deref before indexing)
constexpr size_t kAreaNameLocOffset          = 0x150;  // CExoLocString name
constexpr size_t kAreaTagOffset              = 0x158;  // CExoString tag (fallback)
constexpr size_t kAreaRoomNamesOffset        = 0x25c;  // CExoString* room_names
constexpr size_t kAreaRoomCountOffset        = 0x268;  // ulong room_count
constexpr size_t kCExoStringStride           = 0x8;    // char* + uint32 length

// CSWSRoom inline-array stride. Per investigation §"point-in-room query":
// "room stride is 0x4c bytes per GetRoom decomp". Used when deriving a
// room index from a CSWSRoom* via pointer arithmetic.
constexpr size_t kRoomStride = 0x4c;

// CSWSObject base-class field offsets.
constexpr size_t kObjectKindOffset = 0x8;   // uint32, GAME_OBJECT_TYPES enum
constexpr size_t kObjectTagOffset  = 0x18;  // CExoString — modder-assigned stable id
// kServerObjectPositionOffset (0x90) is already in engine_player.h.

// Per-subclass localized-name offsets (CExoLocString unless noted). Values
// from investigation Q5 + Q7 — all CONFIRMED in the SARIF DATATYPEs table.
constexpr size_t kDoorLocNameOffset            = 0x39c;
// Server-side door state + extra-text fields (CSWSDoor). Verified against
// Lane's SARIF DATATYPE entry for CSWSDoor. Used by `GetObjectName` to
// enrich the bare loc_name with state ("verriegelt"/"offen"), the
// transition destination (e.g. "Brücke"), and any description the modder
// set on the .utd template.
constexpr size_t kDoorLockedOffset             = 0x2c4;  // undefined4 (treated as bool)
constexpr size_t kDoorOpenStateOffset          = 0x2cc;  // byte
constexpr size_t kDoorDescriptionOffset        = 0x3a4;  // CExoLocString
constexpr size_t kDoorTransitionDestOffset     = 0x3c8;  // CExoLocString
constexpr size_t kCreatureStatsPtrOffset       = 0xa74;  // CSWSCreatureStats* in CSWSCreature
constexpr size_t kCreatureStatsFirstNameOffset = 0x14;   // CExoLocString in CSWSCreatureStats
constexpr size_t kPlaceableLocNameOffset       = 0x228;
constexpr size_t kItemLocNameOffset            = 0x280;
constexpr size_t kWaypointLocNameOffset        = 0x238;
constexpr size_t kTriggerLocNameOffset         = 0x228;

// Per-subclass sub-state offsets (Pillar 4 filter refinement).
constexpr size_t kPlaceableUsableOffset        = 0x328;  // bool (1 byte)
constexpr size_t kPlaceableHasInventoryOffset  = 0x334;  // bool (1 byte)
constexpr size_t kWaypointHasMapNoteOffset     = 0x228;  // bool (1 byte)
constexpr size_t kTriggerTransitionDestOffset  = 0x30c;  // Vector (12 bytes)

// CSWSWaypoint map-note offsets — Bioware-authored "atmospheric" labels
// (verified against k1_win_gog_swkotor.exe.xml line 15692, CSWSWaypoint
// SIZE=0x240).
constexpr size_t kWaypointMapNoteEnabledOffset = 0x22c;  // int
constexpr size_t kWaypointMapNoteLocOffset     = 0x230;  // CExoLocString (8 bytes)

// ---------------------------------------------------------------------------
// Walkmesh wall-edge extraction (Phase 3 lay-off 1, Pillar 1 foundation).
//
// CSWSArea
//   +0x230 rooms        // CSWSRoom* — heap array, stride kRoomStride (0x4c)
//   +0x268 room_count   // ulong (already declared above as
//                       //        kAreaRoomCountOffset)
//
// CSWSRoom layout (swkotor.exe.h:10397):
//   +0x00 CSWRoom (vtable + Vector position + Quaternion + CResRef + ...
//                  — total 0x3c bytes)
//   +0x3c CSWRoomSurfaceMesh*  surface_mesh
//
// CSWRoomSurfaceMesh layout (swkotor.exe.h:15742):
//   +0x00 CSWCollisionMesh mesh   (embedded; 0x88 bytes)
//   +0x88 SurfaceMeshAdjacency*   adjacencies   // face_count entries
//   +0x8c CExoArrayList<SurfaceMeshEdge> edges
//   ...
//
// CSWCollisionMesh layout (swkotor.exe.h:8337) — relevant offsets only:
//   +0x00 vtable
//   +0x04 world_coords           // int — 1 = vertices already in world space
//   +0x50 vertex_count           // ulong
//   +0x54 vertices               // Vector* (vertex_count × 12 bytes,
//                                //          mesh-local space unless
//                                //          world_coords=1)
//   +0x58 face_count             // ulong
//   +0x60 face_indices           // WalkmeshFace* (face_count × 12 bytes;
//                                //                three vertex indices each)
//   +0x64 materials              // ulong* (face_count entries; index into
//                                //         surfacemat.2da; per-face material)
//
// SurfaceMeshAdjacency (swkotor.exe.h:15583):
//   int indices[3];   // one per triangle edge
//                     //   -1                 = perimeter (WALL EDGE)
//                     //   otherwise edge_id  = neighbour face's edge index;
//                     //                        face_id = edge_id / 3
//
// CSWCollisionMesh::LocalToWorld @0x596aa0 — __thiscall, BYTES_PURGED=8:
//   void LocalToWorld(this=cm, Vector* output, Vector* local_point)
//   Internally short-circuits when this->world_coords != 0 (output := input);
//   we always call it and let it decide.
// ---------------------------------------------------------------------------

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
// uses ~1cm tolerance to absorb LocalToWorld floating-point variance.
//
// Output:
//   - if outBuf is non-null and maxEdges > 0: writes up to min(emitted,
//     maxEdges) post-seam-filtered edges and returns the post-filter
//     count. (Effectively the count of REAL walls in the area, bounded
//     by buffer size.)
//   - if outBuf is null OR maxEdges <= 0: returns the pre-seam-filter
//     discovered count — useful for buffer-sizing telemetry. Note this
//     differs from the buffer-mode return value; callers comparing the
//     two should expect filtered ≤ discovered.
//
// Every read is SEH-guarded internally; faults at any layer (null
// surface_mesh on a partially-loaded room, garbage face/vertex indices, etc.)
// abort that single room's contribution and the scan continues with the next
// room. The function never raises.
//
// Cost: O(total_faces * 3) reads + one LocalToWorld engine call per emitted
// edge. The walkmesh is immutable per area-load (doors are separate
// collision meshes, not modifications to the room mesh), so this should be
// called once per area-change and the result cached by the caller.
int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges);

// 2D segment-vs-walkmesh-perimeter test in the XY plane (Z is ignored —
// rooms in K1 are layout-flat enough that planar collision matches the
// engine's own walkability behaviour for the room-scale virtual cursor).
//
// Walks `walls[0..wallCount)` testing each perimeter edge against the
// movement segment a→b. Returns the *closest* hit along a→b (smallest t
// in [0,1] where movement reaches the wall) — for a slow-moving cursor
// every tick, the closest hit is the only physically meaningful one.
//
// Output:
//   - returns true if any edge intersects the segment a→b; outHitPoint
//     is written with the world-space intersection point for the
//     closest-along-a→b hit (suitable for both clamping the cursor and
//     placing a 3D collision cue).
//   - returns false if the segment is fully clear; outHitPoint is
//     untouched.
//
// Degenerate inputs:
//   - walls == nullptr or wallCount <= 0  → false (no walls cached yet).
//   - a == b                                → false (cursor is stationary;
//                                              avoid divide-by-zero).
//   - degenerate edge (wall.a == wall.b)  → that edge is skipped.
//
// Phase 4 lay-off 4 — view-mode virtual cursor. Pulls the wall buffer
// from `acc::spatial::change_detector::GetCachedWalls` (the cache lives
// there since the change-detector built it in Phase 3 lay-off 3); we
// only consume the data here, no rebuild path of our own.
bool SegmentCrossesWalkmesh(const WallEdge* walls,
                            int wallCount,
                            const Vector& a,
                            const Vector& b,
                            Vector& outHitPoint);

}  // namespace acc::engine
