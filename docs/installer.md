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

## KOTOR 2 mod bundle (research, July 2026)

Status: research only — no installer code yet. Target install is the **Steam
Aspyr build** of `swkotor2.exe` (the user's copy). Same filters as K1: no
visual/texture mods, no English-locked mods where avoidable, strict-vanilla
baseline with a vanilla+ toggle for rebalances.

### Framework support already in place

The vendored KPatchManager supports KOTOR 2 out of the box — no upstream work
needed to *detect* the game:

- `KPatchCore.GameDetector` recognises four K2 exe hashes: Steam-Aspyr
  ("2 1.0.2 (Aspyr+Steam)"), GOG-Aspyr, and two legacy 1.0/1.0b builds.
- Upstream ships K2 patches with `kotor2-steam-aspyr.hooks.toml` variants:
  `4GBPatch`, `K2AspyrFogReflectionFix`, `K2AspyrColorCorrection`,
  `K2AspyrMusicFix`, `BorderlessFullscreen` (see
  `third_party/Kotor-Patch-Manager/Patches/`).
- Consequence: we do **not** need the community "3C-FD Patcher" or the ntcore
  4GB patch. Feature comparison against 3C-FD's README (checked 2026-07-27):
  3C-FD ships exactly six patches — Fog Fix, Reflections Fix, 4GB, Subtle
  Color Shift, Music Volume During Dialogue Fix, Borderless Window Mode
  (experimental) — and Lane's kpatch set maps onto them one-to-one
  (K2AspyrFogReflectionFix = fog + reflections, 4GBPatch,
  K2AspyrColorCorrection = color shift, K2AspyrMusicFix, BorderlessFullscreen).
  Nothing is lost by taking the kpatch route, and we gain: KPatchCore-managed
  install/uninstall, no second exe-hex-patching GUI tool (3C-FD is a GUI that
  edits swkotor2.exe in place — accessibility unknown, and it would fight the
  SHA-256 hash management). The neocities "Stutter Fix and Force Cage Update"
  is a separate Override-file mod, not part of 3C-FD, and can be added as a
  drop-in later. The standalone ntcore 4GB Patcher is only for non-Aspyr
  legacy installs.
- Our accessibility patch itself is a separate, much larger story: Lane's RE
  database and all our hook addresses are K1-only. K2 hooks mean a new RE
  effort against the Aspyr exe. Out of scope for the mod-bundle work.

### Essential mods (the K2 equivalent of K1CP)

**TSLRCM 1.8.6** (TSL Restored Content Mod) — the mandatory foundation;
restores cut content and fixes hundreds of bugs. Community consensus: no K2
mod setup without it.

- Source: DeadlyStream file 578 (exe installer, `tslrcm2022.exe`). Also ModDB
  (Cloudflare-gated) and Steam Workshop — the neocities build explicitly
  prohibits the Workshop version (breaks compatibility with directory-installed
  mods; users must *unsubscribe* from all Workshop items before modding).
- Guest download verified (2026-07-27): DeadlyStream serves the file to
  guests without an account. There is no static direct URL — the download
  needs a session cookie plus the per-session `csrfKey` scraped from the file
  page (`?do=download&csrfKey=<key>` then returns 200 with
  `Content-Disposition: tslrcm2022.exe`). A human just opens the page and
  presses Download; the installer implements the two-step scrape in
  `DeadlyStreamClient` (see the preparation section below). Scraping is
  fragile and gray-zone regarding site ToS, so the permission-based mirror
  remains the preferred long-term path; every scrape failure falls back to
  the manual page link.
- Installer tech (from the downloaded exe): **Inno Setup 5.5.7**, 137,947,655
  bytes, SHA-256 pinned in `Config.TslrcmInstallerSha256`. No `.isl` language
  files inside — the wizard UI is English-only. Standard Inno silent flags
  (`/VERYSILENT /DIR=...`) should work for a future unattended install but
  are untested; the current flow runs the wizard interactively (Inno wizards
  are standard-control, screen-reader friendly). Whether the payload contains
  localized game content is still unverified — needs an innoextract or a test
  install on a German copy.
- Steam/Aspyr: explicitly supported by the 1.8.6 installer.
- Languages: credits list French, Italian, Spanish, German, Russian
  translation teams. **Verify:** whether the single 1.8.6 exe carries all
  locales (and how it picks one) or whether localized builds are separate
  downloads.
- **Auto-download problem: no GitHub presence, no API-friendly host.** This is
  the one essential mod without a clean scripted download path. Options, in
  preference order: (1) ask the TSLRCM team (zbyl2 et al.) for permission to
  mirror the installer as an asset on our GitHub release — same
  permission-request route already planned for DeadlyStream-only K1 mods;
  (2) scripted DeadlyStream download (cookie-gated, fragile, likely against
  site ToS); (3) steamcmd Workshop fetch (prohibited layout, wrong content
  variant — rejected).
- **Verify:** whether the exe installer supports silent flags (likely
  Inno/NSIS `/VERYSILENT /DIR=...`) so our installer can drive it headlessly.

**KOTOR 2 Community Patch (K2CP) v1.6.2** — the K1CP sibling
(same GitHub org), curated community bugfixes on top of TSLRCM.

- Source: GitHub `KOTORCommunityPatches/TSL_Community_Patch`. `tslpatchdata/`
  is in the repo tree and — unlike K1CP — the `.gitattributes` has **no
  export-ignore**, so the plain master tarball is complete. No GitHub releases
  or tags exist; pinned commit `4850a44` ("Admin update", 2025-09-26, the
  v1.6.2 release point) in `Config.K2cpPinnedRef`.
- Installer: TSLPatcher-style (`INSTALL.exe` bundled); K2CP has *not*
  migrated to HoloPatcher yet, but HoloPatcher drives any `tslpatchdata/`
  payload headlessly, same as our K1 flow.
- Languages: **no translation subfolders** (K1CP has DE/FR/RU; K2CP has
  none). Neocities marks it English-only. On a German install the appended
  fix strings will be English — same known-limitation footnote as K1CP on
  IT/ES.
- Order: K2CP's own README says "before anything else, except TSLRCM"; the
  neocities build sequences it after TSLRCM and the Tweak Pack. Follow the
  neocities order.

**Unofficial TSLRCM Tweak Pack v1.3** (Pavijan357) — reverts/adjusts TSLRCM
restorations that were arguably cut intentionally by Obsidian.

- Source: DeadlyStream file 296, hosted on-site (no GitHub). The neocities
  build links a Fair Strides-updated copy on MEGA — **verify** which copy is
  current before choosing a download path.
- Auto-download verified (2026-07-27): the same DeadlyStream guest scrape
  works — `?do=download&csrfKey=<key>` serves a 1.37 MB **RAR5** archive
  directly (SHA-256
  `e98c94d53dfccaddf6753aa58662e1afd1d6ebb0241f66c8000ba0ff3a2f13b5`).
- Archive contents (listed with Windows' built-in `tar.exe` / libarchive,
  which reads RAR5 — no extra extraction tool needed): `Complete
  Installer/URCMTP.exe` plus `Individual component
  installer/tslpatchdata/` with per-component subfolders, each carrying its
  own `changes.ini` (1-Kaevee_Removal incl. Part_2, 2-Saedhe, 3-Ravager,
  4-Atton, 5-Kreia_Atris_DLG, 6-Mandalore_Trayus, Extras/1-Trayus_Sith_Lords,
  Extras/2-Gand_Warrior) and a root `namespaces.ini`.
- Install route: we never run the bundled TSLPatcher exes — extract the RAR,
  point HoloPatcher at each selected component's `tslpatchdata`, same
  headless drive as K1CP/K2CP. Open design points: which components to
  select (follow the neocities-recommended subset vs. user choice — these
  are taste tweaks, not pure fixes), whether HoloPatcher's CLI handles the
  `namespaces.ini` multi-component layout directly or we invoke
  per-component, and per-component language vetting (the components ship
  `.dlg` edits; strref-based, but verify against a localized TSLRCM install).
- Requires TSLRCM 1.8.3+.

### Second-tier candidates from the neocities spoiler-free build

Marked "Non-English compatible" + non-visual on the build page; each needs the
same per-mod vetting we did for K1 restorations before bundling:

- Saving Throw Fixes (Rovan, DeadlyStream) — prestige-class save fix
- Classic Class Attack Bonus (DeadlyStream) — restores class attack tables
- Prologue Item Recovery (Leilukin, DeadlyStream)
- Peragus Medical Bay Enhancement (WildKarrde, DeadlyStream)
- Harbinger Arrival Performance Enhancement (PapaZinos, DeadlyStream) —
  stabilises the opening sequence; performance, not visuals
- Droid Model Animation Fix (DeadlyStream) — droids get dodge animations
- Kreia's Longsword (bead-v, MEGA)
- KEBCD (Hassat Hunter, MEGA) — restores item spawning
- Thorium Charge Mod (DeadlyStream)
- Bao-Dur Can Wear Heavy Armor (Effix, DeadlyStream) — borderline rebalance

Vanilla+ toggle territory (rebalances, GitHub-hosted, so easy to pull):
Thematic KOTOR 2 Companions, TSL boss redesign (Snigaroo), TJM (Sniggles /
JCarter426). Rejected outright: everything visual (the large majority of the
118-mod list), English-only dialogue mods, M4-78 (explicitly incompatible
with the neocities build).

### K2 installer preparation (implemented 2026-07-27, not yet released)

Code landed in `installer/KotorAccessibilityInstaller/` ahead of the actual
KOTOR 2 flow:

- **Game-version screen** (`GameVersionSelectionForm`) — new step between the
  welcome dialog and the base-components screen. Two checkboxes: KOTOR 1
  (default on, fully supported) and KOTOR 2 (default off, "in preparation").
  Next requires at least one checked; zero-checked shows an explanatory
  MessageBox rather than a silently disabled button (screen-reader reasoning
  in the class comment).
- **KOTOR 2 preparation flow with unattended TSLRCM install** — checking
  KOTOR 2 shows a localized Yes/No offer: whether a KOTOR 2 Steam install was
  detected (App ID 208580 / `swkotor2.exe`, `Program.DetectKotor2GamePath`),
  the no-Workshop warning, the manual DeadlyStream link, and the question
  "download TSLRCM now (~138 MB)?". On Yes, `TslrcmInstallForm` scrapes the
  guest download via `DeadlyStreamClient` (cookie + csrfKey two-step),
  verifies the pinned SHA-256 (fail-closed: a changed upstream file routes to
  the manual link), then — when the KOTOR 2 folder was detected — runs
  TSLRCM's Inno Setup **silently** (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART
  /SP- /DIR=<path> /LOG=<temp>`); silent is the default because the TSLRCM
  wizard is English-only while our installer speaks the user's language. The
  silent run is verified by a before/after `dialog.tlk` fingerprint (TSLRCM
  always replaces it; exit 0 with unchanged tlk = failure). Fallbacks: no
  detected path → announce handoff, run the wizard visibly; silent failure →
  Yes/No offer of the visible wizard. Download progress announces via UIA
  notifications at 25% steps; the install phase heartbeats ~15 s and disables
  Cancel (aborting Inno mid-install would leave a half-written mod install).
  Untested end-to-end as of 2026-07-27: the scrape was proven live via curl,
  the C# path and the silent switches still need a real run on a KOTOR 2
  install. KOTOR-2-only selections exit after this flow.
- **KOTOR 2 engine patches (wired, analog to the K1 widescreen flow)** —
  checking KOTOR 2 with a detected install applies Lane's two static kpatches
  to `swkotor2.exe` before the TSLRCM offer: `4gb-patch` (LAA flag) and
  `borderless_fullscreen`, bundled as `Resources/4GBPatch.kpatch` +
  `Resources/BorderlessFullscreen.kpatch` (zipped verbatim from
  `third_party/Kotor-Patch-Manager/Patches/<name>/` — manifest + all hooks
  variants; KPatchCore selects hooks by exe SHA). Static-only install:
  `PatcherDllPath = null`, so no runtime DLL, loader, or address DB lands in
  the KOTOR 2 folder — just the patched exe plus an inert patch_config.toml.
  Idempotency via the same hash gate as K1 widescreen: an exe whose SHA-256
  is not declared by the manifests (already patched, 3C-FD'd, unknown build)
  is skipped with a reason, never force-patched. `swkotor2.ini` gets
  `AllowWindowedMode=1` + `FullScreen=0` (required by the borderless patch;
  windowed is the screen-reader baseline). Result is reported as one line
  inside the preparation dialog. No backup — Steam "Verify integrity"
  restores vanilla; uninstall does not touch KOTOR 2.
- **`K2cpInstaller`** — complete and buildable, mirrors `K1cpInstaller`
  (GitHub tree fetch at pinned SHA → headless HoloPatcher). Not in any active
  pipeline; `ModInstallerCoordinator.BuildKotor2Pipeline()` exists but is not
  invoked. Two activation gates documented in the class: TSLRCM-presence
  check (order: TSLRCM before K2CP) and a `.lyt`/`.vis` line-ending audit of
  K2CP's files (K1CP needed CRLF normalization; K2 engine uses the same
  parser family).
- **Shared helpers** extracted so K1CP/K2CP don't duplicate code:
  `GitHubTslpatchdataFetcher` (tree API + raw.githubusercontent.com download)
  and `HoloPatcherRunner` (headless HoloPatcher drive with throttled status
  forwarding + heartbeat). `K1cpInstaller` now calls these.
- Locale strings for all of the above added to all six languages.

### K2 open questions

- TSLRCM + Tweak Pack distribution: TSLRCM now auto-downloads via the
  DeadlyStream guest scrape (hash-pinned, manual-link fallback). Still open:
  permission request to mirror (removes the scrape fragility), and the same
  question for the Tweak Pack (also DeadlyStream-hosted; same scrape would
  work).
- TSLRCM language handling on a German Steam install — needs a hands-on test
  of the 1.8.6 installer.
- Steam Workshop pre-flight check: installer should detect a non-empty
  `steamapps/workshop/content/208580/` and warn the user to unsubscribe
  before installing anything.
- Aspyr quirks that survive even with fixes (per neocities): occasional
  savegame loss, dialogue-skip bug, controller camera offset after workbench
  use. Track in known-issues once the K2 effort starts in earnest.

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
