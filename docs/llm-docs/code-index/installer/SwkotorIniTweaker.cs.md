# SwkotorIniTweaker.cs (260 lines)

Static in-place editor for `swkotor.ini` / `swkotor2.ini`. Applies community-sourced stability tweaks (`[Graphics Options]`: V-Sync, Frame Buffer, Disable Vertex Buffer Objects, FullScreen) and, on full installs only, the mod's recommended movement keybind remap (`[Keymapping]`: strafe -> A/D, camera turn -> Y/C physical bottom-left cluster) via one shared engine `ApplySectionPairs` routine. Also handles KOTOR 2's windowed-mode requirement (`AllowWindowedMode=1`, `FullScreen=0`) needed by Lane's BorderlessFullscreen patch. All operations preserve section ordering, comments, and unrelated keys, and are idempotent (re-running doesn't duplicate keys).

Gotcha: `KeymapTweaks` values are the engine's numeric InputIndices encoding *physical* key position, not the logical key label — value 76 is the bottom-left letter key (Y on DE keyboards, Z on US), confirmed against `keymap.2da`.

## Declarations (in source order)

- L26 — `public static class SwkotorIniTweaker`
- L28-31 — file/section-name constants (`IniFileName`, `Kotor2IniFileName`, `GraphicsSectionHeader`, `KeymappingSectionHeader`)
- L34 — `private static readonly (string Key, string Value)[] Tweaks` — the 4 KOTOR 1 stability tweaks
- L54 — `private static readonly (string Key, string Value)[] KeymapTweaks` — Action281/284 A/B remap
  note: Action283 (minigame steering) intentionally left on A/D, unaffected
- L62 — `public sealed class Result` — Success, Error, Changed, AlreadyCorrect, Added, IniPath
- L76 — `private static readonly (string Key, string Value)[] Kotor2Tweaks` — AllowWindowedMode=1, FullScreen=0
- L88 — `public static Result ApplyAccessibilityDefaults(string gameDir)`
- L97 — `public static Result ApplyKeymapDefaults(string gameDir)`
  note: caller must skip this on the update path so returning players' custom bindings aren't overwritten
- L104 — `public static Result ApplyKotor2WindowedDefaults(string k2GameDir)`
- L113 — `private static Result ApplySectionPairs(string gameDir, string iniFileName, string sectionHeader, (string Key, string Value)[] pairs)`
  note: shared engine for all three public methods; appends missing section/keys, writes CRLF explicitly (ReadAllLines strips it)
- L238 — `private static int FindSectionStart(List<string> lines, string sectionHeader)`
- L248 — `private static int FindNextSectionStart(List<string> lines, int from)`
