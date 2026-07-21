# Unified Resolution Patch — compatibility notes

Investigation of **J0-o/KotorUniResPatch** as the screen-scaling / widescreen
solution to bundle for low-vision players and for streaming the game to sighted
viewers. Written 2026-07-21 against upstream KPM 0.6.0.

Repo: https://github.com/J0-o/KotorUniResPatch

## TL;DR

- Directionally the **right target**: it is the only project in the ecosystem
  that actually *scales the HUD, menus, fonts, map and scrollbars* rather than
  just changing the window. The `2x Font` / `2x List Item Height` modules are a
  direct low-vision win; the HUD/menu scaling is what makes a widescreen res
  look correct when streaming.
- KPM-native (a suite of `.kpatch` modules), so it drops into our exact pipeline.
- **Not ready to wire into the public installer yet.** Three gates: (1) it has
  **no license** — we cannot legally redistribute it; (2) the actual
  resolution-unlock is a **static exe patch that does not work on the Steam
  release**; (3) it is **early beta** with essentially no external testing.

## What it is, and how it relates to KPM

- **KPM 0.6.0 does NOT ship a resolution/scaling patch.** It ships
  `BorderlessFullscreen` (author vrifftech, id `borderless_fullscreen`) — just
  borderless-window presentation. The old standalone `Widescreen` patch was
  deleted upstream.
- **KotorUniResPatch is a separate, wider project**, not bundled in KPM, but
  built to run *through* KPM ("Use with Kotor Patch Manager"). Author **J0-o is
  an active KPM-core contributor** (shader override, MoviePatch, K2 console,
  post-combat fix, headless — all landed in KPM 0.5.2/0.6.0), so the two repos
  are closely related but distinct.

## Module inventory (v1.0.34)

It is not one patch — it is a modular suite of ~15 KPM patches, each
independently installable, plus a texture pack:

- `Resolution Unlocker` — removes the exe's resolution caps (**static**)
- `2x Font`, `2x List Item Height`, `2x Popup Messages`, `Scale Container Popup`
- `Scaled HUD`, `Scaled Menu`, `Scaled Class Menu`, `Scaled Panels`,
  `Scaled Scrollbars`, `ScaledMenuBorders`, `Scaled Letterbox`,
  `Scaled Map + Minimap`, `Map Texture Patch`, `Movie Patch`
- `for Override` — scaled UI TGAs, shipped as the 21 MB `_textures.zip`

Releases ship two zips: `KotorUniRes_<ver>_patches.zip` (~77 KB, the .kpatch
bundle) and `KotorUniRes_<ver>_textures.zip` (~21 MB, Override TGAs).

## The Steam problem (most important finding)

The modules split into two hook kinds that behave very differently on the
**Steam** release we target:

- **Scaling modules are `type = "detour"`** — runtime, in-memory hooks, the
  same mechanism our accessibility patch uses. These **work on Steam.**
  Confirmed on `2x Font`: it detours `CAurFont::TextOutA` (0x004A1770) and
  `CAurGUIStringInternal::Draw` (0x0045A850) at runtime.
- **`Resolution Unlocker` is `type = "simple"`** — static byte-patches written
  to `swkotor.exe` on disk (e.g. zeroing resolution caps at 0x0068C4E3,
  0x0068C4F3, 0x0068C4FA). Its `[patch.supported_versions]` lists only
  `kotor1_gog_103` and `kotor1_cdcrack_103` — **no Steam hash.**

Static exe patching is exactly what KPM's PR #130 states cannot work on Steam:
K1's Steam `.text` is SteamStub-encrypted on disk, so `original_bytes` read as
ciphertext and the patch cannot apply. KPM 0.6.0's new `DeSteamify` patch —
which could strip the wrapper — is **KOTOR 2 only** right now (`kotor2-desteamify`,
targets `kotor2_steam_aspyr`); there is no K1 Steam DeSteamify in KPM 0.6.0.

**Net on Steam:** the UI-scaling detours apply and help, but the
resolution-unlock is blocked — so there is no unlocked widescreen resolution to
scale *to*.

**Live confirmation (2026-07-21):** we hit this exact wall with KPM 0.6.0's own
`BorderlessFullscreen` patch (id `borderless_fullscreen`, the intended
replacement for the deleted `widescreen` patch). It is a single static hook at
0x0044DC0C. Applying it to our Steam exe failed byte-verify — expected
`[BE 00 00 CF 02]`, got `[68 31 3C D9 95]` (ciphertext), and because the install
is one transaction it aborted our runtime accessibility patch too. Our dev loop
now bundles no screen patch (`additional_patches = []`); our own patch is all
runtime detours and applies on Steam alone. This is direct empirical evidence
that any *static* screen patch (borderless or `Resolution Unlocker`) is a hard
Steam blocker without DeSteamify or a runtime re-implementation.

