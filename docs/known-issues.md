# Known Issues

Status tracker for accessibility-mod work, in four buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### Resolution / fullscreen / menu-offset resilience (umbrella)

The mod's promise is "no mouse, no screen," yet several code paths silently assume a particular screen geometry and break when the user changes resolution, runs windowed, or installs a community widescreen / HR-menus patch that moves GUI layouts. Known coupling points so far: the radial cursor-ray dependency and the save-thumbnail `ImageScale` divide-by-zero (both fixed in v0.2.1 — see CHANGELOG; they're the first two examples of this class), in-world cursor parking, and any hardcoded hit-test offset compensations (e.g. the Options-panel tab-pitch shift). We deliberately avoided bundling the complex community resolution/menu patches for simplicity, but their changes — plus whatever resolution/windowed state a user lands in — must not be able to break core mod function. Needs: (a) an audit of every place we use screen-pixel coordinates, the OS cursor, or hardcoded offsets; (b) read geometry live from the engine instead of assuming; (c) installer-set sane graphics defaults; (d) a decision on auto-installing the widescreen + HR-menus patches and making our code robust to exactly the layout changes they introduce. See `installer.md` (Widescreen / HR-menus bundling) and the `project_radial_cursor_coupling` memory.

### Startup crash with non-NVDA screen readers (delay-load backend fault) — fix pending tester verification

