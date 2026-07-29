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

## C8 — EXECUTED 2026-07-29

Done. `engine_offsets.h` is now a 28-line aggregator over four headers:

- `engine_offsets_types.h` (62 lines) — `CExoArrayList`, `Vector`,
  `CExoString`. Also carries the family's rationale comment.
- `engine_offsets_addresses.h` (727) — 105 constants: 103 `.text`
  function/vtable addresses (all `acc::addr::R()`-wrapped) plus the 2
  `.data` global pointers in their own banner-marked section, and the 15
  `PFN_*` typedefs.
- `engine_offsets_fields.h` (1128) — 244 constants: struct field offsets
  plus the geometry and bit-mask/sentinel constants that decode them.
- `engine_offsets_values.h` (74) — 18 constants: vtable slot indices,
  strrefs, sentinels, action-type enum bytes, panel input codes.

**Verification** (all green):
- 367 `constexpr`/`const` declarations match before and after on **type,
  name and value** — not just name. Zero diff.
- 15 typedefs and 3 structs byte-identical.
- Every non-blank source line accounted for exactly once; no duplicates.
  Exactly two lines changed, both deliberate: the stale in-file
  cross-reference `"see kAddrRulesGlobal definition higher up in this file
  (line ~526)"` — which pointed at the wrong line even before the move
  (the constant was at 583) and would have been actively misleading after
  it.
- Name list matches `c8-constant-names-baseline.txt` exactly (358/358).
- `kdev build --clean`: 194 TUs, 0 warnings, 0 errors. Matches baseline.

**Deviations from the spec above, and why:**

1. **Four files, not three.** The spec's "ordering hazard" (structs and
   typedefs interleaved through all 1820 lines, some typedefs depending on
   the structs) resolves cleanly by giving the three structs their own base
   header. The `PFN_*` typedefs did *not* need to go there — each one
   documents the calling convention of the address declared next to it, so
   they stayed with their addresses and `engine_offsets_addresses.h`
   includes the types header.
2. **`engine_offsets_fields.h`, not `engine_offsets_structs.h`.** Having
   `_structs.h` mean "field offsets" while `_types.h` means "actual C++
   structs" was a name collision waiting to be misread. `fields` says what
   it holds.
3. **The split is not purely by declared type.** 26 of the file's 132
   comment blocks span categories — a class's vtable address and its member
   offsets are documented in one flowing narrative. Cutting purely by type
   would have orphaned about a third of the documentation from the
   constants it explains. Rule applied instead: the narrative stays with
   the half it actually documents (layout narrative → fields, function
   narrative → addresses), and the other half gets a one-line pointer.
   17 blocks were hand-split this way; the other 9 stayed whole because
   their "value" member is really struct geometry (element counts, strides)
   or field interpretation (bit masks, sentinels), which is now stated as a
   scope rule in the fields header.
4. **The spec's constant count of 358 is an undercount.** Its own grep
   recipe does not match multi-word types, so it silently skips the nine
   `constexpr unsigned char kActionType*` constants. True total is 367.
   Both figures were checked; the recipe is fine for a before/after diff
   (it is lossy in the same way on both sides) but should not be quoted as
   a census.

**Not done, deliberately:** the constants were not renamed to carry their
class (upstream's `offsets` table is keyed class + member; our flat
`k*Offset` names lose that). The class name is in the comment above each
group, which is enough for the eventual `GameVersion::GetOffset` swap and
avoids a 244-constant rename touching 86 includers. Renaming is a separate
decision, not a side effect of a file move.

## The 19 unwrapped .text addresses — investigated, and FIXED (2026-07-29)

The spec flagged "19 of 103 .text addresses not wrapped in `acc::addr::R()`"
and said to understand them before moving them. Doing that produced a
different and worse picture than the note suggested.

**Inside `engine_offsets.h` there was no gap at all.** Measured against the
actual PE section table of the reference exe (`.text` 0x00401000-0x0073D000,
`.rdata` to 0x0078D000, `.data` to 0x00835498): all 103 `.text` constants
were wrapped, and the only 2 unwrapped `uintptr_t` constants
(`kAddrRulesGlobal` 0x007a3a28, `kAddrTlkTablePtr` 0x007a3a08) are `.data`
globals, correctly raw. The spec's own type-spread section already said
this on the next line — "2 `constexpr uintptr_t` — .data global pointers
(raw by design)" — so the 19 figure contradicted the paragraph below it.

