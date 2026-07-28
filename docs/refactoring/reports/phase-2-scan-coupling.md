# Phase 2 scan — coupled responsibilities and state ownership

Scope: `patches/Accessibility/` (282 files). Topic: who owns which mutable
state, and where ownership is muddled. Method: full reads of the three
Phase-1-failure files (`transitions.cpp` 1413 lines, `room_topology.cpp`
3297 lines, `engine_radial.cpp` 937 lines), variable-level greps for every
`g_*` / `s_*` declaration and every read/write site, then a targeted
cross-file sweep for duplicated engine-derived caches and implicit tick-order
dependencies. Namespaces were grepped directly (`acc::transitions::`,
`acc::room_topology::`, `ws::` / `acc::spatial::wall_surfaces::`), not file
names, per the file-name-vs-namespace trap that already burned Phase 1's
candidate 22 scan (`docs/refactoring/STATE.md`, "Candidate 22 ... CANCELLED").

This report only covers state-ownership coupling. It does not propose
behavior changes, hook/offset/calling-convention/exports.def changes, file
splits, or dead-code removal — those are Phase 1's, Phase 3's, or the user's
to decide elsewhere.

## Summary of the state groups found

`transitions.cpp` has one anonymous namespace holding at least five
logically distinct state groups, declared in an order that does not match
their ownership:

- **Cluster/area identity tracker**: `g_prev_area` (L53), `g_prev_cluster_id`
  (L54), `g_module_load_pending` (L60), `g_prev_friendly_room_name` (L68),
  `g_pending_cluster_id`/`g_pending_cluster_count` (L77-78),
  `g_pending_cluster_since_ms`/`g_pending_cluster_minor` (L103-104).
- **Landmark cache**: `g_landmarks[]`/`g_landmark_count` (L137-138),
  `g_landmark_enabled_at_scan` (L183), range state
  `g_lm_enter_range_m`/`g_lm_exit_range_m`/`g_lm_ranges_from_fog` (L370-371,
  381), `g_landmark_recheck_last_ms` (L406).
- **Landmark proximity-fire state machine**: `g_lm_prox_pending_idx`,
  `g_lm_prox_pending_count`, `g_lm_prox_last_spoken_idx` (L373-375).
- **Room-speech dedup/history**: `g_last_spoken_room_text` (L267),
  `g_last_spoken_pos`/`g_last_spoken_pos_valid` (L280-281),
  `g_flap_prev_text`/`g_flap_prev_pos`/`g_flap_prev_ms`,
  `g_flap_cur_pos`/`g_flap_cur_ms` (L299-305).
- **Platz delayed-announce state**: `g_pending_platz_*` (L338-342).
- **Gated-cluster refire state**: `g_gated_cluster_pending`,
  `g_gate_clear_since_ms` (L434-435).

`room_topology.cpp` has one anonymous namespace with the `AreaGraph`
(`g_graph`, L335), `DoorStabilityState` (`g_doors_stability`, L368),
edge-classification counters (`s_class_clear`/`s_class_door`/
`s_class_blocked`/`s_caveat1_hits`/`s_caveat2_hits`, L878-882), and the
union-find array (`s_uf_parent`, L1163). This matches the count already
recorded in `docs/refactoring/STATE.md` under candidate 12's execution
finding (6 pieces of mutable state, ~25 helpers, `UFFind` called 18x from
build + 4x from diagnostics + 3x from the union-find block itself) — this
scan re-derived those call counts independently (see C2) and adds the
field-level detail that record wasn't tracking (the `DoorRecord.landmarkName`
field, and which of the three `ClassifyEdge` call sites are diagnostic vs.
production).

`engine_radial.cpp` holds **no mutable state at all** — every function reads
live engine memory each call. Its coupling (C6) is a different species: a
shared block of anonymous-namespace *constants* plus one shared *stateless
helper* (`CallVtableAsClass`) used by both the debug-dump functions and
production code. Flagged here because it was one of the three named leads,
but it should not be filed alongside C1-C5 as a state-ownership problem.

## Findings

### C1 — transitions.cpp: room-speech state lives inside the landmark block, and Tick() pokes three subsystems' internals directly instead of calling an owned Reset()

