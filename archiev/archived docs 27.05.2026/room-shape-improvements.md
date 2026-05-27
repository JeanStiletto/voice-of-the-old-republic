# Room shape description — improvements brief (speculative)

Status: speculative — analysis pulled from gameplay logs of 2026-05-21 evening
(~4 hours of Taris play across 8 distinct areas). No code changes yet.

Source logs (Steam install logs/):
- patch-20260521-200400.log (~2 MB, 20:04–22:25)
- patch-20260521-204207.log (~12 MB, 20:42–00:02) — main session
- patch-20260521-221759.log (~15 MB, 22:17–02:19) — second main session
- patch-20260522-003751.log (~1.5 MB, 00:37–02:47)

The system reviewed is the `wall_topology` (Path 3) nav-graph-driven
room-shape classifier in `patches/Accessibility/wall_topology.cpp`, plus the
trigger path in `transitions.cpp::SpeakRoomChange` and the per-position
`LookupAt`. Region classifier (path 1/2) is a silent observer at this point.

---

## What was visited

Eight distinct K1 area-build signatures across the evening:

- tar_m02aa — Süd-Apartments
- tar_m02ac — Südliche Oberstadt
- tar_m02ab — Nördliche Oberstadt
- tar_m02ad — Cantina der Oberstadt
- tar_m02ae — Hidden Bek base
- tar_m02af — small apartment interior (7 nodes, trivial)
- tar_m03aa — Lower City courtyard
- tar_m03ae — Vulkar Garage
- tar_m11aa — Hidden Bek inner

Several areas re-entered 3–5× via save/reload + module hops.

Volume in the main session window: ~675 .lyt-room transitions, 73% text-
deduped, ~153 actually announced.

---

## Patterns where description diverges from the real map

### 1. Under-merged corridor strips → same generic label fires repeatedly

Build summaries make the problem visible:

- tar_m02ac: 54 nodes → 51 clusters, 1 merge
- tar_m02ab: 69 nodes → 52 clusters, 7 merges
- tar_m03aa: 31 nodes → 31 clusters, 0 merges

These maps are authored as long chains of degree-2 patrol nodes ~3–5 m
apart. None of the existing merge gates fire (degree≥3 hub, density
cluster, hub absorption), so each node becomes its own cluster. Walking
10–15 m along one physical corridor flips through 2–3 clusters that
share an identical "Korridor X-Y" label.

Symptoms:
- "Korridor Ost-West" spoken 13× in one session
- "Korridor Nord, West" 12×
- "Korridor Nord-West, Ost" 10×
- Same-label re-fires within 7–35 s. Examples:
  - "Kreuzung, Tür Nord-Ost, Süd, Nord-West" at +7s
  - "Korridor Nord, Süd-Ost" at +7s
  - "Korridor Nord-West, Ost" at +10s

Matches the deferred memory `project_walltopo_chain_merge_idea.md`
(merge degree-2 corridor nodes when their connecting axis matches). The
evening's logs make a strong empirical case for promoting it from
deferred to next-up.

### 2. Junctions DEMOTED to "Korridor" because the rotated walkmesh gate is too aggressive

42 `DEMOTED to Korridor` events across the two main sessions. Pattern:
a degree-3 or degree-4 nav-graph junction has neighbours, but one or
two spokes fail the rotated walkmesh probe and the cluster falls back
to a 2-octant corridor label.

Worst case: at (200.0, 100.2) in tar_m02ae, a 9-spoke central hub had
all nine neighbours fail the rotated gate. Cluster ended up labelled
bare "Kreuzung" with no directions:

```
node[71] cluster=71 kind=2 sig=2 pos=(200.0,100.2) label="Kreuzung"
```

The player didn't step on it during this session, but if they had, the
announcement would have been useless. Sighted players see ~8 corridors
branching off; we'd describe it as just "junction".

Also one DEMOTED → "Korridor Nord-West" (single direction word) from
octants 3+7 (NW + SE diagonals collapsing) — incomplete label.

Gate is doing the right thing on wall-bumps but coarse for plazas and
curved spokes.

### 3. Snap-with-no-hysteresis flips labels within 1–3 m

`LookupAt` is a pure "nearest reachable labelled node within 15 m"
function. In node-dense regions (cantina doorways tar_m02ac, junctions
tar_m03aa) the snap flips between cluster centroids 2–4 m apart.
Concrete:

- (399.28,143.60) → (399.90,143.61): 0.62 m, 2 s later, Offene Fläche → big Kreuzung label
- (208.24,88.04)  → (206.40,88.16): 1.8 m, 0 s, Korridor → Kreuzung
- (29.87,70.41) ↔ (30.48,72.75): oscillation Korridor Nord-Süd ↔ Korridor Nord, Ost twice in 2 s
- (180.08,62.44) → (184.14,63.05): 4 m, 0 s, Korridor Ost-West → Korridor Nord, West

Fix shape: per-cluster hysteresis. Once snapped to cluster X, require
the player to be a fixed margin (1–2 m) closer to a different cluster
centroid before switching. Analog of the rotation debounce in
`turn_announce`.

### 4. Path 3 falls through to "Offene Fläche" — three different root causes

16 Offene Fläche fires audited; they split into three buckets that need
different fixes:

#### Bucket 1 — Wall-filtered (~7 fires, NOT solved by raising the cap)

Labelled nodes are physically close (3–15 m) but every one is on the
wrong side of a wall in the cached perimeter:

- (117.7, 71.6) tar_m02ae: nearest labelled at 4.6 m wall-filtered, 56 candidates blocked
- (110.2, 113.8) tar_m02ae: 14.7 m wall-filtered (55 candidates blocked)
- (195.4, 72.4) tar_m02ac: ALL-BLOCKED, 45 candidates
- (393.3, 155.5) tar_m03ae: ALL-BLOCKED, 22 candidates, z=8.2 (upper walkway)
- (399.3, 143.6) tar_m03ae: 10.6 m wall-filtered

This is the wall-cache cross-concern (see "Wall cache shared concern" below).

