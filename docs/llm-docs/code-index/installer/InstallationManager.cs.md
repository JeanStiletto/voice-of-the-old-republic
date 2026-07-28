# InstallationManager.cs (600 lines)

Drives the actual KOTOR-side installation. Extracts embedded-resource runtime files (KotorPatcher.dll, sqlite3.dll, kotor1_0_3.db address database, Prism speech runtime, dinput8.dll proxy loader, WAV Override assets) into a staging directory in the layout `KPatchCore.Applicators.PatchApplicator` expects, then calls into `KPatchCore` directly to apply the .kpatch(es) to swkotor.exe/swkotor2.exe. Widescreen and the KOTOR 2 static patches are gated by SHA-256 hash-vs-manifest `supported_versions` comparison (`IsPatchSupportedForExe`) rather than a named-version lookup, since an unrecognized build has no name — this is how a Russian-translation exe (its own relink) is silently skipped for widescreen instead of taking the whole install down. All embedded-resource writes go through `WriteFileResilient`, which retries on `IOException`/`UnauthorizedAccessException` with backoff (200/400/800/1600ms) to ride out a transient antivirus scan-lock; a persistent block on the dinput8.dll proxy specifically is turned into the actionable `LoaderBlockedException`. Talks to Config (asset/patch-id names), Program (Kotor2ExeName), KPatchCore.Applicators/Managers/Common, and PriorityGroup2da (not in this batch).

## Declarations (in source order)

- L16 — `public class InstallationManager`
- L24 — `PatcherRuntimeFiles` — KotorPatcher.dll, sqlite3.dll
- L25 — `AddressDbFiles` — kotor1_0_3.db
- L26-27 — `PrismDllName` = "prism.dll", `LoaderDllName` = "dinput8.dll"
- L33 — `OverrideAssets` — 5 WAV samples for swoop/turret cues; `OverrideAssetNames` public accessor for the uninstaller
- L36 — `InstallationManager(string gameDir)`
- L52 — `string StagePatcherRuntime(string kpatchSourcePath)` — builds `<staging>/bin`, `/AddressDatabases`, `/patches` (accessibility + bundled Widescreen kpatch)
  note: swaps CWD to stagingRoot at apply time because PatchApplicator's AddressDatabases lookup is relative and resolves empty inside a single-file app
- L96 — `PatchApplicator.InstallResult ApplyKPatch(string stagingRoot)` — installs accessibility + conditionally Widescreen patch IDs against swkotor.exe
- L162 — `static bool IsWidescreenSupported(...)`
- L171 — `static bool IsPatchSupportedForExe(PatchRepository repository, string patchId, string gameExe, out string reason)` — SHA-256-vs-manifest gate shared by widescreen and K2 static patches
- L221 — `PatchApplicator.InstallResult ApplyKotor2StaticPatches(out string skipReason)` — applies Lane's 4GB-LAA + borderless-fullscreen patches to swkotor2.exe; no patcher DLL/loader/address-db needed (pure static byte patches)
  note: returns null+skipReason both for "already patched" and genuinely unrecognized builds — re-running stays idempotent
- L302 — `void InstallLoader()` — drops dinput8.dll proxy; converts IO/UnauthorizedAccess failures into `LoaderBlockedException`
- L332 — `void InstallPrismRuntime()` — copies prism.dll into `<game>/patches/`; deletes stale Tolk.dll/nvdaControllerClient32.dll from earlier installer versions
- L369 — `void InstallOverrideAssets()` — per-asset try/catch isolation so one missing embedded resource doesn't drop every remaining asset (regression from v0.5.7)
- L410 — `void InstallPriorityGroup()` — appends the mod's sentinel-tagged row to Override/prioritygroups.2da; idempotent, reads existing Override file if present else bundled vanilla; never removed on uninstall
- L439 — `string CopyUninstaller()` — copies the running installer exe into the game folder for Add/Remove Programs
- L463 — `void CleanupStaging(string stagingRoot)`
- L479 — `static byte[] ReadEmbeddedResourceBytes(string shortName)`
- L494 — `static void ExtractEmbeddedResource(string shortName, string targetPath)`
- L508 — `WriteRetryBackoffMs` = 200/400/800/1600 ms
- L518 — `static void WriteFileResilient(string targetPath, byte[] data, string shortName)` — retry-with-backoff writer
- L547 — `static void ClearReadOnly(string path)`
- L565 — `static string FindResourceName(Assembly assembly, string shortName)`
- L587 — `public class LoaderBlockedException : Exception` — carries GameDir + LoaderPath for MainForm's antivirus-exclusion guidance
