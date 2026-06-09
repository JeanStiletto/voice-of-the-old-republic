# Room-shape build-signature dataset

Per-area `WallTopo` (Path 3) nav-graph build signatures, harvested from the
patch DLL logs. This is the raw-data companion to the analysis brief
`archiev/archived docs 27.05.2026/room-shape-improvements.md` — the brief
interprets the patterns; this file is the measured ground truth per area,
kept current as new planets are played.

## Provenance

- Source: every `WallTopo: BuildForArea:` summary line across all patch logs
  in `<install>/logs/patch-*.log` (571 logs at time of writing,
  2026-05-20 → 2026-06-09).
- Area names resolved by matching each build's `area=<ptr>` to the most
  recent preceding `Transition: area -> '<name>' (areaPtr=<ptr>)` line
  (pointers are reused across modules, so latest-wins matching is required).
- Module resref codes (`tar_m0...`, `danm...`, `manm...`, `ebo_...`) taken
  from authoritative `SaveLoad ... planet="<code>" area="<name>"` rows.
- Dantooine, Manaan and Ebon Hawk were added 2026-06-09 from the
  2026-06-08/09 sessions — the first step toward a complete picture before
  improving the open-chamber and merge handling.

## How to read an entry

Each area lists one representative current-code build, then the spread seen
across rebuilds where it varies. Fields, in order:

- **nodes → clusters** — raw nav-graph nodes folded to merged clusters,
  with the % reduction.
- **merged-pairs** — adjacent-pair merges (the merge-pair gate).
- **vetoed** — `merge-vetoed-by-door` (a merge blocked because a door sits
  on the connecting edge).
- **chain-merges** — degree-2 corridor-chain absorptions.
- **multi-node-clusters** — clusters that ended up holding more than one
  node (the rest are singletons).
- **classification split** — `dead` (Sackgasse) / `corridor` (Korridor) /
  `junction` (Kreuzung/Platz) / `open` (Offene Fläche / `kKindOpenArea`),
  counted over clusters at build time.

Determinism note: `nodes`, `clusters`, `merged-pairs` and `chain-merges`
are stable across rebuilds of the same area under the same code. The
classification split jitters by a few clusters at boundaries (nodes whose
degree flips between corridor/junction across rebuilds), so those are given
as ranges where observed.

---

## Endar Spire

### end_m01aa — Kommandomodul
- 77 → 44 clusters (43% reduction, 33 merged)
- merged-pairs 4, vetoed 0, chain-merges 4, multi-node-clusters 8
- dead 6–7, corridor 27, junction 10, open 0–1
- (Older-classifier rebuilds in the log show 45–73 clusters; the 44-cluster
  build is the current-code steady state.)

### end_m01ab — Steuerbord-Deck
- 26 → 19 clusters (27% reduction, 7 merged)
- merged-pairs 1, vetoed 1, chain-merges 3, multi-node-clusters 2
- dead 4, corridor 12, junction 3, open 0
- Tiny area; classification noisy across versions (16–22 clusters seen).

---

## Taris

### tar_m02aa — Südliche Apartments
- 51 → 25 clusters (51% reduction, 26 merged)
- merged-pairs 6, vetoed 4, chain-merges 2, multi-node-clusters 6
- dead 8, corridor 10, junction 7, open 0

### tar_m02ab — Nördliche Oberstadt
- 69 → 48 clusters (30% reduction, 21 merged)
- merged-pairs 7, vetoed 0, chain-merges 4, multi-node-clusters 9
- dead 12–15, corridor 20–23, junction 11–13, open 0–1

### tar_m02ac — Südliche Oberstadt
- 54 → 42 clusters (22% reduction, 12 merged)
- merged-pairs 1, vetoed 0, chain-merges 9, multi-node-clusters 5
- dead 9, corridor 23, junction 10, open 0

### tar_m02ad — Nördliche Apartments
- 56 → 21–27 clusters (≈55% reduction, ~30 merged)
- merged-pairs 9, vetoed 2, chain-merges 6, multi-node-clusters 5–7
- dead 3–8, corridor 8–14, junction 5, open 0–5

### tar_m02ae — Cantina der Oberstadt
- 77 → 23 clusters (70% reduction, 54 merged)
- merged-pairs 27, vetoed 0, chain-merges 0, multi-node-clusters 5
- dead 15, corridor 5, junction 3, open 0
- High merged-pairs / low chain: a tight doorway-dense interior.

