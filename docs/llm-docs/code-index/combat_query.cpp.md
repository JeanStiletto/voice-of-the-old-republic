# combat_query.cpp (608 lines)

Implements the three surfaces from combat_query.h. HP reads go through `CSWCLevelUpStats` at client-leader+0x2f8 (pregame_current_hp @+0x4c, max_hit_points @+0x4e) rather than the `CSWSCreature::GetMaxHitPoints` engine accessor, which gates internally on is_pc and returns garbage for companions. `BuildTargetCombatBrief` composes name + damage-level bucket (via `CSWSObject::GetDamageLevel`, masked to AL — the upper bytes carry comparison-flag garbage for buckets 0-3) + 2D distance + effects summary + main/off-hand weapon into one appendable string. `BuildEffectsSummary` prefers the effect-icon row (sighted-portrait parity) and falls back to the legacy `CSWSObject.effects` walk only when no icons exist (script-applied buffs with no EFFECTICON). `TickLeaderChangeAutoAnnounce` suppresses the leader-name speak during the ~3s grace window after an area-pointer change, since a save/module load re-establishes the party and would otherwise talk over the area-name cue.

## Declarations (in source order)

- L22 — `namespace acc::combat::query`
- L26 — `typedef int (__thiscall* PFN_GetIntThiscall)(void* this_)`
- L52-54 — `kClientCreatureLvlUpStatsOffset = 0x2f8`, `kClientStatsCurrentHpOffset = 0x4c`, `kClientStatsMaxHpOffset = 0x4e`
  note: verified live 2026-05-24 against the character-sheet display across PC + companions
- L56 — `int ReadCurrentHpFromClient(void* clientLeader)`
- L76 — `int ReadMaxHpFromClient(void* clientLeader)`
- L103 — `bool ReadEquippedItemName(void* serverCreature, size_t slotHandleOffset, const char* slotLabel, char* outBuf, size_t outBufSize)`
  note: walks CSWSCreature.inventory @+0xa2c → slot handle → GetObjectDisplayNameByHandle, SEH-guarded per hop
- L145 — `float ComputePlayerDistanceMeters(void* targetObject)`
  note: 2D only, z ignored — matches cycle_state's distance convention
- L159 — `struct BriefBuf` / L165 `void BriefAppend(BriefBuf&, const char*, ...)`
  note: appendable composer, never overflows, no-op once saturated
- L180 — `int ReadEffectCount(void* serverObject)`
- L201 — `int BuildEffectIconSummary(void* serverObject, char* outBuf, size_t outBufSize)`
  note: reads CSWSCreature.effect_icons — same priority-sorted/deduped order the sighted portrait renders; capped at 5
- L253 — `bool BuildEffectsSummary(void* serverObject, char* outBuf, size_t outBufSize)`
  note: icon row first; legacy CSWSObject.effects walk only as fallback
- L306 — `int ReadDamageLevelDirect(void* serverObject)`
  note: mask to AL (& 0xFF) — full 32-bit read blows the range check for buckets 0-3
- L324 — `acc::strings::Id DamageLevelStringIdFor(int level)`
- L339 — `void TickLeaderChangeAutoAnnounce()`
  note: area-pointer-change gate (3s grace) prevents talking over the area-name cue after a load/transition
- L416 — `bool BuildTargetCombatBrief(void*, const char*, char*, size_t)`
  note: name → condition (damage-level, skips healthy) → distance → effects → main-hand → off-hand
- L507 — `void SpeakSelfStatus()`
  note: prefers "%d of %d" HP; falls back to cur-only when max resolves to 0 (driving/minigame creature shapes)
- L582 — `void PollWin32SelfStatusHotkey()`
  note: gated on player-loaded + not-UI-blocking
