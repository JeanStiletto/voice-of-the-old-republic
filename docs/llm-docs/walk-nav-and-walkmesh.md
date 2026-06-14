# Walk, pathfinding & walkmesh topology (RE reference)

Leader-walk recipe, nav-graph layout, dialog-speaker resolution, and the WallTopo walkmesh clustering model.

> Migrated from the agent memory store on 2026-06-14 (memory-system cleanup).
> Each section below is one former memory note, preserved verbatim. Verify
> addresses/offsets against current code before relying.

## addmovetopoint_leader_broken
_The leader CAN walk to a bare walkmesh coordinate (nav-graph A*, around corners) via AddMoveToPointAction — IF you prime ActionManager(creature,8) first and pass secondaryPoint=(0,0,0). Shipped in acc::guidance::WalkTo. See docs/llm-docs/interaction-dispatch-model.md_


**Resolved + shipped (2026-06-13, verified in-game).** The leader walks to an
arbitrary walkmesh coordinate with full nav-graph pathfinding (around corners).
The recipe (recovered from the native click-to-move handler
`CSWSMessage::HandlePlayerToServerInputWalkToWaypoint @0x005235b0`):

1. Prime the action subsystem: `CSWSCreature::ActionManager(creature, 8) @0x004f8770`
   (mode 8 = move) BEFORE `AddMoveToPointAction @0x004f8b60`. Without it the move
   bails (`field427_0xa8c` stays 2 = "never ran").
2. Pass `secondaryPoint=(0,0,0)` (a non-zero secondary triggers a line-of-sight
   straight-move shortcut that stops at the first wall; zero skips it so A* runs),
   `actionId=0xffff`, `objectId2=INVALID`.
3. Leave player input ENABLED (`SetPlayerInputEnabled(false)`→`SwitchMode(0)`
   suppresses the walk).

Live-confirmed: Shift+- to a map pin walked the PC ~45 m around corners.

**What pumps the player's queue:** `CSWSObject::RunActions` — NOT the NPC AI
scheduler (which skips the player). A standalone queue-write only moves the
player if something drains it; the verb dispatch path (and the ActionManager
prime above) is what triggers `RunActions`.

**Shipped routing** (`acc::guidance`): bare points / map-pins → `WalkTo`
(the AddMoveToPointAction recipe above); game objects → `UseObject(handle)`
(`CSWSObject::AddUseObjectAction @0x0057c810`, walks the player to the target then
fires the kind-appropriate USE callback, handles cross-area transitions). The
engine also walks the player as the setup phase of verbs — `AIActionUseObject
@0x0057e8c0` / `AIActionDialogObject @0x0057a470` both `AddMoveToPointActionToFront`
on the player.

We supply A* over the static per-area nav graph
([[project_kotor_nav_graph_layout]]); "recreate the nav graph" was never needed.
The old "AddMoveToPointAction is permanently NPC-only / FollowLeader gate" theory
was wrong — it was the missing ActionManager prime. Full model:
`docs/llm-docs/interaction-dispatch-model.md`.

---

## kotor_nav_graph_layout
_CSWSArea path_points + path_connections offsets, PathPoint stride, CSR adjacency encoding — verified 2026-05-11 via in-game probe + Lane's Ghidra type DB_

KOTOR's per-area nav graph (the data Bioware's pathfinder reads, e.g. `CSWSModule::PlotPath`) lives on `CSWSArea`. Fully decoded 2026-05-11 by the Phase 5 lay-off 1 probe.

**Offsets** (from `swkotor.exe.h:9184-9190`, verified live):

- `CSWSArea.path_points_count` at `+0x238` (ulong)
- `CSWSArea.path_points` at `+0x23c` (PathPoint*, heap)
- `CSWSArea.path_connections_count` at `+0x240` (ulong)
- `CSWSArea.path_connections` at `+0x244` (ulong*, heap)

**PathPoint stride: 16 bytes.**

```
+0x00  Vector position    (12 bytes: float x, y, z)
+0x0c  uint32 csr_offset  (start index into path_connections[])
```

