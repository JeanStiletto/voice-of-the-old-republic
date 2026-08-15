// Shared minigame aim-assist facility — the engine-grounded primitives that let
// us STEER a minigame's controlled object by writing its integration field,
// plus the console-style "sticky magnetism" math.
//
// Both the swoop bike and the turret gun are a CSWMiniPlayer (extends
// CSWTrackFollower). Each holds an `offset` Vector at +0x1c4 that
// CSWMiniPlayer::Control integrates every tick (`offset += axis-velocity`) and
// then applies to the controlled object:
//   - turret: offset.x = elevation°, offset.z = azimuth° (the gun/camera aim);
//   - swoop:  offset.x = lateral lane coordinate (the bike's position in the
//             tunnel; world-X tracks tunnel-X 1:1, so a unit of offset.x is a
//             unit of world-X).
// Because the engine RE-INTEGRATES offset from its current value each tick, a
// per-tick WRITE (issued after Control has run — i.e. from the OnUpdate tick
// where the minigame modules live) sticks: the next tick only adds the player's
// own small delta on top. This is the mechanism the shipped turret aim-assist
// uses; this header lifts the reusable parts out so swoop can share them.
//
// What is shared (here): the offset read/write primitives and the magnetism
// curve + per-tick-capped step, plus the SEH read primitives and minigame
// object-array resolution all three minigame TUs need. What stays per-game
// (in minigame_turret.cpp / minigame_swoop_audio.cpp): how the aim ERROR is
// computed (angular for the turret, linear lane-units for swoop), target
// selection, and the offset→world SIGN (the turret calibrates it; swoop's
// 1:1 mapping passes +1). All reads SEH-guarded.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector
#include "engine_offsets_select.h"

namespace acc::minigame {

// ---- SEH read primitives ---------------------------------------------------
// Fault-tolerant field reads against engine objects. Every minigame TU used to
// carry its own byte-identical copy of these (Phase-2 duplication finding D2);
// they live here now. Each returns a benign value rather than propagating a
// fault, so a stale or half-torn-down engine pointer can never take the game
// down through us.
//
// Naming note: SafeReadFloat, not SafeReadF32 — minigame_turret.cpp used the
// latter and the other two used the former; one name had to win.
void*    SafeReadPtr(void* base, size_t off);
uint32_t SafeReadU32(void* base, size_t off);
float    SafeReadFloat(void* base, size_t off);
bool     SafeReadVector(void* base, size_t off, Vector& out);

// ---- Minigame object array -------------------------------------------------
// CClientExoAppInternal holds the minigame object array (obstacles, accelerator
// pads, track followers) at offset 0. Resolution walks
// AppManager -> CClientExoApp -> internal -> array.
//
// KOTOR 2: identical. Witnessed in its GetMGOArray twin @0x00740070, which is
// literally `internal = [this+4]; return [internal+0]` — the same two hops.
const size_t kClientInternalMgoArrayOffset = acc::off::Same(0x0);

// Returns the minigame object array, or nullptr if any link of the chain is
// null or faults (i.e. no minigame is live).
void* ResolveMgoArray();

// Call a vtable AsSWCxxx-style downcast at `vtableSlotOffset` on `obj`.
// Returns nullptr on null input, a null slot, or any fault.
void* CallAsCast(void* obj, size_t vtableSlotOffset);

// ---- Track-follower world position -----------------------------------------
// A CSWTrackFollower (the swoop bike, the turret gun, and every racer /
// obstacle rider) carries a CExoArrayList of model wrappers at +0x68. The
// first model's vtable slot +0x64 is a GetPosition thunk that fills a
// caller-supplied Vector. Reading the follower's own transform is not
// equivalent — the model is what the engine actually renders and what the
// spatial cues must track.
//
// KOTOR 2: both identical, and the identification is exact — its
// CSWTrackFollower::GetPosition twin @0x00835f50 reads size at +0x6c, data at
// +0x68, takes element [0], and calls the model's vtable[+0x64] to copy three
// floats, instruction for instruction like KOTOR 1's @0x0066d5d0. (The list's
// element STRIDE is 8 bytes in BOTH games — the engine's own PlayAnimation walk
// indexes `data[i*8]` on each — so `data[0]` being the bare model pointer holds
// either way. Only index 0 is ever read here.)
const size_t kTrackFollowerModelsDataOffset = acc::off::Same(0x68);
constexpr size_t kModelVtableSlotGetPosition    = 0x64;

// World position of `follower`'s first model. False (out untouched) if the
// follower, its model list, the model, or the thunk is missing or faults.
bool ReadFollowerPosition(void* follower, Vector& out);

// CSWMiniPlayer.offset — the per-tick-integrated aim/lane field. See the file
// header for the per-game interpretation of its components.
//
// KOTOR 2 = +0x1f4, i.e. KOTOR 1 + 0x30. That delta is CSWTrackFollower's, not
// CSWMiniPlayer's: K2's follower carries THIRTEEN per-object script CResRefs
// instead of ten (its .are Scripts struct gained OnAccelerate / OnBrake /
// OnHitWorld — the swoop jump), so the base class grew by 3 * 0x10 and every
// CSWMiniPlayer field above it shifted by exactly that. Anchored at both ends:
// the follower's own sphere_radius (+0x84) and speed (+0x98) are UNMOVED (they
// sit below the script array), while min/max speed are +0x30 up, witnessed
// directly in K2's SetMinSpeed/SetMaxSpeed (@0x00838240 / @0x00838290 write
// +0x208 / +0x20c). `offset` is the first Vector its constructor zeroes, at
// dword index 0x7d = +0x1f4.
const size_t kMiniPlayerOffsetVectorOffset = acc::off::Pick(0x1c4, 0x1f4);

// Read / write the whole offset Vector on a CSWMiniPlayer `player` pointer.
// Read returns false (and leaves `out` untouched) on a null/faulting player;
// write is a no-op on null/fault. Callers that touch only one component should
// read-modify-write so the other components are preserved.
bool ReadOffsetVector(void* player, Vector& out);
void WriteOffsetVector(void* player, const Vector& v);

// ---- Console-style sticky magnetism (the "less invasive" assist) -----------
// A proximity-ramped pull on one offset axis: gentle far from the target (a
// guide you can steer straight through to pick a different target) and strong
// on it (sticky), blended on top of the player's own input and capped per tick
// so a far target is pulled, not yanked. This is the turret's default-mode
// magnetism, generalised; it is NOT the full lock-on (that stays turret-only).
struct MagnetParams {
    float gainFar;   // pull gain at the engage edge (gentle guide)
    float gainNear;  // pull gain dead on target (sticky)
    float maxStep;   // per-tick cap on the pull, in offset units
};

// Proximity-ramped gain. `t` in [0,1]: 0 at the engage edge, 1 dead on target
// (callers clamp). t² keeps the far end gentle and the near end sticky.
float MagnetGain(float t, const MagnetParams& p);

// One tick of magnetism on a single axis. `mappedErr` is the signed
// aim-minus-target error already mapped into offset units and offset→world
// sign (so the corrective pull is simply -mappedErr): the turret passes
// sign·worldErr, swoop passes (bikeX − padX) for its 1:1 axis. Returns the new
// offset value: offsetVal − mappedErr·gain, with the step capped to ±maxStep.
float MagnetStep(float offsetVal, float mappedErr, float gain,
                 const MagnetParams& p);

}  // namespace acc::minigame
