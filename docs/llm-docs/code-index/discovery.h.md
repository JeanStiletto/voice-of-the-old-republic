# discovery.h (64 lines)

Middle tier of object cycling: records organically-narrated static/named objects per area and persists the index in the save, so the everyday `,`/`.` cycle resurfaces known objects without extended/map-side spoilers. Storage lives on the player creature's CSWSScriptVarTable. Keys are locale-independent (`N~tag` / `S~tag~ordinal`); names are regenerated live at cycle time. Access: discovery is the DEFAULT in-world cycle filter; "Extended cycling" setting widens back to everything-on-map.

## Declarations (in source order)

- L40 — `void OnAreaChanged(void* area)`
  note: idempotent on same area; actual save-var read deferred to Tick()
- L45 — `void Tick()`
  note: per-tick driver from core_tick; deferred load once player creature is stable
- L56 — `void Record(void* gameObject)`
  note: called ONLY from organic narration sites, plus one internal auto-seed exception for combat-critical placeables
- L61 — `bool IsDiscovered(void* gameObject)`
  note: false until the area's set has reconciled from the save
