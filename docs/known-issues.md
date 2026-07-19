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

### Shift+Down reads descriptions twice (Journal, items)

Pressing Shift+Down to read a full description often reads it twice — seen in the Journal and on items. Duplicate fire between our Shift+arrow description reader and an engine re-narration, or a missing Consume so the key both drives our read and falls through. (The superficially similar hacking-dialogue double-read, fixed in v0.5.3, turned out to be two separate reply readers — an input-path announce plus a poll monitor — both speaking; worth checking whether this Shift+Down case is the same shape: a second reader rather than a missing Consume.)

## Unreproduced

### Zaalbar's subtitles not reading

Reported that Zaalbar has no subtitles read at all — his Shyriiwook is untranslated alien speech the player can't otherwise follow and should still be read, so the voiced-speaker filter (`dialog_speech.cpp`) looks to be over-suppressing him. No reliable repro yet. Capture a log on a Zaalbar conversation line and confirm whether the suppression path is firing on his lines. Pairs with the bark under-suppression bug above (same speaker-classification code).

### Off-by-one description reads while leveling up or character creation

Reported that during level-up and/or character creation the description read aloud belongs to the entry *above* the focused one — e.g. landing on Dexterity reads the Strength description. The focus/selection itself is on the right row; it's the description lookup that's shifted by one, so the player hears the wrong attribute's (or skill's/feat's) text. No reliable repro yet. Needs the exact screen (chargen step vs level-up panel — attribute, skill, or feat list), the row navigated to vs the description heard, and a timestamped log. Likely candidate: the description reader indexing into the row list with an off-by-one (stale-vs-current selection index, or a header/blank row offsetting the mapping).

## Planned

### Tutorial keyboard hints: French / Italian / Spanish translation — DONE (2026-07-19), AI drafts pending native review

The full tutorial keyboard-hint set (game pop-ups + Trask's Endar Spire walkthrough + the level-up and stealth hints) is now authored in all five languages: the `TutHint*` / `TutTrask*` / `TutLevelUp` / `TutStealthMode` cases were filled into `strings_fr.cpp` / `strings_it.cpp` / `strings_es.cpp`, mirroring DE/EN. FR/IT/ES installs now speak the keyboard hint instead of falling back to the vanilla mouse-worded pop-up. **Caveat:** like the installer strings (see below), the three new-language sets are Claude-authored drafts, not reviewed by native speakers — same standing review item.

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

- **Endar Spire scenery-battle hint.** Beta finding (userlogs/endarspiresoldiersunreachable1): in the Command Module (`END_M01AA`) the scripted background firefight actors — tags `end_cut2_sith1`–`5` and `end_cut2_soldier1`–`4`, plus the Jedi/Sith Apprentice duel pair — are neutral cutscene props, but Q/E focuses them and the enemy-style brief (One Damage Blaster, permanent effects) makes them sound fightable. The engine's default verb for them is Dialog, so Enter just walks the PC into the crowd (often ending in "way blocked"); one tester spent most of a session trying to fight them across three restarts. When the tutorial work lands, speak a one-shot custom hint if the player Q/E-focuses exactly these creatures ("scripted battle, no need to fight — walk past"), keyed on the `end_cut2_*` tags. A general friendly/neutral marker on every creature was considered and rejected as too chatty for a tutorial-only confusion; sighted players get the same information from the reticle colour, and outside the tutorial hostile-looking neutrals are rare.

### Trask / Endar-Spire dialogue tutorial hints via a post-VO popup — IMPLEMENTED, double-read wart

The voice-acted Endar-Spire tutorial lines (Trask + the `end_pop*` windows) stay suppressed during Trask's Basic VO; at each dialogue **reply prompt** (the game's own break) we now fire a real engine tutorial popup carrying the keyboard hint. Mechanism (`tutorial_popup.cpp`): `dialog_speech` records the pending hint (+ strref) when a rewritten tutorial line plays (`HintForDialogLine`); `MonitorDialogReplies` fires the popup at the reply break; `FirePopup` clears the tutorial once-shown bit for a repurposed reason (0x2a), calls `CGuiInGame::ShowTutorialWindow` @0x0062f4a0 directly (bypasses the funnel's `field45` "no tutorials in dialogue" gate), `SetTutorialReason` @0x006aa900 configures it as a real tutorial, `CSWGuiMessageBox::SetMessage(strref)` @0x006249d0 sets the visible (mouse) text, and it pauses via `SetPauseState`. A `SyntheticActive` flag routes the spoken text to the keyboard hint; `PollDismiss` unpauses on close. Confirmed working: fires at the right breaks, mounts over the dialogue, pauses, single tutorial button, dismisses cleanly, Trask's own subtitle stays suppressed.

The earlier double-read (keyboard hint + redundant mouse sentence) is **fixed**: it was **five** independent announce paths reading the same box, not the four first assumed — content-fingerprint, single-row listbox monitor, engine focus-announce, arrow-nav chain (`AnnounceControl`), and a fifth, `MonitorFocusedControl`, which re-speaks the focused control's text on change. The synthetic popup routes speech by **identity** now (its message is the popup's only single-row listbox → speak the hint; buttons read their labels), applied consistently in `AnnounceControl` and `MonitorFocusedControl`, plus `SyntheticActive` suppression on the listbox monitor and focus-announce. (Debugging note: a stale-DLL trap cost several cycles — `kdev apply` silently skips the copy when the game holds the DLL open, so always close the game and verify `patches/accessibility.dll`'s mtime after apply.) Timing is now correct: the popup fires when a reply first becomes readable/navigable (`src=dialog-state` in `MonitorDialogReplies`, i.e. the VO has ended), not when the reply list merely appears — so it no longer talks over Trask and Enter still skips his line. (The `sel −1 → 0` transition can't be used: the reply listbox is one persistent object whose selection never resets to −1 between prompts.) Trask often delivers several rewritten lines back-to-back before a break, so hints are **accumulated** (newline-joined, generous 4 KB buffers, no 256-char clip on any speak path) and shown together in one popup. FR/IT/ES dialogue hints and the reply-bracket lines (`48340`–`48343`) remain deferred.

