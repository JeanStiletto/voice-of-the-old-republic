# Phase 2 — High-level cleanup: consolidated report and candidate list

Date: 2026-07-28. Branch: `refactor/phase2-coupling` (from main @ 4c4e216,
the Phase-1 merge). Inputs: three scan reports in this directory —
`phase-2-scan-duplication.md` (D1-D4), `phase-2-scan-coupling.md` (C1-C6),
`phase-2-scan-k2-portability.md` (K-findings) — plus spot-verification in
the main session. Synthesis and recommendations: main session.

**Nothing here is approved or executed.** Same rules of engagement as
Phase 1: item-by-item approval, behaviour-preserving only, do-not-touch
list unchanged (hook addresses/byte patterns, offset VALUES, calling
conventions, exports.def names).

## What changed in how the scans were run

Phase 1's scans searched function names and missed state; four of five
execution failures came from that. This round every finding had to cite
file:line with quoted code, and any proposed shared helper had to declare
what mutable state it needs. The difference shows: the duplication scan
found a third BWM consumer Phase 1 had missed, the coupling scan produced
two findings nobody had recorded, and the K2 scan surfaced an existing
PARKED feasibility doc (`docs/kotor2-port-feasibility.md`) that the whole
plan had been ignoring.

Spot-verified in the main session before writing this: D3, D4, C3, C5's
contradictory comment, and the K2 address count. All held up.

## The K2 question, answered

This phase existed partly to decide how `engine_offsets.h` should be cut
(the deferred Phase-1 candidate 10). The answer is: **not by subsystem.**

Measured content of `engine_offsets.h` (1820 lines, 86 includers):
- 103 addresses/vtables already indirected through `acc::addr::R()`.
- The bulk of the rest are raw, unindirected struct-field offsets.
- Plus a third class nobody had named: resource-derived values — `.gui`
  control IDs and TLK strrefs — which come from game *resources*, not the
  executable, and vary independently of both.

Those three classes have completely different portability behaviour, so
they are the correct cut lines. Splitting by subsystem would have grouped
a volatile struct offset with a stable logic constant and called it done.

Prior art the scan found and the plan should absorb: K2 on Steam is an
Aspyr recompile eleven years later, not a relocated K1 build — kdev's
signature scanner scores 0/213 against it, and struct offsets, not
addresses, dominate the port cost. That is exactly why the offset classes
matter more than the address ones.

Also found: KPatchManager upstream already has a `GameVersion` /
`addresses.toml` seam designed for this kind of split, and our patch uses
it nowhere.

## Major finding: the upstream address database is real, and K2 is seeded

Investigated while answering the user's C11 question ("can we improve the
code now to be compatible?"). The answer is yes, and the mechanism is much
further along than the scan reported.

`third_party/Kotor-Patch-Manager/AddressDatabases/` holds SQLite databases
keyed by executable SHA-256, with three entry types:
- **global_pointers** — name -> address
- **functions** — class + function -> address
- **offsets** — class + member -> offset

Measured contents:
- `kotor1_0_3.db`: 9710 functions, 4720 offsets, 977 classes, 16 global
  pointers. Thoroughly populated.
- `kotor2_steam_aspyr.db` and `kotor2_gog_aspyr.db`: 48 functions, 21
  offsets, 14 global pointers, 0 classes. A seed, not a port — but not
  empty, and someone upstream has already started K2.

Two verifications that matter:
1. Our hardcoded `kAddrAppManagerPtr = 0x007A39FC` is in the K1 database
   as `APP_MANAGER_PTR`. Our globals are already named upstream.
2. **The same names are already mapped for K2.** `APP_MANAGER_PTR` is
   `0x00A1B4A4` in K2 Steam; all 14 global pointers exist in both. And
   `CAppManager|Client` is `0x4` in K2 — identical to our K1 value, so
   some struct offsets are stable across the two games while others
   plainly are not.

Consequence for this phase: **C8's split should use the upstream taxonomy**
(global pointers / functions / offsets) rather than the volatility axis
invented by the scan. They largely coincide, but matching upstream means
each group maps one-to-one onto a `GameVersion::Get*` query, so adopting
the mechanism later is a substitution rather than a redesign. Resource-
derived values (`.gui` control IDs, TLK strrefs) remain a fourth category
that the address database does not model, because they come from game
resources rather than the executable.

