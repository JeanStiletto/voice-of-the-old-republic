# trap_watch.h (52 lines)

Public surface for sighted-parity trap ("mine") awareness. Mirrors the
engine's own detected-by list model (CSWSCreature::UpdateMineCheck) so a
blind player learns about a trap at the exact moment a sighted player
would see its red ground overlay — never earlier. Full engine model in
docs/llm-docs/mine-trap-model.md.

## Declarations (in source order)

- L33 — `namespace acc::trap_watch`
- L38 — `void Tick()`
  note: internally throttled; scans area's trappable objects + proximity warning
- L44 — `void ScanNow()`
  note: rate-limited forced rescan (min 100ms); combat.cpp's RuleMineDetect calls this to beat the engine's own feedback-line race
- L49 — `bool PeekFreshMine(char* nameOut, size_t nameSize, Vector& posOut)`
  note: freshest not-yet-consumed ground-mine detection; entries expire after a few seconds
- L50 — `void ConsumeFreshMine()`