#### Bucket 2 — Genuinely just over 15 m (~4 fires, raise-cap WOULD fix)

- (119.3, 89.5) tar_m02ac: nearest labelled 15.9 m
- (119.8, 82.9) tar_m02ac: 17.3 m
- (162.1, 101.1) tar_m02ae: 16.3 m
- (160.9, 102.3) tar_m02ae: 17.8 m

Bumping `kMaxSnapM` from 15 m → 20 m catches these cleanly. Bounded
risk: multi-floor maps already have z=8.2 examples in tar_m03ae, so
naive 25 m+ caps risk cross-floor snaps.

#### Bucket 3 — Nearest labelled node is far, but a closer unlabelled node exists (~2 fires)

- (70.7, 30.1) tar_m11aa: nearest labelled 23.8 m, nearest *any* 0.8 m
- (70.6, 33.8) tar_m11aa: labelled 20.1 m, any 2.9 m

The 0.8 m / 2.9 m candidates are degree-1 nav nodes whose Sackgasse
label got dropped by the walkmesh-shape gate. Raising the cap to 25 m
would reach a wrong label across the building. Fix: stop dropping the
local Sackgasse (item 5 below), or have transparent-node nearest-snap
emit a generic "Korridor" rather than scan outward.

### 5. Sackgasse walkmesh-shape gate over-fires

Per-area `FAILED walkmesh-shape gate — dropping Sackgasse label` count
was 51 in tar_m02aa alone. Conservative by design, but only **3
Sackgasse announcements ever fired** across the whole evening. Vanilla
content's degree-1 nodes are recessed corners, alcoves with crates /
lore items, NPC apartment side-doors — many would benefit from being
announced.

Empirical result: cure (rotated 4-ray probe) worse than the disease
(occasional false dead-end at wall-bumps) on Taris content. Worth
tightening thresholds or relaxing for nodes ≥3 m from the perimeter
wall cache.

### 6. Tür-direction labels are informationally rich but spatially ambiguous

"Kreuzung, Nord, Süd, Tür West" reads identically whether the west door
is 1 m or 8 m away. The two most-heard labels were of this form:
- "Kreuzung, Tür Nord, Nord-Ost, Süd, West" × 6
- "Kreuzung, Tür Nord nach Taris - Apartm. Unterstadt, Ost, Tür Süd nach Taris - Nördliche Oberstadt" × 6

Distance-bucketed door qualifiers ("Tür West nahe", "Tür weiter West")
would disambiguate. Not urgent — would make long labels feel less
stale on revisits.

### 7. Cross-room-clear edges — 543 + 743 per session

Graph edges that cross a .lyt-room boundary with no wall and no door —
currently logged as "Possible undetected archway / .wok seam noise" and
used only by the merge-pair gate. Most are 4–6 m long. Many are real
archways/wide openings that don't reach merge criteria. Could be
surfaced as a perceptual feature in their own right ("offener Übergang",
"Bogen") or merged more aggressively when no door is found.

### 8. Same area built 5× — identical output

tar_m03aa reloaded 5× → same 31 nodes / 31 clusters / 0 merges every
time. Determinism good. But it means 0 merges is the steady state for
this layout: the merge thresholds don't fit Lower City courtyard.
Either it needs a different merge profile or the corridor-chain merge
from item 1 absorbs most of it.

---

## Wall cache shared concern (cross-cuts Pillar 1)

Bucket 1 of item 4 above — five Offene Fläche fires where the nearest
labelled nav node was 4–15 m away but every one was rejected by the
wall-reachability filter — points to a problem in the cached perimeter
wall data that the same cache feeds to Pillar 1 wall cues. See the
follow-on investigation section below for the audit plan and findings.

---

## Numeric summary

- 23 distinct area builds (8 unique map signatures across reloads)
- 70 `DEMOTED to Korridor` + 5 `DEMOTED to Sackgasse` total
- 51 dropped Sackgasse per single area-build (tar_m02aa); similar elsewhere
- 49 distinct spoken labels across 168 announcements in the main session
- 0.85 announcements/min average; bursty in node-dense areas

---

## Suggested priority order (for redirection)

1. **Corridor-chain merge** (deferred design from `project_walltopo_chain_merge_idea.md`) —
   biggest single win for Class-A maps (under-merged corridor strips).
2. **Per-cluster hysteresis at `LookupAt`** — fixes the rapid 1–3 m flips
   and cantina-doorway thrash.
3. **Wall cache audit** — fixes wall-filtered Offene Fläche bucket
   *and* potentially improves Pillar 1 cue accuracy. See section below.
4. **Loosen rotated walkmesh gate for high-degree hubs** — fix bare
   "Kreuzung" at the Bek 9-spoke and similar plazas.
5. **Loosen Sackgasse gate or add a "wahrscheinlich Sackgasse" tier** —
   51 dropped labels per area is a lot of silent map content.
6. **Mild `kMaxSnapM` raise 15 m → 20 m** — bounded-risk first step,
   fixes 4 bucket-2 Offene Fläche fires.
7. **Archway labelling** for surviving CROSS-ROOM-CLEAR edges.

---

## .lyt-room reliability audit

### Hypothesis under test

K1's `.lyt`-rooms ("walkmesh sub-meshes" / per-room authoring chunks) are
the foundation for several signals in our code:

- Wall-cache cross-room seam filter (drop edges that two rooms share)
- Wall-cache same-room dedup (drop step/slope duplicates)
- `Transition::SpeakRoomChange` trigger ("room change → speak shape")
- Landmark binding ("you're in room X, fire landmark X")
- Room display name fallback (`GetRoomDisplayName`)
- `WallTopo` `CROSS-ROOM-CLEAR` signal in merge-pass logic

If `.lyt`-rooms are not perceptually meaningful, several of those uses
are foundationally wrong. The memory entries already note this in
fragments (`project_wok_rooms_not_visual_rooms.md`,
`project_kotor_walkmesh_portal_seams.md`), but the evidence was scattered.
With yesterday's logs we have enough position-tagged room IDs to make
the picture precise.