**State involved**: `g_last_spoken_room_text`, `g_last_spoken_pos`,
`g_last_spoken_pos_valid` (room-speech dedup) vs. `g_landmarks[]`,
`g_landmark_count`, `g_landmark_enabled_at_scan` (landmark cache) vs.
`g_lm_prox_pending_idx`, `g_lm_prox_pending_count`,
`g_lm_prox_last_spoken_idx` (landmark proximity-fire).

**Physical interleaving**: the landmark-cache block runs L106-258
(`kMaxLandmarks`, the `Landmark` struct, `g_landmarks`, `g_landmark_count`,
`g_landmark_enabled_at_scan`, `RebuildLandmarkCache`). Room-speech dedup
state is declared immediately after it, still ahead of the landmark
*proximity* block: `g_last_spoken_room_text` L267, `g_last_spoken_pos`/
`_valid` L280-281, the flap-dedup quintet L299-305. Only after all of that
does the landmark-proximity-fire trio appear, L370-375. So the room-speech
group is sandwiched between two halves of the landmark subsystem, not
grouped with the cluster-identity tracker it actually belongs to.

**Cross-subsystem writes without an owned reset**: the landmark-proximity
trio is reset from three different places, none of which is a function the
proximity subsystem itself exposes:
- `TickLandmarkCacheRecheck` (a landmark-*cache* function) resets it after
  a cache rebuild, L807-809:
  `g_lm_prox_pending_idx = -1; g_lm_prox_pending_count = 0; g_lm_prox_last_spoken_idx = -1;`
- `Tick()`'s player-loss branch resets it inline alongside the cluster
  tracker and room-speech dedup, L1019-1022.
- `Tick()`'s area-change branch resets it again inline, L1100-1102, in the
  same statement block as `g_prev_cluster_id`, `g_prev_friendly_room_name`,
  `g_last_spoken_room_text`, `g_pending_platz_valid`, `g_gated_cluster_pending`,
  and `g_flap_prev_text` — six unrelated state groups zeroed by one function
  that has to know every one of their field names.

**Why it's a problem**: `Tick()` (L990-1305) is the only place that
currently knows the complete list of "things to clear on area change" or
"things to clear on player loss." Adding a seventh piece of per-area state
anywhere in this file means finding and editing both of Tick's reset blocks
by hand; missing one silently leaks state across an area transition. This
is exactly the failure mode Phase 1's candidate 13 attempt hit (see
`STATE.md`, "Candidate 13 ... ATTEMPTED, REVERTED": "the speech side resets
the landmark proximity trio ... directly on area change").

**Proposed ownership fix**: give each state group its own
`ResetOnAreaChange()` (and, where they differ, `ResetOnPlayerLoss()`)
function defined next to its state, and have `Tick()` call four or five
named functions instead of inlining every field. This does not move any
state to a different file or change any behavior — it just makes each
group visually and functionally own its own lifecycle inside the existing
anonymous namespace. It also directly un-blocks a future retry of candidate
13's file split, since the split's blocker was exactly this interleaving.

**Risk**: low. Pure refactor of reset code into named functions in the same
TU; no control-flow or timing change if each new function is called at
exactly the point the inline code used to run.

**Confidence**: high — every cited line was read directly, and the finding
matches Phase 1's own recorded execution failure for this file.

**Evidence**: transitions.cpp L106-138, L176-183, L189-258 (landmark cache);
L260-267, L269-281, L283-306 (room-speech dedup, physically inside the
landmark block); L370-375 (proximity trio); L807-809, L1012-1023,
L1093-1109 (three independent reset sites for the same trio).

**Verdict**: (a) worth doing now, but as a narrow "add owned Reset()
functions" change, not a file split. The file split itself is Phase 1's
call to re-attempt once this lands.

### C2 — room_topology.cpp: ClassifyEdge's classification counters are shared between two real graph-build passes and one diagnostics-only call, with no separate accounting

**State involved**: `s_class_clear`, `s_class_door`, `s_class_blocked`,
`s_caveat1_hits`, `s_caveat2_hits` (L878-882).

