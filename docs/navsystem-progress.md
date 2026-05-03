# Navigation System — Implementation Progress

> **Document role:** execution state, not design. The design lives in `docs/navsystem-longterm-plan.md` and changes rarely; this file changes every commit and tracks *where we are in building the plan*.
>
> **Update rule:** every commit / lay-off point / new bug discovery → update the relevant section of this file as part of the same commit. A fresh session should be able to read this file alone and know exactly what to do next.

---

## Phase status

- **Phase 0 — Refactor.** *Complete (2026-05-03).* All six lay-offs landed: `core_dllmain` + `engine_input`, `engine_offsets` + `engine_reads`, `engine_panels`, `engine_manager`, rename to `menus.cpp`, and the user-driven menu regression test ("everything working as before. no new bugs.").
- **Phase 1 — Foundation.** *Complete (2026-05-03).* All planned lay-offs landed: `engine_player` (1), CExoSound singleton trace (2), `audio_bus` (3), test fixture + exit gate (4), atmospheric-pass curation + `audio_cues.h` wiring (5), `core_settings` stub (7). Lay-off 6 (`audio_listener`) was dropped at lay-off 4 — engine default listener proved camera-anchored at the gate.
- **Phase 2 — Playable baseline.** Pending.
- **Phase 3 — Pillar 1.** Pending.
- **Phase 4 — Pillar 2 polish + view mode.** Pending.
- **Phase 5 — Pillar 3 polish.** Pending.
- **Phase 6 — Map markers & nice extras.** Pending.
- **Phase 7 — User options UI.** Deferred per plan.

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

Phase 0 is closed. Next session opens **Phase 1 — Foundation**.

- Read `docs/navsystem-longterm-plan.md` for the Phase 1 scope and exit criteria. The plan lists Phase 1 as the foundation work that the playable-baseline (Phase 2) and pillar phases (3-5) build on.
- Per plan, the engine layer (`engine_input.{h,cpp}`, `engine_offsets.h`, `engine_reads.{h,cpp}`, `engine_panels.{h,cpp}`, `engine_manager.{h,cpp}`) is the surface Phase 1 features extend; menu-side state lives in `menus.cpp`. New Phase 1 modules should follow the same flat-with-prefix layout convention.
- Add a "Phase 1 — Foundation" section to this file (mirroring the Phase 0 lay-off log structure) when Phase 1 work begins, so execution state stays trackable.
- The chargen Class c0000409 fix (pre-Phase-0) and `KPatchManager` LEA-vs-MOV / selective-POPAD ESP bugs (memory-recorded) remain context for future work but are not Phase 1 blockers.
