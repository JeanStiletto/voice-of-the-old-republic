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
// curve + per-tick-capped step. What stays per-game (in turret_game.cpp /
// swoop_spatial_audio.cpp): how the aim ERROR is computed (angular for the
// turret, linear lane-units for swoop), target selection, and the offset→world
// SIGN (the turret calibrates it; swoop's 1:1 mapping passes +1). All reads
// SEH-guarded.

#pragma once

#include "engine_offsets.h"  // Vector

namespace acc::minigame {

// CSWMiniPlayer.offset — the per-tick-integrated aim/lane field. See the file
// header for the per-game interpretation of its components.
constexpr size_t kMiniPlayerOffsetVectorOffset = 0x1c4;

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
