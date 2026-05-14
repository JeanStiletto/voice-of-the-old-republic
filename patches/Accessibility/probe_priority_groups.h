// Priority-groups one-shot probe.
//
// Walks CExoSoundInternal.priority_groups[N] (a CPriorityGroup[]
// table the engine indexes via the `priority_group` byte passed to
// PlayOneShotSound / Play3DOneShotSound) and dumps each entry's
// volume/priority/distance/variance to the patch log.
//
// Background: PlayOneShotSound's volume slot is multiplied by the
// per-group volume scalar (decompile of CExoSoundSourceInternal::
// SetPriorityGroup @0x5dbb20, 2026-05-14). So changing the
// priority_group can implicitly amplify a cue several × without
// touching the byte volume — but only if the chosen group's
// `volume` field is higher than the default group 0. We don't know
// which groups have the highest volumes; this probe surfaces the
// table contents so we can pick the loudest group.
//
// Output (one block per session, fired once after engine stable):
//
//   [Probe.PriorityGroups] table @<addr> entries:
//   [Probe.PriorityGroups]   [00] vol=NN priority=NN max_player=NN
//                                  min_dist=F max_dist=F variance=F
//                                  fade_ms=NN
//   ...
//
// One-shot: arms internally, dumps once, then becomes a no-op.

#pragma once

namespace acc::probe::priority_groups {

// Per-tick driver. No-op after the first successful dump. Called
// from core_tick.cpp's Dispatch() after combat::TickCombatMode so
// the engine is stable.
void Tick();

}  // namespace acc::probe::priority_groups
