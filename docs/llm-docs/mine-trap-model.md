# Mine / trap model (engine-confirmed)

How KOTOR 1 traps ("mines") are detected, shown to sighted players, and
fired. Decompiled 2026-07-16 from Lane's gzf via `tools/ghidra-scripts/
decomp.sh`; raw dumps preserved in the session scratchpad, reproduce with
the addresses below.

## Status: known / suspected / open

### Known (decompiled)

**Traps are world-space trigger objects ("mines"), avoidable, not internal
machinery.** `CSWSTrigger::OnEnterMine @0x0058d570` fires only when a
creature actually enters the trigger polygon AND `GetCanFireMineOnObject
@0x0058d4a0` passes. Firing runs the trigger's TrapTriggered script and
(if `trap_one_shot`) queues a destroy event. A creature carrying an item
whose tag matches the trigger's `key_name` bypasses the blast (custom
script path, optional `auto_remove_key`). So walking around the polygon
avoids the trap completely — exactly what a sighted player does after
seeing it.

**Detection is a periodic server-side Awareness check —
`CSWSCreature::UpdateMineCheck @0x004fa390`:**
- Cadence/range have two tiers, selected on `detect_mode` (+`animation ==
  10000`): active tier = every 100 ms with radius² 400 (20 m), passive
  tier = every 3000 ms with radius² 9.0 (3 m).
- Roll: `d10` (or `d10 + 10` in the favoured mode branch) + Awareness
  skill rank (`GetSkillRank(stats, 3, …)` — skill 3 = Awareness),
  compared against the trap's detect DC (`detect_dc_mod` on triggers,
  `trap_detect_dc` on placeables, byte at +0x2ed on doors).
- Gates before the roll: `trap_detectable == 1`, distance to the trigger's
  NEAREST GEOMETRY point (`CSWSTrigger::CalculateNearestPoint`) within the
  tier radius, reputation of the trap toward the creature < 90 (friendly
  mines are never "detected"), and faction differs.
- Already-flagged traps (`field24_0x2c8 == 1`, cf. `AIActionFlagMine`)
  skip the roll and are auto-known.
- The same loop also checks **trapped doors and trapped placeables** —
  the area keeps a dedicated trapped-object list at `CSWSArea +0x12c`
  (data) / `+0x130` (count) that the loop iterates; all three kinds carry
  their own detected-by bookkeeping.

**Detected state is a per-trap "detected-by" id list.** On success the
creature's id — or, when the detector belongs to the player party
(`field426_0xa88 == 1`), EVERY current party member's server id — is
`AddUnique`d into the trap's detected-by `CExoArrayList<ulong>`:
- CSWSTrigger: data at `+0x2a8`, count at `+0x2ac`
- CSWSDoor: data at `+0x2dc`, count at `+0x2e0`
- CSWSPlaceable: data at `+0x318`, count at `+0x31c`
The list is checked first on every pass, so each trap is rolled at most
once per creature; membership IS the engine's "this trap is visible to
that creature" fact.

**Sighted players really see detected mines.** On first detection (not
already flagged) the server builds a message with (roll, skill rank, DC)
and `CSWSCreature::BroadcastMineDetectionData @0x004ec840` sends CCMessage
type 0x13 to every player client in the detector's faction in the same
area. Client side, `CSWCTrigger::DisplayTrigger @0x00691470` /
`UpdateTriggerColor` render the trigger polygon as a coloured translucent
ground overlay: hostile mine = colour 0 (red), area transition = colour 2
(blue), generic = colour 1 (yellow). `SpecialDisplayTrigger @0x00691e00`
is the hover/highlight variant (yellow 0.35 alpha for plain triggers, blue
0.25 for transitions) via `AurPartTriggerSetHighlightParams`. Detected
mines also gain action-menu verbs (`CSWCTrigger::ActionMenuDisableMine`,
`ActionMenuRecoverMine`, `CSWSCreature::AddMineActions`).

### Suspected (typed but not fully traced)

- `detect_mode` (`CSWSCreature +? via GetDetectMode @0x004edae0` /
  `SetDetectMode @0x0050ee30`) is the NWN-heritage "actively searching"
  state; which player actions set it in K1 (walking? stealth? solo mode)
  is untraced. `animation == 10000` in the tier check is likely the
  idle/paused animation id.
- Trap display name: trap_type indexes `traps.2da`
  (`Load2DArrays_Traps`), which carries a TLK strref for the localized
  mine name ("Splittermine" etc.); `GetObjectName` on a detected trigger
  is expected to return it (the Examine GUI path
  `SendServerToPlayerExamineGui_MineData @0x0056f160` reads the same).

### Open

- Exact bit meanings of the favoured-roll condition chain (d10 vs d10+10
  branch) — decompile is boolean-mangled; doesn't matter for parity work.
- Whether flagged (`FlagMine`d) traps render for players who never rolled.

## Implementation (2026-07-16)

`trap_watch.cpp` mirrors the detected-by lists (250 ms poll +
`ScanNow` race-closer): ground-mine detections queue for
`combat.cpp::RuleMineDetect`, which enriches the engine's TLK-42132
feedback line (matched locale-independently on the mine's localized
object name, roll math stripped, clock direction + metres appended);
trapped doors/placeables — which get NO engine line — announce as
"Falle entdeckt: <name>, auf X Uhr, Y Meter"; detected mines within 4 m
speak one non-repeating proximity warning (re-arm at 8 m). Party-id ∩
detected-by test in `engine_area::TrapDetectedByAnyOf`.

## Accessibility implications

Parity = mirror the engine's own detected-by list, NOT reimplement
detection: poll the trapped-object list of the current area (or filter
kind==Trigger objects with trap fields), and treat a trap as "visible"
exactly when any party-member server id is in its detected-by list. That
inherits Awareness rolls, party-wide sharing, flagging, faction rules —
every rule above — for free, and can never spoil undetected traps.
Detection moment (list count transition) is pollable; no hook needed
(fits the hook-vs-poll rule).