**Write sites**: all five are incremented only inside `ClassifyEdge`
(defined L917, increments at L961, L965, L975, L1001). `ClassifyEdge` itself
is called from exactly three places:
- L2037, inside `LogClusterMemberAdjacency` — a pure diagnostics function
  (its own comment says "dump member adjacency + ClassifyEdge verdicts",
  L3068) — called with `areaForDiag=nullptr` specifically to suppress the
  function's own SEH-guarded area probes, but with no equivalent
  suppression for the shared counters.
- L2766 and L2900, inside `BuildForArea`'s real merge/absorb passes — these
  increments reflect genuine graph construction and feed the summary log.

**Read site**: the classification summary is logged once, L3052-3058,
immediately reading `s_class_clear`/`door`/`blocked`/`s_caveat1_hits`/
`s_caveat2_hits`. Critically, this print happens **before**
`LogClusterMemberAdjacency` runs (`BuildForArea`'s tail: `LogTopologyMetrics`
L3063, `LogNavWallCrossings` L3067, `LogClusterMemberAdjacency` L3069,
`DumpGraphToLog` L3071) — so `LogClusterMemberAdjacency`'s own
`ClassifyEdge` calls silently mutate global counters that were already
printed and are never read again before the next `Reset()` (L2204-2209,
called from `Reset()` itself and transitively at the top of every
`BuildForArea`, L2295).

