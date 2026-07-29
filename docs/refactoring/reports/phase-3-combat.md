# Phase 3 scan — combat subsystem

Scope: `patches/Accessibility/` combat.cpp (313), combat.h (44),
combat_log.cpp (1215), combat_queue.cpp (834), combat_queue.h (78),
combat_queue_hooks.cpp (73), combat_query.cpp (608), combat_query.h (33),
combat_special_watch.cpp (312), combat_special_watch.h (21),
combat_strings.cpp (535), combat_strings.h (133), combat_diag.cpp (355),
combat_diag.h (70), combat_diag_internal.h (22).

Method: full `Read` of every file in the batch (no truncation). Cross-file
verification by targeted `Grep` for: symbol usage per include (to confirm
unused includes rather than guess), the shared linked-list-offset constants
in `engine_offsets_fields.h`, all call sites of `combat_diag::LogPreFire` /
`LogPostFire` / `Tick()` (to rule out dead code before reporting), and every
call site of `kLinkedListHeadOffset` codebase-wide. `core_tick.cpp` was
grepped to confirm which of this batch's `Tick*` functions run every frame
(all nine `PHASE("combat...")` lines at core_tick.cpp:389-399 do).

## combat_diag.cpp separation — verified clean

Per the brief: Phase 1 candidate 14 moved the shipped `OnCombatRoundAddAction`
hook plus the shared queue-size readers into `combat_queue_hooks.cpp`, and
left the three genuinely diagnostic hooks (`OnCombatRoundRemoveAllActions`,
`OnCombatRoundSetCurrentAction`, `OnCombatRoundRemoveLastAction`) plus
`Tick()`/`LogPreFire`/`LogPostFire` in `combat_diag.cpp`. Confirmed still
clean: `combat_diag.cpp` now contains no production narration logic, only
log-emitting probes; `combat_queue_hooks.cpp` contains exactly one function
(`OnCombatRoundAddAction`) and forwards to `acc::combat::queue::
OnEngineActionAdded`. `RoleTag` is shared correctly via
`combat_diag_internal.h`. Nothing misfiled.

## Section A — general low-level cleanup

### A1 — Stale file reference in combat.h's own doc comment (combat.h:7)

`combat.h`'s header comment says combat-log narration is registered "(see
the msg_router subscriber at the bottom of combat.cpp)". That code
(`RegisterCombatMsgRules`, `combat_log.cpp:1166`) moved to `combat_log.cpp`
under Phase 1 candidate 7 — `combat.h` was never updated to match. The
comment is one line above the exact code it's pointing readers away from.
- Proposed change: point the comment at `combat_log.cpp` instead.
- Risk: mechanical (comment-only).
- Estimated line delta: 0 (edit in place).

### A2 — Unused includes in combat.cpp (combat.cpp:9, combat.cpp:13)

`engine_manager.h` (comment claims `kAddrGuiManagerPtr, kMgrPanels*Offset`)
and `engine_reads.h` are included but nothing they declare
(`FindOwningPanel`, `IsPanelInManager`, `GetForegroundPanel`,
`LogManagerStack`, `kAddrGuiManagerPtr`, `kMgrPanels*Offset`,
`kAddrMoveMouseToPosition`, click-sim primitives / `ReadControlNameFields`,
`CallDowncast`, `ReadCExoString`, `ReadU32`, `LookupTlk`,
`DisplayToolTip`-mirror) is referenced anywhere in the file. Verified by
grepping every symbol each header declares against the full file text.
`CExoArrayList` and the list-box offset constants combat.cpp actually uses
come from `engine_offsets.h`, already included separately.
- Proposed change: delete both `#include` lines.
- Risk: mechanical (compiler-checked — a genuinely-needed symbol would fail
  the next `kdev build` immediately).
- Estimated line delta: -2.

### A3 — Unused includes in combat_log.cpp (combat_log.cpp:25,26,28,30)

Same pattern, four headers this time: `engine_area.h`, `engine_manager.h`,
`engine_panels.h`, `engine_reads.h`. None of their declared symbols
(`GetObjectName`, `ResolveServerObjectHandle`, `PanelKind`, `IdentifyPanel`,
`ResolveGuiInGame`, the click-sim/manager primitives, the SEH-guarded read
helpers) appear anywhere in the 1215-line file. This file's own header
comment says the include block was carried over "verbatim" from the
pre-split combat.cpp, which is consistent with these already being unused
before the Phase 1 split and simply never pruned.
- Proposed change: delete the four `#include` lines.
- Risk: mechanical (compiler-checked).
- Estimated line delta: -4.

### A4 — Unused include in combat_query.cpp (combat_query.cpp:13)

