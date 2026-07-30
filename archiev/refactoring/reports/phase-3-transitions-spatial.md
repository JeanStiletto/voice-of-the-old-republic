# Phase 3 scan — area transitions and spatial change detection

Scope: `transitions.cpp` (1459 lines) / `transitions.h` (77),
`spatial_change_detector.cpp` (1231) / `spatial_change_detector.h` (62),
`spatial_wall_surfaces.cpp` (476) / `spatial_wall_surfaces.h` (92).

Method: full read of all six files, top to bottom (two paginated reads for
`transitions.cpp`, one pass each for the other five). Targeted greps to
verify every finding before writing it down:
- `strings::Get\(` across all six files — dead-null-check sweep.
- `0x[0-9a-fA-F]+` across all six files — raw-hex-literal sweep.
- `IsWorldSpeechGatedImpl` — to confirm a suspected duplicate forward
  declaration.
- `kFireDedupTol` codebase-wide — to confirm two constants are genuinely
  unreferenced outside their own definition, not just outside this file.
- `kRadToDeg` / `57.295779` codebase-wide — to establish the sibling-file
  convention before flagging the raw literal here as an outlier.
- `GetPlayerYawDegrees` / `GetPlayerPosition` in `spatial_change_detector.cpp`
  — to check an include comment against actual call sites.
- `IterateLandmarks` / `MarkLandmarkClaimedByDoor` / `RebuildLandmarkCache` /
  `AttachLandmarksToDoors` codebase-wide — to map every caller before
  proposing the C4 fix (single-caller confirmed for both cross-file
  functions).
- `change_detector` / `transitions::Tick` in `core_tick.cpp` — to verify a
  comment's claim about dispatch order instead of trusting the prose.
- `struct WallEdge` / `struct Vector` / `GameObjectKind` — to locate which
  header a used type actually lives in, for the candidate-28 note and to
  rule out an `engine_offsets.h` dependency that isn't really there.
- Read `docs/llm-docs/walk-nav-and-walkmesh.md`'s `kotor_walkmesh_quirks`
  section — to check whether a "diagnostic to remove later" comment in
  `spatial_wall_surfaces.cpp` is still accurate.

Two of these came back clean and are worth stating explicitly rather than
silently omitting, per the evidence standard:
- **Dead `strings::Get()` null checks: zero found in this batch.** Only two
  call sites exist (`transitions.cpp:170` and `:1423`), both feed straight
  into `snprintf` with no null check either way.
- **Raw hex literals matching an `engine_offsets_fields.h` constant: zero
  found.** No `.text`/`.data` offset literals appear in code in any of the
  six files; the only `0x...` token is inside a comment in `transitions.h:8`
  (a hook address reference, correctly left as a comment per the
  do-not-touch list).

## Section A — general low-level cleanup

### A1 — Duplicate forward declaration of `IsWorldSpeechGatedImpl` (transitions.cpp:739-740)

