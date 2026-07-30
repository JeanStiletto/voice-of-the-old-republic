# Phase 3 scan — map, discovery cycling and object announcing

Scope: map_ui_cursor.cpp (1272) / .h (48), map_user_markers.cpp (199) / .h
(37), map_note_renames.cpp (109) / .h (36), map_shipped_hints.cpp (75) / .h
(49), discovery.cpp (336) / .h (63), narrated_target.cpp (114) / .h (48),
announce_degrees.cpp (226) / .h (20), door_announce.cpp (77) / .h (34),
floor_puzzle.cpp (634) / .h (67), filter_objects.cpp (90) / .h (45).

Method: full read of every file in the batch (all 20). For every candidate
duplicate/dead-code finding, grepped the qualified symbol (not the file
name) across the relevant header family before writing it up, per the
brief's namespace-vs-filename trap. Specific greps run, and what they
proved, are quoted inline under each finding. Cross-checked
`docs/refactoring/STATE.md` "Execution findings" and "Phase 2 status" first;
nothing below duplicates a settled item.

## Section A — general low-level cleanup

### A1 — map_ui_cursor.cpp Tick() is one 566-line function doing 9 separable jobs (map_ui_cursor.cpp:704-1270)

What's there: a single `Tick()` that in sequence (1) gates on foreground +
map-panel + areaMap, (2) does first-activation seeding (cursor position,
area-name announce, nav-graph build), (3) computes dt, (4) reads bound
movement keys and updates cursor pixel position with clamping, (5) plays
the edge-collision cue, (6) runs three parallel nearest-hit scans
(waypoint / user pin / shipped hint) and picks the closest, (7) probes the
terrain-shape label at the cursor position, (8) handles the ~110-line
"cursor is on a hit" branch (hover-pause timer, text resolution per hit
kind, speak), (9) handles the ~210-line "cursor is on ambient ground"
branch (fog/landmark/room/shape classification, text-based dedup overlay,
hover-pause timer, speak).

Why it's a problem: this is exactly the shape the brief calls out as
in-scope for Phase 3 (comparable to `ClassifyCluster`/`BuildForArea` in
`room_topology.cpp`). All state lives in one file-local `CursorState
g_state`, so — per the candidate-12/13/24 lesson — any split must carry
state by reference/accessor, not duplicate it. Every phase already reads
and writes only `g_state` plus its own locals; nothing is shared between
non-adjacent phases, so a split into file-local static functions (not a
file split) looks safe.

Proposed change: extract into named file-local statics inside the existing
anonymous namespace, called in sequence from `Tick()`, e.g.
`GateAndMaybeDeactivate`, `ActivateIfNeeded`, `ApplyMovementInput`,
`MaybePlayEdgeCue`, `FindBestHoverHit` (folds the three-way scan + nearest
pick), `ProbeTerrainShapeAtCursor`, `HandleWaypointHover`,
`HandleAmbientHover`. `Tick()` itself becomes a ~20-line driver. No header
change — everything stays file-local.

Risk: needs-in-game-test. The logic is mechanically a cut-and-paste into
named functions with `g_state` passed by reference where a phase doesn't
already close over it, so the risk is a copy/paste slip in the multi-branch
ambient dedup logic (lines 1057-1269), not the split concept itself.
Exercise: open the area map, pan the cursor with WASD over a map-note
waypoint, a Shift+N user marker, a shipped hint, fog, a named room, and open
terrain, confirming each still narrates once per zone with no double-speak
or silence regression.

Estimated line delta: -0 net (pure decomposition), readability-only.

### A2 — map_ui_cursor.cpp re-derives the generic CExoLinkedList offsets instead of reusing the canonical ones (map_ui_cursor.cpp:119-122)

What's there:
```
constexpr size_t kCExoLinkedListInternalOffset = 0x0;
constexpr size_t kCExoLLInternalHeadOffset     = 0x0;
constexpr size_t kCExoLLNodeNextOffset         = 0x4;
constexpr size_t kCExoLLNodePayloadOffset      = 0x8;
```
used in `FindNearestExploredMapNote` (map_ui_cursor.cpp:339-385) to walk
the map-hider's waypoint `CExoLinkedList`.

