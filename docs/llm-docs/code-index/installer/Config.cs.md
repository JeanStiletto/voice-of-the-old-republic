# Config.cs (215 lines)

Static class of build-time constants: GitHub repo/asset names for the accessibility .kpatch and Widescreen .kpatch, registry Publisher/DisplayName/uninstaller filename, and pinned source/version info for every bundled third-party mod (K1CP, K2 4GB/borderless static patches, Unofficial TSLRCM Tweak Pack, K2CP, TSLRCM, HoloPatcher). Pins are commit SHAs or tags plus SHA-256 hashes so a scraped or repo-sourced download is verified before use — an upstream change fails closed rather than silently installing an unverified file. Consumed by GitHubClient, DeadlyStreamClient, InstallationManager, and the ModInstallers pipeline (not in this batch).

## Declarations (in source order)

- L7 — `public static class Config`
- L13 — `ModRepositoryUrl` — accessibility mod's GitHub repo
- L19 — `ModSiteUrl` — GitHub Pages README site
- L24 — `KPatchAssetName` = "Accessibility.kpatch"
- L30 — `PatchId` = "accessibility"
- L37 — `WidescreenKPatchAssetName`, L43 `WidescreenPatchId` — Lane's bundled Widescreen patch
- L48-53 — `Publisher`, `DisplayName` — Add/Remove Programs identity
- L60 — `UninstallerExeName` — persistent uninstaller copied into game folder
- L74-77 — K1CP pin: `K1cpRepoOwner/RepoName/PinnedRef/DisplayVersion` (commit SHA, not master)
- L89-92 — KOTOR 2 static patches: `K2FourGbKPatchAssetName/PatchId`, `K2BorderlessKPatchAssetName/PatchId`
- L107 — `Kotor2WorkshopAppId` = "208580"
- L109-123 — Tweak Pack: download page URL, archive filename/version/SHA-256/size
- L133-136 — K2CP pin (commit SHA, no releases published upstream)
- L145-171 — TSLRCM: DeadlyStream download page, installer filename, SHA-256, size
  note: verification fails closed — an upstream update requires bumping the hash constant
- L179-212 — HoloPatcher: exe name, repo (NickHugi/PyKotor fork, not canonical), pinned tag, display version, asset zip name, path inside zip
