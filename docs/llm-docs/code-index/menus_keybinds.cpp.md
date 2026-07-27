# menus_keybinds.cpp (405 lines)

Entirely virtual "Tastenbelegung" mod-keybind configurator (no engine panel) reached from the Mod-settings submenu. Two-level nav: Category level (5 fixed categories — World/Exploration/Menus/Minigames/General — plus a synthetic "restore defaults" row) then Action level (每 row reads "{name}: {bind}"). Enter on an action arms capture (`ArmCapture`); `Tick()` then Win32-polls for the next physical key, checks for a mod-vs-mod clash (`FindConflict`) and a bare game-key clash (`engine_keymap::IsKeyUsedByGame`), and either warns + stays armed or commits via `hotkeys::SetUserBinding`. Talks to `hotkeys` (Action enum, bindings), `engine_keymap`, `prism`, `strings`.

## Declarations (in source order)

- L30 — `struct ActionEntry { action, name }`
- L32-105 — `constexpr ActionEntry kWorld[]/kExploration[]/kMenus[]/kMinigames[]/kGeneral[]` — the 5 catalogue arrays; every user-rebindable Action appears exactly once
- L114 — `struct Category { name, entries, count }`; `template<int N> constexpr Category MakeCat(name, arr)`
- L121 — `const Category kCategories[5]`
- L130 — `constexpr int kResetIndex, kCatLevelCount`
- L134 — `enum class Level { Categories, Actions }`
- L136-143 — state: `g_open, g_level, g_catCursor, g_curCat, g_actCursor, g_capturing, g_snap[256]`
- L145 — `const ActionEntry* FindEntry(A action)` (anonymous ns)
- L155 — `void SpeakCategory(interrupt)` / L164 `void SpeakActionRow(interrupt)`
- L175 — `void SnapshotKeys()` — baseline GetAsyncKeyState for all 256 VKs
- L184 — `bool IsCandidateVk(vk)` — excludes mouse buttons, modifier keys, Win keys, CapsLock
- L199 — `void ArmCapture()` — snapshots keys (ignores the arming Enter), speaks capture prompt
- L214 — `void ApplyCapturedKey(vk)` — checks mod-clash then game-bind clash before committing via SetUserBinding
- L269 — `const char* acc::menus::keybinds::DisplayName(A action)`
- L274 — `bool acc::menus::keybinds::IsOpen()`
- L276 — `void acc::menus::keybinds::Open()` — reloads engine keymap config for fresh conflict data, announces title + first category
- L291 — `void acc::menus::keybinds::Reset()` — force-close without speech
- L300 — `bool acc::menus::keybinds::HandleInput(int keyCode)` — category-level nav/drill/reset/close; action-level nav/arm-capture/undrill
- L378 — `void acc::menus::keybinds::Tick()` — capture state machine: Esc cancels first, then scans VK 8..255 for a fresh press edge (one capture per tick)
