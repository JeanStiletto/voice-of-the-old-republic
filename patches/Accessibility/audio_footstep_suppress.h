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
// Stuck-direction probe:
//   Same module also tracks a 2-second progress window: when the
//   leader's PlayFootstep is firing (animating walk) AND the player's
//   net displacement over the last 2 seconds is under 0.5 m, runs an
//   8-cardinal probe (walls via the spatial change_detector cache +
//   nearby creature/placeable bodies) and speaks the clear directions
//   on the urgent channel. Once per stuck episode — re-arms only when
//   the player makes meaningful (>0.5 m) forward progress. Silence-
//   when-stuck remains the primary signal; the probe is the rescue
//   announce for "I tried multiple directions and nothing worked"
//   pocket geometries that normally trap follower-huddled blind play.
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

// Records that the engine just ticked PlayFootstep on the party leader.
// Stamps the timestamp the stuck-direction probe gates on. Called from
// OnPlayFootstep (same TU) without exposing the internal state.
void NoteLeaderFootstep();

}  // namespace acc::audio::footstep_suppress
