# discovery.cpp (337 lines)

Per-area "organically discovered" object index, persisted into the save via one CSWSScriptVarTable string var per area (`ACC_DISC_<areaTag>`), keyed by locale-independent tags (`N~<tag>` for unique NPCs, `S~<tag>~<northToSouthOrdinal>` for static objects). Backs the default in-world `,`/`.` cycle tier (discovery.h documents the model). Talks to engine_area (iteration/tag/position), engine_player, engine_scriptvar, filter_objects (category classification).

## Declarations (in source order)

- L23-30 — module state: `g_areaTag`, `g_varName`, `g_keys` (vector<string>), `g_area`, `g_loaded`, `g_creature`, `g_settle`
- L36 — `constexpr int kSettleTicks = 60` — ~1s wait for stable player-creature pointer before reading save var (avoids racing CSWSObject::LoadObjectState)
- L39-40 — `constexpr size_t kMaxKeys = 400`, `kReadBufSize = 16384`
- L45 — `bool PositionLess(const Vector& a, const Vector& b)` — north-to-south total order, mirrors cycle_input::PositionLess
- L52 — `const acc::filter::CycleCategory kEligible[]` — Door, Npc, Container, Landmark, Transition
- L60 — `acc::filter::CycleCategory Classify(void* obj)`
- L70 — `bool DeriveKey(void* obj, void* area, std::string& outKey)`
  note: NPC key requires the tag to be unique among area creatures (dedups generic mobs); static key appends a rank computed via PositionLess among same-category/same-tag peers
- L126 — `bool Contains(const std::string& key)`
- L133 — `void LoadFromSave()` — parses `;`-joined string var into g_keys
- L156 — `void Persist()` — re-serializes g_keys back into the save var
- L174-196 — `struct AreaSeed`, `kSeedTagsStaM45ab/ac/ad[]`, `kAreaSeeds[]`
  note: combat-critical Star Forge placeables (turret computers, droid terminals, captive Jedi) auto-recorded on area load so mid-fight cycling reaches them without a prior discovery walk
- L201 — `void ApplySeeds()`
- L232 — `void OnAreaChanged(void* area)` (public)
  note: per-area key = module resref (stable/language-independent) preferred over the area GFF Tag (defaults to useless "untitled"); idempotent on same area+key; defers the actual save-var read to Tick()
- L269 — `void Tick()` (public)
  note: requires the SAME non-null player-creature pointer for kSettleTicks consecutive ticks before LoadFromSave+ApplySeeds
- L288 — `void Record(void* gameObject)` (public)
  note: self-syncs area via OnAreaChanged if transitions hasn't fired yet; drops (does not record) during the pre-load settle window to avoid clobbering saved data; caps at kMaxKeys with a one-time warn
- L320 — `bool IsDiscovered(void* gameObject)` (public)
