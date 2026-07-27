# hotkeys.cpp (763 lines)

Implementation of the central hotkey registry (see hotkeys.h). Holds the
default-binding table (`g_bindings`/`g_defaults`, indexed by `Action`), OS
modifier reads, per-Action rising-edge state (`g_edge[]`, with a one-shot
`claimed` guard for the manager-hook input window), and user-rebind
persistence through `acc::settings` (key `Bind_<Name>`, format
"vk,altVk,req,forbid"). `InitDefaults()` is the single lazy-init gate every
public entry point routes through; binding order must track the `Action`
enum exactly or presses silently misfire. Talks to `mod_settings_store` (user
rebind persistence) and `log.h` (`PatchDir` gates when overrides load).

## Declarations (in source order)

- L25 — `Binding g_bindings[Action::COUNT]` — the live table every call site reads
- L31 — `Binding g_defaults[Action::COUNT]` — frozen factory defaults for reset
- L43 — `struct EdgeState { bool now; bool last; bool claimed; }` + `g_edge[]`
  note: `claimed` lets sites firing between EndTick/BeginTick (manager hook window) pre-suppress a rising edge Consume() can't reach yet
- L53 — `const char* const kActionNames[]` — must stay in sync with the Action enum, one row per action
- L131 — `bool IsDownVk(int vk)`
- L141 — `uint32_t ReadModifiers()` — Shift/Ctrl/Alt/AltGr from GetAsyncKeyState, AltGr tracked independently from Alt
- L160 — `bool BindingMatches(const Binding& b, uint32_t mods)`
- L179 — `void InitDefaults()`
  note: every default binding assignment for all ~75 Actions; comments document key-collision precedence (forbidden-mod carve-outs) per binding — see file for the full table
- L408 — `void BindKey(Action a, char* out, size_t cap)` — "Bind_<Name>" settings key
- L412 — `void SerializeBinding` / L419 `bool ParseBinding` — "vk,altVk,req,forbid" round-trip
- L437 — `void EnsureOverridesLoaded()`
  note: no-op until PatchDir() resolves; retries every BeginTick rather than locking in defaults on an early call
- L465 — `void BeginTick()` — InitDefaults + EnsureOverridesLoaded + sample `now` edges from live modifiers
- L474 — `void EndTick()` — shift now→last, clear claimed
- L483 — `bool IsForegroundGame()`
- L491 — `bool Pressed(Action a)` — rising edge AND foreground AND not claimed
- L500 — `bool Held(Action a)`
- L507 — `void Consume(Action a)` — forces last==now for the rest of this tick
- L516 — `void ClaimRisingEdge(Action a)` — pre-claims the NEXT rising edge for pre-BeginTick callers
- L523 — `bool ShiftHeld()` / `bool CtrlHeld()` / `bool AltHeld()` / `bool AltGrHeld()` / `uint32_t CurrentModifiers()`
- L543 — `bool ModifiedComboOwns(int vk)`
  note: deliberately does NOT re-check GetAsyncKeyState(vk) — called from engine hooks where the event itself proves the key was pressed, sidestepping a quick-tap release race
- L571 — `Binding Get(Action a)` / L578 `void Set` / L590 `GetDefault` / L597 `SetUserBinding` / L608 `ResetUserBindings`
- L624 — `Action FindConflict(Action self, int vk, uint32_t mods)` — configurator double-fire scan
- L644 — `bool IsUserRebindable(Action a)` — excludes diagnostic probes + CameraStateProbe
- L664 — `const char* Name(Action a)`
- L670 — `const char* VkLabel(int vk)` — printable/spoken label table for common VKs, falls back to raw char or "?"
- L725 — rotating `g_describeBufs[4][32]` for `Describe()`
- L734 — `const char* Describe(Action a)` — builds "Ctrl+AltGr+Shift+Key" style string from a binding
