# probe_priority_groups.h (36 lines)

Priority-groups one-shot probe.

Walks CExoSoundInternal.priority_groups[N] (the CPriorityGroup[] table
the engine indexes via the `priority_group` byte passed to
PlayOneShotSound / Play3DOneShotSound) and dumps each entry's
volume/priority/distance/variance to the patch log.

Background: PlayOneShotSound's volume slot is multiplied by the per-group
volume scalar (decompile of CExoSoundSourceInternal::SetPriorityGroup,
2026-05-14). Changing the priority_group can implicitly amplify a cue
several times without touching the byte volume — but only if the chosen
group's `volume` field is higher than default group 0. Purpose: surface
the table contents so we can pick the loudest group.

One-shot: arms internally, dumps once, then becomes a no-op.

## Declarations (in source order)

- L29 — `namespace acc::probe::priority_groups`
- L34 — `void Tick()`
  note: no-op after first successful dump; waits ~2s after first observation for sound engine to stabilise before dumping
