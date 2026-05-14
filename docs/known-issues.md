# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.
- **Beta Preparations** — non-feature work that must land before a public beta.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

- **Map-mode edge collision is silent.** `map_ui_cursor.cpp` (~L683) fires `PlayCue3D(GetNavCueResref(NavCue::Collision), cuePos, kEdgeCueGain)` when the virtual cursor hits the explored-region edge, but no sound is heard live (verified 2026-05-14 — view-mode wall collision IS audible, ruling out the audio-bus path itself). Likely callsite-gating rather than audio-bus: either the edge detector never fires, the `kEdgeCueGain=8.0` quiet-throttle window is suppressing repeated triggers, or the resref `gui_invdrop` is masked under the map UI's own audio. Reproduce by opening the in-game map and trying to scroll past the fog-of-war boundary; grep the patch log for `Map.UI: ... PlayCue3D_ok` to see whether the fire path is being reached at all.

- **Container Enter takes ALL items (FIX SHIPPED, UNTESTED).** Original behaviour: Enter on the loot panel fired `BTN_OK` (id=3, "Nehmen"), which emptied the entire chest in one shot. Working hypothesis was that `BTN_OK` is "Take All" by engine design and per-item take happens by clicking the listbox row directly. Implemented fix in `menus.cpp` (Container input block): if `selection_index >= 0`, `FireActivate` the row at `lb.controls[selection_index]` instead of `BTN_OK`; if `selection_index < 0` (no row navigated yet), still fall back to `BTN_OK` so the take-all gesture is reachable by pressing Enter immediately on panel open. Verify next session by reading the `Container: Enter resolves to row ...` log line and checking whether the chest shrinks by exactly one item per Enter or still empties wholesale. If the fix doesn't work, escalate to investigating the engine's `CSWGuiListBox::SetSelectedControl` / `OnRowClicked` path — row activation may need a different vtable slot than the standard `kInputActivate` (0x27) FireActivate.

- **Charakterblatt character carousel doesn't actually swap the displayed character.** Pressing Enter on `Vorheriger Charakter` / `Nächster Charakter` dispatches `FireActivate` cleanly (log shows `is_active=0->1`), and the engine handlers `CSWGuiInGameCharacter::OnSwitchLeft` @0x006af450 / `OnSwitchRight` @0x006af6d0 do fire, but the panel labels don't rewrite — no `ContentChange` follows, no new snapshot is announced. OnSwitchLeft's decompile shows an early-out when `(char)btn_change1.custom_value == -2`, which may be the no-op gate on a single-character party (only Trask present on Endar Spire / pre-Taris). Retest with a multi-character party (post-Taris) before chasing further: if the engine swap fires there, the existing content-fingerprint diff in `MonitorPanelContents` should pick up the label rewrite and re-snapshot. If it stays silent even with a real party, the click likely needs to route through a different engine entry (the carousel may animate via `btn_change1`/`btn_change2` decoration buttons rather than firing the OnSwitch handlers under our `kInputActivate` dispatch).

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
- **Force powers picker (Dantooine onward).** The 5th LevelUp step button `Kräfte` opens `CSWGuiPowersLevelUp` (constructed by `CSWGuiLevelUpPanel::OnSelectPowersButton` @0x006ee350, `operator new(0x1a10)`). It's locked for non-Force classes — only navigable after the player becomes a Jedi on Dantooine. Untested layout; expect similar shape to the Feats/Skills/Attributes pickers (description listbox + chart of available powers + accept/back). Investigate the panel structure once a Jedi-class save is available.

### Game modes

- Support for the card game (Pazaak).
- Support for swoop races.
- Support for turret shooting.

## Monitor

- Combat announcement.
- Room descriptions.

## Polish

