# endar_softlock.cpp (275 lines)

Endar Spire Command Module (END_M01AA) softlock guard: hangs a rule off the feedback router's generic "locked" report (never off interact dispatch, to avoid suppressing story-trigger door-opens) to speak graduated guidance when the player repeatedly pokes the room5-gated bridge door, plainly explain doors that are genuinely sealed (cut2 battle door, post-sacrifice Trask door), and edge-log the room3/5/7/8/sith/bridge-combat plot-global timeline. Talks to engine_area (resref/globals/tag), engine_player (combat/party state), locked_recall, msg_router, narrated_target, prism, strings.

## Declarations (in source order)

- L21 — `constexpr char kCommandModule[] = "end_m01aa"`
- L27 — `constexpr char kRoom5Door[] = "end_door16"` — the only reported softlock chokepoint
- L35 — `constexpr char kCut2Door[] = "end_door10_cut2"`
  note: blueprint-unlocked; reaching "locked" here means the engine truly refused — must NEVER be spoken in place of an actual open attempt (v0.6.1 regression)
- L51 — `constexpr char kTraskDoor[] = "end_door19"`
  note: locked=1 from load; first "locked" report IS the story trigger (level-up gate), second (companion roster empty) means genuinely sealed
- L57 — `constexpr int kReloadHintAfterAttempts = 3`
- L61 — `constexpr DWORD kHintDelayMs = 500` — lands after the engine's own locked bark
- L64 — `constexpr DWORD kDiagScanIntervalMs = 500`
- L66-73 — visit state: `g_inModule`, `g_door16Attempts`, `g_spokeBattleHint`, `g_spokeReloadHint`, `g_hasPendingHint`, `g_pendingHint`, `g_pendingHintAtMs`
- L78 — `struct Snapshot { room3,room5,room7,room8,sith,bridgeCombat,inCombat; operator==; }` — -2 sentinel forces first emit
- L89 — `bool EqualsCI(const char* a, const char* b)`
- L99 — `bool IsCommandModule()`
- L105 — `Snapshot ReadSnapshot()` — via `ReadGlobalNumber` + `IsAnyPartyMemberInCombat`
- L118 — `void LogSnapshot(const char* why, const Snapshot& s)`
- L126 — `void ResetVisit()`
- L138 — `int CompanionCount()` — active companions excluding PC, via `GetPartyMembers`
- L147 — `void QueueHint(acc::strings::Id which)` — deferred so it follows the router's own "locked" line
- L155 — `void OnDoorLocked(const char* tag)`
  note: dispatches per-tag: cut2 (always sealed line), Trask door (only sealed once companion roster is empty), room5 door (graduated battle-hint → reload-hint after kReloadHintAfterAttempts)
- L216 — `bool RuleEndarDoorLocked(const char* text)`
  note: router rule; never consumes — reads the acting object from `narrated_target::TryGet`, dispatches to OnDoorLocked by tag
- L235 — `void RegisterMsgRule()` (public)
- L239 — `void OnAreaChanged(void*)` (public) — resets visit state on module enter/exit, logs entry snapshot
- L252 — `void Tick()` (public) — flushes queued hint after kHintDelayMs, throttled diagnostic edge-log of plot-state changes
