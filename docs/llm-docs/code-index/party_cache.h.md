# party_cache.h (24 lines)

Declares the party-member-name cache used by the combat message-bus filter.
Snapshots the same display-name bytes the engine puts into its own message
buffer, so string comparison stays locale-clean without a separate name-to-id
map.

## Declarations (in source order)

- L17 — `bool IsPartyMember(const char* name)` — case-sensitive, byte-exact; lazily refreshes per window
- L21 — `void InvalidatePartyCache()` — optional forced refresh; time-based backstop catches roster changes regardless
