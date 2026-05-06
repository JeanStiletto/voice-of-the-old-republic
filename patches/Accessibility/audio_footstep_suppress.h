// Pillar 1 — stuck-detection via footstep suppression.
//
// Layer: audio/ (cross-cutting; OnUpdate Tick + OnPlayFootstep handler).
//
// Design intent (locked plan, navsystem-longterm-plan.md §"Mechanics —
// stuck-detection via footstep suppression"): when the player creature has
// movement intent (engine plays the walk animation, which is what triggers
// PlayFootstep) but the character's actual displacement between consecutive
// ticks is below `kStuckEpsilonMeters`, suppress the footstep playback.
// Silence-when-stuck signals "you're not moving" to a blind player without
// a new cue.
//
// Mechanism:
//   1. OnUpdate calls Tick() once per frame. Reads the player's world
//      position via engine_player::GetPlayerPosition; if the displacement
//      since the previous observation is below epsilon, sets g_was_stuck.
//   2. The engine's CSWCCreature::PlayFootstep dispatches per animation-
//      event tick (variable rate, typically 0–2 per frame depending on the
//      walk anim's footstep frame markers). Our OnPlayFootstep detour
//      checks (a) the creature is the active player leader, (b)
//      g_was_stuck. When both are true, returns 1 → wrapper consumes via
//      the engine's own LAB_0061a632 early-return path (skips audio +
//      footprint visual + rumble together).
//
// Filter to player only:
//   PlayFootstep also fires for NPCs (companions, enemies). We must not
//   suppress their footsteps. The handler compares `this` against
//   GetClientLeader() — NPCs return without action. Same identity check
//   the engine itself uses internally to assign priority groups (decompile
//   shows `if (uVar2 == GetPlayerCreatureId(...))` selecting group 0x13).
//
// One-tick lag:
//   Per the design doc, `was_stuck` reflects the PREVIOUS tick's
//   displacement, not the current one. ~33ms latency at 30 Hz —
//   imperceptible.
//
// Phase 3 lay-off 5.

#pragma once

namespace acc::audio::footstep_suppress {

// Per-frame stuck detector. Self-gates on player resolved (returns silently
// when no player creature is loaded). Idempotent on repeated calls.
void Tick();

// Returns true when the most-recent Tick() observed a sub-epsilon
// displacement from the prior position. Read by OnPlayFootstep.
bool WasStuckLastTick();

}  // namespace acc::audio::footstep_suppress