User decision (2026-07-29): adopt the KPatchManager mechanism. It is our
main tool and solving this problem is what it is for.

## Candidate list

### Batch A — state ownership (the Phase-1 blockers, now fixable)

1. **transitions.cpp: give each state group an owner** (C1). Room-speech
   dedup state (`g_last_spoken_room_text`, `g_last_spoken_pos`,
   `g_last_spoken_pos_valid`) is physically declared inside the
   landmark-cache block, and `Tick()` resets three unrelated groups by
   reaching into their internals. Fix: each group gets its own `Reset()`;
   `Tick()` calls those instead of poking variables. This is the
   prerequisite that makes the reverted Phase-1 candidate 13 (landmark
   cache split) possible later — but the ownership fix is worth doing on
   its own merits whether or not the split ever happens. Verify: build +
   in-game room/landmark narration.
2. **room_topology: give `DoorRecord::landmarkName` an owner** (C3).
   `SnapshotDoors` clears it on every call; `AttachLandmarksToDoors` sets
   it; `MaybeRefreshDoors` therefore hand-saves and restores the field by
   position-matching across re-snapshots. Fix: make the door snapshot
   preserve landmark attribution instead of destroying and reconstructing
   it. Smallest, cleanest item in the report. Verify: build + in-game door
   landmark announces.
3. **Fix the contradictory ordering comment** (C5, comment half only).
   `core_tick.cpp:337` correctly documents that transitions must run
   AFTER the change detector; a comment in transitions.cpp states the
   opposite. One of them is wrong and it is a trap for the next reader.
   Comment-only, zero risk.

### Batch B — duplication

4. **Shared SEH read primitives for the minigames** (D2). All three
   minigame files independently define byte-identical `SafeReadPtr` /
   `SafeReadU32` / `SafeReadFloat` / `SafeReadVector`; turret and
   swoop-audio additionally duplicate `ResolveMgoArray`, the cast helper,
   and a track-follower position reader under two different names
   (`ReadFollowerPosition` vs `ReadTrackFollowerPosition`). These are pure
   functions — no shared state — which is why this is low risk. Natural
   home: `minigame_aim.*`, which already exists as the shared-primitives
   file for exactly this. Verify: build + brief swoop/turret pass.
5. **Consolidate the AppManager chain constants** (D4). The
   AppManager -> ClientExoApp chain is declared three times, twice under
   names one letter apart (`kAppManagerClientAppOffset` vs
   `kAppManagerClientOff`). Note: the `engine_panels_internal.h` copy is
   mine, created during Phase-1 candidate 6 — I moved it verbatim rather
   than consolidating, which was right for a behaviour-preserving move and
   wrong to leave. Verify: build.
6. **One hover-debounce constant** (D3). `kHoverPauseMs = 300` declared
   separately in `map_ui_cursor.cpp` and `view_mode.cpp`, backing four
   instances of the same "arm on change, fire after quiet window" idiom.
   Minimum: one shared constant. Optional: a shared debounce helper — the
   scan found the four sites divergent enough that a single helper may not
   fit, so this needs a judgement call. Verify: build + hover announces.
7. **Hoist BWM parsing in kdev** (D1). Now three consumers, and the source
   comment's own stated trigger ("hoist only if a 3rd consumer appears")
   has been met. **Caveat: `/tools/` is gitignored** — no commit, no
   rollback. Decide the versioning question before touching kdev again.

### Batch C — K2 portability groundwork

8. **Split `engine_offsets.h` by volatility class** (the redesigned
   candidate 10): executable addresses/vtables (already `R()`-indirected)
   / raw struct offsets / resource-derived `.gui` IDs and TLK strrefs.
   Keep `engine_offsets.h` as an aggregator so all 86 includers are
   untouched. This is mechanical and zero-behaviour, and unlike the
   Phase-1 version it has a real purpose: it makes the port's actual risk
   surface visible and countable. Verify: build + a scripted name/value
   diff proving no constant changed.
9. **Tag the K1-only content modules** (K-finding). `floor_puzzle`,
   `spectator_scene`, `endar_softlock`, `tutorial_hints`,
   `map_shipped_hints` are irreducibly K1 story content and will not port.
   Documentation only — a header note per file plus a line in
   `docs/llm-docs/CLAUDE.md`. Zero risk.
