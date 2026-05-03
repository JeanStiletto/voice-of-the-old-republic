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
// Engineering basis (investigation Q5, all CONFIRMED):
//   CSWSArea
//     +0x190 game_objects      // CSWSObject** (heap array of pointers)
//     +0x194 game_object_count // int
//     +0x230 rooms             // inline CSWSRoom[] (stride 0x4c per GetRoom decomp)
//     +0x25c room_names        // CExoString* (layout untested at lay-off 1)
//
//   CSWSArea::GetRoom @0x4bb600 — point-in-room query
//     Signature: CSWSRoom* (CSWSArea*, Vector*, int* /*outRoomIndex, may be NULL*/)
//     The third arg is a NULL-passable out-int observed in PositionWalkable's
//     decomp; we pass NULL here and derive the index from pointer arithmetic
//     against +0x230 if a consumer needs it.
//
//   CSWSObject (every game-object subclass inherits this base)
//     +0x008 object_kind  // uint32 — GAME_OBJECT_TYPES enum (per Q5)
//     +0x090 position     // Vector (server-side authoritative; matches engine_player)
//
// Why direct CExoArrayList iteration over CSWSArea::GetFirstObjectInArea /
// GetNextObjectInArea: the SARIF entries return `undefined4` (likely an
// engine handle, not a pointer) and would re-enter engine code per step. The
// underlying array layout at +0x190 / +0x194 matches our existing CExoArrayList
// pattern exactly and gives us the raw CSWSObject* we need without any
// engine-side iteration cost.

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

// Server-side world position read (CSWSObject layout, +0x90). Returns false
// if obj is null or the read faults.
bool GetObjectPosition(void* gameObject, Vector& out);

// Wraps CSWSArea::GetRoom @0x4bb600. Returns the CSWSRoom* containing pos,
// or nullptr if the position is outside any room or any pointer in the
// chain faults. Caller treats the room pointer opaquely — internal layout
// (stride 0x4c) is not surfaced until a Phase 2 consumer needs it.
void* GetRoomAt(void* area, const Vector& pos);

// Iterator over CSWSArea.game_objects[]. Constructed once per scan; Next()
// returns successive CSWSObject* values until exhausted (returns nullptr).
//
// Snapshot semantics: the data pointer + size are captured at construction
// time. If the engine resizes game_objects mid-scan the iterator will read
// stale pointers — in practice the array is rebuilt on area-load, never
// mid-frame, so a single-OnUpdate-tick scan is safe.
//
// All field reads are SEH-guarded; if the area pointer faults at construction
// the iterator yields nothing (Next() returns nullptr immediately).
class AreaObjectIterator {
public:
    explicit AreaObjectIterator(void* area);

    // Returns the next CSWSObject*, or nullptr when exhausted. May skip
    // entries that read as null (engine occasionally leaves holes during
    // object-deletion bookkeeping).
    void* Next();

private:
    void** data_;
    int    size_;
    int    index_;
};

}  // namespace acc::engine

// CSWSArea::GetRoom — __thiscall. Third arg is an int* outRoomIndex
// (NULL-passable per PositionWalkable's decomp).
constexpr uintptr_t kAddrCSWSAreaGetRoom = 0x004BB600;

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
// kServerObjectPositionOffset (0x90) is already in engine_player.h.
