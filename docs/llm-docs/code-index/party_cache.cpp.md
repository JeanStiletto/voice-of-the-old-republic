# party_cache.cpp (113 lines)

Caches party-member display names (companions via `GetPartyMembers` +
`GetObjectDisplayNameByHandle`, plus the active leader's name via
`GetActiveLeaderName`) for the combat msg-bus filter to classify "X attacks Y"
lines as party-vs-not. Lazily refreshes every 1s (`kRefreshIntervalMs`) or on
explicit invalidation. Deliberately uses `GetActiveLeaderName` and not
`GetPlayerCharacterName`, which returns a stale chargen-slot leftover that
never matches the combat log's PC name (this PC-inclusion logic is new since
the last index refresh — the prior version only cached companions).

## Declarations (in source order)

- L15-L21 — constants/statics: `kMaxMembers` (=`kPartyTableMaxMembers`), `kNameCap=96`, `kRefreshIntervalMs=1000`, `g_names[][]`, `g_name_count`, `g_last_refresh_tick`, `g_initialised`
- L24-L88 — `void Refresh()` — pulls companion names, trims trailing space/dot, appends dedup'd active-leader name, logs the resolved set
  note: with a companion leading (Tab'd), the non-leader PC can briefly fall out of the party set until the PC leads again — accepted as a known gap
- L90-L95 — `void RefreshIfStale()` — time-based backstop
- L99-L106 — `bool IsPartyMember(const char* name)` — case-sensitive byte-exact match, lazily refreshes first
- L108-L110 — `void InvalidatePartyCache()` — forces refresh on next call
