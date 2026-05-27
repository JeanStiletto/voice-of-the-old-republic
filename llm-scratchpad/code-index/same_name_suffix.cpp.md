# same_name_suffix.cpp (140 lines)

Implementation of the same-name suffix disambiguator. Uses an Entry/Bucket pair of vectors; linear lookup; handles re-bucketing when a name changes under an existing handle (polymorph, script swap).

## Declarations (in source order)

- L12 — `namespace acc::narration`
- L14 — `namespace { // anonymous`
- L19 — `struct Entry`
  note: one per game object ever narrated in the current area; bucketIdx + serial are stable across the area lifetime
- L27 — `struct Bucket`
  note: one per distinct LocName ever seen; size counts total serials assigned (including dead members, so living members keep their number)
- L32 — `std::vector<Entry>  s_entries;`
- L33 — `std::vector<Bucket> s_buckets;`
- L35 — `int FindBucketIdx(const char* name)`
- L43 — `int GetOrCreateBucketIdx(const char* name)`
- L55 — `Entry* FindEntry(uint32_t handle)`
- L62 — `} // namespace (anonymous)`
- L64 — `void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize)`
  note: suffix only appended when bucket.size >= 2; re-keys entry on name-change without disturbing other bucket members
- L117 — `bool GetSpokenName(void* gameObject, char* outBuf, size_t bufSize)`
- L130 — `void Reset()`
