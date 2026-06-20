# Known Issues

Status tracker for accessibility-mod work, in four buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Unreproduced** — reported issues we can't reliably reproduce yet; need a repro before fixing.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### Resolution / fullscreen / menu-offset resilience (umbrella)

The mod's promise is "no mouse, no screen," yet several code paths silently assume a particular screen geometry and break when the user changes resolution, runs windowed, or installs a community widescreen / HR-menus patch that moves GUI layouts. Known coupling points so far: the radial cursor-ray dependency and the save-thumbnail `ImageScale` divide-by-zero (both fixed in v0.2.1 — see CHANGELOG; they're the first two examples of this class), in-world cursor parking, and any hardcoded hit-test offset compensations (e.g. the Options-panel tab-pitch shift). We deliberately avoided bundling the complex community resolution/menu patches for simplicity, but their changes — plus whatever resolution/windowed state a user lands in — must not be able to break core mod function. Needs: (a) an audit of every place we use screen-pixel coordinates, the OS cursor, or hardcoded offsets; (b) read geometry live from the engine instead of assuming; (c) installer-set sane graphics defaults; (d) a decision on auto-installing the widescreen + HR-menus patches and making our code robust to exactly the layout changes they introduce. See `installer.md` (Widescreen / HR-menus bundling) and the `project_radial_cursor_coupling` memory.

### Polish (and other) UI language defaults to German speech — fixed (now English)

Secondary finding from the same session log: `Lang: unknown LanguageID=5; defaulting to German`. LanguageID 5 is Polish; we had no mapping for it. Fixed: `DetectLanguageFromTlk` in `core_dllmain.cpp` now defaults to English (`L::En`) on every fallback path — unrecognised LanguageID, unreadable/bad `dialog.tlk`, and the path-resolution failures — since English is the most universal fallback for the non-DE user base. A correctly-installed DE copy still detects LanguageID=2 → German. Still tracks with the "Integrate a Polish translation" Planned item for proper PL labels.

### Tutorial popups not reading correctly?

The in-game tutorial popups (`Tutorial Popups=1`) may not be read aloud correctly — text missing, partial, or not spoken at all. These are a distinct message-box type from normal dialogue; confirm what's spoken vs shown and capture a log on a known tutorial trigger.

### Droids sometimes don't read all dialogue options

In some droid conversations not every reply option is read while cycling the choices. Possibly a listbox enumeration miss or an options-changed re-read race. Note which droid / conversation; check the option enumeration in `dialog_speech.cpp`.

### Shift+Down reads descriptions twice (Journal, items)

Pressing Shift+Down to read a full description often reads it twice — seen in the Journal and on items. Duplicate fire between our Shift+arrow description reader and an engine re-narration, or a missing Consume so the key both drives our read and falls through. (The superficially similar hacking-dialogue double-read, fixed in v0.5.3, turned out to be two separate reply readers — an input-path announce plus a poll monitor — both speaking; worth checking whether this Shift+Down case is the same shape: a second reader rather than a missing Consume.)

### Combat target-options columns stick when targets are far away

In the combat action menu, switching between the target-options columns isn't always fluid — when targets are far away, the column change sometimes won't go through, so the player can't reliably move across the options. Likely the target-resolution / range gate interfering with the column navigation (the engine re-deriving the target menu from distance). Capture which column move fails, the target distance, and a log; cross-check against `project_engine_action_picker.md` and the bare 1–7 dispatch (`project_bare_combat_keys_dispatch.md`).

## Unreproduced

### Zaalbar's subtitles not reading

Reported that Zaalbar has no subtitles read at all — his Shyriiwook is untranslated alien speech the player can't otherwise follow and should still be read, so the voiced-speaker filter (`dialog_speech.cpp`) looks to be over-suppressing him. No reliable repro yet. Capture a log on a Zaalbar conversation line and confirm whether the suppression path is firing on his lines. Pairs with the bark under-suppression bug above (same speaker-classification code).

### Off-by-one description reads while leveling up or character creation

