# combat_special_watch.cpp (324 lines)

Implements the specials-queue heartbeat cue (Phase 3 side-channel). Walks all party
members' combat_round.actions each tick, counts "specials" (non-routine actions), and
plays an audio cue when the count drops to zero or repeats on a 6s cadence.

## Declarations (in source order)

- L18 — `namespace acc::combat::special_watch`
- L26 — `constexpr const char* kCueResref`
  note: "c_drdastro_hit2" — astromech droid hit; chosen over gui_actqueue/cb_gr_boncehard2 for better audibility under combat audio
- L42 — `constexpr uint8_t kCuePriorityGroup`
  note: priority group 15 — same as weapon swings, vol=127; competes on equal footing with combat SFX
- L49 — `constexpr uint8_t kCueVolumeByte`
- L56 — `constexpr bool kCuePlayAs3D`
  note: false — reverted from 3D-at-listener trick; clean 2D, same API as nav cues
- L61 — `constexpr DWORD kFirstRoundQuietMs`
  note: 6000ms — matches KOTOR's 6s round length; keeps "Kampf beginnt" announcement clean
- L64 — `constexpr DWORD kRepeatPeriodMs`
- L73 — `constexpr size_t kActionAttackFeatOffset`
  note: @+0x84 on CSWSCombatRoundAction — feat ID for Power Attack / Flurry / Critical Strike; non-zero means special
- L76 — `void* ReadCombatRound(void* serverCreature)`
- L93 — `bool IsRoutineAutoAttack(void* action)`
  note: true only for action_type=1 + feat=0 + Creature-kind target; fault returns false (fail-safe, better to silence than over-fire)
- L135 — `int CountSpecialsForCreature(void* creature, const char* tag)`
  note: walks one creature's action list; emits per-item diagnostic log for every non-placeholder item (intentionally noisy during development)
- L189 — `int CountPartySpecials()`
  note: aggregates CountSpecialsForCreature over all party members; falls back to leader-only when party table unreadable
- L214 — `struct State`
  note: watcher state machine — inCombatPrev, specialsPrev, combatEnteredAt, lastTickAt; resets cleanly across combat boundaries
- L221 — `State& GetState()`
- L226 — `void ResetForExit(State& s)`
- L233 — `void FireCue(const char* reason, int specials, DWORD now)`
  note: plays kCueResref via PlayCue (2D) or PlayCue3D (if kCuePlayAs3D); updates lastTickAt
- L260 — `void Tick()`
  note: state machine: combat-entry edge sets baseline; first-round gate; edge-fire on specials >=1->0; repeat heartbeat when specials==0 and period elapsed
