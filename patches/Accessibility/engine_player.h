// Player state readers — position, facing, area.
//
// Layer: engine/ (pure read-side helpers, SEH-guarded; no engine re-entry,
// no menu-side state). Phase 1 foundation; consumers land in Phase 2+.
//
// Address chain (investigation Q1, Lane's Ghidra DB CONFIRMED):
//
//   *kAddrAppManagerPtr → CClientExoApp instance
//     → CClientExoApp::GetPlayerCreature() @0x5ed540 → CSWCCreature* (client)
//       → server_object @+0xf8 → CSWSObject* (server)
//         → position @+0x90 (Vector), orientation @+0x9c (Vector, z=0)
//
//   CSWSObject::GetArea() @0x4cb120 → area handle (CSWSArea*; opaque here)
//
// We deliberately use the server-side CSWSObject layout (+0x90 / +0x9c) and
// not the client CSWCObject layout (+0x24 / +0x30) — the two are independent
// per investigation Q1 and the server is the authoritative source.
//
// Functions return false / nullptr cleanly when any pointer in the chain is
// null (main menu, between-area loads, early DLL attach). Every dereference
// is __try-wrapped so corrupted/freed engine state can't fastfail the
// process — same convention as engine_reads.h.
//
// Functions are namespaced under `acc::engine`; addresses and offsets at
// file scope, matching engine_manager.h / engine_offsets.h conventions.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::engine {

// World position of the controlled player creature.
// Returns false when no player creature is loaded (main menu, area-load
// transitions) or when the address chain faults (engine teardown).
bool GetPlayerPosition(Vector& out);

// Heading vector of the controlled player creature. The engine zeroes the
// z component for object facing — see Q1 (heading is a 2D unit vector,
// not a quaternion). We propagate that convention.
bool GetPlayerFacing(Vector& out);

// Yaw in degrees, [0, 360), with 0° = +X = east, CCW positive. Matches
// ExecuteCommandGetFacing's normalization. Returns false for the same
// reasons as GetPlayerFacing, plus when the heading is degenerate
// (x=y=0, which the engine only emits transiently during spawn).
bool GetPlayerYawDegrees(float& out);

// Returns the player's current area as an opaque CSWSArea* handle, or
// nullptr if no player is loaded. Caller treats the pointer opaquely until
// engine_area lands in a later phase (consumers don't exist yet).
void* GetPlayerArea();

}  // namespace acc::engine

// CClientExoApp singleton. *kAddrAppManagerPtr holds the live app instance
// pointer; nullptr before the engine creates it (early DLL attach).
constexpr uintptr_t kAddrAppManagerPtr = 0x007A39FC;

// CClientExoApp::GetPlayerCreature — __thiscall(void) -> CSWCCreature*.
// SARIF CONFIRMED.
constexpr uintptr_t kAddrGetPlayerCreature = 0x005ED540;

// CSWSObject::GetArea — __thiscall(void) -> CSWSArea*. SARIF CONFIRMED.
constexpr uintptr_t kAddrCSWSObjectGetArea = 0x004CB120;

// CSWCObject.server_object — pointer to the matching server-side CSWSObject.
// Same offset for every client object (creatures, items, doors, etc.).
constexpr size_t kClientObjectServerObjectOffset = 0xf8;

// CSWSObject.position / orientation. Server-side layout.
constexpr size_t kServerObjectPositionOffset    = 0x90;
constexpr size_t kServerObjectOrientationOffset = 0x9c;
