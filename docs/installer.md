# Installer

End-user installer for the accessibility mod. Goal: one download, one run, working game — without expecting the user to navigate DeadlyStream, install Python, pick mods, or know what TSLPatcher is. Sighted players abandon multi-step modding workflows within minutes; blind players using a screen reader have even less tolerance for friction.

This document is a scratch reference until the installer work starts. Sections will be added as design progresses.

## Mods to bundle

What the installer ships alongside our own patch. Scope intentionally narrow.

### Filters

The bundle excludes any mod that:

- Modifies visuals, textures, models, or camera (no value for blind users)
- Is locked to English text/VO and has no translation path (we target at least English + German + French; ideally also Italian/Spanish/Russian)
- Adds narrative content, new questlines, or restored cut content with new VO (out of scope; spoiler risk; usually English-only)
- Rebalances combat or stats in ways that diverge meaningfully from vanilla (a strict-vanilla baseline is a feature for accessibility)

These filters collapse the ~50-mod kotor.neocities.org Spoiler-Free build down to a handful of true survivors.

### Survivor list

**KOTOR 1 Community Patch (K1CP)** — the foundational bugfix compilation. Fixes hundreds of softlocks, broken triggers, missing scripts, quest-blocking issues. Without it, a blind playthrough is at real risk of hitting an unrecoverable state (Tatooine quest break, Manaan trial logic, etc.).

- Source: GitHub master at `https://github.com/KOTORCommunityPatches/K1_Community_Patch` — confirmed current with the latest DeadlyStream upload (K1CP v1.10.1, Feb 2026). DeadlyStream (`https://deadlystream.com/files/file/1258-kotor-1-community-patch/`) is the same content but cookie-gated. We pull the master tarball; no DeadlyStream auth needed.
- Installer: HoloPatcher (K1CP migrated from TSLPatcher in v1.10.0; the bundled `INSTALL.exe` *is* HoloPatcher with `tslpatchdata/` next to it). HoloPatcher has a headless CLI: `holopatcher <game-path> <tslpatchdata-path>`.
- Language story: base archive edits `dialog.tlk` in English. The repo's `tslpatchdata/translation_{german,french,russian}/` subfolders carry locale-specific `append.tlk` + `info.rtf` (translations by Ian Starrider, Harlockin, olegkuz1997, JayDominus). Our installer detects the locale via `dialog.tlk` header (offset 8, language ID 0=EN/1=FR/2=DE/3=IT/4=ES) and copies the matching `translation_*/append.tlk` over `tslpatchdata/append.tlk` before invoking HoloPatcher.
- Italian / Spanish: no official translation patch. For those locales we install K1CP in English — the bugfix text appears in English even though the rest of the game stays localised. Tracked as a known limitation in `docs/known-issues.md` and surfaced to the user on the modding-info screen footnote.

**Swoop Bike Upgrades** — restores two cut upgrade items (acceleration, obstacle damage reduction) that were referenced by the original swoop racing code but never shipped. Pure 2DA / UTI changes, no dialog, language-agnostic. Low-risk inclusion.

**Thematic KOTOR Companions** *(optional / toggle)* — rebalances companion attributes, skills, feats, powers so each companion has level-appropriate bonuses matching their backstory. Pure mechanics, language-agnostic. Borderline because it diverges from strict vanilla; gate behind a "strict vanilla" vs "vanilla+" toggle in the installer.

**Widescreen / FOV patch** — engine-level fix for KOTOR's hardcoded 4:3 assumptions. Not strictly accessibility-relevant but standard QoL; no language ties. Acceptable to bundle if it's the GitHub-hosted variant (clean license, single binary).

**Juhani Dialogue Restoration (JDR)** — verified language-agnostic. Ships only 5 NCS gate scripts (`k_hjuh_w10/p05/p19/p20/p21`), no `dialog.tlk` edits. The cut conversations live in vanilla `k_hjuh_dialog.dlg` (from `data/templates.bif`) with strrefs already pointing into the per-locale `dialog.tlk`. Confirmed against the German install: all 5 entry-point strrefs (4448, 4164, 4655, 4451, 4463, 4449) have German text and all 5 cut-line VO files exist in German `streamwaves/m35aa/juha11/`.

**Party Conversations on Ebon Hawk** — verified language-agnostic. Touches `k_pebn_pophawk.ncs` and existing `banter.dlg` triggers; relocates vanilla banter to the ship instead of adding new lines. Confirmed against the German install: all 113 unique strrefs referenced by `banter.dlg` resolve to German text (0 missing); 83 of 91 NPC-VO files present in German `streamwaves/globe/bant00/`; the 8 missing files are Zaalbar lines that fall back to the generic Wookie growl asset `n_gwwook_comm1` (present), which is vanilla engine behaviour. PC choice lines (22) have no VO by design, also vanilla. No mod-induced gaps.