Why it's a problem: `engine_offsets_fields.h:673-677` already carries the
identical, SARIF-verified, generic layout under different names:
`kListInternalOffset = 0x0`, `kListInternalHeadOffset = 0x0`,
`kLinkedListNodeNextOff = 0x4`, `kLinkedListNodeDataOff = 0x8` — same four
values, same struct (`CExoLinkedList<T>` / `CExoLinkedListInternal` /
`CExoLinkedListNode`), with a comment block documenting a real historical
bug in `combat_queue.cpp` caused by getting this exact layout wrong. Those
constants are file-scope (not namespaced, engine_offsets.h:1-3 says so
explicitly) and already in scope: map_ui_cursor.cpp includes
`engine_area.h` (line 23) and `engine_reads.h` (line 28), both of which
pull in `engine_offsets.h` → `engine_offsets_fields.h`. Grep run to
confirm no other file redefines these names:
`grep -rn "CExoLinkedList\|CExoLL\|ExoLinkedList" engine_*.h` — only
`engine_offsets_fields.h` and `map_ui_cursor.cpp` define layout constants
for this struct.

Proposed change: drop the four local constants and use
`kListInternalOffset` / `kListInternalHeadOffset` / `kLinkedListNodeNextOff`
/ `kLinkedListNodeDataOff` directly at the three call sites
(map_ui_cursor.cpp:344, 361, 379, 385).

Risk: mechanical (same values, compiler-checked at each substitution site).

Estimated line delta: -4.

### A3 — map_ui_cursor.cpp redefines the CSWSObject position offset already in scope (map_ui_cursor.cpp:126)

What's there: `constexpr size_t kWaypointPositionOffset = 0x90;`, used once
at line 404 to read a waypoint's world position.

Why it's a problem: `engine_player.h:204` already declares
`kServerObjectPositionOffset = 0x90` at file (global) scope with the same
meaning (`CSWSObject` base-class position field — waypoints derive from
it). `engine_player.h` is already included at map_ui_cursor.cpp:27. The
file's own comment two lines above the local waypoint offsets
(map_ui_cursor.cpp:124-125: "CSWSWaypoint layout offsets (already used
elsewhere — re-stated here so the cursor code is self-documenting)")
confirms the value was deliberately copied rather than reused, but the
canonical name was available and unqualified (map_ui_cursor.cpp:619
already references the sibling global-scope constant `kObjectTagOffset`
directly, without namespace qualification, so the pattern is already used
in this file). Grep:
`grep -n "kServerObjectPositionOffset" engine_*.h *.cpp` shows one
declaration (engine_player.h:204) and two consumers
(engine_area.cpp:166, engine_player.cpp:57) — no conflicting redefinition
elsewhere.

Proposed change: drop the local constant, use `kServerObjectPositionOffset`
at map_ui_cursor.cpp:404.

Risk: mechanical.

Estimated line delta: -1.

### A4 — map_ui_cursor.cpp's "HasMapNote" constant is actually the enabled-flag offset, and duplicates one already in scope (map_ui_cursor.cpp:127)

What's there: `constexpr size_t kWaypointHasMapNoteOff  = 0x22c;`, used at
line 401 to gate the map-note scan (`if (hasNote == 0) { ...continue; }`).

Why it's a problem, in two parts:
1. Duplicate: `engine_area.h:486` already declares
   `kWaypointMapNoteEnabledOffset = 0x22c` at global scope (between the two
   `acc::engine` blocks, same pattern as `kObjectTagOffset` this file
   already uses unqualified) — same value, same field
   (`CSWSWaypoint.map_note_enabled`, per `engine_area.h:245`). Already in
   scope via `engine_area.h` at map_ui_cursor.cpp:23.
2. Misleading name: `engine_area.h:476` separately declares
   `kWaypointHasMapNoteOffset = 0x228` — a genuinely DIFFERENT field
   (`has_map_note`, the "was this waypoint authored with a note at all"
   flag; see `engine_area.h:249-256` for the has_map_note-vs-enabled
   relationship). map_ui_cursor.cpp's local name `kWaypointHasMapNoteOff`
   is one character removed from that unrelated 0x228 constant while
   actually holding the 0x22c value — a name collision that could mislead
   a future reader into thinking this is the same field as
   `engine_area.h`'s `kWaypointHasMapNoteOffset`. The header's own doc
   comment (map_ui_cursor.h:7) confirms 0x22c (`map_note_enabled`) is the
   intended field, so the current VALUE is correct — only the local name is
   wrong/duplicated. Grep run:
   `grep -n "0x22c\|0x230\b" engine_area.h` confirms both canonical names
   and their exact semantics.

