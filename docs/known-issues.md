# Known Issues

Status tracker for accessibility-mod work, in four buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### Character occasionally spins erratically in-world

The player character sometimes starts spinning around with no apparent trigger. Root cause unknown — no reliable repro yet. Likely candidates: a stale movement command we're re-queuing, an AI-action conflict with our autowalk path, or a camera-orient call leaking onto the creature. Needs a way to capture the offending tick (timestamped patch log + screen-reader narration of what happened just before).

### Menus lag noticeably on first open

The first open of a menu panel in a session has a perceptible delay; subsequent opens of the same panel are smooth. Likely first-touch cost of our menu wiring (string-table lookup, listbox enumeration, voice/SAPI warm-up). Profile a cold open against a warm one via `kdev logs` timestamps to identify the slow stage.

### Map-hint filter double-announces items

Items in the map-hint filter are announced twice on a single cycle step. Likely a duplicate fire — one from our cycle dispatcher and one from an engine-side re-narration path, or the unified Map-hint category folding waypoints + pins announces both rows for a single underlying object. See `project_map_cycle_architecture.md` for the category structure.

## Planned

### Pazaak minigame accessibility

The card game is fully mouse-driven and unreadable. Needs keyboard navigation across the hand and side deck, and announcements for running totals, opponent state, stand/end-turn, and round outcomes.

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

### Integrate a Polish translation

Add Polish as a supported language. Decide the integration path — installer locale JSON (alongside de/en/fr/it/es) and/or in-game speech strings routed through the shared strings system — and wire it in. Source of the Polish strings (community contribution vs AI draft like fr/it/es) to be determined.

### Alternative hotkeys for Alt/Ctrl-bound actions

Some of our actions are bound to Alt+ and Ctrl+ key combinations, which not every user can reach — some keyboards/layouts make those modifiers awkward or unavailable. Offer alternative (and ideally rebindable) bindings so these actions are reachable without the Alt/Ctrl chord.

## Monitor

### Main menu occasionally still needs one alt-tab after launch

After replacing the wrapper-based DirectInput-mouse guard with an inline trampoline installed from `OnRulesInit`, the alt-tab-required-every-launch regression cleared. In a 3-launch sample one still showed the symptom — keys ignored until a single alt-tab cycle, then normal. No EngineInput log line, no crash, and the trampoline-installed log line was present, so the residual isn't our guard tripping; it looks like the same vanilla KOTOR background-launch / bink-window focus race that existed before the regression but is now visible against a clean baseline. Watch beta feedback. If it stays at ~1-in-3 or rarer, leave it; if testers report it consistently, instrument the engine's input-pump pause path around bink/focus transitions and consider forcing a DirectInput re-Acquire from our patch on main-menu first sight.

## Polish

### Clean up announcements when changing in-game panels

Switching between in-game panels (Equip, Inventory, Map, Journal, Options, Character) leaves stale announcements in flight — the previous panel's last row or title can finish speaking after the new panel is already focused. Need to flush or cancel pending narration on panel switch so the new panel's opening cue isn't drowned out.

### Installer fr/it/es translations are AI drafts

`installer/KotorAccessibilityInstaller/Locales/{fr,it,es}.json` were drafted by Claude from the English source rather than by native speakers. Strings render correctly and key parity with `en.json` is verified (136/136 keys), but specific phrasings — error messages, formal-vs-informal address ("vous" / "lei" / "usted"), and idiomatic phrasings around modding terminology — may read awkwardly to a native ear. Native-speaker contributors are welcome to refine; PRs against the three JSON files are scoped contributions. German (`de.json`) is human-authored by the maintainer and is the quality bar.
