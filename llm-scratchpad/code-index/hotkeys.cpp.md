# hotkeys.cpp (504 lines)

Hotkey registry implementation. Default bindings table (g_bindings), per-action edge state (g_edge: now/last/claimed), modifier sampling (ReadModifiers reads VK_SHIFT/CONTROL/MENU/RMENU), binding match logic (BindingMatches). InitDefaults fills the binding table in Action enum order. Describe() uses rotating 4-slot buffer.

## Declarations (in source order)

- L12 — `namespace acc::hotkeys`
- L102 — `static bool IsDownVk(int vk)` (anonymous namespace)
- L112 — `static uint32_t ReadModifiers()` (anonymous namespace)
- L131 — `static bool BindingMatches(const Binding& b, uint32_t mods)` (anonymous namespace)
- L150 — `static void InitDefaults()` (anonymous namespace)
  note: fills g_bindings in Action enum order; guarded by g_inited flag
- L296 — `void BeginTick()`
- L304 — `void EndTick()`
  note: shifts now→last; clears claimed flags
- L313 — `bool IsForegroundGame()`
- L321 — `bool Pressed(Action a)`
- L330 — `bool Held(Action a)`
- L337 — `void Consume(Action a)`
- L346 — `void ClaimRisingEdge(Action a)`
- L353 — `bool ShiftHeld()`
- L357 — `bool CtrlHeld()`
- L361 — `bool AltHeld()`
- L365 — `bool AltGrHeld()`
- L371 — `Binding Get(Action a)`
- L378 — `void Set(Action a, Binding b)`
  note: also resets edge state so a brand-new binding doesn't fire on held keys
- L390 — `bool IsUserRebindable(Action a)`
- L407 — `const char* Name(Action a)`
- L415 — `static const char* VkLabel(int vk)` (anonymous namespace)
  note: maps VK codes to human-readable strings; printable A-Z and 0-9 via thread_local buffers
- L476 — `const char* Describe(Action a)`
