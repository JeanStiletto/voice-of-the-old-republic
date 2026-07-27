# MinidumpStripper.cs (285 lines)

Byte-level Windows minidump shrinker used by LogCollector. Drops the captured memory of modules never inspected during KOTOR crash triage (stock Windows/GPU-driver DLLs) while keeping swkotor.exe + the mod's own DLLs (code+data), every thread stack, all crash-referenced heap, thread contexts, exception record, and the full module list. Shrinks a real ~151 MB WER dump to ~6 MB. Relies on a specific layout verified against WER's output for the project's `CustomDumpFlags` set: memory blobs form one contiguous tail; everything else (header, stream directory, module names, contexts) sits in the front below the first blob. It copies the front verbatim, drops unwanted blobs from the tail, relocates the (now-shorter) memory-descriptor list to the new tail, and patches just the MemoryList directory entry plus each thread's stack RVA. Only supports the 32-bit `MemoryListStream` layout (no `MiniDumpWithFullMemory`/Memory64List) — any structural surprise throws `NotSupportedException` so the caller (LogCollector) falls back to bundling the untouched dump.

## Declarations (in source order)

- L34 — `public static class MinidumpStripper`
- L36 — `class Stats { OriginalBytes, StrippedBytes, KeptRanges, DroppedRanges, KeptMemoryBytes, DroppedMemoryBytes }`
- L47 — `KeepModules` — swkotor.exe, kotorpatcher.dll, dinput8.dll, prism.dll, sqlite3.dll
- L61 — `readonly struct Module { Base, End, Name }`
- L70 — `static Stats StripFile(string inputPath, string outputPath)` — read, strip, write
- L79 — `static byte[] Strip(byte[] input, Stats stats)` — main algorithm: parse stream directory, module list, memory descriptors; locate blob-region floor; keep/drop per descriptor by module membership; reassemble front+kept-blobs+relocated-descriptor-list
  note: throws NotSupportedException on Memory64List, missing MemoryList stream, or bad signature
- L230 — `static bool ShouldKeep(ulong start, List<Module> modules)` — keep if not inside any known module, or inside a `KeepModules` entry
- L245 — `static List<Module> ReadModules(byte[] input, int modRva)`
- L263 — `static string ReadMinidumpString(byte[] input, int rva)` — UTF-16 minidump string format
- L272 — `static string FileName(string path)`
- L279-282 — `U32/U64/PutU32/PutU64` — little-endian byte helpers