A pl-PL beta tester (v0.2.1) crashed at startup, repeatedly, before any speech. Dump exception `0xC06D007F` = MSVC delay-load `ERROR_PROC_NOT_FOUND`: prism's `prism_registry_acquire_best()` walks every backend in priority order calling each `initialize()`; the ZDSR backend delay-loads the user's `ZDSRAPI.dll` (`C:\Program Files (x86)\zdsr\zdsr\`, confirmed via `kdev analyze-dump --modules`), which is present but exports a mismatched symbol set, raising an unhandled structured exception. NVDA/JAWS users never saw it because their backend wins priority and `acquire_best` returns before reaching the broken one — so it read as "non-NVDA readers crash." Fix (in `prism.cpp`): SEH-guard every backend probe (`acquire_best`, per-backend `acquire`, `initialize`); on an `acquire_best` fault, fall through `AcquireNormalFallback` which probes our preferred order one backend at a time, each guarded, skipping the broken one and landing on the next working backend (SAPI last as the universal safety net). Also covers the JAWS crash report (no log yet) — any backend whose vendor delay-load is incompatible is now skipped, not fatal. **Not yet verified in-game** — needs the ZDSR tester (and ideally the JAWS reporter) to confirm with a new build; can't repro locally without ZDSR installed. See the `project_prism_backend_delayload_crash` memory.

### Polish (and other) UI language defaults to German speech — fixed (now English)

Secondary finding from the same session log: `Lang: unknown LanguageID=5; defaulting to German`. LanguageID 5 is Polish; we had no mapping for it. Fixed: `DetectLanguageFromTlk` in `core_dllmain.cpp` now defaults to English (`L::En`) on every fallback path — unrecognised LanguageID, unreadable/bad `dialog.tlk`, and the path-resolution failures — since English is the most universal fallback for the non-DE user base. A correctly-installed DE copy still detects LanguageID=2 → German. Still tracks with the "Integrate a Polish translation" Planned item for proper PL labels.

### Character occasionally spins erratically in-world

The player character sometimes starts spinning around with no apparent trigger. Root cause unknown — no reliable repro yet. Likely candidates: a stale movement command we're re-queuing, an AI-action conflict with our autowalk path, or a camera-orient call leaking onto the creature. Needs a way to capture the offending tick (timestamped patch log + screen-reader narration of what happened just before).

### Menus lag noticeably on first open

The first open of a menu panel in a session has a perceptible delay; subsequent opens of the same panel are smooth. Likely first-touch cost of our menu wiring (string-table lookup, listbox enumeration, voice/SAPI warm-up). Profile a cold open against a warm one via `kdev logs` timestamps to identify the slow stage.

### Map-hint filter double-announces items

Items in the map-hint filter are announced twice on a single cycle step. Likely a duplicate fire — one from our cycle dispatcher and one from an engine-side re-narration path, or the unified Map-hint category folding waypoints + pins announces both rows for a single underlying object. See `project_map_cycle_architecture.md` for the category structure.

### Endless leveling bug

The character can keep levelling up with no cap enforced — the level-up flow can be entered/applied repeatedly past where it should stop. Needs a clean repro (class, current level, and entry path — Shift+L vs the engine's own level-up prompt) and a check of whether our level-up wiring (`engine_levelup.cpp`, `menus_powers_levelup.cpp`) is re-firing the apply, or the engine is being driven twice.

### Tutorial popups not reading correctly?

The in-game tutorial popups (`Tutorial Popups=1`) may not be read aloud correctly — text missing, partial, or not spoken at all. These are a distinct message-box type from normal dialogue; confirm what's spoken vs shown and capture a log on a known tutorial trigger.

### Droids sometimes don't read all dialogue options

In some droid conversations not every reply option is read while cycling the choices. Possibly a listbox enumeration miss or an options-changed re-read race. Note which droid / conversation; check the option enumeration in `dialog_speech.cpp`.

### Hacking/security dialogues — options read twice, plus general jank

Computer-spike / security "dialogues" read their options twice, and the interaction feels janky/slow. Likely the same double-read path as the Shift+Down item below (our description/option reader firing alongside an engine re-narration on the menu's option refresh). Confirm whether it's the engine re-firing or our reader, and whether the jank is a separate per-tick cost.

### Shift+Down reads descriptions twice (Journal, items)

Pressing Shift+Down to read a full description often reads it twice — seen in the Journal and on items. Duplicate fire between our Shift+arrow description reader and an engine re-narration, or a missing Consume so the key both drives our read and falls through. Same shape as the hacking double-read above; likely one root cause.

### Subtitle filter regressions (voiced-speaker suppression)

Two regressions from the voiced-speaker subtitle suppression change (CHANGELOG: "Untertitel vertonter Sprecher vorlesen"), both in `dialog_speech.cpp`'s speaker classification:
- **Zaalbar now has no subtitles read at all.** His Shyriiwook is untranslated alien speech the player can't otherwise follow and should still be read — the filter is over-suppressing him.
- **Some barks / ambient one-liner lines still have their subtitles read** over the audio, when the filter should already cover them (under-suppressing).
We already have the suppression code; it's mis-classifying these two cases. Tune the voiced-speaker vs read-anyway decision so genuinely-alien speech stays read while every voiced line (including barks) is suppressed.

## Planned

### Pazaak pre-game deck-build + wager menu

The board game itself is implemented (see Monitor). The pre-game `CSWGuiPazaakStart` screen — build your 10-card side deck from the owned-card collection and set the wager via `CSWGuiWagerPopup` — is still mouse-only. Needs keyboard selection across the owned-card grid and the chosen-deck slots, plus the wager less/more/accept controls. RE surface mapped in `docs/pazaak-investigation.md` §11.

### Turret-shooting minigame accessibility

The Ebon Hawk turret defense sequences (Leviathan, mid-game space encounters) need keyboard aim/fire and audio cues for incoming TIE direction, lock-on, hit/miss, and turret HP.

### Better combat reading

Iterate on combat narration: clearer turn/round structure, attacker → action → target framing, and a cleaner hit/miss/damage rollup that flows when several rolls fire on the same tick. Builds on the `AppendToMsgBuffer` funnel (`project_appendtomsgbuffer_is_combat_log.md`).

### Better combat ability handling

Improve the activation flow for combat abilities — force powers, feats, items — across selection, target picking, and dispatch. Today the bare 1–7 path (`project_bare_combat_keys_dispatch.md`) covers basics; we still need a clean affordance for variant cycling and multi-step abilities.

### Class-selection chargen screen

The "select your class" screen during character creation still needs accessible narration of the three class choices, their summaries, and confirm/back wiring.

### Class selection should step left/right, not up/down

On the class-selection chargen screen the three classes are a horizontal row, so navigation should be Left/Right between classes rather than Up/Down. Align the arrow binding with the spatial layout when wiring the narration above.

### Star map and Ebon Hawk travel menu

The galaxy map / travel-to-planet UI needs keyboard navigation across discovered worlds and announcements for the selected planet, its lore blurb, and the confirm/cancel actions.

### HP bars exposed to screen reader

Surface current/max HP for the player and party on a read-on-demand hotkey. Live HP data is already located (`project_creature_hp_lives_on_client_stats.md`).

### Party-member talents, feats, and powers inspection

Expose talents, feats, and force powers for the other party members. Today only the player creature is fully read out; companions' progression sheets are unreachable from the screen reader.

### Open-space room narration (shape, form, important exits)

Walltopo handles corridors and junctions well, but open / non-corridor rooms still narrate poorly. Need a clearer summary for plaza/hall-shaped spaces: rough shape, key exits, important landmarks. May need a different topology pass than the chain-merge corridor model (`project_walltopo_chain_merge_idea.md`).

Specifics to address:
- **Room-shape descriptions often fail in more open spaces** — shape detection produces nothing in plaza/hall geometry and the narration falls back to a generic "open space" line. Make shape detection succeed there.
- **Improve the announcement of open spaces** generally — more useful summary than the current fallback.
- **The shape announcement should replace the generic "open space" line** — once we can describe the shape, that description supersedes the generic fallback rather than sitting alongside it.

### Additional manual map hints

Hand-authored map hints for specific story locations the game doesn't mark but players struggle to find without sight. Backlog so far:
- Rebel corpses.
- The backdoor of the Santan (Sandral?) estate.
Add these to the manual map-hint registry (see `project_map_cycle_architecture.md` / `map_user_markers.cpp` for how user/manual hints fold into the map-hint cycle). Confirm the exact module and in-world position for each before adding.

### Integrate a Polish translation

Add Polish as a supported language. Decide the integration path — installer locale JSON (alongside de/en/fr/it/es) and/or in-game speech strings routed through the shared strings system — and wire it in. Source of the Polish strings (community contribution vs AI draft like fr/it/es) to be determined.

### F1 / Ctrl+F1 keyboard-help commands

Add an on-demand keyboard-help system. F1 reads the hints for the current screen — the keys that do something useful in the context the player is in right now (the action bar, a listbox menu, dialogue, a minigame, in-world navigation, etc.). Ctrl+F1 reads the general keyboard help — the mod's global hotkeys that work everywhere. This gives keyboard-only players a way to discover and re-check the controls without leaving the game or memorising a manual. Needs a per-context hint registry so each screen can supply its own F1 text, plus a single global list for Ctrl+F1.

### Alternative hotkeys for Alt/Ctrl-bound actions

Some of our actions are bound to Alt+ and Ctrl+ key combinations, which not every user can reach — some keyboards/layouts make those modifiers awkward or unavailable. Offer alternative (and ideally rebindable) bindings so these actions are reachable without the Alt/Ctrl chord.

## Monitor

### Turret minigame — cue re-anchored to the real fire line (winnable); tuning + auto-aim open

**Root cause found and fixed (2026-06-05).** The whole cue had been referenced to `CSWMiniPlayer +0x1c4` ("aim"), which is **decoupled from where bolts actually go** — proven in both auto and manual play (`boltVsAim` ~114°, manual kills landed with `+0x1c4` 21–130° off the fighter). The gun is **fixed** and the world rotates around it (`+0x240` orientation is a constant identity); bolts fire along a **near-fixed world line ≈ az −84°**. The fix re-anchors the cue to that line, **measured live** from the player's own bolts (`OnTurretAddBullet` hook → `DrainBoltProbes` EMAs `g_state.fire_line`). The cue now goes solid exactly when a bolt will connect — a tester won the mission with it.

Still open (next session, fresh context): **tune the cue feel** (convergence speed, ramp widths, "which way to rotate" directionality — it still "sounds a bit like before"), and **re-do aim-assist / Autoaiming for the fixed-gun model** (drive whatever rotates the WORLD to bring the locked fighter onto the fire line — `turret_steer.cpp`'s WASD barrel-turn is moot for a fixed gun — or decide auto-aim is unneeded since "point in the general direction and mash fire" already wins). Full write-up, offsets, and log channels (`TurretBolt`, `TurretCue`) in **`docs/turret-difficulty-investigation.md`**. The old turn-rate finding (`MovementPerSec = 100` @ `CSWMiniGame +0x74`) stands but was a red herring for difficulty.

**Update (2026-06-04): largely RESOLVED via aim-assist.** RE confirmed the hitbox is a real ~20 m sphere that can't be enlarged at runtime (model-scale and `sphere_radius` writes both no-ops). Measured raw aim-by-ear at ~0.8% hit rate — effectively unplayable, confirming the "luck only" report. Shipped aim-assist (writes `CSWMiniPlayer.aim +0x1c4`): DEFAULT always-on magnetism (pulls the gun onto the locked fighter once within 15°) + opt-in "Autoaiming" toggle (full lock-on). A within-session A/B proved magnetism ~14× the hit rate (ON 11.3% vs OFF 0.8%). Pending tester confirmation that the default feel is right.

### Main menu needs one alt-tab after launch — fix shipped (v0.3.2), watching

**Root cause found and fixed (2026-06-08).** Decompiled the input path: the engine acquires its DirectInput keyboard only on a `CExoInput::SetActive(1)` transition (driven by game-state events like `HideLoadScreen` via `FUN_005f6b10`, not purely by the window's `WM_ACTIVATE`), and `CExoRawInputInternal::GetKeyboardBuffer` returns zero input while `active == 0`. On cold start the final render window could be created without that activation reaching the live input object, leaving the keyboard dead until an alt-tab forced a real `SetActive(0)→SetActive(1)` cycle. Multi-launch logs then showed a second, dominant cause: the **Xbox Game Bar launch popup (and similar overlays) steal the foreground window** for several seconds right as the menu appears — and DirectInput (foreground cooperative level) can't `Acquire()` while a foreign window owns the foreground, so the steal can land up to ~6 s *after* the menu and kill input again.

Two-part fix: (1) `acc::engine::EnsureInputAcquired()` reads the engine's own `ExoInput` global and calls `CExoInput::SetActive(ExoInput, 1)` at MainMenu first-sight (idempotent — exactly what `HideLoadScreen` does), fixing the pure no-overlay case; (2) a **bounded cold-start foreground guard** in `diag_focus.cpp` — for 15 s after the menu, the 100 ms poll thread pulls the game's render window back to the foreground (via `AttachThreadInput` + `SetForegroundWindow`, no synthetic keypress) whenever a non-game window steals it, which makes the engine re-`Acquire` input on its own. Bounded by time (15 s) and reclaim count (6) so it never fights focus afterwards — Game Bar and deliberate alt-tabbing still work. Log lines: `EnsureInputAcquired: ... SetActive(... 1) dispatched` and `StartupForegroundGuard: ... reclaiming render ...`. Confirmed working by the developer across multiple launches; keep watching beta feedback on diverse machines before closing.

### Pazaak board game (keyboard play + narration)

Implemented in `patches/Accessibility/pazaak.cpp` — no engine detour hook: the live `CSWGuiPazaakGame` board is identified structurally (foreground modal matching the model layout; vtable learned + logged on first sight), state is polled per tick for delta announcements, and play is driven through the engine's own `HandlePlayHandCard`/`HandleStand`/`HandleContinue`. Builds clean; **pending in-game test.** Keys (board foreground only): Tab/Shift+Tab cycle the playable hand, Enter play, S stand, E end turn, C read hand, T read table; +/- cards open a sign sub-zone (Left/Right pick, Enter play, Esc cancel). Watch for: correct draw/play/stand/result lines and totals, the acquire log line (`Pazaak: acquired board panel=... vtable=0x...`), and that the engine's end-of-match result message box is dismissable from the keyboard. The default starting side deck has no +/- cards, so the sign sub-zone only appears once the player owns flip cards.

## Polish

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Clean up announcements when changing in-game panels

Switching between in-game panels (Equip, Inventory, Map, Journal, Options, Character) leaves stale announcements in flight — the previous panel's last row or title can finish speaking after the new panel is already focused. Need to flush or cancel pending narration on panel switch so the new panel's opening cue isn't drowned out.

### Installer fr/it/es translations are AI drafts

`installer/KotorAccessibilityInstaller/Locales/{fr,it,es}.json` were drafted by Claude from the English source rather than by native speakers. Strings render correctly and key parity with `en.json` is verified (136/136 keys), but specific phrasings — error messages, formal-vs-informal address ("vous" / "lei" / "usted"), and idiomatic phrasings around modding terminology — may read awkwardly to a native ear. Native-speaker contributors are welcome to refine; PRs against the three JSON files are scoped contributions. German (`de.json`) is human-authored by the maintainer and is the quality bar.

### Investigate extracting shared panel-feature helpers

Several screen modules repeat the same scaffolding — structural panel identification (foreground modal + vtable/field checks), SEH-guarded field readers, a snapshot/diff "announce on change" loop, and hotkey-`Consume`-then-poll gating. `pazaak.cpp` is the newest example; the menu/combat modules have older variants. Worth a pass to see how much can move into shared helpers without over-generalising (past refactors already pulled out the listbox/select-then-confirm and `menus_*` seams). Ready-to-paste prompt for a future session:

> Audit the panel/screen feature modules in `patches/Accessibility/` (e.g. `pazaak.cpp`, `menus_store.cpp`, `menus_*`, `combat*`, `examine_view.cpp`, `view_mode.cpp`) for repeated boilerplate: (a) structural panel identification, (b) SEH-guarded `ReadInt`/`ReadPtr`/field readers, (c) snapshot-and-diff "announce deltas" loops, (d) `hotkeys::Consume` + `Pressed` gating tied to a foreground panel. Propose a small set of shared helpers (header + cpp) that capture the common shapes WITHOUT forcing unrelated modules into one mould — respect the existing `menus_*` seams and the prior refactor boundaries. For each proposed helper: which call sites adopt it, what stays bespoke, and the risk. Don't refactor yet — produce the plan and one worked example (convert `pazaak.cpp`), then stop for review.