**Adjacency: CSR (compressed sparse row) over `path_connections[]`.** Node N's neighbours = `path_connections[meta_N .. meta_{N+1}-1]`. Last node's terminator = `path_connections_count`. Sample CSR sequence observed: `0, 1, 2, 5, 8, 10, 11, 14` → node 0 has 1 neighbour, node 1 has 1, node 2 has 3, node 3 has 3, node 4 has 2, etc.

**path_connections[] is a flat `uint32[]` of neighbour node indices.** Edges are symmetric undirected (0↔2, 1↔3 observed in samples).

**Sample sizes:** Endar Spire start area = 51 path_points / 104 connections. Other observed area = 104 / 104. Small enough that linear-scan nearest-point + A* is trivial.

**Why:** Phase 5 (Pillar 3) needs a path solution for the beacon, but the engine's `AddMoveToPointAction` refuses to plot for the leader (separate memory). We run A* over this graph ourselves; data IS the engine's authoritative nav data, we just supply the search.

**How to apply:** When building `guidance_pathfind` in Phase 5 lay-off 2 — read these offsets directly from the area pointer (`acc::engine::GetCurrentArea()`). No engine call needed; pure data access. SEH-guard the reads per project convention. Cache the parsed graph once per area-change (rebuilds on `transitions::OnAreaChange`).

---

## dialog_speaker_resolution
_How to identify the current conversation partner from any in-dialog state — and the per-line speaker caveat for multi-party cutscenes_

Every server-side game object (CSWSObject) has `dialog_owner: CSWSObject*` at +0x54 (constant `kServerObjectDialogOwnerOffset`). When the player is in a conversation, reading this off `GetPlayerServerCreature()` gives the NPC partner — no need to walk into the CSWGuiDialog panel.

