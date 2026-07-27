# K1cpInstaller.cs (294 lines)

`IModInstaller` implementation for KOTOR 1 Community Patch (K1CP). Fetches `tslpatchdata/` from the pinned GitHub commit via `GitHubTslpatchdataFetcher` (needed because K1CP's `.gitattributes` marks that dir `export-ignore`, and K1CP has no GitHub releases), applies a per-locale `append.tlk` overlay (German/French only — Russian is detected but deliberately left unwired pending a translation-variant decision; Italian/Spanish/unknown fall back to English), runs `HoloPatcherRunner.RunAsync`, then normalizes LF-only `.lyt`/`.vis` files under Override to CRLF.

Gotcha: the CRLF normalization works around a real engine bug — `CLYT`/`CRoom-info`'s line parser advances by `strlen(line) + 2`, hardcoding CRLF; LF-only K1CP files cause an over-read that can hit a decommitted page and crash (see docs/upstream-prs.md PR-6).

## Declarations (in source order)

- L32 — `public sealed class K1cpInstaller : IModInstaller`
- L34 — `public string Id => "k1cp"`
- L37 — `public bool IsSelected(ModSelection selection) => selection?.K1cp == true`
- L39 — `public async Task<ModInstallResult> InstallAsync(ModInstallContext ctx)`
  note: progress 0..55 covers download, 55..60 staging/overlay, 60..100 HoloPatcher run
- L126 — `private static void ApplyLocaleOverlay(string tslpatchdataDir, GameLocale locale)`
  note: Russian unwired — K1CP ships two competing Russian overlays and the Allard translation is untested against K1CP; both need a human call
- L202 — `private static readonly string[] LineEndingNormalizeExtensions = { ".lyt", ".vis" }`
  note: deliberately excludes `.txi`/`.lod` — different parsers, no +2 hardcode
- L216 — `private static int NormalizeOverrideLineEndings(string gameDir)` — iterates Override dir for the two extensions, returns count rewritten
- L251 — `private static bool NormalizeFileIfLfOnly(string path)`
  note: idempotent — skips files already containing any 0x0D byte, or with no 0x0A at all; appends trailing CRLF if file doesn't end in LF
