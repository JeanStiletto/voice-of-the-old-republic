# Phase 3 scan — examine/camera/probes batch

Scope: `examine_view.cpp` (792) + `examine_view.h`, `examine_view_effect_names.cpp`
(766), `camera_announce.cpp` + `.h`, `camera_orient.cpp` + `.h`,
`camera_spin_guard.cpp` + `.h`, `probe_audio_frame.cpp/.h`,
`probe_camera_distance.cpp/.h`, `probe_camera_state.cpp/.h`,
`probe_mouselook.cpp/.h`, `probe_pathfind.cpp/.h`, `probe_priority_groups.cpp/.h`.

Method: full read of every file in the batch (not excerpts). For every
"is this dead/duplicate?" claim below I re-verified with a targeted grep quoted
under the finding — namespace-qualified or symbol-qualified, never a filename
grep, per the brief's trap #1. For the probe-retirement questions I cross-checked
`docs/llm-docs/*.md` and the shipped consumer files rather than guessing from
the probe's own comments.

## Section A — general low-level cleanup

### A1 — Three dead includes + one orphaned comment, same removed feature (examine_view.cpp:11,13,20,762)

`examine_view.cpp` includes three headers annotated `// ... — engine-panel
watcher`:
```
11: #include "engine_manager.h"   // kAddrGuiManagerPtr — engine-panel watcher
13: #include "engine_panels.h"    // PanelKind / IdentifyPanel — engine-panel watcher
20: #include "transitions.h"      // IsModuleLoadPending — engine-panel watcher gate
```
Grepped the actual exported symbols of each header (`kAddrGuiManagerPtr`,
`FindOwningPanel`, `IsPanelInManager`, `GetForegroundPanel`, `LogManagerStack`,
`PanelKind`, `IdentifyPanel`, `IsModuleLoadPending`) against the whole file:
every hit is the include-line comment itself, never a use in code.

Line 762 confirms why — it's an orphaned comment sitting directly above the
current `Tick()`, describing a function that no longer exists:
```
762: // Engine-panel open/close logger. The engine's CSWGuiExamine is a generic
763: void Tick() {
764:     if (!g_state.active) return;
765:     // Self-disarm if the player loses connection to the world (area
766:     // transition / load). Cheap probe.
```
The comment is mid-sentence and describes a panel open/close logger; the
actual `Tick()` body does something unrelated (self-disarm on
player-position-loss). This is the fossil of a removed "watch CSWGuiExamine
open/close via the panel manager" feature — the three includes were its only
consumers and were never cleaned up when it was cut.

- Proposed change: delete the three includes and the stale comment fragment at
  762 (replace with a comment matching what `Tick()` actually does, or fold
  into the function).
- Risk: mechanical (compiler-checked — an actually-used symbol from any of the
  three headers would fail to compile once its include is removed).
- Estimated delta: -4 lines.

### A2 — `camera_spin_guard.h` describes a fix the code doesn't use (camera_spin_guard.h:9-11 vs .cpp:210-216)

Header:
```
 9: // Guard (the fix): on an actual edge-turn spin — in-world, foreground,
10: // cursor in band, camera rotating — nudge the physical cursor a small inset
11: // inward (SetCursorPos), clearing the band. Gating on live rotation keeps it
```
Code:
```
210:    bool guardFired = false;
211:    if (edgeNow &&
...
214:        guardFired = SafeWriteOff<uint32_t>(
215:            gui, kGuiMouseXOffset, (uint32_t)(vpW / 2));
```
The `.cpp`'s own comment two lines above (line ~207-209) says the opposite of
the header: *"a direct field write isn't subject to that [broken pipeline]"*
— i.e. the fix is a raw write to the engine's `GuiManager->mouse_x` field to
dead-centre (`vpW/2`), not a `SetCursorPos` call, and not merely an "inset."
`docs/llm-docs/camera-and-swoop.md` (`camera_spin_edge_turn` section)
confirms this in the project's own words: *"KEY LESSON — injected motion
does NOT move mouse_x after load. Both SetCursorPos AND SendInput ... left
mouse_x stuck at 0 ... Direct field write is the only thing that moved it."*
So the header is not just imprecise, it names the exact mechanism the
investigation proved does **not** work.

- Proposed change: reword camera_spin_guard.h lines 9-11 to say "writes the
  engine's cursor field directly to viewport centre" and drop the
  `SetCursorPos` / "inset" wording.
- Risk: mechanical (comment-only).
- Estimated delta: ~0 lines (rewording).

### A3 — `kAddrGuiManagerPtr` redefined locally instead of reusing engine_manager.h's (camera_spin_guard.cpp:22)

