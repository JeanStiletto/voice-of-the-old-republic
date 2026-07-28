# log.cpp (638 lines)

Implementation of the DLL's logging primitives: session file at
`<install>/logs/patch-<utc>.log`, optional `OutputDebugStringA` mirror
gated on `IsDebuggerPresent()` (an idle DBWin listener would otherwise
serialise every log line and stall the game's tick — see comment at L109).
One `CRITICAL_SECTION` (`g_lock`) guards the file handle and all dedup
state, since hooks can fire off the main thread (audio dispatch, async
loaders). Three dedup mechanisms share the "flush on tag mismatch or staleness"
shape: per-tag content dedup (`Trace`), state-transition dedup (`Edge`), and
whole-block dedup (`BlockLog`, FNV-1a hashed, optionally keyed by a
volatile-pointer-free `Key()` stream so re-created panels with fresh heap
addresses still fold). `Init` resolves the patch dir + log path from the
DLL's own module path and writes a session-start header (UTC/local time,
locale, PID).

## Declarations (in source order)

- L13 — path/handle statics: `g_logDir`, `g_logPath`, `g_patchDir`, `g_logFile`, `g_logOpenAttempted`
- L23 — `CRITICAL_SECTION g_lock` + `struct Locker` (RAII enter/leave)
- L37 — dedup constants: `kMessageBuf=1024`, `kMaxKeys=96`, `kStaleMs=1000`
- L41 — `struct DedupEntry` — per-tag Trace/Edge/Once state; L53 `g_keys[]`/`g_keyCount`
- L56 — `DedupEntry* GetOrCreateEntry(const char* tag)` — strcmp-matched, table-full falls back to plain write
- L79 — `struct PtrName` + `g_ptrNames[]` — symbolic pointer-name registry for FmtPtr
- L88 — `void RawWriteLocked(const char* tag, const char* content)` — timestamp format, OutputDebugString gate, lazy file open
- L141 — `void FlushTraceLocked` / L150 `FlushEdgeHoldLocked` — emit "(repeated Nx more)" / "(prev state held Nx)" footers
- L165 — `struct BlockEntry` — per-tag block-hash dedup state; L176 `g_blocks[]`/`g_blockCount`, `kMaxBlockKeys=16`
- L179 — `uint64_t Fnv1a64(const char* p, size_t n)`
- L188 — `BlockEntry* GetOrCreateBlock` / L204 `FlushBlockLocked` / L214 `EmitBlockLinesLocked` (splits '\n'-joined block into per-line RawWriteLocked calls)
- L232 — `void SweepStaleLocked()` — auto-flushes any dedup entry idle >1s; called at the top of every public helper
- L254 — `void Init(HINSTANCE hinstDLL)` — resolves patch dir + timestamped log path, writes session-start header
- L317 — `void Shutdown()` — flushes all dedup counters, closes file; lock stays initialized for late DLL_PROCESS_DETACH writes
- L338 — `void Write(const char* tag, const char* fmt, ...)`
- L352 — `void Trace(const char* tag, ...)` — content-dedup per tag
- L389 — `void Once(const char* tag, ...)` — first-observation-only per tag
- L412 — `void Edge(const char* tag, int state, ...)` — state-transition dedup, held-count footer
- L448 — `void WriteHex(const char* tag, const char* label, const void* bytes, size_t len)` — 64-bytes-per-line hex dump
- L472 — `void RegisterPtr` / L487 `FmtPtr` — symbolic names for known pointers, rotating buffer pool of 8 for concurrent calls
- L503 — `void FlushAll()`
- L519 — `BlockLog::BlockLog/~BlockLog` (RAII, calls End() on destruction)
- L527 — `BlockLog::Key(fmt,...)` — folds into a running FNV-1a hash only, never emitted (identity contribution)
- L545 — `BlockLog::Line(fmt,...)` — buffers display text; overflow (>16KB) switches to passthrough (un-deduped) writes
- L576 — `BlockLog::End()` — identity = Key() stream if used else full-text hash; folds identical consecutive blocks
- L618 — `const char* PatchDir()`
- L622 — `ULONGLONG g_bringupBaselineMs`
- L626 — `void BringupMark(const char* name)` — first call sets baseline (t+0ms), subsequent calls report elapsed ms
