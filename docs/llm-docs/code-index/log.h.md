# log.h (123 lines)

Public logging API. Every line carries a dotted "Tag" prefix
("Menus.Chain"). No verbosity levels — dedup is structural: Trace/Edge/Once
collapse high-frequency repeats while preserving the suppressed count in
full fidelity (per feedback_log_no_rate_limits — never rate-limit
diagnostics, only fold exact repeats). `BlockLog` does the same for
multi-line snapshots (panel walks, manager-stack dumps) where the fold
would otherwise need to span interleaved strides.

## Declarations (in source order)

- L21 — `void Init(HINSTANCE hinstDLL)` — call from DllMain on DLL_PROCESS_ATTACH
- L24 — `void Shutdown()` — idempotent
- L27 — `void Write(const char* tag, const char* fmt, ...)`
- L32 — `void Trace(const char* tag, const char* fmt, ...)` — per-tag content dedup, "(repeated Nx more)" footer
- L35 — `void Once(const char* tag, const char* fmt, ...)`
- L39 — `void Edge(const char* tag, int state, const char* fmt, ...)` — emits only on state change, with held-count of the previous state
- L42 — `void WriteHex(const char* tag, const char* label, const void* bytes, size_t len)`
- L79 — `class BlockLog`
  note: identity is full display text by default; call Key() alongside Line() to strip volatile pointers from the fold identity (Key() costs nothing extra — it folds into a running hash, no buffer); oversized (>16KB) blocks fall back to un-deduped direct writes
  - `Line(fmt, ...)` — display text, always emitted
  - `Key(fmt, ...)` — identity contribution only, never emitted
  - `End()` — runs automatically on scope exit too
- L104 — `void RegisterPtr(const void* ptr, const char* name)` / `const char* FmtPtr(const void* ptr)`
  note: FmtPtr uses a rotating static buffer so multiple calls in one log line coexist
- L107 — `void FlushAll()`
- L116 — `void BringupMark(const char* name)` — first call captures baseline, later calls report t+Nms from it
- L120 — `const char* PatchDir()` — absolute `<install>\patches` path, empty before Init; used by prism.cpp for SetDllDirectory
