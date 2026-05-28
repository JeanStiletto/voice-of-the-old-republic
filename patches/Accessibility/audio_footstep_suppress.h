// Stuck-detection via footstep suppression.
//
// When the engine plays a walk animation (which fires PlayFootstep) but
// the character isn't actually moving (sub-epsilon displacement between
// ticks), we suppress the footstep. Silence signals "stuck" to a blind
// player without introducing a new cue.
//
// Mechanism:
//   - Tick() samples GetPlayerPosition each frame; sets g_was_stuck if
//     displacement < kStuckEpsilonMeters.
//   - OnPlayFootstep detour returns 1 (consume via LAB_0061a632 early-
//     return — skips audio + footprint + rumble together) when the
//     creature is the player leader AND g_was_stuck.
//
// NPC footsteps must NOT be suppressed; the handler gates on
// GetClientLeader identity, same check the engine uses internally for
// priority-group assignment.
//
// CRITICAL: hook site is mid-function at engine's own JZ. Earlier attempts
// at "cleaner" entry-point hooks silenced all footsteps because cut bytes
// landed across TEST EAX,EAX, clobbering ZF before the engine's downstream
// JZ. See hooks.toml around OnPlayFootstep for the forensic detail and
// the EFLAGS / EAX wrapper-bug notes. Do not "simplify" without re-reading.
//
// was_stuck reflects the PREVIOUS tick's displacement (~33ms lag at 30Hz).
//
// Stuck-direction probe: when the leader is animating walk AND net
// displacement over the last 2 seconds is < 0.5 m, runs an 8-cardinal
// probe and speaks clear directions once per stuck episode. Rearms when
// the player makes >0.5 m progress.

#pragma once

namespace acc::audio::footstep_suppress {

// Self-gates on player resolved; idempotent.
void Tick();

// Was the most-recent Tick a sub-epsilon displacement? Read by
// OnPlayFootstep.
bool WasStuckLastTick();

// Stamps the timestamp the stuck-direction probe gates on. Called from
// OnPlayFootstep without exposing internal state.
void NoteLeaderFootstep();

}  // namespace acc::audio::footstep_suppress
