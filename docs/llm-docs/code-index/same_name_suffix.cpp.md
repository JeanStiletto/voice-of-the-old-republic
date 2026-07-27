# same_name_suffix.cpp (227 lines)

Implements the disambiguator declared in same_name_suffix.h. Keeps two parallel
vectors: `s_buckets` (one per distinct LocName seen this area, tracking total
serials ever assigned) and `s_entries` (one per narrated object handle, with
its bucket index + serial). `AppendSuffix` assigns/looks up a handle's serial
(re-keying if the LocName changes under it, e.g. polymorph); `AppendAreaPositionOrdinal`
instead ranks the object against every live same-category peer sharing its
spoken name, via `PositionLess`, giving a pure function of world position.
`GetSpokenName` chains GetObjectName -> AppendDisambiguator -> state_overrides's
`AppendStateLabel` -> a local `AppendEmptyContainerLabel` (trailing ", leer"
tag for emptied loot containers, self-gating on `engine::IsEmptyContainer`).
`Reset()` clears both vectors on area transition.

## Declarations (in source order)

- L21 — `struct Entry { uint32_t handle; int bucketIdx; int serial; char locName[64]; }`
- L29 — `struct Bucket { char locName[64]; int size; }`
- L34 — `std::vector<Entry> s_entries; std::vector<Bucket> s_buckets;`
- L37 — `int FindBucketIdx(const char* name)`
- L46 — `int GetOrCreateBucketIdx(const char* name)`
- L57 — `Entry* FindEntry(uint32_t handle)`
- L70 — `void AppendEmptyContainerLabel(void* gameObject, char* outBuf, size_t bufSize)`
  note: no-op unless engine::IsEmptyContainer; reads item count fresh, no caching
- L83 — `void AppendSuffix(...)`
  note: bucket key = current outBuf contents snapshotted before mutation; rekeys entry->bucketIdx on LocName change
- L136 — `bool PositionLess(const Vector& a, const Vector& b)`
- L142 — `void AppendAreaPositionOrdinal(...)`
  note: ranks via AreaObjectIterator scan of ALL same-category objects whose GetObjectName matches, not just discovered ones
- L188 — `void AppendDisambiguator(...)`
- L202 — `bool GetSpokenName(...)`
- L217 — `void Reset()`
  note: logs entry/bucket counts only when non-empty
