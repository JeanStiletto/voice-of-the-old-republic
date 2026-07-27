# combat_query.h (33 lines)

Read-only combat queries over `creature_stats @+0xa74` and related engine surfaces. Surfaces: leader-change auto-announce (Tab speaks the new leader's name), cycle/passive-narrate target-brief enrichment for Creature-kind targets, and the bare-H self-status readout (HP/FP/effects/equipped weapons). Every entry self-gates on player-loaded.

## Declarations (in source order)

- L15 — `namespace acc::combat::query`
- L18 — `void TickLeaderChangeAutoAnnounce()`
- L23 — `bool BuildTargetCombatBrief(void* targetServerObject, const char* targetName, char* outBuf, size_t outBufSize)`
  note: caller has already resolved targetServerObject; not re-resolved here
- L29 — `void SpeakSelfStatus()`
  note: bare H — no name/distance (always self)
- L31 — `void PollWin32SelfStatusHotkey()`