### Method

For every `WallTopo.Compare` (player position with the resolved room ID)
and every `CROSS-ROOM-CLEAR` (edge endpoints with their per-vertex room
IDs), aggregate per `(build-instance, room-id)`: count of samples and
the bounding box of all positions tagged with that room. Per-build keys
prevent area-pointer reuse from polluting the data (one pointer hosted
4 different modules during the evening).

Then ask: how big are the room bboxes? Is room ID stable over
sub-metre distances? How often does the room ID flip per player step?

### What the data says

#### .lyt-room extents — pinpoints and ribbons coexist

`tar_m02ab` (Nördliche Oberstadt, build with 69 nav-nodes, 52 clusters):

- room=1: **252.1 m diagonal**
- room=3: **318.3 m**
- room=22: **352.3 m**
- room=26: **300.7 m**
- room=42: **308.2 m**

while in the same area:

- room=27: 0.7 m
- room=30: 0.3 m
- room=37: 13.2 m
- room=4: 60.3 m

For reference the entire Nördliche Oberstadt module is about 350 m wide.
Rooms 1, 22, 26, 3, and 42 each cover essentially the whole map. Yet
they coexist with rooms that are pinpoints. The same pattern shows up
in `tar_m02aa` (rooms 6, 8, 14 = 70-85 m diagonals in a building that
fits in a ~80 m square) and `tar_m03aa` (Lower City) where every
crossed room had non-trivial extent.

#### Room ID is per-position deterministic

A 1 m XY bucketing across all sampled positions found **only 2 buckets
in the entire evening** where two different rooms applied at the same
1 m cell. So `GetRoomAtIndexed` is a stable function of position — the
wild spans aren't lookup noise, the rooms are genuinely sprawling
ribbons.

#### Player-step room flip rate

For consecutive `WallTopo.Compare` events:

- `tar_m03aa` (Lower City): 185 events → **174 room flips** (94 %),
  41 within 5 m of the previous sample, 10 within 2 m
- `tar_m02ae`: 80 events → **78 flips (98 %)**, 30 within 5 m, 6 within 2 m
- `tar_m02ac`: 30 events → 27 flips (90 %), 8 within 5 m
- `tar_m02ab`: 21 events → 20 flips (95 %)

The player essentially never stays in the same room for two consecutive
samples. The room ID is a high-frequency function of `(x, y)` —
useful as a unique mesh tag, useless as "the place I am right now".

### Verdict per current consumer

#### Wall-cache cross-room seam filter — USES MESH IDENTITY, KEEP

The filter pairs every emitted edge with every other emitted edge from
a DIFFERENT room and drops pairs whose endpoints coincide. It uses
`room_id` as a "different sub-mesh" tag, not as a perceptual marker.
Both consumers — the engine itself joins rooms via portals, and the
walkmesh stores both sides of a portal seam as adjacency=-1 per room —
need exactly this discrimination. The filter works whether rooms are
ribbons or chunks because it only cares about mesh boundaries.

Risk: if two different sliver-rooms happen to share a wall that ISN'T
a portal (just an authoring coincidence — both authored an
adjacency=-1 edge at the same coords), the filter wrongly drops a real
wall. We can't detect this from the logs (silent bug, no fire). Bounded
by the kSeamEpsSq ~1 cm² tolerance — implausible for unrelated walls
to coincide that precisely.

**Conclusion: keep.**

#### Wall-cache same-room dedup — USES MESH IDENTITY, KEEP

Same reasoning: only pairs edges within the same sub-mesh to drop
step/slope and non-manifold duplicates. Mesh-identity. Keep.

#### `SpeakRoomChange` trigger — REPLACE

Today's trigger: `roomIndex != g_prev_room_idx` ⇒ speak. Empirically
this fires every 1-5 m of player movement in dense maps. The
text-dedup gate (`g_last_spoken_room_text`) catches ~73 % of fires by
collapsing same-text repeats, leaving 153 actual announcements over
2 hours of play — better than the raw 675 but still bursty.

