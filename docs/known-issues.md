# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.
- **Beta Preparations** — non-feature work that must land before a public beta.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

- **Container Enter takes ALL items (FIX SHIPPED, UNTESTED).** Original behaviour: Enter on the loot panel fired `BTN_OK` (id=3, "Nehmen"), which emptied the entire chest in one shot. Working hypothesis was that `BTN_OK` is "Take All" by engine design and per-item take happens by clicking the listbox row directly. Implemented fix in `menus.cpp` (Container input block): if `selection_index >= 0`, `FireActivate` the row at `lb.controls[selection_index]` instead of `BTN_OK`; if `selection_index < 0` (no row navigated yet), still fall back to `BTN_OK` so the take-all gesture is reachable by pressing Enter immediately on panel open. Verify next session by reading the `Container: Enter resolves to row ...` log line and checking whether the chest shrinks by exactly one item per Enter or still empties wholesale. If the fix doesn't work, escalate to investigating the engine's `CSWGuiListBox::SetSelectedControl` / `OnRowClicked` path — row activation may need a different vtable slot than the standard `kInputActivate` (0x27) FireActivate.

## Planned

### Map

- Update map support to best-working description system.
- Cycle functionality for the map.
- Check for additional map info (Phase 6).
- Support for the starmap.

### Keybinds

- Clean up keybinds.
- Keybind for waypoint and orthogonal orientation.

### Combat

- Add sound cues for empty combat queue.
- Add filtering and parsing for combat messages.

### Object filters

- Add line-of-sight filter.
- Additional filtering (TBD).

### Menus

- Support for trading menu.
- Support for planet picker.
- Support for group selector.
- Support for fast travel.
- Support for leveling up.

### Game modes

- Support for the card game (Pazaak).
- Support for swoop races.
- Support for turret shooting.

## Monitor

- Combat announcement.
- Room descriptions.

## Polish

- **Cycle items have English description labels.** Cycle widgets such as Difficulty announce as `"Difficulty, Leicht"` rather than `"Schwierigkeitsgrad, Leicht"` in the German build. The captured category text comes from the `.gui` file's default at panel construction time, which is English. Localized German lives in the parent Options panel's listbox-blob (per-tab) or in TLK str_refs we haven't located yet — fixing this needs either cross-panel blob lookup or a hooked panel-init function that captures the text before the engine's localization step replaces it.

## Beta Preparations

Items needed before a public beta release. Not blocking individual feature work, but each must be in place before we ship to outside testers.

- **Installer.** End-user-facing install path. Currently the project assumes a developer with `kdev` + Visual Studio Build Tools to compile + apply patches. Beta testers need a one-shot installer that drops the `.kpatch` + Tolk runtime into the right Steam/GoG install location, ideally with auto-detection of the install path and a rollback / uninstall option. Decide between a TSLPatcher / HoloPatcher flow (community-standard but doesn't naturally handle DLL injection) vs. a custom installer that wraps `KPatchLauncher`.
- **Change log.** Curated user-facing change log separate from git history. Each release lists which screens / features got accessible, which screen-reader behaviours changed, which key bindings moved. Likely `CHANGELOG.md` at repo root, [Keep a Changelog](https://keepachangelog.com) format. Pre-1.0 changes get a single "0.x.0" entry block.
- **Version structure.** Decide and document the version number scheme (semver? `0.MAJOR.MINOR`?), where it lives in the source (current candidate: a `kVersion` constant plus the version banner in `tolk::Speak("[!] KOTOR accessibility mod loaded, version 0.1.0")` — those should source from the same place), and how it gets stamped into the built `.kpatch` package's manifest.
- **`CONTRIBUTING.md`.** Onboarding doc for outside contributors. Cover: how to clone + build, dev loop (`kdev dev` etc.), where to find the documentation that's essential before changing engine-side code (`docs/llm-docs/re/`, `docs/known-issues.md`, the per-pillar plan docs), commit-message style (the existing `Accessibility: ... (TESTED|UNTESTED|PARTIAL)` convention), how to run an in-game session and read the patch log, code-of-conduct expectations.