```
22: constexpr uintptr_t kAddrGuiManagerPtr        = 0x007A39F4;  // CSWGuiManager**
```
`engine_manager.h:42` already declares the identical name and value:
```
engine_manager.h:42: constexpr uintptr_t kAddrGuiManagerPtr = 0x007A39F4;
```
Verified via `grep -rn "007A39F4"` across the patch — the only two
definitions are these; every other of the ~25 files that read the GUI manager
pointer (`combat.cpp`, `menus*.cpp`, `engine_panels.cpp`, `input_pipeline.cpp`,
...) includes `engine_manager.h` and reuses its constant.
`camera_spin_guard.cpp` does not include `engine_manager.h`; it silently
re-declares the same pointer under the same name in its own anonymous
namespace. Not a compile hazard (anonymous-namespace scoping keeps them from
colliding), but it is exactly the "raw literal duplicating a named constant
already in scope [via one more include]" pattern called out in the brief —
and if this address were ever a `.text` symbol needing `R()` rebase-wrapping,
only one of the two copies would get updated. (It's a `.data` global, so per
the project's C10 finding it is correctly left unwrapped here.)
- Proposed change: `#include "engine_manager.h"` and drop the local
  redeclaration.
- Risk: mechanical.
- Estimated delta: -1 line, +1 include.

### A4 — Same AppManager→...→Camera chain-walk reimplemented in four places (camera_orient.cpp:22-23,91-100; probe_camera_distance.cpp:21-22,105-114; probe_camera_state.cpp:19,30-55; engine_player.cpp:102-103,134-135)

Four sites independently walk `*kAddrAppManagerPtr → +0x4 ClientApp →
+kClientExoAppInternalOffset ClientInternal → +0x18 Module → +0x40 Camera`,
each with its own local `constexpr size_t kClientInternalModuleOffset = 0x18`
/ `kCSWCModuleCameraOffset = 0x40` and its own little `SafeDeref`-shaped
helper:
- `camera_orient.cpp` `GetModule()`/`GetCamera()` (lines 91-100, 123-127)
- `probe_camera_distance.cpp` `GetCSWCModule()`/`GetCamera()` (105-120)
- `probe_camera_state.cpp` `GetClientInternal()`/`GetCSWCModule()` (30-55)
- `engine_player.cpp`'s own `GetCameraPosition()` and `GetCameraYawRadians()`
  (102-103, 134-135) each re-declare the same two constants a second time
  *inside the same file*.

`kAppManagerClientAppOffset` and `kClientExoAppInternalOffset` are already
centralised in `engine_player.h` and reused correctly everywhere; only the
last two hops (`+0x18` Module, `+0x40` Camera) get re-derived from scratch
per call site instead of being published once (e.g. an
`acc::engine::GetCameraModule()` / an exposed `kClientInternalModuleOffset`
/ `kCSWCModuleCameraOffset` pair in `engine_player.h`).
- Proposed change: publish the two offsets (and ideally a shared
  `GetCameraObject()` chain-walk) from `engine_player.h`, since it already
  owns the rest of the chain; the four sites drop their local copies. This is
  the same shape as Phase 2's B4-B6 minigame-primitive consolidation, so it
  may be more efficient to batch with that kind of pass rather than execute
  in isolation.
- Risk: low (four call sites, each currently self-contained and SEH-guarded
  the same way; behaviour-preserving if the published constants keep the
  same values). No in-game test needed beyond the existing camera checklist
  items, since the values don't change.
- Estimated delta: roughly -30 lines net (four duplicated 6-10 line helpers
  collapse toward one).

### A5 — Four dead offset constants in probe_camera_state.cpp (probe_camera_state.cpp:20,25-27)

```
20: constexpr size_t kCSWCModuleCameraYawOffset       = 0x98;
...
25: constexpr size_t kCameraYawOffsetA                = 0x90;
26: constexpr size_t kCameraYawOffsetB                = 0x94;
27: constexpr size_t kCameraYawOffsetC                = 0x40;
```
Grepped each name against the file: all four appear exactly once (their own
declaration) and nowhere else. The code that reads these candidate offsets
uses raw literals instead of the constants declared for exactly that purpose:
```
96:  float modCamera = SafeRead<void*>(module, 0x40, nullptr);      // == kCameraYawOffsetC, unused
121: float cam_0x90 = SafeRead<float>(pcCamera, 0x90, 0.0f);        // == kCameraYawOffsetA, unused
122: float cam_0x94 = SafeRead<float>(pcCamera, 0x94, 0.0f);        // == kCameraYawOffsetB, unused
```
`kCSWCModuleCameraYawOffset` (0x98) isn't even referenced as a raw literal —
it's fully dead. This matches the project's own documented conclusion in
`docs/llm-docs/camera-and-swoop.md`: *"`cachedYaw` field (module+0x98) was a
wrong guess (reads 0/-1, not the accumulator) — drop it."* The investigation
already dropped the read; the declared-but-unused constant just never got
deleted with it.
- Proposed change: delete `kCSWCModuleCameraYawOffset`; either wire the other
  three constants into the reads that already use their raw values, or delete
  them too if the raw-literal form is intentional (kept minimal because this
  is a diagnostic-only probe).
- Risk: mechanical.
- Estimated delta: -4 lines (or 0 net if replacing literals in place).

### A6 — Dead `kVK_F9` constant, real dispatch goes through the hotkeys system (probe_pathfind.cpp:22)

```
22: constexpr int kVK_F9 = 0x78;
```
Appears once (its own declaration); the actual key edge-detection in
`PollWin32()` uses `acc::hotkeys::Pressed(acc::hotkeys::Action::ProbePathfind)`,
not a raw `GetAsyncKeyState(kVK_F9)` check. Looks like a leftover from an
earlier direct-VK-poll version of this probe that was later rewired onto the
hotkeys system without removing the now-unused constant.
- Proposed change: delete.
- Risk: mechanical.
- Estimated delta: -1 line.

### A7 — Function doing five separable jobs: examine_view.cpp's `BuildRows()` (examine_view.cpp:368-530)

`BuildRows()` is ~163 lines building: the Name row, four creature-stat rows
(Faction/Condition/HP/Level), the Distance row, the Blind status row, five
equipment rows, the Effects block, and the Feats block — each its own
`if (idx < kMaxRows) { ...; snprintf(...); ++idx; }` shape (see A-pattern
note B1 below; this entry is the "should this function be split" structural
half of the same observation). Matches the brief's explicit carve-out for
Phase 3 function-level decomposition (the `ClassifyCluster`/`BuildForArea`
precedent).
- Proposed change: split into `AppendNameRow`, `AppendCreatureStatRows`,
  `AppendDistanceRow`, `AppendStatusRows`, `AppendEquipmentRows`
  (`AppendEffectRows`/`AppendFeatRows` already exist as separate functions,
  so this would make the shape consistent). Behaviour-preserving — pure
  extraction, same row order.
- Risk: low; needs the existing examine-panel in-game check (open Ö on a
  creature, arrow through all rows) since row order/content must stay
  byte-identical.
- Estimated delta: ~0 net lines (extraction, not shrinkage).

## Section B — AI-pattern findings

### B1 — Repeated "if (idx < kMaxRows) { snprintf; ++idx; }" block, partially already recognised (examine_view.cpp:412-527)

Ten of the row-building blocks inside `BuildRows()` hand-roll the same
three-line shape independently. The author already extracted this exact
pattern once, for the equipment slots only:
```
401: auto appendEquipRow = [&](size_t slotOffset, S fmtId) {
402:     if (idx >= kMaxRows) return;
403:     ...
408:         ++idx;
409:     }
410: };
```
...but the Faction/Condition/HP/Level/Distance/Blind rows just above and
below it don't use an equivalent helper, so the same three-line shape repeats
seven more times with only the format-id and read-call varying. This is the
textbook "copy-paste block an abstraction should own" the brief asks Section
B to flag.
- Proposed change: fold into the A7 split, or independently generalise
  `appendEquipRow`'s shape into a small `AppendRow(fmtId, args...)` /
  `AppendRowIf(condition, fmtId, args...)` helper reused by all the scalar
  rows.
- Risk: low; same in-game check as A7.
- Estimated delta: roughly -25 lines.

### B2 — `camera_orient.cpp`'s own comment flags a dead parameter (camera_orient.cpp:106-120, call sites 210, 288)

```
106: bool ReadCurrentEngineYawRad(void* camera, float& out) {
107:     float degsFromAnnounce = 0.0f;
108:     if (acc::camera_announce::TryGetCameraEngineYawDegrees(
...
118:     (void)camera;  // chain re-walked inside the helper
119:     return acc::engine::GetCameraYawRadians(out);
120: }
```
The `camera` parameter is explicitly discarded — the author's own comment
admits the fallback path re-derives the camera chain independently inside
`acc::engine::GetCameraYawRadians` rather than using the pointer just walked
by the caller. Both call sites (`Tick()` lines 210 and 288) still walk the
chain via `GetCamera()` purely to pass this now-unused argument (they also
use the return value for a separate `!camera` existence check, so `GetCamera()`
itself is NOT dead — only the parameter on `ReadCurrentEngineYawRad` is).
- Proposed change: drop the `camera` parameter from `ReadCurrentEngineYawRad`
  and update the two call sites to stop passing it.
- Risk: mechanical (compiler-checked; behaviour is identical since the
  parameter was already unused).
- Estimated delta: -1 line, -2 call-site arguments.

### B3 — Hardcoded speech strings bypassing `Get(Id)` (probe_audio_frame.cpp:71, probe_camera_distance.cpp:235)

```
probe_audio_frame.cpp:70-72:
    char msg[64];
    std::snprintf(msg, sizeof(msg), "Probe %s", dirName);
    prism::Speak(msg, /*interrupt=*/true);

probe_camera_distance.cpp:234-237:
    char msg[64];
    std::snprintf(msg, sizeof(msg), "Camera distance probe: %s",
                  ModeName(g_clampMode));
    prism::Speak(msg, /*interrupt=*/true);
```
Both literals are spoken via `prism::Speak`, bypassing `strings::Get(Id)` —
the exact "hardcoded literal built via snprintf into a buffer that is then
spoken" pattern called out in the brief. Contrast with `probe_mouselook.cpp`,
which does route its spoken toggle state through
`acc::strings::Get(Id::MouseLookOn/Off)` despite being an equally throwaway
probe — so the convention is not being followed consistently even among
sibling probes. Given both are diagnostic/RE-only surfaces (never reaches a
release build's normal play, only fires on unbound dev hotkeys F10/F11 and
Ctrl+F11), this is lower priority than a production-string violation would
be, but it's still a real inconsistency worth a decision.
- Proposed change: either add `strings::Id` entries (English-only, matching
  other diagnostic speech if any exists), or explicitly document
  probe-diagnostic speech as exempt from centralisation.
- Risk: mechanical if adding IDs; needs the probe's own hotkey to verify the
  new phrase speaks (F10/F11 or Ctrl+F11 in a live session).
- Estimated delta: +2 string IDs (×6 locales) or 0 if declared exempt.

## Findings (possible bugs — user decides)

None. The SEH-guard / null-check cross-cutting pattern that three other
batches flagged was checked carefully in every file of this batch (every
`__try` block and every raw `*reinterpret_cast<...>` dereference was matched
against its enclosing function) and no gap was found — every raw engine-memory
dereference in this batch is either inside a SEH `__try` directly, or routed
through an already-guarded helper (`SafeDeref`/`SafeRead`/`SafeReadOff`/
`SafeWriteOff`/`SafeReadU32`/`SafeReadVector`/`SafeBulkRead`/`CallIntThis*`).
The two files with the most raw pointer arithmetic (`probe_pathfind.cpp`,
`probe_priority_groups.cpp`) are in fact the most defensively written in the
batch — `probe_pathfind.cpp` even falls back to a byte-by-byte guarded copy
if the bulk `memcpy` faults. This batch is clean on that specific pattern.

## Probe-retirement questions (per the brief — never a mechanical deletion)

Three of the five `probe_*` files in this batch show strong, documented
evidence that the question they were built to answer has already been
settled and consumed by shipped code. Per the brief this is flagged as a
question, not a candidate:

- **`probe_mouselook.cpp/.h`** — its own header says *"once the probe has
  informed the view-mode design ... this file goes away ... nothing else in
  the patch consumes it."* View mode has since shipped (`view_mode.cpp/.h`),
  and `docs/llm-docs/camera-and-swoop.md`'s `kotor_camera_character_decoupling`
  note explains the shipped design explicitly chose the *simpler* native
  A/D-vs-W/S decoupling over the mouse-look-forcing approach this probe
  investigated ("No Mouse Look forcing, no SendInput delta synthesis ...
  needed"). The probe's own hypothesis was investigated, documented, and NOT
  the path taken.
- **`probe_pathfind.cpp/.h`** — its own header says *"once the probe has
  informed the design ... this TU + the F9 hotkey go away."*
  `docs/llm-docs/walk-nav-and-walkmesh.md`'s `kotor_nav_graph_layout` note
  says outright: *"Fully decoded 2026-05-11 by the Phase 5 lay-off 1
  probe"* (this file) and gives the exact `CSWSArea` nav-graph offsets this
  probe discovered. `guidance_pathfind.cpp/.h` now exists and (per the doc)
  is the shipped consumer of exactly that data.
- **`probe_priority_groups.cpp/.h`** — its header says it exists to
  *"surface the table contents so we can pick the loudest group"* for cue
  volume. `audio_bus.cpp:101` states outright: *"Layout mirrors
  probe_priority_groups.cpp"* — the shipped mod-cue-volume-slider feature
  (memory note `project_mod_cue_volume_slider`, the sentinel-FadeTime=31337
  row) already implements the priority-group scan this probe was for.

Note this is NOT the probe_priority_groups-is-dead trap from Phase 1 — that
was about the probe's *own* `Tick()` being uncalled (it isn't; `core_tick.cpp`
calls it every tick via `acc::probe::priority_groups::Tick()`, confirmed live
this session too). This finding is different: the probe still runs and still
logs, but the investigation it exists to support already has an answer that
shipped elsewhere.

`probe_audio_frame.cpp/.h` and `probe_camera_distance.cpp/.h` /
`probe_camera_state.cpp/.h` do NOT have the same evidence trail — no doc or
shipped file was found that explicitly closes out their questions (audio
listener-frame characterisation; the camera-distance-zero "Option B"
feasibility check), so they are left alone here rather than guessed at.

## Candidate 28 — narrow-header include opportunities

- `examine_view.cpp:12` includes the full `engine_offsets.h` aggregator but
  only uses symbols from `engine_offsets_addresses.h` + `engine_offsets_fields.h`
  + `Vector` (`engine_offsets_types.h`) — never touches
  `engine_offsets_values.h`. Minor narrowing (drop to 3 of 4 headers).
- `camera_orient.cpp:11` includes the full aggregator but the only symbol it
  actually uses from the whole `engine_offsets` family is `Vector` — every
  chain-walk offset it uses (`kAppManagerClientAppOffset`,
  `kClientExoAppInternalOffset`) comes from `engine_player.h`, already
  included separately. Could narrow to `engine_offsets_types.h` alone.
- `probe_pathfind.cpp:12` — same story: the only symbol used from the
  aggregator is `Vector`; every offset it reads is a locally-declared
  constant. Could narrow to `engine_offsets_types.h` alone.

## Files scanned with nothing to report

- `examine_view.h` — clean; declarations match implementation, comments are
  accurate and current (including the note about `AppendEffectRows`'s home
  and the removed `ReadInvisibleFlag`, which is itself a good example of a
  comment correctly explaining a past removal rather than going stale).
- `examine_view_effect_names.cpp` — verified as pure data: scripted a
  case-label diff of all five `EffectNameXx` tables against the English
  anchor and all five `EffectIconNameXx` tables against their English
  anchor — identical case sets in every language (68 effect-type entries,
  59 effect-icon entries, all five locales, zero drift). No unreachable
  entries, no hardcoded-string violation (this file is explicitly and
  correctly exempt — see its own top-of-file comment on why these are keyed
  by raw engine ids rather than `strings.h` ids).
- `camera_announce.cpp/.h` — clean; the debounce/hysteresis state machine
  is dense but is the project's documented stability-debounce pattern, not
  disorganised. No dead includes, no unguarded reads (no raw engine-memory
  reads live in this file at all — everything funnels through
  `acc::engine::GetCameraPosition`/`GetPlayerPosition`), no orphaned
  comments found.
- `camera_orient.h` — clean; accurately describes the current mechanism
  (synthesised bound-turn-key SendInput, not the abandoned direct-call
  approach it correctly documents as tried-and-rejected).
- `camera_spin_guard.cpp` — clean beyond A2/A3 above; the episode-based
  logging design matches its header's description exactly, `SafeRead`/
  `SafeReadOff`/`SafeWriteOff` are used consistently everywhere an engine
  address is touched.
- `probe_audio_frame.cpp/.h` — clean beyond B3; the compass-to-world-offset
  math and its own commentary are self-consistent and match
  `camera_announce`'s compass convention.
- `probe_camera_distance.cpp/.h` — clean; `SafeDeref`/`SafeRead`/`SafeWrite`
  used consistently, clamp/stomp accounting logic is coherent, no dead
  constants found (all four `kAddrCamera*`/`kAddrZoomCamera` and the four
  tuning-global addresses are read at least once).
- `probe_mouselook.cpp/.h` — clean beyond the retirement question above; the
  sweep state machine and cursor-restore logic are internally consistent.
- `probe_pathfind.cpp/.h` — clean beyond A6 and the retirement question;
  notably the most defensively-coded file in the batch (guarded bulk copy
  with a guarded byte-by-byte fallback).
- `probe_priority_groups.cpp/.h` — clean beyond the retirement question;
  confirmed live via `grep -n "probe::priority_groups::Tick" core_tick.cpp`
  (namespace-qualified, not a filename grep, per the brief's trap #1) —
  still called every tick, still a no-op after its one-shot dump completes.
