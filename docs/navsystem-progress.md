# Navigation System — Implementation Progress

> **Document role:** execution state, not design. The design lives in `docs/navsystem-longterm-plan.md` and changes rarely; this file changes every commit and tracks *where we are in building the plan*.
>
> **Update rule:** every commit / lay-off point / new bug discovery → update the relevant section of this file as part of the same commit. A fresh session should be able to read this file alone and know exactly what to do next.

---

## Phase status

- **Phase 0 — Refactor.** *Complete (2026-05-03).* All six lay-offs landed: `core_dllmain` + `engine_input`, `engine_offsets` + `engine_reads`, `engine_panels`, `engine_manager`, rename to `menus.cpp`, and the user-driven menu regression test ("everything working as before. no new bugs.").
- **Phase 1 — Foundation.** *Complete (2026-05-03).* All planned lay-offs landed: `engine_player` (1), CExoSound singleton trace (2), `audio_bus` (3), test fixture + exit gate (4), atmospheric-pass curation + `audio_cues.h` wiring (5), `core_settings` stub (7). Lay-off 6 (`audio_listener`) was dropped at lay-off 4 — engine default listener proved camera-anchored at the gate.
- **Phase 2 — Playable baseline.** *In progress (started 2026-05-03).* Lay-offs 1+2+3 landed in one session, lay-off 4 closed 2026-05-04 with the gate met in-game (cycle keys announce localised name + 3D cue + clock + distance; sub-state filters tight; German strings centralised). Phase 1 audio test fixture retired by lay-off 4's cue-on-cycle. Lay-off 5 (`guidance_autowalk`) closed 2026-05-04. Lay-off 6 (Pillar 4 → guidance binding) **partially closed** 2026-05-04 — code paths land cleanly, every Tolk announce / log line works, but engine-side player-control-mode dominance prevents both `AddMoveToPointAction` and `ForceMoveToPoint` from actually moving the player creature; functional autowalk is **parked** pending RE work (see lay-off 6 entry). Diagnostic instrumentation (per-tick progress watchdog, `Alt+-` ForceMoveToPoint diagnostic path) ships permanent so future autowalk debugging across guidance / view-mode / pathfind callers gets the same telemetry. Lay-offs 7-8 pending. Lay-off 9 (interaction model — Layers A+B passive narration + Execute*-routed interact hotkey) **scoped 2026-05-04** with parallel cursor-warp probe; see Phase 2 section below + `docs/navsystem-longterm-plan.md` "Cross-cutting — Interaction model".
- **Phase 3 — Pillar 1.** Pending.
- **Phase 4 — Pillar 2 polish + view mode.** Pending.
- **Phase 5 — Pillar 3 polish.** Pending.
- **Phase 6 — Map markers & nice extras.** Pending.
- **Phase 7 — User options UI.** Deferred per plan.

---

## Phase 2 — Playable baseline (in progress 2026-05-03)

### Goal

Make the game playable end-to-end via cycle-and-autowalk. Lands the four pieces called out in `docs/navsystem-longterm-plan.md` Phase 2: `engine_area.{h,cpp}` object-list + room-lookup foundation, Pillar 4 cycle (filter + cycle keys + announce name+direction+distance), cross-cutting `guidance/autowalk` (`AddMoveToPointAction` wrapper), and Pillar 2 `announce/transitions` (room + area).

**Exit criterion:** solo playthrough of an area works — player can `,/.` cycle to a focused object, hear name + direction + distance, press `Shift+-` to autowalk there, and hear room / area transitions narrate as they cross. No regressions in existing menu accessibility.

### Lay-off plan (drafted 2026-05-03)

1. **`engine_area.{h,cpp}`** — object iterator + kind/position reads + `GetRoom` wrapper. Foundation; no menu-side consumer wired up. *Closed.*
2. **Pillar 4 filter + cycle state** — `filter_objects.{h,cpp}` (six-category filter over `AreaObjectIterator`) + `cycle_state.{h,cpp}` (current category + focused object + per-tick rebuild). No input wiring yet; pure data layer testable from the per-tick monitor. *Closed.*
3. **Pillar 4 cycle keys** — `,` / `.` / `Shift+,` / `Shift+.` wired into `OnHandleInputEvent`. Mutates cycle_state; no announcement yet (focus changes go silent until lay-off 4). *Closed.*
4. **Pillar 4 announce** — `-` keypress speaks "name + direction (clock position) + distance (m)" via Tolk. First user-perceptible Phase 2 milestone. Per-type name resolution (Door, NPC, Container, Item, Landmark, Transition) lands here. *Closed (this lay-off).*
5. **`guidance_autowalk.{h,cpp}`** — `AddMoveToPointAction` wrapper. Cross-cutting subsystem callable with a `Vector` destination. *Closed.*
6. **Pillar 4 → guidance binding** — `Shift+-` pathfind to currently-focused object via guidance/autowalk. With autowalk-only (beacon comes in Phase 5). *Closed.*
7. **Pillar 2 transitions** — room transition (per-tick `GetRoomAt` delta, speak room name) + area transition (hook `CClientExoApp::AddMoveToModuleMovie @0x5edb50` for "loading: …", then on area-enter speak the new area name).
8. **Phase 2 exit gate** — solo playthrough of one area confirms cycle-and-autowalk loop. Phase 1 audio test fixture removed once a real Phase 2 consumer (lay-off 4 or 7) demonstrates 3D audio in production code.
9. **Interaction model — Layers A+B** *(scoped 2026-05-04 — see `docs/navsystem-longterm-plan.md` "Cross-cutting — Interaction model")*:
   - **9-probe** (parallel single-trip RE step) — *Closed 2026-05-04 (in-game data captured).* Diagnostic in `patches/Accessibility/probe_world_hover.{h,cpp}` ran live (`patch-20260504-063846.log`). Verdict: **`LastTarget` populates organically** as the player walks (transitions captured: `0x7f000000` ↔ `0x80000004`, `0x80000004` ↔ `0x800000c6` near interactables) — Layer A unblocked. **`MoveMouseToPosition(mgr, 320, 240)` does NOT change world-hover state** (`target_changed=0` and `mover_changed=0` across 8 Alt+P warps) — Layer C dropped. Probe stays in tree until lay-off 9a lands as a working pair (LastTarget watcher *should* fire on the same handles the probe logged); deletable thereafter as a single commit. Investigation Q6 + long-term plan updated 2026-05-04.
   - **9a — Passive-selection narration loop.** *Build verified 2026-05-04; awaiting in-game test.* Implementation:
     - `patches/Accessibility/passive_narrate.{h,cpp}` (~165 lines) — `Tick()` runs from `OnUpdate`, reads `LastTarget` via the same client-app chain the probe used, caches last-seen handle, classifies the resolved object through `acc::filter::ObjectMatches` over the six locked Pillar 4 categories, plays the per-category 3D cue at the object's position, speaks the localised name via Tolk.
     - `engine_area.{h,cpp}` — added public `ResolveObjectHandle(uint32_t)` helper. Walks the same `AppManager → CServerExoApp → CGameObjectArray::GetGameObject` chain `AreaObjectIterator::Next` uses, with the inverted-bool semantics; SEH-guarded; rejects all three engine sentinels (`0`, `0xFFFFFFFF`, `0x7F000000`).
     - Empty-name fallback: speaks the localised category label (`Tür`, `Person`, `Behälter`, …) when the per-kind name resolver returns empty.
     - Skips no-target transitions (silence on focus loss; logs the transition for post-mortem). First-tick-after-DLL-load is suppressed to avoid speaking on resume.
     - Logs every resolved + spoken event as `PassiveNarrate: <prev> -> <new> cat=X name=[Y] pos=(...)`. Same log file as the probe — easy to correlate against the `Probe: LastTarget changed` lines from the same handle stream.
     - **Independent of cycle:** cycle's own narration path keeps firing on cycle keys; 9a adds the ambient channel on top. Double-narration acceptable for first cut; recency-suppress (~500 ms) added later if disruptive.
     - **Run:** `kdev apply` + `kdev launch --monitor`, load a save, walk past doors / NPCs / containers — should hear cue + name as `LastTarget` changes (correlate `PassiveNarrate:` log lines with `Probe: LastTarget changed` lines for the same handles seen in the probe run).
   - **9b — Combined autowalk+interact hotkey.** Single key (proposed `Enter`) reads cycle focus first / `LastTarget` fallback, dispatches by kind: NPC → `ExecuteCommandActionStartConversation @0x0052d5b0`, Door / Placeable / Container / Item → `ExecuteCommandActionInteractObject @0x0052cc70`. Engine handles walk-to-target leg internally. Spoken pre-roll ("Sprich mit X" / "Öffne X" / "Hebe X auf") via existing strings table. Side-channel test of the parked autowalk blocker — if Execute* moves the player when `AddMoveToPointAction` doesn't, we've found the missing layer.

Each lay-off = one session = one commit, per the discipline rule.

### Lay-off log

**Lay-off 1** — `engine_area.{h,cpp}`. *Build verified 2026-05-03; awaiting commit.*

Added to `patches/Accessibility/`:

- `engine_area.h` — public surface: `GetCurrentArea`, `GetObjectKind`, `GetObjectPosition`, `GetRoomAt`, `AreaObjectIterator` class, `enum class GameObjectKind` (Door=10, Creature=5, Item=6, Trigger=7, Placeable=9, Waypoint=12, …). Plus file-scope addresses (`kAddrCSWSAreaGetRoom = 0x004BB600`) and offsets (`kAreaGameObjectsOffset = 0x190`, `kAreaGameObjectCountOffset = 0x194`, `kAreaRoomsOffset = 0x230`, `kAreaRoomNamesOffset = 0x25c`, `kRoomStride = 0x4c`, `kObjectKindOffset = 0x8`). Convention matches `engine_player.h` / `engine_manager.h`.
- `engine_area.cpp` — implementation. Pulls `engine_player.h` for `GetPlayerArea` (single source of truth) and reuses `kServerObjectPositionOffset` (already defined there) for `GetObjectPosition`. Every dereference is SEH-wrapped per the engine_reads convention.

Decision — **direct CExoArrayList iteration over `CSWSArea::GetFirstObjectInArea` / `GetNextObjectInArea`**. The SARIF entries return `undefined4` (likely an engine handle, not a `CSWSObject*`) and would re-enter engine code per step. The underlying array layout at `+0x190` / `+0x194` matches our existing `CExoArrayList` pattern exactly — `data` is `CSWSObject**`, `size` is `int`. Direct iteration gives us the raw object pointers we need with no per-step engine call. The "object deletion mid-iter" risk is real but contained: a single OnUpdate-tick scan completes before the next engine bookkeeping pass, and per-element reads are SEH-guarded.

Decision — **defer per-type name resolution to consumer lay-offs**. Each game-object subclass has its own name field at a different offset (Door.loc_name +0x39c, Creature.first_name +0x14, Container.loc_name +0x228, Item.localized_name +0x280, Waypoint.localized_name +0x238, Trigger.localized_name +0x228 — per investigation Q5). Wiring all six readers in this lay-off would be code without a consumer to validate the offsets against. Lay-off 4 (Pillar 4 announce) lands them with the speech path so each offset gets verified by actually pronouncing a name in-game.