Proposed change: drop the local constant, use `kWaypointMapNoteEnabledOffset`
at map_ui_cursor.cpp:401 (and consider renaming the local `hasNote` variable
to `noteEnabled` while there, to match).

Risk: mechanical, same value.

Estimated line delta: -1.

### A5 — map_ui_cursor.cpp's kWaypointMapNoteLocOff is unused dead code (map_ui_cursor.cpp:128)

What's there: `constexpr size_t kWaypointMapNoteLocOff  = 0x230;` — declared
alongside the two constants in A3/A4, but never referenced again in the
file. Grep run: `grep -n "kWaypointMapNoteLocOff\b" map_ui_cursor.cpp`
returns only the declaration line (128); no consumer.

Why it's a problem: dead constant. It also duplicates
`engine_area.h:487`'s `kWaypointMapNoteLocOffset = 0x230` (same value,
already in scope) — moot once removed, but noted for the record since it's
the same family as A2-A4.

Proposed change: delete the line.

Risk: mechanical (compiler-checked — an unused `constexpr` produces no
warning today, but removal cannot break a build since nothing references
it).

Estimated line delta: -1.

### A6 — map_ui_cursor.cpp: three dead includes (map_ui_cursor.cpp:17, 21, 25)

Verified by grepping the qualified symbol each header exists to provide,
not the header's file name, per the brief's trap:
- `<cstring>` (line 17): grepped
  `strcmp\|strncmp\|memcpy\|memset\|memmove\|strlen\|_stricmp\|_strnicmp\|std::str\|::mem\|strcpy\|strcat\|strchr\|strstr`
  against the file — zero matches. Only `<string>`'s `std::string` is
  used, a different header.
- `audio_cue_player.h` (line 21): its only declared symbol is
  `PlayCueAtPosition` (audio_cue_player.h:23). `grep -n
  "PlayCueAtPosition" map_ui_cursor.cpp` returns nothing — the file calls
  `acc::audio::PlayCue3D` (from `audio_bus.h`, already included) and
  `acc::audio::GetNavCueResref` (from `audio_cues.h`, already included)
  directly instead.
- `engine_manager.h` (line 25): grepped every symbol it declares
  (`FindOwningPanel`, `IsPanelInManager`, `GetForegroundPanel`,
  `LogManagerStack`, `kAddrGuiManagerPtr`, the `kMgrPanels*`/
  `kMgrModalStack*` constants) against the file — the only hit is
  `GetForegroundPanel` inside a prose comment (line 238) explaining why
  the code does NOT use it. No call site.

Proposed change: remove all three includes.

Risk: mechanical (compiler-checked — a stray dependency on a
transitively-pulled symbol would fail to compile).

Estimated line delta: -3.

### A7 — discovery.cpp: dead include \<cstdint\> (discovery.cpp:3)

Grepped `_t\b` excluding `size_t` across the file — no `uint32_t`,
`uint8_t`, or any other fixed-width integer typedef is used anywhere in
discovery.cpp (it uses `int`, `char`, `size_t`, `std::string`).

Proposed change: remove the include.

Risk: mechanical.

Estimated line delta: -1.

### A8 — map_ui_cursor.cpp's area-map pixel-scale constants duplicate engine_area.h's, but the duplication looks deliberate (map_ui_cursor.cpp:95-96) — flagged for awareness, not a clear defect

What's there: `kAreaMapWorldUnitsPerXPxOffset = 0x18` /
`kAreaMapWorldUnitsPerYPxOffset = 0x1c` (map_ui_cursor.cpp:95-96) duplicate
`engine_area.h:398-399`'s `kAreaMapWorldPerPxXOffset` /
`kAreaMapWorldPerPxYOffset` — same values, same field, already in scope via
the `engine_area.h` include.

Difference from A2-A5: `engine_area.h:394-395` carries an explicit comment
acknowledging the split — "map_ui_cursor keeps a local copy of the
transform fields (+0x18..+0x24) for its cursor projection" — meaning a
prior author already knew about and accepted this duplication (unlike
A2-A5, where nothing in either file acknowledges the overlap). It reads as
a deliberate "the cursor's inverse-projection math wants its own named copy
next to the other three inverse-only fields (orientation, origin X/Y) which
have no canonical constant elsewhere" choice, not an oversight.

Proposed change: none proposed — flagging only so the user can decide
whether to fold these two into the canonical names for full consistency
with A2-A4, or leave them as the header comment already documents.

