# Navigation System — Implementation Progress

> **Document role:** execution state, not design. The design lives in `docs/navsystem-longterm-plan.md` and changes rarely; this file changes every commit and tracks *where we are in building the plan*.
>
> **Update rule:** every commit / lay-off point / new bug discovery → update the relevant section of this file as part of the same commit. A fresh session should be able to read this file alone and know exactly what to do next.

---

## Phase status

- **Phase 0 — Refactor.** *Complete (2026-05-03).* All six lay-offs landed: `core_dllmain` + `engine_input`, `engine_offsets` + `engine_reads`, `engine_panels`, `engine_manager`, rename to `menus.cpp`, and the user-driven menu regression test ("everything working as before. no new bugs.").
- **Phase 1 — Foundation.** *Complete (2026-05-03).* All planned lay-offs landed: `engine_player` (1), CExoSound singleton trace (2), `audio_bus` (3), test fixture + exit gate (4), atmospheric-pass curation + `audio_cues.h` wiring (5), `core_settings` stub (7). Lay-off 6 (`audio_listener`) was dropped at lay-off 4 — engine default listener proved camera-anchored at the gate.
- **Phase 2 — Playable baseline.** *In progress (started 2026-05-03).* Lay-offs 1 + 2 + 3 landed in one session: `engine_area.{h,cpp}` foundation (object iterator + kind/position reads + GetRoom wrapper); `filter_objects.{h,cpp}` + `cycle_state.{h,cpp}` (Pillar 4 six-category filter + sorted-by-distance listing + focus state singleton); `cycle_input.{h,cpp}` wired into `OnHandleInputEvent` (`,/.` cycle items, `Shift+,/.` cycle categories, `-`/`Shift+-` log-only stubs). First in-game test pending (verifies key-code assumptions for comma/period/minus). Lay-offs 4-8 pending — see Phase 2 section below.
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

1. **`engine_area.{h,cpp}`** — object iterator + kind/position reads + `GetRoom` wrapper. Foundation; no menu-side consumer wired up. *Closed (this lay-off).*
2. **Pillar 4 filter + cycle state** — `filter_objects.{h,cpp}` (six-category filter over `AreaObjectIterator`) + `cycle_state.{h,cpp}` (current category + focused object + per-tick rebuild). No input wiring yet; pure data layer testable from the per-tick monitor.
3. **Pillar 4 cycle keys** — `,` / `.` / `Shift+,` / `Shift+.` wired into `OnHandleInputEvent`. Mutates cycle_state; no announcement yet (focus changes go silent until lay-off 4).
4. **Pillar 4 announce** — `-` keypress speaks "name + direction (clock position) + distance (m)" via Tolk. First user-perceptible Phase 2 milestone. Per-type name resolution (Door, NPC, Container, Item, Landmark, Transition) lands here.
5. **`guidance/autowalk.{h,cpp}`** — `AddMoveToPointAction` wrapper. Cross-cutting subsystem callable with a `Vector` destination.
6. **Pillar 4 → guidance binding** — `Shift+-` pathfind to currently-focused object via guidance/autowalk. With autowalk-only (beacon comes in Phase 5).
7. **Pillar 2 transitions** — room transition (per-tick `GetRoomAt` delta, speak room name) + area transition (hook `CClientExoApp::AddMoveToModuleMovie @0x5edb50` for "loading: …", then on area-enter speak the new area name).
8. **Phase 2 exit gate** — solo playthrough of one area confirms cycle-and-autowalk loop. Phase 1 audio test fixture removed once a real Phase 2 consumer (lay-off 4 or 7) demonstrates 3D audio in production code.

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

Phase 2 is in progress. Lay-offs 1 + 2 + 3 closed in one session; next session opens with **a brief in-game key-code verification**, then picks up **Phase 2 lay-off 4 — Pillar 4 announce on `-`**.

**In-game verification (start here).** Launch into a save with a player loaded; press `,` `.` `Shift+,` `Shift+.` `-` `Shift+-` while in the world. Confirm the existing per-event `HandleInputEvent` log lines show `key=KEYBOARD_COMMA(103)` / `KEYBOARD_PERIOD(104)` / `KEYBOARD_MINUS(94)` / `KEYBOARD_LEFTSHIFT(24)` etc., and that `Cycle:` log lines fire in response with sensible `category=` + `obj=` + `dist=` values. If any code differs from the working assumption, patch the constant in `engine_input.h` and rebuild. Should be a 5-minute verification.

**Lay-off 4 — Pillar 4 announce.** Plan: `docs/navsystem-longterm-plan.md` §"Per-item announcement" locks the payload as **name + direction (clock-position relative to player facing) + distance (metres)**. Three pieces of work:

1. **Per-type name resolution.** Each game-object subclass has its localised name at a different offset (per investigation Q5): CSWSDoor.loc_name +0x39c, CSWSCreature.CreatureStats.first_name +0x14 / last_name +0x1c, CSWSPlaceable.loc_name +0x228, CSWSItem.localized_name +0x280, CSWSWaypoint.localized_name +0x238, CSWSTrigger.localized_name +0x228. Each is a CExoString or CExoLocString → use the existing `ReadCExoString` / `LookupTlk` chain from `engine_reads.h`. Add to `engine_area.{h,cpp}` (object-type-aware name read) or split into `engine_object_names.{h,cpp}` if it grows.
2. **Direction frame: clock-position.** `atan2(dy, dx)` of (object - player) in player's facing frame. Player yaw via `GetPlayerYawDegrees`. 12 sectors of 30° each, 12 = directly ahead, 6 = directly behind, 3 = right, 9 = left. Cheap modular-arithmetic.
3. **Sub-state filter tightening (lay-off 2 TODOs).** Container needs `usable=true OR has_inventory=true` at CSWSPlaceable +0x328 / +0x334; Landmark needs `has_map_note=true` at CSWSWaypoint +0x228; Transition needs `transition_destination` set at CSWSTrigger +0x30c. Folded in here because each offset needs a hearable verification, same trip.

Wire the announce path: replace the `-` log-only stub in `cycle_input.cpp` with `tolk::Speak(formattedString, /*interrupt=*/true)`. Format: `"{name}, {clock} o'clock, {distance:.0f} metres"` — refine wording with the user once it's audible.

- The chargen Class c0000409 fix and `KPatchManager` LEA-vs-MOV / selective-POPAD ESP bugs (memory-recorded) remain context for future work but are not Phase 2 blockers.