`engine_reads.h` — none of its declared symbols are referenced; every
engine read in this file goes through `engine_area.h` / `engine_player.h`
accessors instead.
- Risk: mechanical (compiler-checked).
- Estimated line delta: -1.

### A5 — Magic number duplicates an existing named constant (combat_queue_hooks.cpp:57)

```cpp
action_type = *(reinterpret_cast<uint8_t*>(action) + 0x10);
```
The comment two lines above (line 51) even names the constant this should
be: `kCombatRoundActionTypeOffset` (`engine_offsets_fields.h:684`, value
`0x10`), which is already used for the identical read in `combat_queue.cpp`
(twice) and `combat_special_watch.cpp` (twice) — this is the one
non-conforming site. `engine_offsets.h` is already included in this file
(currently otherwise unused — see A5b), so fixing this also gives that
include a reason to be there.
- Proposed change: replace the literal with `kCombatRoundActionTypeOffset`.
- Risk: mechanical (same value, compiler-checked type match).
- Estimated line delta: 0.

**A5b — companion finding, same file:** with the magic number in place,
`engine_offsets.h`, `engine_player.h`, `engine_rebase.h` and `<cstdio>` are
currently all unused in `combat_queue_hooks.cpp` (no `R(...)` call, no
`acc::engine::` call, no `printf`-family call anywhere in the 73-line
file). Fixing A5 re-justifies `engine_offsets.h`; the other three stay
removable.
- Risk: mechanical (compiler-checked).
- Estimated line delta: -3 (keep engine_offsets.h, drop the other three).

### A6 — Stale doc comment names the wrong cue (combat_special_watch.h:9)

The header says: "Edge trigger on ≥1 → 0 while in combat fires
`gui_actqueue` immediately." The actual resref, per
`combat_special_watch.cpp:26`, is `c_drdastro_hit2` — and the .cpp's own
comment right above it (lines 22-25) documents that `gui_actqueue` and a
second candidate (`cb_gr_boncehard2`) were both tried and rejected before
landing on the current one. The header simply never got updated after the
second swap.
- Proposed change: update the header line to name `c_drdastro_hit2` (or
  just say "a short audio cue" if the resref is expected to keep changing
  during tuning, matching how loosely other tuning-in-progress headers in
  this codebase describe adjustable constants).
- Risk: mechanical (comment-only).
- Estimated line delta: 0.

### A7 — Unconditional per-tick recompute during a 6-second gate window (combat_special_watch.cpp:277-284)

```cpp
// First-round gate.
if (now - s.combatEnteredAt < kFirstRoundQuietMs) {
    // Still keep specialsPrev fresh so the post-gate edge detector
    // doesn't fire spuriously on whatever happened during the
    // quiet window.
    s.specialsPrev = CountPartySpecials();
    return;
}
```
This runs every frame (see the core_tick.cpp:399 wiring) for the first
`kFirstRoundQuietMs` (6000 ms) of every combat encounter. `CountPartySpecials`
walks every party member's combat-round action list. Only the *last* value
computed right before the gate closes is ever read (the post-gate branch
computes its own fresh `specialsNow` unconditionally, so nothing from the
gate window feeds forward except the discarded intermediate writes to
`s.specialsPrev`). This is the same shape as the just-fixed 360×/s
combat-round-clear bug: real work done every tick purely to keep a value
"fresh" that is thrown away until the one moment it's needed.
See the possible-bug finding below — this recompute is presently far more
expensive than it looks, because `CountPartySpecials` is very likely
raising and SEH-catching a hardware exception on almost every call while
any queued action exists.
- Proposed change: skip the recompute during the gate; compute
  `specialsPrev` fresh exactly once, on the tick the gate closes (fold into
  the existing post-gate `specialsNow` read).
- Risk: low (behavior-preserving — same value ends up in `specialsPrev`
  either way, just computed once instead of up to ~360 times).
- Estimated line delta: -4.

### A8 — Leftover investigation logging, self-flagged as temporary (combat_special_watch.cpp:118-122, 154-159)

```cpp
// Diagnostic: when the walk encounters ANY non-placeholder item (even a
// routine auto-attack), emit a per-item log line. This is intentionally
// noisy — gives us per-tick visibility into the queue while we're
// chasing the "bare 1-7 dispatch produces no special" bug. Remove or
// gate behind a verbosity flag once the dispatch is understood.
```
The comment itself marks this as scaffolding for a closed(?) investigation
("chasing the bug") that should be removed once resolved. It fires from
inside `CountSpecialsForCreature`, called every tick throughout combat (not
just the first-round gate). Per the "don't remove logging for verbosity
alone" rule this is flagged only because the code's own comment already
calls it out as removable — not because of volume.
- Note: per the possible-bug finding below, this log line likely almost
  never actually reaches its `acclog::Write` call in practice (the crash
  happens one read earlier), so removing it is lower-impact than it looks
  at first read; it's still worth a decision once the walk itself is fixed.