Reported that during level-up and/or character creation the description read aloud belongs to the entry *above* the focused one — e.g. landing on Dexterity reads the Strength description. The focus/selection itself is on the right row; it's the description lookup that's shifted by one, so the player hears the wrong attribute's (or skill's/feat's) text. No reliable repro yet. Needs the exact screen (chargen step vs level-up panel — attribute, skill, or feat list), the row navigated to vs the description heard, and a timestamped log. Likely candidate: the description reader indexing into the row list with an off-by-one (stale-vs-current selection index, or a header/blank row offsetting the mapping).

## Planned

### Class-selection chargen screen

The "select your class" screen during character creation still needs accessible narration of the three class choices, their summaries, and confirm/back wiring. In particular, the game already provides a per-class description on this screen (the lore/role blurb a sighted player reads when highlighting a class) — that description text must be read out on class selection, not just the class name.

### Class selection should step left/right, not up/down

On the class-selection chargen screen the three classes are a horizontal row, so navigation should be Left/Right between classes rather than Up/Down. Align the arrow binding with the spatial layout when wiring the narration above.

### Beacon-active navigation announcements (remaining-route reading)

While a beacon (or autowalk) is active, the route announcements should describe the *remaining* way to the active waypoint — leading with the range and direction of the current target — rather than the full original route. The hard part is disambiguating intents: if the player selects another object just to hear where it is, the announcement must not balloon into the long, confusing full-route description for the beacon target. Design questions to resolve: how to keep "where is this thing I just selected" reads short while a beacon is running, and whether the separate Shift+Enter autowalk / Shift+`-` gestures are still needed at all, or whether they can be folded into / replaced by the plain selection + beacon flow. See `project_narrated_target_unified.md` and `project_map_cycle_architecture.md`.

### Additional manual map hints

Hand-authored map hints for specific story locations the game doesn't mark but players struggle to find without sight. Backlog so far:
- Rebel corpses.
- The backdoor of the Sandral estate.
Add these to the manual map-hint registry (see `project_map_cycle_architecture.md` / `map_user_markers.cpp` for how user/manual hints fold into the map-hint cycle). Confirm the exact module and in-world position for each before adding.

### Waypoints

Navigation waypoint / map-hint issues on specific maps. Maintainer-reported from play; the specifics below are as-described and need the exact module and in-world position confirmed before acting.

- **Dantooine paths correlate with nearby area transitions.** On Dantooine the route guidance / waypoints appear to line up with (or get pulled toward) the nearby area-transition points, so the path read points at a transition rather than the intended route. Investigate how the Dantooine route waypoints / map hints are derived near transitions, and which module(s) and transitions are involved. May turn out to be a bug rather than planned work once reproduced.
- **Missing waypoint for the banthas near the "dragon cafe".** Add a map hint / waypoint for the banthas by the "dragon cafe" (maintainer's name for the spot — confirm the actual landmark, module, and in-world position). Folds into the manual map-hint registry alongside the "Additional manual map hints" backlog above.

### Integrate a Polish translation

Add Polish as a supported language. Decide the integration path — installer locale JSON (alongside de/en/fr/it/es) and/or in-game speech strings routed through the shared strings system — and wire it in. Source of the Polish strings (community contribution vs AI draft like fr/it/es) to be determined.

### Nameable personal map pins

Let players give their own map pins a name when they drop one, and read that name back when cycling to the pin. Personal pins currently announce only a generic label plus position, so a player who marks several spots can't tell them apart. Needs a text-entry path that works from the keyboard with a screen reader (the editbox handling shipped for chargen + save naming — `menus_editbox.cpp` — is the closest existing mechanism, though map pins aren't an engine editbox so the entry surface differs), storage of the label alongside the pin in the save, and the cycle/focus announcements updated to speak it. See `project_narrated_target_unified.md` / `map_user_markers.cpp`.

### Improved tutorial for mod users

A better onboarding/tutorial for players using the mod — introducing the mod's keys and concepts (navigation, cycling, targeting, screens) in a guided way rather than expecting users to discover them. Scope and delivery (in-game guided flow vs. F1/Ctrl+F1 reference vs. external readme) to be decided.