### tar_m02af — Versteck
- 7 → 7 clusters (trivial, 0 merged)
- corridor 7, everything else 0.

### tar_m03aa — Unterstadt (Lower City courtyard)
- 31 → 26 clusters (16% reduction, 5 merged)
- merged-pairs 0, vetoed 0, chain-merges 5, multi-node-clusters 2
- dead 9, corridor 13, junction 4, open 0
- (Pre-chain-merge logs show the 31 → 31 / 0-merge baseline cited in the
  brief; chain-merge now folds 5.)

### tar_m03ad — Apartm. Unterstadt
- 61 → 25 clusters (59% reduction, 36 merged)
- merged-pairs 6, vetoed 4, chain-merges 0, multi-node-clusters 6
- dead 12, corridor 6, junction 7, open 0

### tar_m03ae — Javyars Cantina
- 72 → 17 clusters (76% reduction, 55 merged)
- merged-pairs 23, vetoed 0, chain-merges 0, multi-node-clusters 7
- dead 9, corridor 4, junction 4, open 0
- The most aggressively merged area on record (pair-merge dominated).

### tar_m03af — Swoop-Plattform
- 17 → 17 clusters (trivial, 0 merged)
- dead 6, corridor 10, junction 1, open 0.

### tar_m04aa — Slums
- 165 → 134 clusters (19% reduction, 31 merged)
- merged-pairs 12, vetoed 0, chain-merges 3, multi-node-clusters 10
- dead 33, corridor 53, junction 48, open 0
- The plaza re-fire case study from the brief's Slums addendum.

### tar_m05aa — Kanalisation  [3D-fix corrected 2026-06-09]
- 124 → 96 clusters (23% reduction)
- merged-pairs 6, vetoed 0, chain-merges 7, multi-node-clusters 7
- dead 17, corridor 62, junction 17, **open 0** (z-skipped 28)
- PHANTOM CORRECTION: pre-fix this read `open 9` and was cited as the
  original "open chambers" exemplar. With the 3D wall guard (commit
  abc6db7) the open count is 0 — the sewer is multi-level (z-skipped
  28), and the 2D ray was isolating connected nav nodes into bogus
  Offene-Fläche clusters. There were never any real build-time open
  chambers here.

### tar_m05ab — Obere Kanalisation
- 54 → 45 clusters (17% reduction, 9 merged)
- merged-pairs 0, vetoed 0, chain-merges 9, multi-node-clusters 8
- dead 13, corridor 24, junction 5, open 3

### tar_m08aa — Daviks Anwesen
- 177 → 112 clusters (37% reduction, 65 merged)
- merged-pairs 14, vetoed 8, chain-merges 11, multi-node-clusters 21
- dead 30, corridor 60, junction 21, open 1
- Largest node count on Taris; most door-vetoed merges (8).
- Flat (z-skipped 0) — build unchanged by the 3D fix. BUT the nav/wall
  crosscheck found the **only 4 genuine same-floor phantom walls** in the
  whole reharvest: nav edges crossing wall edges that carry material 10
  (Metal = a walkable floor surface) in room 9 — missed portal seams the
  coincidence filter didn't pair. Lead for the cause-#2 fix: drop wall
  edges whose surfacemat is walkable.

### tar_m09aa — Sith-Basis
- 71 → 68 clusters (4% reduction, 3 merged)
- merged-pairs 1, vetoed 3, chain-merges 2, multi-node-clusters 2
- dead 13, corridor 40, junction 15, open 0
- Barely merges — corridor-dense base, nearly all singleton clusters.
- (`tar_m09ab` is a tiny 7-node sub-area that stays 7 → 7.)

### tar_m10aa — Schw. Vulkar-Basis
- 89 → 69 clusters (22% reduction, 20 merged)
- merged-pairs 4, vetoed 3, chain-merges 9, multi-node-clusters 7
- dead 16, corridor 37, junction 16, open 0

---

## Dantooine (new — 2026-06-08)