- Risk: low — needs a decision once A-bug-1 below is resolved, since fixing
  the walk will make this log line fire for real and its true volume needs
  re-assessing then.
- Estimated line delta: -8 (if removed outright) or 0 (if gated behind a
  verbosity flag as the comment itself suggests).

## Section B — AI-pattern findings

### B1 — Near-duplicate HP-read functions (combat_query.cpp:56-94)

`ReadCurrentHpFromClient` and `ReadMaxHpFromClient` are two ~19-line
functions that do the identical SEH-guarded chain walk
(`clientLeader → +0x2f8 lvlUpStats`) and differ only in the final field
offset read (`+0x4c` vs `+0x4e`). A shared
`ResolveLvlUpStats(void* clientLeader)` returning the intermediate pointer,
with two one-line field readers on top, would cut the duplicated chain-walk
in half.
- Risk: low (same reads, same SEH guarding, mechanically extractable).
- Estimated line delta: -18.

### B2 — `ReadCombatRound`-shaped code duplicated four times across the subsystem

The read "`serverCreature` → `+kCreatureCombatRoundOffset`, SEH-guarded"
appears as:
- `combat_queue.cpp:43-52` — standalone `ReadCombatRound` helper.
- `combat_special_watch.cpp:63-72` — a second, byte-for-byte identical
  `ReadCombatRound` helper (different anonymous namespace, same body).
- `combat_diag.cpp` — inlined again inside `ReadQueueSize` (~line 82-86)
  and a third time inside `GetPlayerCombatRound` (~line 284-289).
No shared header currently owns this one-hop read, so it's been
independently retyped four times in three files of the same subsystem.
- Proposed change: publish one `ReadCombatRound(void* serverCreature)` (a
  natural fit for `combat_queue.h`, which already publishes the
  action-queue surface) and have the other three call it.
- Risk: low — identical semantics, mechanically verifiable by diffing the
  four bodies (done above).
- Estimated line delta: -24.

### B3 — Small duplicate list-box walk (combat.cpp:215-239)

`ReadListBoxRowCount` and `ReadListBoxRow` both reconstruct the same
`CExoArrayList*` from `lb + kListBoxControlsOffset` and both re-check
`!lbList || !lbList->data`. Only used from the two call sites inside
`TickCombatLog`. A shared `ResolveListBoxRows(void* lb)` returning the
`CExoArrayList*` (or a `{data,size}` pair) once would remove the ~8-line
duplication. Low value on its own (small, two call sites, one file) —
noting it because it's a clean small instance of the pattern, not because
it is costly to leave.
- Risk: mechanical.
- Estimated line delta: -6.

## Findings (possible bugs — user decides)

### Bug-1 — combat_special_watch.cpp reads the party's special-action queue through a walk `engine_offsets_fields.h` documents as already fixed everywhere else

`engine_offsets_fields.h:663-672` carries this comment, describing a
historical bug:

> "The original walker in combat_queue (and combat_diag) treated the
> internal pointer as a node and walked via +0 — which on a real node is
> `prev`. On the head node `prev` is NULL, so the walk terminated after one
> iteration regardless of how many entries the list actually held. ...
> Correct walk: combat_round.actions → +0 = internal* → +0 = head node* →
> walk via Node.next at +4 until null."

`kLinkedListHeadOffset` (`engine_offsets_fields.h:682`) is kept explicitly
as "Legacy name kept so existing call sites compile" — and a codebase-wide
grep for that name finds exactly one remaining call site:
`combat_special_watch.cpp:136`, inside `CountSpecialsForCreature`:

```cpp
void* listPtr = *reinterpret_cast<void**>(
    reinterpret_cast<unsigned char*>(round) +
    kCombatRoundActionsOffset);
if (!listPtr) return 0;
void* node = *reinterpret_cast<void**>(
    reinterpret_cast<unsigned char*>(listPtr) +
    kLinkedListHeadOffset);          // == kListInternalOffset == 0x0
```

This does the single-hop walk the comment says was wrong: `node` here is
actually `internal*`, not the head node. `combat_queue.cpp`'s
`CountQueueEntries`/`GetQueueAction` and `combat_diag.cpp`'s
`ReadRoundActionCount` all correctly do the two-hop
`kListInternalOffset` → `kListInternalHeadOffset` walk instead — verified
by re-reading all three.

