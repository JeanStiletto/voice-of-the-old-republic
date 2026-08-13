# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Unreproduced** — reported issues we can't reliably reproduce yet; need a repro before fixing.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### KOTOR 2 lab station: nothing can actually be crafted

On KOTOR 2's "Neue Gegenstände erzeugen" screen (the lab station / workbench
crafting panel, `PanelKind::WorkbenchCreateItem` / `WorkbenchCreateMedical`),
pressing Enter on a recipe row does nothing, and a subsequent press on "Objekt
erstellen" does nothing either. The screen reads correctly — titles, rows, the
create/breakdown view flip — but no item can be made from the keyboard.
Reported 2026-08-13 during the pre-0.7.2 audit.

**The screen had never been exercised.** It was written offline in the K2
crafting batch and no log had ever contained it: every earlier workbench
session shows only panel kinds 42 (`Upgrade-Werkbank`) and 44 (`Aufwerten:
<item>`). The first sighting of kind 45 anywhere is
`patch-20260813-113119.log`, the session that produced this report.

**What the log establishes.** `menus_crafting.cpp`'s row-commit fires on the
Enter, finds the right control, and delivers the activate — `row-commit
panel=... btn(id=12)=... fired=1`, twice. So `BTN_Accept` exists on the panel
and the event reaches it. The engine simply declines to act on it.

**Working hypothesis (not yet verified).** `DispatchRowCommit` writes the
listbox selection index and then fires `BTN_Accept` directly, never running the
recipe row's own activate handler — so the engine's notion of which recipe is
selected is never set, and Accept has nothing to build. The user's own press on
"Objekt erstellen" fails for the same reason. This is the shape workbench round
3 already found on K2: the real work lives in the row's own activate handler
(`OnUpgradeSelected`), not in a separate commit button. The same
write-index-then-fire-button recipe works for `BTN_UPGRADEITEMS` because that
button only has to open a panel for whatever row is selected, not build
anything.

If that holds, the fix also carries a UX decision: Enter on a recipe row should
*select* the recipe, leaving "Objekt erstellen" as a separate press, rather than
today's attempt to build immediately on the row press.

Needs its own session, starting with one offline Ghidra round on K2's
`component_p` / `chemical_p` panel class — find the row activate handler and
what state `BTN_Accept` reads. See the entry under WHERE TO RESUME in
`docs/kotor2-port.md`.

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

KOTOR 2 mostly cannot: the answer to this entry's original open question (decompiled 2026-08-12, the dead-Medicine-row investigation) is that K2's item appender **never initialises the flag word for inventory items** — only the three equipped-slot entries get one. What we read there is heap garbage (ASCII string fragments in the logs), the engine gates on the same garbage, and `VariantRefusal` therefore skips the refusal verdict for K2 item entries entirely (a garbage bit would fake a refusal with a random reason). Force/feat entries DO compute real flags on K2 — "Macht erschöpft" (`usable=0 reason=1`) was observed live — so the mechanism exists, it is just never fed for items. The no-op watch stays as the honest answer for silent item refusals.

**The K2 Medicine row needed a deeper fix (2026-08-12).** K2 gives every medical-category item a TWO-STEP use flow: the press arms a target-pick (MainInterface `+0x15230` armed / `+0x15234` party slot) that only a mouse click on a party portrait completes — and `DoPersonalAction`'s preamble wipes the state before the medical handler reads it, so from the keyboard the row can never fire at all (one press wedges it armed-with-no-slot; the 2026-08-11 21:58 "flip" in this playthrough was simply the first medical press whose base-item row byte routes into the two-step flow). The shipped answer bypasses that machinery: `SendUseItemRequestK2` emits the same client→server use-item message the portrait click's consume path emits, and the unified menu adds a party-member picker (narrated member preselected, Shift+Enter = instant self, bare 5 = instant self). The `Diag.UseItemReq` hook on the message writer doubles as the no-op watch's cancel signal: a sent request proves the press acted, which also fixed mines announcing "Nicht möglich" while visibly planting (they act without a combat-round add).

**One false-positive class found and fixed (2026-08-08).** The watch was armed for every menu fire, and target-row actions tripped it three for three on a footlocker: Sicherheit, Sicherheits-Überbrücker, and a Schallmine that visibly blew the lock open all announced "Nicht möglich" while working perfectly (`patch-20260808-103232.log`). Target rows act without a combat-round add as a matter of course, so the watch is now armed only for the personal block — `ArmNoOpWatch`, called from the menu's personal path and from bare 4..7, never from `ArmUserQueueAdd` itself.

**Still watching:** KOTOR 2's Combat Behaviour column (key 8) may legitimately queue nothing — if it does it will wrongly announce "Nicht möglich" and needs the same send-witness treatment. And a server-side drop of a direct-sent medical item (e.g. full health) is silent-then-generic rather than reasoned — the engine's client-side CanUseItem gate (`0x005D2F80`) is skipped by the direct send and could later supply real reasons.

Code: `engine_actionbar.cpp` (`VariantRefusal`, `SendUseItemRequestK2`, the always-on flags log), `combat_queue.cpp` (`ArmUserQueueAdd` / `TickNoOpWatch`), `combat_diag.cpp` (`OnUseItemRequestK2`), `unified_action_menu.cpp` (`SpeakRefusal`, the medical party picker).

## Polish

### Starting an autowalk/beacon while one is active should switch, not cancel

If an autowalk or beacon is already running and the player triggers a new autowalk or beacon on a different target, the current behaviour just cancels the existing one. Instead it should immediately start the new action on the new target — switching the route in one gesture rather than requiring cancel-then-start.

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Fold in human translation contributions as they arrive

Most of the mod's non-German, non-English text is machine-drafted — the in-game strings for French, Italian, Spanish, Russian and Polish, and the installer's `Locales/{fr,it,es}.json`. They are correct in structure (key parity verified) but the phrasing, formal-vs-informal address, and modding terminology may read awkwardly to a native ear. German is human-authored by the maintainer and is the quality bar. As native speakers contribute corrections, merge them; PRs against the individual string files are a well-scoped contribution to point people at.
