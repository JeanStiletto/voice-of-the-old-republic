# log.cpp (430 lines)

Logging implementation. File opened lazily on first write (creates logs/ dir). CRITICAL_SECTION guards file handle and all dedup state. DedupEntry tracks per-tag content, suppression count, edge state, once-flag, and stale timestamp. Stale entries (>kStaleMs=1000ms idle) auto-flush in SweepStaleLocked called at every entry point. Pointer registry supports up to kMaxPtrNames=32 entries.

## Declarations (in source order)

- L7 — `namespace acclog`
- L25 — `struct Locker` (anonymous namespace)
- L40 — `struct DedupEntry` (anonymous namespace)
- L55 — `static DedupEntry* FindEntry(const char* tag)` (anonymous namespace)
- L62 — `static DedupEntry* GetOrCreateEntry(const char* tag)` (anonymous namespace)
- L83 — `struct PtrName` (anonymous namespace)
- L93 — `static void RawWriteLocked(const char* tag, const char* content)` (anonymous namespace)
- L133 — `static void FlushTraceLocked(DedupEntry* e)` (anonymous namespace)
- L142 — `static void FlushEdgeHoldLocked(DedupEntry* e)` (anonymous namespace)
- L153 — `static void SweepStaleLocked()` (anonymous namespace)
- L169 — `void Init(HINSTANCE hinstDLL)`
- L232 — `void Shutdown()`
- L250 — `void Write(const char* tag, const char* fmt, ...)`
- L264 — `void Trace(const char* tag, const char* fmt, ...)`
- L301 — `void Once(const char* tag, const char* fmt, ...)`
- L324 — `void Edge(const char* tag, int state, const char* fmt, ...)`
- L360 — `void WriteHex(const char* tag, const char* label, const void* bytes, size_t len)`
- L384 — `void RegisterPtr(const void* ptr, const char* name)`
- L399 — `const char* FmtPtr(const void* ptr)`
- L415 — `void FlushAll()`
- L426 — `const char* PatchDir()`