**Why it's a problem**: the diagnostics call's contribution to the shared
counters is pure dead weight — it changes global state for no observable
effect, purely as an accidental side effect of reusing `ClassifyEdge`. It
also means the file's own already-documented caveat ("Counts are
multi-fire ... reading them as raw per-edge truth would overcount",
L3042-3046) is actually *worse* than the comment states: the counts are not
just multi-fired across build passes, they're also polluted by a call that
has nothing to do with classification accuracy and runs after the number
was already reported. This is the same shape of problem the Phase 1
candidate-12 execution note flagged in aggregate ("ClassifyEdge from 4
blocks") without identifying which call site is load-bearing and which is
inert.

**Proposed ownership fix**: give `ClassifyEdge` an explicit
`bool countStatistics` parameter (default true), and pass `false` from
`LogClusterMemberAdjacency`'s diagnostic call. This makes the counters
unambiguously owned by the two build passes and stops a diagnostic-only
code path from being able to skew production telemetry.

**Risk**: low — the diagnostic call's own log lines don't read the
counters, only the final summary does, and that summary already prints
before the diagnostic call runs today, so gating it changes nothing anyone
currently observes; it only prevents a future reordering (e.g. someone
moving the summary print after the diagnostics dump) from silently
including diagnostic noise in the reported numbers.

**Confidence**: high on the call sites and ordering (all read directly);
medium on whether this is worth spending a change on, since today's
ordering happens to make the pollution harmless.

**Evidence**: room_topology.cpp L878-882 (counters), L917-1001 (`ClassifyEdge`
def + increments), L2037 (diagnostic call, `areaForDiag=nullptr`), L2766,
L2900 (build-pass calls), L3042-3058 (summary log + its own multi-fire
caveat comment), L3069 (diagnostic call runs after the summary already
printed), L2204-2209 (`Reset()` zeroes the counters).

**Verdict**: (c) leave alone for Phase 2/3 purposes — the pollution is
currently harmless given the fixed call order inside one function, and
`docs/refactoring/STATE.md` already recorded that splitting this file's
helpers apart is rejected as **not worth doing** (candidate 12's option
(a): "accept room_topology.cpp at 3289 lines as cohesive"). Recorded here
only so the next person touching `ClassifyEdge`'s call order or the summary
log knows the counters are load-bearing for one purpose and coincidental
for another.

### C3 — room_topology.cpp: DoorRecord.landmarkName has no single owner across a snapshot/re-snapshot cycle, forcing a manual save-and-restore dance

**State involved**: `DoorRecord::landmarkName` (field declared L269, one
field of `g_graph.doors[]`).

**Write sites**:
- `SnapshotDoors` (L567-619) unconditionally clears it for every door on
  every call: `rec.landmarkName[0] = '\0';` (L599). `SnapshotDoors` runs
  once from `BuildForArea` and then again every tick from
  `MaybeRefreshDoors` until the door count stabilises (`kDoorStabilityRequiredStreak`,
  L359).
- `AttachLandmarksToDoors` (L2099-2196) sets it exactly once per `BuildForArea`
  call (L2169-2172), by matching against `transitions.cpp`'s landmark cache
  (see C4) — and only for doors whose name is currently empty (the
  `if (g_graph.doors[bestDoor].landmarkName[0] != '\0')` conflict check,
  L2157).

**The workaround**: because `SnapshotDoors` always wipes the field and
`AttachLandmarksToDoors` only ever runs once (at build time, not on every
`MaybeRefreshDoors` retick), `MaybeRefreshDoors` has to manually preserve
the field itself: it snapshots every door's position + `landmarkName` into
a local `SavedLandmark saved[kMaxDoors]` array (L2231-2240) before calling
`SnapshotDoors`, then re-attaches each saved name to the door at the
matching position afterward (L2244-2260), commented explicitly as a
workaround: "Preserve landmark attachments across the re-snapshot.
SnapshotDoors rebuilds g_graph.doors from scratch (clearing landmarkName),
but AttachLandmarksToDoors only runs once during BuildForArea — re-running
it here would re-log + re-claim every tick." (L2224-2230).

**Why it's a problem**: nobody owns `landmarkName` end-to-end.
`SnapshotDoors` treats the whole `DoorRecord` as its own the way it treats
`pos`/`transitionDest`/`locName`, clearing all four fields uniformly — but
`landmarkName` is actually owned by a second, later pass and needs to
survive a rebuild that the first pass believes it's entitled to reset from
scratch. The workaround (position-match by <0.1m squared distance,
L2251) is a heuristic re-derivation of an identity that a stable `DoorRecord`
handle or a "this field is populated post-build, don't touch it here" flag
would avoid needing at all. A new call site added to `SnapshotDoors` later
(e.g. a hypothetical fourth field) would silently reproduce the exact same
bug class unless the author already knows this precedent exists.

**Proposed ownership fix**: either (i) have `SnapshotDoors` not touch
`landmarkName` at all (leave whatever was there, since doors don't move and
the field is populated later by an owner that already knows how to detect
conflicts), or (ii) split `landmarkName` out of `DoorRecord` into a
parallel array explicitly owned by the landmark-attach pass, keyed by the
same door index, so `SnapshotDoors`'s "clear and rebuild" semantics don't
apply to it by construction. Either removes the position-matching
workaround entirely.

**Risk**: low for (i) (deleting one line + a stale comment); medium for
(ii) (touches every `g_graph.doors[...].landmarkName` read site, ~8 call
sites per the earlier grep).

**Confidence**: high — every write site and the workaround's own comment
were read directly and confirm the mechanism.

**Evidence**: room_topology.cpp L262-269 (field + comment "populated by
AttachLandmarksToDoors ... during BuildForArea"), L595-599 (`SnapshotDoors`
zeroing it every call), L2157-2172 (`AttachLandmarksToDoors` setting it once),
L2218-2260 (`MaybeRefreshDoors`'s save/restore workaround, with its own
comment naming the exact tension).

**Verdict**: (a) worth fixing now as a narrow, low-risk coupling fix
(option (i) above) — it removes ~35 lines of position-matching workaround
code for a one-line cause, and it's the kind of thing Phase 3's per-file
sweep is likely to walk past as "just a defensive loop" without connecting
it to `SnapshotDoors`'s unconditional clear four hundred lines away.

### C4 — transitions.cpp landmark cache and room_topology.cpp door-attach share one boolean, with an unenforced call-order contract between the two files

**State involved**: `Landmark::doorMatched` (transitions.cpp, field
declared as part of the `Landmark` struct, L134; comment at L122-126:
"`doorMatched` is set by room_topology::AttachLandmarksToDoors when this
landmark's name has been embedded in a cluster label").

**Cross-file access pattern**: `transitions.cpp` never sets `doorMatched`
itself. It exposes two functions purely so `room_topology.cpp` can reach in:
`IterateLandmarks` (L968-983, a stateful cursor-based iterator over
`g_landmarks[]`) and `MarkLandmarkClaimedByDoor` (L985-988, the only writer
of `doorMatched`). `room_topology.cpp::AttachLandmarksToDoors` calls both:
`acc::transitions::IterateLandmarks(...)` in a `while` loop (L2115-2116) and
`acc::transitions::MarkLandmarkClaimedByDoor(landmarkIdx)` when a match
commits (L2173). The reader of `doorMatched` is back in `transitions.cpp`,
inside `TickProximityLandmarks`: `if (g_landmarks[i].doorMatched) continue;`
(L851), skipping landmarks already spoken as part of a door/cluster label.

**Implicit ordering contract**: `AttachLandmarksToDoors` only produces
correct results if `transitions.cpp`'s landmark cache
(`RebuildLandmarkCache`) has already been rebuilt for the current area —
otherwise `IterateLandmarks` walks a stale or empty cache. Both existing
call sites get this right today, but only by construction, not by any
enforced contract:
- `TickLandmarkCacheRecheck`: `RebuildLandmarkCache(area);` immediately
  followed by `acc::room_topology::AttachLandmarksToDoors(area);`
  (transitions.cpp L805-806).
- The area-change branch of `Tick()`: `RebuildLandmarkCache(area);` (L1080)
  then, several lines later, `acc::room_topology::BuildForArea(area);`
  (L1092) — which internally calls `AttachLandmarksToDoors(area)` itself
  (room_topology.cpp L2342).

**Why it's a problem**: the ownership of "has this landmark already been
spoken via a door label" is split down the middle — the storage lives in
transitions.cpp, the decision of when to set it lives in room_topology.cpp,
and the two are wired together by two hand-authored call-order pairs rather
than one API that can't be called out of order (e.g. passing the freshly
rebuilt landmark list into `AttachLandmarksToDoors` as a parameter, instead
of having it pull the cache via a global iterator with no versioning). A
third call site added later without noticing both existing pairs would
either operate on a stale cache silently, or (if it forgot the sequencing
entirely) leave every door-adjacent landmark double-announced.

**Proposed ownership fix**: no code change is required to make this
*correct* today — both call sites are in the right order. The lower-risk
Phase-2-appropriate fix is documentation-level: a comment on
`IterateLandmarks`/`MarkLandmarkClaimedByDoor` in `transitions.h`
explicitly stating the "must run after RebuildLandmarkCache for the same
area" precondition, since right now that precondition is only spelled out
in prose comments in the two `.cpp` files, not on the shared header
surface a future caller would actually read. A stronger fix (out of scope
here) would fold `AttachLandmarksToDoors` into `RebuildLandmarkCache`'s own
completion path so the ordering is structural, but that changes when
door-attach diagnostics fire relative to today's log output.

**Risk**: low for the header-comment fix; the structural fold is a larger,
Phase-2-scope change with log-ordering side effects that would need a
before/after log diff, not something to wave through under "no behavior
change."

**Confidence**: high on the mechanism (every call site read directly);
medium on whether a third caller is ever likely to appear, since this is a
narrow, actively-maintained code path.

**Evidence**: transitions.cpp L122-126 (ownership comment), L968-988
(`IterateLandmarks`/`MarkLandmarkClaimedByDoor` defs), L805-806, L1080/1092
(both correct call-order pairs), L851 (the read site); room_topology.cpp
L2111-2116, L2173 (the reach-in calls), L2342 (`BuildForArea`'s internal
call).

**Verdict**: (b) — the header-comment version is small enough for Phase 3's
per-file sweep to pick up when it visits `transitions.h`; not worth its own
Phase-2 action item.

### C5 — transitions.cpp and spatial_change_detector.cpp each keep an independent "did the area change" latch, and one depends on the other's cache with a stale comment describing the wrong order

**State involved**: `void* g_prev_area` — declared **twice**, once per
file, as two unrelated variables that happen to share a name:
transitions.cpp L53, spatial_change_detector.cpp L435.

**Independent detection + reset logic**:
- `transitions.cpp::Tick()` compares `area != g_prev_area` (L1043) and, on
  a genuine player-loss, resets its own `g_prev_area = nullptr` (L1012).
- `spatial_change_detector.cpp::Tick()` does the same thing independently:
  compares `area != g_prev_area` (L647) and resets `g_prev_area = nullptr`
  on player-loss (L625).

Neither file's area-change branch calls into the other's. Both are
triggered off the same underlying fact (`acc::engine::GetCurrentArea()`
returned a new pointer this tick) but arrive at that conclusion via two
separately-maintained latches. Contrast this with `discovery::OnAreaChanged`
and `endar::OnAreaChanged`, both of which transitions.cpp already calls
explicitly on its own area-change branch (L1074-1075) — i.e. transitions.cpp
is already a de facto area-change dispatcher for some subsystems, just not
for this one.

**Real cross-file dependency, enforced only by array position**:
`room_topology::BuildForArea` (called from `transitions::Tick`, L1092)
needs `spatial::wall_surfaces`'s wall-edge cache
(`ws::RebuildForArea`, called from `spatial_change_detector::OnAreaChange`,
spatial_change_detector.cpp L453) to already be populated for the new area,
or the graph build comes back empty. `core_tick.cpp` documents this
explicitly as a load-bearing ordering constraint: "ORDER LOAD-BEARING ...
spatial::change_detector reads camera yaw + rebuilds wall cache. transitions
builds room_topology that depends on the wall cache — must run AFTER
change_detector or the first tick of an area change uses stale walls from
the previous area." (core_tick.cpp L331-339), and the actual `PHASE(...)`
call order matches: `spatial.change_detector` at L350 runs before
`transitions` at L360.

**The stale comment**: `transitions.cpp` itself describes the *opposite*
order at the exact point this matters — right before its retry-until-built
call to `room_topology::BuildForArea`: "The wall-edge cache the
decomposition depends on may not be ready on this exact tick
(**spatial_change_detector::Tick runs later in the dispatch order**).
BuildForArea silently leaves the graph empty when that's the case; we retry
below on every tick until it builds successfully." (transitions.cpp
L1086-1091, emphasis added). This directly contradicts `core_tick.cpp`'s
own authoritative ordering comment 700+ lines away in a different file.
Given `spatial_wall_surfaces::RebuildForArea` is synchronous and fully
populates its buffers in one call (spatial_wall_surfaces.cpp L373-397), the
retry loop in `transitions.cpp` is very likely defending against a
condition (`spatial.change_detector` running *after* `transitions` in the
same tick) that the current `PHASE` order in `core_tick.cpp` no longer
produces — but nothing enforces that either comment stays true as
`core_tick.cpp`'s phase list is edited over time.

