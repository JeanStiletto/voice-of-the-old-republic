# low-level-cleanup — round 1 record

Run 2026-05-28 against `patches/Accessibility/`. Triage via 16 parallel subagents grouped by
module (action bar+radial, audio, camera+compass, combat, core+log, cycle+narrated_target,
speech/msg/prism, engine area/nav/offsets/options, engine UI cluster, focus/announce, guidance,
input, map+party, menus core+chargen, menus other, probes, spatial+wall+strings+misc). Pruned a
~30-item raw set (working notes preserved in `low-level-cleanup-raw-findings.md`) to a 15-item
shortlist; user approved bulk-execute. All 15 shipped; in-game test (map cursor, Shift+- autowalk,
Ctrl+- beacon, examine view, walk/footstep audio) confirmed green.

Net diff across 13 files: **+167 / −408 = −241 lines**.

## Done (15 commits)

### A. Dead code

1. **`engine_offsets.h` — orphan combat-attack-data constants** (`6d185d2`) — 24 lines.
   `kCombatAttackDataStride`, `kCombatAttackDataCount`, the seven `kAttackData*` per-entry
   offsets, and the five `kAttackResult*` enum values were sibling fields of the
   `TickAttackResolutions` cascade deleted in commit `f3a0b2a`. Zero references after that
   deletion; the live `kCombatRound*` block above stays. Flagged independently by 2 subagents
   (combat + engine_offsets) — matches the prediction at the end of `bloat-audit-round-1.md`.

2. **`menus_chargen_attr.cpp` — empty `namespace {}` block** (`8131285`) — 4 lines.
   Vestige of the `menus_chargen_layout` extraction (`6f2b6e0`); three lines of empty
   anon-namespace between two adjacent blocks.

3. **`log.cpp::FindEntry` inline-and-delete** (`4c2c7de`) — net −6 lines (2 added / 8 removed).
   Only call site was `GetOrCreateEntry`; fold the 5-line loop into its preamble.

4. **`examine_view.cpp::TickEnginePanelLifecycle` deleted** (`648e298`) — 66 lines.
   Pure-diagnostic watcher emitting `Examine.Panel opened/closed` + row-count log lines.
   User-facing speech is owned by `menus_listbox::kExamineSpec` (rows) and synthetic-view speech
   is gated on `g_state.active`. The kept constant `kExaminePanelListBoxOffset` is still used by
   `menus_listbox.cpp`.

5. **`core_dllmain.cpp::DumpFunctionBytes` + 6 call sites** (`fc28e7b`) — 34 lines.
   RE scaffolding that dumped runtime-decrypted bytes for ShowLevelUpGUI / AppendToMsgBuffer /
   AddFloatyText / ShowFlashingStatus / SetCombatMessage / AppendToDialogBuffer during early
   investigations. All those paths are RE'd. The header said "Removable in one commit."

6. **`guidance_autowalk.cpp` — WalkTo pre-dispatch field427/field101 snapshot** (`8209cb0`) —
   54 lines. The 2026-05-07 diag block sampled creature fields before and immediately after
   `AddMoveToPointAction`; the watchdog at `t+1s` does the same read with more elapsed time and
   produces the same signal. Investigation closed: leader walks go through `UseObject` per
   memory `project_addmovetopoint_leader_broken`. Watchdog untouched.

### B. Local duplication / boilerplate

7. **`engine_panels.cpp::ProbeReadGuiString` replaced with `engine_reads::ReadGuiString`**
   (`1e59f9e`) — net −27 lines. The trimmed local mirror claimed circular-dep avoidance, but
   `engine_reads.h` only includes `engine_offsets.h` — no menus dep exists. Canonical helper
   also writes an SEH-fault trace, useful for the probe site.

8. **`engine_area.cpp` walkmesh-edge scan helpers** (`8b77317`) — net −1 line, big readability
   win. `ScanRoomWallEdges` and `ScanRoomAllTriangleEdges` shared per-face uint32 vertex-index
   read, `LocalToWorld` with SEH fallback, and the 5cm² XY-length filter. Extracted as
   anon-namespace helpers (`ReadFaceVertexIndices`, `TransformEdgeEndpoints`,
   `kMinEdgeXYLengthSq`).

9. **`engine_area.cpp::GetObjectDisplayNameByHandle` reuses `IsSentinelHandle`** (`65cac7e`) —
   2 lines. The anon-namespace helper already factored the three-value sentinel check for the
   two Resolve* paths; this site still inlined the same triple-OR.

10. **`cycle_input.cpp` empty-slot fallback helper** (`26b1251`) — net −13 lines.
    `OnAnnounceFocus`/`OnPathfindFocus`/`OnBeaconFocus`/`OnPathfindFocusForce` all carried the
    same `ResolveNarratedActivation→fail→speak GuidanceNoFocus→log→return` shape, differing only
    in their log-tag prefix. Hotkey bodies now start with
    `if (!TryResolveOrAnnounceNoFocus(a, "Tag")) return;`.

