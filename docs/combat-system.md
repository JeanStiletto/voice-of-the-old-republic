# Combat System — Investigation

A discovery doc for the combat-accessibility pillar. Same format as
`docs/llm-docs/accessibility-map.md`:

- **known** — verified live (we have hooked it OR confirmed offsets via DumpBytes)
- **suspected** — DB / SARIF / swkotor.exe.h says it exists and the type fits, but unvalidated
- **open** — we know we need this; no candidate yet

Status as of 2026-05-10: **Phases 1–4 skeleton implemented and live-tested
across 3 in-game iterations.** Combat-mode entry/exit, combat-log narration,
attack resolutions, dialog speech, queue submenu, PC stats, and Examine
hotkey all function. Several plan-doc assumptions were corrected against
runtime behaviour — see the "Implementation status & validated findings"
section immediately below for the running diff between this plan and what
the code actually ships. Sources used to build the original plan:
`third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db`,
`docs/llm-docs/re/swkotor.exe.h` (Lane's Ghidra-exported types), Lane's
SARIF excerpts, and `docs/llm-docs/re/k1_win_gog_swkotor.exe.xml`
(Lane's full symbol/function table — most useful single source for
verifying suspected addresses).

The four investigation tracks the user asked for map cleanly to four pillars:

1. **Char-state pillar** — properties shown for player + opponent during combat
2. **Combat-feedback pillar** — damage numbers, hit/miss, action names, floaty text
3. **Action-queue pillar** — the per-character pending-actions strip the sighted player edits during pause
4. **Round/mechanics pillar** — turn structure, attack resolution, round timer, save throws, death

Plus an **adjacent** section on the in-game dialog screen — same listbox-spec
pattern as the combat-log review-on-demand path, ships in the same Phase 1.

A **Plan** section at the bottom captures the phased implementation approach.

---

## Implementation status & validated findings

This section reconciles the original plan against three iterations of
in-game testing. Subsequent sections (the per-pillar maps + the Plan)
remain as originally written — they are the design intent. Read THIS
section first for what's been live-tested, what was wrong in the plan,
and what's still skeleton.

### Per-phase implementation status

**Phase 1A — combat-mode entry/exit.** Working. Implemented as a per-tick
poll of `CClientExoApp::GetCombatMode @0x5ede70` (no hook needed) with the
stability-debounce pattern from `turn_announce.cpp`. Speaks
`Kampf beginnt` / `Kampf beendet` cleanly across rapid combat-mode
oscillation. TU: `combat.cpp::TickCombatMode`.

**Phase 1B — live combat-log narration.** Working via hook on
`CGuiInGame::AppendToMsgBuffer @0x0062b5c0` (TU: `combat.cpp::OnAppendToMsgBuffer`,
registered in `hooks.toml`). Every engine-emitted combat-feedback string
flows through this single funnel — attack lines with attack name + roll
+ defense math + damage, three-line stat breakdowns (Angriffsstatistik /
Abwehrstatistik / Schadensstatistik), specials (Schleichangriff, Starke
Explosion), XP awards, loot pickups, enemy ability calls, world text
(locked doors, etc.). Handler reads the `CExoString*` argument and pushes
the c_string straight to TTS. Honours vanilla feedback-verbosity options
for free (the engine populates the buffer per its own options).

**Investigation diff — what the original plan got wrong on this phase.**
The original plan targeted `CSWGuiInGameMessages::AddMessages @0x626920`
and started with a poll on `messages_listbox @+0x64`. Both were on the
wrong layer:

- `messages_listbox` is filled **lazily** when the review screen mounts.
  Verified live in `patch-20260521-093926.log` and `patch-20260521-095251.log`:
  during a complete in-game fight the listbox stays at 0 rows; on review-
  screen open all 64 rows arrive in a single tick burst. The listbox is
  a display-time rebuild from a separate backing store, not the live
  source of truth.
- `AddMessages` has only ONE caller (per SARIF xref): `ShowDialogEntry`
  on the dialog path. Hooking it produced zero fires across a full
  combat in `patch-20260521-095251.log`. AddMessages populates the
  listbox at review-mount time, not during live combat.
- The real live-combat-log surface is `CGuiInGame::AppendToMsgBuffer`,
  named (correctly) in the plan's "Visible-to-sighted mirror channel"
  recommendation but not implemented until 2026-05-21. The function
  writes each appended string into a 64-slot, 16-byte-stride ring
  buffer at `this[+0xF8]` with write index at `this[+0x100]`. The
  Messages review panel rebuilds `messages_listbox` from this ring on
  mount — explaining the 0→64 burst we observed.

The poll on `messages_listbox` remains in `TickCombatLog` as a log-only
sanity check on review-screen contents (speech is OFF; live narration
flows from the hook). The find-path was also corrected: it now resolves
the panel via `CGuiInGame.in_game_messages @+0x1c` instead of walking
`CSWGuiManager.panels[]` (the latter only finds the panel when the
review screen is mounted).

**Phase 1C — Messages-panel review-on-demand.** Working. Single
`ListBoxPanelSpec` entry in `menus_listbox.cpp` for `InGameMessages`
routing to `messages_listbox`. The dialog_listbox toggle (engine
show_button) is **not yet implemented** — the spec routes only to
messages_listbox.

**Phase 1D — live dialog screen.** Working. Per-tick poll of dialog panels
in `dialog_speech.cpp` for NPC line / reply count / bark text changes,
plus four `ListBoxPanelSpec` entries (DialogCinematic /
DialogCinematicCopy / DialogComputer / DialogComputerCamera) routing to
`replies_listbox @+0x19c4`. Reply-row spec adds `(unavailable)` suffix
for inactive rows via `is_active @+0x4c` read.

**Phase 2A — selected-PC stat block (Shift+S).** Working in user-triggered
path. The `TickLeaderChangeAutoAnnounce` was originally meant to
auto-fire the full stat block on Tab; it now only speaks the leader name
because the suspected engine accessors used in `SpeakSelectedPcStatBlock`
(GetMaxHitPoints, GetArmorClass, save accessors) had not been live-
validated and a wrong-address call could __fastfail uncatchably. Manual
Shift+S still calls them — user accepts the risk per session.

**Phase 2B — opponent cycle-announcement enrichment.** Working. Hooked
into `passive_narrate.cpp` which calls `combat_query::BuildTargetCombatBrief`
when the cycle target is a Creature. Skeleton output:
`<name>, neutral, <hp_cur> Lebenspunkte`. The faction word is
hardcoded "neutral" pending CSWSFaction decode (open). HP-current via
direct field read at `CSWSObject.hit_points @+0xe0`; AC and HP-max
stripped from the format until those engine accessors are validated.

**Phase 2C — Shift+H Examine.** Working in a redesigned shape. Original
plan called for driving `CGuiInGame::ShowExamineBox @0x62d3e0` and
reading the populated panel. **The plan was wrong on two counts:**
(1) `ShowExamineBox` is `void(ulong, int)` with `BYTES_PURGED=8`, not
the single-arg signature the plan implied — calling with 1 arg created
stack imbalance and likely contributed to the 2026-05-09 crash;
(2) the panel is **server-driven** via 5 sister functions
`SendServerToPlayerExamineGui_{Creature,Item,Door,Placeable,Mine}Data`
(0x56ebe0..0x56f370) — `ShowExamineBox` only opens the slot, server
must push the data. Working implementation in `combat_query.cpp::HotkeyShiftH`
skips ShowExamineBox entirely and reads stats directly via the same
helpers Phase 2A uses, then speaks `<name>, neutral, <hp_cur> hp`.
Verified live 2026-05-10 against `Sith-Soldat` (kind=5) and
`Überreste` (kind=9 placeable corpse).

**Phase 3A — action queue submenu (Shift+K).** Working. Pattern lifted
from `actionbar_menu.cpp`. Walks `combat_round.actions` linked list,
filters out the leading `type=0xFF` placeholder entry the engine
maintains (the "currently dispatching action" slot, target=0 when idle).
Up/Down nav works; Enter removes tail entries (non-tail still returns
"Aktion kann nicht entfernt werden" pending positional-remove
primitive); Shift+Enter clears all via repeat-RemoveLast loop;
Esc closes. **Action-type byte enum mapping is currently disabled** —
the inferred values were wrong (type=1 mapped to SpellCast was actually
a basic attack); all entries render as generic "Aktion" until probed.

**Phase 4A — per-attack callout.** Implemented as a per-tick poll of the
player creature's `combat_round.attacks_list[7]`, edge-detected on
(target, result, baseDamage) tuple change. **Speech intentionally OFF as
of 2026-05-21** — the Phase 1B AppendToMsgBuffer hook now delivers the
engine's own much richer combat-log text (attack name, full to-hit math,
defense math, damage breakdown) for every attack. The synthesized
`SpeakAttackOutcome` line ("X trifft Y für N Schaden") is strictly less
informative and was duplicating every attack. The log entry
(`Combat.Attack ... (silent)`) is preserved as a cross-check; the slot
walk + edge detection is still useful as a per-attack signal for future
features (combat sound cues, attack-bucket statistics, etc.). Plan was
wrong about the edge condition (`Pending(0) → Resolved` only) — the
engine reuses slots without going back through Pending. Tuple-change
with damage-settle gate (skip when baseDamage == -1, the engine's "not
yet computed" sentinel) is the working pattern.

**Phase 4B — saving-throw callout.** Skeleton no-op. Real signal needs
a hook on `SavingThrowRoll @0x5b92b0` or `BroadcastSavingThrowData
@0x4ec760`; polling for save-field changes is too noisy.

### Validated engine surfaces (anchor table)

Confirmed live during iteration testing. All addresses verified against
`docs/llm-docs/re/k1_win_gog_swkotor.exe.xml` — Lane's symbol table is
high-confidence and addresses paste straight into our hooks/code.

- **`CClientExoApp::GetCombatMode @0x005ede70`** — `int __thiscall(void)`.
  Returns 0 = peace, !=0 = combat. Used in `combat::TickCombatMode` and
  as the auto-firing-path gate in `combat::TickAttackResolutions`.
- **`CClientExoApp::GetObjectName @0x005ed350`** — `int __thiscall(ulong handle, CExoString* outName)`,
  `BYTES_PURGED=8`. Universal localized-name accessor; resolves the full
  template/appearance/racial-type chain so generic enemies whose
  `first_name` strref is empty still get a proper display name (e.g.
  "Sith-Soldat" instead of the modder tag "end_cut2_sith2"). **Caveat:
  resolves only client-side handles** (high bit `0x80000000` set). For
  server-side handles, `acc::engine::GetObjectDisplayNameByHandle`
  retries with `handle | 0x80000000` per
  `memory:project_object_handle_namespaces`.
- **`CSWSObject.hit_points @+0xe0` (short)** — current HP. Direct read
  is safe and used by every auto-firing read path (Phase 4A,
  Phase 2B brief, Phase 2C examine).
- **`CSWSCreature.combat_round @+0x9c8` → `CSWSCombatRound*`** — round
  state; null when no round is active. Used by Phase 3A queue and
  Phase 4A attacks.
- **`CSWSCombatRound.actions @+0x9b0` → `CExoLinkedList*`** — confirmed
  walkable as `{head, tail, count}` followed by `{next, prev, data}`
  nodes. The engine maintains a leading sentinel entry with
  `action_type=0xFF, target=0` representing the "currently dispatching"
  slot — must be filtered when presenting the queue to the user.
- **`CSWSCombatAttackData` per slot @ `combat_round + 0x4 + slot*0xc0`,
  7 fixed slots.** Field offsets `react_object @+0xc`, `base_damage @+0x38` (short),
  `attack_deflected @+0x54` (int), `attack_result @+0x5c` (int),
  `critical_threat @+0x50` (int) all live-verified.
- **`CGuiInGame::ShowExamineBox @0x0062d3e0`** — `void __thiscall(ulong handle, int param_2)`,
  `BYTES_PURGED=8`. **DO NOT CALL DIRECTLY** in the skeleton: panel
  population is server-driven (5 sister `SendServerToPlayerExamineGui_*Data`
  functions @ `0x0056ebe0..0x0056f370`) and `param_2`'s purpose isn't
  documented. Calling with the wrong arg count produces stack imbalance.
- **`CSWGuiExamine` panel layout** (validated via panel-walk dump):
  `controls.size = 6`. Children: `[1] id=1 listbox` (vtable=0x0073E840 =
  `CSWGuiListBox_vtable` — actual stat-block content lives here as
  rows), `[3] id=3 button "Schliess."`, `[4] id=4 button "Abbrechen"`,
  `[5] id=-1 label` (vtable=0x0073E5B8 = `CSWGuiLabel_vtable`,
  contents vary). The skeleton picks the listbox by vtable match
  rather than by id, then concatenates rows.
- **`CGuiInGame::AppendToMsgBuffer @0x0062b5c0`** —
  `void __thiscall(CExoString* msg)`. **THE live combat-log row append.**
  Every feedback string the engine emits (attack, stats, specials, XP,
  loot, world text) flows through this function. Body writes into a
  64-slot, 16-byte-stride ring buffer at `this[+0xF8]` with write index
  at `this[+0x100]`. Hook is wired in `hooks.toml` (entry-point detour,
  7-byte cut: PUSH EDI + MOV EDI,ECX + MOV ECX,[ESP+8]).
- **`CGuiInGame.in_game_messages @+0x1c`** — stable pointer to the
  CSWGuiInGameMessages review panel, allocated for the lifetime of the
  game session. Resolving via this slot beats the older
  `CSWGuiManager.panels[]` walk, which only finds the panel while the
  review screen is mounted.
- **`CSWGuiInGameMessages.messages_listbox @+0x64`** — combat log feed.
  Row count diff per tick; new rows go to TTS via `menus_extract::FromControl`.
- **`CSWGuiDialog.replies_listbox @+0x19c4`** + **`message_label @+0x1ca4`** —
  dialog panel offsets confirmed working across Cinematic / CinematicCopy /
  Computer / ComputerCamera variants (all share base layout).

### Validated enum values

- **`CSWSCombatAttackData.attack_result`**:
  - `-1` = unused slot (engine sentinel; verified 2026-05-09)
  - `1` = Hit (verified 2026-05-10 with positive damage 1, 5)
  - `2` = Miss (verified 2026-05-10 — first anchor)
  - `3` = Critical (still unconfirmed but likely; the inferred guess
    in `engine_offsets.h::kAttackResultCrit`)
  - `4` = Deflected / parried (verified 2026-05-10 in
    `test wird von end_cut2_sith2 pariert.` callouts)
- **`CSWSCombatAttackData.base_damage`**:
  - `-1` = "not yet computed" sentinel; the engine sometimes publishes
    `attack_result` before damage settles. Phase 4A skips the Hit/Crit
    speak when damage is -1 and waits for the next-tick value.
  - Hit and Miss have different conventions: a Miss carries `dmg=-1` as
    the steady value (no damage on miss); a Hit carries `dmg=-1` only
    transiently before it lands on the rolled positive value.
- **`CSWSCombatRoundAction.action_type`**:
  - `0xFF` (255) = "currently dispatching action" placeholder entry
    the engine maintains as the queue head when nothing is mid-dispatch;
    must be filtered.
  - `1` = NOT SpellCast (the originally inferred value). Real value
    pending probe — observed in tutorial-level Trask attacks (creature
    with no Force powers). Likely a flavour of Attack.
  - `11` = also unmapped; observed pointing at Sith creature handles.
    Likely another Attack flavour.

### Auto-firing path safety rules

Learned the hard way during the 2026-05-09 crash investigation
(STATUS_STACK_BUFFER_OVERRUN, /GS canary fastfail, uncatchable by SEH):

1. **Never call a "suspected" engine accessor address from an
   auto-firing path** (per-tick poll, hotkey routed automatically on
   game state changes). A wrong address may not crash on the call
   itself, but can corrupt the stack canary leading to fastfail on
   epilogue elsewhere. SEH does not catch /GS fastfail. Restrict
   suspected accessors to user-pressed hotkeys where the user gets
   immediate feedback if it fails.
2. **Always gate combat-data reads on `GetCombatMode != 0`** before
   touching `attacks_list[7]`. Outside combat the array contains
   uninitialized heap bytes; reading and acting on them led to the
   2026-05-09 false-fire (`test wird von ? pariert.` against a garbage
   handle on the very first tick of area-load).
3. **First-observation suppression per tracked slot** — adopt the
   first-seen value as baseline silently, never as a "transition to
   speak". Without this, leftover state from a prior session reads as
   a spurious edge.
4. **Validate target before speaking** — the resolved object pointer
   must be non-null AND the name must resolve to non-empty text.
   Speaking "X attacks ?" against a wild handle is a clear smell that
   we're misinterpreting state.

### Skeleton gaps still open

Tracked here so the next iteration knows exactly where to dig:

- **Action-type byte enum** — needs DumpBytes probe in a controlled
  scenario (queue an attack, dump action's +0x10; queue Force, dump;
  etc.). Until then `combat_queue::VerbForActionType` returns
  generic "Aktion" for everything.
- **CSWSFaction decode for hostile/friendly/neutral classification** —
  Phase 2B currently hardcodes "neutral". Open in plan §Pillar 2.
- **Max HP / AC / saves engine accessors** — addresses are in Lane's
  table (validated against `k1_win_gog_swkotor.exe.xml`) but their
  call signatures haven't been live-tested. Currently called only
  from the manual `Shift+S` path (`SpeakSelectedPcStatBlock`); the
  auto-firing paths use direct-field-read for HP-current only.
- **Saving-throw event** — Phase 4B will need a hook on
  `SavingThrowRoll @0x5b92b0` or `BroadcastSavingThrowData @0x4ec760`.
  Polling save fields per-tick is too noisy to be useful.
- **Per-index queue removal primitive** — `RemoveLastAction @0x4d37b0`
  only removes tail; mid-queue Enter currently speaks
  "Aktion kann nicht entfernt werden". Investigate splice / repeat-
  remove + re-queue / SARIF for a positional remove.
- **Examine sister-function trigger** — to drive a real engine
  Examine without ShowExamineBox guesswork, we'd need to invoke the
  SendServerToPlayer*Data path directly OR find the higher-level
  client→server "request examine" message that triggers it. Out of
  scope for skeleton; current direct-stat-read approach works fine.
- **Messages-panel dialog_listbox toggle** — the spec only routes to
  messages_listbox. The toggle button @+0x76c needs a state read so
  we can switch the spec's `findListBox` callback to `dialog_listbox`
  when the user toggles the view.
- **Validated names in Phase 4A callouts pre-iteration-4** —
  `tgt=[end_cut2_sith2]`-style tags appeared because the original
  helper called `engine_area::GetObjectName` which falls through to
  `CSWSObject.tag` for generic enemies. The fix (use
  `GetObjectDisplayNameByHandle` with high-bit conversion) landed
  iteration 4 and is build-validated but not yet in-game tested.

### Files added by the implementation

Skeleton lives in 4 new TUs plus targeted edits to existing files:

- `patches/Accessibility/combat.{h,cpp}` — Phases 1A, 1B, 4A, 4B.
- `patches/Accessibility/combat_query.{h,cpp}` — Phases 2A, 2B, 2C.
- `patches/Accessibility/combat_queue.{h,cpp}` — Phase 3A submenu.
- `patches/Accessibility/dialog_speech.{h,cpp}` — Phase 1D polls.
- `patches/Accessibility/menus_listbox.cpp` — 5 new listbox specs
  (InGameMessages, DialogCinematic, DialogCinematicCopy,
  DialogComputer, DialogComputerCamera).
- `patches/Accessibility/passive_narrate.cpp` — Phase 2B integration.
- `patches/Accessibility/core_tick.cpp` — wires the new Tick calls.
- `patches/Accessibility/interact_hotkey.cpp` — Win32 hotkey poll
  for Shift+H (Examine), Shift+S (PC stats), Shift+K (queue submenu)
  + queue-submenu input routing.
- `patches/Accessibility/engine_area.{h,cpp}` —
  `GetObjectDisplayNameByHandle` helper.
- `patches/Accessibility/engine_offsets.h` — combat-system addresses,
  enum constants, struct offsets (annotated with validation status).
- `patches/Accessibility/strings.h`, `strings_en.cpp`, `strings_de.cpp`
  — every user-facing string for the skeleton.

No new hooks were added in `hooks.toml` — the skeleton is entirely
poll-based. Hook upgrades for AddMessages / ResolveAttack /
SetDialogMessage / SetReplies / SetBark are documented as one-line
follow-ups once their bytes are captured.

---

## Pillar 1: Character properties shown in combat

What the sighted player can see for each character during combat (player and
hostile). All offsets are from class base unless noted.

### Identity & faction

- suspected: `CSWSCreatureStats.first_name @+0x14` / `last_name @+0x1c`
  (CExoLocString) — used by the existing in-world cycle hook
  (`engine_area::GetObjectName`, lay-off-1).
- suspected: `CClientExoApp::GetPlayerCharacterName @0x005edab0` — the chargen
  PC name (already used in `engine_player::GetPlayerCharacterName`).
- suspected: `CSWSCreature::GetFaction @0x513fc0` → `CSWSFaction*`. Faction
  determines hostile/friendly — needed to label "hostile creature" vs "ally"
  in the announcer.
- suspected: `CSWSCreature::GetCreatureReputation @0x513ff0` — likely a 0..100
  score relative to the player's party.
- suspected: `CSWSObject::GetReputation @0x57cb80` — base-class variant.

### Vitals (HP, FP, AC, saves)

Server-side, authoritative:

- suspected: `CSWSObject::GetCurrentHitPoints @0x4caec0` and `SetCurrentHitPoints @0x4cc280`.
- suspected: `CSWSObject::GetMaxHitPoints @0x4d01a0`.
- suspected: `CSWSObject.hit_points @+0xe0` — direct field; getter just reads it.
- suspected: `CSWSCreature::GetMaxHitPoints @0x4ed310` — creature override.
- suspected: `CSWSCreature::GetArmorClass @0x4ed1d0`.
- suspected: `CSWSCreature::GetMaxForcePoints @0x4fd490` — Force points pool.
- suspected: `CSWSCreatureStats::GetFortSavingThrow @0x5ab810`,
  `GetReflexSavingThrow @0x5ab8f0`, `GetWillSavingThrow @0x5ab880`.

Client-side (faster reads, no server round-trip — may lag a tick):

- suspected: `CSWCCreatureStats.hit_points @+0x48` (short),
  `pregame_current_hp @+0x4c`, `max_hit_points @+0x4e` (short).
- suspected: `CSWCCreatureStats.armor_class @+0x50`.
- suspected: `CSWCCreatureStats.max_force_points @+0x11e`,
  `force_points @+0x120` (ushort).
- suspected: `CSWCCreatureStats.fort_save_throw @+0x5c`,
  `will_save_throw @+0x5d`, `ref_save_throw @+0x5e`.
- suspected: `CSWCCreatureStats.experience @+0x58`.

Path to the client stats: `CSWCCreature → server_object @+0xf8 → CSWSCreature →
creature_stats @+0xa74 → CSWCCreatureStats`. Or directly via the CSWGuiManager
party panel (next section).

### Attribute scores (STR/DEX/CON/INT/WIS/CHA)

- suspected: `CSWCCreatureStats.strength_total..charisma_total @+0x34..+0x39`
  (one byte each, post-mod totals).
- suspected: getters under `CSWSCreatureStats::GetSTRStat @0x5a6190` ... etc.

### Equipped weapons + armor

`CSWCCreature` carries the equipment slots inline (`+0x230` main hand,
`+0x234` off hand, plus head/torso/hands/arm-bands/implant/belt/claws/bite/hide).
The values are object IDs into the inventory; resolve via
`CSWCCreature::GetEquippedItemID @0x60ca80`.

- suspected: `CSWCCreature.main_hand_id @+0x230` (ulong item id)
- suspected: `CSWCCreature.off_hand_id @+0x234`
- suspected: `CSWCCreature.head_id @+0x220`, `torso_id @+0x224`,
  `hands_id @+0x22c`, `belt_id @+0x248`, `implant_id @+0x244`
- suspected: `CSWSCreature.inventory @+0xa2c`, `item_repo @+0xa30` →
  `CItemRepository`. Use `ItemListGetItem @0x5560b0` for iteration.
- suspected: `CSWSItem::GetIcon @0x5556b0` — icon ResRef per item.
- suspected: `CSWSItem::GetDamageTypeString @0x5545d0` — "slashing", "energy", etc.
  (TLK-resolved; useful for "attacks with vibroblade dealing energy damage").
- suspected: `CSWSItem::GetSortedPropertyStrings @0x5562d0` — full property
  list of an item as displayable strings.

### Live status flags & effects

- suspected: `CSWSObject::GetDead @0x4cb810`, `CSWSCreature::GetDead @0x4ef820`.
- suspected: `CSWSObject.effects @+0x124` — runtime effect list (poison,
  buffs, debuffs, force shield, drain, etc.).
- suspected: `CSWSCreature::GetFilteredEffectList @0x5014a0` — filtered view.
- suspected: `CSWSObject.is_destroyable @+0xec`, `is_raiseable @+0xf0`,
  `dead_selectable @+0xf4`.
- suspected: `CSWCCreature.stealth_state @+0x194`.
- suspected: `CSWSCreature::GetInvisible @0x501950`,
  `CSWSCreature::GetBlind @0x4ee210`.
- suspected: `CSWSCreatureStats::GetEffectImmunity @0x5a6960`.

### What the HUD itself shows (UI side, for parity / read fallback)

`CSWGuiMainInterface` (the in-game HUD) carries the displayed widgets for the
player and the two followers. Pulling text from these is the *visual ground
truth* — same path our menu work uses.

- known (DB): `CSWGuiMainInterface.party_members @+0x1f88` (3 entries of
  `CSWGuiMainInterfaceChar`).
- known (DB): `CSWGuiMainInterface.main_character @+0x4b80` — the controlled
  party member's panel (separate slot from `party_members`).

Per `CSWGuiMainInterfaceChar` (one per visible portrait):

- known (DB): `back_label @+0x28`
- known (DB): `debilitated_label @+0x168`, `disabled_label @+0x2a8` —
  flashing-status overlays (stunned/disabled/etc.)
- known (DB): `level_up_label @+0x3e8` — level-up indicator
- known (DB): `char_label @+0x668` — character name label
- known (DB): `vitality_progress_bar @+0x7a8` — HP bar
- known (DB): `force_progress_bar @+0x8f8` — Force points bar
- known (DB): `positive_effecr_label @+0xa48` — buff icon row
- known (DB): `negative_effect_label @+0xb88` — debuff icon row
- known (DB): `character_button @+0xcc8` — clickable portrait

Progress bars themselves: `CSWGuiProgressBar.max @+0x5c` (caller writes the
current value into the standard CSWGuiControl text/state fields). For an
HP-bar read we only need (current, max) — `current` from the creature stats,
`max` from the bar's `max` field (or from `CSWCCreatureStats.max_hit_points`).

- known (DB): `CSWGuiMainInterface.combat_mode_message_label @+0x735c` —
  the on-screen "ATTACKING X" text shown while attacking.
- known (DB): `CSWGuiMainInterface.action_description_label @+0xa1d4` — the
  current-action description (Pillar 3 surface).
- known (DB): `CSWGuiMainInterface.status_summaries @+0xa460` — the lower-left
  status row.
- known (DB): `CSWGuiMainInterface.target_action_menu @+0xbc` — the radial-on-
  target menu (used by the existing radial work; combat-relevant for "what
  can I do to this enemy" cues).

### Recommended hook surface for Pillar 1

For a per-tick "tell me about character X" reader (player + party + current
target + nearest hostile), prefer the **server-side** chain:

1. Walk to `CSWSCreature` via `engine_player::GetPlayerServerCreature` (already
   built) or via the `engine_area::AreaObjectIterator` for non-player creatures.
2. Read `creature_stats @+0xa74` once → snapshot HP, FP, AC, saves, attributes.
3. For equipped weapon, read `main_hand_id`/`off_hand_id` from the matching
   `CSWCCreature` (via `server_object` indirection — already used elsewhere)
   and resolve to a `CSWSItem*` through the area's `CGameObjectArray`.
4. Faction comes from `CSWSCreature::GetFaction`.

The HUD widget reads (HP-bar live value, debilitated label) are a useful
**verification source** — they show what the engine itself currently displays,
not the latest server state. For a screen-reader announcement we want the
authoritative state, so the stats path is primary.

### Open

- open: short / cleanly-named "is the creature in combat right now" predicate.
  `CSWCCreature::InCreatureCombat @0x60d3e0` is the obvious candidate; needs
  decompilation to confirm semantics.
- open: damage-type immunity / resistance breakdown read path (we know the
  effects list is at `+0x124`, but the *displayed* damage-resist UI text — if
  any — needs investigation).
- open: weapon-vs-armor effectiveness (`CSWSCreature::GetIsWeaponEffective
  @0x4edb20`) — useful for "your blaster does nothing to this droid" hints.

---

## Pillar 2: Damage / hit / miss / action display

This is the floaty-number / combat-feedback channel: what the sighted player
sees as numbers floating up from creatures, the "ATTACKING X" combat-mode
message, the message log entries ("you struck for 8 damage"), and the
animation cues that go with each.

### Per-attack data structures

The round carries `attacks_list[7]` (struct `CSWSCombatAttackData`,
swkotor.exe.h:9933, DB-confirmed) — a fixed-size 7-element array of in-flight
attack records. Each record:

- known (DB): `attacks_list` array starts at `CSWSCombatRound +0x4`.
- known (DB): `CSWSCombatAttackData.react_object @+0xc` — the target id.
- known (DB): `CSWSCombatAttackData.base_damage @+0x38` (short) — rolled
  damage before reductions.
- known (DB): `CSWSCombatAttackData.weapon_attack_type @+0x3a` (byte) —
  melee / ranged / unarmed / touch.
- known (DB): `CSWSCombatAttackData.attack_mode @+0x3b` (byte) — BAB tier
  or feat-based attack tier.
- known (DB): `CSWSCombatAttackData.sub_attacks @+0xac`
  (CExoArrayList<CSWSCombatSubAttack>) — sub-attacks (off-hand, deflection).
- known (DB): `CSWSCombatAttackData.attack_debug_text @+0x78` (CExoString) —
  per-attack human-readable debug text. **Promising read path**: if engine
  fills this with a parseable "rolled 14+5=19 vs AC 17, hit" string, we get
  the breakdown for free.
- known (DB): `CSWSCombatAttackData.damage_debug_text @+0x80` (CExoString) —
  per-attack damage breakdown text.
- suspected (swkotor.exe.h, fields not in DB but in header): `missed_by @+0x18`,
  `attack_result @+0x5c` (0 pending / 1 hit / 2 miss / 3 crit / 4 deflected
  — *inferred*, needs validation), `critical_threat @+0x50`,
  `attack_deflected @+0x54`, `sneak_attack @+0x48`, `coup_de_grace @+0x4c`,
  `killing_blow @+0x4c`-area, `attack_type @+0x64`, `ranged_target_pos @+0x68`.
- open: validate the `attack_result` enum experimentally. `attack_debug_text`
  may already be the highest-level user-friendly read and avoid the enum.
- **Partial validation (2026-05-10, from crash log
  `patch-20260509-235058.log`):** unused-slot sentinel is **-1**
  (0xFFFFFFFF), NOT 0. Confirmed against attacks_list[7] read on the
  very first tick of Endar Spire load — every slot returned
  `result=-1 dmg=0 defl=-1 crit=-1`. Implication: the "is this slot
  populated" check is `attack_result != -1`; the pending/resolved
  enum values still need validation when an actual attack lands.
- **Confirmed (2026-05-10, from `patch-20260510-000722.log`):**
  attack_result = **2 confirms Miss** (live observation of player
  attack that whiffed in tutorial combat). `defl=0 crit=0` for a
  normal miss (NOT -1 as in unused slots — those flags only sentinel
  in unused state). `dmg=-1` accompanies a miss. Hit / crit / deflect
  enum values still need validation but `2 = miss` is now anchor-
  confirmed.
- **Confirmed (2026-05-10):** the engine reuses attacks_list[7] slots
  *transitioning resolved→resolved without going back through pending*.
  An edge detector that only fires on Pending(0)→Resolved misses every
  attack after the first. Use a `(target, result, base_damage)` tuple
  diff with `result IN valid resolved range` instead — that's the
  approach in the current `combat::TickAttackResolutions`.

The action-list mirror (`CSWSCombatRoundAction`, swkotor.exe.h:22829) carries
its own copies of the resolved fields — these are what the UI watches:

- known (DB): `CSWSCombatRoundAction.action_type @+0x10` (byte) — see Pillar 3
  for the enum.
- known (DB): `target @+0x14` (ulong handle).
- known (DB): `inventory_slot @+0x1c`, `target_repository @+0x20`,
  `move_to_position @+0x38` (Vector — note: header says +0x28 but DB says
  +0x38; trust DB), `should_run @+0x54`.
- known (DB): `animation @+0x78`, `attack_result @+0x7c` (int),
  `damage @+0x80` (int).
- swkotor.exe.h carries an additional `attack_feat? @+0x54` field at the
  end — needs validation.

So once an attack node has `attack_result != 0` we know the round resolved it,
and `damage` is the final number. Neither structure needs walking to reach the
fields — they're plain int reads.

### Floaty-text / damage-number paths

Three layers, increasing ones:

1. **Server fires the broadcast**:
   - suspected: `CSWSCreature::BroadcastFloatyData @0x4ec610` — "show floaty N
     above creature this". Single best server-side hook for "damage
     number popped".
   - suspected: `CSWSCreature::DisplayFloatyDataToSelf @0x4ef560` — local-
     player variant.
   - suspected: `CSWSCreature::BroadcastDamageDataToParty @0x4ec400` — sends
     damage event to the party (multi-recipient).
   - suspected: `CSWSCreature::BroadcastDamageBreakdown @0x4ecca0` — the
     detailed breakdown ("8 phys + 2 fire = 10").
   - suspected: `CSWSCreature::BroadcastSavingThrowData @0x4ec760` — saving
     throw outcome.

2. **Client-side message handler unpacks**:
   - suspected: `CSWCMessage::HandleServerToPlayerCombatRound @0x6514c0` — the
     network entry point on the client. Unpacks server→client combat-round
     messages and dispatches to the GUI. Single best post-deserialize hook
     for "the round's results are now visible to the player".

3. **GUI renders**:
   - suspected: `CGuiInGame::AddFloatyText @0x62b080` — UI floaty list insert.
     Hook here for "floaty text appeared", since both BroadcastFloatyData and
     DisplayFloatyDataToSelf funnel through `AddFloatyText`.
   - suspected: `CSWGuiMainInterface::AddFloatyText @0x68b7c0` — main HUD
     floaty path; called from CGuiInGame variant. Also
     `RemoveFloatyText @0x688060` for lifecycle.
   - suspected: `CSWGuiMainInterface::SetCombatMessage @0x687700` — sets the
     "ATTACKING X" label (writes into `combat_mode_message_label @+0x735c`).
   - suspected: `CGuiInGame::SetCombatMessage @0x62b110` — wrapper.
   - suspected: `CSWPartyMemberData::SetCombatMessage @0x6345e0` — per-party-
     member combat-message slot.

### Damage-application path (HP change)

- suspected: `CSWSObject::DoDamage @0x4ccf80` — base-class damage application;
  writes `hit_points @+0xe0`. Unconditional path; runs even for placeables.
- suspected: `CSWSCreature::DoDamage @0x4f3830` — creature override; runs
  parent then handles death / unconscious / state effects. **Best single
  hook for "creature X took N damage, now at Y/Z HP"** — it has `this`
  (the creature), the damage int, and post-call HP is consistent.
- suspected: `CSWSCreature::SignalDamage @0x5b8050` — post-damage signal;
  branches to `SignalMeleeDamage @0x5b75d0` / `SignalRangedDamage @0x5b6f30`.
  Useful if we want melee/ranged differentiation in announcements.
- suspected: `CSWSCreature::ApplyOnHitAbilityDamage @0x5b9e40` — on-hit
  weapon-property damage (e.g. "+1d6 fire").
- suspected: `CSWSCreature::ApplyPoisonDamage @0x4ee770`.
- suspected: `CSWSCreature::ResolveDamage @0x5bb440` — damage-roll path
  (computes the number; doesn't apply yet).
- suspected: `CSWSCreature::ResolveDamageShields @0x5b7d10` — shield/DR
  reduction.
- suspected: `CSWSCreature::ResolvePostMeleeDamage @0x5b8b00`,
  `ResolvePostRangedDamage @0x5b8c20` — late-stage damage hooks (after
  the engine has finalized the number); good for "speak the post-DR damage".
- suspected: `CSWSObject::GetLastDamageAmountByFlags @0x4cafe0` — recent
  damage by flag (slashing, fire, etc.) — useful for retrospective queries.

### Attack-roll resolution path

- suspected: `CSWSCreature::ResolveAttack @0x5bba80` — entry: rolls d20 vs
  AC, sets the attack record's hit/miss/crit, calls ResolveDamage on hit.
  The bottom-of-stack hook for "this single attack just resolved".
- suspected: `CSWSCreature::ResolveAttackRoll @0x5baca0` — d20-only;
  no damage.
- suspected: `CSWCCreature::ResolveAttackIndex @0x60d0e0` — client
  echo / per-attack-index lookup.
- suspected: `CSWSCombatAttackData::ResolveAttackToMeleeSubAttack @0x4d5a50`,
  `ResolveAttackToRangedSubAttack @0x4d5b60`,
  `ResolveSubAttackToAttack @0x4d27e0` — subdivides multi-hit attacks.
- suspected: `CSWSCombatAttackData::GetTotalDamage @0x4d2180` — sums
  base + bonuses across the sub-attacks. Cleanest "final damage of this
  attack" call.
- suspected: `CSWSCombatAttackData::GetBaseDamage @0x4d2060`,
  `GetDamage @0x4d1f80` — finer-grained per-type/sub-attack reads.
- suspected: `CSWSCreature::SavingThrowRoll @0x5b92b0` — d20 save roll.

### Message buffer (the running "combat log" feed)

- suspected: `CGuiInGame::AppendToMsgBuffer` (per accessibility-map.md) —
  the rolling text feed.
- suspected: `CGuiInGame::GetMessageBuffer @0x62a9e0`,
  `GetMessageBufferSize @0x62a9f0` — reading the existing buffer.
- suspected: `CGuiInGame::UpdateMessageGUI @0x62ad60` — refresh tick.
- suspected: `CSWGuiOptionsFeedback::OnFloaty @0x6de6a0` — the options screen
  for floaty text (worth checking what user toggles control).

The message-buffer path is interesting because users can already enable
"verbose feedback" in vanilla options — that's the same pipeline. If we hook
`AppendToMsgBuffer` we get a stream of clean, localised, post-format strings
without having to rebuild the formatter. Trade-off: it's one frame later
than `Broadcast*Data`, and we lose structured data (just text).

### Existing combat-log UI: `CSWGuiInGameMessages`

KOTOR ships an in-game **Messages** screen — reachable from the in-game menu
via `CSWGuiMainInterface.messages_button @+0xb71c`. It's a single panel with
two listboxes inside, plus a toggle button that swaps which is visible. We
should expose it via the existing `ListBoxPanelSpec` plumbing rather than
build our own combat-log overlay.

- known (DB): `CSWGuiInGameMessages.panel @+0x0` — the panel base.
- known (DB): `CSWGuiInGameMessages.messages_listbox @+0x64` — the
  combat-feedback log. Same data the in-world running-text feed shows,
  but persisted and scrollable.
- known (DB): `CSWGuiInGameMessages.dialog_listbox @+0x344` — dialog
  history (every line spoken in the most recent conversation).
- known (DB): `CSWGuiInGameMessages.messages_label @+0x624` — the panel
  title label.
- known (DB): `CSWGuiInGameMessages.show_button @+0x76c` — toggles
  between feedback / dialog view.
- known (DB): `CSWGuiInGameMessages.exit_button @+0x930`.

Public API:

- suspected: `CSWGuiInGameMessages::AddMessages @0x626920` — appends one or
  more entries to `messages_listbox`. **Single-best live hook**: every
  combat-feedback string lands here, already localised and already gated
  by the user's vanilla feedback-verbosity options.
- suspected: `CSWGuiInGameMessages::AddDialogMessages @0x626b10` — same
  for `dialog_listbox`.
- suspected: `CSWGuiInGameMessages::ShowFeedbackMessages @0x624f70`,
  `ShowDialogMessages @0x624df0` — view-toggle handlers.
- suspected: `CSWGuiInGameMessages::OnPanelAdded @0x626d90`,
  `HandleInputEvent @0x628260` — lifecycle / input.

**Dual-channel implication.** Pillar 2 splits cleanly into two complementary
mechanisms, both reusing existing infrastructure:

- **Live narration during combat** — hook `AddMessages` and pipe each
  appended row to TTS. The user hears events as they happen. Reuses the
  vanilla feedback-verbosity options (no new toggle to build).
- **Review-on-demand** — register a `ListBoxPanelSpec` for
  `CSWGuiInGameMessages` pointing at `messages_listbox @+0x64`. The user
  can open the Messages screen any time, scroll back, and re-read whatever
  they missed. Same pattern we use for SkillInfoBox / Equip / etc. — focus
  sync + per-row announce comes for free.

### Recommended hook surface for Pillar 2

Two announcement channels make sense, with different latencies:

- **"Round-resolved" structured channel**: hook `CSWSCreature::ResolveAttack`
  (or `ResolveAttackToMeleeSubAttack` / `ResolveAttackToRangedSubAttack` for
  per-sub-attack granularity). At return we have `CSWSCombatAttackData` fully
  populated — `attack_result`, `base_damage`, `react_object`,
  `attack_debug_text`. Build a localised string ourselves.
- **"Visible-to-sighted" mirror channel**: hook the message buffer
  (`AppendToMsgBuffer`) or `CGuiInGame::AddFloatyText`. We get the engine's
  own user-facing string for free — including the localised, options-
  -respecting "verbose vs concise" feedback level. Pair with a configurable
  verbosity that mirrors the engine's own toggle.

Phase 1 of an implementation plan would likely start with the second channel
(low-effort, high-coverage) and only move to the structured channel for events
that aren't in the message buffer or that need richer announcements (target
HP after, e.g.).

### Open

- open: validate `attack_result` enum values (1=hit / 2=miss / 3=crit /
  4=deflected, *inferred*) by hooking `ResolveAttack` and dumping the post-
  call value in known scenarios.
- open: confirm whether `attack_debug_text` / `damage_debug_text` are populated
  in retail builds or only debug. They could be the cleanest read path if so.
- open: identify the exact "miss" / "deflected" floaty strings (likely TLK
  strrefs); the engine's own German/English split should give us these.
- open: where is the "Critical Hit!" callout displayed? Probably a special-
  case in `BroadcastFloatyData` or one of the SignalDamage variants.
- open: damage type — slashing / piercing / fire / cold / energy. Likely
  encoded in a flags int on `CSWSCombatSubAttack` / `CSWSCombatAttackData`;
  needs SARIF dive.

---

## Pillar 3: Per-character action queue (combat strip)

The visible "what is this character about to do" strip on the HUD. The user
edits it during pause; AI auto-fills it when control is released. There's one
strip per party member (3 visible at once, plus the controlled-character
panel).

### Engine surfaces

- known (DB): `CSWSCreature.combat_round @+0x9c8` → `CSWSCombatRound*`.
- known (DB): `CSWSCombatRound.actions @+0x9b0` —
  `CExoLinkedList<CSWSCombatRoundAction>*`. The queue itself.
- known (DB): `CSWSCombatRound.current_action @+0x9d0` (byte) — index of the
  in-flight action.
- known (DB): `CSWSCombatRound.current_attack @+0x96c` (byte) — index into
  `attacks_list[7]` for the attack being resolved this tick.
- known (DB): `CSWSCombatRound.timer @+0x944` (int) — round-elapsed ms.
- known (DB): `CSWSCombatRound.roung_length @+0x94c` (int) — round duration.
- known (DB): `CSWSCombatRound.player_creature @+0x9b4` (CSWSCreature*) —
  back-pointer.
- known (DB): `CSWSCombatRound.engaged @+0x9b8` (int) — combat-engaged flag.
- known (DB): `CSWSCombatRound.master @+0x9bc` / `master_id @+0x9c0` — who
  the creature is currently locked in melee with.

`CSWSCombatRoundAction` (the queue node):

- known (DB): `action_timer @+0x0` (int) — animation/duration timer.
- known (DB): `animation_id @+0x4` (ushort).
- known (DB): `amination_time @+0x8` (int — the engine has this typo).
- known (DB): `num_attacks @+0xc` (int).
- known (DB): `action_type @+0x10` (byte) — the dispatch code (see below).
- known (DB): `target @+0x14` (ulong handle).
- known (DB): `retargettable @+0x18` (int — can the action be auto-redirected
  if the target dies?).
- known (DB): `inventory_slot @+0x1c`, `target_repository @+0x20`,
  `move_to_position @+0x38` (Vector), `should_run @+0x54` (int),
  `animation @+0x78` (int), `attack_result @+0x7c` (int),
  `damage @+0x80` (int).

Add paths (one per action category):

- suspected: `CSWSCombatRound::AddAction @0x4d3660` — generic.
- suspected: `CSWSCombatRound::AddAttackAction @0x4d38b0`.
- suspected: `CSWSCombatRound::AddSWSpellAction @0x4d39b0` — Force / spell.
- suspected: `CSWSCombatRound::AddSWItemSpellAction @0x4d3a90` — item-cast.
- suspected: `CSWSCombatRound::AddEquipAction @0x4d3c30`,
  `AddUnequipAction @0x4d3ce0` — weapon switch.
- suspected: `CSWSCombatRound::AddCutsceneAttackAction @0x4d3810`,
  `AddCutsceneMoveActions @0x4d3b30` — scripted scenes.

Higher-level adders on the creature itself (these wrap the round's adders
and also touch the `CSWSObject.action_nodes @+0xfc` outer queue):

- suspected: `CSWSCreature::AddAttackActions @0x4fde40`,
  `AddCastSpellActions @0x4f9460`,
  `AddCounterSpellActions @0x4fce90`,
  `AddItemCastSpellActions @0x4f8c70`,
  `AddEquipItemActions @0x4f0420`, `AddUnequipActions @0x4f06d0`,
  `AddDropItemActions @0x4f0840`, `AddPickUpItemActions @0x4eb620`,
  `AddHealActions @0x4f0900`,
  `AddUseTalentAtLocationActions @0x4fd070`,
  `AddUseTalentOnObjectActions @0x500ca0`.
- suspected: `CSWCCreature::ActionMenuAttack @0x616800`,
  `ActionMenuUseAttackFeat @0x617dd0` — the player-facing "attack" /
  "use feat" menu paths (called from the radial — links to existing
  radial work).

Remove paths:

- suspected: `CSWSCombatRound::RemoveAllActions @0x4d3770`,
  `RemoveLastAction @0x4d37b0`, `RemoveSpellAction @0x4d3ba0`,
  `ClearAllAttacks @0x4d37e0`, `ClearAllSpecialAttacks @0x4d4f60`.
- suspected: `CSWCObject::ClearAllQueuedCombatActions @0x63d490` —
  client-side full clear.
- suspected: `CSWSCombatRoundAction::ClearData @0x4d1d50` — zeroes a node;
  observed-from-decomp field set matches the offsets above.

Server-side per-tick processor:

- suspected: `CSWSCreature::UpdateActionQueue @0x4f6f30` — walks
  `combat_round.actions`, dispatches per `action_type`, decrements timers.
  Fires every tick during combat. **The per-tick "queue advanced" point.**
- suspected: `CSWSCreature::ProcessPendingCombatActions @0x4f7530` — pulls
  the next action and starts it.

### UI rendering surfaces (CSWGuiMainInterface)

- known (DB): `CSWGuiMainInterface.action_description_label @+0xa1d4` — the
  current-action description text label.
- known (DB): `CSWGuiMainInterface.action_description_bg_label @+0xa314`.
- known (DB): `CSWGuiMainInterface.clear_one_button @+0x6cd0`,
  `clear_one_2_button @+0x6e94`, `clear_all_button @+0x7058` — the
  player-facing strip buttons.
- known (DB): the action panel widgets are inline `CSWGuiMainInterfaceAction`
  structs (struct in swkotor.exe.h:10194 — `action_button`, `action_label`,
  `up_button`, `down_button`, `is_action`). They're embedded in
  `CSWGuiMainInterface` but the DB doesn't expose the offset; needs SARIF
  walk to find. `action_description_label @+0xa1d4` is likely *inside* one
  of them, so the action-panel offsets are around there.

Render functions:

- suspected: `CSWGuiMainInterface::ShowActionQueue @0x6874c0` — per-character
  refresh (param = 0..2 party-member index).
- suspected: `CSWGuiMainInterface::UpdateActionQueue @0x68a010` — refresh-all
  tick; iterates the three party panels.
- suspected: `CSWGuiMainInterface::GetActionIcon @0x686fb0` — translates
  `action_type` + `target` into two CResRef icons (action icon + target icon).
  The action_type → icon mapping is in here.
- suspected: `CSWGuiMainInterface::ShowActionIcon @0x685610` — paints one
  slot in the strip.
- suspected: `CSWGuiMainInterface::SetActionDescription @0x685560`,
  `UpdateActionDescription @0x686e20` — set/refresh the "Attack Revan" label.
- suspected: `CSWGuiMainInterface::OnActionDownArrowPressed @0x68afe0`,
  `OnActionUpArrowPressed @0x68af70` — the up/down arrow buttons next to the
  strip. They cycle the visible-action selection within the queue (the strip
  shows a window).
- suspected: `CSWGuiMainInterface::OnDefaultActionFocus @0x6856c0`,
  `OnDefaultActionLeft @0x68b970` — focus + "default action" path; relevant
  for keyboard-only operation.
- suspected: `CSWGuiMainInterface::ClearOneAction @0x688790`,
  `OnClearOneButtonPressed @0x68b050`,
  `OnClearAllButtonPressed @0x68b0a0` — the strip's clear buttons.
- suspected: `CSWGuiMainInterface::DoPersonalAction @0x68ad60`,
  `DoTargetAction @0x68ad20`,
  `SelectPrevPersonalAction @0x6888e0` — queue-add path from the strip UI.

### Inferred action_type enum

From the AddX function names and matching adders — the byte at
`CSWSCombatRoundAction +0x10` plausibly takes values from a small enum like:

- attack, special-attack, spell-cast, item-cast spell, equip, unequip,
  move-to-point, use-talent-on-object, use-talent-at-location, drop-item,
  pickup-item, cutscene-attack, cutscene-move, heal, ...

The exact byte values aren't yet pinned. **Open**: hook `AddAttackAction` etc.
once and dump the byte each writes; the enum locks down in a single session.

### Recommended hook surface for Pillar 3

Two complementary channels, again:

- **"Action queued / unqueued" event channel**: hook the AddX adders on
  `CSWSCombatRound` (or `CSWSCreature`) and the Remove/Clear paths.
  Announce "Carth added: attack Malak" and "Bastila cleared: cast Heal".
- **"Action started executing" channel**: hook
  `CSWSCreature::ProcessPendingCombatActions` (or
  `UpdateActionQueue`) for "Carth begins: Power Attack on Malak". The
  current_action byte change is the easiest signal.

For a full read of the queue at any moment ("Tab+Q: read me Carth's queue"),
walk `combat_round.actions` linked-list yourself and resolve each
action_type byte through a static table built once we know the enum.

### Open

- open: action_type byte enum values — needs DumpBytes in a controlled
  scenario (queue an attack, dump 0x10; queue a spell, dump 0x10; etc.).
- **Partial finding (2026-05-10):** the inferred enum mapping
  (Attack=0 / SpellCast=1 / ItemCast=2 / Equip=3 / ...) was WRONG. In
  tutorial-level play with no Force powers learned, `type=1` rendered
  as "Macht einsetzen" (the user's actual complaint), so type=1 is
  NOT SpellCast. Real values seen in the queue: `1`, `11`, and
  `255 (0xFF)` — the last is a placeholder leading entry on every
  queue ("current dispatching action" slot the engine maintains, with
  target=0 when nothing is dispatching). The current
  `combat_queue.cpp` filters type=0xFF and renders all other types as
  generic "Aktion" until the enum is pinned.
- open: the icon ResRef → human-readable name mapping. `iact_attack`,
  `iact_cast_spell` etc. are GUI layer; the description text comes from
  somewhere localised (likely TLK via a 2DA or via the action handler's
  own format string). Worth investigating after enum is locked.
- open: `CSWGuiMainInterfaceAction` panel offset inside `CSWGuiMainInterface`
  — the structure's there but its *position* isn't in the DB. Needed if we
  want to read the displayed description text directly from the widget
  (mirror channel).
- open: the AI auto-queue path — when the player releases pause, the engine
  fills the queue automatically for non-controlled members. We may want a
  separate "AI auto-queued X for Bastila" announcement (or to suppress them
  to reduce noise).

---

## Pillar 4: Round structure / mechanics / lifecycle

The skeleton everything else hangs on.

### Round lifecycle

- suspected: `CSWSCombatRound::StartCombatRound @0x4d5f70` — round begins;
  resets `attacks_list`, starts timer.
- suspected: `CSWSCombatRound::EndCombatRound @0x4d4620` — round ends;
  garbage-collects.
- suspected: `CSWSCombatRound::IncrementTimer @0x4d4c10` — per-tick.
- suspected: `CSWSCombatRound::DecrementRoundLength @0x4d3440`,
  `DecrementPauseTimer @0x4d4e80`.
- suspected: `CSWSCombatRound::SetRoundPaused @0x4d28f0`,
  `SetPauseTimer @0x4d2920` — pause handling.
- suspected: `CSWSCombatRound::ResolveChoreographedState @0x4d2b70`,
  `ResolveEngagedState @0x4d2c30`, `ResolveMasterState @0x4d2d60` — the
  round's engaged/master/choreographed state machines.
- suspected: `CSWSCombatRound::CheckActionLengthAtTime @0x4d2970` — schedule
  query.

### Combat-mode globals (per-app, not per-creature)

- suspected: `CClientExoApp::GetCombatMode @0x5ede70`,
  `SetCombatMode @0x5ede80` — "is the game in combat mode" boolean.
- suspected: `CClientExoApp::GetPausedByCombat @0x5edc10`,
  `SetPausedByCombat @0x5edc20` — engine-level combat pause.
- suspected: `CClientExoApp::GetSoundPausedByCombat @0x5ee2d0`.
- suspected: `CClientExoAppInternal::UpdateCombatMode @0x5f3ad0` — per-tick
  combat-mode evaluator. **Best hook for "we just entered combat" /
  "we just left combat" context-change announcements.**
- suspected: `CSWCCreature::SetCombatMode @0x610a10`,
  `CSWCCreature::SetCombatState @0x6182a0` — per-creature flags.
- suspected: `CSWBehaviorCameraCombat::*` (0x6d12b0..0x6d22c0) — combat
  camera. Not directly relevant for a screen reader but useful for context
  (e.g. the camera shifts when combat starts).

### AI vs player control transitions

- known: `engine_player::SetPlayerInputEnabled` (already built) wraps
  `CSWPlayerControl::SetEnabled @0x006792e0` — toggles per-tick movement
  clobber. The sustained-disable path (autowalk, view mode) is documented
  in `engine_player.h`.
- suspected: `CSWGuiMainInterface::OnSoloButtonPressed @0x688610` — solo /
  party mode toggle (links to the existing radial / engine-input work).
- suspected: `CSWGuiMainInterface::OnPauseButtonPressed @0x688590`,
  `SetupPauseGuiExtent @0x688db0` — the user-toggleable pause.

### Initiative / who-attacks-whom

- suspected: `CSWSCreature::GetFirstAttacker @0x5ba400`,
  `GetNextAttacker @0x5b7fe0` — iteration over attackers targeting this
  creature.
- suspected: `CSWSCreature::ClearHostileActionsVersus @0x4f61d0` — drop all
  queued hostile actions on a target.
- suspected: `CSWSCreature::GetNearestEnemy @0x4f2de0` — tactical query;
  matches our existing nearest-object work.
- suspected: `CSWSCreature::AIActionCombat @0x5b6210` — AI dispatcher's
  combat branch.

### Death / dying

- suspected: `CSWSObject::GetDead @0x4cb810`, `CSWSCreature::GetDead @0x4ef820`,
  `GetDeadTemp @0x4ef890` — death state.
- suspected: `CSWSObject::SetCurrentHitPoints @0x4cc280` will fire the
  death/dying path internally when HP drops below 1; investigate whether a
  separate "creature died" broadcast exists (likely yes via the message bus).
- suspected: `CSWGuiMessageBox::DisplayDeathMessage @0x627260` — the "you
  have died" overlay.

### Faction / aggro

- suspected: `CSWSFaction::GetMostDamagedMember @0x5bed00`,
  `GetLeastDamagedMember @0x5bee70`,
  `SendFactionUpdateAdd @0x5be9c0`, `SendFactionUpdateRemove @0x5bea20`.
  Faction state is per-faction (not per-creature); it tracks who the AI is
  targeting and group-level reactions.

---

## Adjacent surface: in-game dialog screen (`CSWGuiDialog`)

Sibling to the Messages panel — same listbox-spec pattern applies, so we
fold it into the same Phase 1 alongside the combat-log work. Two distinct
UIs cover dialog: the **live conversation panel** (CSWGuiDialog and
variants) and the **dialog history** (already covered above as
`CSWGuiInGameMessages.dialog_listbox`).

### Live conversation panel (`CSWGuiDialog`)

Player chooses replies from a listbox while the NPC's current line shows
in a label above it. Variant subclasses cover cinematic / computer-terminal
shapes; all share the same base.

- known (DB): `CSWGuiDialog.panel @+0x0`.
- known (DB): `CSWGuiDialog.replies_listbox @+0x19c4` — the player's
  reply choices. Plug into the existing `ListBoxPanelSpec` plumbing.
- known (DB): `CSWGuiDialog.message_label @+0x1ca4` — the NPC's currently
  spoken line. Read its text on every panel-update tick or hook
  `SetDialogMessage` (below) for an event-driven feed.

Public API:

- suspected: `CSWGuiDialog::SetDialogMessage @0x6a7010` — writes the NPC
  line into `message_label`. **Best hook for "NPC just said X"** —
  fires once per dialog node entered.
- suspected: `CSWGuiDialog::ShowDialogMessage @0x6a70a0` — wrapper.
- suspected: `CSWGuiDialog::SetReplies @0x6a86a0` — populates the
  reply listbox; fires once per node-with-replies. **Best hook for
  "you have N replies".**
- suspected: `CSWGuiDialog::SetReplyActive @0x6a6fc0`,
  `SetReplyInactive @0x6a6fe0` — gates individual replies (skill checks,
  alignment locks). The dialog screen needs to surface inactive-reply
  state too — sighted players see them greyed.
- suspected: `CSWGuiDialog::SelectReply @0x6a7110`,
  `OnSelectReply @0x6a6f20` — the player commits to a reply.
- suspected: `CSWGuiDialog::HandleSelectReply @0x6a7390`,
  `GetReplyIndex @0x6a6d80` — input + index helpers.
- suspected: `CSWGuiDialog::EndDialog @0x6a6d70`,
  `CSWGuiDialog::Reset @0x6a7160` — lifecycle.
- suspected: `CSWGuiDialog::HandleInputEvent @0x6a7230` — same pattern as
  every other panel; usable as a focus-sync entry point.

### Variant panels (also need handling)

- suspected: `CSWGuiDialogCinematic` — letterboxed cinematic dialog
  (overrides `SetReplies @0x6a8de0`, `ShowDialogMessage @0x6a7d30`,
  `SelectReply @0x6a7e20`).
- suspected: `CSWGuiDialogComputer` — computer-terminal dialog. Has its
  own `message_listbox @+0x2cfc` (the *terminal output* shown above the
  reply choices), `obscure_label @+0x34dc`, plus a `UpdateSkills @0x6a8240`
  path because computer dialogs route through skill checks. We need to
  read both the message_listbox (terminal text) and the embedded
  `dialog.replies_listbox`.
- suspected: `CSWGuiBarkBubble` — floating short NPC blurts (overworld
  flavour text, e.g. taunts). `SetBark @0x6a9920` is the entry point;
  `StopBark @0x6a9c60` clears it. Different lifecycle from the main
  dialog — short, transient, no replies. Speak via TTS, no panel
  needed.

### Engine-side dialog tick (`CGuiInGame`)

The conversation state machine itself. Useful for context events and as
fallbacks when the panel-side hooks miss something.

- suspected: `CGuiInGame::IsInDialog @0x62dbd0` — predicate;
  cheap to poll.
- suspected: `CGuiInGame::HandleDialogEntry @0x631d80` — fires on
  entering a dialog node (NPC line). Server-driven equivalent of
  `SetDialogMessage`.
- suspected: `CGuiInGame::ShowDialogEntry @0x62b730`.
- suspected: `CGuiInGame::AppendToDialogBuffer @0x62b680` — text feed
  accumulator (drives `CSWGuiInGameMessages.dialog_listbox`).
- suspected: `CGuiInGame::HandleDialogReplies @0x634340` — replies
  presented; engine-side counterpart to `SetReplies`.
- suspected: `CGuiInGame::ShowDialogReplies @0x6340e0`.
- suspected: `CGuiInGame::HandleDialogReplyChosen @0x62e100` — the
  player committed.
- suspected: `CGuiInGame::HandleDialogSelection @0x633660`,
  `SetDialogSelection @0x62ada0` — selection state (which reply is
  highlighted).
- suspected: `CGuiInGame::CloseDialog @0x6332b0`,
  `EndDialogCamera @0x62c2a0` — exit context-change.
- suspected: `CGuiInGame::UpdateDialog @0x6339c0` — per-tick update.

### Recommended hook surface for the dialog screen

For both variants the same pattern works:

- **Speak the NPC line on entry** — hook `CSWGuiDialog::SetDialogMessage`
  and read the line text either from the arg (CExoString*) or from
  `message_label` after the call. Bark variant: hook
  `CSWGuiBarkBubble::SetBark`.
- **Speak the reply choices** — hook `CSWGuiDialog::SetReplies` to learn
  *that* replies are available. Surface them via the existing
  `ListBoxPanelSpec` for `replies_listbox @+0x19c4` so the user
  navigates with the same arrow / Enter keys as elsewhere.
- **Surface inactive replies** — `SetReplyActive` /
  `SetReplyInactive` is per-row. Add an `enrichRow` callback to the
  listbox spec that announces "(unavailable: skill 7+ required)" or
  similar when active=false.
- **Computer-terminal extra channel** — for `CSWGuiDialogComputer`,
  the `message_listbox @+0x2cfc` is a separate listbox we expose
  alongside `replies_listbox`. Two listbox specs, same panel.

### Open

- open: validate `CSWGuiDialog`'s exact reply-row layout — likely the
  same row struct other listboxes use, but worth a DumpBytes pass.
- open: where do active/inactive flags live on the reply row? Likely a
  byte at a known offset within the listbox row struct; read it from
  `enrichRow` to announce the gate.
- open: how does the bark bubble timeout work? `Pause @0x6a9bb0` /
  `Resume @0x6a9c10` exist — investigate when they fire so we don't
  re-speak a paused-and-resumed bark.

---

## Plan

Phased implementation plan for combat + dialog accessibility. Phase numbers
indicate landing order, not parallelism within a phase. Each phase is
shippable on its own; later phases enrich rather than depend on earlier ones.

**Status (2026-05-10):** Skeleton for Phases 1–4 landed in one pass — all
phases compile clean and load via the existing kpatch pipeline; in-game
behaviour is **untested**. New TUs:
- `patches/Accessibility/combat.{h,cpp}` — Phase 1A combat-mode poll,
  Phase 1B combat-log poll, Phase 4A per-attack poll, Phase 4B saving-
  throw stub (no-op until the SavingThrowRoll hook lands).
- `patches/Accessibility/combat_query.{h,cpp}` — Phase 2A PC stat
  block (Shift+S + auto-on-leader-change), Phase 2B target enrichment
  (consumed by `passive_narrate.cpp`), Phase 2C Examine hotkey
  (Shift+H + ShowExamineBox call + per-tick panel reader).
- `patches/Accessibility/combat_queue.{h,cpp}` — Phase 3A action-queue
  submenu (Shift+K opens; Up/Down navigate; Enter removes tail entry,
  non-tail returns "cannot remove" until positional-remove primitive
  is found; Shift+Enter wipes; Esc closes).
- `patches/Accessibility/dialog_speech.{h,cpp}` — Phase 1D NPC line +
  reply count + bark bubble polls.
- `patches/Accessibility/menus_listbox.cpp` — five new
  `ListBoxPanelSpec` entries for `InGameMessages` and four dialog
  variants (Phase 1C/1D listbox plumbing).

Skeleton gaps explicitly deferred (each tagged in code with a TODO):
- Hook-based delivery for Phase 1B (`AddMessages @0x626920`), Phase 1D
  (`SetDialogMessage @0x6a7010`, `SetReplies @0x6a86a0`,
  `SetBark @0x6a9920`), and Phase 4 (`ResolveAttack @0x5bba80`,
  `SavingThrowRoll @0x5b92b0`). Polling delivers the same data one
  frame later — acceptable for skeleton; hook upgrade is a one-line
  wiring change once bytes are captured.
- `attack_result` enum values + `action_type` byte enum values are
  inferred (placeholders in `engine_offsets.h`); validate via the
  one-shot probe session listed below before relying on the verb /
  outcome strings.
- Faction relation classification (Phase 2B "open"): currently every
  non-dead creature reads as "neutral" until `CSWSFaction` is
  decoded.
- Non-tail queue removal (Phase 3A): only tail-remove and full clear
  work; mid-queue Enter speaks "cannot remove this action."
- InGameMessages dialog-history view toggle: spec routes to
  `messages_listbox` only; `dialog_listbox` swap pending engine-side
  toggle-button state read.

### Phase 1 — Free wins from existing engine infrastructure

Three landing pads, all reusing infrastructure we've already built. None
of these need new RE; the offsets and addresses are in the sections above.

**1A. Combat-mode entry/exit announcement.**

- Hook `CClientExoAppInternal::UpdateCombatMode @0x5f3ad0`.
- Read `CClientExoApp::GetCombatMode @0x5ede70` after the call; debounce
  via the stability-debounce pattern (already in `turn_announce.cpp`) so
  fast on/off cycles collapse to a single edge.
- Announce via `strings.h` (German + English): "Combat begins" /
  "Combat ends".
- Tiny scope; ships first because everything else benefits from having
  a "we're in combat" signal.

**1B. Live combat-log narration.**

- Hook `CSWGuiInGameMessages::AddMessages @0x626920`.
- Pull each appended row's text from `messages_listbox @+0x64` (same
  reader as the rest of `menus_listbox.cpp`) and pipe to TTS.
- Honour vanilla feedback verbosity — the user already toggles it in
  options, no new setting needed.
- Trade-off acknowledged: one frame later than `Broadcast*Data`, but
  free localisation and zero new formatter code.

**1C. Messages-panel review-on-demand.**

- Register a `ListBoxPanelSpec` for `CSWGuiInGameMessages` pointing at
  `messages_listbox @+0x64` (combat feedback) and a second spec
  pointing at `dialog_listbox @+0x344` (dialog history).
- Reuse the existing `titleOverride` / `enrichRow` extension points
  (per `project_listbox_spec_extensions.md`).
- Wire `show_button @+0x76c` swaps so the active spec follows the
  user's view-toggle.

**1D. Live dialog screen.**

- Register a `ListBoxPanelSpec` for `CSWGuiDialog` pointing at
  `replies_listbox @+0x19c4`.
- Hook `CSWGuiDialog::SetDialogMessage @0x6a7010` to speak the NPC
  line on each new node.
- Hook `CSWGuiDialog::SetReplies @0x6a86a0` to announce "N replies
  available" as a context-change cue.
- `enrichRow` reads the per-row active flag (offset open) so inactive
  replies are spoken with a "(unavailable)" suffix.
- Variants: add specs for `CSWGuiDialogCinematic` (same shape) and
  `CSWGuiDialogComputer` (two listboxes — `message_listbox @+0x2cfc`
  and the embedded `replies_listbox`).
- Bark bubbles: hook `CSWGuiBarkBubble::SetBark @0x6a9920` and pipe
  the bark string straight to TTS (no panel; ephemeral).

Phase 1 deliverable: combat events read aloud as they happen, dialog
fully navigable by keyboard with full speech, full review screens for
both. Together this covers the bulk of the
"can-the-blind-player-finish-Taris" question without needing any
structured combat reads.

### Phase 2 — Stat / target snapshot hotkeys

Read-side, no event hooking. All paths land on
`engine_player::GetPlayerServerCreature` (already built) and
`engine_area::AreaObjectIterator` (already built).

Two-channel design:

- **Selected PC** — full stat block on demand (and on party-leader
  switch). Three party members max — fully tractable.
- **Opponents** — enrichment of the **existing Q/E target-cycle
  announcement** (per `project_engine_native_qe_cycle.md`,
  passive_narrate already watches LastTarget); cycle gives a brief.
  A second Shift+H hotkey gates the engine's full **Examine** panel
  for everything the brief leaves out.

This mirrors the door pattern — the existing cycle announcement gets
enriched with status info (analog to "door, locked"), and a separate
hotkey opens the deeper read.

**2A. Selected-PC full stat block.**

Scope: announce **everything the sighted player can read off the
HUD/character sheet for the selected character, except equipment**
(the blind user equipped it themselves; redundant). Order tuned for
combat-relevance:

- Name + class + level.
- HP current / max, FP current / max.
- AC.
- Six attribute totals (STR/DEX/CON/INT/WIS/CHA).
- Three saves (Fort / Reflex / Will).
- Alignment (light / dark / neutral).
- Active status effects (buffs + debuffs from
  `CSWSObject.effects @+0x124`).

Source path: `CSWSCreature.creature_stats @+0xa74` →
`CSWCCreatureStats` field reads. No engine calls needed for the
numeric fields; effects list walks `CSWSObject.effects @+0x124`.

Triggers:
- Configurable hotkey (default open) — read on demand.
- Auto-read on party-leader switch (we have leader-change detection
  already in `engine_player::GetActiveLeaderName`).

**2B. Opponent cycle-announcement enrichment.**

Hook into the **existing Q/E cycle** announcement path
(`passive_narrate.cpp` watches `CClientExoAppInternal.last_target
@+0x2b4`). When the new target is hostile or neutral, append
combat-relevant fields to the cycle announcement — the same shape we
already use for doors ("door, locked").

Tier 1 + Tier 2 + Tier-3-numbers, in the cycle line:

- Name (already announced).
- Faction (hostile / neutral / friendly) — color-ring equivalent.
- HP current / max — the exact numbers, replacing the bar's
  high/medium/low abstraction.
- AC.
- Main-hand weapon (item name only — no full property dump on cycle).
- Active status effects (one summary, e.g. "stunned" /
  "force-shielded" / "on fire").
- Alive / dying / dead state.

Sources: same `creature_stats @+0xa74` path; weapon via
`CSWCCreature.main_hand_id @+0x230` → `CSWSItem*` resolution + name.
Status from the effect list.

Out of scope for the cycle line (too verbose for a per-cycle event):

- Attribute scores, saves — defer to 2C Examine.
- Off-hand weapon, armor — defer to 2C Examine.
- Description / class / level — defer to 2C Examine.

**2C. Shift+H Examine hotkey.**

The engine ships an Examine panel — `CSWGuiExamine` (functions at
0x6ce9c0/0x6cea30, opened via `CGuiInGame::ShowExamineBox @0x62d3e0`,
closed via `HideExamineBox @0x62d440`). Internally it's a thin wrapper
around a message box (`CSWGuiExamine.message_box @+0x0`) that the
engine populates with a fully-formatted, fully-localised stat block
covering everything 2B intentionally skipped.

**Live panel layout (validated 2026-05-10, `patch-20260510-000722.log`
panel-walk dump):**

- panel `controls.size = 6`
- `[0]` NULL
- `[1] id=1` — listbox (vtable=0073E840) — **the rendered stat block;
  one row per line of text**
- `[2]` NULL
- `[3] id=3` — button "Schliess." (Close)
- `[4] id=4` — button "Abbrechen" (Cancel)
- `[5] id=-1` — label-like control (vtable=0073E5B8); contents vary
  per examine (e.g. "Laserschwert werfen" — possibly a
  default-action label)

The reader in `combat_query::TickExaminePanel` walks `[1]`'s rows and
concatenates them; the original "first non-empty short text" walk
landed on `[3] / [4] / [5]` and read the button label / default-action
label, not the actual stat block.

Plan:

- Bind a configurable hotkey (proposed: Shift+H) to drive
  `ShowExamineBox` for the currently-cycled target (LastTarget).
- Hook `ShowExamineBox` itself so that whenever the panel comes up
  — by our hotkey *or* by any vanilla path — its `message_box` text
  is read aloud. Reuses the existing message-box reader from
  `menus.cpp`.
- Register a `ListBoxPanelSpec`-equivalent or message-box spec for
  `CSWGuiExamine` so the user can navigate the rendered text the
  same way they navigate other panels (scroll / re-read).
- The user closes Examine with the standard exit key; nothing extra
  to wire.

The user-facing model: cycle through enemies for the brief
("hostile Sith Trooper, 23/45 HP, AC 14, vibrosword, stunned"); hit
Shift+H when they want the full detail. Same two-tier shape sighted
players use (HUD glance vs. click-to-Examine).

**Sub-decision deferred**: whether to also expose the engine's
auto-Examine-on-mouseover behaviour (vanilla shows a tooltip with
short data on hover); probably yes once 2B is shipped, since the
short tooltip text is a third tier between cycle and Examine.

### Open for Phase 2

- open: where exactly is the per-row "active status effect" name
  resolved — TLK strref via the effect list, or an inline string?
  Affects how 2A + 2B speak buffs/debuffs.
- open: faction → "hostile / neutral / friendly" mapping. Likely a
  `CSWSFaction` reputation threshold; needs a one-shot probe.
- open: confirm `ShowExamineBox` accepts the LastTarget handle as-is
  (server- vs client-side handle namespace).
- open: alignment good/evil read path —
  `CSWSCreatureStats::GetSimpleAlignmentGoodEvil @0x5a5110` exists;
  validate it returns the displayed value.

### Phase 3 — Action-queue submenu

The active-scope plan only covers **reading + manipulating the queue**
as a navigable menu — not announcing every queued add/remove or every
performed action. Layer 3 (Phase 1B) already announces performed
combat actions (attacks/spells/feats) for free; we lean on that for
"action just executed" coverage and add only what Layer 3 misses.

**3A. Queue submenu — same shape as the action-bar Shift+4..7 menu.**

The user opens a hotkey to enter the submenu for the controlled
character's queue (analog to `actionbar_menu` for the action-bar
columns — see `patches/Accessibility/actionbar_menu.h` for the
established pattern). Inside the submenu:

- **Open**: speaks a pre-roll like "Cue: 3 actions" and arms the
  input gate. If the queue is empty, speaks "Cue ist leer" and
  leaves the gate disarmed (same shape as `actionbar_menu::Open`).
- **Up / Down**: cycles through queued actions. Each focused entry
  is announced as "Power Attack on Malak" / "Cure on Bastila" /
  "Move" — `action_type` byte mapped to a human-readable verb,
  target resolved to a name via `engine_area::ResolveServerObjectHandle`.
- **Enter** on a focused queued action: removes that single action
  from the queue. Speaks "removed: Power Attack" and re-announces
  the new focus (or closes the menu if the queue is now empty).
- **Shift+Enter**: clears the entire queue and auto-closes the
  menu. Speaks "cue cleared".
- **Esc**: closes without changes.
- **Self-disarm**: closes when combat ends, area transitions, or
  the resolve chain breaks (same trigger set as the existing
  submenus).

Source path: walk `CSWSCreature.combat_round.actions` (linked list
at CSWSCombatRound +0x9b0) for the controlled character. Per-node
fields needed: `action_type @+0x10`, `target @+0x14`,
`inventory_slot @+0x1c`, `move_to_position @+0x38`.

Engine call surface for the manipulation verbs:

- **Clear all** — `CSWSCombatRound::RemoveAllActions @0x4d3770` or
  the higher-level `CSWCObject::ClearAllQueuedCombatActions @0x63d490`
  (client-side wipe). Either works; pick the cleaner of the two
  during implementation.
- **Clear one** — *open*. The named primitives are `RemoveLastAction
  @0x4d37b0` (only removes the most recent) and `RemoveSpellAction
  @0x4d3ba0` (specific case). There's no obvious "remove at index N".
  Likely implementation paths: (a) find a positional remove primitive
  in SARIF that the named exports missed, (b) splice the linked
  list manually (risky — engine bookkeeping fields may need
  updating), (c) repeat-RemoveLast while saving + re-queuing the
  trailing actions (preserves order, slow but engine-safe).
  Investigate before locking down the Enter behavior.

**3B. Probable / deferred — explicit "performed" reads.**

Layer 3's combat-message buffer already announces performed attacks,
spells, and feats. Two action types fall through that net and may
deserve our own readouts:

- **Move** — Layer 3 doesn't announce "Carth: moves to X". Probable
  add: hook `CSWSCombatRound::SetCurrentAction @0x4d2b40` (or watch
  the `current_action` byte) and emit a localised "moves" cue when
  the activating node's `action_type` is the Move type. Defer until
  the action_type enum probe lands; reassess if it actually feels
  needed in play.
- **Equip / unequip mid-round** — also silent in Layer 3. Same
  pattern as Move; same deferral.

Both are explicitly out of the active-scope Phase 3 plan; we'll
revisit after Phase 1B is shipped and we have real-play feedback on
whether the silence around Move/Equip is actually a problem.

**Prep**: one-shot probe session that hooks each `AddX` adder
(`AddAttackAction`, `AddSWSpellAction`, `AddEquipAction`,
`AddCutsceneAttackAction`, `AddMoveToPointAction`-equivalents) with
DumpBytes on `CSWSCombatRoundAction +0x10`. Pins the enum so 3A's
verb table and any future 3B work both have what they need.

### Phase 4 — Structured combat callouts

Higher cost than Phase 1B because we build the formatter ourselves;
delivers richer info than the message buffer path can.

**4A. Per-attack resolved callout.**

- Hook `CSWSCreature::ResolveAttack @0x5bba80` (or one of the
  SubAttack resolvers if we want per-sub-attack granularity).
- Post-call read of `CSWSCombatAttackData.{attack_result,
  base_damage, react_object}` from the round's
  `attacks_list[7]` slot.
- Resolve target via `react_object`; resolve the result enum via the
  table we lock down in Phase 4 prep.
- Build a localised string: "Hit Malak for 12 damage; Malak at 47/82
  HP".

**4B. Saving-throw callout.**

- Hook `CSWSCreature::SavingThrowRoll @0x5b92b0` or
  `BroadcastSavingThrowData @0x4ec760`.
- Announce "Bastila resists fire damage (Reflex 17 vs DC 14)".

**4C. Critical hit / deflection / death callouts.**

- Likely deferred until 4A and 4B are stable; reuse the same
  per-attack hook + branch on the post-resolution flags.

Prep: validate `attack_result` enum (open question in Pillar 2).
Without that, 4A can't disambiguate hit/miss/crit/deflected reliably.

### Phase 5 — Combat tutorial / discoverability

Once the mechanical channels are reliable, layer on:

- **Initiative / turn order announcement** at round start
  (`CSWSCombatRound::StartCombatRound @0x4d5f70`).
- **Round-summary bookend** at round end
  (`CSWSCombatRound::EndCombatRound @0x4d4620`).
- **Tactical hints** built on existing surfaces:
  `CSWSCreature::GetNearestEnemy`, `GetIsWeaponEffective`, etc.

Everything in Phase 5 is opportunistic — no new hooks required, just
new uses of the data we've already exposed.

### Out of scope (for now)

- New combat UI overlays beyond what the engine renders. Stick to the
  vanilla widgets so screen reader behaviour matches sighted-player
  context.
- Combat camera control / pause-on-event behaviour. The user can
  already toggle these in vanilla options; we don't replace them.
- Multiplayer / coop concerns. Single-player only, matching the rest
  of our scope.

### Risks & dependencies

- **Phase 1B / 1C / 1D depend on `CSWGuiInGameMessages.AddMessages` /
  `CSWGuiDialog.SetDialogMessage` being reliable hook points.** We
  haven't decompiled them yet — open. If either turns out to be
  inlined or called from an unfortunate context, we fall back to a
  per-tick `messages_listbox` change-detector (slower, still works).
- **Phase 3 + 4 both gate on enum validation.** Without
  `action_type` and `attack_result` pinned, we can announce "queued
  action" / "attack resolved" but not the *what*. Plan a single
  probe session before Phase 3A to lock both at once.
- **The Messages panel's combat-log entries respect vanilla
  feedback-verbosity options.** If the user disables a category
  there (e.g. "show feat results"), we lose those events from our
  TTS too. Document this; mention in the in-game accessibility help
  that vanilla feedback options gate our verbosity.

---

## Open questions worth a one-shot probe session

- Validate `CSWSCombatAttackData.attack_result` enum values.
- Validate `CSWSCombatRoundAction.action_type` byte enum values.
- Confirm `attack_debug_text` / `damage_debug_text` are populated in the
  retail build (vs. debug-only).
- Confirm `CSWGuiOptionsFeedback`'s feedback-level setting actually drives
  the message buffer's verbosity.
- Find the "Critical Hit!" / "Miss" / "Deflected" floaty-text TLK strrefs
  (the engine displays them during combat — they're somewhere).
- Decide whether we want to surface server-side broadcast (richer, raw
  numbers, untimed) or client-side display (already localised, paced,
  gated on user options) as the primary channel.

---

## Cross-references

- `docs/llm-docs/accessibility-map.md` — overall accessibility surface map
  (this doc is its combat-pillar deep dive).
- `docs/llm-docs/re/swkotor.exe.h` — Lane's struct definitions
  (CSWSCombatRound at line 10083, CSWSCombatAttackData at 9933,
  CSWSCombatRoundAction at 22829, CSWSCreature at 14074, CSWGuiMainInterface
  inline structures around 9907 / 10194).
- `third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db` — the
  symbol DB this whole doc cross-references; query with `sqlite3` for any
  function/offset not listed here.
- `patches/Accessibility/engine_player.{h,cpp}` — server-creature chain
  walker we'll reuse (already returns the player CSWSCreature).
- `patches/Accessibility/engine_area.{h,cpp}` — area-object iterator;
  reusable for "walk all hostile creatures in current area" reads.
- `docs/upstream-prs.md` — keep PR opportunities for KPatchManager bugs we
  hit during combat-pillar implementation.