**Codebase-wide, however, there is a real gap: 12 addresses.** Scanning all
of `patches/Accessibility` for VA-range literals in *code* (comments
stripped) and classifying by PE section:

- `engine_panels_state.cpp` — `kAddrPrevSWInGameGui` (0x0062cdf0),
  `kAddrHideSWInGameGui` (0x0062cba0), `kAddrSetGlobalDialogState`
  (0x0062ec60), `kAddrSetInputClass` (0x005eda60)
- `peek_description.cpp` — `kAddrInventoryOnControlEntered` (0x006b3d10),
  `kAddrStoreOnControlEntered` (0x006c0aa0),
  `kAddrJournalOnControlEntered` (0x00645100)
- `menus_journal.cpp` — `kAddrJournalOnControlEntered` (0x00645100, a
  second copy), plus two inline `reinterpret_cast`s of 0x005ed320
  (`GetQuestJournal`) and 0x005ed690 (`GetInGameGui`)
- `menus_galaxymap.cpp` — `kAddrGalaxyHandleInput` (0x00695980)

Every one is `reinterpret_cast` to a function pointer and **called**.

**Are they really wrong on the Russian exe? Yes.** Three independent lines
of evidence:

1. Of the 214 `.text` addresses `kdev sigscan` resolved against the Allard
   build, **zero** kept their reference value. Not one.
2. Two of the twelve are already in the generated rebase table under their
   hook names — `0x005DB3D0 → 0x005DB580` (+432) and
   `0x0062CBA0 → 0x0062CD30` (+400). So for those we can state the error
   exactly: the code calls an address 400-432 bytes off.
3. The other nine are absent from the table, but every one is bracketed by
   resolved neighbours with large non-zero deltas on both sides, several
   within 50-80 bytes. `kAddrSetInputClass` sits 80 bytes below a +432
   neighbour; `GetQuestJournal` 48 bytes below a +464 one;
   `kAddrPrevSWInGameGui` has +400 neighbours 592 bytes before and 317
   bytes after; `kAddrStoreOnControlEntered` has -272 neighbours on both
   sides within 1.2 KB. Displacements are per-object-file, so these are
   near-certain to carry the neighbouring delta.

Consequence on the Allard build: a `__thiscall` into the middle of an
unrelated function. Four of the twelve sites are SEH-guarded, which
converts a fault into a logged failure but does nothing about a call that
runs and corrupts state without faulting. Five have no guard at all.

**Why they were missed — the mechanism, which is the actually useful
finding.** `kdev`'s `EngineAddresses.Collect` harvests addresses with one
regex: `^\s*(constexpr|const)\s+uintptr_t\s+NAME\s*=\s*(0x…)\s*;`. All
twelve are declared in a form it cannot see — `static constexpr uintptr_t`
(leading `static`), `constexpr std::uintptr_t` (`std::` prefix), or an
inline `reinterpret_cast<PFN>(0x…)` that is not a declaration at all. Being
invisible to the harvester, they never got a signature, never got a table
entry, and were never reported as unresolved. The sigscan report confirms
it: searching it for those nine addresses returns nothing — they were never
even attempted.

Note the second-order effect: the harvester also cannot see the *wrapped*
form `= acc::addr::R(0x…);`, so a re-run of `kdev sigscan` today would
harvest almost none of the 103 constants in `engine_offsets_addresses.h`.
The current table survives only because it was generated (2026-07-25)
before the R() wrapping was applied. Regenerating it now would silently
produce a much smaller table.

**FIXED 2026-07-29, all three layers** (user asked for the full fix rather
than the finding).

*Step 1 — the harvester.* `EngineAddresses.Collect` no longer matches
declaration shapes. It sweeps **every hex literal in the image's VA range**
and matches declaration shapes only to recover a readable name for the
report, so a new declaration style cannot hide again. Opt-out is explicit:
`kdev-sigscan: ignore` per line, `kdev-sigscan: ignore-file` per file. The
one place that needs it is `engine_rebase.cpp`'s `kXrefTable`, whose
right-hand column holds target-build addresses that are meaningless in the
reference image. Harvest went 264 → 281 distinct addresses, 216 → 225 in
`.text`.