**Why it's a problem**: two independent latches for one fact is itself
minor duplication, but the real risk is the ordering dependency it's
entangled with: it's enforced today only by (1) the literal position of two
`PHASE(...)` lines in `core_tick.cpp`, (2) a prose comment above them, and
(3) a *second*, contradictory prose comment in a completely different file
that nobody has reconciled with (1)/(2). A future edit to `core_tick.cpp`'s
phase order — plausible, since nothing but the comment stops someone from
reordering — would silently reintroduce the stale-wall-cache bug the retry
loop was written to survive, and the stale comment actively points a future
reader in the wrong direction about why the retry loop exists at all.

**Proposed ownership fix**: reconcile the two comments (transitions.cpp's
comment should say "runs *before*", matching core_tick.cpp, or explain
what specific narrower race it's actually guarding if the order claim was
never about full-tick position). Longer-term, replacing the "two files each
poll `GetCurrentArea()` independently" pattern with one canonical
"area epoch" counter or callback list (transitions.cpp already plays that
role for `discovery`/`endar`; extending it to `spatial::change_detector`
would remove one of the two duplicate latches) would make the ordering
explicit in code instead of comment-and-array-position.

**Risk**: low for the comment fix (text-only, no behavior change). Medium
for the structural fix (consolidating the two `g_prev_area` latches touches
the tick dispatch order and both files' area-change branches — needs the
in-game region/landmark narration smoke test either way).