CSWGuiDialog has an untyped `undefined4 owner` at +0x19c0 (right before `replies_listbox` at +0x19c4 in Lane's struct dump) but its type is unknown; the server-side path is cleaner and verified.

**Caveat:** `dialog_owner` is the *conversation partner*, not the *per-line speaker*. For 1-on-1 dialog and barks this IS the speaker. But it's read off the PLAYER creature, so it's **null whenever the player isn't a participant** — overheard NPC-to-NPC cutscenes resolve to nothing → appearance=-1 → default-to-speak, leaking human VO to TTS (observed Taris cantina Christya/noble scene, 2026-06-01).

**Per-line speaker — solved via GUI poll (commit 85bef70, UNTESTED in-game as of 2026-06-01):** RE'd the dispatch — `CSWSDialog::SendDialogEntry @0x5a4010` resolves each node's speaker tag with `GetSpeaker()`, passes it (3rd object arg) to `SendDialogEntryNode @0x5a13d0`, which `ServerToClientObjectId`-converts it and hands it to `CGuiInGame::HandleDialogEntry @0x631d80`. HandleDialogEntry stashes it at **CGuiInGame+0x170 (`kCGuiInGameDialogSpeakerOffset`, CLIENT object id), set on EVERY entry regardless of player participation** (siblings: +0x174 listener, +0x178 prev speaker, +0x184 third). `CloseDialog @0x6332b0` resets +0x170 to the 0x7f000000 sentinel on conversation end, so outside-conversation barks read the sentinel and fall through. `ResolveDialogSpeaker` now reads +0x170 first (`ClientToServerObjectId` → `ResolveServerObjectHandle`), falling back to the +0x54 player-partner path. No hook needed — pure poll, fits [[feedback_hook_vs_poll_principle]].

**Why:** Needed for the `HumanSubtitles` toggle in `dialog_speech.cpp` — we classify by speaker's `appearance_type` (CSWSCreature +0xa4c) to decide whether to read the subtitle (alien VO incomprehensible) or suppress it (human VO clashes with TTS).

**How to apply:** When a feature needs "who is the PC talking to right now", chain `GetPlayerServerCreature()` → `+0x54 dialog_owner` → kind-check (=Creature, 5). For per-line / multi-party / overheard accuracy, read `CGuiInGame+0x170` (client id) via the chain above — that's the authoritative current speaker.

---

## walltopo_obstacle_fragmentation
_Cluttered rooms (crates/furniture) fragment WallTopo into a junction maze because static obstacles are baked into the room .wok_


In rooms full of static placeables (Ebon Hawk cargo bay: Metallkiste,
Sektor, Plastikzylinder, Vorräte crates), WallTopo reads one open room as a
maze of degree-3 "Kreuzung" clusters. Root cause confirmed 2026-06-10
(patch-20260610-134338.log): the crate footprints are **cut into the room's
`.wok` walkmesh as interior boundary loops**, so they sabotage the area on
two axes at once:

1. **Nav graph:** authored path points thread the gaps between crates →
   dense degree-3 nodes routing around each box → reads as junctions.
2. **Openness probe:** `BuildAreaWallCache` builds the wall cache purely
   from room surface meshes (it never reads placeables), yet the crate loops
   are *in* the .wok, so the cached walls include them. Clearance rays from a
   boxed-in node are short (node 8 at (30,37.5): N ray = 1.8m = exactly the
   Sektor crate at (30.3,39.3)) → fails the `nodeOpen` test → classified as a
   junction, never an area. Wall count was 576 for the small ship vs ~281 in
   a crate-free area.

Consequence: crate-walls are **indistinguishable from architecture by
source** (both are room-walkmesh boundary edges). The principled fix is
"room shape ignores furniture": detect small interior closed-loop surfaces
(the surface clustering in spatial_wall_surfaces already groups edges into
surfaces) and exclude them from the openness/room-shape probe, so a cluttered
bay reads as one open Bereich and the crates stay as object narration. Meaty
— affects every furnished area. Cheaper partial relief = anti-bounce on
cluster re-entry (transitions.cpp) which cuts the back-and-forth repetition
but not the maze of distinct labels. See [[project_walltopo_two_pass_arch]]
and [[project_wok_rooms_not_visual_rooms]].

---

## walltopo_two_pass_arch
_WallTopo clustering = 2 passes (core-merge + straggler-absorb, bridge-junctions kept separate); heap-backed std::string labels; later +/-35deg curved-corridor merge (7ff9bfe) built on top_


WallTopo (wall_topology.cpp `BuildForArea`) was rewritten (2026-06-10) from six
merge passes to **two**, superseding the older per-pass / successor-path design
sketches (archived):

- **Pass 1 core-merge**: ONE nav-edge walk. Unions a clear, door-free edge
  when endpoints share a kind: junction (both deg≥3), room (both
  room-class), open (both open-class), corridor (both straight deg-2).
  Replaced old junction / room-coalesce / open-coalesce / corridor-chain.
- **Pass 2 straggler-absorb**: folds a deg≤2 corner/spoke into an adjacent
  multi-node core — graph-adjacent (cascades to fixpoint) or bbox-contained
  for graph-unattached nodes. Replaced hub + bbox absorption. **Skips
  bridge-junctions** (a node reaching ≥2 distinct cores stays its own
  junction so the connection stays audible).
- **Pass 3**: per-cluster classification (ClassifyCluster) — unchanged.

Info-source split is now clean: nav graph = connectivity + doors; clearance
probe = shape. **Adjacency (a real engine edge) is the connector for every
rule** — proximity-across-a-throat over-merges can't happen by construction
(that was the Oberstadt store/cantina-avenue bug, [[project_kotor_walkmesh_portal_seams]]).

Also this session: region labels are now **heap-backed std::string** end to
end (new `strfmt.h` = printf→std::string) — announcements never truncate
(the store-door exit was being cut off a 96-byte buffer). LookupAt signature
is now `std::string&`.

A later refinement shipped a **degree-2 curved-corridor merge** (commit 7ff9bfe,
wall_topology.cpp ~L187-207): adjacent deg-2 corridor nodes merge when their
connecting direction agrees within ±35°; sharp bends stay split.

Known remaining gaps to judge (Oberstadt / Slums plaza, the heaviest
open-merging areas): (1) node 24 (frontage↔plaza)
still folds into the plaza because its frontage side is via a pendant, not a
Pass-1 core; (2) the western store pocket is still several clusters
(frontage / corridor / store-junction node 9) — merging across shape-classes
is a separate question; (3) exits to a named neighbouring area read as bare
compass directions ("Ost" toward the avenue, no "Cantina"/destination cue) —
a labeling improvement, not clustering. See [[project_bereich_area_label_wip]].

---

## kotor_walkmesh_portal_seams
_Per-room mesh adjacency=-1 marks BOTH the room's outer wall and shared room-boundary seams; wall cache must seam-filter or Pillar 1/2/3 see phantom walls_

KOTOR's walkmesh is split into per-room sub-meshes. Each triangle stores per-edge adjacency ("which neighbour triangle is on the other side?" or `-1` for "none"). Inside one room, `-1` *usually* means "outer perimeter wall".

But areas are stitched together from multiple rooms via **portals / AABB structures**, NOT via triangle adjacency. So at the boundary between room A and room B (e.g. a doorway, or an open seam where two corridor sections meet):
- Room A's mesh ends → its edge there has `adjacency=-1`
- Room B's mesh ends at the same world coords → its edge there has `adjacency=-1`
- Both rooms emit the shared edge as a "perimeter wall" even though the engine treats it as walkable.

**Why:** the engine's portal/AABB join is invisible to the per-room adjacency scan; we can only see the meshes themselves.

**How to apply:**
- `engine_area::BuildAreaWallCache` does post-scan **seam filtering**: for every pair of emitted edges in different rooms with coincident world-space endpoints (≤1cm), drop both. Real outer walls (emitted by ONE room only) survive; double-emitted boundary seams are dropped.
- Observed seam rate on Endar Spire start area: 30/494 ≈ 6%. Larger hub areas (Taris, Dantooine) likely higher — they're stitched from more rooms. O(N²) filter cost stays fine at observed N.
- Three consumers depend on this filtering being correct:
  - **Pillar 1** wall-narration cues (`spatial_change_detector`)
  - **Pillar 2** view-mode virtual-cursor collision (`view_mode::SegmentCrossesWalkmesh`)
  - **Pillar 3** path-smoothing in `guidance_pathfind::ComputePath`
- Symptom of regression / breakage: smoother's `Smooth: BLOCK` log entries where the blocking edge has `material_id` for a *floor* material (e.g. 10=Metal) and is emitted from a single room but lines up suspiciously with a known walkable seam → either seam filter wasn't called or epsilon needs tuning.
- Verified live 2026-05-12 on Endar Spire: removed SE-detour from path to Einfache Sicherheitstür, 4 waypoints → 2.

---

## kotor_walkmesh_quirks
_Non-manifold edge duplicates, vertical-edge artefacts, and multi-elevation patterns in K1's walkmesh — and how engine_area + Pillar 1 handle them as of f6a1a5e_

K1's walkmesh has three recurring authoring patterns that bite anything building
2D walls or regions from it. All three are now handled in `engine_area::Build-
AreaWallCache` as of commit f6a1a5e on the wall-topology branch.

**1. Non-manifold same-room edge duplicates.** Two faces in one room with
adjacency=-1 on the same physical edge — typically a flat floor wall (mat=10)
paired with a slanted step face (mat=7) sharing the foot at Z=0. Per-face wall
extraction emits the edge twice. Match signature: XY footprints coincide AND
at least one 3D endpoint matches exactly. The 3D-shared-endpoint requirement
preserves real multi-floor walls.

**Why:** the engine emits one wall edge per adjacency=-1 face, so a single
physical wall backed by two faces shows up twice. material_id differs between
the two but isn't used by any production path.

**How to apply:** if you read walls and assume one wall = one edge, you'll
double-count. Consume the post-dedup buffer from `engine_area::BuildAreaWall-
Cache` (or via `acc::spatial::change_detector::GetCachedWalls`).

**2. Near-vertical edges.** K1's walkmesh stores 3D edges that run essentially
straight up/down at one XY point — the side of a step or a small cliff. Some
have sub-cm XY drift at the foot. Filter threshold: 5cm² of XY squared length
(matches Pillar 1's `kEndpointTolMeters`). Below that, the edge isn't a
navigable 2D wall and downstream XY-only clustering treats zero-XY-length as
"always collinear", gluing unrelated walls together via the vertical's XY foot.

**Why:** Pillar 1's `EdgesAreSameSurface` has a degenerate-length early-out
that accepts any pair as collinear, so vertical edges act as pivot points.

**How to apply:** if you read raw face data, skip edges with XY length < 5cm.
The wall cache already does this at emit time.

**3. Multi-floor walls (same XY, different Z).** Lower-floor wall at Z=0 and
upper-floor wall at Z=3 with identical XY footprint. No 3D-shared endpoint
distinguishes these from step/slope pairs. Pillar 1 clusters them as one
surface because clustering is XY-only — this is consistent with sound output
(closest-point lookup picks the right wall per player elevation) but produces
a 2D-irreducible "surface" that walltopo can't reduce to one segment.

**Why:** the engine renders both floors but our 2D abstraction can't
distinguish them.

**How to apply:** in walltopo / cell decomposition, expect surfaces whose
endpoints span >0.5m of Z range. Treat them as legit multi-elevation features,
not bugs. The `surface-descriptor anomalies` diagnostic in
spatial_change_detector tags these as `multi-elev` separate from `broken`.

**API for clean wall consumption:** `acc::spatial::change_detector::Get-
WallSurfaceCount() / GetWallSurfaceDesc(idx, &desc)` returns clustered wall
surfaces reduced to straight segments (a, b endpoints + unit direction +
length). Use these instead of rebuilding merge logic per consumer.

---

## open_clusters_were_phantom_wall_artifact
_WallTopo kKindOpenArea counts across all areas were caused by the 2D SegmentCrossesWalkmesh blocking nav edges against other-floor walls; the 3D z-guard (abc6db7) collapsed every open count to 0_


Reharvest on 2026-06-09 proved the entire build-time `open`
(`kKindOpenArea`) signal in `room-shape-dataset.md` was an artifact of
`engine_area::SegmentCrossesWalkmesh` being **2D-only**: a wall edge on a
different floor whose XY projection landed on a nav edge was counted as a
blocker, isolating genuinely-connected nav nodes into bogus open clusters
(`ClassifyCluster` externalCount==0 → Offene Fläche).

**Fix (commit abc6db7):** added a z-overlap guard — a wall blocks only
when its edge z at the crossing is within `kWallCrossZToleranceM` (2m,
shared constant in engine_area.h) of the ray. The guard only ever *skips*
a hit, so it can't create new blocks: single-floor areas build
byte-identically (Ebon Hawk, Slums, Daviks); only multi-level areas change.

**Every open-positive area collapsed to open 0:**
- Manaan Östliches Zentrum 22–27→0 (z-skipped 83), Landebucht 24–27→0 (88),
  Westliches Zentrum 3–8→0 (17)
- Dantooine Jedi-Enklave 12–20→0 (36)
- Taris Kanalisation 9→0 (28) — even the "original open-chambers exemplar"

**Consequences:**
- The "large open chambers → describe shape+exits" feature (Problem B in
  the room-shape brief) was largely chasing this bug. There are no genuine
  build-time open clusters. Big rooms now classify as connected
  junction/corridor clusters, matching the engine nav graph. The real open
  question merges with the under-merge work: should large multi-node
  junction/corridor clusters carry a size/shape descriptor derived from the
  cluster's node footprint (cleaner than the 8-ray probe).
- `wall_topology::LogNavWallCrossings` is a permanent build-time regression
  guard: nav edge crossing a cached wall (3D) ⇒ phantom wall. Found 0
  crossings in every multi-floor area (cache is phantom-free; the 2D ray
  was the whole problem).
- **Only genuine same-floor phantoms found:** Daviks Anwesen (tar_m08aa) —
  4 nav edges cross wall edges carrying surfacemat 10 (Metal = walkable
  floor) in room 9 = missed portal seams. Lead for the cause-#2 fix: drop
  wall edges whose surfacemat is walkable. See [[project_kotor_walkmesh_portal_seams]].

---

## wok_rooms_not_visual_rooms
_GetRoomAtIndexed returns engine sub-mesh id; safe for mesh-identity tagging (wall-cache seam filter, same-room dedup); MUST NOT be used as a perceptual region signal (trigger speech, bind landmarks, infer adjacency). Use wall_topology cluster_id for the perceptual role._


K1's `.wok` rooms (one `<area>_<NN>a.wok` per sub-mesh in
`build/wok-extract/`, exposed at runtime via `GetRoomAtIndexed`) are
walkmesh authoring chunks. They are NOT perceptual rooms a sighted
player would identify, and they're not even spatially localised — many
sprawl across the whole map as ribbons.

## The rule

- **OK to use room_id**: as a tag distinguishing different walkmesh
  sub-meshes when pairing/deduplicating walkmesh-derived geometry. The
  wall-cache cross-room seam filter and same-room edge dedup in
  `engine_area::BuildAreaWallCache` are correct uses — they're asking
  "are these edges from the same sub-mesh?" which is exactly what
  room_id answers.
- **NOT OK to use room_id**: as a perceptual region signal. Anything
  shaped like "the player just changed rooms, fire X" or "you're in
  room R, the landmark for R is Y" is broken because rooms aren't
  perceptual regions.

## Empirical evidence

### Old evidence (2026-05-17, tar_m02aa)
- Central "Platz" cluster spans 4 different `.wok` rooms (ids 17, 25,
  27, 8) with no door or archway between them.
- Small ~12 m loot room behind security door[5] is split into 2 rooms
  (1 and 4) with no visible divider.

### New evidence (2026-05-21 evening, several Taris areas)

Per-build .lyt-room bounding boxes from gameplay positions and edge
endpoints across 4 hours of Taris play:

- `tar_m02ab` Nördliche Oberstadt (~350 m wide module): rooms 1, 3,
  22, 26, 42 have diagonals of **252-352 m** — each spans essentially
  the whole map as a ribbon. Same area has rooms 27 (0.7 m), 30
  (0.3 m), 37 (13 m) — pinpoints coexisting with ribbons.
- `tar_m02aa` Süd-Apartments: rooms 6, 8, 14 have 70-85 m diagonals in
  a ~80 m building footprint.
- `tar_m03aa` Lower City: every crossed room had non-trivial extent.

Per-step room-flip rate from consecutive `WallTopo.Compare` events:
- tar_m03aa: 185 events → 174 flips (**94 %**), 41 within 5 m, 10
  within 2 m
- tar_m02ae: 80 events → 78 flips (**98 %**)
- tar_m02ac: 30 events → 27 flips (**90 %**)
- tar_m02ab: 21 events → 20 flips (**95 %**)

Room ID is per-position deterministic at 1 m resolution (only 2
buckets in the whole evening showed two room IDs in one 1 m cell),
so the wild extents aren't lookup noise — the rooms are genuinely
sprawling.

**Why:** `.wok` rooms are walkmesh authoring chunks the level
designer carved for engine processing (rendering occlusion, AI
patrol partitions, etc.) — chunks that minimise sub-mesh face count
or fit some authoring tool, not chunks that match human perception.

## How to apply

### Legitimate uses (mesh identity)
- **`engine_area::BuildAreaWallCache` cross-room seam filter** pairs
  edges across rooms to detect portal-shared duplicates. Uses room_id
  as "different sub-mesh" tag. Keep.
- **Same-room edge dedup** in the same cache build pairs edges within
  one room to detect step/slope and non-manifold duplicates. Uses
  room_id as "same sub-mesh" tag. Keep.
- **Per-door diagnostic `front_room` / `back_room`** in
  `wall_topology::SnapshotDoors` — descriptive logging only, not load-
  bearing. Fine.

### Misuses — replace these (or already replaced)
- **`SpeakRoomChange` trigger** (`roomIndex != g_prev_room_idx`):
  fires every 1-5 m of player motion. Text-dedup catches ~73 % but
  bursty leaks remain. Replace with `wall_topology::LookupAt`
  cluster_id change.
- **Landmark binding by room** (was: "you're in room R → speak R's
  landmark") — already replaced 2026-05-13 with proximity-based
  `TickProximityLandmarks` (15 m radius around the waypoint position).
  Don't re-introduce.
- **WallTopo merge-gate "same room ⇒ merge"** — tried as gate on
  2026-05-17 and got 10 vetoes per area, only 3 were actual visual
  separators. Replaced with `FindDoorOnEdge`. Don't re-introduce.
- **Room display name fallback** (`GetRoomDisplayName` before the
  shape label) — harmless in K1 Taris (all rooms have resref-style
  names filtered out), but if TSL or other content gives non-resref
  names, gate by bbox extent (skip if room spans > 15 m diagonal).

### Replacement for the perceptual role
`wall_topology::LookupAt` returns a cluster_id in the sig. Clusters
are merged nav-graph regions designed to be spatially stable and
perceptually meaningful. Use cluster_id wherever the question is
"have I entered a new perceptual region?"

---

