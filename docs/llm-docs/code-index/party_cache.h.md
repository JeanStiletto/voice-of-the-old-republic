# party_cache.h (23 lines)

Party-member-name cache. Snapshots display names from the engine's accessor — same bytes the engine puts in the message buffer — for byte-exact comparison in combat message filtering. Lazily refreshes on IsPartyMember calls (1s backstop).

## Declarations (in source order)

- L14 — `namespace acc::combat`
- L17 — `bool IsPartyMember(const char* name)`
  note: case-sensitive, byte-exact; lazily refreshes per refresh window
- L21 — `void InvalidatePartyCache()`
  note: optional; time-based backstop catches roster changes anyway