### Improvements to the Pillar 1 wall tones

Refine the in-world wall sounds (the Pillar 1 navigation cues that signal nearby walls) — accuracy, timing, and how clearly they convey wall proximity/direction. Capture concrete cases where the current tones mislead or fall short, then tune the cue logic.

## Monitor

### Startup crash with non-NVDA screen readers (delay-load backend fault) — fix pending tester verification

A pl-PL beta tester (v0.2.1) crashed at startup, repeatedly, before any speech. Dump exception `0xC06D007F` = MSVC delay-load `ERROR_PROC_NOT_FOUND`: prism's `prism_registry_acquire_best()` walks every backend in priority order calling each `initialize()`; the ZDSR backend delay-loads the user's `ZDSRAPI.dll` (`C:\Program Files (x86)\zdsr\zdsr\`, confirmed via `kdev analyze-dump --modules`), which is present but exports a mismatched symbol set, raising an unhandled structured exception. NVDA/JAWS users never saw it because their backend wins priority and `acquire_best` returns before reaching the broken one — so it read as "non-NVDA readers crash." Fix (in `prism.cpp`): SEH-guard every backend probe (`acquire_best`, per-backend `acquire`, `initialize`); on an `acquire_best` fault, fall through `AcquireNormalFallback` which probes our preferred order one backend at a time, each guarded, skipping the broken one and landing on the next working backend (SAPI last as the universal safety net). Also covers the JAWS crash report (no log yet) — any backend whose vendor delay-load is incompatible is now skipped, not fatal. **Not yet verified in-game** — needs the ZDSR tester (and ideally the JAWS reporter) to confirm with a new build; can't repro locally without ZDSR installed. See the `project_prism_backend_delayload_crash` memory.

## Polish

### Starting an autowalk/beacon while one is active should switch, not cancel

If an autowalk or beacon is already running and the player triggers a new autowalk or beacon on a different target, the current behaviour just cancels the existing one. Instead it should immediately start the new action on the new target — switching the route in one gesture rather than requiring cancel-then-start.

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Installer fr/it/es translations are AI drafts

`installer/KotorAccessibilityInstaller/Locales/{fr,it,es}.json` were drafted by Claude from the English source rather than by native speakers. Strings render correctly and key parity with `en.json` is verified (136/136 keys), but specific phrasings — error messages, formal-vs-informal address ("vous" / "lei" / "usted"), and idiomatic phrasings around modding terminology — may read awkwardly to a native ear. Native-speaker contributors are welcome to refine; PRs against the three JSON files are scoped contributions. German (`de.json`) is human-authored by the maintainer and is the quality bar.

### Investigate extracting shared panel-feature helpers

Several screen modules repeat the same scaffolding — structural panel identification (foreground modal + vtable/field checks), SEH-guarded field readers, a snapshot/diff "announce on change" loop, and hotkey-`Consume`-then-poll gating. `pazaak.cpp` is the newest example; the menu/combat modules have older variants. Worth a pass to see how much can move into shared helpers without over-generalising (past refactors already pulled out the listbox/select-then-confirm and `menus_*` seams). Ready-to-paste prompt for a future session:

> Audit the panel/screen feature modules in `patches/Accessibility/` (e.g. `pazaak.cpp`, `menus_store.cpp`, `menus_*`, `combat*`, `examine_view.cpp`, `view_mode.cpp`) for repeated boilerplate: (a) structural panel identification, (b) SEH-guarded `ReadInt`/`ReadPtr`/field readers, (c) snapshot-and-diff "announce deltas" loops, (d) `hotkeys::Consume` + `Pressed` gating tied to a foreground panel. Propose a small set of shared helpers (header + cpp) that capture the common shapes WITHOUT forcing unrelated modules into one mould — respect the existing `menus_*` seams and the prior refactor boundaries. For each proposed helper: which call sites adopt it, what stays bespoke, and the risk. Don't refactor yet — produce the plan and one worked example (convert `pazaak.cpp`), then stop for review.
