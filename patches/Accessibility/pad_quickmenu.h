// KOTOR 1: the Y button's quick menu.
//
// KOTOR 2 ships one — `CSWGamepadMenuIos`, an eight-entry overlay the pad opens
// with Y — and it turned out to be an ordinary panel the mod's navigation chain
// can walk, so on that game there is nothing to build. KOTOR 1 has no such
// panel and no pad at all, so the mod supplies the surface itself.
//
// Same shape, one entry shorter: KOTOR 1 has no Switch Weapons. What is left is
// the list of things a player wants without leaving the world — the menu
// screens, the party leader, solo mode, stealth, a quick save — plus Help,
// which on KOTOR 2 the mod also claims because the engine's own Help entry
// opens a picture of a controller.
//
// Every game entry fires by synthesising the player's OWN bound key for that
// [Keymapping] action (engine_keymap::GameActionScancode + key_inject), so a
// rebind is honoured and nothing here needs to know how the engine performs
// the action. Help is the exception: it opens the mod's key list directly.
//
// Deliberately does NOT pause the world. It is an overlay over a running game,
// exactly as it is on KOTOR 2, and the entries are one-shot actions rather than
// something to deliberate over. The left stick IS stood down while it is open
// (see pad_movement), so the player cannot walk off mid-choice.
//
// Modelled on the F1 help list (help.cpp): an in-DLL overlay with no engine
// panel of its own, navigated by whatever routes codes into it. Up/Down clamp
// at the ends rather than wrapping — the repeated line is the boundary cue,
// the same rule every submenu in this mod follows.

#pragma once

namespace acc::pad::quickmenu {

// True while the list is up and owns the pad's navigation.
bool IsOpen();

void Open();
void Close();
void Toggle();

// Drive the list from the pad seam. `code` is an engine logical nav code
// (kInputNavUp/Down, kInputEnter1, kInputEsc1). Returns false when the list is
// closed or the code is not one it owns, so the caller can route on.
bool HandleNavCode(int code);

// Self-disarm: closes if the world drops out from under it (area load,
// teardown). Cheap when closed.
void Tick();

}  // namespace acc::pad::quickmenu
