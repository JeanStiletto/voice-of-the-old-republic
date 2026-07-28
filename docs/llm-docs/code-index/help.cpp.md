# help.cpp (442 lines)

Implements the F1 keybind-list overlay and Ctrl+F1 context-help speech. Both
surfaces read one static catalog (`kEntries`) tagged with a `grp` (F1 section)
and a `ctx` bitmask (screens Ctrl+F1 speaks it on). F1 is a synthetic in-DLL
listbox driven by `PollWin32` via the hotkey registry (no engine panel);
Ctrl+F1 detects the current screen (`DetectContext`) and reads only the
tagged subset. Talks to `hotkeys`, `engine_panels`/`engine_subscreen` (world
pause + panel-kind detection), `unified_action_menu`, and `strings`. Gotcha:
`FmtHelpNumberActions` is composed at `BuildRows` time from `MenuCat*` strings
so the bare-1..7 help line can never drift from the action menu's own labels.

## Declarations (in source order)

- L34 — `enum CtxBit : uint32_t { kWorld, kMenu, kMap, kActionMenu, kDialog, kContainer, kStore }`
- L44 — `enum class Context { World, Menu, Map, ActionMenu, Dialog, Container, Store }`
- L46 — `uint32_t ContextBit(Context c)`
- L59 — `S ContextNameId(Context c)`
- L74 — `Context DetectContext()`
  note: priority order — action menu > dialog > container/store > map > generic menu-block > world; title/loading screen falls to Menu
- L93 — `enum class Grp { General, Movement, Interaction, Combat, Exploration, Screens, Map, Mod, COUNT }`
- L98 — `constexpr S kGroupHeader[]` + static_assert covering every Grp
- L107 — `struct Entry { S label; Grp grp; uint32_t ctx; bool composed; }`
- L118 — `constexpr Entry kEntries[]`
  note: single source of truth; F1 always lists every entry, ctx=0 entries never spoken by Ctrl+F1 (F1/Ctrl+F1 self, strafe, mod settings, etc.)
- L193 — `constexpr int kMaxRows = 96`
- L195 — `struct Row { bool isHeader; bool composed; S label; char text[224]; int entryPos; }`
- L203 — `struct State { bool open; bool pausedWorld; int focus; int rowCount; int entryTotal; Row rows[kMaxRows]; }`
- L217 — `void BuildComposedText(S formatId, char* out, size_t cap)`
  note: only FmtHelpNumberActions composes today — pulls the 7 MenuCat* names
- L234 — `void BuildRows()` — flattens kEntries into header+entry rows, skipping empty groups
- L273 — `void SpeakRow(int idx, bool interrupt)`
- L291 — `void SpeakContext()` — Ctrl+F1: joins ctx-tagged entries for `DetectContext()`'s screen, or speaks HelpContextNothing
- L331 — `bool IsMenuOpen()`
- L333 — `void OpenMenu()`
  note: pauses world via BeginOverlayPause only when opened from pure in-world context (menus already hold the world)
- L354 — `void CloseMenu()`
- L367 — `void PollWin32()` — F1 toggle, Ctrl+F1 speak, and (while open) Up/Down/Home/End/Enter/Esc list nav, each Consume()d
- L421 — `void Tick()` — self-disarm: closes silently if world drops out mid-open (area load/teardown) while pausedWorld held
