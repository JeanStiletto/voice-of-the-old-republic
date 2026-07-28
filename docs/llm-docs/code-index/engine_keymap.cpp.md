# engine_keymap.cpp (461 lines)

Maps the engine's hardcoded in-world "quick action" command codes (action-menu digits, quick-menu letters — a SEPARATE namespace from swkotor.ini [Keymapping] ActionNNN ids) to physical-key VKs, resolved against the active keyboard layout, so mod hotkeys know when they're shadowing an engine-bound key. Also parses [Keymapping] itself to build a flat "is this VK used by the game" set, a slotless action→VK lookup, and four movement/turn-axis VK buckets (always WASD-seeded) used by the map cursor and camera_orient's snap-turn. Talks to log.h only.

## Declarations (in source order)

- L18 — `struct Pair { int code; int vk; }`
- L20-23 — `kMaxPairs=128`, `s_pairs[]`, `s_count`, `s_built`
- L29-32 — `kMaxGameVks=256`, `s_gameVks[]`, `s_gameVkCount`, `s_gameLoaded`
- L57-60 — `kMoveAxisCount=4`, `kMaxAxisVks=8`, `s_axisVks[4][8]`, `s_axisVkCount[4]`
- L67 — `int s_turnScan[2]` — DIK scancode per direction for camera_orient's synthetic-key drive, 0=unconfigured
- L71 — `constexpr int kAxisDefaultVk[4] = {'W','S','A','D'}`
- L79-81 — `struct ActionVk { int actionId; int vk; }`, `s_actionVks[160]`, `s_actionVkCount`
- L84-90 — `struct AxisContrib { int axis; int actionId; char slot; }`, `kAxisContribs[]` — maps [Keymapping] Action280/282/283/284/285/286 A/B slots to the 4 axis buckets
- L92 — `bool IsDownVk(int vk)`
- L96 — `void AddAxisVk(int axis, int vk)`
- L112 — `int InputIndexToScancode(int ii)`
  note: direct InputIndex→DIK (not via VK+layout) to avoid the QWERTZ Y/Z swap that broke turn-bind-to-physical-Z
- L140-163 — `struct CodeScan { int code; int scancode; }`, `kEngineCommands[]`
  note: command codes proven empirically distinct from [Keymapping] ids (e.g. command 221 opens Quests via L, but Action221=DIK_O); covers action-menu digits 1-7 (6/7 logically swapped) and quick-menu J/U/L/Q/E
- L165/175 — `void AddPair(int code, int vk)`, `void AddGameVk(int vk)` — both dedup
- L186 — `bool ResolveIniPath(char* out, size_t cap)` — derives swkotor.ini path from acclog::PatchDir()
- L199 — `int ScancodeToVk(int scancode)` (public) — MapVirtualKeyEx against active layout, MapVirtualKey fallback
- L210 — `int InputIndexToVk(int ii)` (public) — full InputIndices→VK table (letters/digits/F-keys/numpad/named keys)
- L272 — `void ReloadGameConfig()` (public)
  note: seeds WASD axis defaults before any early-out so buckets are never empty; parses [Keymapping] lines, routing slotted Action<N>A/B entries into axis buckets (capturing Action284 turn scancode) and slotless entries into s_actionVks
- L368 — `bool IsKeyUsedByGame(int vk)` (public) — hardcoded quick keys OR configurable [Keymapping] binds
- L378 — `int GameActionVk(int actionId)` (public)
- L386 — `void Rebuild()` (public) — builds hardcoded command→VK table + calls ReloadGameConfig
- L402 — `int VksForCode(int code, int* out, int cap)` (public)
- L416 — `int CodeForVk(int vk)` (public) — hardcoded quick keys only
- L424 — `int MoveAxisVks(MoveAxis axis, int* out, int cap)` (public)
- L435 — `int TurnScancode(bool left)` (public) — falls back to DIK A/D (0x1E/0x20) when unconfigured
- L442 — `bool AnyMovementKeyHeld()` (public) — 4 axis buckets plus legacy C/Y/Z extras for German-layout regressions
