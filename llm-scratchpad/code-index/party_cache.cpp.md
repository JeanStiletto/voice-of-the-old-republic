# party_cache.cpp (85 lines)

Party cache implementation. Calls GetPartyMembers + GetObjectDisplayNameByHandle, trims trailing whitespace, stores up to kPartyTableMaxMembers names. Refreshes when cache is older than 1s. Logs the resolved party set for debugging name-byte parity.

## Declarations (in source order)

- L11 — `namespace acc::combat`
- L24 — `static void Refresh()` (anonymous namespace)
- L63 — `static void RefreshIfStale()` (anonymous namespace)
- L72 — `bool IsPartyMember(const char* name)`
- L81 — `void InvalidatePartyCache()`
