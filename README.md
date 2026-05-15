# KOTOR Blind Accessibility

A screen-reader accessibility mod for **Star Wars: Knights of the Old Republic 1** (BioWare, 2003) targeting the Steam release. Aims to enable fully blind players to navigate menus, hear focused UI elements, dialog, combat events, and game state via NVDA / JAWS / Narrator.

**Status:** Early development. Focus-change announcements work for the main menu and panel-based screens (options, character creation buttons). Many control types and in-game systems (dialog, combat, inventory, listboxes, sliders) are not yet covered.

## What works today

- Process-injected C++ patch DLL hooks the engine and reads control state from memory directly (no polling).
- [Prism](https://github.com/ethindp/prism) (MPL-2.0, dynamically loaded) bridges to NVDA / JAWS / Window-Eyes / ZDSR / System Access / OneCore / SAPI for normal speech; urgent map-cursor / region-cursor cues route through Prism's SAPI backend specifically so NVDA's typed-character cancel does not silence them.
- Startup announcement: `"KOTOR accessibility mod loaded, version X"`.
- Per-focus-change announcement: extracts the focused control's text via vtable downcast (`CSWGuiPanel::SetActiveControl` hook → `vtable->AsButton()` / `AsLabel()` → read CExoString at the subclass-specific offset). German Steam build verified — produces real localized labels ("Neues Spiel", "Optionen", "Gameplay", etc.).

## Architecture

```
patches/Accessibility/
├── Accessibility.cpp     DllMain + per-hook OnXxx handlers
├── log.{h,cpp}           file/debug logging primitives
├── tolk.{h,cpp}          speech bridge (Prism, LoadLibrary, lazy init —
│                         namespace name is a historical hold-over from
│                         the pre-migration Tolk path)
├── hooks.toml            Kotor-Patch-Manager hook declarations
├── exports.def           DLL exports table
└── manifest.toml         patch metadata + supported game SHAs

third_party/prism-dist/   vendored Prism x86 binaries (prism.dll +
                          headers) + MPL-2.0 license
```

Hooks land mid-function and capture `this` / args from registers (the upstream framework's stack-source path has a known LEA-vs-MOV bug). Prism is initialized lazily on first hook fire (not in DllMain — Prism loads COM and screen-reader driver DLLs, both unsafe under the loader lock).

## Build dependencies (not in this repo)

- `kdev` — internal CLI that drives clean → build → apply → launch. Wraps Visual Studio Build Tools 2022 (x86), Lane Dibello's Kotor-Patch-Manager release, and the game install.
- Lane Dibello's [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) — the in-process hook framework + launcher.
- [Prism](https://github.com/ethindp/prism) prebuilt x86 binary (vendored under `third_party/prism-dist/`).
- Lane Dibello's reverse-engineered Ghidra database (linked from the [DeadlyStream RE thread](https://deadlystream.com/topic/11948-kotor-1-gog-reverse-engineering/)). Required for finding hook addresses + struct offsets.

## Status of major hook surfaces

| Surface | Status |
|---|---|
| Main menu navigation | ✅ button labels announced |
| Options menu navigation | ✅ button labels announced |
| Character creation (some controls) | ⚠️ partial — buttons work; listbox/slider/edit-box don't |
| In-game GUI (inventory, char sheet, journal) | ❌ not yet exercised |
| Dialog text / VO subtitles | ❌ no hook |
| Combat events / floating text | ❌ no hook |
| Game state changes (HP, level, area) | ❌ no hook |

## License

This project's source is to be determined.

Prism (vendored under `third_party/prism-dist/`) is MPL-2.0 — see `third_party/prism-dist/LICENSE` (or the upstream tree under `third_party/prism/`).

The game itself, BioWare's reverse-engineered structures, and Lane Dibello's Ghidra database are all third-party material with their own licensing constraints — not included in this repo.