11. **`map_ui_cursor.cpp` ambient-text resolution dedup** (`0582365`) — net −9 lines.
    `Tick()` had two near-identical switch-on-AmbientKind ladders rendering the same labels.
    Pulled into `ResolveAmbientText(kind, roomIdx, landmarkBuf, shapeText, out, cap)`; the
    dedup-fingerprint pass passes `shapeTextLocal` (current frame), the hover-pause speak pass
    passes `g_state.pending_shape_text` (stashed) so the spoken label matches what armed the
    timer.

12. **`map_ui_cursor.cpp` route activation through `ResetSessionState`** (`afab34b`) — net
    −9 lines. The activation block inlined 11 of the 15 field resets that the helper already
    did. The two omitted fields (`announced_area_name`, `last_edge_cue_ms`) are correct to
    reset on re-activation — area-name re-announcement on each map-open is the intended
    behaviour per the surrounding comment.

### C. Clarity wins

13. **`filter_objects.cpp::IsMapCycleable` switch → one-liner** (`b22fa5e`) — net −11 lines.
    6 same-return cases + 1 Landmark → `return c == CycleCategory::Landmark;`. Rationale
    comment kept.

14. **`engine_area.h/cpp` name MapPin flags + subtype offsets** (`d0a3422`) — net +2 lines
    (clarity win, not a line saver). Added `kMapPinFlagsOffset = 0x108` and
    `kMapPinSubtypeOffset = 0x10c` alongside the existing `kMapPin*` block; swapped the two
    hardcoded literals in `GetMapPinFlags` and `CreateMapPin`.

15. **`engine_options.cpp::ToggleMouseLook` resolves `GetClientOptions` once** (`39a65b3`) —
    net +17 lines (refactor adds a helper). Extracted the bit write to a file-scoped
    `WriteMouseLook(options, enabled)` helper so `SetMouseLook` and `ToggleMouseLook` share
    the mutation. `ToggleMouseLook` reads + writes under the single resolved pointer instead
    of walking AppManager → CClientExoApp → CClientExoAppInternal → CClientOptions twice.

## Dropped from shortlist during pruning

- **engine_manager iteration cap harmonisation** (16 / 32 / 256 inconsistency, FindOwningPanel=16
  may miss CSWGuiInGameCharacter children) — changing limits risks observable behaviour change;
  warrants separate investigation, not a low-level pass.
- **swoop_race SafeRead* template** (4 functions sharing __try shape) — abstraction risk on
  hot per-tick reads exceeds savings.
- **probe micro-cleanups** (kRadToDeg, kFloatEpsilon file-const, IsValidPtr helper) — each <10
  lines, low value, probes themselves are diagnostic-only.
- **view_mode AnnounceCursorRegion text-equality consolidation + region-reset lambda** — low-value
  polish on a complex state machine.
- **same_name_suffix SafeStringCopy helper** — 5-line savings on safety-critical code path.
- **transitions.cpp landmark-scan extract / ResolveRoomSpeech outSig return / Platz struct
  grouping** — touches speech path, too risky for a low-level pass.
- **guidance_beacon dead defensive bounds check** — removing defensive guards in state-machines
  rarely worth it.
- **menus_pending Queue*() boilerplate** (11 functions × 3-5 lines identical setup) — the
  subagent itself recommended against macro extraction (readability cost).
- **engine_player::GetPartyMembers wrong-base** — already filed in `docs/known-issues.md` Bugs.

## Subagent miscounts caught during pruning

- The `engine_panels::ProbeReadGuiString` subagent flagged "circular dep with menus layer" as
  the design rationale; verification showed `engine_reads.h` only includes `engine_offsets.h`,
  so the dep doesn't exist. The Probe* mirror was unnecessary from day one.
- The `engine_options::ToggleMouseLook` agent's recommended fix was "cache options once, call
  SetMouseLookDirect" — pursuing that would have left `SetMouseLook` callerless. Adjusted to
  extract a private `WriteMouseLook` that both public functions share.
- The `examine_view::TickEnginePanelLifecycle` subagent's claim that the constant
  `kExaminePanelListBoxOffset` could go was wrong — `menus_listbox.cpp` still uses it. Constant
  preserved.

## Notes for future rounds

- After this round, `engine_options.cpp::SetMouseLook` has zero callers (only `ToggleMouseLook`
  used to call it; now it walks the bits inline via the shared `WriteMouseLook`). Public API
  surface that could be trimmed next round if no external users surface.
- `core_dllmain.cpp::OnRulesInit` is now just `EnsurePrismInitialized + one log line`. The
  static `fired` guard is technically redundant (Prism init has its own done-latch and the log
  layer dedups identical lines via `acclog::Once`), but removing it changes a "fire once" vs
  "fire-once-clear-log" contract that wasn't part of this round's scope.
- The `engine_area.cpp::ScanRoomWallEdges`/`ScanRoomAllTriangleEdges` extraction left both
  scan functions reading their distinguishing logic clearly. Future rounds touching wall
  topology can lean on `TransformEdgeEndpoints` / `ReadFaceVertexIndices` rather than re-spelling
  the patterns.
- `map_ui_cursor.cpp` is still the largest single TU in the patch (~1200 lines). The ambient-
  resolution extraction shrunk the busiest function but the file as a whole remains a candidate
  for a large-file-handling pass.