10. **Fix `audio_bus.h`'s misleading name** (K-finding). It carries raw K1
    addresses despite not being `engine_`-prefixed, so the naming
    convention this project just settled does not flag it as
    engine-coupled. Either move the addresses behind the engine seam or
    document the exception. Needs a decision on which.
11. **Record the KPatchManager `GameVersion` / `addresses.toml` seam**
    (K-finding). Upstream already built the K1/K2 split mechanism and our
    patch ignores it. Documentation + a note in `docs/upstream-prs.md`;
    adopting it is a future decision, not this phase's.

### Carried from Phase 1

12. **Candidate 23** — menus_listbox picker split (~575 lines). Approved
    in Phase 1, never reached. It moves state
    (`s_equipPickerActive` / `s_workbenchUpgradePickerActive` and their
    panel pointers), so measure the variables before cutting.

## Explicitly rejected (recorded so it is not re-litigated)

- **A full `k1/` vs `shared/` directory split**, and **interface-per-
  subsystem abstraction**: premature before any K2 binary, `.gui` or
  `dialog.tlk` has been reverse-engineered. Also blocked by the flat
  build glob (Phase-1 finding). Revisit only when a K2 RE pass exists.
- **C2** (classification counters shared by build and diagnostics) —
  harmless today.
- **C6** (engine_radial's diagnostic/production constant surface) — the
  file has zero mutable state; this closes out the reverted Phase-1
  candidate 24 as correctly abandoned rather than merely unfinished.
- **C4** (`Landmark::doorMatched` split across files) — deferred to
  Phase 3's per-file sweep.
- **Candidate 28** (migrating includers to narrow headers) — still
  deferred; falls out of Phase 3 naturally.

## C8 — specified, NOT executed (next session picks this up)

Approved 2026-07-29 and designed, but deliberately not started: it is an
1820-line reorganisation of the one file where a wrong value is silent
(the do-not-touch rule exists because these are RE facts), and it was
reached at the end of a long session. Half-doing it would be worse than
not starting. Everything needed to execute it is below.

**Measured type spread** (this is the whole file, 358 constants):
- 236 `constexpr size_t` — struct field offsets.
- 103 `const uintptr_t` — .text addresses. 84 go through `acc::addr::R()`;
  **19 do not, and that discrepancy should be understood before moving
  them** — either they are .data misfiled as .text, or they are genuine
  gaps in the rebase seam. Check each against engine_rebase.h's ".text
  only" rule.
- 11 `constexpr int` — vtable indices and misc.
- 10 `constexpr unsigned`, 5 `constexpr uint32_t` — flags, strrefs.
- 2 `constexpr uintptr_t` — .data global pointers (raw by design).

**Proposed split**, matching the upstream AddressDatabase taxonomy so each
group later maps 1:1 onto a `GameVersion::Get*` query:
- `engine_offsets_addresses.h` — the 103 .text function addresses plus
  the 2 .data global pointers, in clearly separated sections (they are
  different upstream tables: `functions` vs `global_pointers`, and only
  the former is R()-eligible).
- `engine_offsets_structs.h` — the 236 field offsets (upstream `offsets`,
  keyed class + member — the class name is the piece our flat `k*Offset`
  names lose, and worth capturing in comments while splitting).
- `engine_offsets_values.h` — vtable indices, strrefs, flags, tunables.
  These are NOT in the address database at all; strrefs and `.gui` IDs are
  resource-derived and vary independently of the executable.
- `engine_offsets.h` stays as a thin aggregator including all three, so
  all 86 includers are untouched.

**Ordering hazard** (the reason this is not a mechanical line-range cut):
structs (`CExoArrayList`, `Vector`, `CExoString`) and typedefs are
interleaved with the constants through all 1820 lines, and some typedef
signatures depend on those structs. The structs must land in a base header
included first by the other three. Do not cut by line range without
resolving the dependency order.

**Verification recipe** (run before and after; the diff must be empty):
    grep -oE "^(inline )?(constexpr|const) [a-z0-9_:]+ +k[A-Za-z0-9_]+ *= *[^;]+" \
      engine_offsets*.h | sed 's/.*\(k[A-Za-z0-9_]*\) *= */\1=/' | sort
Plus a clean `kdev build` (0 warnings) and the 358-name count check —
`/tmp/c8_names_before.txt` from this session recorded the baseline name
list; regenerate it from git if lost.

## Status

Awaiting item-by-item approval. No code changed in Phase 2 so far; the
only writes are the four report files and STATE.md.
