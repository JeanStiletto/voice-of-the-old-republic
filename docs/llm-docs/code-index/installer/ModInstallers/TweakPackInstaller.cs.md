# TweakPackInstaller.cs (212 lines)

`IModInstaller` implementation for the Unofficial TSLRCM Tweak Pack (Pavijan357, KOTOR 2) — reverts several TSLRCM restorations believed to be intentional Obsidian cuts. Pipeline: DeadlyStream guest-scrape download (`DeadlyStreamClient`, same cookie/csrfKey flow as TSLRCM) with pinned SHA-256 verification, RAR5 extraction via Windows' built-in `tar.exe` (libarchive reads RAR5 natively, no bundled extractor needed), then one standalone headless HoloPatcher run per component (7 components hardcoded in `Components`, mirroring the archive's `namespaces.ini` minus two visual/rebalance Extras that are out of scope). Each component is staged as its own `tslpatchdata` dir with `changes.ini` (Part 2's `changes2.ini` renamed on copy); the archive's multi-component `namespaces.ini` mechanism is bypassed entirely. Requires TSLRCM 1.8.3+ — gated by the caller on TSLRCM presence.

## Declarations (in source order)

- L29 — `public sealed class TweakPackInstaller : IModInstaller`
- L31 — `public string Id => "tweakpack"`
- L34 — `public bool IsSelected(ModSelection selection) => selection?.TweakPack == true`
- L38 — `private const string ArchiveTslpatchdataPath = "Individual component installer/tslpatchdata"`
- L43 — `private static readonly (string DataPath, string IniName, string Name)[] Components` — 7 hardcoded components (Kaevee Removal Part 1/2, Saedhe, Ravager, Atton, Kreia-Atris, Trayus Mandalore)
- L54 — `public async Task<ModInstallResult> InstallAsync(ModInstallContext ctx)`
  note: progress 0..15 download, 18 extract start, 20..100 spread across components
- L168 — `private static async Task<(bool Success, string Error)> ExtractRarAsync(string rarPath, string destDir)` — shells out to `%SystemRoot%\System32\tar.exe -xf ... -C ...`
- L202 — `private static void CopyDirectory(string sourceDir, string destDir)` — simple recursive dir copy for component staging