**Confidence**: high on the contradiction itself (both comments and the
actual `PHASE` order were read directly); medium on whether the retry loop
is fully dead today, since I did not instrument a live run to confirm
`BuildForArea` never actually hits the empty-graph branch on a real area
load — the synchronous nature of `RebuildForArea` makes it likely but not
proven from static reading alone.

**Evidence**: transitions.cpp L53 (own `g_prev_area`), L1043, L1012,
L1086-1091 (stale comment); spatial_change_detector.cpp L435 (separate
`g_prev_area`), L625, L647-656; core_tick.cpp L331-340 (authoritative order
comment), L349-350/L360 (actual `PHASE` order); spatial_wall_surfaces.cpp
L373-397 (`RebuildForArea` is synchronous).

**Verdict**: (a) worth fixing now for the comment reconciliation alone (near
zero risk, prevents a future reader from "fixing" the wrong thing); (c) for
the deeper "one shared area-epoch owner" restructuring — that duplicates
effort with whatever Phase 2's broader duplication scan finds elsewhere and
is better sized as its own candidate once all such duplicate-latch cases
across the codebase are inventoried together, not fixed one pair at a time.

### C6 — engine_radial.cpp: diagnostic dumps and operational code share one private constant/helper surface (not state — recorded for completeness since it was a named lead)