- **View-mode wall-collision cue feels off.** Currently uses `NavCue::Wall` = `as_nt_wtrdrip_09` (water drip ambient), which doesn't carry the "you bumped into geometry" semantics — it sounds environmental, not interactive. Source-WAV swap candidate, same audition workflow as the heartbeat-cue entry above. Constraints: short transient, neutral semantics (not "alarm", not "combat"), and ideally something distinct from the heartbeat so the two are easily separable by ear.

- **View-mode collision should migrate to the unified room-shape system.** Today `view_mode.cpp` re-runs its own `SegmentCrossesWalkmesh` against `spatial::change_detector::GetCachedWalls` to clamp the virtual cursor. As the longer-term walkmesh/nav work converges on one canonical room-shape description (see `docs/walltopo*` / `docs/navsystem*` — Path 3 GVD/medial-axis is the current preferred direction), view-mode should switch to that shared accessor instead of carrying its own wall-cache plumbing. Wins: one place to fix walkmesh edge cases (manifold quirks, portal seams, multi-elev surfaces), consistent collision feel across view mode + autowalk + map + future features.

- **Specials-empty heartbeat cue needs a better source WAV.** `combat_special_watch.cpp` currently uses `c_drdastro_hit2` (astromech droid hit — sharp metallic clang). It IS audible over combat, but the timbre isn't ideal — the user reports it as "working but not great" and wants a better-feeling cue. Constraints we've already pinned: 2D playback at priority_group=15 (vol=127), volume_byte=127 — every gain knob in the OneShot path is maxed. The fix is a source-WAV swap, not more amplification. Candidates to audition next: explosion claps (`cb_ex_*`), error chimes, voice-bark fragments. The bottleneck is finding a source mastered hot enough AND with a sharp transient AND with the right "tactical decision moment" semantics — current candidate fails on the third criterion. Glossary at `build/cue-glossary/` already has the previous candidates (A–I) for A/B comparison.

- **Cycle items have English description labels.** Cycle widgets such as Difficulty announce as `"Difficulty, Leicht"` rather than `"Schwierigkeitsgrad, Leicht"` in the German build. The captured category text comes from the `.gui` file's default at panel construction time, which is English. Localized German lives in the parent Options panel's listbox-blob (per-tab) or in TLK str_refs we haven't located yet — fixing this needs either cross-panel blob lookup or a hooked panel-init function that captures the text before the engine's localization step replaces it.

## Beta Preparations

Items needed before a public beta release. Not blocking individual feature work, but each must be in place before we ship to outside testers.

- **Installer.** End-user-facing install path. Currently the project assumes a developer with `kdev` + Visual Studio Build Tools to compile + apply patches. Beta testers need a one-shot installer that drops the `.kpatch` + Tolk runtime into the right Steam/GoG install location, ideally with auto-detection of the install path and a rollback / uninstall option. Decide between a TSLPatcher / HoloPatcher flow (community-standard but doesn't naturally handle DLL injection) vs. a custom installer that wraps `KPatchLauncher`.
- **Change log.** Curated user-facing change log separate from git history. Each release lists which screens / features got accessible, which screen-reader behaviours changed, which key bindings moved. Likely `CHANGELOG.md` at repo root, [Keep a Changelog](https://keepachangelog.com) format. Pre-1.0 changes get a single "0.x.0" entry block.
- **Version structure.** Decide and document the version number scheme (semver? `0.MAJOR.MINOR`?), where it lives in the source (current candidate: a `kVersion` constant plus the version banner in `tolk::Speak("[!] KOTOR accessibility mod loaded, version 0.1.0")` — those should source from the same place), and how it gets stamped into the built `.kpatch` package's manifest.
- **`CONTRIBUTING.md`.** Onboarding doc for outside contributors. Cover: how to clone + build, dev loop (`kdev dev` etc.), where to find the documentation that's essential before changing engine-side code (`docs/llm-docs/re/`, `docs/known-issues.md`, the per-pillar plan docs), commit-message style (the existing `Accessibility: ... (TESTED|UNTESTED|PARTIAL)` convention), how to run an in-game session and read the patch log, code-of-conduct expectations.
