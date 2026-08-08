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

## Monitor

### KOTOR 2 does not refuse impossible actions — we announce their absence instead

A medkit used on a full-health character behaves differently in the two engines, and only one of them tells the player anything.

KOTOR 1 refuses it at the GUI layer: `DoPersonalAction` finds the entry's usable bit clear, plays its error sound, and never dispatches. The entry's flag word carries a reason code, so we speak the engine's own sentence — logged as `ActionBar: variant flags ... usable=0 reason=5` followed by `refused — speaking engine reason strref=0xa602 [Volle Gesundheit]`.

KOTOR 2 does not. The same item at full health reads `usable=1 reason=0`, so the engine dispatches into a handler that silently does nothing: no queue add, no feedback line, no sound. Sighted players get nothing either. Our answer is `combat_queue`'s no-op watch — a user fire that produces no `AddAction` inside the attribution window speaks the generic "Nicht möglich".

**The open question this entry exists for:** whether KOTOR 2 genuinely permits the action, or whether the personal-action list we read is stale. `usable=1 reason=0` is consistent with both — a list populated while the character was damaged would read the same. The circumstantial evidence favours "genuinely permits" (the error sound has never been heard on KOTOR 2, including in sessions where the character was at full health from the start, so the list can only have been baked at full health), but that is an argument, not proof.

**The test that would settle it:** make an entry unusable on KOTOR 2 for a reason *other* than health — a Force power with too few Force points, or an item down to zero charges — and fire it from the action menu. Any `usable=0` line with a non-zero reason means the mechanism is alive there and only the health verdict is missing, which would be worth chasing into K2's populate path. If nothing on KOTOR 2 ever produces `usable=0`, the flag is never computed in that build and the watch is the correct permanent answer.

**Also unverified, and the reason to watch rather than close this:** the false-positive side of the watch. A successful use must speak "…, Platz N" and *not* the generic line, and the KOTOR 2 Combat Behaviour column (key 8) may legitimately queue nothing — if it does, it will wrongly announce "Nicht möglich" and that column needs excluding. Neither case has been through a round.

Code: `engine_actionbar.cpp` (`VariantRefusal`, the always-on flags log), `combat_queue.cpp` (`ArmUserQueueAdd` / `TickNoOpWatch`), `unified_action_menu.cpp` (`SpeakRefusal`).

## Polish

### Starting an autowalk/beacon while one is active should switch, not cancel

If an autowalk or beacon is already running and the player triggers a new autowalk or beacon on a different target, the current behaviour just cancels the existing one. Instead it should immediately start the new action on the new target — switching the route in one gesture rather than requiring cancel-then-start.

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Fold in human translation contributions as they arrive

Most of the mod's non-German, non-English text is machine-drafted — the in-game strings for French, Italian, Spanish, Russian and Polish, and the installer's `Locales/{fr,it,es}.json`. They are correct in structure (key parity verified) but the phrasing, formal-vs-informal address, and modding terminology may read awkwardly to a native ear. German is human-authored by the maintainer and is the quality bar. As native speakers contribute corrections, merge them; PRs against the individual string files are a well-scoped contribution to point people at.