Also made unresolved loud: any `.text` address left ambiguous or not-found
now exits **7** with an explicit message (documented in
`docs/kdev-design.md`). Previously it exited 0 — the one condition the
command exists to detect was the one it stayed quiet about. Two follow-on
corrections were needed to keep that signal honest: addresses already
mapped by hand in `kXrefTable` are reclassified `hand-resolved` and
excluded from the count (otherwise the warning is permanently on, and a
permanent warning is ignored), and a signature that failed to *build* no
longer fails the run if the ordinal pass or the hand-resolved supplement
placed the address anyway.

*Step 2 — the table.* The Allard exe was extracted from the repo-root
archive with Windows' own bsdtar (`C:\Windows\System32\tar.exe`, libarchive
3.8.4, reads RAR5 — no third-party extractor needed). It is a WinRAR SFX
containing `swkotor.exe`; PE link timestamp `0x4047CD47` matches
`kTimestampAllard172` exactly. `kdev sigscan` regenerated
`engine_rebase_table.inc`: **214 → 223 entries, nothing lost, nothing
changed**, and the 9 additions are exactly the missing addresses. Final
resolve: 221 unique + 2 ordinal + 2 hand-resolved = all 225, zero
unresolved, `hooks.toml` cross-check 25/25, exit 0. `target.hooks.toml`
hook addresses are byte-identical to the shipped `allard.hooks.toml`, so
that file needed no change.

Measured displacements, against the predictions made from bracketing:

- `0x005ED320` GetQuestJournal → +464 (predicted +464)
- `0x005ED690` GetInGameGui → +464 (predicted +432 or +464)
- `0x005EDA60` SetInputClass → +432 (predicted +432)
- `0x0062CDF0` PrevSWInGameGui → +400 (predicted +400)
- `0x0062EC60` SetGlobalDialogState → +384 (predicted +384..+400)
- `0x00645100` JournalOnControlEntered → +192 (predicted ~+192)
- `0x006B3D10` InventoryOnControlEntered → -256 (predicted -256..-272)
- `0x006C0AA0` StoreOnControlEntered → -272 (predicted -272)
- `0x00695980` GalaxyHandleInput → -208 (predicted -320..-256; the only
  miss, and the one whose nearest resolved neighbours were 44-85 KB away)

Eight of nine landed on the exact byte. Every one is non-zero, which is the
part that mattered: the bug was real at all twelve sites.

*Step 3 — the call sites.* All twelve wrapped in `acc::addr::R()` and
guarded with `acc::addr::Ok()` in `engine_panels_state.cpp`,
`peek_description.cpp`, `menus_journal.cpp` and `menus_galaxymap.cpp`. The
guards are placed by what each site actually needs rather than uniformly:

- `CallOnControlEnteredWithActive` guards *before* touching `is_active`,
  since it force-sets that flag and restores it after the call — bailing
  mid-way would leave it set.
- `CloseInGameMenuToWorld` requires *both* its addresses, because
  hiding the GUI without the matching `SetInputClass` leaves input_class
  != 0 and in-world movement dead — a documented past bug.
- `menus_journal::SpeakDescription` treats an unresolved address like its
  existing SEH path: skip the repaint, still read whatever the listbox
  holds.

Also swept up: `audio_bus.h`'s
`kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency` was raw but
also unused — the detour is declared in `hooks.toml`. Wrapped anyway so the
file has one rule rather than an exception, with a comment saying why it
exists.

*Verification.* A repo-wide scan for VA-range literals in code (comments
stripped, classified against the exe's real PE section table) now reports
**zero** unwrapped `.text` addresses outside `engine_rebase.cpp`'s mapping
table itself. `kdev build --clean`: 194 TUs, 0 warnings. `dotnet build`
for kdev: 0 errors. Re-running sigscan against the wrapped sources produces
a byte-identical table, which is the regression test that the wrapping did
not change what gets harvested.

**Zero behaviour change on the reference build**: `R()` is the identity
function there, so `Ok()` always passes. The fix is only observable on
Allard. Still needs an in-game pass on the Russian build to confirm the
now-working paths (item/store/journal descriptions, galaxy-map nav, closing
a menu back to the world).

## C8 — original specification (kept for reference)

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
