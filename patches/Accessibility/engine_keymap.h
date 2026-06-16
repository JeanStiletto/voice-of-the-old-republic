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
// is not in the table. Covers only the hardcoded quick keys; use IsKeyUsedByGame
// for the complete picture.
int CodeForVk(int vk);

// True iff `vk` is bound to ANY game action — a hardcoded quick key (CodeForVk)
// OR a player-configurable bind read from swkotor.ini [Keymapping]. This is the
// unified "is this physical key used by the game" query the mod keybind
// configurator warns on. Auto-loads the config on first use.
bool IsKeyUsedByGame(int vk);

// (Re)read the configurable [Keymapping] binds from swkotor.ini. Called at
// startup (Rebuild) and when fresh game-side state is wanted (e.g. opening the
// configurator, since the player may have changed game binds since launch).
void ReloadGameConfig();

// Resolve a DirectInput scancode to a Win32 VK against the active layout. Used
// internally for the hardcoded command-code table (those are DIK scancodes).
int ScancodeToVk(int scancode);

// Resolve an engine InputIndices value (KEYBOARD_*/MOUSE_* — what the keymap
// row's key_code and swkotor.ini [Keymapping] actually store) to a Win32 VK.
// Exposed for the Key Mapping screen accessibility layer, which reads a row's
// captured key_code and needs the VK to test it against mod bindings.
int InputIndexToVk(int inputIndex);

}  // namespace acc::engine_keymap
