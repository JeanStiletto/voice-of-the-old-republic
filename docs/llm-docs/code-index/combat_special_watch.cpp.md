# combat_special_watch.cpp (312 lines)

Implements the specials heartbeat. `IsRoutineAutoAttack` classifies a queued `CSWSCombatRoundAction` as routine only if action_type==1 (attack), attack_feat==0 (@+0x84, per Lane's type DB), and the target resolves to a Creature — anything else (feats, casts, item-uses, equips, object bashes, or a read fault) counts as "special" (fail-safe: better to under-silence than over-fire on corrupt state). `CountPartySpecials` walks every party member's combat_round via `GetPartyMembers`, falling back to the controlled creature alone if the party table is unreadable. The state machine (`State`) tracks combat-entry time, previous specials count, and last-fire time; fires the cue via `audio_bus::PlayCue` (2D, resref `c_drdastro_hit2`, chosen after `gui_actqueue` and `cb_gr_boncehard2` proved too soft). Priority group now rides the mod's shared full-volume cue group via `PlayCue`'s default arg (`audio::GetCuePriorityGroup`) rather than a hardcoded group 15.

## Declarations (in source order)

- L18 — `namespace acc::combat::special_watch`
- L26 — `constexpr const char* kCueResref = "c_drdastro_hit2"`
- L44 — `constexpr bool kCuePlayAs3D = false`
  note: reverted from a 3D-at-listener trick (small perceived loudness gain) as an overcomplicated quirk-exploit
- L49 — `constexpr DWORD kFirstRoundQuietMs = 6000`
- L52 — `constexpr DWORD kRepeatPeriodMs = 6000`
- L61 — `constexpr size_t kActionAttackFeatOffset = 0x84`
- L64 — `void* ReadCombatRound(void* serverCreature)`
- L81 — `bool IsRoutineAutoAttack(void* action)`
  note: fail-safe — any SEH fault or ambiguous field treats the action as special
- L123 — `int CountSpecialsForCreature(void* creature, const char* tag)`
  note: emits a per-item Combat.QueueRaw log line for every non-placeholder entry (intentionally noisy diagnostic)
- L177 — `int CountPartySpecials()`
- L202 — `struct State` (inCombatPrev/specialsPrev/combatEnteredAt/lastTickAt) / L209 `State& GetState()`
- L214 — `void ResetForExit(State& s)`
- L221 — `void FireCue(const char* reason, int specials, DWORD now)`
- L248 — `void Tick()`
  note: combat-entry edge → first-round gate → falling-edge-to-zero fires immediately → else 6s repeat heartbeat while empty
