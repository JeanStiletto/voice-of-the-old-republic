# Pre-KOTOR-2 Refactoring — End Report

Branch `refactor/phase2-coupling`, 73 commits, cut from main @ 4c4e216.
Phases 0-5 ran 2026-07-27 to 2026-07-30. Phase 1 landed on main earlier;
everything from Phase 2 onward is in this branch.

Goal, as set at the start: make the codebase the best it can be — fewer
bugs, better maintainability, less accidental complexity — and prepare it
for a port to KOTOR 2, which runs a very similar engine in a different
executable.

## Outcome in numbers

- **`patches/Accessibility`**: 131 files changed, +7,314 / −8,124 —
  **net −810 lines** of mod source, with more behaviour than before.
- **`installer/`**: 20 files, +254 / −322 — net −68.
- **`tools/`**: now tracked in git (41 files). It was gitignored until
  2026-07-29, which meant the Phase-1 kdev work had no history and no
  rollback path. That is fixed.
- Build at close: `kdev build --clean` **193 TUs, 0 warnings** (196 before
  the three probes were retired). kdev and installer `dotnet build` 0
  errors; warning baseline is 1 pre-existing installer warning
  (PriorityGroup2da) + 3 in third_party KPatchCore.

Ten defects were found and fixed along the way (F1-F8 plus two that
refactoring surfaced by construction). None were found by looking for
bugs; see "What actually finds defects" below.

## What each phase did

**Phase 0 — infrastructure.** Branch, STATE.md, per-phase reports, build
baseline, and a full code-index refresh (all 262 patch files re-indexed,
6 orphan entries removed, plus new coverage for kdev and the installer).

**Phase 1 — structure.** 28 candidates. Executed: the menus split, the
wall-vs-roomshape system rename (`wall_probe` + `room_topology`), the
engine_area / engine_reads / engine_player / engine_panels .cpp splits,
combat_log, minigame_* renames, focus_guard / camera_spin_guard renames,
the kdev Core/ extraction and the installer three-way split.

**Phase 2 — coupling and duplication.** State ownership in transitions,
the minigame SEH-primitive consolidation, BwmFile hoisting, the
`engine_offsets.h` four-way split (367 declarations proved identical
before and after), and the K2-portability lens recorded in
`docs/llm-docs/CLAUDE.md`.

**Phase 3+4 — the merged per-file sweep** (general cleanup + AI-pattern
pass as two separately-approvable sections). 24 Sonnet agents covering all
362 files / ~103,000 lines. Section A: dead includes, dead
using-declarations, dead code, dead string ids, narrowed Get() guards,
raw-hex substitutions, stale comments, mojibake, linkage fixes. Section B:
the engine resolve-chain seam (`engine_app`), five oversized-function
decompositions, the panel-array accessor, the installer announcement
helper, per-screen menu duplication, smaller duplications, and
belt-and-braces guards. Section F: eight bugs fixed, one rejected.

**Phase 5 — this report**, plus the merge to main and the archival of
`docs/refactoring/`.

## What was rejected, and why it matters

The rejections are the most reusable part of this record, because each one
is a case where "obvious cleanup" was wrong.

- **`DriveSelectedPeg` decomposition** — it is one data-dependency
  pipeline (position → lead solution → aim error → hitbox → cue →
  steering → sign calibration), each stage consuming the previous stage's
  floats. Splitting it would have produced parameter lists longer than the
  code.
- **The generic virtual-row-anchor abstraction** (credits / equipstats /
  charsheet) — reported as "the identical three-function shape". They are
  not: credits keys on panel KIND with a hardcoded sort order, the other
  two key on panel-relative OFFSET, and charsheet drops a row
  conditionally with no field for it in the proposed spec. A per-row
  format function pointer would mean following an indirection to learn
  what a row does.
- **`room_topology.cpp`'s 4-way file split** — measured the cross-block
  call matrix first: nearly every anon-namespace helper is called from
  2-5 of the proposed files. Executing it would have published ~25
  internal functions and 6 pieces of mutable state through a header,
  turning a cohesive TU into a wide-interface mini-library.
