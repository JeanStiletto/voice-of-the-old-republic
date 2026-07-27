# AnalyzeDumpCommand.cs (677 lines)

`kdev analyze-dump` — opens a Windows minidump (WER `%LOCALAPPDATA%\CrashDumps`), extracts the exception record + crashing-thread register state via a hand-rolled minidump exception-stream reader (ClrMD doesn't expose it), walks the EBP frame chain, ESP-scans for code-shaped return addresses, and resolves every code address against `Models.FunctionTable` (Lane's Ghidra XML). Built for the KOTOR Tab-crash investigation. Also supports `--list` (newest-first dump listing), `--modules` (loaded module list via ClrMD `DataTarget`), and `--peek` (raw byte read at an arbitrary VA — used to inspect the SteamStub-decrypted `.text` that static tools can't disassemble). Talks to `KdevConfig` (for the Ghidra XML path) and `Models.FunctionTable`.

## Declarations (in source order)

- L22 — `static class AnalyzeDumpCommand`
- L24 — `const string DefaultDumpDir = "%LOCALAPPDATA%\CrashDumps"`
- L25 — `const string DefaultGhidraXml = "docs\llm-docs\re\k1_win_gog_swkotor.exe.xml"`
- L27 — `Command Build()` — wires `dump` arg + `--list`/`--depth`/`--stack-bytes`/`--peek`/`--peek-len`/`--modules` options
- L103 — `int RunModules(string? dumpArg)` — lists loaded modules via ClrMD `DataTarget.LoadDump`
- L146 — `int RunPeek(string? dumpArg, string vaStr, int len)` — reads raw bytes at a VA and hex-dumps them
- L199 — `bool TryParseHex(string s, out uint v)`
- L209 — `string ExpandDumpDir()`
- L212 — `FileInfo[] DiscoverDumps()` — newest-first `*.dmp` in the crash dir
- L222 — `int RunList()`
- L242 — `int RunAnalyze(string? dumpArg, int depth, int stackBytes)` — resolves dump + loads `FunctionTable`
- L296 — `int AnalyzeOne(FileInfo dump, FunctionTable? table, int depth, int stackBytes)` — main 7-step analysis (exception, ClrMD open, context, registers, EBP walk, ESP scan, hex dump)
  note: prefers the exception stream's `SavedContext` (fault-time registers) over ClrMD's live `GetThreadContext`, which is post-unwind and useless for stack walking
- L462 — `void ResolveAndPrint(string reg, uint value, FunctionTable table)`
- L472 — `string Resolve(FunctionTable? table, uint addr)`
- L475 — `string ResolveSuffix(FunctionTable? table, uint addr)`
- L482 — `bool TryReadU32(IDataReader reader, uint addr, out uint value)`
- L491 — `int ReadAvailable(IDataReader reader, uint addr, byte[] buf)` — chunked read tolerant of unmapped pages near stack top
- L508 — `void DumpHex(byte[] buf, int count, uint baseAddr)`
- L536 — `readonly record struct ExceptionRecord(uint ThreadId, uint Code, uint ExceptionEip, uint NumParams, ulong Param0, ulong FaultingAddress, byte[]? SavedContext)`
- L545 — `const uint StreamTypeException = 6`
- L547 — `ExceptionRecord? ReadExceptionStream(string path)` — hand-parses the MINIDUMP_EXCEPTION_STREAM directly from the file
  note: ClrMD has no API for this stream; offsets are hardcoded per the documented minidump format
- L626 — `string ExceptionCodeName(uint code)`
- L637 — `string AccessViolationKind(ulong rwFlag)`
- L647 — `readonly struct Ctx32` — x86 CONTEXT (716 bytes), fixed-offset field extraction (`Size`, `Edi..Esp`, `From(byte[])`)