**What's actually shared**: unlike C1-C5, this file has zero mutable
global state (confirmed by a full read — every function derives its result
fresh from engine memory each call). The coupling here is over a shared set
of `constexpr` offsets and one shared stateless helper, all declared in the
same anonymous namespace:
- `CallVtableAsClass` (L736-751) is called by `LogTargetDiag` — a pure
  diagnostic function (L755-917) — at L796-799, and by the operational
  `IsCreatureClientTarget` (L919-935) at L934.
- The vtable offset constants `kVtableAsSWCDoorOffset`/
  `kVtableAsSWCCreatureOffset`/`kVtableAsSWCTriggerOffset`/
  `kVtableAsSWCPlaceableOffset` (L126-129) and the door-field offsets
  `kDoorCannotBashOffset`/`kDoorCanUseActionsOffset`/`kDoorIsHostileOffset`/
  `kDoorStateOffset`/`kDoorField17Offset` (L138-142) are used almost
  exclusively by `LogTargetDiag`, but `kVtableAsSWCCreatureOffset` is also
  the one `IsCreatureClientTarget` needs.

**Why this isn't a state-ownership problem**: there is no mutable shared
fact here to assign an owner to — `CallVtableAsClass` is a pure function of
its two parameters. The coupling is purely about *visibility*: extracting
the diagnostic functions into their own translation unit (Phase 1's
candidate 24) requires publishing ~10 constants plus this one helper
through an internal header, for a diagnostics-only payoff. This is exactly
why Phase 1 reverted candidate 24 (`STATE.md`, "Candidate 24 ...
ATTEMPTED, REVERTED": "the dumps also need ~10 anonymous-namespace
*constants* ... and CallVtableAsClass ... is also called by the operational
IsCreatureClientTarget").

**Proposed ownership fix**: none needed for correctness — this is a
pre-existing, working, low-traffic file. If a future file split is
attempted, the mechanical unblock is to move just `CallVtableAsClass` and
`kVtableAsSWCCreatureOffset` (the two symbols the operational code actually
needs) into the file that keeps `IsCreatureClientTarget`, and let the
diagnostics file own its own private copy of the vtable-offset constant if
it also needs it (a 4-byte `constexpr` duplicated once is cheaper than a
shared header for one helper).

**Risk**: n/a — no fix proposed.

**Confidence**: high — full file read, zero global state confirmed by
direct inspection.

**Evidence**: engine_radial.cpp (whole-file read, no `g_`/`s_` mutable
declarations found); L126-142 (constants), L736-751 (`CallVtableAsClass`),
L796-799 + L813-916 (`LogTargetDiag`'s diagnostic-only use), L934
(`IsCreatureClientTarget`'s operational use).

**Verdict**: (c) — leave alone. There is no state to reassign, the existing
file works, and Phase 1 already tried and rejected the split this coupling
blocks; re-litigating it here would just repeat that conclusion.

## Cross-references

- Phase 1's own execution log already independently surfaced the state
  shape behind C1, C2 (partially), and C6 while attempting file splits
  that then failed (`docs/refactoring/STATE.md`, candidates 12, 13, 24).
  This report's contribution is the field-level detail (which write site
  clears what, which call site is diagnostic vs. production, the exact
  ordering contract) needed to actually act on C1's and C3's low-risk
  pieces, rather than the file-split-shaped fix that was rejected.
- C4 and C5 are new findings not previously recorded in `STATE.md` — both
  came out of tracing every `acc::transitions::`/`acc::room_topology::`
  cross-call and every other `g_prev_area`-shaped variable in the codebase,
  rather than reading either file in isolation.