- **`transitions.cpp` landmark split and `engine_radial` diag split** —
  both attempted, both reverted. Function-name scans looked clean; the
  *state* and *constants* were interleaved at a granularity the scan
  could not see.
- **K1cp / K2cp installer "skeletons"** — K2cp is a 120-line stub its own
  docs mark as not wired up, against K1cp's 293 lines. Abstracting over an
  unfinished shape means redoing it later.
- **Four of B7's eight belt-and-braces guards** — including one whose own
  comment says it exists so a failure path SPEAKS rather than going
  silent. Removing that would make a failure silent, which is the one
  failure mode a blind user cannot notice.
- **F9 (gated dialog replies)** — there are no unavailable dialogue
  options in KOTOR 1, so a gating cue would manufacture false positives.
  Closed, not deferred.

## What actually finds defects

Not reading code. Every defect this effort found came from one of three
things, and that is worth carrying into future work:

1. **Refactoring by construction.** `ResolveGuiInGame` had no SEH guard at
   all — three raw dereferences called from panel code that runs during
   teardown. 8 of 18 panel-array walks were unguarded, two of them sitting
   directly above a guarded neighbour whose comment states the hazard
   outright. Neither was visible to a per-file scan, because each caller
   looked fine alone.
2. **The logs.** Whether the menus_extract tooltip clobber was live was
   settled by 9,202 `Menus.FocusChange` samples (it is not — K1 never
   populates that field). The panel-array cap divergence was settled by
   finding one 2026-05-30 log where `panels.size` hit 27. The character
   sheet's speech was diagnosed by grepping what the content monitor had
   ever spoken across 969 session logs — bare context-free numbers.
3. **The compiler.** `LaunchCommand.TryPin`'s platform guard was rated
   "mechanical" to delete and is load-bearing: CA1416 does not propagate a
   caller's platform guard across a method boundary. Two `menus_chain`
   `continue` statements silently changed meaning when their block moved,
   and only the compiler noticed.

## Three findings that should shape how the next effort is run

**1. Scan reports are a starting point, not a specification.** In six of
Section B's seven items the report's framing or counts were wrong, and in
several cases the correction changed what the right fix was — B1
undercounted ~20 sites as ~40 across ~25 files; B3's "adopt
FindPanelByKind" was impossible because the surviving scans are predicate
scans; B4's four copies were five because a bug fix had added one; C4's
premise named a parameter that does not exist. **Verify every claim
against the code before proposing anything.** B2 was the only fully
accurate entry, and its risk framing was still inverted in both
directions.

**2. Shared helpers are under-discovered, not under-provided.** Four times
in a single day an existing helper turned out to be re-implemented
locally, and twice a fifth copy was nearly written during this very
effort:
- `acc::engine::ReadLabelText` — the local copy was *worse*; it did not
  clear the buffer on fault.
- `acc::engine::ReadLabelTextAt` — hand-rolled in two files, while its own
  header comment names it as THE panel+offset convenience form.
- `acclog::BlockLog` — building it was offered before grepping `log.h`.

CLAUDE.md already says "before adding a helper, search for an existing
one" and that rule prevented none of them. The problem is that these
surfaces are not findable from the call site. **Recommendation: add an
index of the `engine_reads` and `acclog` surfaces to `docs/llm-docs/`** —
what exists, what each is for, and which one to reach for. That is a
concrete artifact, unlike restating the rule.

**3. C2712 is a real architectural constraint of this codebase.** MSVC
forbids objects needing unwinding — and function-local `static`s, whose
init guard counts — in any function using `__try`. Since engine-reading
code is SEH-heavy by convention, this rules out RAII and lazily-cached
constants across a large fraction of the mod. It bit three times in one
day. The workaround, now used twice and worth knowing: **declare the
object in an SEH-free caller and pass a pointer in.** A pointer is
trivially destructible, so the constraint never fires and the SEH code
needs no restructuring.

## KOTOR 2 port readiness

What this effort actually bought the port:

