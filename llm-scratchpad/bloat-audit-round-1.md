# ai-bloat-audit — round 1 record

Run 2026-05-28 against `patches/Accessibility/`. Triage via 4 parallel subagents (engine wrappers, large >1000-line files, chargen near-duplicates, recent splits + probes + diags + tiny files). Returned 13 raw findings; validated to 11 shortlist items; user approved 10 (1 deferred).

## Done (10 commits)

1. **`diag_play3doneshotsound` module deleted** (`c7aa323`) — header self-documented removal contract; hook commented out in `hooks.toml`; per-step humanoid footstep audio caller already RE'd (now `OnPlayFootstep` at @0x0061a31a). ~80 lines + export + commented-hook block + cross-references.

2. **`combat::TickSavingThrows` deleted** (`804b0b7`) — body was literally `return;` waiting for a SavingThrowRoll hook. Already flagged in archived `hook-vs-poll-audit.md` F3. ~25 lines.

3. **`engine_reads::ResolveItemFromServerHandle` deleted** (`87329e4`) — near-duplicate of `ResolveItemFromClientHandle` minus one translation step; zero callers. ~38 lines.

4. **`engine_area::GetRoomAt` (non-indexed) deleted** (`394db2d`) — all 14 callers use `GetRoomAtIndexed`. ~12 lines.

5. **`spatial_change_detector::GetWallSurfaceCount`/`GetWallSurfaceDesc` deleted** (`8752624`) — two of the five legacy thin-wrappers from the recent `spatial_wall_surfaces` split had no callers. Other three wrappers kept (live consumers). ~9 lines.

6. **`wall_topology::KindTransition` enum value deleted** (`414691b`) — explicitly documented as "reserved/unused — cross-room degree-2 detection was retired". ~3 lines.

7. **`combat::TickAttackResolutions` cascade deleted** (`f3a0b2a`) — 252-line per-tick AreaObjectIterator-style block whose terminal speech was commented OFF (replaced by `OnAppendToMsgBuffer` hook per memory `project_appendtomsgbuffer_is_combat_log`). Was a stale cross-check.

8. **`engine_reads::ReadButtonText`/`ReadLabelText`/`ReadLabelTextAt` hoist** (`2234a17`) — same SEH-guarded two-step pattern (ReadGuiString → ExtractTextOrStrRefIndirect) at 6 sites (chargen_attr/skills/feats, powers_levelup, menus_listbox, dialog_speech) consolidated to one canonical pair in engine_reads. Net −171 lines across 8 files. `engine_radial.cpp::ReadButtonText` deliberately NOT collapsed (uses file-private `*Local` primitives for a documented cross-module-include avoidance).

9. **`menus_chargen_layout.{h,cpp}` extracted** (`6f2b6e0`) — `IsPanelOfVtable`, `IndexFromButton`, `RowPitchFromButtonExtents` shared by `menus_chargen_attr` + `menus_chargen_skills`. Per-panel public API preserved as thin wrappers. Net −56 across attr/skills, +75 in the new TU; ~20 source lines net saved, 1 new build target.

10. **`docs/known-issues.md` Bugs entry for `engine_player::GetPartyMembers`** (`2134943`) — wrong-base read (facade + 0x1b770 instead of internal + 0x1b770) that the file's own header documents as a bug pattern. Three consumers (combat_queue / combat_special_watch / party_cache) currently rely on it. Filed for follow-up rather than fixed inline during the audit.

## Deferred

- **`probe_camera_state` deletion** — subagent flagged investigation as concluded; user is still using the probe.

## Subagent miscounts caught during validation

- The `menus.cpp` listbox handlers in the original `large-file-splits.md` plan were claimed live but were dead (also subsequently tombstoned).
- The audit subagent counted `engine_radial::ReadButtonText` as a duplicate of the chargen `ReadButtonText` pair, but it uses a different primitive (`ReadGuiStringLocal` instead of `acc::engine::ReadGuiString`). Correctly excluded from the hoist.
- The audit subagent claimed `combat_query::HotkeyShiftH` / `PollWin32Hotkey` were live — they had no callers (already cleaned up in the prior `combat_query` cleanup commit `3099e24`).

## Notes for future rounds

- After `low-level-cleanup.md` lands, several engine-offset constants under `kCombatAttackData*` / `kAttackResult*` / `kCreatureCombatRoundOffset` may be orphaned by the TickAttackResolutions deletion — audit pass should catch them.
- The `engine_player.h:170` legacy alias `kServerExoAppPartyTableOffset` survives only because `GetPartyMembers` still uses it; deleting it forces the bug fix from item 10.
