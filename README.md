# KOTOR 1 Accessibility Mod

A screen-reader accessibility mod for **Star Wars: Knights of the Old Republic 1** (BioWare, 2003) targeting the Steam release. Lets fully blind players play KOTOR 1 with NVDA, JAWS, Narrator, or any other Tolk-supported screen reader.

The mod is written by a blind developer. Every workflow — installing, playing, contributing — is designed to be doable with a screen reader and keyboard alone.

## What works today

Implemented and playable end-to-end:

- **Menus.** Main menu, options, character creation, save / load, in-game menus (inventory, character sheet, journal, map, store, container, equip, level-up, workbench, party selection). Listboxes, sliders, edit boxes, toggles, cycle buttons, radial menu — all keyboard-navigable, all announced.
- **In-world navigation.** Per-area room and area announcements. A wall-distance audio cue layer ("Pillar 1") gives spatial awareness in 3D space. Target cycling with Q / E (engine-native) and a separate map-context cycle on `,` / `.`. Autowalk to any cycled object via `-`; beacon mode via `Ctrl+-`.
- **Combat.** Combat log narration, attack resolutions, queue submenu, PC + opponent stats, Examine, bare 1–7 action keys, action bar.
- **Dialog.** Conversation lines spoken, choice menus navigable.
- **Map.** Engine-rendered doors / landmarks / transitions / quest pins cycle through the same vocabulary as in-world; user-placed markers via Shift+N; fog-of-war respected.
- **Audio cues.** Wall proximity, room transitions, door material cues, footstep suppression when stuck, character-frame audio compensation.

Status of each subsystem is tracked in `docs/known-issues.md`.

## Installing

The end-user installer lives in `installer/KotorAccessibilityInstaller/` (Windows, self-contained single-file EXE).

For now: download the latest release from the releases page, run it, point it at your KOTOR install. The installer bundles the K1 Community Patch (K1CP) by default; opt out if you already have it.

Source-build installation is documented in `CONTRIBUTING.md`.

## Reporting bugs

The installer ships a "Collect logs" button on its post-install screen that zips the most recent patch log + any Windows Error Reporting dump into your Downloads folder. Attach that zip to an issue and describe what you were doing.

If you can reproduce a crash, mention which area you were in (the room/area announce will have said it just before).

## Documentation

- **`README.md`** — this file. What the mod is, install, where to file bugs.
- **`ARCHITECTURE.md`** — how the codebase fits together. Read before opening a PR.
- **`CONTRIBUTING.md`** — dev setup, the inner build loop, conventions, screen-reader testing.
- **`docs/`** — reference material (tool inventory, kdev design, controls survey, known issues, upstream PR backlog, installer notes). Historical investigation docs live in `archiev/`.
- **`CLAUDE.md`** — AI-pair-programming context. Humans can read it too; it captures conventions and the project's mental model.

## Architecture in one paragraph

A dev CLI (`kdev`) builds a C++ patch DLL that's injected into `swkotor.exe` at launch via Lane Dibello's [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager). The DLL hooks the engine's GUI, input, and combat paths, reads game state directly from process memory, and routes announcements through [Prism](https://github.com/ethindp/prism) (a Tolk-compatible screen-reader bridge). Per-area 3D audio cues use the engine's own sound API. See `ARCHITECTURE.md` for the full picture.

## License

Source: to be determined.

Vendored dependencies under `third_party/` carry their own licenses (Prism MPL-2.0; Tolk LGPL; Kotor-Patch-Manager TBD). The game itself, Lane Dibello's reverse-engineered Ghidra database, and BioWare's struct layouts are third-party material not included in this repo.

## Credits

- **Lane Dibello** — Kotor-Patch-Manager, the GoG-derived Ghidra database, and most of the reverse-engineering legwork the mod stands on. See `docs/tools.md` § "Upstream sources" for the full attribution.
- **Prism** (Ethin P.) — Tolk-compatible speech bridge with SAPI fallback.
- **K1 Community Patch** team (KOTORCommunityPatches) — bundled bug-fix layer.
- **xoreos / xoreos-tools** — open-source engine reimplementation; used as a cross-reference for file formats.
- **DeadlyStream community** — modding knowledge base.