- **One engine resolve seam.** `engine_app.{h,cpp}` owns the whole
  AppManager → client/server/GUI/camera walk. Before, it was hand-walked
  at ~40 sites across ~25 files under six different names for one hop,
  with three private copies of the root pointer. The invariant is now
  grep-checkable: exactly ONE dereference of `kAddrAppManagerPtr` exists,
  and zero uses of the hop constants outside `engine_app`.
- **Offsets isolated by kind.** `engine_offsets.h` is a 28-line aggregator
  over `_types` / `_addresses` / `_fields` / `_values`. On a K2 port every
  value changes; they are now separated from the structural declarations
  that do not.
- **Every `.text` address is wrapped and guarded.** A codebase-wide scan
  found 12 unwrapped ones that were provably wrong on the Allard Russian
  build; the root cause was kdev's harvester regex, which is now a
  full in-range hex sweep with explicit opt-outs and a non-zero exit on an
  unresolved address.
- **Upstream AddressDatabases are seeded for K2** — both K2 databases
  carry all 14 global pointers under K1's names (C11).

What the port still has to decide, recorded rather than solved:
`engine_player.h`, `engine_area.h`, `engine_panels.h` and
`engine_reads.h` are still one header each — Phase 1 split only their
.cpp files. Splitting them is new work, and candidate 28's includer
migration only pays off once it happens.

## Outstanding, deliberately

**The installer manual pass — a decision with a trigger, not a gap.** The
user's call on 2026-07-30: trust the code review until there is a real
KOTOR 2 port to test against, since the installer must be reworked then
anyway (K2 detection, a second game path, K2CP wiring), so a manual pass
now would be spent twice. **The trigger is the K2 port starting.** Run it
before shipping anything K2-facing — it is the accumulated debt of F3-F6,
B4 and both B6 hold-backs, and all of it is end-user-facing:
- browse to a WRONG folder — reason spoken, Install-button transition
  announced
- run an uninstall — progress spoken
- confirm intro movies are disabled on install and restored on uninstall
  (nine states are unit-verified, never on a real install)
- with the spatial-audio toggle on, check EAX lands in swkotor.ini

**Behaviour questions surfaced and left for the user:**
- `GetForegroundPanel`'s last-resort return indexes
  `panelData[panelSize - 1]` using the RAW size while its scan covers only
  the first 32 — above 32 it reads outside its own window. Already
  guarded, so it was not in the class B3b closed.
- `InGameMessagesAnnounce` has no `rowCount <= 0` guard where its two
  siblings do. Each site kept its own; almost certainly never matters.
- `menus_credits`' text-read fallback fires only when `ReadGuiString`
  returns false, without the non-empty check its siblings use — related to
  the "9999999" placeholder the panel carries before populate. Changing it
  changes when the fallback fires.
- The 2026-05-30 panel-array growth (`panels.size` 17→27 in one second).
  User declined to chase a month-old one-time event;
  `patch-20260530-112606.log` is the starting point if it recurs.

## Method notes worth reusing

- **Slice function bodies by line range with a script, never retype
  them.** Then diff code lines with comments and whitespace stripped and
  `comm` the result. That proved five decompositions (1472, 682, 406, 804
  and 504 lines) touched nothing but the intended seams.
- **Throwaway harnesses where "it should be fine" is not good enough.**
  Locale JSON equivalence (1,146 keys across six languages, zero diffs),
  the intro-movie rename (nine folder states including both mid-states and
  a round trip), the INI tweak (one line changed out of 166 in a copy of
  the real file).
- **Check DLL freshness after `kdev apply`, before believing any test
  result.** `apply` silently skips the copy while the game holds the file
  open; this cost a full round trip once, with "all tested and working"
  reported against a three-day-old binary.
- **Measure the right metric.** Choosing a log-dedup mechanism by
  distinct-line count would have picked exactly the wrong one: three of
  the four spammiest tags had huge redundancy and *zero* consecutive
  repeats, because they emit a repeating cycle. Only consecutive-repeat
  ratio tells you whether line-level dedup can help.
- **`\b` is not a word boundary in awk — it is a backspace.**
  `awk '/\barea\b/'` matches nothing, silently, and it produced a wrong
  answer that an earlier correct `grep -E` had already given.