**Better signal: `wall_topology` cluster change.** We already compute
`LookupAt` per call and have a stable cluster ID for the player's
current position. Triggering on cluster-change instead of room-change
would drop the trigger rate by ~10 ×, eliminate the need for the
text-dedup escape hatch (rooms wouldn't fire identical text), and
each remaining fire would correspond to a perceptual change.

Side benefit: this removes the dependency on room-stability ticks
(`kRoomStabilityTicks`) since clusters are spatially stable.

#### Landmark binding by room — REPLACE (already done)

Memory entry `project_kotor_text_indirection.md` notes the
`PlayerInLandmarkRange` 15 m proximity gate exists precisely because a
sliver room covered "Zur Oberstadt" across 50 m. The room-based path
was already abandoned in `Transition::SpeakRoomChange`:

```cpp
// Landmark tier removed from the room-transition path 2026-05-13.
// Landmarks now fire via TickProximityLandmarks below, which scans
// every waypoint each tick and triggers on geometric proximity to
// the waypoint position — not on .lyt-room crossing.
```

**Conclusion: already replaced. No action.**

#### Room display name fallback — KEEP WITH GATE, OR DROP

`ResolveRoomSpeech` first tries `GetRoomDisplayName` and only falls
through to `wall_topology` if the name is empty or matches
`IsResrefStyleRoomName`. Across the evening this tier produced 0
fires — every announce was `src=shape`. The vanilla room names are
all resref-style for K1 Taris content, so the tier is dead code in
practice but harmless when it fires.

**Conclusion: harmless dead-codepath today. If we ever encounter
non-resref names (TSL has more), add an extent-cap gate (skip room
name if bbox diagonal > 15 m).**

#### `WallTopo` `CROSS-ROOM-CLEAR` merge signal — KEEP, NOT A PORTAL HINT

Today this signal is logged for diagnostics; the `merge VETOED (door on
edge)` check uses door geometry directly, not room-id changes. So the
existing merge logic doesn't actually trust room boundaries as portal
hints — it uses doors and geometry. The CROSS-ROOM-CLEAR is a
descriptive label in the log, not a load-bearing signal in the code
path.

**Conclusion: descriptive only; not load-bearing. Keep for diagnostics.**

### Net effect

`.lyt`-rooms are reliable as **mesh-identity tags** (their original
walkmesh authoring role). They're not reliable as **perceptual region
identifiers** — proven by 100-350 m bboxes coexisting with 0-1 m
pinpoints in the same area, and 94-98 % per-sample flip rates.

The good news: every load-bearing use of room_id today is already at
the mesh-identity level, EXCEPT `SpeakRoomChange`'s trigger. Replacing
that one trigger with cluster-change recovers the perceptual model
without disturbing the wall cache.

### Suggested action: lift `SpeakRoomChange` off room IDs

Replace the `roomIndex != g_prev_room_idx` trigger with a
`cluster_id != prev_cluster_id` trigger, computed from
`wall_topology::LookupAt`'s returned cluster (already in the sig). Keep
everything else identical:

- Stability ticks gate stays the same (or relax it, clusters are
  already spatially stable)
- Text-dedup stays as a belt-and-braces second filter (would catch
  the rare case where two unrelated clusters share the same label
  string)
- Combat / UI-blocking gate unchanged
- Platz delay path unchanged

Expected impact: trigger rate ~10× lower (the 90-98 % of flips that
were room-only artefacts go away), each remaining fire corresponds to
a perceptual change, the same-label re-fires within 7-35 s reported in
item 1 of the main analysis disappear.

This is also the prerequisite for the corridor-chain merge (item 1)
to deliver its full value — without cluster-change triggering, even a
perfect merge wouldn't reduce announcement rate because each merged
corridor would still get re-announced on every `.lyt`-room boundary
crossing.

### Implementation prerequisites (verified)

Verified against current code (`wall_topology.cpp`, `transitions.cpp`)
so the next session can go straight to editing.

#### 1. cluster_id is not exposed today, but trivially derivable

`wall_topology::LookupAt`'s current signature is:

```cpp
bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig);
```

`outSig` encodes `kind | (kind-specific bits << 8)` (e.g. junction
direction bitmask) — it is a *label fingerprint*, NOT a stable cluster
identity. Two unrelated clusters with the same shape and direction
list share the same sig. Cannot be used as the trigger key.

The cluster_id IS available though: the union-find array
`s_uf_parent[kMaxNodes]` in `wall_topology.cpp` (file-scope static,
reset at the start of each `BuildForArea`) holds the merged cluster
roots. `DumpGraphToLog` already prints `UFFind(i)` per node as the
canonical cluster id. After the merge passes complete, the root for
any node is stable for the lifetime of the build.

Two implementation options, pick during the edit:

- **Option A — add `int& outClusterId` to `LookupAt`.** Inside the
  function, after picking `best`, do `outClusterId = UFFind(best)`.
  All three call sites need updating (see #2). Cleanest API.
- **Option B — precompute and store `node_cluster_id[kMaxNodes]` in
  the `AreaGraph` struct** at the end of `BuildForArea` (one extra
  pass: `for (i) node_cluster_id[i] = UFFind(i);`). Then
  `LookupAt` returns `outClusterId = g_graph.node_cluster_id[best]`.
  ~400 bytes more memory; saves the per-lookup UFFind cost
  (already amortised O(1), so this is negligible). Slightly less
  coupling — callers don't need to know UFFind exists.

Recommendation: Option B. Cleaner separation and the cost is trivial.

#### 2. `LookupAt` has exactly 3 call sites, all in `transitions.cpp`

Verified via grep across `patches/Accessibility/`:

- `transitions.cpp:314` — `ResolveRoomSpeech` (primary speech resolver)
- `transitions.cpp:338` — `LogWallTopoComparison` (diagnostic)
- `transitions.cpp:401` — `SpeakRoomChange`'s Platz-delay peek

No call sites in `map_ui_cursor.cpp`, `spatial_change_detector.cpp`,
or anywhere else (the other files reference `acc::wall_topology` for
`Kind*` enums only). Extending the signature is a localised change.

For the trigger move, the new logic lives in `Tick()` (currently around
line 745, where `GetRoomAtIndexed` is called). The simplest shape:

```cpp
// Replace the GetRoomAtIndexed-based block at transitions.cpp:745.
char shapeBuf[128];
int  shapeSig = 0;
int  clusterId = -1;
bool haveShape = acc::wall_topology::LookupAt(
    area, pos, shapeBuf, sizeof(shapeBuf), shapeSig, clusterId);

if (!haveShape || clusterId < 0) return;  // no graph / outside snap

if (clusterId == g_prev_cluster_id) {
    g_pending_cluster_id    = -1;
    g_pending_cluster_count = 0;
    return;
}
// ... rest of the stability + speak logic unchanged, just s/roomIndex/clusterId/.
```

`SpeakRoomChange` still wants a roomIndex for the landmark/log path
(it calls `GetRoomDisplayName`, `LogWallTopoComparison`, etc.) —
either keep the `GetRoomAtIndexed` call as a SECONDARY read (purely
for those consumers, not the trigger), or pass `clusterId` through
and let `ResolveRoomSpeech` get the room internally. The latter is
slightly cleaner; the former is a smaller diff.

#### 3. Stability-tick gate

Current value: `constexpr int kRoomStabilityTicks = 5` (~80 ms at
60 fps). Introduced because room-IDs flickered 60+ times in 21 s
at boundaries (`patch-20260504-203810.log`).

With cluster_id, boundary flicker should drop dramatically:
- Clusters are spatially stable (player needs ~2-3 m of motion to flip)
- The 1-3 m label flips reported in item 3 of the main analysis are
  still possible at cluster-cluster boundaries, but quantitatively
  fewer
- Hysteresis (item 3 of the priority list) would eliminate the
  remaining flips entirely, but it's a separate change

Recommendation for the first cut: **keep `kRoomStabilityTicks = 5`
unchanged**. Observe the new fire rate in tar_m02ab and tar_m03aa.
Tune down to 2-3 if announcement timing feels laggy with cluster-
based triggers. Don't touch this on the same commit as the trigger
change — they're independent variables.

### Edit checklist (for the next session)

1. `wall_topology.h`: extend `LookupAt` signature with `int& outClusterId`.
2. `wall_topology.cpp`:
   - In `AreaGraph` struct, add `int node_cluster_id[kMaxNodes];`.
   - In `BuildForArea`, after all merge passes complete and clusters
     are finalised, fill `node_cluster_id[i] = UFFind(i);` for all `i`.
   - In `LookupAt`, set `outClusterId = g_graph.node_cluster_id[best]`
     (or `-1` on the Offene-Fläche / no-snap fallbacks).
3. `transitions.cpp`:
   - Rename `g_prev_room_idx` → `g_prev_cluster_id`,
     `g_pending_room_idx` → `g_pending_cluster_id` (etc.) and update
     the reset paths in the area-change + player-loss branches.
   - In `Tick()`, replace the `GetRoomAtIndexed`/`g_prev_room_idx`
     trigger block with the cluster_id-based block (sketch above).
   - Update the three `LookupAt` call sites in this file to pass
     the new out-param (ignore the value at the two that don't need
     it).
   - Decide whether `SpeakRoomChange` still receives roomIndex
     (smaller diff) or just clusterId + does its own room lookup
     internally (cleaner).
4. Build via `kdev build` (or full `kdev dev`).
5. Walk tar_m02ab Nördliche Oberstadt — count announces in the patch
   log. Expect ~15-20 over a 5-minute walk (was ~70 in yesterday's
   logs over a similar span).
6. Confirm in tar_m03aa Lower City (94 % flip rate area) and
   tar_m02ae Bek base (98 % flip rate).
7. Sanity-check tar_m02af (the trivial 7-node interior) for no
   regression.

### Out of scope for this change

- Corridor-chain merge (deferred memory) — separate edit, follow-on.
- Per-cluster hysteresis at `LookupAt` (item 3 priority) — separate.
- Wall cache audit fixes (items in wall-cache section) — separate.

---

## Wall cache audit (follow-on)

The bucket-1 Offene Fläche fires above (5 cases where `LookupAt` ALL-BLOCKED
or wall-filtered the nearest labelled nav node within 4–15 m) led to an
audit of the shared wall cache. This cache also feeds Pillar 1 wall cues,
so improvements here cross-cut.

### What the cache actually contains

`acc::engine::BuildAreaWallCache` (engine_area.cpp:984):

1. For every room in the area, scan the walkmesh. Emit every triangle edge
   with `adjacency==-1` (the engine itself marks "no neighbour face here").
2. Cross-room seam filter: drop pairs of edges that two different rooms
   emit at the same world position (portal seams — both rooms' meshes
   mark their side adjacency=-1 even though the engine joins them as
   walkable).
3. Same-room dedup: drop step/slope and non-manifold duplicates.

What is NOT done: any occlusion / visibility / room-membership culling.
The cache simultaneously holds every room's full perimeter — including
the back walls of rooms the player hasn't entered.

### Bug 1 — 2D-only ray for the walkable filter

`SegmentCrossesWalkmesh` (engine_area.cpp:1194) is purely XY:

```c
float denom = abx * cdy - aby * cdx;   // only x and y
if (t < 0.0f || t > 1.0f) continue;
if (u < 0.0f || u > 1.0f) continue;
```

The z component is interpolated for the hit point but never tested. On
multi-floor maps a ray from an upper-walkway player crosses ground-floor
walls in 2D projection that have nothing to do with the player's actual
line of sight.

Direct evidence in the logs:
- `ALL-BLOCKED at (393.3, 155.5, z=8.2): 22 candidates` — Vulkar Garage
  upper walkway, every labelled nav node rejected
- `WALL-FILTERED at (399.3, 143.6, z=8.2): nearest 10.6 m wall-filtered`
  — same area

Affects: `wall_topology::LookupAt` only. Pillar 1 uses
`ClosestPointDistanceSquared` which IS 3D
(spatial_change_detector.cpp:261-279), so its 5 m awareness range
correctly puts ground-floor walls 8 m below the upper walkway out of
range.

### Bug 2 — No occlusion / room-membership filter on the cache

This is the "walls behind other walls" concern, both consumers hit it.

Cache holds ALL rooms' perimeters at once. So when the player is in
room A, walls belonging to rooms B, C, D, … are also in the cache and
geometrically near (in 2D) the player.

For `wall_topology::LookupAt`: a 2D segment from player P in room A to
candidate node N in room B (reachable via a portal) can cut through an
unrelated wall of room C that happens to lie along the direct line.
The player can walk to N (they'd take the portal), but our reachability
check sees a "wall" on the impossible direct path and rejects N. Likely
cause of the Bek-base ALL-BLOCKED-56 at (117.7, 71.6).

For Pillar 1: walls in rooms the player hasn't entered yet are in
the cache and within the 5 m awareness range. The seam filter only kills
walls SHARED between two rooms (portal seams); back walls of room B not
shared with room A are kept. Result: cues fire for walls in adjacent
rooms across an open portal — walls the player can't perceive as
perimeter of their current space.

Bounded in practice by the 5 m awareness range, but visible on layouts
with lots of interlocking rooms (Cantinas, Bek hub, Vulkar Garage).
Pillar 1's existing `CalibrateInRange` exists partly to suppress the
related save-load "wall of cues" — this would also reduce that pressure.

### Suggested fixes (cross-cuts Pillar 1)

1. **3D-aware ray in `SegmentCrossesWalkmesh`.** Add a Z-overlap check
   before declaring a hit. Cost: two compares per wall. Fixes the
   Vulkar upper-walkway ALL-BLOCKED case and any future multi-floor
   areas (Manaan habitat domes, Sith base decks, Endar Spire decks).

2. **Room-membership filter at the candidate level for `LookupAt`.**
   When testing reachability from player P in room A to candidate N in
   room B, only consider walls whose `room_id` matches A, B, or rooms
   whose portal seam was filtered between them. The `WallEdge.room_id`
   field already exists. Fixes the unrelated-room false-blocks
   underlying the Bek-base ALL-BLOCKED-56.

3. **For Pillar 1: cue only walls in the room the player is currently
   in, plus portal-adjacent neighbours.** Same `room_id` plumbing.
   Reduces cross-room phantom cues. Also reduces the `CalibrateInRange`
   pressure on save-load (fewer in-range walls to seed).

### Verification plan once any fix lands

- Per-area cache stats already logged: emitted vs kept after seam filter.
  Add: per-room cache count.
- Re-run the Vulkar Garage path; ALL-BLOCKED at (393.3, 155.5, z=8.2)
  should disappear (bug 1 fix).
- Re-run the Bek base path; ALL-BLOCKED at (117.7, 71.6) should
  disappear (bug 2 fix).
- Pillar 1 listening test in Vulkar Garage and Cantinas: count distinct
  surface IDs cued per minute before/after. Expect drop with bug-2 fix.
- Watch for regressions: areas where one room genuinely sits open to
  another and the player CAN perceive cross-room walls should still
  cue them (portal-adjacent neighbours rule should cover this).

---

## Addendum — Slums (tar_m04aa) + Kanalisation (tar_m05aa) play, 2026-05-22

Status: speculative — pulled from `patch-20260522-203132.log` (~12 MB,
20:31–00:02) and the follow-on short sessions `patch-20260522-22{3157,
3818,4503,4906}.log` plus `patch-20260523-092124.log`. None of the
already-listed priority work has landed yet, so this is "what those
priorities look like in larger / more open content."

### What was newly visited

Two new K1 area-build signatures dominate the evening:

- `tar_m04aa` — Taris - Slums (Lower City courtyard + ring of alleys,
  Bek + Vulkar gates, Igears Bazar, Rukils Zelt, Gendars Zelt)
- `tar_m05aa` — Taris - Kanalisation (sewers — Gamorrean Rakghoul tunnels)

Slums played ~47 min continuously (20:58–21:45); Kanalisation
~17 min in this log (21:45–22:02) with shorter re-entries across the
later sessions (~5 area-builds of identical signature). The earlier
visited Taris content (Apartments / Oberstadt / Cantina / Bek base /
Vulkar Garage / Lower City courtyard tar_m03aa) is the baseline.

### Build signatures

Slums:
- nodes=165 → clusters=134 (19 % reduction, 31 merged)
- merged-pairs=12, chain-merges=3, multi-node-clusters=10
- per-classification: dead=33 corridor=53 junction=48 open=0

Kanalisation (5 builds; values stable across reloads):
- nodes=124 → clusters=99 (20 % reduction, 25 merged)
- merged-pairs=6, chain-merges=4, multi-node-clusters=4
- per-classification: dead=33 corridor=43 junction=14 **open=9**

Same fragmentation pattern that drove items 1+8 in the main analysis,
plus a new signal: Kanalisation has 9 cluster nodes that get
`kKindOpenArea` at *build time* — the `ClassifyCluster` `externalCount
== 0` path fires because those nav nodes have zero graph neighbours.

### Class A — under-merged corridors: no longer the dominant failure here

Slums announces 0 (zero) `Korridor*` labels across the 47 min.
Kanalisation announces 2 (`Ost-West` × 1 corridor between chambers).
The "Korridor X-Y" repeat-fire pattern that motivated the deferred
chain-merge is essentially absent in these two areas — the geometry
isn't strip-shaped, it's hub-and-radiate.

What did fire:
- Slums: 49 distinct labels in 75 announces; ratios `Kreuzung`/`Platz`/
  `Sackgasse`/`Offene Fläche` = 42 / 22 / 7 / 4
- Kanalisation: 6 distinct labels in 8 announces; ratios `Kreuzung`/
  `Sackgasse`/`Offene Fläche` = 4 / 1 / 3

So the new failure surface is:
1. Repeated re-fires of the same `Kreuzung` label as the player walks
   inside the central plaza (cluster-flip-without-real-region-change).
2. Bare `Offene Fläche` fires inside the Kanalisation chambers where
   the player IS in a real perceptual room — just not one the nav
   graph captures.

### Class B — Slums plaza: same-label re-fire from cluster flips

The dominant offender across the 47-min Slums session:

- `Kreuzung, Nord, Süd-Ost, Süd-West, Nord-West` × 14 (the central plaza)
- `Kreuzung, Nord, Süd-Ost, West` × 8 (~5 m NE of the same plaza)
- `Kreuzung, Ost, Süd, West` × 7
- `Kreuzung, Ost, Süd, Nord-West` × 5
- `Platz, Nord-Ost, Ost, West` × 7 (Hidden Bek gate Platz, multi-node hub)
- `Kreuzung, Haupttor Nord-Ost, Tor des Dorfs, Süd-West, Nord-West` × 6

WallTopo.Compare positions tell the story directly. Clusters 118 and
110 sit ~5 m apart and the player flips between them on every short
walk loop:

```
21:16:57 pos=(251.77,210.89) cluster=118 "Kreuzung, Nord, SO, SW, NW"
21:16:59 pos=(250.73,218.18) cluster=110 "Kreuzung, Nord, SO, West"
21:19:10 pos=(251.32,215.17) cluster=118 "Kreuzung, Nord, SO, SW, NW"
21:19:16 pos=(252.33,219.96) cluster=110 "Kreuzung, Nord, SO, West"
21:21:40 pos=(251.32,215.17) cluster=118 …
21:21:41 pos=(247.14,218.22) cluster=110 …
21:22:46 pos=(251.32,215.17) cluster=118 …
21:22:48 pos=(247.14,218.22) cluster=110 …
21:28:01 pos=(253.12,219.31) cluster=118 …
21:28:25 pos=(253.01,222.21) cluster=110 …
```

8 flips between two adjacent cluster centroids in 12 minutes — the
player is wandering one perceptual plaza and we describe it as two
plazas they walk in and out of. Exactly the "per-cluster hysteresis"
item from the main analysis (priority 2), now reproduced in higher
density in a content type the player actually spends real time in.

The 134 clusters / 165 nodes in this area shape makes the problem
worse than in Oberstadt-class areas: many clusters are degree-3
singletons sitting 4–6 m apart inside what reads as one place.

Net: the cluster-id trigger move described earlier in this doc helps,
but only if hysteresis lands with it. Otherwise `cluster_id` becomes a
noisy trigger source on plaza-class content.

### Class C — Kanalisation: open=9 nodes + bare "Offene Fläche"

Build-time `open=9` is unique to Kanalisation across all areas we've
mapped so far (every other area had open=0 at build). The classified
positions are scattered across the map:

```
node[0]   (160.1, 202.8)   — entrance corner
node[17]  (197.6, 184.3)   — tunnel widening
node[41]  (234.1, 223.4)   — chamber 1 fringe
node[68]  (247.2, 227.7)   — chamber 1 fringe
node[82]  (254.9,  85.9)   — chamber 2 fringe
node[89]  (262.6, 171.7)   — corridor exit pad
node[96]  (270.7, 128.3)   — chamber 3 fringe
node[100] (273.9, 131.5)   — chamber 3 fringe
node[108] (278.4, 137.9)   — chamber 3 fringe
```

These are AI-patrol bookmark nodes the engine placed without graph
connections. ClassifyCluster's `externalCount == 0` branch labels them
`Offene Fläche` (the only sensible fallback when there's no neighbour
to bear a direction off). Once classified that way, they sit in
LookupAt as primary candidates.

Runtime "Offene Fläche" fires in this session:
- 21:45:46 (294.28, 184.28) — z=0 entry tile, src=shape, cluster=-2
- 21:48:03 — second sewer chamber
- 22:02:17 — third sewer chamber

All three are in the big sewer chambers (5–15 m × 5–15 m volumes) that
sit between the corridor segments. To a sighted player these are
visually distinct rooms with combat in them; we read them as
unstructured "open space."

The hub absorption fires fine for the long thin areas (nodes 44–66
fold into hub cluster=52 covering ~7 m × 19 m; nodes 69–83 fold into
hub cluster=74), so K1's narrow Kanalisation corridors are handled.
What we miss is the larger chambers where the engine placed AI
bookmarks not patrol-edges.

### Class D — wall-filtered hits still happen, but mostly degrade gracefully

50 wall-filtered events across Slums (10 cases, 5 retries each in some
spots). Pattern: nearest candidate at 4–10 m wall-rejected, picked a
labelled cluster 6–20 m away instead. Examples:

```
node[86] "Kreuzung, N, SO, SW, NW" at 4.3m WALL-FILTERED  → picked node[79] "Platz, NO, Ost, West" at 6.7m
node[115] "Nord-West, Ost" at 6.5m WALL-FILTERED          → picked node[114] "Kreuzung, Haupttor NO, ToD, SW, NW" at 19.0m
node[61] "Sackgasse, Nord-Ost" at 6.2m WALL-FILTERED      → picked node[65] "Ost-West" at 7.3m
node[61] "Sackgasse, Nord-Ost" at 4.5m WALL-FILTERED      → picked node[70] "Platz, NO, Ost, S, West" at 8.9m
```

The first three pick a meaningful nearby cluster. The fourth picks the
plaza 19 m away with a 5-element direction list — a sighted player
behind that wall sees a corner, not a plaza on the other side of the
building. Wall-cache audit (existing section above) is still the right
fix here; nothing in the new data argues for a different approach.

### Summary of new evidence vs the existing priority list

The priorities in the main analysis stay valid; the new content
re-prioritises within them:

- **Per-cluster hysteresis at LookupAt** (was priority 2) is now
  jointly *most-load-bearing* with the cluster-id trigger move. Slums
  plaza demonstrates that cluster-flip noise outweighs room-flip noise
  on this content type.
- **Corridor-chain merge** (was priority 1) is still desirable but
  contributes nothing to Slums and very little (4 chain-merges) to
  Kanalisation. Its main beneficiary remains Oberstadt-class areas.
- **Loosen rotated walkmesh gate** (was priority 4) — Slums and
  Kanalisation produced no `DEMOTED to Kreuzung` events (versus 42 in
  the older Taris sample). Gate is fine for hub-and-radiate layouts.
- **Loosen Sackgasse gate / "wahrscheinlich Sackgasse" tier** (was
  priority 5) — only 8 Sackgasse fires across both areas combined.
  Less urgent on this content; defer for ruined-Taris and beyond.
- **`kMaxSnapM` raise 15 m → 20 m** (was priority 6) — would not have
  fired on any of the bucket-2-shaped failures in this dataset.

The genuinely new piece is **Class C — large open chambers**, addressed
separately below.

---

## Large open chambers — describing them as more than "Offene Fläche"

### The gap

Path 3 (nav-graph topology) classifies each cluster from the
neighbours it has. A degree-0 node, a sliver of nodes the engine
placed for AI patrol/spawn but didn't connect into the graph, has no
neighbours and resolves to `Offene Fläche` by design. That's the
correct answer at the data level — the algorithm has nothing to say
about a chamber it has no graph for.

But "Offene Fläche" is not what the player wants. A blind player
walking into a 10 m × 10 m sewer chamber needs to know: it's a room,
it has roughly this shape, and these are the ways out. The nav graph
gives us nothing here because the chamber's interior was authored
walkable but unwaypointed.

### Why Path 3 alone is the wrong base for these

Recapping `project_walltopo_raycast_flavor_a.md`:

- **Path 1 — per-face raycast probe.** Cast 8 rays from a sample point
  against the cached wall edges; read distance pattern. *Has the
  signal we need for chambers* — open space looks like 8 long rays.
- **Path 2 — GVD / medial axis.** Compute the skeleton of the
  walkable region. Large chambers show as low-density skeleton
  regions with one branch point. *Also has the signal*, but requires
  the medial-axis math which we have not built.
- **Path 3 — BioWare nav-graph topology.** Use `path_points` +
  `path_connections`. *No signal for chamber interiors* — the data
  source itself is the limit.

The right architectural shape is *not* to replace Path 3, which works
well for corridors, dead-ends, and most junctions. The right shape is
to add a *secondary* probe specifically for the Path 3 fallback case
(`kKindOpenArea` clusters, the runtime fallback at the end of
`LookupAt`, and possibly the bare `Kreuzung` demote case).

This is Path 1, narrowed: instead of per-face probing the whole
walkmesh, probe only at positions where Path 3 has nothing useful to
say. ~9 build-time open clusters in Kanalisation, plus the runtime
no-snap positions. Two-orders-of-magnitude fewer probes than
walkmesh-wide, same signal.

### What an open-chamber announcement should contain

Two parts. Shape, then exits.

**Shape (one of a small enum, derived from the 8-ray pattern):**
- `kammer rechteckig` — 2 cardinal axes long, 2 cardinal axes short
  (long > 2× short). Speak: "Rechteckiger Raum, ~10 × 5 Meter, lange
  Achse Ost-West."
- `kammer quadratisch` — 4 cardinal axes within 30 % of each other,
  diagonals ~same range. Speak: "Quadratischer Raum, ~10 Meter Kante."
- `kammer rund` — all 8 rays within ~20 % of each other (circle / oct).
  Speak: "Runder Raum, ~10 Meter Durchmesser."
- `kammer offen` — 8 rays >12 m, no consistent boundary at the probe
  range. Speak: "Offener Bereich."
- `kammer unregelmäßig` — fallback when none of the above patterns
  match. Speak: "Unregelmäßige Kammer, ~Δ Meter."

Numbers reported as approximate ("~10"), not precise — wall positions
have authoring jitter and the player doesn't need precision.

**Exits (one bullet per nearest labelled cluster the chamber can reach,
ordered by clock-face bearing from probe centre):**
- Speak: "Ausgänge: Nord nach Kreuzung, Süd-Ost nach Tunnel, West Tür
  nach …" — one direction word per exit, plus the destination's
  label (truncated to the noun: Kreuzung, Tunnel, Tür, Sackgasse).

A full announcement: "Rechteckiger Raum, ~12 × 6 Meter, lange Achse
Ost-West. Ausgänge: Nord Tür, Süd nach Korridor, West nach Kreuzung."

### Why this stays tractable

The shape enum is 5 buckets. The probe is the same 8-ray-against-wall-
cache code `region_classifier` already runs in silent-comparator mode
(see `region_classifier` reference, kept on as observer). All the
infrastructure exists; we're just connecting it to one of the
`LookupAt` outcomes.

Exit enumeration uses the existing nav graph: from a chamber probe
position, walk outward to nearest labelled clusters and grab their
bearing + first label word. Bounded — ~5-element list at most for
star-shaped chambers; truncate to N exits if a chamber has more
(unlikely in K1 content).

### What stays finicky

- **Where to probe.** For build-time `open=N` clusters with one node,
  the node position is fine. For the runtime no-snap fallback, the
  player position is the probe — but it might be next to a wall, in
  which case 1 ray is short. Use the centroid of the 4-ray walkable-
  forward area as an offset, the way the Sackgasse alcove probe
  already does.
- **Chamber-vs-corridor at boundaries.** A 6 m × 30 m chamber reads as
  rectangular under the heuristic but is actually a wide corridor.
  Defer the threshold tuning: pick "kammer rechteckig" reports the
  shape honestly, and the long-axis cue tells the player it's a
  corridor-shaped chamber. Player loss = nuisance, not safety.
- **Re-fire control.** Chambers shouldn't re-announce shape every
  cluster-flip. Pin "you're in a chamber" to the open-cluster id (the
  9 open-class nodes already have unique cluster_ids), use existing
  cluster-change trigger, and emit the chamber announcement *once per
  entry*.
- **Exits to far-away things.** Don't enumerate every labelled cluster
  within 15 m — limit to clusters the chamber actually connects to via
  the nav graph (one step out) or via a direct walkmesh ray.

### Three flavours to choose from when this is implemented

1. **Bolt-on probe** — keep Path 3 as today, add the 8-ray probe only
   to `kKindOpenArea` cluster labels and the runtime fallback. Most
   conservative; minimal code change; covers the Kanalisation chambers
   directly and the Slums plaza-edge fallbacks indirectly.

2. **Promote probe to a tier alongside Path 3** — for every cluster of
   size ≥ N (e.g. ≥ 3 nodes, or area-bbox > 8 m diagonal), enrich the
   label with the probe-derived shape. Covers cases like Hidden Bek
   inner where a `Platz` reads "Platz, NO, Ost, West" but is actually
   a square chamber the player could be told the size of.

3. **Hybrid Path 1 + Path 3** — same as Path 3 today for corridors /
   dead-ends / small junctions, but classify clusters whose centroids
   land in genuinely open walkmesh (8 long rays) as `kammer*` from the
   start, not as junctions. This is the largest change; aligned with
   the "GVD/medial axis" intuition but built on the existing 8-ray
   probe we already have.

Recommend (1) for the first iteration: smallest blast radius, fills
the actual gap the new content exposed, leaves Path 3 untouched for
the content it handles well.

### Open questions deferred to the future-iteration session

- Whether the "exit description" form is what the player wants, or
  whether mode-switching ("Du bist in einer Kammer" / press a key to
  get exits) reduces overhead. Either way the probe is the same.
- Whether shape-only announcements ("Quadratische Kammer") work
  better than shape+exits for the player's flow — exits read long.
- Whether to use this same probe to fix the Slums plaza
  inside-vs-edge problem (priority 2 hysteresis) — a probe inside the
  plaza says "10 m square" and stays anchored, instead of cluster
  centroids 5 m apart flipping the label.
