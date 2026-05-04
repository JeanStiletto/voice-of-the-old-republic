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

// CSWSArea field offsets (investigation Q5, CONFIRMED).
constexpr size_t kAreaGameObjectsOffset      = 0x190;
constexpr size_t kAreaGameObjectCountOffset  = 0x194;
constexpr size_t kAreaRoomsOffset            = 0x230;  // inline CSWSRoom[]
constexpr size_t kAreaRoomNamesOffset        = 0x25c;  // CExoString* (untested)

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
