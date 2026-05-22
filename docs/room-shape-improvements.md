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