### danm13 — Jedi-Enklave  [3D-fix corrected 2026-06-09]
- 120 → 76 clusters (37% reduction)
- merged-pairs 12, vetoed 0, chain-merges 7, multi-node-clusters 14
- dead 18, corridor 45, junction 13, **open 0** (z-skipped 36)
- PHANTOM CORRECTION: pre-fix this read `open 12–20` and was the second
  most open-dominated area. With the 3D wall guard the open count is 0 —
  the enclave halls/courtyard are multi-level (z-skipped 36). The
  "open-dominated" picture was a 2D-ray artefact, same as Manaan.

### danm14aa — Hof
- 85 → 72 clusters (15% reduction, 13 merged)
- merged-pairs 6, vetoed 0, chain-merges 3, multi-node-clusters 5
- dead 21–23, corridor 32–33, junction 16, open 0–3

### danm14ab — Matale-Anwesen
- 163 → 145 clusters (11% reduction, 18 merged)
- merged-pairs 4, vetoed 0, chain-merges 8, multi-node-clusters 11
- dead 22, corridor 81, junction 42, open 0
- Junction-dense (42) and corridor-heavy (81); rock-stable across 13
  rebuilds.

### danm14ac — Gehölz
- 117 → 86 clusters (26% reduction, 31 merged)
- merged-pairs 15, vetoed 0, chain-merges 6, multi-node-clusters 11
- dead 14, corridor 43, junction 29, open 0
- Outdoor grove; junction-dense, no open clusters.

### danm14ad — Sandral-Anwesen
- 86 → 82 clusters (5% reduction, 4 merged)
- merged-pairs 2, vetoed 0, chain-merges 0, multi-node-clusters 2
- dead 10, corridor 48, junction 24, open 0
- Barely merges (like the Sith base): corridor-and-room interior of
  near-all singletons.

### danm14ae — Kristallgrotte
- 30 → 14 clusters (53% reduction, 16 merged)
- merged-pairs 3, vetoed 0, chain-merges 4, multi-node-clusters 3
- dead 1, corridor 12, junction 1, open 0
- Small cave; merges cleanly to a short corridor chain.

### danm15 — Ruinen
- 75 → 57 clusters (24% reduction, 18 merged)
- merged-pairs 9, vetoed 0, chain-merges 1, multi-node-clusters 3
- dead 29, corridor 25, junction 3, open 0
- Dead-end-dominated (29 Sackgassen) — alcoves and recesses around the
  ruin.

### danm16 — Anwesen der Sandral (grove)
- 77 → 60 clusters (22% reduction, 17 merged)
- merged-pairs 8, vetoed 3, chain-merges 5, multi-node-clusters 8
- dead 22, corridor 29, junction 9, open 0

---

## Manaan (new — 2026-06-09)

### manm26ad — Landebucht (Docking Bay)  [3D-fix corrected 2026-06-09]
- 137 → 106 clusters (23% reduction)
- merged-pairs 5, vetoed 1, chain-merges 13, multi-node-clusters 12
- dead 32, corridor 57, junction 17, **open 0** (z-skipped 88)
- PHANTOM CORRECTION: pre-fix this read `open 24–27` (the most open
  area on record). With the 3D wall guard the open count is 0 — highest
  z-skipped anywhere (88), i.e. the most multi-level wall geometry. The
  "scattered unconnected pads" reading was the 2D ray blocking real
  inter-pad nav edges against walls on other levels.

### manm26ae — Östliches Zentrum (East Central)  [3D-fix corrected 2026-06-09]
- 116 → 83 clusters (28% reduction)
- merged-pairs 4, vetoed 0, chain-merges 21, multi-node-clusters 10
- dead 22, corridor 46, junction 15, **open 0** (z-skipped 83)
- PHANTOM CORRECTION: pre-fix `open 22–27`. With the 3D wall guard the
  open count is 0 (z-skipped 83); the Ahto-City "open-dominated" profile
  was a 2D-ray artefact. Real shape is corridor/junction-dominated.
  First area where the regression was caught (deterministic across
  reloads).

### manm26aa — Westliches Zentrum (West Central) — code inferred  [3D-fix corrected 2026-06-09]
- 79 → 44 clusters (44% reduction)
- merged-pairs 6, vetoed 0, chain-merges 18, multi-node-clusters 13
- dead 11, corridor 26, junction 7, **open 0** (z-skipped 17)
- PHANTOM CORRECTION: pre-fix `open 3–8`. With the 3D wall guard the
  open count is 0 (z-skipped 17).
