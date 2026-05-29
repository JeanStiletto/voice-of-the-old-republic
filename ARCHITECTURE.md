# Architecture

The mental model of the Voice of the Old Republic mod. Read this before making non-trivial changes.

## Layered overview

From the outside in:

```
                    +-------------------------------+
   end user  --->   |  installer (WinForms .NET)    |
                    +-------------------------------+
                              |
                              v
                    +-------------------------------+
                    |  swkotor.exe install layout   |
                    |   + KPatchLauncher.exe        |
                    |   + KotorPatcher.dll          |
                    |   + patches/Accessibility/    |
                    |   + Prism.dll, Tolk.dll, ...  |
                    +-------------------------------+
                              |
                  KPatchLauncher injects KotorPatcher.dll
                  KotorPatcher loads our patches[].dll
                              v
                    +-------------------------------+
                    |  in-process: swkotor.exe      |
                    |                               |
                    |   our patch DLL (this repo)   |
                    |    + hooks into engine code   |
                    |    + reads game state         |
                    |    + speaks via Prism         |
                    |    + plays 3D audio cues      |
                    +-------------------------------+
```

The dev CLI (`kdev`) sits beside this, automating the build / install / launch loop.

## Pieces in this repo

### `patches/Accessibility/` — the patch DLL

Native C++ (Visual Studio Build Tools 2022, x86). Compiled by `create-patch.bat` from the upstream Kotor-Patch-Manager checkout into a single `.kpatch` archive.

Source files are organised by **role**, not by GUI screen. File-name prefixes group related modules:

- **`core_*`** — DllMain, per-tick dispatch, runtime settings. Loader-lock-safe; no DLL loads or COM init in `core_dllmain.cpp`.
- **`engine_*`** — pure read-side bindings to engine memory. Struct offsets, function-pointer wrappers, vtable downcasts. Mostly stateless; the project's "header file for the engine".
- **`audio_*`** — audio bus, 3D cue player, looping sources, footstep suppression, pitch shifting. Calls the engine's own `Play3DOneShotSound` / `CExoSoundSource` APIs.
- **`menus_*`** — per-screen GUI accessibility (chargen, character sheet, journal, store, equip, save / load, level-up, workbench, party selection). All share the listbox / chain helpers in `menus_listbox.cpp`, `menus_chain.cpp`, `menus_extract.cpp`.
- **`combat_*`** — the combat-pillar implementation (log narration, queue submenu, attack resolution, examine, action bar).
- **`guidance_*`** — Pillar 3 navigation helpers: autowalk (`UseObject` dispatch), beacon mode (A* over the engine's nav graph), description generation.
- **`map_*`** — Pillar 4 map-context cycle, fog-of-war gating, user-placed pins.
- **`announce_*`, `passive_narrate`, `camera_*`, `narrated_target`, `peek_description`, `examine_view`** — announcement pipelines and the "what is the player focused on" model.
- **`hotkeys`, `interact_hotkey`, `view_mode`, `msg_router`** — input layer: Enter / Shift+Enter / hotkey table / view mode toggle.
- **`cycle_*`** — Q / E / `,` / `.` target cycling. Two parallel state singletons (world cycle, map cycle); a `CycleContext` routes input to whichever is active.
- **`spatial_change_detector`, `wall_topology`, `transitions`** — Pillar 1 (per-sector wall distance cues) and Pillar 2 (room / area announces). Reads area walkmesh + nav graph.
- **`radial_menu`, `target_action_menu`, `actionbar_menu`** — action menus (the right-click radial, plus the player's per-portrait action bar).
- **`prism`, `log`, `strings`, `strings_de.cpp`, `strings_en.cpp`** — infrastructure. Prism is the speech bridge; strings are localised per-id.
- **`diag_*`, `probe_*`** — diagnostic / research code. Logging only, no behaviour. Kept around because re-running a probe is cheaper than re-deriving it.

`hooks.toml` declares the engine functions we detour. Most hooks are mid-function with register-source args — the upstream framework's stack-source path has a known LEA-vs-MOV bug (see `docs/upstream-prs.md`).

### `tools/kdev/` — the dev CLI

.NET 10 (`net10.0-windows`, x86). Wraps Lane Dibello's `KPatchCore` library and `create-patch.bat` build script. Commands live under `Commands/`:

- `kdev status` — sanity-check the project + game install.
- `kdev build` — stage source into the layout `create-patch.bat` expects, run the bat under MSVC, drop the `.kpatch` in `build/`.
- `kdev apply` — install the patch into the configured Steam install via `KPatchCore.PatchApplicator`.
- `kdev launch [--monitor]` — launch the game with the patch active.
- `kdev dev` — clean + build + apply + launch in one shot (the inner loop).
- `kdev kill` — terminate the running game.
- `kdev logs [--follow]` — tail the patch log.
- `kdev analyze-dump <wer.dmp>` — extract crash context from a Windows Error Reporting dump.
- `kdev walkmesh-*` — offline analyses of the game's `.wok` walkmesh files (used during nav-system design).

### `installer/KotorAccessibilityInstaller/` — end-user installer

.NET 8 WinForms, self-contained single-file EXE. References `KPatchCore` directly. Bundles K1CP (via HoloPatcher), Widescreen, our `.kpatch`, and Prism + Tolk binaries. Modelled on the arena installer at `C:\Users\fabia\Dev\arena\installer\AccessibleArenaInstaller/`.

`release.ps1` is the local release pipeline (build → publish installer → tag → gh release).

### `third_party/`

Read-only mirrors / vendored binaries:

- `Kotor-Patch-Manager/` — Lane Dibello's hook framework. We patch local copies of upstream bugs here (see `docs/framework-changes-backup.patch`).
- `KotorMessageInjector/` — out-of-process attach / inject library. Demoted to research tool; not in the runtime path.
- `prism-dist/` — Prism x86 binaries + MPL-2.0 license.
- `tolk/` — Tolk + NVDA controller-client x86 binaries + LGPL.

## Key design decisions

### Hook vs. poll

The default is **polling** from a per-tick dispatcher (`core_tick.cpp`). Hooks are reserved for paths where we need to consume input, mutate engine state, or catch a one-shot event with no alternative observation path (combat log line, focus change, area load).

Rule of thumb captured in memory entry `feedback_hook_vs_poll_principle.md`: hooks for control flow, polling for observation.

### Read engine state directly; never mirror it

Struct offsets are in `engine_offsets.h`; vtable indices and downcast helpers in `engine_panels.cpp` / `engine_reads.cpp`. If you need a field the engine renders, find the offset and read it — do not maintain a parallel cache in patch state. Mirrors get out of sync; offsets do not.

### Localisation — language-agnostic by construction

Handler code never sees a localised literal. User-facing strings are id-keyed in `strings.h`; per-locale tables (`strings_en.cpp`, `strings_de.cpp`, ...) supply the actual text. Handlers call `strings::Get(Id)` and don't know or care which locale is loaded. Adding a new locale is a new table file — no handler change. Log lines stay English so the patch log is a single language for debugging.

### Speech priority

Most speech is queued (`prism::Speak(text)`). Urgent cues (compass-cardinal turn cue, map-cursor region cue) route through `prism::SpeakUrgent` with the SAPI backend — NVDA's typed-character cancel ignores priority, so SAPI is the only escape hatch.

### Audio bus

3D cues go through `audio_bus.cpp` → engine's `Play3DOneShotSound`. The engine's listener follows the camera, so for character-relative cues we shift source positions by `(camera − character)`. Looping sources (e.g. swoop accelerator-pad pings) use `audio_loop.cpp` over `CExoSoundSource` directly.

### Memory as design notes

The author keeps decisions, gotchas, and reverse-engineering findings in `~/.claude/projects/.../memory/` (referenced by `MEMORY.md` index). When the code does something surprising, there's usually a memory entry explaining why. The corresponding mental model for an outside contributor is: read the relevant header comment; if it points to a `project_*` memory entry, ask for it.

### Don't reach for upstream fixes silently

If you need the patch framework to behave differently, document the design need in `docs/upstream-prs.md` first, then either work around it locally or patch the vendored copy in `third_party/Kotor-Patch-Manager/`. Local edits are tracked in `docs/framework-changes-backup.patch`.

## Pillars (conceptual)

The accessibility model the in-world part of the mod is built on:

- **Pillar 1 — Spatial awareness.** Continuous low-volume audio cues for nearby walls (per-cardinal-sector) and notable objects. Implemented in `spatial_change_detector.cpp` + `wall_topology.cpp`.
- **Pillar 2 — Medium-scale navigation.** Room and area announcements on transitions; pre-load destination announce. Implemented in `transitions.cpp`.
- **Pillar 3 — Target cycle + autowalk.** Q / E cycles in-world targets via the engine's `SelectNearestObject`; `-` autowalks to the cycled target via `UseObject`; `Ctrl+-` runs A* beacon mode. `,` / `.` cycle a parallel object list; `Shift+,` / `Shift+.` are sub-cycles within a category.
- **Pillar 4 — Map cycle.** Same cycle vocabulary as Pillar 3, but the data source is the engine's area map (doors / landmarks / transitions / quest pins). Fog-of-war gated. User-placed pins added via Shift+N.

## What this codebase is not

- Not a content mod. It does not edit `.dlg`, `.utc`, `.uti`, `.2da`, `.nss`, or `dialog.tlk`. All access goes through reading game state and inserting accessibility-only signals.
- Not a runtime-options or configuration UI. Mod-side settings exist (`core_settings.h` + `menus_modsettings.cpp`) but are minimal and not user-facing yet (Phase 7 planned, deferred).
- Not a cross-engine port. We target the original Odyssey binary specifically. Open-source rebuilds (reone, KotOR.js) are tracked as long-horizon options; the design knowledge is portable but the code is not.
