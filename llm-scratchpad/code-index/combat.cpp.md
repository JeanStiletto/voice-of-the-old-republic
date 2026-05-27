# combat.cpp (1038 lines)

Implements all four combat narration phases (1A mode, 1B log, 4A attacks, 4B saves) plus
the msg-bus rule set that merges 3-4-line attack sequences (Summary / Angriffsstatistik /
Bedrohungsstatistik / Abwehrstatistik / Schadensstatistik) into compact single lines.
Also registers rules into acc::msg::Router.

## Declarations (in source order)

- L22 — `namespace acc::combat`
- L36 — `typedef int (__thiscall* PFN_GetCombatMode)(void* this_)`
- L38 — `bool ReadCombatMode(int& outMode)`
  note: inner helper; SEH-guarded CClientExoApp::GetCombatMode call
- L58 — `bool IsCombatActive()`
- L64 — `void TickCombatMode()`
  note: Phase 1A — stability-debounced speak on enter/exit; suppresses first tick
- L126 — `void* FindInGameMessagesPanel()`
  note: resolves persistent CSWGuiInGameMessages via CGuiInGame singleton (+0x1c), not panels[] walk
- L139 — `int ReadListBoxRowCount(void* lb)`
- L152 — `void* ReadListBoxRow(void* lb, int i)`
- L167 — `void TickCombatLog()`
  note: Phase 1B — log-only (no speech); speech is via OnAppendToMsgBuffer hook
- L260 — `struct AttackSnap`
  note: per-slot snapshot for attacks_list[7]; `initialized` flag prevents first-tick false-fires from heap-reuse
- L267 — `AttackSnap g_attackSnaps[kCombatAttackDataCount]`
- L270 — `bool ReadAttackData(void* combatRound, int slot, AttackSnap& out, short& outBaseDamage, uint32_t& outTarget, int& outResult, int& outDeflected, int& outCriticalThreat)`
- L299 — `void* ReadCombatRound(void* serverCreature)`
- L317 — `void ReadCreatureHpDirect(void* creature, int& outCur, int& outMax)`
  note: reads CSWSObject.hit_points @+0xe0 only; max stays 0 (accessor not called here)
- L329 — `void SpeakAttackOutcome(int result, short damage, int deflected, int criticalThreat, const char* attacker, const char* target, int targetHpCur)`
  note: Phase 4A — speech is intentionally OFF; logs only (OnAppendToMsgBuffer delivers richer text)
- L365 — `void TickAttackResolutions()`
  note: Phase 4A — walks player's attacks_list[7]; three gates: combat-active, initialized snap, damage-settle
- L502 — `void TickSavingThrows()`
  note: Phase 4B — permanent no-op skeleton; hook on SavingThrowRoll not yet landed
- L540 — `struct AttackBlock`
  note: accumulator for one in-flight attack sequence across multiple msg-bus lines
- L562 — `AttackBlock g_pending`
- L564 — `bool MsgStartsWith(const char* s, const char* p)`
- L569 — `void CopyRange(char* dst, size_t cap, const char* start, const char* end)`
- L578 — `bool ParseSummary(const char* text, AttackBlock& b)`
  note: msg-bus rule helper; opens a block on hit/miss anchor; extracts actor, target, feat, numeric totals
- L624 — `bool ParseAngriff(const char* text, AttackBlock& b)`
  note: msg-bus rule helper; parses Angriffsstatistik line; compresses verbose token labels to short forms
- L725 — `bool ParseAbwehr(const char* text, AttackBlock& b)`
  note: msg-bus rule helper; parses Abwehrstatistik; strips labels, keeps numbers only
- L763 — `bool ParseSchaden(const char* text, AttackBlock& b)`
  note: msg-bus rule helper; parses Schadensstatistik; produces both full-breakdown and short-form strings
- L874 — `void BuildCompact(const AttackBlock& b, char* out, size_t cap)`
  note: assembles full "actor verb target. Angriff N (comps) gg. Vert N (nums). dmg." string for feat-use or full-detail emit
- L937 — `void BuildShortForm(const AttackBlock& b, char* out, size_t cap)`
  note: "target: value type von actor[, kritisch]" — used only for vanilla hits landing on a party member
- L961 — `void FlushPending()`
  note: emit decision: feat-use -> BuildCompact+speak; vanilla hit on party -> BuildShortForm+speak; otherwise -> log-only
- L986 — `bool RuleSummary(const char* text)`
  note: msg-bus rule; opens a new AttackBlock; flushes any prior pending block first
- L994 — `bool RuleAngriff(const char* text)`
  note: msg-bus rule; requires open block; parses Angriffsstatistik line
- L999 — `bool RuleAbwehr(const char* text)`
  note: msg-bus rule; requires open block; parses Abwehrstatistik line
- L1004 — `bool RuleSchaden(const char* text)`
  note: msg-bus rule; requires open block; parses Schadensstatistik and flushes block
- L1011 — `bool RuleBedrohung(const char* text)`
  note: msg-bus rule; speaks Bedrohungsstatistik as-is mid-block (critical-threat roll cue)
- L1019 — `void OnUnmatched(const char* /*text*/)`
  note: unmatched line = block boundary; flushes pending (typically a miss with no Schadensstatistik)
- L1028 — `void RegisterCombatMsgRules()`
  note: registers all five rules + OnUnmatched into acc::msg::Router; must be called once at init