That's roughly it. ~6 mods.

### Dropped from the spoiler-free build

For reference, the major Spoiler-Free entries our filters reject:

- All Ultimate HD environment packs (Korriban, Tatooine, Kashyyyk, …) — visual
- HD Darth Malak, Ultimate Character Overhaul — visual
- Camera Replacement, Ebon Hawk camera fix — visual / camera
- KOTOR Dialogue Fixes — English text rewrites, no translations
- Crashed Republic Cruiser on a Nameless World — full new questline, English VO, spoiler territory anyway

### Restoration mods — more nuanced than "all English-locked"

Initial assumption was that every restoration mod ships English-only. After checking actual mod pages (Dark Sacrifice, Party Conversations on Ebon Hawk, Juhani Dialogue Restoration) and reasoning from what the mods technically do, the picture is more nuanced. **None of these mod pages document language support** — this is reasoned from mechanics, not author statements, so verify before shipping.

Three buckets:

**Script-only restorations** — only change which existing dialog nodes fire. Reuse vanilla strrefs and vanilla VO. Work fully on localised installs because BioWare shipped translated `dialog.tlk` files containing the cut strrefs.

- Party Conversations on Ebon Hawk — relocates existing party banter to the ship. Touches `k_pebn_pophawk.ncs` + `banter.dlg`. No new strings. Should play in German with German text + German VO.
- Juhani Dialogue Restoration (JDR) — ships only NCS scripts, no `dialog.tlk` edits. Restores cut Juhani conversations. Same story: should work on German, modulo whether German VO was recorded for the cut lines (BioWare usually recorded localised VO for content cut late in QA).

These are candidates for bundling **if** we verify the assumptions below.

**Mixed restoration + new author content** — reuse some vanilla strrefs, append new ones via TSLPatcher StrRef append. The vanilla part plays localised; the new lines appear in English text + English VO mid-conversation. Functionally works, aesthetically inconsistent.

- Dark Sacrifice (the "important romance fix") — author confirms "a mix of new and unused content". On a German install: mostly German, with English bleed-through on the new author-written lines and English-only VO for those lines.

Borderline. Probably skip for the bundled installer because the inconsistency is more jarring with a screen reader (TTS switching from German to English voice mid-conversation) than for sighted players reading subtitles.

**Pure new content / rewrites** — every line is author-written. English on every locale. Out unconditionally. KOTOR Dialogue Fixes, Crashed Republic Cruiser, most quest-addition mods fall here.

### Verification needed before bundling restorations

Before promising any restoration mod works on German/French installs, check:

- Does the localised `dialog.tlk` actually contain the cut strrefs the mod relies on? Extract with `xoreos-tools tlk2xml` and look up the cut strrefs.
- Does localised VO exist in `streamwaves/` for the cut lines? List the relevant filenames; for cut content, this is hit-or-miss.

Worst case for script-only restorations is subtitled silence on lines where localised VO wasn't recorded — degraded but not broken.

### Verification method (reproducible)

Done once for German on the user's Steam install (locale ID 2). Same recipe applies to French / Italian / Spanish installs.

1. Confirm install locale: `tlk2xml.exe <install>/dialog.tlk` and read `<tlk language="N">` (0=EN, 1=FR, 2=DE, 3=IT, 4=ES).
2. Find the `.dlg` file the mod gates: `unkeybif l chitin.key | grep <mod-script-name>` to find the BIF; extract the dlg; `gff2xml.exe <name>.dlg <name>.xml`.
3. Find dialog nodes referenced by the mod's scripts: `grep 'Active">k_modscript' <name>.xml` → note the `Index` values, then look up each `<struct id="N">` to pull `strref` and `VO_ResRef`.
4. Verify strrefs in localised TLK: `grep '<string id="N"' dialog.tlk.xml` — every referenced strref should resolve.
5. Verify VO files in localised streamwaves: `find streamwaves -iname "<voresref>*"`. For PC choice lines (`_`-prefixed VO_ResRef) absence is expected. Audio fallbacks via the entry's `Sound` field (e.g. Wookie growls via `n_gwwook_*`) are vanilla and acceptable.

Verification artefacts for JDR + Party Conversations against the German Steam install live under `build/installer-verify/` (gitignored). Re-run for new locales when needed.

### K1R alternative

KOTOR 1 Restoration (K1R) is the competing restoration project. It's **incompatible with K1CP** — they patch overlapping things and conflict. Community consensus (and the neocities build) defaults to K1CP. We follow that. Don't try to bundle both.

## Installation mechanics

The first cut of the end-user installer lives in `installer/KotorAccessibilityInstaller/`.
It is modelled on the **Accessible Arena** installer
(`C:\Users\fabia\Dev\arena\installer\AccessibleArenaInstaller\IMPLEMENTATION.md` —
recommended reading before working on this one) with three KOTOR-specific deltas:

- **Patcher** — drops `KotorPatcher.dll` + `sqlite3.dll` + `addresses.db` and a
  generated `patch_config.toml` via `KPatchCore.PatchApplicator` instead of
  installing MelonLoader. The installer takes a `ProjectReference` on
  `third_party/Kotor-Patch-Manager/src/KPatchCore/KPatchCore.csproj`, so the
  same logic that powers `kdev apply` runs at end-user install time.
- **Speech** — bundles `prism.dll` (from `third_party/prism-dist/x86/`) into
  `<game>/patches/` instead of `Tolk.dll` + `nvdaControllerClient*.dll`.
  Prism statically links its NVDA, JAWS, SAPI, and Speech-Dispatcher bridges, so
  no separate NVDA controller DLL ships with the installer. The install path
  also actively removes stale `Tolk.dll` / `nvdaControllerClient32.dll` files
  left behind by older installs.
- **Modding-info screen** — between the welcome wizard and the main install
  form, an additional `ModdingInfoForm` surfaces the *Mods to bundle* section
  of this document (what gets installed, what we filter out, optional add-ons,
  the IT/ES localisation footnote) in a screen-reader-friendly linear layout
  using a focusable multiline read-only `TextBox`.

### Layout dropped into the game folder

After a successful install of a Steam KOTOR copy, the install root contains:

- `swkotor.exe` (patched with static hooks, `.backup.<ts>` saved by KPatchCore)
- `KotorPatcher.dll`, `sqlite3.dll`, `addresses.db`, `patch_config.toml`
- `patches/accessibility.dll` (extracted from the downloaded `.kpatch`)
- `patches/prism.dll` (bundled with the installer)
- `KotorAccessibility_Uninstaller.exe` (persistent copy of the installer EXE,
  so Add/Remove Programs keeps working after the original download is deleted)

### Project layout

- `installer/KotorAccessibilityInstaller/` — .NET 8 WinForms project, builds
  one self-contained single-file EXE (`dotnet publish -c Release -r win-x64
  --self-contained true -p:PublishSingleFile=true`).
- `installer/release.ps1` — local release pipeline; calls `kdev build` to
  produce `Accessibility.kpatch`, refreshes the installer's bundled binaries
  from `third_party/`, publishes the EXE, tags, and uploads both artifacts to
  the GitHub release via `gh release create`.

### Building blocks available

- **Our own patch** — ships from our GitHub releases; single DLL + loader, no TSLPatcher needed
- **Patch Manager (KPatchManager)** — our framework; GitHub-hosted, vendored in `third_party/`
- **K1CP** — needs TSLPatcher-style install; HoloPatcher / PyKotor is the modern headless driver
- **Swoop Upgrades / Thematic Companions / Widescreen** — mix of Override drop-ins and TSLPatcher; needs per-mod check

### Existing user-facing installers (for reference, not necessarily what we ship)

- **KOTORModSync** — multi-mod orchestrator, single self-contained .exe, HoloPatcher + PyKotor bundled internally. Reads TOML config encoding mod order. Does **not** auto-download; user pre-downloads ZIPs into a folder. The kotor.neocities.org builds ship as KOTORModSync configs. Could be the foundation of our installer (we ship a TOML preset) or a model for what to build.
- **HoloPatcher** — modern cross-platform reimplementation of TSLPatcher. Drop-in compatible with `tslpatchdata` payloads. Drivable headlessly.
- **TSLPatcher** — legacy Windows-only original. We probably never invoke it directly.

### Open questions

- Auto-download vs bundled redistribution: **resolved for K1CP** — pull from GitHub master at install time, same flow as our own `.kpatch`. No bundling, no DeadlyStream auth. Other survivors still TBD per-mod: Swoop Bike Upgrades, JDR, Party Conversations on Ebon Hawk, Thematic Companions are DeadlyStream-only as of this writing; widescreen (UniWS) is hosted on WSGF and KOTOR High Resolution Menus is DeadlyStream-only. Permission requests where needed.
- Localisation detection: `dialog.tlk` header at offset 8 carries a uint32 language ID (0=EN, 1=FR, 2=DE, 3=IT, 4=ES). The installer already has `LanguageDetector` for its own UI; mod-side detection should reuse the same lookup.
- Strict-vanilla toggle: minimum (K1CP only) vs vanilla+ (K1CP + Swoop + Thematic). Surface in installer UI in screen-reader-friendly form.
- Update story: K1CP versions itself frequently. Pin a known-good commit SHA (not `master`) per installer release so a mid-release K1CP regression doesn't break new installs.
- Uninstall: TSLPatcher mods write a backup folder. Our installer needs to expose a clean uninstall path that restores those backups in reverse order.