Decision — **defer room-name reading to lay-off 7**. The investigation has `CSWSArea.room_names @+0x25c` typed as `CExoString*` but doesn't pin whether that's a pointer to a heap CExoString[] or an inline CExoString embedded at the offset. Either interpretation reads as a 4-byte pointer at +0x25c followed by uint32 length, and a wrong guess could fault on the very first lookup. Lay-off 7 (Pillar 2 transitions) is where the runtime ground truth surfaces — a per-tick `GetRoomAt` delta will exercise the read on every transition, and the live log will tell us within seconds whether the layout guess was right. Until then, `engine_area.h` exposes `GetRoomAt` (returns the opaque room pointer) and consumers compare pointers for delta-detection without needing the name.

Decision — **`AreaObjectIterator` as a small POD class, not a templated visitor**. Keeps the header lean and matches the C-style of `engine_player.h` / `engine_reads.h`. Pattern at the consumer: `for (AreaObjectIterator it(area); void* obj = it.Next(); )`. Snapshot of `data` + `size` taken once at construction; safe across a single per-tick scan (the array is rebuilt on area-load, never mid-frame).

Decision — **enum value `Area = 4` exposed alongside player-facing kinds** even though it isn't surfaced by Pillar 4. The enum is the engine's authoritative `GAME_OBJECT_TYPES` table; truncating it would force consumers comparing kinds against `4` to either use a magic literal or re-declare the enum. Cheap to include all values; only `Door`/`Creature`/`Item`/`Trigger`/`Placeable`/`Waypoint` are filter targets per the plan.

No menu-side consumer yet — `menus.cpp` is unchanged. The Phase 1 audio test fixture remains in place and stays silent at the test point.

Build verified: `kdev build` clean (12 .cpp files, DLL exports verified).

Discipline: pure addition, no behavioural change, no engine indirection beyond the wrapped `GetRoom` call (which itself faults cleanly under SEH if anything in the chain is bad). Safe to commit and chain into a fresh session for lay-off 2.

**Lay-off 2** — `filter_objects.{h,cpp}` + `cycle_state.{h,cpp}`. *Build verified 2026-05-03; awaiting commit.*

Chained into the same session as lay-off 1 per user direction: both are pure code-base additions building on the same source (`engine_area.h`), neither has a testable runtime effect, splitting them across sessions adds overhead without buying review value.

Added to `patches/Accessibility/`:

