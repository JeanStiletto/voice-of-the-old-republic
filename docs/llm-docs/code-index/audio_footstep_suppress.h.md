# audio_footstep_suppress.h (47 lines)

Header for the stuck-via-footstep-suppression mechanism. Documents that the hook is a mid-function cut at the engine's own JZ (not a "clean" entry-point hook — earlier attempts landed cut bytes across TEST EAX,EAX and clobbered ZF, silencing all footsteps) and that NPC footsteps are gated on GetClientLeader identity like the engine's own priority-group assignment logic.

## Declarations (in source order)

- L37 — `void Tick()` — self-gates on player resolved; idempotent
- L41 — `bool WasStuckLastTick()` — read by OnPlayFootstep
- L45 — `void NoteLeaderFootstep()` — stamps stuck-direction-probe freshness timestamp
