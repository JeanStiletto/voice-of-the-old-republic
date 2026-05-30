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

### swconfig.exe silently reverts three of four `swkotor.ini` stability tweaks

`swconfig.exe` (KOTOR's launcher config tool) is reachable from Steam's "Configure" cog and is also auto-run by Steam on a language change or "Verify integrity of game files". Each run resets the Graphics Options section of `swkotor.ini` to vanilla defaults, including the three tweaks our installer relies on:

- `V-Sync=1` → `0` (vanilla default)
- `Frame Buffer=0` → `1`
- `FullScreen=0` → `1`

The fourth tweak, `Disable Vertex Buffer Objects=1`, survives because the key isn't part of swconfig's known schema, so it leaves it alone.

`FullScreen=1` is the operationally critical one: exclusive fullscreen wedges our entire cursor-warp pipeline. `MoveMouseToPosition` returns `mouseOverControl=NULL` for every chain step (no engine-side `SetActiveControl` fires from the warp), which silently breaks:

- Options panel tab activation (click-sim's `HandleLMouseDown` returns 0)
- Chargen class screen voice (icons 1..5 silent — `class_label` cache only fills via the `SetActiveControl` side effect)
- Equip-screen slot picker
- Workbench upgrade slot picker
- Any other panel using cursor-warp + click-sim

Diagnostic signature in `patch-*.log`: `Update: MoveMouseToPosition(x, y) target=... mouseOver before=<ptr> after=00000000` on every chain step. Confirm with `grep -nE "^V-Sync|^Frame Buffer|^FullScreen|^Disable Vertex" swkotor.ini` — if the first three deviate from `1/0/0` and the fourth is `1`, swconfig has hit.

Today's workaround: re-run `VoiceOfTheOldRepublicInstaller.exe` **as administrator** (it touches `Program Files (x86)`); the installer's `SwkotorIniTweaker.ApplyAccessibilityDefaults` is idempotent and puts all four tweaks back.

Memory: nothing yet — this is the first time we've traced it end-to-end.

Possible permanent fixes:

1. Re-apply `SwkotorIniTweaker` from inside the patcher on `OnRulesInit`, so every game launch corrects swconfig damage automatically (no admin needed since the patcher is already in-process).
2. Detect `FullScreen=1` at startup and either (a) speak a "fullscreen detected — please re-run installer" cue and refuse to apply the cursor-warp click-sim path, or (b) hot-rewrite the ini on next launch + restart instructions.
3. Investigate whether the cursor-warp regression is unique to `FullScreen=1` or also reproduces in `Disable Vertex Buffer Objects=0` / `Frame Buffer=1`; if so, broaden the check.

Option 1 is probably the right one — it keeps the install workflow drop-in and silently re-armors against the next swconfig run.

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

### Main menu occasionally still needs one alt-tab after launch

After replacing the wrapper-based DirectInput-mouse guard with an inline trampoline installed from `OnRulesInit`, the alt-tab-required-every-launch regression cleared. In a 3-launch sample one still showed the symptom — keys ignored until a single alt-tab cycle, then normal. No EngineInput log line, no crash, and the trampoline-installed log line was present, so the residual isn't our guard tripping; it looks like the same vanilla KOTOR background-launch / bink-window focus race that existed before the regression but is now visible against a clean baseline. Watch beta feedback. If it stays at ~1-in-3 or rarer, leave it; if testers report it consistently, instrument the engine's input-pump pause path around bink/focus transitions and consider forcing a DirectInput re-Acquire from our patch on main-menu first sight.

## Polish

### Clean up announcements when changing in-game panels

Switching between in-game panels (Equip, Inventory, Map, Journal, Options, Character) leaves stale announcements in flight — the previous panel's last row or title can finish speaking after the new panel is already focused. Need to flush or cancel pending narration on panel switch so the new panel's opening cue isn't drowned out.

### Installer fr/it/es translations are AI drafts

`installer/KotorAccessibilityInstaller/Locales/{fr,it,es}.json` were drafted by Claude from the English source rather than by native speakers. Strings render correctly and key parity with `en.json` is verified (136/136 keys), but specific phrasings — error messages, formal-vs-informal address ("vous" / "lei" / "usted"), and idiomatic phrasings around modding terminology — may read awkwardly to a native ear. Native-speaker contributors are welcome to refine; PRs against the three JSON files are scoped contributions. German (`de.json`) is human-authored by the maintainer and is the quality bar.

## Beta Preparations

### Enable GitHub Pages on the repo before public release

The installer's "Open the getting-started guide" checkbox opens `Config.ModSiteUrl` (`https://jeanstiletto.github.io/voice-of-the-old-republic/`). The repo is currently private and on a plan that doesn't allow Pages on private repos, so the URL 404s until we either flip the repo to public (preferred — this is open-source accessibility work and the installer is meant for public download) or upgrade to a paid plan. Flip Pages on at the same time we make the repo public: `gh api repos/JeanStiletto/voice-of-the-old-republic/pages -X POST -f 'source[branch]=main' -f 'source[path]=/'`. Source/branch already set up: `_config.yml` at the repo root, `README.md` carries `permalink: /` frontmatter.
