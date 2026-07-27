# trap_watch.cpp (284 lines)

Implements trap/mine detection watching. Scans the current area's
triggers/doors/placeables every 250ms, checking each object's
detected-by list against the party's server handles
(`TrapDetectedByAnyOf`). Ground mines (trigger kind) get NO direct
speech here — the engine already emits a combat feedback line, so this
module just stashes the freshest detection (`PushFresh`/`PeekFreshMine`/
`ConsumeFreshMine`) for combat.cpp's `RuleMineDetect` to enrich with
clock-direction + distance. Trapped doors/placeables get NO engine
feedback line, so this module announces them directly. A detected
ground mine within 4m fires one proximity warning (re-arms only past
8m — deliberate no-repeat, the player is expected to disarm or step
over it). Talks to: engine_area (AreaObjectIterator, TrapDetectedByAnyOf,
is_trap offset), engine_compass (ClockPosition), engine_player (party
handles, position/yaw), prism, strings/strfmt.

## Declarations (in source order)

- L27 — `kScanIntervalMs = 250` — throttled scan cadence
  note: detection transitions are driven by the engine's own 100ms/3s UpdateMineCheck cadence
- L33-34 — `kMineWarnEnterM = 4.0f`, `kMineWarnExitM = 8.0f` — proximity warning hysteresis
- L39 — `kFreshMineExpireMs = 4000` — fresh-detection entries age out if no combat line arrives
- L41 — `struct TrackedTrap` — handle/detected/warnArmed/isMine/pos
- L49-51 — `kMaxTracked = 96`, `g_tracked[]`, `g_tracked_count`
- L53-62 — `struct FreshMine` (2 slots) — name/pos/tick for combat-line enrichment
- L64-65 — `g_area`, `g_last_scan` — module state
- L67 — `TrackedTrap* FindOrAddTracked(uint32_t handle)`
- L78 — `void PushFresh(name, pos, now)` — evicts oldest of the 2 slots
- L91 — `void ExpireFresh(now)`
- L98 — `bool TriggerIsTrap(void* trigger)` — SEH-guarded CSWSTrigger.is_trap read
- L111 — `void TrapSpokenName(obj, isMine, out, size)`
  note: falls back to the localized "mine" noun when the authored name is empty or (for mines) looks like a raw script tag (contains '_')
- L122 — `std::string WithClock(label, target)` — "<label>, auf X Uhr, Y Meter" via shared format; falls back to bare label if pos/yaw unavailable
- L141 — `void ScanInternal(now)` — forward-declared, defined at L160
- L143 — `void Tick()` — throttled entry point
- L154 — `void ScanNow()` — 100ms-floor forced rescan for the message-enrichment race
- L160 — `void ScanInternal(now)` — area-change reset, party-handle collection, per-object
  iterate (trigger/door/placeable), detected-transition handling (mine: PushFresh only;
  door/placeable: direct SpeakUrgent-free `prism::Speak`), proximity warning state machine
- L263 — `bool PeekFreshMine(nameOut, nameSize, posOut)` — newest valid fresh entry
- L276 — `void ConsumeFreshMine()`