**Tractable path:** the resolution unlock is only a handful of byte-writes at
known addresses. It could be re-implemented as a **runtime** patch that writes
those bytes in memory after decryption — the same trick our DLL already uses —
making the whole suite Steam-viable. Small, well-scoped engineering; either we
build it or J0-o adds it.

## Maturity / beta assessment

Early beta, but competently and actively developed:

- Created 2026-06-06 (~6 weeks old at time of writing); last commit 2026-07-13.
- ~19 downloads on the latest release; 1 star, 0 issues, 0 forks — no external
  testing footprint. We would be an early adopter.
- Version numbers (`v1.0.34`) are the author's choice, not a maturity signal:
  `0.1.69` → `v1.0.24` → `v1.0.33` → `v1.0.34` in five weeks.
- Release notes read like the classic hard KOTOR-scaling bugs being knocked
  down one at a time ("fixed giant hud icons", "fixed missing HUD items", "HUD
  shouldn't reset back to 800x600 anymore", "letterbox and dialogue choice
  fix") — encouraging, but those were live very recently.
- Stated rough edge: changing resolution **in-game** breaks; set it at the main
  menu, and if the menu goes blank after a change, "spam esc". Set-once-at-menu
  is the safe path.

## Licensing (redistribution blocker)

The repo has **no license file** (GitHub `/license` returns 404). "No license"
= all-rights-reserved by default, so we **cannot bundle or redistribute it** in
our GPLv3 installer without J0-o's explicit permission. Options: (a) J0-o adds a
compatible license; (b) installer downloads it from their releases at install
time (cleaner with permission); (c) coordinate a direct arrangement. J0-o is
already in the KPM circle and approachable.

## Interaction with our accessibility mod

- It scales the exact GUI surfaces our menu-nav cursor synthesis drives (menus,
  listboxes, HUD, dialog replies). We already carry widescreen-specific nav
  handling (the "park cursor off dialog reply list so widescreen keyboard nav
  reaches every reply" fix). Resolution/scaling changes the hit-test geometry
  our synthesized cursor relies on — needs deliberate regression testing, not
  install-and-go.
- Being KPM-native, it installs in the **same KPM transaction** as our patch,
  against the clean exe, so the runtime pieces sidestep the exe-hash /
  version-gate problem PR-5 was about.
- `Resolution Unlocker` declares `conflicts = ["widescreen", "widescreen-simple",
  "kotor-widescreen", ...]` — it replaces the old widescreen patch; do not run
  both.

## Open questions for J0-o

1. **License** — will you add a license (ideally GPL-compatible) so an
   accessibility mod can redistribute/bundle the suite? If not, are you OK with
   an installer that downloads your releases at install time?
2. **K1 Steam** — is there an intended path for the Steam release? Specifically:
   do you plan a **runtime** resolution-unlock (in-memory byte-writes after
   decryption) or a **K1 DeSteamify**, so the static `Resolution Unlocker` isn't
   a hard Steam blocker? Would you accept a runtime resolution-unlock module if
   we built one?
3. **Version stability** — is there a version you consider stable enough to pin,
   or is the API/module set still moving? Which modules do you consider solid vs
   experimental?
4. **Module independence** — can the scaling modules (font/HUD/menu/list) be run
   *without* `Resolution Unlocker` at stock resolutions (useful on Steam until
   the res-unlock story is solved), or do they assume an unlocked resolution?
5. **Textures** — are the Override TGAs required for the scaling modules to look
   right, or optional polish? Any conflict risk with K1CP's Override content?

## Recommended sequencing

1. Land the KPM 0.6.0 rebase first (it is the base J0-o targets; gives us the
   current AddressDatabases). — see the rebase work; dev loop swaps
   `widescreen` → `borderless_fullscreen` for now.
2. Reach out to J0-o with the questions above; the K1-Steam answer decides
   whether Steam users can use it at all.
3. Pilot locally against our patch on the Steam install: apply the runtime
   scaling modules, test menu/dialog nav for regressions, prototype a runtime
   resolution-unlock so a widescreen res is actually selectable.
4. Only then decide bundle-vs-optional-download in the installer, pinned to a
   specific version.