Risk: n/a (no change proposed).

### A9 — floor_puzzle.cpp Tick() is a 200-line function covering six phases (floor_puzzle.cpp:433-632) — minor, lower priority than A1

What's there: on-demand board read (R key), area/cache (re)detection, board
diffing, solved detection, nav-position vs. plate hit-test, then
entry/delta speech composition, intro, and the off-plate nearest-plate
stream — all inline in one `Tick()`.

Why it's a note rather than a strong finding: unlike map_ui_cursor.cpp's
`Tick()`, this one already has `// ---- <Phase> ----` section-comment
banners marking each block, and most of the actual logic (geometry,
naming, `DeltaText`/`BoardStateText`/`SpeakIntro`) is already factored into
named helper functions above it — `Tick()` itself is mostly orchestration
and speech-timing state transitions, which are harder to split cleanly
without threading several `DWORD`/`bool` locals through new parameters.

Proposed change: optional — if the user wants it split, the natural cut is
`ReadBoardAndAnnounceIfSolved` (lines ~479-510) and `UpdateNavPositionAndSpeak`
(lines ~512-631) as two file-local statics, leaving the R-key read and
area/cache setup in `Tick()` itself.

Risk: needs-in-game-test if executed (same class of risk as A1 — walk the
puzzle plates and confirm entry/delta/nearest-stream timing is unchanged).

Estimated line delta: -0 net if executed; this item can also simply be
declined given the file already reads reasonably.

### A10 — announce_degrees.cpp: one function uses the project's truncation-safe formatter, its neighbour doesn't (announce_degrees.cpp:96-105 vs. 195-204)

What's there: `OnAnnounceWorldDegrees` builds its spoken message via
`acc::strfmt::Format(Get(...), ...)` into a `std::string` (unbounded,
line 98-100). Fifty lines later in the same file, `OnAnnounceMapDegrees`
builds its message via a fixed `char msg[384]` +
`std::snprintf(msg, sizeof(msg), Get(...), ...)` (lines 195-199) — the
exact fixed-buffer-plus-localized-format-string pattern `strfmt.h`'s own
header comment says it was written to replace, citing a real prior
truncation bug (a Taris cluster label that "dropped off the end of a
96-byte cluster buffer").

Why it's a problem: today this specific instance is not a live truncation
risk — `roomBuf` is capped at 160 bytes and the German template
`"%s. Blick auf der Karte nach %d Grad, %s."` adds only ~40 bytes of
literal text, comfortably under 384 — but it is an inconsistency within one
file where the safer pattern is demonstrated two functions above, and any
future template/room-name growth (e.g. a longer localisation) would
silently truncate here while its sibling function cannot.

Proposed change: switch `OnAnnounceMapDegrees` to build `msg` via
`acc::strfmt::Format` into a `std::string`, matching
`OnAnnounceWorldDegrees` immediately above it in the same file.

Risk: low (same call site, same Get(Id), string content unchanged;
`prism::Speak`/`SpeakUrgent` already take `const char*` via `.c_str()`
elsewhere in this exact file).

Estimated line delta: -2 (drops the fixed buffer).

### A11 — narrated_target.cpp: raw sentinel literal where a named constant is already in scope (narrated_target.cpp:19-20) — low priority, codebase-wide pattern

What's there:
```cpp
if (!obj || serverHandle == 0u || serverHandle == 0xFFFFFFFFu ||
    serverHandle == 0x7F000000u) {
```

Why it's a note rather than a strong finding: `engine_offsets_values.h:49`
declares `kInvalidObjectId = 0x7F000000u` at file scope, already in scope
here via the `engine_area.h` include (narrated_target.cpp:6, which pulls in
`engine_offsets.h`). Swapping the third literal for the name is mechanical
and correct by value. But grepping the same 3-literal shape
(`0u`/`0xFFFFFFFFu`/`0x7F000000u` together) turns up identical inline
copies in `combat_query.cpp:126`, `combat_queue.cpp:340/407/462`,
`dialog_speech.cpp:380/382/405/438/441`, and even a private helper inside
`engine_area.cpp:82-84` (`IsSentinelHandle`, anonymous-namespace, not
exported) — this is an established, repeated pattern across the whole
codebase, not something unique to this batch. Neither `0u` nor
`0xFFFFFFFFu` has a named constant anywhere, so a partial swap (naming only
the third literal) would leave the other two as raw literals right next to
it, which is arguably less consistent, not more.

Proposed change: for this batch alone — swap
`serverHandle == 0x7F000000u` for `serverHandle == kInvalidObjectId` at
narrated_target.cpp:20. A full fix (exporting `IsSentinelHandle` or
equivalent and using it everywhere) touches many files outside this
batch and is a separate decision for the user, not proposed here.

Risk: mechanical.

Estimated line delta: 0 (rename only).

## Section B — AI-pattern findings

### B1 — map_ui_cursor.cpp: four dead null-checks on acc::strings::Get() (map_ui_cursor.cpp:577, 980, 992, 1010)

What's there:
```cpp
const char* t = acc::strings::Get(acc::strings::Id::MapCursorUnexplored);
if (t) return std::string(t);
```
and three instances of the shape
```cpp
const char* poi = acc::strings::Get(acc::strings::Id::MapCursorWaypointPOI);
if (poi && poi[0]) { ... }
```

Why it's a problem: verified `acc::strings::Get(Id)` (strings.cpp:15-25)
switches over the active language and calls into `lang_xx::Get(id)`, and
each of those (checked `strings_de.cpp:19` and its siblings) is a plain
`switch` returning string literals, with no path that returns `nullptr` —
the brief's stated convention. So `if (t)` at line 577 is always true (dead
branch — the `break` two lines below can never be reached from this arm),
and the `poi &&`/`generic &&` half of the three `&&` checks at 980/992/1010
is dead; only the `[0]` emptiness check is meaningful (Get can return `""`
for a genuinely missing Id).