- Module code not seen in a save row; inferred from the two unresolved
  Ahto codes (manm26aa / manm26ac). West-Ahto is the other.

### manm26ac — West-Ahto — code inferred
- 48 → 40 clusters (17% reduction, 8 merged)
- merged-pairs 1, vetoed 1, chain-merges 3, multi-node-clusters 3
- dead 9, corridor 25, junction 6, open 0
- Code inferred (see Westliches Zentrum note).

---

## Ebon Hawk (new — 2026-06-09)

### ebo_m12aa — Ebon Hawk
- 64 → 29 clusters (55% reduction, 35 merged)
- merged-pairs 11, vetoed 0, chain-merges 3, multi-node-clusters 7
- dead 6, corridor 15, junction 8, open 0
- Perfectly deterministic: identical signature across all 32 rebuilds in
  the dataset. Ideal regression-baseline area.

---

## Cross-area observations (new evidence)

### 1. Build-time `open` was a phantom-wall artefact — corrected to 0 everywhere

**Superseded finding.** This section originally read "build-time open is
no longer a Kanalisation curiosity," citing Manaan and Dantooine as
open-area-dominated:

- Manaan Landebucht: open 24–27
- Manaan Östliches Zentrum: open 22–27
- Dantooine Jedi-Enklave: open 12–20
- Manaan Westliches Zentrum: open 3–8

That was wrong. All of those numbers were produced by the 2D-only
`SegmentCrossesWalkmesh`, which treated walls on other floors as blockers
whenever their XY projection landed on a nav edge. On multi-level areas
this isolated genuinely-connected nav nodes into bogus `kKindOpenArea`
clusters. After the 3D wall guard (commit abc6db7, 2026-06-09) every one
of these reharvested to **open 0**:

- Manaan Landebucht: 24–27 → 0 (z-skipped 88)
- Manaan Östliches Zentrum: 22–27 → 0 (z-skipped 83)
- Manaan Westliches Zentrum: 3–8 → 0 (z-skipped 17)
- Dantooine Jedi-Enklave: 12–20 → 0 (z-skipped 36)
- Taris Kanalisation: 9 → 0 (z-skipped 28)

Confirmed via the nav-graph/wall crosscheck: 0 real crossings in every
area (the wall cache is phantom-free; the 2D ray was the whole problem),
and single-floor areas (Ebon Hawk, Slums, Daviks) build byte-identically.

Consequence for the brief's "large open chambers" section: there are **no
genuine build-time open clusters anywhere**. The open-chamber probe is not
the defining description problem — big rooms now classify as connected
junction/corridor clusters, matching the engine's own nav graph. The real
remaining question merges with the under-merge work: *should large
multi-node junction/corridor clusters carry a size/shape descriptor?*

### 2. Content shape clusters into distinct profiles

- **Junction-dense, open 0** — Dantooine Matale (junction 42), Gehölz (29),
  Taris Slums (48). Hub-and-radiate; the plaza re-fire / hysteresis problem
  dominates here.
- **~~Open-dense, junction-sparse~~** — SUPERSEDED. Pre-3D-fix this read
  Manaan Landebucht / both Zentrum areas as junction 3–5, open 22–27.
  Corrected, those areas are junction 15–17, open 0 (corridor-dominated).
  See observation 1 — the "open-dense" profile was a phantom-wall artefact
  and does not exist.
- **Barely-merging corridor interiors** — Taris Sith-Basis (71 → 68),
  Dantooine Sandral-Anwesen (86 → 82). Near-all singleton clusters; the
  corridor-chain merge has little to grab.
- **Aggressively merged interiors** — Javyars Cantina (72 → 17),
  Cantina der Oberstadt (77 → 23). Pair-merge driven, tight doorways.

A single merge/description profile will not fit all four. The dataset gives
the per-area numbers to tune against.

### 3. Determinism

`nodes`, `clusters`, `merged-pairs` and `chain-merges` are fully
deterministic per area under fixed code (Ebon Hawk: identical 32×;
Matale, Gehölz, Sandral-Anwesen, Sith-Basis all rock-stable). Only the
final classification split jitters by a few clusters at degree-ambiguous
boundaries. This means regression-testing a merge/classification change
against a fixed area (Ebon Hawk is the cleanest) will surface real
behavioural deltas, not noise.
