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
// CRITICAL: on both games the hook site is mid-function at the engine's
// own early-out branch (K1 JZ, K2 CMP+JNZ — with INVERTED return polarity;
// see the handler comment in the .cpp). Earlier K1 attempts at "cleaner"
// entry-point hooks silenced all footsteps because cut bytes landed across
// TEST EAX,EAX, clobbering ZF before the engine's downstream JZ. See
// hooks.toml / kotor2.hooks.toml around OnPlayFootstep for the forensic
// detail and the EFLAGS / EAX wrapper-bug notes. Do not "simplify" without
// re-reading.
//
// was_stuck reflects the PREVIOUS tick's displacement (~33ms lag at 30Hz).
//
// Stuck-direction probe: when the leader is animating walk AND net
// displacement over the last 2 seconds is < 0.5 m, runs an 8-cardinal
// probe and speaks clear directions once per stuck episode. Rearms when
// the player makes >0.5 m progress.

#pragma once

#include <cstdint>   // LastLeaderFootstepMs

namespace acc::audio::footstep_suppress {

// Self-gates on player resolved; idempotent.
void Tick();

// Was the most-recent Tick a sub-epsilon displacement? Read by
// OnPlayFootstep.
bool WasStuckLastTick();

// True iff the stuck state has been continuously held for at least min_ms.
// Read by OnUpdateRollingFootstep: the droid drive LOOP needs a stricter
// verdict than the one-shot footsteps, because sub-second stuck dips are
// normal during course corrections and pivots — muting a continuous loop on
// those reads as broken audio, while skipping a single footstep click is
// inaudible. (2026-08-05 K2 T3 feedback: loop went silent during ordinary
// walking.)
bool StuckSustainedFor(unsigned int min_ms);

// Stamps the timestamp the stuck-direction probe gates on. Called from
// OnPlayFootstep without exposing internal state.
void NoteLeaderFootstep();

// GetTickCount64() at the last leader footstep, 0 if none this session.
//
// Second reader, added 2026-08-14: the dead-keyboard watchdog treats this as a
// liveness channel. A footstep is the engine acting on a held movement key, and
// it keeps firing when the player is walking into a wall — the one case where
// position and camera yaw both stay put while the keyboard is demonstrably
// fine. Witnessed on K2 (patch-20260813-232824.log, "Sackgasse" dead end): the
// engine logged footsteps with stuck=1 throughout, and the watchdog still armed
// three times because it could not see them.
uint64_t LastLeaderFootstepMs();

}  // namespace acc::audio::footstep_suppress