Proposed change: line 577 — replace the `if (t) return std::string(t);`
with an unconditional `return std::string(t);` (or fold the whole case into
one line). Lines 980/992/1010 — drop the `poi &&`/`generic &&` half,
leaving `if (poi[0])` / `if (generic[0])`.

Risk: mechanical (verified against strings.cpp/strings_de.cpp; behavior is
identical since the removed check was always true).

Estimated line delta: -1 (the line-577 case collapses by one line; the
other three are same-line edits).

### B2 — map_user_markers.cpp: self-admitted redundant "belt-and-braces" gate (map_user_markers.cpp:188-197)

What's there:
```cpp
void PollWin32() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::SaveMarkerAtCursor)) {
        return;
    }
    // In-world gate — speak only when a player is loaded; otherwise the
    // map panel can't be foreground anyway. Belt-and-braces.
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;
    OnDrop();
}
```

Why it's a problem: the comment itself names this exactly the pattern the
brief asks for ("belt-and-braces guards that duplicate a check one frame up
the call stack") — by the comment's own logic the map panel cannot be
foreground without a loaded player, so the check can never actually fire in
a way `OnDrop()`'s own gates (`HasActiveMapPanel()`, then current-area,
then client-area, at map_user_markers.cpp:106/118/123) don't already cover.
`playerPos` is read and then discarded — it isn't passed into `OnDrop()`.

Proposed change: drop the `Vector playerPos` + `GetPlayerPosition` check,
call `OnDrop()` directly after the hotkey check.

Risk: low. `OnDrop()` already fails safe (logs + returns) if the map isn't
truly active, so removing this pre-check cannot newly crash or newly fire
the drop in an invalid state — worth a quick in-game Shift+N check on the
map screen to confirm the marker still drops normally.

Estimated line delta: -4.

### B3 — announce_degrees.cpp: same shape as B2, lower confidence (announce_degrees.cpp:213-224)

What's there:
```cpp
void PollWin32() {
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::AnnounceDegrees)) return;

    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) return;

    if (acc::engine::HasActiveMapPanel()) {
        OnAnnounceMapDegrees();
    } else {
        OnAnnounceWorldDegrees();
    }
}
```
`playerPos` is likewise read and discarded. Both `OnAnnounceWorldDegrees`
and `OnAnnounceMapDegrees` already call `acc::engine::GetPlayerPosition`
themselves (line 122 / via `ResolveClusterLabelForPlayer`, and similar) and
fail gracefully (log + return) when it's unavailable — so this looks like
the same redundant early-out as B2. Flagged at lower confidence because,
unlike B2, there's no comment here admitting it's a belt-and-braces check,
so it's possible the author wanted "don't even touch HasActiveMapPanel /
log a skip" cheaply before dispatching. Leaving this for the user's call
rather than pairing it into B2's mechanical fix.

Risk: low if executed, same reasoning as B2.

Estimated line delta: -3 if executed.

