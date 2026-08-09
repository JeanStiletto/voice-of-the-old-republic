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

// The VK currently bound to a slotless [Keymapping] game action ("Action<id>="
// lines, e.g. 207 = Solo Mode — keymap.2da row + 200). 0 when the ini has no
// such line (the game then uses the keymap.2da default) or the ini is
// unreadable. Auto-loads the config on first use.
int GameActionVk(int actionId);

// The same bind as a DIK scancode, for a caller that has to SYNTHESISE the key
// rather than test it — the KOTOR 1 pad's quick menu drives Solo Mode, Stealth,
// Quick Save and the rest this way (the engine reads DirectInput, which sees
// scancodes only). 0 when the ini has no such line OR when the bound key is one
// this mod refuses to inject (Caps Lock — see InputIndexToScancode). A caller
// that gets 0 must fall back to its own default or decline the entry.
int GameActionScancode(int actionId);

// Resolve a DirectInput scancode to a Win32 VK against the active layout. Used
// internally for the hardcoded command-code table (those are DIK scancodes).
int ScancodeToVk(int scancode);

// Resolve an engine InputIndices value (KEYBOARD_*/MOUSE_* — what the keymap
// row's key_code and swkotor.ini [Keymapping] actually store) to a Win32 VK.
// Exposed for the Key Mapping screen accessibility layer, which reads a row's
// captured key_code and needs the VK to test it against mod bindings.
int InputIndexToVk(int inputIndex);

// ---- Movement / turn axis queries ------------------------------------------
// The player's bound movement + turn keys, so the mod can react to "the player
// is moving / turning" and steer the map cursor by the same keys the player
// uses in the world — independent of which physical keys they've bound (turning
// off A/D no longer breaks anything). Resolved from swkotor.ini [Keymapping];
// each axis is always seeded with its WASD default so it is never empty.
enum class MoveAxis { Forward = 0, Backward = 1, TurnLeft = 2, TurnRight = 3 };

// Fill `out` with up to `cap` distinct VKs bound to the given movement/turn
// direction (default key + any [Keymapping] binds on that direction's slots).
// Returns the count written. Auto-loads the config on first use.
int MoveAxisVks(MoveAxis axis, int* out, int cap);

// The single VK a consumer should treat as "the" key for this direction, when
// reacting to every bound key would be wrong.
//
// MoveAxisVks above returns the whole union, which includes KOTOR's arrow-key
// alternates (Action285/286) — the game binds Up/Down/Left/Right to movement
// as a second set alongside W/S and the strafe pair. That is correct for
// "is the player moving?", but a screen where the arrows have their own job
// must not have them swallowed: on the in-game map, one Down press both panned
// the virtual cursor south (this union, polled) and stepped the menu chain
// (the engine's nav code, dispatched) — two actions from one key.
//
// Primary = the walk axes, Action280 (move forward/back) and Action281
// (strafe left/right), falling back to the WASD default when the ini has no
// bind. Strafe rather than camera-rotate because panning a map IS a strafe,
// and because it lands on the ergonomic cluster the installer writes
// (strafe -> A/D, camera rotate -> Z/C; see docs/controls-and-input.md). On a
// vanilla binding set the same rule yields W/S + Z/C — still the player's own
// movement cluster, which is the promise this whole API exists to keep.
// Never returns the arrow alternates. Auto-loads the config on first use.
int MoveAxisPrimaryVk(MoveAxis axis);

// The same primary bind as a DIK scancode. KOTOR 1 has no gamepad path in the
// engine at all — its DirectInput layer creates a keyboard and a mouse device
// and nothing else — so the pad's left stick MOVES the character by holding
// these keys down (pad_movement.cpp). Falls back to the WASD DIK when the ini
// has no bind, mirroring MoveAxisPrimaryVk's fallback exactly.
int MoveAxisScancode(MoveAxis axis);

// DIK scancode to synthesise (via SendInput KEYEVENTF_SCANCODE) to drive the
// engine's camera-turn axis in the given direction — resolved from the
// player's configured primary turn bind (Action283/284), so it follows a
// rebind. Falls back to DIK A (left, 0x1E) / D (right, 0x20) when the ini has
// no turn bind (vanilla defaults / unreadable ini). Used by camera_orient's
// N-hotkey snap-turn. Auto-loads the config on first use.
int TurnScancode(bool left);

// True iff any bound movement/turn key is currently held down. Union of all
// four directional buckets plus the legacy German-layout extras (Z/C/Y), so it
// never regresses below the old hardcoded {W,S,A,D,C,Y} movement check.
bool AnyMovementKeyHeld();

// Same scan, but returns the VK that answered (0 when none). Diagnostic twin of
// AnyMovementKeyHeld: a key that is stuck down in the OS async state cancels
// every mod-armed walk on arrival, and the log needs to NAME it — "the walk was
// cancelled" without the culprit is a dead end.
int FirstMovementKeyHeld();

// True iff a bound forward/backward key is currently held (the walk axes,
// including the arrow alternates) — deliberately EXCLUDING the turn/strafe
// buckets. This is the "the player is commanding translation" signal: the
// droid drive-loop suppression uses it so that pivoting in place (turn keys
// only, legitimately audible servo) can never read as wall-grinding.
bool ForwardBackwardKeyHeld();

// ---- Device-independent movement queries ------------------------------------
// The two above ask about KEYS, which is what this module knows about. What
// their consumers actually mean is "is the player commanding movement right
// now", and on KOTOR 2 that can also be the gamepad's left stick — which holds
// no key, so the keyboard queries answer no while the character walks.
//
// The union lives here, once, rather than at each call site: there are only
// two consumers today (drive-loop suppression, autowalk cancel) and both want
// the same answer, so a shared "…|| pad" at both would be the same knowledge
// written twice. Identical to the key-only queries on KOTOR 1.
bool ForwardBackwardCommanded();
bool AnyMovementCommanded();

}  // namespace acc::engine_keymap
