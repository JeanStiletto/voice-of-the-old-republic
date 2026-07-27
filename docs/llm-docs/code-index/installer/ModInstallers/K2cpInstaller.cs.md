# K2cpInstaller.cs (121 lines)

`IModInstaller` implementation for KOTOR 2 Community Patch (K2CP). Same GitHub tree-API + HoloPatcher pipeline as `K1cpInstaller`, but simpler: no `.gitattributes export-ignore` (verified 2026-07-27, kept anyway for one shared download path) and no translation subfolders — K2CP is English-only upstream, so non-English installs just get bugfix strings in English (logged, not surfaced as an error).

Not yet wired into any pipeline the installer runs end-to-end: gated on two prerequisites per docs/installer.md ("KOTOR 2 mod bundle") — TSLRCM-first install ordering (no auto-download path yet) and unverified `.lyt`/`.vis` CRLF status (K1CP needed the CRLF-normalization fix; whether K2CP needs it too is unconfirmed).

## Declarations (in source order)

- L39 — `public sealed class K2cpInstaller : IModInstaller`
- L41 — `public string Id => "k2cp"`
- L44 — `public bool IsSelected(ModSelection selection) => selection?.K2cp == true`
- L46 — `public async Task<ModInstallResult> InstallAsync(ModInstallContext ctx)`
  note: progress 0..55 download, 55..60 staging, 60..100 HoloPatcher; logs (not overlays) non-English locale note