### B4 — map_ui_cursor.cpp: three near-identical "nearest hit within radius" scans (map_ui_cursor.cpp:332-433, 444-480, 487-516)

`FindNearestExploredMapNote`, `FindNearestUserMapPin`, and
`FindNearestShippedHint` each repeat the same shape: iterate a candidate
list, compute pixel-space distance², compare against a running
`bestDist2` seeded at `kHoverHitRadiusPx²`, track a `scanned` counter, and
write both out through optional out-params. The three iterate genuinely
different data sources (a raw `CExoLinkedList` walk, a `CSWCMapPin*`
array, and a static table), so a full unification into one generic
"nearest-of" helper would need a source abstraction (iterator/callback) to
avoid becoming its own layer of indirection — not a clear win. Flagging
per the brief's request to surface copy-paste-shaped blocks; no change
proposed, left for the user's judgement given the cost/benefit is close.

Risk: n/a (no change proposed).

### B5 — map_ui_cursor.cpp: trivial one-line pass-through wrapper (map_ui_cursor.cpp:518-520)

```cpp
bool ReadWaypointMapNoteText(void* waypoint, char* outBuf, size_t bufSize) {
    return acc::engine::GetWaypointMapNote(waypoint, outBuf, bufSize);
}
```
Called from exactly one site (line 998) with no added logic, error
translation, or documentation beyond what `GetWaypointMapNote` already
carries. Minor — inlining the call at its one use site would remove the
indirection, but it's a cosmetic call and not worth doing in isolation from
A1's larger `Tick()` decomposition (the call site moves anyway if A1 is
executed).

Risk: n/a (no change proposed standalone; folds naturally into A1 if done).

## Findings (possible bugs — user decides)

None found with enough confidence to report. Two things were investigated
and downgraded to Section A notes instead of bug reports because the
actual failure mode doesn't reproduce today: the fixed-buffer `snprintf` in
announce_degrees.cpp (A10, buffer size comfortably exceeds the realistic
message length) and the mismatched-but-value-correct `kWaypointHasMapNoteOff`
name in map_ui_cursor.cpp (A4, the VALUE it uses is the one the header's own
design doc calls for — only the local name is misleading).

## Candidate 28 — narrow-header include opportunities

- `map_ui_cursor.h:27` includes `engine_offsets.h` for `Vector` only (used
  in `TryGetCursorWorldPosition`/`PanToWorld` signatures) — `engine_offsets_types.h`
  would suffice.
- `map_note_renames.h:24` includes `engine_offsets.h` for `Vector` only
  (already commented `// Vector`) — same narrowing applies.
- `map_shipped_hints.h:23` includes `engine_offsets.h` for `Vector` only
  (already commented `// Vector`) — same narrowing applies.
- `narrated_target.h:22` includes `engine_offsets.h` for `Vector` only
  (already commented `// Vector`) — same narrowing applies.
- `discovery.cpp:10` includes `engine_offsets.h` for `Vector` only (already
  commented `// Vector`); note it's also already transitively available via
  `engine_area.h` (discovery.cpp:9), so this include is fully redundant
  today, not just coarser than needed — either narrow it to
  `engine_offsets_types.h` for explicitness or drop it.
- All other files in the batch (map_ui_cursor.cpp, map_user_markers.cpp,
  map_note_renames.cpp, map_shipped_hints.cpp, narrated_target.cpp,
  announce_degrees.cpp, door_announce.cpp, floor_puzzle.cpp,
  filter_objects.cpp/.h) use enough distinct symbols from `engine_area.h` /
  `engine_player.h` / `engine_panels.h` / `engine_reads.h` that no
  narrowing applies — and unlike `engine_offsets.h`, none of those four
  aggregators has been split into narrower sibling headers yet (confirmed:
  `ls engine_area*.h engine_player*.h engine_panels*.h engine_reads*.h`
  shows only `_internal.h` variants, which are a different, non-public
  thing), so there is nothing narrower to switch to for them today.

## Files scanned with nothing to report

- map_user_markers.h
- map_note_renames.cpp
- map_shipped_hints.cpp
- discovery.h
- announce_degrees.h
- door_announce.cpp
- door_announce.h
- floor_puzzle.h
- filter_objects.cpp
- filter_objects.h

(map_ui_cursor.h, map_note_renames.h, map_shipped_hints.h, and
narrated_target.h each carry only the Candidate 28 include note above —
no A/B finding.)
