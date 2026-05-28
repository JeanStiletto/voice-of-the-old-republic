# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.
- **Beta Preparations** — non-feature work that must land before a public beta.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### Character occasionally spins erratically in-world

The player character sometimes starts spinning around with no apparent trigger. Root cause unknown — no reliable repro yet. Likely candidates: a stale movement command we're re-queuing, an AI-action conflict with our autowalk path, or a camera-orient call leaking onto the creature. Needs a way to capture the offending tick (timestamped patch log + screen-reader narration of what happened just before).

### Menus lag noticeably on first open

The first open of a menu panel in a session has a perceptible delay; subsequent opens of the same panel are smooth. Likely first-touch cost of our menu wiring (string-table lookup, listbox enumeration, voice/SAPI warm-up). Profile a cold open against a warm one via `kdev logs` timestamps to identify the slow stage.

### Q/E cycle announces "kein Ziel" during cycling

While Q/E-cycling targets in-world, we announce "kein Ziel" / "no target" even though the engine state read appears correct. Suspect either a missing dedup against the previous tick, or we're firing the fallback announcement during a transient empty-list frame between the old target dropping and the new one resolving. Verify with `passive_narrate` log lines around the cycle.

### Map-hint filter double-announces items

Items in the map-hint filter are announced twice on a single cycle step. Likely a duplicate fire — one from our cycle dispatcher and one from an engine-side re-narration path, or the unified Map-hint category folding waypoints + pins announces both rows for a single underlying object. See `project_map_cycle_architecture.md` for the category structure.

### `engine_player::GetPartyMembers` reads party table from facade pointer

The function at `patches/Accessibility/engine_player.cpp:368` walks `AppManager → serverApp + kServerExoAppPartyTableOffset (0x1b770)`, where `serverApp` is the public 8-byte vtable+internal facade. The party table actually lives on the *internal* — `serverApp+4 → internal + 0x1b770`. The header at `engine_player.h:160-170` documents the bug pattern explicitly ("Earlier walks read from facade+0x1b770 — wrong; returned random heap (all 1s)") and even introduced `kServerExoAppPartyTableOffset` as a legacy alias of `kServerInternalPartyTableOffset` to flag remaining wrong-base callers. `GetPartyMembers` is the last surviving wrong-base caller.

Three consumers (`combat_queue`, `combat_special_watch`, `party_cache`) currently rely on this read. They've apparently been working acceptably in the field — either through compensating logic or because the wrong-memory results happen to be benign — so the bug is latent rather than user-visible.

Memory: `project_cserverexoapp_facade_split.md`.

Fix: redirect `GetPartyMembers` to `serverInternal + 0x1b770` (same chain `GetServerPartyTable` at `engine_player.cpp:398` already uses), then verify the three consumers still behave correctly. Test in any scene with companions (Taris apartment after recruiting Carth + Mission + Zaalbar is the canonical multi-party scene).

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

### Star map and Ebon Hawk travel menu

The galaxy map / travel-to-planet UI needs keyboard navigation across discovered worlds and announcements for the selected planet, its lore blurb, and the confirm/cancel actions.

### HP bars exposed to screen reader

Surface current/max HP for the player and party on a read-on-demand hotkey. Live HP data is already located (`project_creature_hp_lives_on_client_stats.md`).

### Party-member talents, feats, and powers inspection

Expose talents, feats, and force powers for the other party members. Today only the player creature is fully read out; companions' progression sheets are unreachable from the screen reader.

### Open-space room narration (shape, form, important exits)

Walltopo handles corridors and junctions well, but open / non-corridor rooms still narrate poorly. Need a clearer summary for plaza/hall-shaped spaces: rough shape, key exits, important landmarks. May need a different topology pass than the chain-merge corridor model (`project_walltopo_chain_merge_idea.md`).

## Monitor

## Polish

### Clean up announcements when changing in-game panels

Switching between in-game panels (Equip, Inventory, Map, Journal, Options, Character) leaves stale announcements in flight — the previous panel's last row or title can finish speaking after the new panel is already focused. Need to flush or cancel pending narration on panel switch so the new panel's opening cue isn't drowned out.

## Beta Preparations

### Enable GitHub Pages on the repo before public release

The installer's "Open the getting-started guide" checkbox opens `Config.ModSiteUrl` (`https://jeanstiletto.github.io/kotor-blind-accessibility/`). The repo is currently private and on a plan that doesn't allow Pages on private repos, so the URL 404s until we either flip the repo to public (preferred — this is open-source accessibility work and the installer is meant for public download) or upgrade to a paid plan. Flip Pages on at the same time we make the repo public: `gh api repos/JeanStiletto/kotor-blind-accessibility/pages -X POST -f 'source[branch]=main' -f 'source[path]=/'`. Source/branch already set up: `_config.yml` at the repo root, `README.md` carries `permalink: /` frontmatter.
