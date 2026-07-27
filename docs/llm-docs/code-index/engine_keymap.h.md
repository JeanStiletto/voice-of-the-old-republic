# engine_keymap.h (103 lines)

Engine keybinding table: maps hardcoded engine command codes back to physical-key VKs (so input hooks can detect when a modifier-using mod hotkey shadows an engine-bound bare key), plus configurable [Keymapping] parsing and movement/turn-axis VK queries. Codes here are the engine's internal "quick action" COMMAND namespace, NOT swkotor.ini [Keymapping] ActionNNN ids (confirmed distinct: command 214=Equip/U but Action214=F4).

## Declarations (in source order)

- L37 — `void Rebuild()` — idempotent; call once at startup (OnRulesInit)
- L42 — `int VksForCode(int code, int* out, int cap)` — auto-builds on first use
- L47 — `int CodeForVk(int vk)` — hardcoded quick keys only; use IsKeyUsedByGame for the full picture
- L53 — `bool IsKeyUsedByGame(int vk)` — the unified "used by the game" query the keybind configurator warns on
- L58 — `void ReloadGameConfig()` — re-reads [Keymapping]; player may have changed binds since launch
- L64 — `int GameActionVk(int actionId)` — 0 when the ini has no such line (keymap.2da default applies)
- L68 — `int ScancodeToVk(int scancode)`
- L74 — `int InputIndexToVk(int inputIndex)` — exposed for the Key Mapping screen accessibility layer
- L82 — `enum class MoveAxis { Forward=0, Backward=1, TurnLeft=2, TurnRight=3 }`
- L87 — `int MoveAxisVks(MoveAxis axis, int* out, int cap)` — always WASD-seeded, layered with configured binds
- L95 — `int TurnScancode(bool left)` — resolved from Action283/284; falls back to DIK A(0x1E)/D(0x20)
- L100 — `bool AnyMovementKeyHeld()` — union of axis buckets + legacy Z/C/Y German-layout extras
