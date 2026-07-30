# llm-docs

Reference material curated for LLM consumption. Progressive-disclosure index — read the entry that matches your current task, not the whole folder.

## Files

- **`game-flow.md`** — lifecycle map (DLL attach → main menu → chargen → world → dialog → combat → menus → save/load → game-over) with the engine signal and hooking module for each phase. Start here when placing or refactoring a hook.
- **`accessibility-map.md`** — pillar map of hook candidates by accessibility goal (dialog, combat, world, UI). Less linear than game-flow.md; complementary.
- **`sarif-cookbook.md`** — jq recipes for querying Lane's local SARIF export (`re/k1_win_gog_swkotor.exe.sarif`). Use when `re/swkotor.exe.h` shows undefined fields or when you need cross-references for an address.
- **`turret-minigame-model.md`** — engine-confirmed model of the swoop/turret minigame subsystem (decompiled). The reference shape for an RE-model doc: input→state→action→effect chain, function addresses, struct offsets, and a "what we got wrong" section. Produced by `kdev re` then curated here. When reconstructing another subsystem, `kdev re "<ClassRegex>" --decompile` and curate the skeleton to a sibling of this file.
- **`swoop-accelpad-hit-model.md`** — engine-confirmed model of **how the swoop bike hits an accelerator pad** (decompiled). The hit is a swept (CCD) sphere-vs-sphere test, combined radius 6.0 (Taris) / 5.0 (Tatooine, Manaan); speed-independent, no timing window — difficulty is purely lateral lane alignment. Includes the boost script (`accelpad.ncs`: +5% speed / +10% accel) and live pad-position spread. Read when tuning the accelpad spatial-audio cue.
- **`code-index/`** — per-file LLM-readable summary for each source file under `patches/Accessibility/` (one `.md` per `.cpp`/`.h` plus `_files.txt` inventory), plus subfolders `kdev/` (tools/kdev C# sources) and `installer/` (KotorAccessibilityInstaller C# sources). Useful as fast context before reading a file in full. Fully regenerated 2026-07-27 (pre-K2 refactoring Phase 0); may drift if not refreshed on major restructuring.
- **`interaction-dispatch-model.md`** — how player/leader walk + verb dispatch (use/talk/take) flow through the engine; the message-bus, ActionManager priming, and the native walk-then-talk model.
- **`tutorial-popup-mechanics.md`** — how the mod drives the engine's own tutorial popup with custom text on demand (Surface 1 silent-popup speech substitution + Surface 2 Trask post-VO custom popups): `ShowTutorialWindow` direct mount, the once-shown bitfield (+0xba8), `SetTutorialReason`, the safe/unsafe `SetMessage` overloads, pause via `SetPauseState`, the reply-break trigger, and the FIVE UI-announce paths that must all be gated. Read before touching tutorial popups or any modal-popup speech.
- **`mine-trap-model.md`** — engine-confirmed mine/trap model: Awareness-based periodic detection (UpdateMineCheck), per-trap detected-by id lists (triggers/doors/placeables), the 0x13 detection broadcast, and the client's red-polygon render for detected hostile mines. Read before building trap warnings or anything touching trigger visibility.
- **`persistence-scriptvartable.md`** — engine-confirmed model for persisting named per-object variables into the `.sav` via `CSWSScriptVarTable` (player creature **+0x100**; Ghidra mislabels the +0x110 field, which is actually a `CSWVarTable`). The reusable in-save storage primitive (no sidecar file, no save hook). Distinguishes the named string-capable table from the fixed NWScript `CSWVarTable`. Read when building any save-persistent mod state (discovery index, custom flags). Includes the object-identity model and the verified write recipe.

### Subsystem RE reference (migrated from the agent memory store, 2026-06-14)
Each is a consolidation of former memory notes (offsets, addresses, decompiled mechanics). Read the one matching your subsystem; they replace the always-on memory index for engine RE. See `feedback_memory_vs_docs_discipline` in memory for the rule (RE → here, not memory).
- **`engine-objects-and-architecture.md`** — client/server (CSWS/CSWC) split, object-handle namespaces, party-table indirection, creature/HP/name accessors, player-control toggle, AI action queue, effect-icon row (sighted buff/debuff parity), Star Forge placeable state (local boolean 19).
- **`gui-and-input-internals.md`** — CSWGui* struct offsets, gui_string text indirection, panel/foreground routing, cursor + hit-test surfaces, listbox model, in-DLL input pipeline + menu chain.
- **`ingame-screens-reference.md`** — per-screen surfaces: workbench, map, options sub-screens, save/load, party-select, galaxy map, abilities, charsheet, placeables, level-up.
- **`action-menu-and-combat.md`** — radial/personal action surfaces, the engine action picker, Q/E targeting + ShowObject focus signal, bare-key dispatch, combat-log funnel.
- **`walk-nav-and-walkmesh.md`** — leader-walk recipe, per-area nav-graph layout, dialog-speaker resolution, WallTopo walkmesh clustering.
- **`audio-internals.md`** — Play3DOneShotSound gain chain, CExoSoundSource lifecycle, sound-mode pause exemptions, footstep paths, cue/party filtering, droid subtitles.
- **`camera-and-swoop.md`** — camera screen-edge turn, A/D-vs-W/S decoupling, mouse-look gating, swoop accelerator-pad classification.

## Source-file naming conventions (patch side)

Established during the Phase-1 structure pass (2026-07-28). These prefixes
carry meaning — trust them when deciding whether code is safe to remove.

- **`probe_*`** — throwaway reverse-engineering tooling. One-shot or
  hotkey-triggered dumps that log engine state and change nothing. Safe to
  treat as disposable once the question they answered is settled.
  **Caveat:** the file name is not the namespace. `probe_priority_groups.cpp`
  lives in `acc::probe::priority_groups`, so a grep for
  `probe_priority_groups::` finds nothing even though `core_tick.cpp`
  calls it every tick. Always grep the *namespace* before concluding a
  probe is dead — a Phase-1 candidate to delete this file was approved on
  exactly that false negative and had to be reverted.
- **`diag_*`** — diagnostic-only, no shipped behaviour. Currently
  `diag_settings` (startup environment dump for support logs) and
  `diag_chargen_feats` (on-demand panel dump).
- **`*_guard`** — a shipped production fix that also logs. The logging is
  not the point and must not be removed as "diagnostics":
  `focus_guard.cpp` (DirectInput reacquire / foreground reclaim / Big
  Picture keystroke warning — the keyboard-death fix) and
  `camera_spin_guard.cpp` (edge-turn spin guard). Both were named
  `diag_*` until Phase 1 because they started as investigations and kept
  their investigation-era names after becoming fixes.
- **`minigame_*`** — the minigame family: `minigame_turret`,
  `minigame_swoop_race`, `minigame_swoop_audio`, `minigame_pazaak`, plus
  `minigame_aim` for the primitives shared across them.
- **`menus_*`** — everything menu-side, including `menus_speak`.
- **`wall_probe` vs `room_topology`** — two systems that used to share the
  name `wall_topology`. `wall_probe` answers "is there a wall, how far"
  (ray casts against the perimeter-wall cache, feeds wall sounds).
  `room_topology` builds the nav-graph clusters and produces the
  perceptual-region vocabulary (Korridor / Kreuzung / Bereich).

## KOTOR 2 portability — what will and will not carry over

Recorded during the Phase-2 K2-portability pass (2026-07-29). K2 on Steam
is an Aspyr recompile eleven years after K1, not a relocated K1 build:
`kdev sigscan` scores 0/213 against it, and struct offsets — not
addresses — dominate the port cost. See `docs/kotor2-port-feasibility.md`.

**Will not port — K1 story content.** These encode specific K1 areas,
quests or scripted moments and have no K2 counterpart:
`floor_puzzle`, `spectator_scene`, `endar_softlock`, `tutorial_hints`,
`map_shipped_hints`.

**Minigames — per-game, not a family.** Do not assume the minigame family
ports as a unit:
- `minigame_turret` — **not present in KOTOR 2** (dev's determination,
  2026-07-29). Treat the whole module as K1-only.
- `minigame_swoop_race` / `minigame_swoop_audio` — swoop racing exists in
  K2 but the tracks, pads and tuning differ; expect re-tuning, not a
  straight port.
- `minigame_pazaak` — exists in K2, rules and UI differ.
- `minigame_aim` — the shared primitives. Engine-shaped
  (CSWMiniPlayer.offset), so it ports as far as the struct layout does.

**Ports for free — no engine dependency.** `strfmt.h`, `announce_degrees`,
the `strings*` localisation tables, and the debounce/geometry/formatting
logic generally.

**The address seam.** `acc::addr::R()` (engine_rebase.h) covers **.text
only** — .data global pointers are byte-stable across the builds it
targets and are deliberately left raw (`kAddrAppManagerPtr`,
`kAddrGuiManagerPtr`, `kAddrCExoSoundPtr`). R() is a *same-game*
build-variant seam, not a cross-game one; it does nothing for K2.

**The real K2 seam is upstream.** KPatchManager ships
`AddressDatabases/*.db` keyed by executable SHA-256, with global pointers,
functions and class-member offsets queried by NAME
(`GameVersion::GetGlobalPointer` / `GetFunctionAddress` / `GetOffset`).
`kotor1_0_3.db` holds 9710 functions and 4720 offsets;
`kotor2_steam_aspyr.db` is seeded with 48 functions, 21 offsets and all 14
global pointers under the same names as K1. Our patch uses none of it yet.
Decision 2026-07-29: adopt it. New engine-address code should be written
so the constant can later become a named lookup.

**The object-graph seam is now ours** (Phase-3 B1, 2026-07-30). The walk
from the AppManager singleton down to the client app, server app, client
module and camera used to be hand-rolled at ~40 sites across ~25 files,
with six different names for the `+0x8` hop alone and per-site SEH guards
that some callers had and others did not. It now lives in
`patches/Accessibility/engine_app.{h,cpp}`: five constants, seven guarded
primitives, one dereference of `kAddrAppManagerPtr` in the whole codebase.
The GUI continuation (`ResolveGuiInGame`, `ResolveMainInterface`) sits in
`engine_panels`. For a K2 port these are the files whose VALUES change;
every consumer above them is engine-version agnostic. Keep it that way —
if you find yourself writing `*reinterpret_cast<void**>(kAddrAppManagerPtr)`
again, use the primitive instead.

## `re/` — reverse-engineering assets

- **`swkotor.exe.h`** — Ghidra-exported C header, ~25k lines. Primary source for struct layouts; ~205 structs have real bodies, ~797 are `PlaceHolder`. Cross-check with SARIF when a struct returns garbage.
- **`k1_win_gog_swkotor.exe.xml`** — Lane's full Ghidra XML export. Function names, addresses, comments.
- **`k1_win_gog_swkotor.exe.sarif`** — Lane's full SARIF (~490 MB). Query via jq per `sarif-cookbook.md`.
- **`KotOR_1_System_Layout-2.pdf`** — community RE reference.

## Deferred / nice-to-have

These were identified as future-useful but not built (would require write-up effort without immediate refactoring payoff):

- **File-format reference** (GFF / 2DA / TLK / ERF binary layouts) — pending content-mod work; defer until needed.
- **CSWGuiDialog full struct dump** — current `engine_offsets.h` covers the touched fields; SARIF query to add speaker/cinematic/node-index when a dialog-side feature requires them.