Tracing the consequence: with `node` actually pointing at the
`CExoLinkedListInternal`, the loop then reads
`data = *(node + kLinkedListNodeDataOff)`, i.e. `*(internal* + 0x8)` — but
`+0x8` on `CExoLinkedListInternal` is the engine's `count` field
(`kListInternalCountOffset`), not a node's data pointer. So `data` becomes
the small integer queue count (0-4), reinterpreted as a pointer. The very
next line, `*(base + kCombatRoundActionTypeOffset)` i.e. `*((void*)count +
0x10)`, dereferences an address like `0x11`-`0x14` — an unmapped page.
That raises a hardware exception, caught by the `__except` wrapping the
whole walk, which returns immediately with whatever `specials` had
accumulated so far (0, since this happens on the very first item).

Net effect: whenever a party member's combat round actually has 1-4 queued
actions — precisely the case this function exists to detect —
`CountSpecialsForCreature` almost certainly SEH-faults on the first item
and returns 0. `CountPartySpecials()` (the caller) would then read as "no
specials pending" essentially all the time, so the shipped "you can act
now" heartbeat cue (`combat_special_watch.cpp`) likely never correctly
suppresses itself while the player already has a feat/power/item queued —
it would just keep treating the queue as empty of specials and firing the
repeat heartbeat regardless.

- What a fix would look like (not proposed as an executed change here):
  swap `kLinkedListHeadOffset` for the same two-hop pattern
  `combat_queue.cpp`'s `CountQueueEntries` uses
  (`kListInternalOffset` then `kListInternalHeadOffset`).
- Risk / verification: needs-in-game-test. Enter combat, have a party
  member queue a feat or force power (not a plain attack) so the party has
  a real "special" pending, and listen for whether the "you can act now"
  cue still repeats every ~6s despite the queued special — if it does,
  that confirms the walk never sees the entry.
- This also changes the cost picture for A7/A8 above: if the walk is
  fixed, the per-tick recompute during the gate window becomes real
  (non-exception) work rather than an immediate fault, and the per-item
  diagnostic log (A8) will actually start firing for real data, at which
  point its volume should be re-assessed.

## Candidate 28 — narrow-header include opportunities

- `combat.cpp` — needs types (`CExoArrayList`) + fields (multiple
  `k*Offset`) + at least one raw `.data` address
  (`kAddrAppManagerPtr`/`kAddrGetCombatMode`); not narrowable to one header.
- `combat_log.cpp` — after A3's include cleanup, this file's only remaining
  use of anything from the `engine_offsets.h` aggregator is `Vector`
  (`engine_offsets_types.h`). Could narrow straight to
  `engine_offsets_types.h`.
- `combat_query.cpp` — uses fields + `Vector` (types) + one raw address
  (`kAddrCSWSObjectGetDamageLevel`); not narrowable to one header.
- `combat_queue.cpp` — uses `Vector` (types) + many fields; its own
  `kAddrCombatRoundRemoveLastAction` is locally `R()`-wrapped, not sourced
  from `engine_offsets.h`, so this one could likely narrow to
  `engine_offsets_types.h` + `engine_offsets_fields.h` (no `_addresses.h`
  needed) — worth confirming with a full identifier diff before acting.
- `combat_special_watch.cpp` — `Vector` (types) + fields; same
  types+fields-only shape as combat_queue.cpp.
- `combat_diag.cpp` — fields + at least one raw `.data` address
  (`kAddrAppManagerPtr`); not narrowable to one header.
- `combat_queue_hooks.cpp` — after A5/A5b, needs only
  `engine_offsets_fields.h` (for `kCombatRoundActionTypeOffset`).

## Files scanned with nothing to report

- `combat.h` — one stale-reference finding (A1); otherwise clean.
- `combat_queue.h` — declarations match the implementation exactly,
  including the documented tail-only-remove limitation. Clean.
- `combat_query.h` — declarations match implementation. Clean.
- `combat_diag.h` — hook wiring comments match hooks.toml-driven call
  sites (verified `LogPreFire`/`LogPostFire` callers in `input_pipeline.cpp`
  and `unified_action_menu.cpp`). Clean.
- `combat_diag_internal.h` — small, single-purpose seam header, does
  exactly what its own comment says. Clean.
- `combat_strings.h` — clean.
- `combat_strings.cpp` — clean. Every locale's `phrase_mit` /
  `status_ist_marker` / anchor divergence from DE carries an inline comment
  explaining the specific engine-template reason (glue-space differences,
  missing copula in Russian, IT's multiplier-first word order, etc.) — this
  is the file the EN double-space bug (already fixed) lived in, and the fix
  plus its rationale are still documented in place. No new anchor asymmetry
  found; nothing looked unexplained.
