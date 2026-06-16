// Engine keybinding table — maps the engine's in-world / GUI-manager command
// codes back to the physical-key VKs that produce them, so the input hooks can
// tell when one of our modifier-using mod hotkeys is shadowing an engine-bound
// key.
//
// Why this exists
// ---------------
// KOTOR reads DirectInput scancodes, which are modifier-blind: pressing
// Shift+4 still makes the engine emit the bare "4" action and fire
// DoPersonalAction. Every mod hotkey that reuses an engine-bound key with a
// modifier (Shift+1..7 open the action submenus, Shift+L opens level-up, …)
// therefore double-fires the engine's bare action. The input hooks consult this
// table on every press: if the live modifier state matches a registered mod
// binding on the physical key the engine code represents, they swallow the
// engine event.
//
// Namespace note
// --------------
// The codes here are the engine's internal "quick action" COMMAND codes (what
// CClientExoAppInternal::HandleInputEvent receives), NOT the swkotor.ini
// [Keymapping] ActionNNN ids — those are a separate namespace (confirmed: command
// 214 = Equip/U, but [Keymapping] Action214 = F4). The shadowed gameplay hotkeys
// (action-menu digits, quick-menu letters) are HARDCODED in the engine — they are
// not exposed in the in-game Key Mapping screen — so they are stable regardless
// of the user's game keybinds. We map command -> DIK scancode from the engine's
// hardcoded handler and resolve scancode -> VK via MapVirtualKeyEx against the
// ACTIVE keyboard layout, so the result matches what the mod's
// GetAsyncKeyState-based bindings see on the same physical key
// (QWERTY/QWERTZ/AZERTY correctness).

#pragma once

namespace acc::engine_keymap {

// (Re)build the table for the active keyboard layout. Call once at startup
// (OnRulesInit); call again if/when layout-change handling is added. Idempotent.
void Rebuild();

// Fill `out` with up to `cap` distinct VKs that, when pressed, make the engine
// emit the given command `code`. Returns the count written (0 for codes no
// mapped engine binding produces). Auto-builds on first use.
int VksForCode(int code, int* out, int cap);

// Reverse lookup: the first engine command code a given VK fires, or 0 if the VK
// is not in the table. For the upcoming free mod-hotkey configurator (warn when
// a chosen key collides with an engine binding).
int CodeForVk(int vk);

}  // namespace acc::engine_keymap