### Improvements to the Pillar 1 wall tones

Refine the in-world wall sounds (the Pillar 1 navigation cues that signal nearby walls) — accuracy, timing, and how clearly they convey wall proximity/direction. Capture concrete cases where the current tones mislead or fall short, then tune the cue logic.

## Monitor

### Corridor-terminus dead-end recognition (widened shape gate) — watch for false positives

The room-shape dead-end gate (`IsAlcoveAlongAxis` in `wall_topology.cpp`) was widened (v0.6.0) so a corridor's blocked end classifies as a real dead end, not just a tight alcove. Root cause: at the west end of an east-west Endar Spire corridor the player heard the corridor's "Ost-West" axis instead of "Sackgasse, Ost". The degree-1 terminus node (`node[5]`, rays E=17.2 back-W=1.4 sides N=2.8/S=2.5) failed the old all-three-rays-within-2m alcove test because a corridor is wider than an alcove, so `ClassifyCluster` marked it filtered and `LookupAt`'s primary scan skipped it, snapping to the neighbouring corridor cluster ~5m east. The gate now accepts an open forward ray with a close wall behind (`kDeadEndBackM = 2.0`) and the two perpendiculars merely bounded to corridor width (`kDeadEndSideM = 4.0`); the old alcove case is the subset where the sides are also within 2m. Confirmed fixed in-game (spoke "Sackgasse"). **What to watch:** the same gate feeds junction-exit rendering (`WalkmeshAgreesDeadEnd` at the two junction call sites), so the relaxation also lets more degree-1 neighbours count as real dead-end spurs rather than being suppressed as wall-curve artefacts. A genuine open-area wall-curve node that happens to be boxed on three sides within 4m could now be voiced as a "Sackgasse" it shouldn't be. If false dead-ends appear in open areas, tighten `kDeadEndSideM` (the clearance-dump rays in the patch log show the geometry per node). No such regression observed yet.

### One-off crash entering the Sith Academy (creature-teardown access violation) — not reproduced