What's there: `IsWorldSpeechGatedImpl()` is forward-declared twice —
once at `transitions.cpp:707-708` ("Forward declaration — defined below
near TickProximityLandmarks"), and again at `transitions.cpp:739-740`
("Forward declaration so TickProximityLandmarks can call it"). The actual
definition is at line 913. A C++ forward declaration is visible for the
rest of the translation unit once seen, so the first declaration already
covers every caller after it, including `TickProximityLandmarks`.

Why it's a problem: harmless to the compiler, but it reads as if two
different call sites each needed their own declaration, which isn't true
— the second one is pure leftover, most likely from an edit that moved
code around without checking whether an existing declaration already
covered the new position.

Proposed change: delete lines 739-740 (declaration + its comment).
Risk: mechanical (compiler-checked — if any code between the two actually
needed the declaration texts to differ, it wouldn't compile).
Estimated line delta: -2.

### A2 — Player-loss reset hand-duplicates the Phase-2 Reset*State() groups, and has already drifted (transitions.cpp:1057-1067)

What's there: `Tick()`'s player-loss branch (no live player position)
inlines its own 11-line reset block:

```cpp
g_prev_area       = nullptr;
g_prev_cluster_id = acc::room_topology::kClusterIdNone;
g_pending_cluster_id    = acc::room_topology::kClusterIdNone;
g_pending_cluster_count = 0;
g_prev_friendly_room_name[0] = '\0';
g_last_spoken_room_text.clear();
g_last_spoken_pos_valid      = false;
g_lm_prox_pending_idx        = -1;
g_lm_prox_pending_count      = 0;
g_lm_prox_last_spoken_idx    = -1;
g_pending_platz_valid        = false;
acc::room_topology::Reset();
acc::narration::Reset();
```

Compare this to `ResetRoomSpeechState()` (transitions.cpp:1004-1014),
`ResetLandmarkProximityState()` (1017-1022) and `ResetPendingPlatzState()`
(1025-1027) — the exact per-group reset functions Phase 2 candidate A1
introduced specifically so this kind of inline clearing would live in one
place per group. The area-change branch a few dozen lines later
(`transitions.cpp:1153-1156`) already calls all three. This block predates
that refactor and was never migrated onto it.

Why it's a problem — concretely, not just style: `ResetRoomSpeechState()`
also clears `g_flap_prev_text`, `g_flap_prev_ms` and `g_flap_cur_ms` (the
boundary-flap dedup state). This hand-written block does not. Today that
gap is inert — `g_prev_area` is set to `nullptr` here, so the very next
area observation trips the area-change branch, which calls
`ResetRoomSpeechState()` for real before the flap state is ever consulted.
But it is exactly the kind of silent per-variable drift Phase 1's
candidate 13 got burned by: two clearing sites for the same state group
that look equivalent and quietly aren't. The next person adding a field to
`ResetRoomSpeechState()` has no reason to know this second site exists.

Proposed change: replace lines 1057-1067 with:
```cpp
g_prev_area = nullptr;
ResetRoomSpeechState();
ResetLandmarkProximityState();
ResetPendingPlatzState();
```
(keep the two `Reset()` calls into `room_topology` and `narration`
unchanged immediately after). This is the same three functions the
area-change branch already calls, so it also makes the two reset sites
provably identical instead of independently maintained.

Risk: low, not pure-mechanical — it changes *what* gets cleared (adds the
three flap fields), even though the added clears are currently
unobservable for the reasons above. Needs-in-game-test: trigger a
player-loss/reload cycle (return to the main menu mid-game, or load a
save) and confirm room/landmark narration on the next area sounds
unchanged — this exercises exactly the state this touches.
Estimated line delta: -8.

### A3 — Dead constants + stale design comment for a mechanism that isn't wired up (spatial_change_detector.cpp:41-54)

What's there: a 12-line comment block describing a "same-closest-point
dedup tolerance" — the idea that when several surfaces meeting at a
T/X-junction all report the same vertex as their closest point, candidates
within `kFireDedupTolMeters` (5cm) of each other should merge at
"candidate-collection time" — followed by:
```cpp
constexpr float kFireDedupTolMeters   = 0.05f;
constexpr float kFireDedupTolSquared  = kFireDedupTolMeters * kFireDedupTolMeters;
```
Neither constant is referenced anywhere else in the file, or anywhere else
in the codebase (checked with a repo-wide grep, not just a per-file one —
this is exactly the kind of check candidate 22's postmortem asked future
phases to do before calling something dead).

Why it's a problem: this reads as a real, load-bearing part of the T1
firing pipeline, but the actual candidate collection in wall pass 3
(`spatial_change_detector.cpp:827-914`) never merges by point-tolerance —
it picks at most one candidate per cardinal *sector* (Front/Left/Back/
Right), which sidesteps the junction-vertex problem a different way (by
construction, only one surface per sector can ever become a candidate).
Most likely history: an earlier design used point-tolerance merging, the
sector-based approach superseded it, and this block was never cleaned up.

Proposed change: delete the comment block and both constants
(lines 41-54). If the point-tolerance merge idea is still wanted as a
belt-and-braces safeguard on top of the sector binning, that's a design
decision for the user, not a mechanical cleanup — flagging here rather
than silently dropping the idea.
Risk: mechanical (unused `constexpr`, no other reference anywhere in the
repo).
Estimated line delta: -14.

### A4 — Stale "remove once understood" comment on a diagnostic that is now documented and permanent (spatial_wall_surfaces.cpp:186-189)

What's there: the anomaly-classification block inside
`BuildSurfaceDescriptors()` opens with:
```cpp
// Anomaly breakdown counters — each anomalous surface increments
// exactly one of these so we can tell which case dominates the
// 15-33% flagged rate seen on Apartments / Upper City. Phase 1
// diagnostic; remove once the cause is understood.
```
`docs/llm-docs/walk-nav-and-walkmesh.md`'s `kotor_walkmesh_quirks` section
(lines 200-254) documents all three causes by name — non-manifold
same-room edge duplicates, near-vertical edges, and multi-floor walls at
the same XY footprint — and explicitly says the `multi-elev` vs `broken`
split in this diagnostic's log line is how a caller is meant to tell them
apart today, not a debugging aid awaiting removal.

Why it's a problem: the comment tells the next reader this ~180-line
block (185-364) is a temporary Phase-1 leftover safe to delete, but the
project's own doc treats its output as the permanent way to classify a
recurring, understood walkmesh-authoring pattern. That's a direct
contradiction between comment and current status, exactly the "stale
comment that contradicts the code" category Section A is scanning for.

Proposed change: this is a documentation fix, not a code deletion — per
the brief's rule to only propose removing diagnostic logging when a
question is *genuinely* closed, and here the doc says the output is still
consulted, not closed. Reword the comment to state what
`walk-nav-and-walkmesh.md` already says: this is the permanent
classification the doc's `multi-elev`/`broken` split depends on, not a
removal candidate. If the user disagrees and considers the investigation
closed, that's a separate, larger decision (whether to simplify the
whole ~180-line categorisation down to a single count) — noted here as a
question, not decided.
Risk: mechanical for the comment reword; the larger simplification (if
chosen) would need the anomaly rate re-measured first, so it isn't scoped
here.
Estimated line delta: ~0 (comment reword) or -140ish if the user later
chooses full simplification (not proposed as part of this item).

### A5 — Repeated raw `57.29577951308232f` literal with no local named constant (spatial_change_detector.cpp:560, 785, 1036)

What's there: the radians→degrees conversion factor appears three times
in this file as a bare float literal:
```cpp
float worldBearing = std::atan2(dy, dx) * 57.29577951308232f;   // :560
float worldBearing = std::atan2(dy, dx) * 57.29577951308232f;   // :785
float wb = std::atan2(dy, dx) * 57.29577951308232f;             // :1036
```
Why it's a problem: every other file in the codebase that does this exact
conversion defines a local `constexpr float kRadToDeg = 57.29577951308232f;`
first — confirmed by grep across `announce_degrees.cpp`, `camera_orient.cpp`,
`camera_announce.cpp`, `camera_spin_guard.cpp`, `engine_compass.cpp`,
`engine_player.cpp`, `guidance_description.cpp`, `guidance_beacon.cpp`,
`minigame_swoop_audio.cpp`, `minigame_turret.cpp` and `probe_camera_state.cpp`
(eleven sibling files, all with a named constant). `spatial_change_detector.cpp`
is the one file in the codebase using the raw literal inline, and it
already defines other named constants nearby (`kSectorCooldownMs`,
`kAwarenessRangeHysteresisMeters`, etc.) — the inconsistency is with the
file's own convention as well as the codebase's.
Proposed change: add `constexpr float kRadToDeg = 57.29577951308232f;`
near the other file-scope constants and replace the three inline literals.
Risk: mechanical (pure constant extraction, identical value).
Estimated line delta: +1/-0 net (one new line, three call sites shrink by
a few characters each, not a line count change).

### A6 — Include comment overstates what's actually called (spatial_change_detector.cpp:13)

What's there: `#include "engine_player.h"        // GetPlayerPosition / GetPlayerYawDegrees`.
Only `GetPlayerPosition` (line 622) is actually called in this file.
`GetPlayerYawDegrees` appears once, only inside a comment at line 395
("matches engine_player::GetPlayerYawDegrees") explaining the bearing
convention — never as a live call.
Why it's a problem: minor, but a maintainer trimming this include later
(e.g. while doing the candidate-28 narrow-header migration) would
reasonably trust the comment and think twice before removing a "used"
symbol that isn't actually used.
Proposed change: trim the comment to `// GetPlayerPosition`.
Risk: mechanical (comment-only).
Estimated line delta: 0.

### A7 — Small duplication across the object-state table helpers (spatial_change_detector.cpp:297-333)

What's there: `FindOrAddObjectState`, `FindObjectState` and
`RemoveObjectState` each independently loop `for (auto& s : g_object_state)`
looking for `s.handle == handle`. `FindOrAddObjectState` in particular
re-implements the same linear search `FindObjectState` already does,
before falling through to its own second loop for an empty slot.
Why it's a problem: three near-identical 5-8 line scans over the same
256-entry table; small, but a textbook "an abstraction should own this"
case, and any future change to the matching rule (e.g. add a generation
check) would need updating in three places.
Proposed change: have `FindOrAddObjectState` call `FindObjectState`
for its existing-handle case instead of re-scanning:
```cpp
ObjectState* FindOrAddObjectState(uint32_t handle, bool& outIsNew) {
    outIsNew = false;
    if (handle == 0) return nullptr;
    if (ObjectState* existing = FindObjectState(handle)) return existing;
    for (auto& s : g_object_state) {
        if (s.handle == 0) {
            s.handle = handle; s.last_distance = 0.0f; s.last_cued_at = 0;
            outIsNew = true;
            return &s;
        }
    }
    return nullptr;
}
```
Risk: mechanical (same behaviour, one fewer duplicated loop).
Estimated line delta: -5.

### A8 — `transitions::Tick()` does five separable jobs in one 317-line function (transitions.cpp:1035-1352)

What's there: one `Tick()` handling, in sequence: the movie-foreground
gate, the player-position-lost gate, the area-null gate, a ~70-line
area-change block (input reacquire, area-speak, narrated-target clear,
discovery/endar re-key, landmark cache rebuild, nav-graph build, four
group resets), the same-area nav-graph retry, four independent per-tick
sub-tickers (landmark cache recheck, proximity landmarks, pending Platz,
gated-cluster refire), then ~150 lines resolving and committing the
cluster/friendly-name change trigger (including the minor-cluster dwell
gate).

Why it's in scope: the brief and `STATE.md` both explicitly carry
function-level decomposition into Phase 3 (the `ClassifyCluster`/
`BuildForArea` precedent in `room_topology.cpp`). This is the same shape
of problem: one function, several jobs, no shared state that couples them
tighter than "written one after another."

Proposed change — extract as same-TU static functions inside the existing
anonymous namespace (no header changes, no new cross-file coupling, so
this does **not** repeat the candidate-13 file-split mistake):
- `HandleAreaChange(void* area)` — lines ~1088-1156, the `if (area !=
  g_prev_area)` block.
- `ResolveClusterTrigger(void* area, const Vector& pos, char*
  friendlyBufOut, size_t friendlyBufSize, int& clusterIdOut, bool&
  earlyOutNoData)` or similar — lines ~1221-1259 (LookupAt + friendly-name
  resolve + the `kClusterIdNone` early return).
- Leave the minor-cluster dwell gate (~1294-1329) and the final commit
  block attached to `Tick()` itself, since they're short and directly
  gate the one remaining `SpeakRoomChange` call — splitting them out would
  just relocate the same three lines of context twice.

This is a proposal for the user to size and approve, not a fully worked
diff — the exact split points are estimates from reading the function,
not yet verified against a second pass counting every variable each
candidate function would need to close over. Flagging that explicitly per
the evidence standard, the same way candidate 13's scan should have.

Risk: needs-in-game-test if executed — the area-change block in
particular drives room/landmark narration on every area load. Exercise:
walk between two areas and confirm the "Bereich"/room-name announce and a
landmark proximity announce both still fire.
Estimated line delta: ~0 net (pure relocation within the file), readability
gain not size gain.

### A9 — `spatial::change_detector::Tick()` does four separable jobs in one 588-line function (spatial_change_detector.cpp:620-1207)

What's there: reference-position selection (player vs. view-mode cursor),
area-change/reference-swap calibration, four wall passes (per-edge →
per-surface, per-surface → per-sector + T2 candidate, per-sector
threshold fire, K-closest fire), the object loop (party-follower
exclusion, T2 candidate, T1 fire), the T2 foremost-in-front debounce/fire,
and the tick-summary log — all in one function.

Why it's in scope: same rationale as A8. This is the largest single
function in the batch and the comments themselves already label it in
passes ("Walls: pass 1", "pass 2", "pass 3", "pass 4"), which is a strong
signal the seams are already understood, just not extracted.

Important nuance — **this one is NOT as clean as it looks**, and the scan
found the same trap that burned candidates 13 and 24: the wall passes and
the object loop both feed into the *same* T2 "foremost" candidate
(`t2_best` / `t2_best_dist` / `t2_best_eff` / `t2_best_pos` / `t2_best_cue`)
before the debounce block consumes it. A naive three-way split (walls /
objects / T2-debounce) would need all five of those threaded through as
in-out parameters (or a small `T2Candidate` struct passed by reference to
both the wall-pass and object-pass functions), plus the six summary
counters (`walls_in_range`, `sector_candidates`, `walls_cued`,
`objs_in_range`, `objs_cued`, `sector_log`) threaded back out for the
final log. That's a real but bounded amount of plumbing, not a blocker —
just sized correctly instead of assumed free.

Proposed change: extract, all as same-TU statics, keeping `Tick()` as the
orchestrator:
- A small `struct T2Candidate { Foremost best; float dist, eff; Vector
  pos; NavCue cue; };` to carry the five T2 fields as one unit instead of
  five loose out-params.
- `TickWalls(...)` — the four wall passes (~732-965), taking the
  reference position/yaw/settings in and updating `T2Candidate&` plus
  returning the three wall summary counters.
- `TickObjects(...)` — the object loop (~984-1087), same `T2Candidate&`
  in-out, returning the two object summary counters.
- `TickForemostDebounce(const T2Candidate&, ...)` — the existing
  first-tick-suppress / settle-or-held / fire logic (~1089-1188).

Risk: needs-in-game-test if executed (this is the whole Pillar 1 wall/
object proximity-cue pipeline) — exercise: walk toward a wall until an
approach cue plays, walk past a door/container until a proximity cue
plays, and turn to sweep the "foremost in front" cue across two adjacent
walls.
Estimated line delta: ~0 net (relocation), plus one new small struct.

## Section B — AI-pattern findings

### B1 — T2 candidate-selection block copy-pasted between the wall pass and the object pass (spatial_change_detector.cpp:801-812 and 1035-1049)

What's there — wall pass (801-812):
```cpp
if (t2_enabled && playerSec == WallSector::Front &&
        ss.best_distance <= range) {
    Foremost cand = { FeatureKind::Wall, s, 0u };
    float eff = T2EffectiveDistance(cand, ss.best_distance);
    if (eff < t2_best_eff) {
        t2_best_eff  = eff;
        t2_best_dist = ss.best_distance;
        t2_best      = cand;
        t2_best_pos  = ss.best_closest_point;
        t2_best_cue  = acc::audio::NavCue::Wall;
    }
}
```
object pass (1035-1049):
```cpp
if (t2_enabled && dist <= range) {
    float wb = std::atan2(dy, dx) * 57.29577951308232f;
    if (ClassifyRelativeBearing(wb - effectiveYaw) == WallSector::Front) {
        Foremost cand = { FeatureKind::Object, -1, handle };
        float eff = T2EffectiveDistance(cand, dist);
        if (eff < t2_best_eff) {
            t2_best_eff  = eff;
            t2_best_dist = dist;
            t2_best      = cand;
            t2_best_pos  = pos;
            t2_best_cue  = cue;
        }
    }
}
```
The five-line "if closer, adopt as new best" body is identical in
structure and updates the same five variables in the same order in both
places.
Why it's a problem: textbook copy-paste that an abstraction should own —
exactly Section B's named category. A future change to the selection rule
(e.g. adding a sixth field, or changing the comparison) has two call
sites to keep in sync, silently, with no compiler check either.
Proposed change: a small helper (works whether or not A9's `T2Candidate`
struct is adopted):
```cpp
void ConsiderT2Candidate(const Foremost& cand, float rawDist,
                         const Vector& pos, acc::audio::NavCue cue,
                         Foremost& best, float& bestDist, float& bestEff,
                         Vector& bestPos, acc::audio::NavCue& bestCue) {
    float eff = T2EffectiveDistance(cand, rawDist);
    if (eff < bestEff) {
        bestEff = eff; bestDist = rawDist; best = cand;
        bestPos = pos; bestCue = cue;
    }
}
```
called from both sites with their respective `cand`/`rawDist`/`pos`/`cue`.
Risk: mechanical (pure refactor, same five assignments, same order).
Estimated line delta: -8.

### B2 — `GetCachedWalls` re-checks a condition its own callee already encodes (spatial_change_detector.cpp:1211-1217)

What's there:
```cpp
bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount) {
    int count = ws::GetWallCount();
    if (count <= 0) return false;
    outBuf   = ws::GetWallBuffer();
    outCount = count;
    return outBuf != nullptr;
}
```
`ws::GetWallBuffer()` (spatial_wall_surfaces.cpp:400-402) is defined as
`return g_wall_count > 0 ? g_walls : nullptr;` — the exact same condition
this function just checked via `GetWallCount()`. The `return outBuf !=
nullptr` at the end is a belt-and-braces re-check of a fact the `count <=
0` guard three lines above already established.
Why it's a problem: not a bug (both conditions agree, always), but it's
the "duplicate a check one frame up the call stack" pattern the brief
calls out for Section B specifically — it reads as if `GetWallBuffer()`
could return `nullptr` for some other reason the `count` check doesn't
cover, which it can't.
Proposed change:
```cpp
bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount) {
    int count = ws::GetWallCount();
    if (count <= 0) return false;
    outBuf   = ws::GetWallBuffer();
    outCount = count;
    return true;
}
```
Risk: mechanical (the two functions are defined in terms of the same
`g_wall_count`, verified by reading both).
Estimated line delta: -1.

## Findings (possible bugs — user decides)

None. Two candidates were investigated and ruled out as live bugs before
being downgraded to the cleanup items above (A2, and a check on
`g_flap_prev_pos`/`g_flap_cur_pos` not being reset by
`ResetRoomSpeechState()` — inert, because `IsFlapRepeat` gates on
`g_flap_prev_text` being non-empty first, and text is what gets cleared).
Not listing either as a "possible bug" per the instruction not to report
speculative findings.

## C4 — landmark `doorMatched` ordering contract (assigned to this batch)

**The problem, precisely.** `Landmark::doorMatched` (transitions.cpp:134)
is written only by `room_topology::AttachLandmarksToDoors`
(room_topology.cpp:2181-2276), reaching into `transitions.cpp`'s private
landmark cache through two functions `transitions.h` exposes purely for
this purpose:
- `IterateLandmarks(int& cursor, char* nameOut, size_t nameBufSize,
  Vector& posOut, int& outLandmarkIdx)` — stateless cursor walk over the
  cache (transitions.h:39-41, defined transitions.cpp:968-983).
- `MarkLandmarkClaimedByDoor(int landmarkIdx)` — sets the flag by opaque
  index (transitions.h:46, defined transitions.cpp:985-988).

`doorMatched` is read back inside `transitions.cpp`'s
`TickProximityLandmarks` (line 851: `if (g_landmarks[i].doorMatched)
continue;`) to suppress a redundant standalone proximity announce for a
landmark whose name was already embedded in a door/cluster label.

The unenforced contract: `AttachLandmarksToDoors` only produces correct
matches if `RebuildLandmarkCache` has already populated the cache **for
the same area** this call. Confirmed by reading both call sites
(`RebuildLandmarkCache` is `transitions.cpp`-private, called from exactly
two places, both of which call `AttachLandmarksToDoors`/`BuildForArea`
immediately after with the same `area` pointer):
- `transitions.cpp:1125` (`RebuildLandmarkCache(area)`) then
  `transitions.cpp:1151` (`room_topology::BuildForArea(area)`, which
  itself calls `AttachLandmarksToDoors(area)` internally at
  `room_topology.cpp:2417`).
- `transitions.cpp:805-806`: `RebuildLandmarkCache(area);` immediately
  followed by `acc::room_topology::AttachLandmarksToDoors(area);`.

Both call sites get it right today **by construction** — nothing checks
it. If a future edit reorders either site (e.g. moves the recheck
sub-ticker earlier, or a K2-port change restructures `Tick()`), the
failure mode is silent: `AttachLandmarksToDoors` would walk whatever
`g_landmarks[]` happens to hold — either a stale cache from the *previous*
area, or (immediately after an area change, before the first
`RebuildLandmarkCache` of the session) all-zeroed slots — and either match
landmarks to the wrong doors or simply match nothing, with no error, just
a wrong or empty `matched=`/`unmatched=` summary line that looks like
normal output.

**Proposed fix.** `AttachLandmarksToDoors` already receives the `area`
pointer as a parameter and currently discards it
(`room_topology.cpp:2181`: `void AttachLandmarksToDoors(void* /*area*/)`).
That's the seam: thread it into `IterateLandmarks` instead of dropping it,
and have the landmark cache record which area it was last rebuilt for so
`IterateLandmarks` can refuse to walk a mismatched cache.

Concrete proposed signature change in `transitions.h`:
```cpp
// Walk the cache. cursor=0 on first call; advanced past each populated
// slot. landmarkIdx is the opaque key for MarkLandmarkClaimedByDoor.
//
// `area` must be the SAME pointer RebuildLandmarkCache last populated
// the cache for. This makes the "AttachLandmarksToDoors only works after
// RebuildLandmarkCache has run for the current area" ordering contract
// enforced here instead of relied on by construction at each call site:
// a mismatch returns false immediately (no landmarks visited) and logs
// once, so a future ordering bug shows up as a loud "cache mismatch" line
// instead of a silent "0 landmarks matched" that looks like normal
// output.
bool IterateLandmarks(void* area, int& cursor,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut, int& outLandmarkIdx);
```

Supporting change (not the public signature, but needed to make it work,
described for completeness): `transitions.cpp` would track which area the
cache belongs to — a new file-local `void* g_landmark_cache_area =
nullptr;` alongside `g_landmark_count`, set at the top of
`RebuildLandmarkCache(void* area)` (before its existing `if (!area)
return;`, so a null-area rebuild also records "cache is for no area").
`IterateLandmarks` then opens with:
```cpp
if (area != g_landmark_cache_area) {
    acclog::Write("Transition",
                  "IterateLandmarks: cache is for area=%p, caller "
                  "requested area=%p — cache not (yet) rebuilt for this "
                  "area; returning no landmarks", g_landmark_cache_area, area);
    return false;
}
```

`MarkLandmarkClaimedByDoor` does not need its own gate: every call to it
happens synchronously, in the same `AttachLandmarksToDoors` loop, with an
`landmarkIdx` that just came out of a (now-validated) `IterateLandmarks`
call on the same tick — there is no interleaving that could hand it a
stale index once the read side is enforced.

**Caller-side impact.** Exactly one call site to update:
`room_topology.cpp:2197-2198`
(`acc::transitions::IterateLandmarks(cursor, name, sizeof(name), lmPos,
landmarkIdx)`) becomes
`acc::transitions::IterateLandmarks(area, cursor, name, sizeof(name),
lmPos, landmarkIdx)` — `area` is already in scope as
`AttachLandmarksToDoors`'s own (currently-unused) parameter, so this also
turns a previously-dead parameter into a used one.

Risk if executed: low, mechanical — one caller, compiler-checked signature
change, and the new failure path (return false + log) can only ever
trigger on what is currently a latent bug, not a working path. Would still
want an in-game check that landmark-embedded door labels (e.g. "Kreuzung,
Ost, Tür Süd, Zur Oberstadt") still suppress the standalone landmark
announce a second later, to confirm the threading didn't change which
`area` value flows through.

Not implemented — this is a design proposal per the brief; the user
decides whether to execute it.

## Candidate 28 — narrow-header include opportunities

- `transitions.h` / `transitions.cpp` — both include the full
  `engine_offsets.h` aggregator (367 declarations post-C8-split) but the
  only symbol either file uses from that family is `Vector`, which lives
  in `engine_offsets_types.h`. (`GameObjectKind` — the other offset-ish
  symbol used in `transitions.cpp` — actually lives in `engine_area.h`,
  already included for `AreaObjectIterator`, not in the offsets family at
  all.) Could narrow both includes to `engine_offsets_types.h`.
- `spatial_change_detector.h` / `spatial_change_detector.cpp` — same
  situation: only `Vector` is needed from `engine_offsets.h`;
  `WallEdge` (the other engine type used) comes from `engine_area.h`,
  already included directly for `AreaObjectIterator` /
  `GetPlayerPosition`-adjacent helpers.
- `spatial_wall_surfaces.h` — same: only `Vector` needed from
  `engine_offsets.h`; `WallEdge` again comes from the already-present
  `engine_area.h`.
- None of the three files in this batch include `engine_player.h` /
  `engine_area.h` / `engine_panels.h` / `engine_reads.h` as *aggregators*
  in the C8 sense — checked, and unlike `engine_offsets.h` those four
  headers have no narrower sibling headers yet (only `*_internal.h`
  variants, which are cpp-only and not consumable by these files). So the
  brief's parenthetical about them being "aggregators now" doesn't apply
  to anything in this batch; noting it so the next reviewer doesn't
  re-check the same thing.

## Files scanned with nothing to report

- `spatial_wall_surfaces.h` — clean; well-scoped public surface, every
  declaration matches an actual `.cpp` definition, comments accurate.