- `filter_objects.h` — `enum class CycleCategory { Door, Npc, Container, Item, Landmark, Transition, Count_ }` (order matches the plan's listing) + `CategoryName` (for log/TTS prefix) + `ObjectMatches(obj, category)` predicate + `NextCategory` / `PrevCategory` rotation helpers.
- `filter_objects.cpp` — kind-only matching wired through `acc::engine::GetObjectKind`. Sub-state predicates (Container's `usable OR has_inventory`, Landmark's `has_map_note`, Transition's `transition_destination`) are tagged `// TODO lay-off 4` rather than implemented — those per-type offsets need runtime verification, which lay-off 4 (Pillar 4 announce) provides via the speech path.
- `cycle_state.h` — `struct CategoryListing { void* objs[64]; Vector positions[64]; float distances[64]; int count }` (fixed-cap; truncates with one-time log) + `struct CycleState { CycleCategory category; void* focusedObj; int focusedIndex }` singleton + `BuildCategoryListing` / `RefreshCurrentListing` / `CycleNextItem` / `CyclePrevItem` / `CycleNextCategory` / `CyclePrevCategory`.
- `cycle_state.cpp` — internal helpers: insertion sort by distance ascending (N≤64; cheap), 2D horizontal distance (Z deliberately ignored — Pillar 4 announces clock-position + metres on the floor, not vertical separation). Empty-category silent skip implemented inline in `CycleCategoryDirectional` (max 6 attempts, then reports empty).

Decision — **whole-area scan as a placeholder for "current room + LOS extension"**. The plan locks the cycle scope to "current room + LOS extension" but the room-cluster slice of `engine_area` (room adjacency walks, LOS-transparent material masks) is currently scheduled for the same lay-off as Pillar 2 transitions, since both consume `GetRoomAt` deltas. Doing the LOS extension in lay-off 2 would force the room-cluster work in here too, which spills the lay-off's scope. Whole-area over-includes objects from adjacent rooms — acceptable for dev-loop testing because lay-off 2 + 3 don't speak; the per-tick monitor log (when added in lay-off 3) will show the over-inclusion clearly when the user audits before lay-off 4 tightens scope.

Decision — **clamp at item-cycle boundaries, not wraparound**. The plan's open question on item-cycle boundary behaviour isn't locked yet. Picked clamp (`,` at index 0 stays at index 0) as the conservative default — wraparound surprises a user with a "back to start" jump on what should be a no-op key, which is more disorienting than a silent hold. Trivial to flip to wraparound later if user testing prefers it.

Decision — **`focusedObj` is the source of truth, `focusedIndex` is derived**. On every rebuild, `RefreshCurrentListing` re-finds the previously-focused object by pointer comparison in the new sorted order; if it's no longer present (object removed, out of scan scope, NPC walked into another room), focus resets to closest (index 0). This is the right default for "focus tracks the thing the user picked, not the slot it happened to occupy" — handles NPC movement and area object churn without surprising the user.

Decision — **insertion sort over qsort/std::sort**. N ≤ 64 means insertion sort is ~2K compares worst-case — same cost order as a single SEH frame, far below either qsort's call overhead or std::sort's template-bloat in our DLL. Avoids dragging in <algorithm> for a one-off use.

Decision — **horizontal-only distance**. `HorizontalDistance` ignores Z entirely. Pillar 4's announcement is "where is the thing relative to the player on the floor" (clock-position + metres); vertical separation in multi-storey rooms doesn't change the answer. If a future feature needs Z (e.g. a "stairs above you" cue), change in one spot.

No menu-side consumer yet — `menus.cpp` is unchanged. The `CycleState` singleton is reachable but never exercised; lay-off 3 will wire the first input dispatch into it.

Build verified: `kdev build` clean (14 .cpp files, DLL exports verified).

Discipline: pure addition, no behavioural change. Chained into lay-off 1's session per user direction; safe to commit both together. Next session opens lay-off 3 (cycle keys input wiring) which needs a runtime key-code discovery step first — see "Next session" below.

**Lay-off 3** — `cycle_input.{h,cpp}` + `engine_input.h` cycle-key constants + wire-up in `menus.cpp`. *Build verified 2026-05-03; in-game key-code verification pending.*

Chained into the same session as lay-offs 1 + 2 once the key-code "discovery step" turned out to be unnecessary — the InputIndices table in `engine_input.cpp` already names KEYBOARD_COMMA(103), KEYBOARD_PERIOD(104), KEYBOARD_MINUS(94), KEYBOARD_LEFTSHIFT(24), KEYBOARD_RIGHTSHIFT(25), and per `engine_input.cpp`'s `ManagerTranslateCode` comment, codes outside the recognised navigation set pass through unchanged. So unmapped keys arrive at our hook as their raw InputIndices values. No runtime probe needed — the working assumption is testable on first run.

Added to `patches/Accessibility/`:

- `engine_input.h` — five new file-scope constants (`kInputKbLeftShift = 24`, `kInputKbRightShift = 25`, `kInputKbMinus = 94`, `kInputKbComma = 103`, `kInputKbPeriod = 104`). Header comment marks them as "working assumption from k_names[] table; will be confirmed at first in-game test".
- `cycle_input.h` — single public surface: `acc::cycle_input::TryHandleEvent(param_1, param_2)` returns true if the event was consumed.
- `cycle_input.cpp` — internal `g_shiftHeld` flag mutated by shift up/down events (DirectInput delivers each key as its own event; manager doesn't bake shift into a modifier flag). Dispatch table: `,` → `CyclePrevItem`, `.` → `CycleNextItem`, `Shift+,` → `CyclePrevCategory`, `Shift+.` → `CycleNextCategory`, `-` and `Shift+-` → log-only stubs (announce + pathfind land in lay-offs 4 + 6). Every cycle action gates on `GetPlayerPosition` succeeding — if no player creature is loaded (menu / chargen / area-load mid-flight), the key falls through to the engine's normal handler. Each consumed event logs the new focused-object pointer + distance + category + index for verification.

Wired into `menus.cpp` at the top of `OnHandleInputEvent` (right after `consumed = false`): `if (acc::cycle_input::TryHandleEvent(param_1, param_2)) consumed = true;`. Routed first because cycle is in-game-only and the handler self-gates; in menus / chargen / dialog it returns false and the key falls through to the existing menu logic (Enter activate / Esc close / arrow chain navigation) below.

Decision — **shift state tracked across calls, not derived per event**. The manager's `param_1` is a logical key code with no modifier flags baked in. Tracking shift via a static bool in `cycle_input.cpp` is the smallest path. Edge cases (shift release while game is suspended, alt-tab between press and release leaving stale `g_shiftHeld=true`) are best-effort — the next shift event self-corrects. If they prove disruptive in practice, query the engine's modifier state directly (the manager almost certainly has a `bit_flags` field for it).

Decision — **typematic allowed (no debounce)**. The OS keyboard repeat rate will fire successive press events while the user holds `,` or `.` — each one cycles one item. Acceptable as default behaviour: holding `.` becomes "scan through this category quickly". If the audio cue from lay-off 4 makes that uncomfortable, debounce can be added in `cycle_input.cpp` with a millisecond-level static.

Decision — **stub minus / shift-minus with log only, not no-op**. The point of wiring `-` / `Shift+-` here even before announce / pathfind exist is so the user can test the dispatch path end-to-end and so cycle_state's `focusedIndex`/`focusedObj` get exercised by the keypress with full visibility. The log line for these stubs prints the focused-object pointer + position + category — enough to verify cycle_state mutations in lay-offs 4 + 6 land on the right object.

Includes added to `menus.cpp`: `cycle_input.h`. No menus.cpp behavioural changes beyond the one-line dispatch insertion.

Build verified: `kdev build` clean (15 .cpp files, DLL exports verified).

In-game verification needed (next session start, single-trip): launch into a save with a player loaded, press `,/./Shift+,/Shift+.` and watch `Cycle:` log lines fire with sensible focused-object data. Confirm `kInputKbComma=103` etc. assumption. If wrong codes, patch `engine_input.h` (the existing per-event log line in `OnHandleInputEvent` will show the actual `param_1` value).

Discipline: third lay-off in one session — chained per the non-testable-lay-offs feedback rule (memory: `feedback_chain_nontestable_layoffs.md`). Strictly speaking lay-off 3 IS testable (verifying key codes), but it's a one-press-per-key-combo sanity check rather than a perceptual evaluation, so chaining is reasonable. The fresh-session boundary is appropriate before lay-off 4 (announce) — that's where TTS quality + per-type name resolution land, both of which warrant un-degraded context for tuning.

**Lay-off 4** — Pillar 4 announce. *Closed 2026-05-04 (gate met in-game).*

The first user-perceptible Phase 2 milestone — `,/.` cycle items, `Shift+,/.` cycle categories, `-` repeats focus, with each cycle key emitting a 3D positional cue at the focused object's world position followed by a Tolk speech announcement of the localized name + clock-position + distance. Verified end-to-end across two test sessions on Endar Spire opening corridors. Scope ballooned well past the original plan due to four engine-side bugs found during gate testing — full bug list below.

### Per-type name resolution (`engine_area.{h,cpp}`)

`acc::engine::GetObjectName(obj, outBuf, bufSize)` — kind-aware resolver. Uses the per-subclass `CExoLocString` offsets locked in investigation Q5 + Q7: Door @+0x39c, Creature via `creature_stats* @+0xa74` → first_name @+0x14, Placeable @+0x228, Item @+0x280, Waypoint @+0x238, Trigger @+0x228. Treats CExoLocString as an 8-byte aggregate equivalent to CExoString at the byte level (per the Q7 "simple read pattern" line) — `engine_reads::ExtractTextOrStrRef` handles both: tries the inline pointer first, falls back to TLK strref at +4. Final fallback: `CSWSObject.tag @+0x18` (CExoString) so unnamed mod scaffolding gets an audible identifier rather than a silent skip. Every dereference SEH-wrapped.

`kCreatureStatsPtrOffset = 0xa74` derived locally from `docs/llm-docs/re/swkotor.exe.h` (Ghidra DATATYPEs dump): `CSWSCreatureAppearanceInfo` is 0x24 bytes starting at +0xa50, so `creature_stats*` lands at +0xa74. Q5 listed `CreatureStats.first_name +0x14` without pinning the parent offset; this is the missing link.

### Sub-state filter tightening (`filter_objects.cpp`)

Container / Landmark / Transition predicates now AND the kind tag with sub-state checks: `usable=1 OR has_inventory=1` at CSWSPlaceable +0x328/+0x334, `has_map_note=1` at CSWSWaypoint +0x228, `transition_destination != (0,0,0)` at CSWSTrigger +0x30c. The lay-off 2 over-inclusion (Container = "every Placeable") is now resolved.

### Clock-position direction frame (`cycle_input.cpp`)

`ClockPosition(playerYawDeg, dx, dy)` — `atan2(dy,dx) - playerYaw` in degrees, negated to flip CCW→CW, bucketed into 12 sectors of 30° each. Returns 1..12 with 12 = directly ahead, 3 = right, 6 = behind, 9 = left. Player yaw read server-side via `GetPlayerYawDegrees` (`CSWSObject.orientation @+0x9c`).

### i18n string table (`strings.{h,cpp}` + `strings_en.cpp` + `strings_de.cpp`)

User-driven course-correction during the gate test: hardcoded English wrapper strings in cycle_input.cpp clashed with German TLK-resolved object names. Centralised every spoken string into a typed-id table:

- Singular category names (used as cycle prefix): "Door", "Tür", etc.
- Per-category empty messages with localised plurals: "Keine Türen in Reichweite" vs "Keine Türen" (the German singular/plural split — Türen, Personen, Behälter, Gegenstände, Orte, Übergänge — would have been awkward to template).
- Format templates that carry the unit words and direction idiom: English `"%s, %d o'clock, %d metres"`, German `"%s, auf %d Uhr, %d Meter"` ("auf X Uhr" is the German nav idiom; bare "X Uhr" would read as time-of-day).

Two language files compiled in, runtime switch via `acc::strings::SetLanguage(Lang::De)`. **German is the default** — the user is testing on a German install and the locale-mix issue surfaced live during the gate. Phase 7 (deferred) will surface a runtime UI toggle. Encoding: Windows-1252 hex escapes (`\xFC` for ü, etc.) so literal bytes match Tolk's `MultiByteToWideChar(CP_ACP)` on the user's German install. UTF-8 source would also work but only with `/utf-8`, which `create-patch.bat` doesn't pass.

Logs intentionally stay English regardless of language — `acc::filter::CategoryName` (developer-readable) is *not* routed through the table.

### Auto-announce on cycle (UX correction)

Original lay-off plan: `,/.` step silently, `-` announces. User feedback during gate test: every screen-reader paradigm announces on focus change; silent step is alien. Restructured handlers so:

- `,/.` → step within current category, auto-announce new focus
- `Shift+,/.` → step to next/prev non-empty category, auto-announce `"{Category}. {item}, {clock}, {distance}"`
- `-` → repeat current focus (useful when the screen reader was interrupted)
- `Shift+-` → still log-only stub (lay-off 6 wires guidance/autowalk)

If category cycle exhausts all 6 categories empty, falls through to the localised "Keine Objekte in Reichweite" without a misleading category prefix.

### 3D cue-on-cycle (Phase 1 fixture retirement)

Each cycle-key fire plays the per-category cue from `audio_cues.h` (`gui_close` for doors, `fs_metal_droid2` for NPCs, etc.) at the focused object's world position via `acc::audio::PlayCue3D` *before* speaking. The engine's Miles 3D pipeline pans + attenuates relative to the camera-anchored listener (verified at Phase 1 lay-off 4 gate). User hears spatial direction first, then the localised speech reinforces it.

This satisfies the Phase 2 exit-gate criterion that retires the Phase 1 audio test fixture: "Phase 1 audio test fixture removed once a real Phase 2 consumer (lay-off 4 or 7) demonstrates 3D audio in production code." The throttled `Phase1Test: PlayCue3D ...` block in `OnUpdate` is removed.

### In-world input plumbing (`cycle_input::PollWin32`)

Critical discovery during gate test: **unbound keys in-world bypass `CSWGuiManager::HandleInputEvent` entirely**. The engine's keymap (kotor.ini `[Keymapping]`) drops scancodes that aren't bound to any action before they reach the manager-level dispatcher. `,/./-` are unbound by default. From `patch-20260503-215023.log`: 86 events captured at our manager hook, zero with codes 103/104/105.

Resolution: added a Win32-side polling path in `OnUpdate` using `GetAsyncKeyState(VK_OEM_COMMA / VK_OEM_PERIOD / VK_OEM_2 / VK_OEM_MINUS / VK_SHIFT)`. Edge-detects rising edges (per-key static `prev` flags), self-gates on `GetForegroundWindow()` matching our PID + `GetPlayerPosition` succeeding. The OnHandleInputEvent path stays in place as a backup if anyone ever binds the keys via kotor.ini, sharing the same per-action handlers (`OnCycleItem` / `OnCycleCategory` / `OnAnnounceFocus`) so behaviour is identical regardless of ingestion path.

VK code subtlety: the physical key right of `.` is layout-dependent — `VK_OEM_2` on US QWERTY (`/`), `VK_OEM_MINUS` on German QWERTZ (`-`). Polling listens for both so the same physical "row" of cycle keys works on either layout. Linker: `cycle_input.cpp` adds `#pragma comment(lib, "user32.lib")` for `GetAsyncKeyState` / `GetForegroundWindow` / `GetWindowThreadProcessId`.

### Bug fixes uncovered during gate test

Four engine-side bugs in our reads, all surfaced because the gate test covered the full data-path end-to-end for the first time:

**1. `CSWSArea.game_objects` is a handle array, not a pointer array.** Source-of-truth check via `docs/llm-docs/re/swkotor.exe.h` — the field is typed `ulong *game_objects;` (an array of 32-bit object IDs), not `CSWSObject **`. Initial implementation dereferenced IDs as pointers; the `+0x8` kind read fell on garbage memory and every kind value came back outside the 5/6/7/9/10/12 set we filter on. Verified by `patch-20260503-224102.log`: snapshotSize=219, scanned=219, every kind bucket=0.

Fix: handle resolution via `CServerExoApp::GetObjectArray() → CGameObjectArray::GetGameObject(id, &out)`. The chain reads `*kAddrAppManagerPtr → AppManager + 0x8 → CServerExoApp*` (new — see bug #2). Iterator now resolves each handle to a CSWSObject* before yielding it. Sentinel handles (0 / 0xFFFFFFFF) skipped before resolution.

**2. AppManager wrapper has both client + server pointers.** Investigation Q1 documented `*kAddrAppManagerPtr → CClientExoApp*` via a single deref; lay-off 4's chain-fix corrected this to `+0x4` for the client. Now we also need server-side. Disassembly of `CSWSObject::GetArea @0x4cb120` (which uses `AppManager->server`) shows `mov ecx, [eax+0x8]` — so `AppManager + 0x8 → CServerExoApp*`. Added `kAppManagerServerOffset = 0x8` to engine_area.h and used it in the new `GetServerObjectArray()` helper.

**3. `CGameObjectArray::GetGameObject` returns *true on miss, false on hit*.** Decompiled @0x004d8230 — the function is structured "if found, write game_object and return false; if not found, write NULL and return true". Initial implementation read the bool as "was it found", treating every hit as a miss and returning nullptr unconditionally. Verified by `patch-20260503-225246.log`: snapshotSize=219, scanned=0 (every Next() short-circuited because `ok && out` was always false). Fix: rename the local from `ok` to `miss` and check `if (!miss && out) return out;`.

**4. German `-` key is `VK_OEM_MINUS`, not `VK_OEM_2`.** OEM virtual-key codes are layout-dependent. On US QWERTY, the key right of `.` (`/`) is `VK_OEM_2 (0xBF)`; on German QWERTZ, the same physical key (`-`) is `VK_OEM_MINUS (0xBD)`. From `patch-20260503-223622.log`: cycle of `,` and `.` fired correctly, no `-` events captured. Fix: poll both VK codes for the announce key.

### Decisions captured (deferred from initial gate plan)

- **Clock-position from server-side yaw, not camera**. Plan locks "relative to player facing"; server-side `CSWSObject.orientation @+0x9c` is the authoritative source. Camera-relative (`CSWGuiCamera`) would be a future option if user testing prefers it.
- **first_name only for Creatures, not first+last**. Most NPCs have only first_name populated. Concatenation introduces empty-last_name code paths; defer until audition shows it'd help.
- **Fall back to category label when name is empty**. "Tür, auf 3 Uhr, 5 Meter" (with an unnamed door rendering as the kind) more useful than "(unknown), …".
- **Distance to whole metres, no decimals.** Reads faster via TTS; player's near/far decision threshold is metre-grain anyway.
- **Interrupt previous speech on every cycle key.** Successive `,/.` presses are common during scan; queueing would lag.
- **3D cue plays on cycle, not on `-` repeat alone.** The cue carries category identification and spatial direction; making it part of every cycle keystroke gives the user immediate non-verbal feedback per step.
- **Cue and speech overlap intentionally.** Cue is short (60–300ms); Tolk speech is queued asynchronously by NVDA. They don't conflict — user hears cue first, speech rolls in.

### Files touched

- `patches/Accessibility/engine_area.{h,cpp}` — name resolver, sub-state predicates, handle-resolution iterator.
- `patches/Accessibility/filter_objects.{h,cpp}` — sub-state filter tightening.
- `patches/Accessibility/cycle_input.{h,cpp}` — ClockPosition, AnnounceCurrent, Win32 polling, OEM-key handling.
- `patches/Accessibility/strings.{h,cpp}`, `strings_en.cpp`, `strings_de.cpp` — new i18n table.
- `patches/Accessibility/engine_input.h` — `kInputKbAnnounce` (slash position) replaces `kInputKbMinus`.
- `patches/Accessibility/menus.cpp` — wired `cycle_input::PollWin32()` into `OnUpdate`; removed Phase 1 audio test fixture.
- `docs/navsystems-investigation.md` — Q1 chain note (AppManager+0x8 server pointer).

Build verified: `kdev build` clean (18 .cpp files, DLL exports verified). In-game verified across two sessions: door / NPC / item / placeable cycle hear localised name + 3D cue + clock + distance, German wording correct, sub-state filters work (Container drops scenery), clock updates as player rotates, distance updates as player walks.

Discipline: lay-off 4 ships as one bundled commit. Originally would have split per concern, but the bug-discovery dependency chain (handle-bug → resolution chain → inverted bool → polling-path → German VK → strings refactor → cue wiring) makes incremental commits ship intermediate states that crash or speak garbage. One coherent close-out commit is the cleaner shape. Fresh session for lay-off 5 (`guidance/autowalk`).

**Lay-off 5** — `guidance_autowalk.{h,cpp}`. *Build verified 2026-05-04; awaiting commit.*

Cross-cutting auto-walk wrapper around `CSWSCreature::AddMoveToPointAction @0x004F8B60`. Pure code-base addition — no consumer wired up; lay-off 6 binds `Shift+-` from cycle_input's pathfind-stub branch into `acc::guidance::WalkTo(dest)`.

Added to `patches/Accessibility/`:

- `guidance_autowalk.h` — public surface: `acc::guidance::WalkTo(const Vector& destination)` returning `bool`. Plus file-scope `kAddrCSWSCreatureAddMoveToPointAction = 0x004F8B60` and `kInvalidObjectId = 0x7F000000`. Convention matches `engine_player.h` / `audio_bus.h`.
- `guidance_autowalk.cpp` — implementation. PFN typedef encodes the full 17-arg signature decoded in investigation §Q3. Calls the engine with the minimum-viable arg set: `INVALID_OBJECT_ID` for both object refs, zeroes for every flag/timeout/radius/path-mode, destination as both primary and secondary point. SEH-wrapped per the engine_player convention.
- `engine_player.{h,cpp}` — added one public function `acc::engine::GetPlayerServerCreature()` that thinly forwards the existing internal `GetPlayerServerObject()` chain walk. Avoids duplicating the AppManager → CClientExoApp → GetPlayerCreature → server_object chain in guidance_autowalk; if the chain ever needs another fix (cf. Phase 1 lay-off 4's +0x4 chain correction) there's still one site to update.

Decision — **walk default, no run knob exposed**. `runFlag` is bit 0 of the engine's packed flags (0=walk, 1=run). The plan calls Mode A "auto-walk" — literal walking — and per CLAUDE.md "don't add knobs the task doesn't require". If a future consumer (lay-off 6 Pillar 4 binding, view-mode click-to-walk, Pillar 3 pathfind) wants run, that's the lay-off that adds the parameter, with `WalkTo(const Vector&, bool run = false)` as the obvious extension. Backwards-compatible by construction.

Decision — **monotonic ushort action-id counter**. Q3 documents `actionId` as caller-assigned and notes it's a queue tag, not a uniqueness key. ushort wraparound (every 65536 calls) is harmless: the engine doesn't enforce uniqueness across a wrap, and we don't read results back from the queue. Static counter inside `WalkTo` keeps state local; no global needed.

Decision — **destination passed as both primary and secondary point**. Q3 documents `secondaryPoint` as "look-at" / arrival-facing direction, with only X/Y read by the engine. Reusing the destination = "face the way you walked" on arrival, which is the natural default for click-to-move. A future Pillar 4 binding could pass the focused-object's facing as secondary if a per-object arrival pose is desired; not in scope here.

Decision — **`kInvalidObjectId` is a guidance-local constant for now**. The 0x7f000000 sentinel is engine-wide (used in many AI-queue object-id slots per Q3) but currently only this lay-off references it. If the next lay-off (cycle_input's `Shift+-` binding) or a future consumer needs it elsewhere, promote to `engine_offsets.h`. Premature promotion violates the YAGNI cue.

Decision — **expose `GetPlayerServerCreature()` instead of duplicating the chain walk**. The internal `GetPlayerServerObject()` in engine_player.cpp is anonymous-namespace static; making the chain reachable to guidance/* without copy-pasting it requires either promoting the existing helper or adding a thin public wrapper. Wrapper chosen: zero-cost forward, keeps engine_player.cpp's existing implementation undisturbed, gives the new public symbol a name that reflects its caller-facing role (caller is going to call thiscall methods on a *creature*, not just any object).

Convention check — flat-with-prefix layout (Phase 0 decision in `docs/navsystem-longterm-plan.md` decision log) means the plan-doc `guidance/autowalk.{h,cpp}` notation lands on disk as `guidance_autowalk.{h,cpp}`. Same translation `engine_area`, `audio_bus`, `cycle_state`, etc. already use.

No menus.cpp touches; no cycle_input.cpp touches. The Shift+- stub in cycle_input still logs only — lay-off 6 is where it dispatches into `acc::guidance::WalkTo`.

Build verified: `kdev build` clean (19 .cpp files, DLL exports verified).

Discipline: pure addition, no behavioural change, no engine indirection at runtime (function only fires when a future consumer calls `WalkTo`). Single coherent commit for the engine_player +1 function + the two new guidance files. Fresh session for lay-off 6 (`Shift+-` binding) — that's where the first runtime test of `WalkTo` lands and warrants un-degraded context for verifying the engine call works against a live player.

**Lay-off 6** — Pillar 4 → guidance binding. *Build verified 2026-05-04; in-game tested 2026-05-04 — code paths land cleanly, but autowalk movement blocked by engine-side player-control-mode dominance. Functional autowalk parked pending RE work (see "Engine-side autowalk blocker" below).*

Replaces the `Shift+-` log-only stub in `cycle_input.cpp` with a real handler that calls `acc::guidance::WalkTo(focused.position)` (lay-off 5's wrapper) on the Pillar 4 currently-focused object, plays the per-category 3D cue at the destination, and speaks the localized "Guiding to {name}" payload. First runtime test of the lay-off 5 wrapper — first time the engine's AddMoveToPointAction is invoked from our DLL.

Files touched:

- `cycle_input.cpp` — `OnPathfindFocusStub` → `OnPathfindFocus`. Empty-state check (no focus → speaks `GuidanceNoFocus`, skips WalkTo). Cue-then-speech ordering matches the Pillar 4 cycle convention. Reuses the same `BindingsFor(category)` table from lay-off 4 — the user already knows the category from the cycle, so the cue is per-category, not guidance-specific. Includes `guidance_autowalk.h`.
- `cycle_input.h` — header comment extended to record lay-off 6's status.
- `strings.h` + `strings_en.cpp` + `strings_de.cpp` — three new IDs: `FmtGuidingTo` ("Guiding to %s" / "Gehe zu %s"), `FmtGuidingFailed` ("Guidance to %s failed" / "Gehe zu %s fehlgeschlagen"), `GuidanceNoFocus` ("No object focused" / "Kein Ziel ausgewählt"). German uses the imperative-direct nav idiom matching the existing `FmtAnnounceWithClock` style ("auf X Uhr").

Decision — **defer explicit toggle-cancel**. The plan §"Cancellation" locks `Shift+-`-pressed-again-while-active = cancel, but the engine's clear-action / abort-move entry point isn't decoded yet (investigation Q3 lists move-queue entries — `AddMoveToPointAction`, `AddMoveToPointActionToFront`, `ForceMoveToPoint`, `AddPathfindingWaitActionToFront` — but no symmetrical `ClearAction` / `CancelMove`). Engine-convention cancel — *any directional input from the player interrupts auto-walk* — already works for free per the plan §"Mode A — Auto-walk". Re-pressing Shift+- in this lay-off re-issues the move (engine queues it; harmless because the destination is identical to the queued one). Explicit toggle-cancel lands in a follow-up lay-off after the cancel function gets RE'd. Captured as a parked follow-up below.

Decision — **per-category cue, not a guidance-specific cue**. Pillar 1's audio vocabulary curation locked `gui_actscroll` as a *beacon-active* cue and `gui_complete` as *beacon-destination-reached* — both Phase 5 work. Until Phase 5, the autowalk-only path has no continuous direction-feedback cue. For the start-of-guidance moment, reusing the per-category cue (the same one the cycle keys play) carries the spatial confirmation we want — "I'm acting on this category at that position" — without introducing a new cue slot. When Phase 5 ships the beacon system, the start-of-guidance cue can be re-evaluated.

Decision — **WalkTo failure speaks the localized "Gehe zu {name} fehlgeschlagen" phrase**. `acc::guidance::WalkTo` returns false only when no player creature is loaded (already gated by the dispatcher's `GetPlayerPosition` check) or when the engine call faults under SEH. Initial implementation log-only-on-failure (rationale: SEH cases produce no actionable diagnosis for the user) — but per memory `feedback_never_silence_fallback_announcement`, silent failure leaves the user unable to distinguish keypress-eaten from action-failed, which is worse than a confusing-but-clear failure announcement. Revised at user direction during decision-walkthrough: speak `FmtGuidingFailed` with the focused-object name so the user knows which attempt failed. Cue still plays before the WalkTo call (cue = "I'm acting on this object at this position"; speech = "but couldn't"), so the user gets spatial confirmation followed by the failure announcement.

Decision — **destination-only autowalk; no facing override**. `WalkTo` already passes the destination as both primary and secondary point per lay-off 5's defaults. The plan's "arrival facing" is implicit in that — the player ends up facing the way they walked, which for "guide me to door X" is "facing door X". A future polish could pass `obj.position + obj.facing` as secondary so the player ends up facing whatever the door faces; not in scope here.

No menus.cpp touches; no engine-side new addresses (the engine call lives entirely in lay-off 5's wrapper).

Build verified: `kdev build` clean (19 .cpp files, DLL exports verified).

In-game verification needed (next session start, single-trip test): launch into a save with a player loaded, cycle to a door / NPC / placeable across the corridor, press Shift+-, watch character path-walk to the destination while TTS speaks "Gehe zu {name}". Distinct test cases: (1) door target (movement across walkmesh), (2) NPC target (moving target), (3) press without focus (Shift+- before any cycle key — should speak `GuidanceNoFocus`), (4) press during active autowalk (re-issues; harmless), (5) press then move with arrow keys (engine should cancel autowalk per plan).

Cancel-on-second-press test will fail in this lay-off — that's expected; it's the parked follow-up.

### Engine-side autowalk blocker (in-game test 2026-05-04)

What works end-to-end:

- `Shift+-` and `Alt+-` (the diagnostic) both fire cleanly. Cycle state, focused-object resolution, `acc::engine::GetObjectName`, distance + clock-position computation, `acc::audio::PlayCue3D` at the destination, Tolk announce, every log line — all of it lands as designed.
- `acc::guidance::WalkTo` calls `CSWSCreature::AddMoveToPointAction @0x004F8B60`. Engine returns `0x00000001` (success-shaped) every time. No SEH fault.
- `acc::guidance::ForceWalkTo` calls `CSWSCreature::ForceMoveToPoint @0x004EDBA0` with a populated `CSWSForcedAction` struct (28 bytes, layout per `swkotor.exe.h`). Function is `void`, returns no diagnostic; runs without SEH fault.

What doesn't:

- Across 18 dispatch attempts spanning multiple game states (in-tutorial, post-tutorial Spire corridor, near-target close-range, far-target across-area), **the player creature did not move once** as a result of either dispatch. Watchdog reports `moved=0.00m` at t+1s and `moved=0.00m (still stuck)` at t+3s on every attempt. Manual walking between attempts works fully — the `from=(...)` field of successive dispatches changes as the user repositions, confirming the player creature itself is movable.

What this rules out:

- *Tutorial / cutscene gate* — manual walking works in the same states where autowalk is silent.
- *Queue contention* — `ForceMoveToPoint` bypasses the action queue entirely; same null result.
- *Bad creature pointer* — same `engine_player::GetPlayerServerCreature` chain whose position reads (`from=(15.42,20.12)`) are correct.
- *Bad signature / struct layout* — neither call faults; both engine-side functions accept the call without protest.

What's left as the most plausible cause:

- **Player-control-mode dominance.** Aurora-engine creatures have a control-mode flag separating "player-driven" (input vector clobbers AI per tick) from "AI-driven" (action queue executes). Our raw calls add the move action but the per-tick loop overwrites it with the input-driven vector (zero when no key is held → "stand still"). NWScript's `ExecuteCommandActionMoveToPoint` works on the player because it presumably toggles control mode internally; we don't.

### Diagnostic instrumentation shipped (permanent)

Every `acc::guidance::WalkTo` and `acc::guidance::ForceWalkTo` call writes:

- One `Autowalk: WalkTo dispatch ...` / `Autowalk: Force-dispatch ...` line at fire time, with destination, pre-dispatch player position, distance-to-dest, action_id, and (for `WalkTo`) the engine return value.
- `Autowalk: <tag> t+1s moved=X.XXm dist=Y.YYm (stuck|moving)` after one second — the canonical "did the engine actually move us" check.
- `Autowalk: <tag> t+3s moved=X.XXm dist=Y.YYm (still stuck|reached|moving)` after three.

Watchdog idle-cost is one bool check per `OnUpdate` tick; only fires when a recent dispatch is in flight. Two log lines per dispatch maximum. Reused by every future guidance caller (Pillar 2 view-mode click-to-walk, Pillar 3 pathfind, anywhere else autowalk gets invoked) — no per-feature instrumentation needed.

`Alt+-` is wired permanently as the `ForceMoveToPoint` path. Side note: pressing Alt in Windows enters menu-activation mode and produces a "ding" system sound when the next key isn't a menu mnemonic — a Windows-side annoyance, not our code, suppressible only by hooking `WndProc`'s `WM_SYSCHAR`. If the diagnostic path stays as a permanent feature, rebind `Alt+-` to something else (`Ctrl+-` clean, or unmodified `=`) to avoid the ding.

### Path forward (not blocking other Phase 2 work)

The engine-side investigation needed to actually move the player is parallel work, not a hard prerequisite for Phase 2's other lay-offs:

1. **Disassemble `ExecuteCommandActionMoveToPoint`** (a `CClientExoApp` method per investigation Q1) — it's NWScript's wrapper that demonstrably works on the player. Compare what it does before/after `AddMoveToPointAction`. Anything outside the bare AI-action enqueue (control-mode toggle, input-override release, AI flag flip) is the missing ingredient.
2. **Search for control-mode setters on `CSWSCreature`.** SARIF candidates: `SetAIControlMode`, `EnablePathfindingAI`, `SetPossessed`, `SetPlayerControlled`, anything in the `0x4f0000–0x510000` range that takes a bool and might gate AI execution.
3. **Compare to `CSWCCreature.moving_orientation +0x1c4`** (investigation Q3) — the *client*-side moving state. Maybe the path-finder gates on the client-side flag mirroring the server's queued action; if there's a missing client-side setup our raw server-side call skips it.
4. **Worst case fallback: route through NWScript.** If the control-mode toggle isn't trivially decoded, call `ExecuteCommandActionMoveToPoint` directly (same address `0x53cae0`-ish range, listed in Q1). Slower (VM-call overhead), but works because that's the path the engine itself uses.

**Follow-ups parked from lay-off 6 (not blocking lay-off 7):**

- **Engine-side autowalk blocker** — see above. Highest-leverage RE item left in Phase 2; without it, autowalk is silent. Doesn't block other lay-offs because Pillar 1 (Phase 3) and Pillar 2 (lay-off 7) make manual walking comfortable enough that autowalk is a nice-to-have rather than a must-have for the playable-baseline gate.
- **Explicit Shift+- toggle-cancel** — needs RE of the engine's clear-action / abort-move function. Recipe-4 candidates: trace callers of `AIActionMoveToPoint @0x51f4f0` for an "if cancelled" early-exit, OR look for a sibling of `CSWSObject::AddAction` named `ClearActions` / `RemoveActions`. Once decoded, add `acc::guidance::CancelMovement()` and a state flag in `cycle_input` to dispatch press-twice → cancel. (Moot until the autowalk-doesn't-move blocker is resolved.)
- **Arrival-facing polish** — pass `dest + obj.facing_offset` as secondary point so the player ends up oriented toward the object's interaction face (e.g. facing the door's open direction, not its back). Needs per-kind facing reads. Defer until first user feedback.
- **Run-vs-walk knob** — currently locked to walk per lay-off 5's default. If user feedback shows autowalk is too slow for cross-area moves, lift `WalkTo` to take an optional `run` parameter and decide policy at this callsite (e.g. `run = (distance > threshold)`).
- **Rebind `Alt+-` diagnostic to a non-Alt combination** to silence the Windows menu-activation "ding" sound. Candidate: `Ctrl+-` (no menu interaction in Windows), or unmodified `=`. Keep the Force path as a permanent diagnostic; just on a quieter modifier.

---

## Phase 1 — Foundation (closed 2026-05-03)

Phase 0 closed 2026-05-03. Phase 1 closed same day. The Phase 0 lay-off log is preserved further down as historical record. The Phase 1 lay-off log below documents the foundation work (player-pose reader, audio-bus wrappers, in-game gate, 12-cue curation + table, settings stub) that Phase 2+ build on.

### Goal

Lay the foundation that the playable-baseline (Phase 2) and pillar phases (3-5) build on: the audio bus + listener override, player-state readers, settings stub, and audio-cue vocabulary. The exit criterion is a **test fixture that plays a 3D positional cue at any world position with character-anchored listener** — i.e. proof that the audio path works end-to-end before Phase 2 wires it up to a real consumer.

### Scope adjustment from plan (2026-05-03)

The plan grouped `engine_area.{h,cpp}` (area cache: walkmesh edges, object lists, room lookups) into Phase 1 as foundation. We're **moving it out** for two reasons:

- It has no Phase 1 consumer. The exit criterion only needs player position to anchor the listener; area-level state isn't exercised.
- Its consumers are split across phases: object lists + room lookups are needed in Phase 2 (Pillar 4 cycle, Pillar 2 transitions); walkmesh edges are needed in Phase 3 (Pillar 1 wall cues). Building the right slice with each consumer keeps each phase focused.

Decision: **`engine_area`'s object-list + room-lookup slice lands at the start of Phase 2; the walkmesh-edge slice lands in Phase 3.** `docs/navsystem-longterm-plan.md` has been updated accordingly.

### Sourcing decision — atmospheric pass over authored cues (2026-05-03)

The plan locks 12 authored WAV cues in `Override/`. We're starting with **existing engine resrefs** instead — curate sounds from `streamsounds\` / `streamwaves\` / BIF wave archives that work atmospherically, fall back to authored cues only if too noisy. Two consequences:

- The `kdev apply` Override/ copy hook is off the Phase 1 critical path (deferred until we ship a custom WAV).
- The 12-WAV authoring lay-off becomes a curation pass — pick existing engine sounds for each of the 12 categories from the locked cross-pillar inventory, document the resref → category mapping.

### Lay-off plan (revised 2026-05-03)

1. **`engine_player.{h,cpp}`** — read player pose + area. *Closed (committed `bb43118`).*
2. **CExoSound singleton xref-trace** — discovery note resolving the OPEN item from investigation Q8. *Closed (singleton at `0x007A39EC`).*
3. **`audio_bus.{h,cpp}`** — 2D + 3D one-shot wrappers around `CExoSound::PlayOneShotSound` / `Play3DOneShotSound`. *Closed.*
4. **Test fixture** with one curated engine resref → in-game verification → **Phase 1 exit gate**. *Closed (gate met 2026-05-03).*
5. **Atmospheric-pass curation** — map existing engine sounds to the 12 cross-pillar audio-vocabulary categories.
6. ~~*(Conditional)* **`audio_listener.{h,cpp}`** — only if step 4 reveals the engine default isn't enough.~~ *Dropped 2026-05-03 — engine default listener proved camera-anchored at the gate (audible pan responding to A/D camera rotation). No override needed for any planned phase. `docs/navsystem-longterm-plan.md` updated.*
7. **`core_settings.{h,cpp}`** — minimal stub returning the plan's locked defaults.

Each lay-off = one session = one commit, per the discipline rule.

### Lay-off log

**Lay-off 1** — `engine_player.{h,cpp}`. *Build verified 2026-05-03; awaiting commit.*

Added to `patches/Accessibility/`:

- `engine_offsets.h` — added file-scope `Vector` struct (3 floats; matches investigation Q1's right-handed Z-up world frame). Centralised here because every future engine_* file (audio_bus 3D position, engine_area walkmesh vertices, engine_listener pose) needs it.
- `engine_player.h` — public surface: `acc::engine::GetPlayerPosition`, `GetPlayerFacing`, `GetPlayerYawDegrees`, `GetPlayerArea`. Plus file-scope addresses (`kAddrAppManagerPtr`, `kAddrGetPlayerCreature`, `kAddrCSWSObjectGetArea`) and offsets (`kClientObjectServerObjectOffset`, `kServerObjectPositionOffset`, `kServerObjectOrientationOffset`). Convention matches `engine_manager.h`.
- `engine_player.cpp` — implementation. One internal `GetPlayerServerObject` helper centralises the chain walk (`*kAddrAppManagerPtr → CClientExoApp → GetPlayerCreature() → server_object @+0xf8`); each public reader pays one SEH frame. `GetPlayerYawDegrees` mirrors `ExecuteCommandGetFacing`'s `atan2(y,x) * 180/π` + `[0,360)` normalization (Q1).

Decision — **direct struct reads over NWScript-equivalent calls**. Investigation Q1 documents both paths (`ExecuteCommandGetPosition @0x53cae0` etc. vs. raw `+0x90` reads). We picked direct reads because (a) we control the call site so the VM-call layer adds no value, (b) `engine_reads.cpp` already uses the same SEH-guarded direct-read pattern, (c) NWScript paths route through the action queue / VM stack with extra latency we don't need.

Decision — **server-side `CSWSObject` layout, not client `CSWCObject`**. Per Q1 the two have independent offsets and the server is authoritative. We lift via `server_object @+0xf8` once and read positions/orientation server-side.

No menu-side consumer yet — `menus.cpp` is unchanged. Phase 2 will be the first consumer.

Build verified: `kdev build` clean (9 .cpp files, DLL exports verified, log `build-20260503-185129.log`).

Discipline: Phase 0's mid-phase lay-off rule applies. No menu-side runtime change in this lay-off (no consumer wired up yet), so no in-game regression test needed; resume after commit for lay-off 2.

Committed `bb43118`.

**Lay-off 2** — CExoSound singleton xref-trace. *Closed 2026-05-03.*

The remaining OPEN item from investigation Q8: "CExoSound singleton's exact global address — all callers go through `someGlobal->PlayOneShotSound`; the global pointer hasn't been labeled in the DB."

**Resolution:** singleton lives at **`0x007A39EC`** in the engine's singleton table (right next to the resource manager at `0x007A39E8`, manager at `0x007A39F4`, app at `0x007A39FC`). Method: SARIF Recipe 4 → 33 direct callers of `Play3DOneShotSound @0x5d5e10`; headless-Ghidra DumpBytes at four sampled callers (`0x57f070`, `0x57f250`, `0x57f377`, `0x5fdada`) all show `8b 0d ec 39 7a 00` (`MOV ECX, [0x007A39EC]`) immediately before `CALL 0x5d5e10`. Four independent direct callers loading from the same absolute address is conclusive.

Bonus: by disassembling `0x5d5e00` itself, confirmed CExoSound facade layout — `CExoSoundInternal* internal` at offset 0, every method null-checks and tail-calls into the internal. Matches investigation's TL;DR.

This was a docs-only lay-off — no patch source touched. The audio_bus implementation in lay-off 3 will reference `0x007A39EC` directly.

`docs/navsystems-investigation.md` Q8 updated: status flipped from OPEN to CONFIRMED, with a new "Singleton resolution" subsection capturing the disassembly evidence and the calling pattern recipe.

Discipline: docs-only, low-risk; safe to chain into lay-off 3 same session if context allows.

Committed `0f309dc`.

**Lay-off 3** — `audio_bus.{h,cpp}`. *Build verified 2026-05-03; awaiting commit.*

Added to `patches/Accessibility/`:

- `audio_bus.h` — public surface: `acc::audio::PlayCue` (2D one-shot) and `PlayCue3D` (positional). Plus file-scope addresses (`kAddrCExoSoundPtr`, `kAddrCExoSoundPlayOneShotSound`, `kAddrCExoSoundPlay3DOneShotSound`). Convention matches `engine_manager.h` / `engine_player.h`.
- `audio_bus.cpp` — implementation. Internal POD `CResRef` (16 chars) populated by `FillResRef` (lowercase + zero-pad). One internal `GetCExoSound` SEH-guards the singleton load; each public API SEH-wraps the engine call. PFN typedefs match the engine signatures decoded in investigation Q8 (Play3DOneShotSound's RET 0x28 and arg layout were re-verified by disassembling 0x5d5e10 with DumpBytes:96 — passes by-value `Vector position`, then `float z_offset`, four byte/dword slots, then `float volume`, `float max_distance`).

Decision — **bake conservative defaults** into `PlayCue` / `PlayCue3D`, expose only the resref + (3D) world position. Priority group 0, no delay, no loop, volume 1.0, pan 0, z_offset 0, max_distance 50.0. Investigation Q8 calls out priority groups as STRONG-but-not-CONFIRMED (group 0 is conservative); volume defaults to 1.0 because the engine already scales by the SFX slider; max_distance 50 metres covers a typical room interior generously without going so wide that distance-encoded cues become useless. We can extend to a fuller-knob variant later if a consumer needs it. YAGNI for now.

Decision — **lowercase + zero-pad** the resref tag in `FillResRef`. The engine resource manager hashes case-insensitively, but defensive lowercasing matches the pattern of every existing engine callsite that constructs a CResRef from a literal. Strings >16 chars are truncated, not rejected — easier to match the engine's own behaviour than to fail a test fixture on a typo.

Decision — **copy `worldPosition` to a local before the SEH frame** in `PlayCue3D`. The engine takes Vector by value (12 bytes pushed onto the stack), so the local is a defensive measure: if the caller's `worldPosition` lives in memory that gets unmapped between our null-check and the engine call (extremely unlikely but cheap to defend against), we still pass valid bytes.

No menu-side consumer yet — `menus.cpp` is unchanged. The test fixture in lay-off 4 will be the first consumer.

Build verified: `kdev build` clean (10 .cpp files, DLL exports verified).

Discipline: pure addition, no behavioural change; safe to chain into lay-off 4 (test fixture) which is the first user-testable lay-off.

Committed `7e63c96`.

**Lay-off 4** — Test fixture + Phase 1 exit gate. *Gate met 2026-05-03.*

Wired a small, throttled (5 s) `acc::audio::PlayCue3D` call into the existing `OnUpdate` handler in `menus.cpp`, anchored on the player's current world position via `acc::engine::GetPlayerPosition` and using the engine resref `n_bith_atk1` (a creature-attack vocalisation in `streamsounds/` — chosen for being loud and unambiguously localisable). Self-gates: when there's no player creature loaded (main menu, chargen pre-spawn, area-load mid-flight), `GetPlayerPosition` returns false and the fixture is silent.

Includes added to `menus.cpp`: `engine_player.h`, `audio_bus.h`. Test stanza is bracketed with `// === TEMPORARY: Phase 1 lay-off 4 audio test fixture ===` markers — easy to remove later.

**Sub-finding (engine_player chain bug).** First in-game run produced silence with no log entries. A diagnostic version of the fixture (logged the four chain values unconditionally) showed `app@0x7A39FC` and `exo@0x7A39EC` both non-null, but `gotPos=0` on every tick — i.e. the singleton lookups worked but `GetPlayerCreature` was returning null. Xref-trace at three callers of `GetPlayerCreature @0x5ed540` (`0x5fba8d`, `0x60541a`, `0x605451`) showed the canonical pattern:

```
8b 0d fc 39 7a 00     MOV ECX, [0x007A39FC]    ; AppManager wrapper
8b 49 04              MOV ECX, [ECX+0x4]       ; → CClientExoApp* (real)
e8 ?? ?? ?? ??        CALL GetPlayerCreature
```

So `*0x7A39FC` is an `AppManager` wrapper holding the actual `CClientExoApp*` at `+0x4` — investigation Q1's "APP_MANAGER_PTR (0x7a39fc) → CClientExoApp instance" implied a single deref that wasn't accurate. Fixed in `engine_player.{h,cpp}` by adding a `kAppManagerClientAppOffset = 0x4` constant and chaining the indirection in `GetPlayerServerObject`. Re-launch then logged `gotPos=1 pos=(15.42, 20.12, -1.27)` correctly (Endar Spire spawn coordinates) and `PlayCue3D -> OK` thereafter.

`docs/navsystems-investigation.md` Q1 updated with the corrected chain and a "Chain correction" note pinning the discovery to this lay-off.

**Gate verification (audible).** User confirmed cue played at the player's position and panned correctly with A/D camera rotation, demonstrating that the engine's default listener is camera-anchored (matches Q8). Distance attenuation wasn't independently verified (an in-game NPC tooltip pinned the player in place — by-design game behaviour, not our bug), but rides on the same Miles 3D pipeline as pan; if pan works, attenuation works.

**Plan simplification (audio_listener obsoleted).** Q8 documented that the engine listener is camera-anchored by default, but the plan kept `audio_listener.{h,cpp}` as a contingency in case we needed to override (e.g. for head-height z-offset). The audible pan at the gate confirms the engine default is sufficient: the listener tracks the camera, which tracks the player. **`audio_listener.{h,cpp}` dropped from Phase 1 (and from any later phase as a planned dependency).** Re-add only if a concrete future need surfaces. `docs/navsystem-longterm-plan.md` Phase 1 line revised accordingly.

**Final fixture trim.** Diagnostic version replaced with the minimal "throttle, try, log on success" form. In menus / pre-spawn the fixture is fully silent (throttle runs unconditionally; `GetPlayerPosition` short-circuits before any other work). Single log line per fire when in-game.

Build verified: `kdev build` clean (10 .cpp files, DLL exports verified). Run-time verified: cue audible, camera-relative pan confirmed.

Discipline: this lay-off bundles the chain fix + fixture wiring + plan simplification because they're all part of the same gate-verification work. Subsequent commit + fresh session for lay-off 5 (atmospheric-pass curation).

**Lay-off 5** — Atmospheric-pass curation. *In progress (started 2026-05-03).*

Goal: pick an existing engine resref for each of the 12 audio-vocabulary slots locked in `docs/navsystem-longterm-plan.md` §"Audio vocabulary inventory". Source pools:

- `build/sounds-extracted/` — 1928 WAVs unpacked from `data/sounds.bif` via `unkeybif` (xoreos-tools 0.0.6, install captured in `docs/tools.md`). Categorical prefixes: `gui_*` (22 — UI vocabulary), `dr_*` (77 — door open/close per material), `fs_*` (67 — footsteps), `as_*` / `cb_*` / `cs_*` / `mgs_*` / `bf_*` / `v_*` (combat / cinematic / force / etc).
- `<install>/streamsounds/` — 1166 loose WAVs, mostly creature combat vocalizations.
- `<install>/streamwaves/` — 570 loose WAVs, mostly NPC speech / cinematics.

User auditions and picks; we record the resref → category mapping here as decisions land. Final mapping becomes the source for the cue-table constants in `audio_bus.{h,cpp}` (or a sibling `audio_cues.h` if the table grows enough to want its own file). Unfilled slots are listed below as "TBD".

Curation decisions (resref names — case-insensitive, engine resolves against the standard search chain so loose WAVs in `Override/` could later replace these):

- **Landmark** — `gui_quest` *(picked 2026-05-03)*. Quest-event timbre carries the "noteworthy named place" weight the slot calls for.
- **Door** — `gui_close` *(picked 2026-05-03; 250 ms)*. The `dr_*` pool was rejected as a source — the swing/slide sounds run 700–1200 ms (3–6× the ~200 ms target); only the three `dr_*_lock` clicks are short enough but they read as locks rather than doors. `gui_close` (250 ms) and `gui_open` (310 ms) are the two short door-evocative UI cues. Picked `gui_close` because it's shorter and **`gui_open` is too sonically similar to be safely re-used in another slot** — `gui_open` therefore stays unallocated.
- **NPC / Creature** — `fs_metal_droid2` *(picked 2026-05-03; 210 ms)*. Droid footstep on metal — distinctive mechanical/metallic timbre. Generic enough to not mis-cue against organic creatures, sonically unmistakable.
- **Container / Placeable** — `gui_invadd` *(picked 2026-05-03; 209 ms; provisional)*. KOTOR has no canonical "open container" cue (per-`.utp` open/close sounds defined per template); literal `*locker*` files in the pool run 3.6–5.2 s, unusable as nav cues. `gui_invadd` ("added to inventory") matches the open-and-loot semantics and lands on the ~200 ms target.
- **Item** — `gui_invselect` *(picked 2026-05-03; 63 ms; provisional)*. Quick "select an inventory item" snap; conceptually distinct from Container's `gui_invadd` (grab vs. loot-add) and from Collision's `gui_invdrop` (pick up vs. drop). Very short — verify it's audible enough at the gate.
- **Transition / Exit** — `mgs_s1` *(picked 2026-05-03; 59 ms)*. Music-game-state stinger; very brief, distinct from all other picks (sole `mgs_*` slot in the table).
- **Wall** — `fs_dirt_hard1` *(picked 2026-05-03; 85 ms; 11025 Hz)*. Hard-dirt footstep — brief scuff timbre reads as "brushed against something solid" without claiming a specific material. Lowest sample rate in the table (engine resamples; not a concern).
- **Hazard / Ledge** — `cb_sw_bldlrg1` *(picked 2026-05-03; 316 ms)*. Combat sound (large blade impact / sword block). Sharp metallic threat-timbre reads as "danger" without being a generic UI bleep. Atmospheric path preferred over `gui_error` (138 ms, functional but UI-flavored) or `gui_minearm` (511 ms, too long).
- **Collision** — `gui_invdrop` *(picked 2026-05-03)*. Drop-thud timbre fits the "cursor hit something solid" semantics.
- **Beacon active** — `gui_actscroll` *(picked 2026-05-03; provisional — flag to re-evaluate volume/maskability under live ambient audio. Concern: this cue repeats every few seconds during guidance, so if it's too quiet it'll be drowned by music/VO/combat; if too loud it'll fatigue. Re-audition during Phase 3 hook test under varied area soundscapes before locking)*.
- **Beacon waypoint reached** — `gui_prompt` *(picked 2026-05-03)*. Attention-getting "noted, advancing" timbre.
- **Beacon destination reached** — `gui_complete` *(picked 2026-05-03)*. Literal-name fit; clear positive resolution.

Sonic-distance check pending live audition: the three beacon cues (`gui_actscroll` repeating + `gui_prompt` per-waypoint + `gui_complete` final) need to be unambiguously distinct from each other when heard in sequence during a path traversal — currently picked from descriptions; verify under Phase 3 hook test.

**Curation pass complete (2026-05-03).** All 12 slots filled. Final mapping summary (resref → slot, sorted by category):

Per-kind cues:
- Door = `gui_close` (250 ms)
- NPC / Creature = `fs_metal_droid2` (210 ms)
- Container / Placeable = `gui_invadd` (209 ms)
- Item = `gui_invselect` (63 ms)
- Landmark = `gui_quest` (TBD ms)
- Transition / Exit = `mgs_s1` (59 ms)
- Wall = `fs_dirt_hard1` (85 ms)
- Hazard / Ledge = `cb_sw_bldlrg1` (316 ms)

Special-purpose cues:
- Collision = `gui_invdrop` (TBD ms)
- Beacon active = `gui_actscroll` (TBD ms — provisional, re-test under live ambient)
- Beacon waypoint reached = `gui_prompt` (TBD ms)
- Beacon destination reached = `gui_complete` (TBD ms)

Picks span four engine-internal sound families: `gui_*` (UI vocabulary, 7 slots), `fs_*` (footsteps, 2 slots), `cb_*` (combat impacts, 1 slot), `mgs_*` (music-game-state, 1 slot). No use of `dr_*` (too long), `streamsounds/*` (MP3-encoded ambient loops, all multi-second), or party `p_*` Force-utterances (too thematically loaded).

**Provisional flags** carried forward to live testing:
- Beacon active needs volume/maskability re-test under varied area soundscapes (the only repeating cue in the set).
- Item (`gui_invselect`, 63 ms) and Transition (`mgs_s1`, 59 ms) are very short — verify audibility over ambient game audio at the gate.
- The three beacon cues need a sequence-audition during Phase 3 to confirm sonic distinctness.

**Wiring (same lay-off 5):** the resref mapping landed as `patches/Accessibility/audio_cues.h` — header-only, `enum class NavCue` (12 plan-stable slots) + `constexpr const char* GetNavCueResref(NavCue)` switch. Single-line edit per cue to swap; engine resource-resolution chain still walks `Override\` first so future custom WAVs shadow the engine asset transparently. Header included from `audio_bus.cpp` for compile-verification only — no runtime consumer in this lay-off; first consumers land in Phase 2 (Pillar 4 cycle) and Phase 3 (Pillar 1 change-driven). Build verified clean (10 .cpp files, header-only addition).

**Lay-off 7** — `core_settings.{h,cpp}`. *Build verified 2026-05-03; awaiting commit.*

Added to `patches/Accessibility/`:

- `core_settings.h` — `acc::core::NavSettings` aggregate built from per-pillar substructs (`Pillar1Settings`, `Pillar2Settings`, `Pillar3Settings`, `Pillar4Settings`, `CrossPillarSettings`). All fields default-initialised to the plan's locked values (per `docs/navsystem-longterm-plan.md` §"Locked defaults"): per-kind cue toggles, trigger toggles, awareness range 5m, distance-delta threshold 0.5m, voice budget 3, octagonal-sector hysteresis 5°, view-mode TTS hover-pause 300ms, distance milestones 200/100/50/20/5m, reached-tolerance 1m, etc.
- `core_settings.cpp` — single accessor `Get()` returning a `static const NavSettings` instance. Phase 7 (deferred) replaces this backing with config-file-loaded mutable state without changing the accessor signature.

Decision — **per-pillar substructs over a flat struct**. Mirrors the plan's locked-defaults section exactly (each pillar has its own bullet list there). Phase 7's user-options UI will need per-pillar grouping anyway. Substruct cost is zero (POD aggregates, no virtual dispatch, all in one cache line per pillar).

Decision — **omit hardcoded design choices from the settings struct**. Things the plan locks as *behaviour* rather than as *user knobs* (bearing frame = world-frame, cycle sort = distance ascending, direction frame = clock-position relative to player facing, spoiler model = engine-state-driven) live in the consumer code as constants, not in `NavSettings`. The struct is for things a future user might toggle/tune, not for everything in the plan.

Decision — **omit movement key swap**. Plan §"Movement model" is explicit: A/D ↔ Q/E ships via KotOR's engine keybind config, not a runtime patch setting. Including it here would create a misleading second source of truth.

No menu-side consumer yet — first read in Phase 2.

Build verified: `kdev build` clean (11 .cpp files, DLL exports verified).

Discipline: tiny addition (one .h + ~10-line .cpp), no behavioural change, no engine indirection. Bundles cleanly with lay-off 5 into the Phase 1 closeout commit per the user's call ("layoff 7 sounds like a quite small step").

### Phase 1 exit summary

All planned lay-offs landed (1, 2, 3, 4, 5, 7; 6 dropped per evidence at lay-off 4). Foundation now in place for Phase 2:

- Player pose readers (`engine_player.{h,cpp}`)
- Audio playback wrappers (`audio_bus.{h,cpp}`)
- 12-slot cue vocabulary mapping (`audio_cues.h`)
- Settings surface (`core_settings.{h,cpp}`)

The Phase 1 exit gate (in-game cue audible at player position with camera-anchored listener) was met at lay-off 4 (`bb43118` predecessor + chain-fix commit). Phase 2 work — `engine_area` object-list slice, Pillar 4 cycle, guidance autowalk, Pillar 2 transition announcements — opens next session.

---

## Phase 0 — Refactor (closed 2026-05-03)

### Goal
Extract `core/` and `engine/` foundations out of the monolithic `Accessibility.cpp`; split menu code into `menus_*.cpp`. Plan-mandated layout decision: **flat with subsystem-prefix filenames** at the patch root (not literal `src/core/` subdirectories) — see plan decision log, dated 2026-05-03.

### Lay-off log

**Lay-off 1** — committed `71b155e` ("Accessibility: Phase 0 (lay-off 1) — extract core_dllmain + engine_input").

Extracted from `Accessibility.cpp`:

- `engine_input.{h,cpp}` — `acc::engine::InputIndexName` + `ManagerTranslateCode` + `kInput*` logical codes (Up/Down/Left/Right/Enter1/Enter2/Esc1/Esc2/Activate)
- `core_dllmain.cpp` — `DllMain`, `OnRulesInit`, `EnsureTolkInitialized`, `kModVersion`, `g_versionSha`

Build verified (`kdev build` clean, 5 .cpp files, all exports verified).

**Lay-off 2** — extract `engine_reads.{h,cpp}` + `engine_offsets.h`.

Extracted from `Accessibility.cpp`:

- `engine_offsets.h` — file-scope `constexpr` constants and engine-data structs:
  - GuiControlMethods vtable indices: `kVtableAsLabel`, `kVtableAsLabelHilight`, `kVtableAsButton`, `kVtableAsButtonToggle`
  - Button/label field offsets: `kButtonTextOffset`, `kButtonStrRefOffset`, `kLabelTextOffset`, `kLabelStrRefOffset`
  - Element-state offsets: `kButtonToggleStateOffset`, `kSliderMaxValueOffset`, `kSliderCurValueOffset`
  - CSWGuiText layout offsets: `kLabelGuiStringPtrOffset`, `kLabelTextObjectOffset`, `kButtonGuiStringPtrOffset`, `kButtonTextObjectOffset`, `kTextObjectTextOffset`, `kTextObjectStrRefOffset`, `kAurGuiStringCStrOffset`
  - Vtable identity addresses: `kVtableCAurGUIStringInternal`, `kVtableSlider`, `kVtableListBox`
  - Container offsets: `kPanelActiveControlOffset`, `kPanelControlsOffset`, `kListBoxControlsOffset`, `kListBoxBitFlagsOffset`, `kListBoxItemsPerPageOffset`, `kListBoxSelectionIndexOffset`, `kListBoxTopVisibleIndexOffset`, `kControlExtentOffset`
  - Engine structs: `CExoArrayList`, `CExoString`, `PFN_GetSimpleString`, `kAddrGetSimpleString`, `kAddrTlkTablePtr`
- `engine_reads.{h,cpp}` — `acc::engine::` namespace functions: `ReadControlNameFields`, `CallDowncast`, `ReadCExoString`, `ReadU32`, `LookupTlk`, `ExtractTextOrStrRef`, `ReadGuiString`, `ExtractTextOrStrRefIndirect`, `IsToggle`, `IsSlider`, `IsListBox`, `ReadToggleState`, `DumpControlVtable`

Convention follows `engine_input.h`: constants at file scope (callsite brevity); functions in `acc::engine::`. `Accessibility.cpp` adds `using namespace acc::engine;` so existing callsites compile unchanged.

Build verified (`kdev build` clean, 6 .cpp files, all exports verified). `Accessibility.cpp` shrank from 3626 → 3212 lines (~414 lines moved out).

Discipline: this is a **mid-phase lay-off**, not Phase 0 exit. Hand-off rule = commit + fresh session for next extraction.

**Lay-off 3** — extract `engine_panels.{h,cpp}`.

Extracted from `Accessibility.cpp`:

- `engine_panels.h` — public API: `PanelKind` enum (file scope, like `engine_input.h`'s codes) + `acc::engine::` functions: `PanelKindName`, `ResolveGuiInGame`, `IdentifyPanel`, `IsPanelKindInGameMenu`.
- `engine_panels.cpp` — internal: App→Client→Internal→GuiInGame address constants, the `kPanelKindOffsets[]` table + `PanelKindOffset` struct, the `g_panelKindCache` + `kPanelKindCacheSize` state. None of these are part of the public surface — adding a panel kind = enum value in the header + table row in the .cpp.

Non-obvious choice: enum value `MessageBox` renamed to `MessageBoxModal` to dodge the Win32 winuser.h `#define MessageBox MessageBoxA` macro. The original Accessibility.cpp worked by accident because `<windows.h>` was included before the enum, so both definition and references consistently expanded to `MessageBoxA`. With the enum now in a header that may be included before/after `<windows.h>` in different TUs, the literal name avoids inconsistency. Comment in `engine_panels.h` documents the rename.

`Accessibility.cpp` adds `#include "engine_panels.h"`; existing `using namespace acc::engine;` covers the new symbols so callsites (`IdentifyPanel`, `PanelKindName`, etc.) compile unchanged. Forward decl of `IsPanelKindInGameMenu` near the top of `Accessibility.cpp` is dropped — header makes it visible up-front.

Build verified (`kdev build` clean, 7 .cpp files, all exports verified). `Accessibility.cpp` shrank from 3212 → 2994 lines (~218 lines moved out).

Discipline: still a mid-phase lay-off. Commit + fresh session for lay-off 4.

**Lay-off 4** — extract `engine_manager.{h,cpp}`.

Extracted from `Accessibility.cpp`:

- `engine_manager.h` — public surface: `kAddrGuiManagerPtr`, `kMgrPanelsDataOffset`/`kMgrPanelsSizeOffset`/`kMgrModalStackDataOffset`/`kMgrModalStackSizeOffset`, `kAddrMoveMouseToPosition` + `PFN_MoveMouseToPosition`, click-sim `kAddrManagerLMouseDown`/`kAddrManagerLMouseUp` + `PFN_ManagerLMouseDown`/`PFN_ManagerLMouseUp`, plus `acc::engine::FindOwningPanel`/`GetForegroundPanel`/`LogManagerStack`.
- `engine_manager.cpp` — function definitions; pulls `engine_offsets.h` for `CExoArrayList` + `kPanelControlsOffset` (`FindOwningPanel` walks each panel's `controls` list).

Convention follows engine_input.h / engine_offsets.h: file-scope constants and PFN typedefs for callsite brevity; functions in `acc::engine::` (covered by `Accessibility.cpp`'s existing `using namespace acc::engine;`).

Left in `Accessibility.cpp` deliberately (out of scope for this lay-off):

- `kAddrPanelSetActiveControl` / `PFN_PanelSetActiveControl` — currently unused (no callsites). Dead code from the pre-click-sim activation path; not part of the engine_manager spec, and the plan-mandated single-topic discipline says no drive-by deletions.
- `kVtableHandleInputEvent` / `PFN_ControlHandleInputEvent` + `FireActivate` — these are control-vtable dispatch primitives, not manager surface. They naturally belong with the menu-side activation logic and stay until the rename to `menus.cpp`.

Build verified (`kdev build` clean, 8 .cpp files, all exports verified). `Accessibility.cpp` shrank from 2994 → 2838 lines (~156 lines moved out).

Discipline: still a mid-phase lay-off. Commit + fresh session for lay-off 5.

**Lay-off 5** — rename `Accessibility.cpp` → `menus.cpp`.

`git mv` only — zero behavior change. Build pipeline globs `*.cpp`, so no manifest/hook updates needed. The file's own header comment was updated to (a) reflect the new layering (mention `engine_panels.{h,cpp}` and `engine_manager.{h,cpp}`) and (b) drop the "will be split further into menus_*.cpp" forward-looking note — per plan, the menu-side logic is NOT decomposed further in Phase 0. Single-file menus.cpp is the steady-state for Phase 0.

Build verified (`kdev build` clean, 8 .cpp files, all exports verified).

Discipline: this finishes the *code-side* lay-offs. Lay-off 6 (menu regression test) is the only remaining gate before Phase 0 can exit; it's user-driven and cannot be performed by the assistant.

**Lay-off 6** — menu regression test. **Passed 2026-05-03.**

User ran `kdev apply` + `kdev launch --monitor` against the build from lay-off 5 and walked the menu paths. Reported result: "everything working as before. no new bugs." This is the Phase 0 exit gate, so Phase 0 is now closed.

(All six lay-offs landed; Phase 0 closed.)

### Current file inventory (`patches/Accessibility/`)

- `manifest.toml` — patch manifest (id, version, supported game hashes)
- `hooks.toml` — detour bindings (6 active hooks, 4 disabled diagnostics)
- `exports.def` — DLL exports (6 functions: OnRulesInit / OnHandleFocusChange / OnHandleInputEvent / OnSetActiveControl / OnListBoxSetActiveControl / OnUpdate)
- `log.{h,cpp}` — file/debug logging primitives (unchanged since pre-plan)
- `tolk.{h,cpp}` — screen reader bridge, lazily loaded (unchanged since pre-plan)
- `core_dllmain.cpp` — DLL entry + Tolk init plumbing *(new in lay-off 1)*
- `engine_input.{h,cpp}` — input code translation *(new in lay-off 1)*
- `engine_offsets.h` — engine struct/vtable offset constants + `CExoString` / `CExoArrayList` *(new in lay-off 2)*
- `engine_reads.{h,cpp}` — SEH-guarded readers + element-class identity helpers *(new in lay-off 2)*
- `engine_panels.{h,cpp}` — `PanelKind` enum + CGuiInGame slot classification (`IdentifyPanel`, `PanelKindName`, `ResolveGuiInGame`, `IsPanelKindInGameMenu`, panel-kind cache) *(new in lay-off 3)*
- `engine_manager.{h,cpp}` — CSWGuiManager surface: singleton lookup, panels[]/modal_stack offsets, MoveMouseToPosition + click-sim PFN typedefs, `FindOwningPanel`/`GetForegroundPanel`/`LogManagerStack` *(new in lay-off 4)*
- `menus.cpp` — menu-accessibility hook handlers (chain navigation, focus events, input dispatch, per-tick monitors). ~2838 lines. Renamed from `Accessibility.cpp` *(in lay-off 5)*. Per plan, NOT decomposed further in Phase 0.

---

## Open bugs / known issues

### Crash: chargen Class screen, c0000409 stack canary

Status: **fixed** in `ReadGuiString` (vtable check on `gui_string` before deref). Verified against the same repro path (`patch-20260503-170800.log`): chain rebuild on `CHARAKTERAUSWAHL` completes cleanly, all 6 vtable=`0x73E658` buttons return empty via the speculative-miss path, user navigates past the panel into the Endar Spire opening dialog without any SEH events. Kept here as the historical record because investigation overturned several earlier hypotheses.

**Repro path (one of two observed):**
- Title screen → Neues Spiel
- Through pre-game movie / first chargen panels until the Class panel (`CHARAKTERAUSWAHL` / `Wähle deine Klasse.`)
- Press Down arrow → crash within ~1 tick

A second symptom — audio stutter when pressing *Schließen* in Options — has been observed but recovery happens (process survives). May be a related but milder manifestation of the same audio-thread stress.

**Crash signature:**
- Exception code `0xc0000409` (`STACK_BUFFER_OVERRUN` / `__fastfail`) — security-cookie failure, *not* a regular access violation
- Faulting thread EIP: our DLL, RVA `0x2c9e` (= `0x10002C9E` at static base)
- Instruction at fault: `mov ecx, [ecx+14h]` — the c_string pointer read inside `CAurGUIStringInternal`
- `ECX = 0xae0f1673` at fault — non-null but garbage

**Call chain (frames in our DLL):**
- Frame [00] `ReadGuiString` (entry `0x10002C30`) — fault at the `[guiString+0x14]` read
- Frame [01] `ExtractTextOrStrRefIndirect` (entry `0x10001EA0`) — caller after `lea eax, [esi-4]` (the `guiStringPtrOffset = cexoOffset - 4` derivation)
- Frame [02] `ExtractAnnounceableText` (return at `0x100014D7`) — at the exit of step 2, the AsButton branch, with the canonical button offsets `0x16C / 0x174 / 0x1BC` pushed before the call

**What the log shows (`patch-20260503-162139.log` tail):**
- Chain rebind on the chargen Class panel (`074BB1C0`) builds 7 entries
- 6 of the 7 navigable entries have vtable `0x73E658`
- `Speculative read miss` events fire repeatedly against these same six controls during chain rebind and again during per-tick monitoring
- Last log line is mid-extraction; no panic / shutdown line follows

**Hypotheses overturned during investigation (kept so future sessions don't relitigate):**
- *"vtable `0x73E658` is an image-only-button override / different class."* Wrong. Lane's Ghidra DB labels `0x73E658` as `CSWGuiButton_vtable` — it's the standard `CSWGuiButton`, the same class as main-menu buttons. Our offsets (`gui_string` ptr at `+0x168`, etc.) are correct.
- *"`bit_flags` and `is_active` reading as garbage means the controls are uninitialized."* Wrong. The same garbage pattern (`is_active=2871141504`, `bit_flags=0xffff000e`, etc.) appears in the *successful* main-menu chain dump for working buttons whose text extracts fine. Those offsets (`+0x44` / `+0x4c`) are not actually `bit_flags` / `is_active` for every button instance — likely aliased / unused fields for some configurations. They're not a liveness signal.
- *"The strlen scan walks into unmapped memory."* Wrong. The fault is at `mov ecx,[ecx+14h]` (the second deref reading `gui_string`'s `c_string`), before the strlen loop runs.

**Actual root cause:**
- The chargen Class buttons reach our chain in a transient state where `[control + 0x168]` (the `gui_string` ptr) is sometimes null / safe and sometimes a non-null garbage pointer — the same controls successfully read empty during the chain rebind and crash on a subsequent monitor tick. The engine appears to mutate that field between our reads (a write-after-read race or partial deinit of the embedded `CSWGuiText`); we don't have clean instrumentation to identify the exact mutation point.

**Fix that landed:**
- `ReadGuiString` now checks `*(uintptr_t*)guiString == 0x00741878` (`CAurGUIStringInternal_vtable`, from Lane's Ghidra DB) before reading the `c_string` at `+0x14`. Garbage values fail the vtable check and we return `false` instead of dereferencing. SEH wrap kept as defense for the rare case where `guiString` itself points at unmapped memory.
- Cost: one extra 4-byte read per `ReadGuiString` call when `guiString` is non-null. No syscall, no `VirtualQuery`.

**Artifacts:**
- Pre-fix log: `<game install>/logs/patch-20260503-162139.log`
- Verification log: `<game install>/logs/patch-20260503-170800.log`
- Crash dump: `C:\Users\fabia\AppData\Local\CrashDumps\swkotor.exe.14140.dmp`
- Disassembly snapshot used for analysis: `build/acc_dll.disasm.txt` (regenerate with `dumpbin /DISASM patches/.../accessibility.dll` if rebuilt — the file is overwritten by each run)

---

## Next session: where to start

Phase 2 is in progress. Lay-off 4 closed in-game 2026-05-04. Lay-offs 5 + 6 partially closed 2026-05-04 — code path lands cleanly, but engine-side player-control-mode dominance prevents the queued/forced action from actually moving the player creature. Functional autowalk is parked pending RE work (see lay-off 6's "Engine-side autowalk blocker" section); permanent diagnostic instrumentation (watchdog + Alt+- ForceMoveToPoint diagnostic) shipped so future investigation across guidance / view-mode / pathfind callers gets the same telemetry.

Lay-off 9 was **scoped 2026-05-04** with the parallel cursor-warp probe; **probe closed 2026-05-04 with in-game data** (Layer A viable, Layer C dropped — see lay-off 9-probe entry).

Next: pick one of —
- **Lay-off 9a — Passive-selection narration loop** *(unblocked).* `LastTarget` watcher in `OnUpdate`: resolve handle → CSWSObject* → name + kind, speak via Tolk, play 3D cue at the object's position. Skip the no-target sentinel. Cycle and watcher stay independent channels. First user-perceptible Layer A milestone.
- **Lay-off 7 — Pillar 2 transitions.** Per-tick `GetRoomAt` delta + room-name announce, plus area-transition hook on `CClientExoApp::AddMoveToModuleMovie @0x5edb50`. Also lands the room-name reading layout decision parked at lay-off 1 (`+0x25c room_names`). Pulls in the room-cluster slice that tightens the lay-off 4 cycle scope.

The two are independent. 9a is more user-visible (ambient narration as the player walks); 7 is more structural (room/area orientation).

**Parked RE item (separate from any specific lay-off):** *the* highest-leverage Phase 2 follow-up — figure out why the player creature ignores raw `AddMoveToPointAction` / `ForceMoveToPoint` calls. See lay-off 6's "Path forward" subsection for the four investigation candidates (`ExecuteCommandActionMoveToPoint` disassembly, control-mode setter search, client-side mirror flag, NWScript fallback). Pickable in any future session; no other lay-off depends on it.

**Lay-off 4 follow-ups parked for later (not blocking lay-off 7):**

- Cycle scope — the area scan is still whole-area, not "current room + LOS extension" per the plan. Distances of 40-80m to doors across the Spire are fine for a stress-test but produce noisy listings in dense areas. Lay-off 7 (Pillar 2 transitions) will land the room-cluster slice and tighten the cycle scope here.
- `last_name` concatenation for creatures (NPC main-cast surname display).
- Camera-relative clock-position option as a `core_settings` knob.

**Lay-off 6 follow-ups parked for later (not blocking lay-off 7):**

- Engine-side autowalk blocker (see above, top-priority RE item).
- Explicit Shift+- toggle-cancel — needs RE of the engine's clear-action / abort-move entry point. Moot until the autowalk-doesn't-move blocker is resolved.
- Arrival-facing polish (secondary point = obj.facing offset).
- Run-vs-walk knob exposed to the consumer.
- Rebind Alt+- diagnostic to a quieter modifier (avoids the Windows menu-activation "ding").

- The chargen Class c0000409 fix and `KPatchManager` LEA-vs-MOV / selective-POPAD ESP bugs (memory-recorded) remain context for future work but are not Phase 2 blockers.
