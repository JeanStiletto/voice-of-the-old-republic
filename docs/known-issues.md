# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Unreproduced** — reported issues we can't reliably reproduce yet; need a repro before fixing.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

_None currently._

## Unreproduced

_None currently._

## Planned

### Beacon-active navigation announcements (remaining-route reading)

While a beacon (or autowalk) is active, the route announcements should describe the *remaining* way to the active waypoint — leading with the range and direction of the current target — rather than the full original route. The hard part is disambiguating intents: if the player selects another object just to hear where it is, the announcement must not balloon into the long, confusing full-route description for the beacon target. Design questions to resolve: how to keep "where is this thing I just selected" reads short while a beacon is running, and whether the separate Shift+Enter autowalk / Shift+`-` gestures are still needed at all, or whether they can be folded into / replaced by the plain selection + beacon flow. See `project_narrated_target_unified.md` and `project_map_cycle_architecture.md`.

### Nameable personal map pins

Let players give their own map pins a name when they drop one, and read that name back when cycling to the pin. Personal pins currently announce only a generic label plus position, so a player who marks several spots can't tell them apart. Needs a text-entry path that works from the keyboard with a screen reader (the editbox handling shipped for chargen + save naming — `menus_editbox.cpp` — is the closest existing mechanism, though map pins aren't an engine editbox so the entry surface differs), storage of the label alongside the pin in the save, and the cycle/focus announcements updated to speak it. See `project_narrated_target_unified.md` / `map_user_markers.cpp`.

### Bundle the resolution / widescreen patch — once we are out of beta

Offer the community resolution / widescreen + HR-menus patches as an installer option, and make our own code robust to the layout changes they introduce. Deliberately deferred until the mod leaves beta: it widens the support surface (several of our paths still assume a particular screen geometry — screen-pixel coordinates, the OS cursor, hardcoded hit-test offset compensations), and a beta is the wrong time to add a second variable to every bug report. Prerequisite when we do take it on: audit every place we depend on screen geometry and read it live from the engine instead of assuming. See `docs/installer.md` (Widescreen / HR-menus bundling) and the `project_radial_cursor_coupling` memory.

### KOTOR 2 controller support — all phases implemented, awaiting the first live round

KOTOR 2's Steam/Aspyr build ships working gamepad support, so unlike KOTOR 1 this needs no third-party mod. All six phases are now written and the patch builds clean, but **nothing has been through a live round yet** — this entry moves to Monitor once it has. What landed: one translation seam (`pad_input.cpp`) that normalises pad events into the mod's existing logical codes, closing the three defects that were ours rather than the engine's (pad A swallowed by our F1 suppression, unconsumed pad nav making menus announce several entries per press, pad B backing out silently); in-world D-Pad cycle bindings with LT/RT as a modifier layer read through XInput; the analog left-stick walk now arming the droid drive-loop silence; a spoken navigator for the Y-button Quick Menu (`pad_quickmenu.cpp`); suppression of the engine's own pad action menu in favour of ours (`pad_actionmenu.cpp`); and a Controller section in the F1 help that appears only when a pad is present. Combined test steps, the binding table, and the one remaining open question (which pad input opens the engine's action menu at the category level — the log now answers it) are in **`docs/kotor2-controller-plan.md`**; engine reference in `docs/llm-docs/k2-controller-support.md`.

Riskiest single piece to watch in that round: `pad_actionmenu.cpp` is the only code here that WRITES engine state (`-1` into the open-category global). Anything odd about the pad action menu should suspect it first; backing it out is a two-line change.

### Controller support (KOTOR 1) — once the community mod is out of beta

A gamepad is a genuine accessibility surface in its own right (fewer keys to memorise, no reliance on modifier chords), so once the community controller-support mod for KOTOR 1 leaves beta, evaluate bundling or interoperating with it. Open questions: whether its input path conflicts with our own DirectInput handling and hotkey polling, how our chain navigation and unified action menu map onto sticks and face buttons, and whether announcements need a separate cue vocabulary when the player has no keyboard in hand. The specific mod is not pinned down in this entry yet — fill in the name and source link before starting.

## Monitor

_None currently._

## Polish

### Starting an autowalk/beacon while one is active should switch, not cancel

If an autowalk or beacon is already running and the player triggers a new autowalk or beacon on a different target, the current behaviour just cancels the existing one. Instead it should immediately start the new action on the new target — switching the route in one gesture rather than requiring cancel-then-start.

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Fold in human translation contributions as they arrive

Most of the mod's non-German, non-English text is machine-drafted — the in-game strings for French, Italian, Spanish, Russian and Polish, and the installer's `Locales/{fr,it,es}.json`. They are correct in structure (key parity verified) but the phrasing, formal-vs-informal address, and modding terminology may read awkwardly to a native ear. German is human-authored by the maintainer and is the quality bar. As native speakers contribute corrections, merge them; PRs against the individual string files are a well-scoped contribution to point people at.
