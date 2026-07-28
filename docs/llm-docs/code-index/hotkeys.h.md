# hotkeys.h (225 lines)

Central hotkey registry — every mod-added binding is one `Action` enum value
with a runtime-mutable `Binding` (vk + altVk + required/forbidden modifier
masks). Built because the engine's `[Keymapping]` is bare DIK→Action with no
modifier combos or AltGr semantics. Pollers call `Pressed(Action)` for
rising-edge + modifier + foreground match; dispatch decides what to do.
Bindings are rebindable via `Set`/`SetUserBinding` for the configurator.

## Declarations (in source order)

- L18 — `enum ModifierBit : uint32_t { kModShift, kModCtrl, kModAlt, kModAltGr }`
  note: AltGr = VK_RMENU; Windows synthesises a phantom Ctrl alongside it on QWERTZ, so Ctrl-required bindings forbid AltGr to dodge double-fire
- L27 — `enum class Action : int { ... COUNT }`
  note: ~75 actions grouped by comment header (world interaction, submenu nav, container/store modes, in-world cycle, orientation/party, view mode, editbox modal, help, updater, diagnostic probes, Pazaak, turret, dialog); order must match hotkeys.cpp's kActionNames and InitDefaults exactly
- L137 — `struct Binding { int vk; int altVk; uint32_t modsRequired; uint32_t modsForbidden; }`
- L145 — `void BeginTick()` / `void EndTick()` — call at start/end of every per-frame dispatch (core_tick::Dispatch)
- L150 — `bool Pressed(Action a)` — rising edge + modifiers + KOTOR foreground, idempotent within a tick
- L154 — `bool Held(Action a)` — pure held-state, no edge/foreground gate
- L159 — `void Consume(Action a)` — suppress this tick's already-fired edge for other consumers
- L165 — `void ClaimRisingEdge(Action a)`
  note: for sites firing before BeginTick on the tick the edge lands (manager-level HandleInputEvent hook window)
- L168 — `bool ShiftHeld()` / `CtrlHeld()` / `AltHeld()` / `AltGrHeld()` — direct OS reads, no edge/foreground gate
- L175 — `uint32_t CurrentModifiers()`
- L183 — `bool ModifiedComboOwns(int vk)`
  note: input hooks use this to decide whether to swallow the engine's modifier-blind bare-key action because a mod-owned combo was actually pressed
- L185 — `bool IsForegroundGame()`
- L189 — `Binding Get(Action a)` / `void Set(Action a, Binding b)` / `bool IsUserRebindable(Action a)`
- L195 — `Binding GetDefault(Action a)` — factory default for the configurator's reset path
- L199 — `void SetUserBinding(Action a, Binding b)` — updates live binding + persists to acc_settings.ini
- L203 — `void ResetUserBindings()` — resets every rebindable action + rewrites persisted overrides
- L211 — `Action FindConflict(Action self, int vk, uint32_t mods)` — first other action that would double-fire on this combo
- L215 — `const char* Name(Action a)` / `const char* Describe(Action a)` — Describe uses a rotating static buffer
- L222 — `const char* VkLabel(int vk)` — spoken/printable label for a bare VK, exposed for game-binding announces (e.g. floor-puzzle solo key)
