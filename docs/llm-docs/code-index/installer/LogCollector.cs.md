# LogCollector.cs (417 lines)

Bundles the newest patch log, a WER crash minidump (stripped via MinidumpStripper to fit under Discord's size limit), the installer's own log, and a generated `system-info.txt` into a single archive in the user's Downloads folder — the one-file artefact beta testers attach to bug reports. Compresses with a bundled `7zr.exe` (LZMA2 level 5, ~44.6 MB vs 46 MB at level 9 but far faster — matters because the collect step runs synchronously with no progress window) and falls back to .NET's Deflate `ZipFile` if 7-zip fails for any reason (AV quarantine, etc.), so the feature never silently produces nothing. `RevealInExplorer` opens Explorer with the produced archive pre-selected. The nested `WerLocalDumps` class enables Windows Error Reporting LocalDumps for swkotor.exe with a `CustomDumpFlags` mask tuned to keep only what `kdev analyze-dump` uses (globals, heap referenced from stack/registers, thread/process data, unpacked .text) — typically 15-50 MB instead of 500+ MB for a full dump.

## Declarations (in source order)

- L17 — `public static class LogCollector`
- L19 — `class Result { Success, ArchivePath, LogCount, DumpCount, IncludedInstallerLog, Error }`
- L29 — `static Result Collect(string gamePath)` — stages files in a temp dir, compresses, cleans up staging regardless of outcome
- L138 — `static void RevealInExplorer(string archivePath)` — `explorer.exe /select,"<path>"`
- L160 — `static string CompressWith7z(string staging, string downloadsDir, string stamp)` — extracts embedded 7zr.exe next to (not inside) staging, runs `a -t7z -m0=LZMA2 -mx=5`; returns null on any failure so caller falls back
- L240 — `static string CompressWithZip(...)` — plain Deflate fallback
- L248 — `static void ExtractResource(string resourceShortName, string targetPath)`
- L269 — `static string FindNewestPatchLog(string gamePath)` — newest `<game>/logs/patch-*.log`
- L279 — `static string FindNewestCrashDump()` — newest `%LOCALAPPDATA%\CrashDumps\swkotor*.dmp`
- L291 — `static string GetDownloadsDir()` — SpecialFolder has no Downloads entry; derived from UserProfile
- L301 — `static void WriteSystemInfo(string path, string gamePath)` — OS/CLR/locale, recent dumps, recent logs, WER-enabled status
- L365 — `public static class WerLocalDumps`
- L367 — `KeyPath` — `HKLM\...\Windows Error Reporting\LocalDumps\swkotor.exe`
- L370 — `static bool IsEnabled()`
- L380 — `static bool Enable()` — sets DumpFolder, DumpCount=10, DumpType=0 (custom), CustomDumpFlags=0x2141
  note: requires elevation; installer already runs as admin
