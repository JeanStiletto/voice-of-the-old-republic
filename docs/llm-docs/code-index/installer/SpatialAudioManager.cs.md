# SpatialAudioManager.cs (250 lines)

Static toggle for the bundled dsoal + OpenAL Soft spatial-audio layer (experimental, opt-in per memory `project_dsoal_spatial_audio_toggle`). "Enabled" state is detected purely by presence of `dsound.dll` in the game folder (dsoal hijacks the DirectSound import; the other bundled files — `dsoal-aldrv.dll`, `alsoft.ini`, two license texts — are dead unless that DLL loads). `Enable`/`Disable` extract or delete the fixed `DeployedFileNames` set from embedded assembly resources and flip `EAX=0/1` under `[Sound Options]` in swkotor.ini via `SetEaxValue`, a hand-rolled in-place INI editor that preserves all other keys/ordering/whitespace. Both operations are idempotent.

## Declarations (in source order)

- L20 — `public static class SpatialAudioManager`
- L23 — `public const string DsoundDllName = "dsound.dll"` — presence = authoritative "enabled" bit
- L24-27 — `AldrvDllName`, `AlsoftIniName`, `DsoalLicenseName`, `OpenAlSoftLicenseName` constants
- L37 — `private static readonly string[] DeployedFileNames` — exactly the 5 files `Disable()` deletes
- L46 — `public sealed class Result` — `Success`, `NowEnabled`, `Error`
- L57 — `public static bool IsEnabled(string gameDir)`
- L68 — `public static Result Enable(string gameDir)` — extracts 5 embedded resources, sets `EAX=1`
- L101 — `public static Result Disable(string gameDir)` — deletes deployed files (missing ones ignored), sets `EAX=0`
- L145 — `private static void SetEaxValue(string gameDir, bool enable)`
  note: appends `[Sound Options]` section if missing entirely; inserts key if section exists but key doesn't
- L227 — `private static void Extract(string resourceShortName, string targetPath)`
  note: matches embedded resource by suffix (`EndsWith`) since full manifest resource names are namespace-qualified
