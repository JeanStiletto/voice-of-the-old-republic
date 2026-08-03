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
- Languages — RESOLVED 2026-07-27 by extracting the exe with innoextract:
  the DeadlyStream `tslrcm2022.exe` is **English-only**. It contains 1134
  files including two `dialog.tlk` variants, both language ID 0 (English);
  no localized files, no language selection logic. Installing it on a
  German (or any non-English) KOTOR 2 replaces the localized `dialog.tlk`
  with English — all game text becomes English; German VO remains only for
  vanilla lines (mixed-language result). The readme's FR/IT/ES/DE/RU
  translation credits refer to the separately-distributed localized
  editions: per-language Steam Workshop items maintained by the TSLRCM team
  (the (German) item was updated 2022-09 and author-confirmed as 1.8.6) —
  but Workshop distribution conflicts with directory-installed mods (K2CP,
  Tweak Pack), which is exactly what the neocities build prohibits mixing.
- Localized-text solution (implemented in `WorkshopTlkHarvestForm`,
  community-endorsed "subscribe and copy the tlk out" route): no anonymous
  direct download exists (GetPublishedFileDetails returns an empty
  `file_url` — UGC-depot hosted, ~335 MB for the German item), but every
  affected user owns KOTOR 2 on Steam. After the English TSLRCM install on
  a non-English game (locale detected BEFORE TSLRCM overwrites
  dialog.tlk), the installer offers: open the language's Workshop page via
  `steam://url/CommunityFilePage/<id>` (user presses Subscribe — the one
  manual step, in Steam's own UI), poll
  `steamapps/workshop/content/208580/<id>/` for `dialog.tlk` until its size
  is stable, verify the language via `GameLocaleDetector` (header ID for
  DE/FR/IT/ES, CP1251 content probe for RU), back up the English tlk as
  `dialog.tlk.english.bak`, copy the localized one in, then remind the user
  to UNSUBSCRIBE. Runs before K2CP/Tweak Pack so their tlk appends land on
  the localized file. Item IDs (all official TSLRCM-team uploads): DE
  485551190, FR 485553656, IT 485556965, ES 485555217, RU 2143250983.
  KOTOR 2 has no localized VO anyway (text-only localization), so the tlk
  swap is the complete localization story. Re-run caveat: once English
  TSLRCM is installed, the original language is no longer detectable from
  the install — the harvest offer only appears in the same run that
  installed TSLRCM.
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

- Source: **DeadlyStream file 1280** (`K2CP_v1.6.2.zip`, 80,523,859 bytes),
  fetched with the same guest scrape as TSLRCM and the Tweak Pack. Pinned in
  `sources.json`.
- ~~Source: GitHub `KOTORCommunityPatches/TSL_Community_Patch`, whose
  `.gitattributes` has no export-ignore so the tarball is complete.~~
  **WRONG, corrected 2026-08-03.** The `.gitattributes` statement is true and
  the conclusion did not follow: **the payload is not in that repo at all.** At
  the pinned commit the entire repository is 13 files and `tslpatchdata/`
  contains exactly two — `changes.ini` and `info.rtf`. The 713 files K2CP
  installs (221 `.mdl`, 221 `.mdx`, 95 `.wok`, 85 `.tpc`, 45 `.uti`, 9 `.mod`,
  6 `.2da`, …) ship only in the DeadlyStream archive. Verified against the git
  tree API, not just the tarball.
- **How it failed, and why it went unnoticed:** a TSLPatcher `changes.ini` both
  edits files already in the game and installs new files out of
  `tslpatchdata`. Fetching from the repo gave HoloPatcher the edit
  instructions with no files to copy, so the edits applied, the installs
  silently did nothing, and the run exited successfully with one warning buried
  in the log. The install looked like it worked. `K2cpInstaller` now refuses to
  run a payload with fewer than 10 files rather than let that recur quietly.
- The repo commit `4850a44` ("Admin update", 2025-09-26) is still recorded in
  `sources.json` for provenance — it matches the archive's release date — but
  is not fetched.
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
- Install route (implemented in `TweakPackInstaller`): we never run the
  bundled TSLPatcher exes — extract the RAR, stage each component as a
  standalone `tslpatchdata`, drive HoloPatcher per component (bypasses
  `namespaces.ini`, so no CLI namespace support needed). Component set: the
  seven main entries; Extras excluded by our filters. Still open:
  per-component language vetting (the components ship `.dlg` edits;
  strref-based, but verify against a localized TSLRCM install).
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

### K2 install flow (mods 2026-07-27; full accessibility install 2026-08-03)

**As of 2026-08-03 the installer installs the accessibility mod into KOTOR 2
itself**, not just the community mods. What differs per game now lives in one
place, `GameTarget.cs`, rather than being hard-coded in each install step.

Per-game facts in `GameTarget`: executable and process name (they are not the
same question — `GetProcessesByName("swkotor")` does not match a running
`swkotor2`), ini filename, Steam app id, Add/Remove Programs key, intro-movie
list, ini defaults, bundled `.kpatch` set, and which vanilla
`prioritygroups.2da` to fall back on.

The KOTOR 2 deltas that were NOT obvious, all confirmed against the real game:

- **`prioritygroups.2da` has different columns.** KOTOR 2 splits three of them
  per platform — `volume_pc` / `volume_xbox`, `maxvolumedist_pc` / `_xbox`,
  `minvolumedist_pc` / `_xbox` — where KOTOR 1 has plain `volume`,
  `maxvolumedist`, `minvolumedist`. Writing the KOTOR 1 names into the KOTOR 2
  table is a *silent* failure: the cells come out empty and our cue group lands
  without its volume or falloff band. `PriorityGroup2da` now maps the
  `_pc`/`_xbox` suffix away, so one row definition serves both and both
  platform variants get the same value. The vanilla KOTOR 2 table ships as
  `Resources/prioritygroups_k2.2da`.
- **The keybind defaults transfer unchanged.** KOTOR 2's `keymap.2da` carries
  the same rows under the same ids with the same vanilla defaults —
  `action281a/b` = ActionLeft/Right (strafe, Z/C), `action284a/b` =
  CameraRotateLeft/Right (A/D), `action283a/b` = minigame steering (A/D). So
  the same four ini lines apply to `swkotor2.ini`.
- **Ini spelling and section differ.** KOTOR 2 writes
  `DisableVertexBufferObjects` without spaces, and carries `FullScreen` in
  BOTH `[Display Options]` and `[Graphics Options]` — windowed mode has to be
  set in both or the surviving one wins. `AllowWindowedMode=1` is absent from
  the shipped file and is required by the BorderlessFullscreen patch.
- **`Frame Buffer` is deliberately NOT set on KOTOR 2.** On KOTOR 1 we write
  `Frame Buffer=0` (the documented fix for crash-after-character-creation and
  loadscreen faults) and neutralise its side effect — a zero-sized save
  thumbnail that makes the engine's image scaler divide by zero — with the
  `save_crash_guard.cpp` detour. That guard is a KOTOR 1 hook whose address
  resolves to zero on KOTOR 2, so setting `Frame Buffer=0` there would hand
  KOTOR 2 the divide-by-zero with nothing underneath it, to dodge a
  driver-era KOTOR 1 bug. KOTOR 2 keeps its shipped `Frame Buffer=1`.
- **Intro movies: KOTOR 2 has no `biologo.bik`.** Its readable string data
  references the same `leclogo` and `legal` stems; `ObsidianEnt.bik` and
  `Aspyr.bik` exist in `Movies/` and are launch-time logos by every other
  sign, but neither stem could be confirmed in the executable (Steam's wrapper
  encrypts the code section on disk, so absence there proves nothing). Both are
  included: the mechanism fails safe, and the uninstaller and in-game toggle
  put every name back. **Unverified — listen on the first KOTOR 2 launch for
  whether an Obsidian or Aspyr splash still plays.**
- **Engine patches install in the SAME transaction as ours.** 4 GB + borderless
  rewrite `swkotor2.exe`, so applying them in a transaction of their own would
  change the hash that the accessibility patch's own version gate then reads.
  This mirrors how KOTOR 1 installs widescreen alongside ours.
- **No address database is needed for KOTOR 2.** `kotor2.hooks.toml` carries
  literal addresses, and KPatchCore treats a missing database match as
  non-fatal (only name-based lookups need one). The `AddressDatabases`
  directory must still exist, which staging creates.

Other per-game plumbing that landed with it: one Add/Remove Programs entry per
game (`--game k1|k2` in the uninstall string; a single shared entry could only
record one InstallLocation, which is why an installed KOTOR 2 used to be
invisible on a later run), WER crash-dump keys for both executables, a
running-game guard that checks both process names, and the in-game F5 updater's
handoff batch now waiting on the game it was injected into and relaunching that
one (it previously waited on a KOTOR 1 that was not running, updated the KOTOR 1
install, and launched KOTOR 1).

The dsoal spatial-audio toggle stays KOTOR 1 only and its button is absent
rather than inert on the KOTOR 2 maintenance dialogs — the pairing with Aspyr's
audio stack is unverified. Open research item, not a gap.

- **Game-version screen** (`GameVersionSelectionForm`) — step between the
  welcome dialog and the base-components screen. Two checkboxes: KOTOR 1
  (default on) and KOTOR 2 (default off). Next requires at least one checked;
  zero-checked shows an explanatory MessageBox rather than a silently disabled
  button (screen-reader reasoning in the class comment). Each selected game
  gets its own `MainForm` pass, sequentially, with the game name in the title —
  a screen-reader user needs the two runs separated, not interleaved. Cost is
  one extra `.kpatch` download when both are selected.
- **KOTOR 2 mod-selection flow (`Kotor2ModSelectionForm`)** — the K2
  counterpart of the K1 optional-mods screen, and it runs BEFORE our own
  install: TSLRCM is a third-party Inno installer that writes freely into the
  game folder (and replaces `dialog.tlk` outright), so nothing of ours should
  be sitting there yet. Three checkboxes (TSLRCM 1.8.6, K2CP, Tweak Pack), all
  default-on. The flow runs: Steam Workshop pre-flight → TSLRCM step → TSLRCM
  presence gate → `Kotor2ModsInstallForm` (K2CP + Tweak Pack pipeline) → one
  spoken per-mod summary box.
- **Steam Workshop pre-flight** — warns when
  `steamapps/workshop/content/208580/` holds anything, since Workshop items
  override directory-installed mods and break exactly what we are about to
  install. Derived from the game path rather than the default library, so it
  follows a game installed to a secondary Steam library. Detection is
  deliberately shallow (any content at all is worth the warning); the user
  chooses whether to continue.
- **Unattended TSLRCM install (`TslrcmInstallForm`)** — `DeadlyStreamClient`
  scrapes the guest download (cookie + csrfKey two-step), verifies the
  pinned SHA-256 (fail-closed: a changed upstream file routes to the manual
  link), then — when the KOTOR 2 folder was detected — runs TSLRCM's Inno
  Setup **silently** (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
  /DIR=<path> /LOG=<temp>`); silent is the default because the TSLRCM wizard
  is English-only while our installer speaks the user's language. The silent
  run is verified by a before/after `dialog.tlk` fingerprint (TSLRCM always
  replaces it; exit 0 with unchanged tlk = failure). Fallbacks: no detected
  path → announce handoff, run the wizard visibly; silent failure → Yes/No
  offer of the visible wizard. Download progress announces via UIA
  notifications at 25% steps; the install phase heartbeats ~15 s and
  disables Cancel (aborting Inno mid-install would leave a half-written mod
  install).
- **TSLRCM-first gate** — K2CP/Tweak Pack run only when TSLRCM is present.
  Three signals, strongest first: (1) the silent install verifies *itself* by
  fingerprinting `dialog.tlk` before and after, so `SilentInstalled` is direct
  evidence independent of any registry entry; (2) otherwise `TslrcmDetector`'s
  uninstall registry entry (DisplayName contains "Sith Lords Restored Content";
  both hives × both views); (3) if neither fires after a wizard handoff, the
  user is ASKED. That third step exists because the registry route rests on an
  assumption nobody has confirmed against a real 1.8.6 run — that its Inno
  script registers an uninstall entry at all. Concluding "not installed" from
  the absence of a signal we are not sure exists would silently skip K2CP and
  the Tweak Pack on every install: no error, no prompt, two mods quietly
  missing, and a player who believes their mod set is complete.
- **`TweakPackInstaller`** — DeadlyStream scrape of the pinned RAR5 archive,
  SHA-256 verify, extraction via Windows' built-in `tar.exe` (libarchive
  reads RAR5), then one headless HoloPatcher run per component. Components
  are staged individually as standalone `tslpatchdata` dirs (Part 2's
  `changes2.ini` renamed on staging), bypassing the archive's
  `namespaces.ini` and its bundled TSLPatcher exes entirely. Installed set:
  the seven main entries (Kaevee Removal 1+2, Saedhe's Head, Ravager,
  Atton, Kreia-Atris, Trayus Mandalore); the two Extras are excluded by our
  filters (Sith Lord Masks = visual, Gand Awareness = rebalance).
- **Untested end-to-end**: the scrapes and archive layout were proven live via
  curl/tar in July 2026, but the C# flow, the Inno silent switches, the
  registry detection, and the per-component HoloPatcher runs all still need a
  real run against a KOTOR 2 Steam Aspyr install.
- **KOTOR 2 engine patches** — Lane's two static kpatches, `4gb-patch` (LAA
  flag) and `borderless_fullscreen`, bundled as `Resources/4GBPatch.kpatch` +
  `Resources/BorderlessFullscreen.kpatch` (zipped verbatim from
  `third_party/Kotor-Patch-Manager/Patches/<name>/` — manifest + all hooks
  variants; KPatchCore selects hooks by exe SHA). They are declared in
  `GameTarget.Kotor2.BundledPatches` and install in the same transaction as the
  accessibility patch (see above for why that ordering is load-bearing).
  Idempotency via the same hash gate as K1 widescreen: an exe whose SHA-256 is
  not declared by the manifests (already patched, 3C-FD'd, unknown build) is
  skipped with a reason, never force-patched. No backup — Steam "Verify
  integrity" restores vanilla.
- **`K2cpInstaller`** — mirrors `K1cpInstaller` (GitHub tree fetch at pinned
  SHA → headless HoloPatcher), now ACTIVE in
  `ModInstallerCoordinator.BuildKotor2Pipeline()` behind the TSLRCM gate.
  Still open before release: a `.lyt`/`.vis` line-ending audit of K2CP's
  files (K1CP needed CRLF normalization; K2 engine uses the same parser
  family).
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
- Both hash pins date from 2026-07-27 and have not been re-verified against
  what DeadlyStream serves today. A stale pin is no longer a dead end (see
  "Keeping the third-party pins alive" above), but re-hashing them is still the
  difference between a one-wizard install and a guided manual one. The K1CP and
  K2CP pinned commits WERE re-verified 2026-08-03 and both still resolve.
- TSLRCM language handling on a German Steam install — needs a hands-on test
  of the 1.8.6 installer.
- ~~Install-order contradiction.~~ **RESOLVED 2026-08-03: TSLRCM → Tweak Pack →
  K2CP, the kotor.neocities.org order.** It contradicts K2CP's own README
  ("before anything else, except TSLRCM"), and the community build wins: that
  is the sequence people actually run end to end and report breakage against,
  whereas the README describes K2CP in isolation and cannot know what the Tweak
  Pack does to the same files. Both patch overlapping `.dlg` and `.2da`
  resources, so K2CP's edits landing last is a real decision.
- K2CP `.lyt`/`.vis` line-ending audit still not done (K1CP needed CRLF
  normalisation and KOTOR 2 uses the same parser family).
- Tweak Pack per-component language vetting still not done — the components
  ship `.dlg` edits; strref-based, so probably fine, but unverified against a
  localised TSLRCM install.
- Whether KOTOR 2's `ObsidianEnt.bik` / `Aspyr.bik` are actually played at
  launch (see the intro-movie note above) — one launch answers it.
- Whether the KOTOR 2 sound settings are worth touching at all. Aspyr's build
  is a modern rebuild with 32 3D voices already configured, so the working
  assumption is that it needs nothing — but that is an assumption, not a
  measurement, and no `[Sound Options]` values are written for KOTOR 2 until
  someone checks. dsoal is not offered there for the same reason.
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

## Keeping the third-party pins alive (2026-08-03)

The installer downloads mods whose upstreams we don't control. Pinning their
SHA-256 makes the download safe and makes it expire. Three layers, deliberately
ordered so the last one needs no maintenance at all.

**The pins never relax.** TSLRCM's installer is 138 MB fetched over a scraped
DeadlyStream endpoint and then executed elevated. If that scrape ever returns a
login redirect, an error page saved as a file, or a hijacked upload, the hash is
the only thing between that and running arbitrary code as administrator.
Weakening it to "looks like an Inno installer of about the right size" would
trade a real guarantee for convenience. A mismatch is refused, always.

**Layer 1 — `Resources/sources.json`, served by `SourcePins`.** All pins (hashes,
sizes, page URLs, pinned commits) moved out of `Config` constants into one JSON
file. It is embedded in the installer AND read from the repo's `main` branch at
startup; the remote copy wins when it parses and carries a schema version this
build understands, and every failure (offline, proxy, 404, malformed, future
schema) falls back to the embedded copy silently. Refreshing a pin is now an
edit to one file on `main` — no installer release, no build toolchain.
`Config`'s members forward to `SourcePins`, so call sites were untouched.

Trust note: the remote file carries hashes that decide what we execute, so it is
fetched over HTTPS from our own repository — the same trust root as the
installer binary and the `.kpatch`. It must never point at a host we don't
control. Anyone able to rewrite that file could already publish a malicious
release, so it is not a new attack surface.

**Layer 2 — guided manual acquisition (`ManualDownloadForm`). This is the one
that matters.** A remote pin file only helps while somebody is still maintaining
the repo; in five years it is likely dead. When automatic acquisition fails, the
installer explains *specifically* what happened ("the download worked, but the
file is not the version this installer was built against" vs "the download
failed: …"), opens the official page in the browser, and takes the file the user
downloaded — via a file picker, or a one-press button when a plausible file is
already sitting in Downloads. The install then continues exactly as if we had
fetched it. **This path cannot go stale**, and it also covers what a pin can't:
DeadlyStream changing its download flow, or going away.

Why accepting a user-supplied file is not a security downgrade: the hash exists
to substitute for provenance we don't have when fetching automatically. A file
the user downloaded from the official page has provenance — theirs. That is the
same trust as every manual mod install ever done, and better, because we drive
the rest correctly. The pin is still computed and logged, so a match is visible;
it is just not a gate. A magic-byte check (MZ / `Rar!`) runs regardless, but as
a mis-pick check, not a security one — it catches the download page being saved
instead of the file.

Wired for TSLRCM (`TslrcmInstallForm` takes a `suppliedInstallerPath` that skips
download and hash) and the Tweak Pack (`ModInstallContext.AskForManualDownload`,
injected so installers stay free of WinForms). A user-supplied file is never
deleted — it is theirs, in their Downloads folder.

**Layer 3 — mirroring with permission**, still open. Removes the scrape
entirely.

### HoloPatcher is bundled, not downloaded (2026-08-03)

It was the most fragile dependency in the pipeline and the only one with no
manual escape hatch: the user never sees HoloPatcher, so they cannot fetch it by
hand, and without it **no** TSLPatcher-style mod installs for either game —
K1CP included. It was also already on a fallback, since the canonical PyKotor
repo re-tagged without attaching binaries and the pin pointed at NickHugi's
fork's 2024 release.

Now embedded as `Resources/HoloPatcher.exe` (v1.60-patcher-beta4, 11.3 MB raw;
+7.1 MB on the published single-file installer, 76.5 → 83.6 MB). Licence: the
LICENSE file at that tag is the plain **GPL-3.0** text — GitHub's repo-level
"LGPL-3.0" label describes the repository's current state, not that tag. Same
licence as this project, and we invoke it as a separate process rather than
linking, so this is mere aggregation. `Resources/HoloPatcher-License.txt` ships
alongside and `Config.HoloPatcherRepositoryUrl` / `HoloPatcherPinnedTag` are
kept for attribution and source availability. Bumping it means replacing the
vendored file — a deliberate, reviewable act rather than an upstream change we
inherit silently.

### Open questions

- Auto-download vs bundled redistribution: **resolved for K1CP** — pull from GitHub master at install time, same flow as our own `.kpatch`. No bundling, no DeadlyStream auth. Other survivors still TBD per-mod: Swoop Bike Upgrades, JDR, Party Conversations on Ebon Hawk, Thematic Companions are DeadlyStream-only as of this writing; widescreen (UniWS) is hosted on WSGF and KOTOR High Resolution Menus is DeadlyStream-only. Permission requests where needed.
- Localisation detection: `dialog.tlk` header at offset 8 carries a uint32 language ID (0=EN, 1=FR, 2=DE, 3=IT, 4=ES). The installer already has `LanguageDetector` for its own UI; mod-side detection should reuse the same lookup.
- Strict-vanilla toggle: minimum (K1CP only) vs vanilla+ (K1CP + Swoop + Thematic). Surface in installer UI in screen-reader-friendly form.
- Update story: K1CP versions itself frequently. Pin a known-good commit SHA (not `master`) per installer release so a mid-release K1CP regression doesn't break new installs.
- Uninstall: TSLPatcher mods write a backup folder. Our installer needs to expose a clean uninstall path that restores those backups in reverse order.