One hard crash (`0xC0000005` access violation, real dump `swkotor.exe.9744.dmp`, 2026-07-13) while walking from Dreshdae (`korr_m33aa`) into the Sith Academy entrance (`korr_m33ab`). The fault is on the **engine's own module-unload teardown**, not the load and not any path we hook: `CClientExoAppInternal::UnloadModule → ~CGameObjectArray → ~CSWCCreature → ~CSWCLevelUpStats → CSWCCreatureStats::ClearFeats`. `ClearFeats` walks the creature's feats vector; at the fault `this=0x1773dfa0`, `feats.size=0x1482f` (84,015 — impossible for a real creature), and the read lands at `this + size×2 ≈ 0x17767000`. So the `CSWCCreatureStats` block is **corrupt / freed-and-reused** (84,015 reads like leftover heap bytes, not a bad `.utc` value) and the destructor loops off the end — a heap-corruption / use-after-free whose *source* is upstream of the victim destructor. The crash log shows no movie foreground and no anomalies during the Dreshdae session, so this is **distinct** from the Leviathan cutscene fix (commits `0f64a0a` / `122fb5c`): that one was our speech-window activity aborting the Bink movie queue → silent process exit with no dump, whereas this is a genuine access violation with a dump on a teardown path we only ever *read* from. We hook nothing on this path and never write creature/feat data, so it leans engine/save-side — but a stray OOB write elsewhere in the mod landing in that block can't be ruled out from the log alone. **Could not reproduce** (maintainer retried the same transition). If it recurs, the decisive test is a mod-off run (rename `dinput8.dll` → `dinput8.dll.off`, load the same save, cross again): still crashes → engine/save; stops → hunt a stray write in our code. Secondary lead: pull the `korr_m33aa` creature templates to check for a malformed NPC.

### Startup crash with non-NVDA screen readers (delay-load backend fault) — fix pending tester verification

A pl-PL beta tester (v0.2.1) crashed at startup, repeatedly, before any speech. Dump exception `0xC06D007F` = MSVC delay-load `ERROR_PROC_NOT_FOUND`: prism's `prism_registry_acquire_best()` walks every backend in priority order calling each `initialize()`; the ZDSR backend delay-loads the user's `ZDSRAPI.dll` (`C:\Program Files (x86)\zdsr\zdsr\`, confirmed via `kdev analyze-dump --modules`), which is present but exports a mismatched symbol set, raising an unhandled structured exception. NVDA/JAWS users never saw it because their backend wins priority and `acquire_best` returns before reaching the broken one — so it read as "non-NVDA readers crash." Fix (in `prism.cpp`): SEH-guard every backend probe (`acquire_best`, per-backend `acquire`, `initialize`); on an `acquire_best` fault, fall through `AcquireNormalFallback` which probes our preferred order one backend at a time, each guarded, skipping the broken one and landing on the next working backend (SAPI last as the universal safety net). Also covers the JAWS crash report (no log yet) — any backend whose vendor delay-load is incompatible is now skipped, not fatal. **Not yet verified in-game** — needs the ZDSR tester (and ideally the JAWS reporter) to confirm with a new build; can't repro locally without ZDSR installed. See the `project_prism_backend_delayload_crash` memory.

### Too many overlapping UI-announce paths — unify

A single on-screen element can be spoken by several independent subsystems, and they don't share one suppression/dedup authority. The tutorial-popup keyboard-hint work (v0.5.9) exposed this: the same TutorialBox message was reachable through the content-fingerprint monitor (`menus_monitors::MonitorPanelContents`), the single-row listbox monitor (`menus.cpp::OnListBoxSetActiveControl`), the panel-focus announce (`menus.cpp::AnnounceNewFocusedControl` → pending-announce drain, channel 0), and the chain-nav announce (`menus_monitors::AnnounceControl`). Substituting the hint required gating/overriding **each** path separately (fingerprint override, text-match suppression on the listbox row, the TutorialBox focus gate, hint substitution in AnnounceControl). It works, but it's fragile: a fifth path or a timing change can leak the old text, and each path has its own dedup channel (or none — `prism::Speak` doesn't dedup; only the channel-0/channel-1 `SpeakIfChanged` sites do). Worth a design pass to funnel all "announce this control's text" through one chokepoint with a single last-spoken dedup and a single substitution/suppression hook, so per-element overrides live in one place instead of N. Related to [[Investigate extracting shared panel-feature helpers]].

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
