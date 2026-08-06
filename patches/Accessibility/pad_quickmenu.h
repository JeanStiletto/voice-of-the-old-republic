// KOTOR 2 Quick Menu (the pad's Y button) — spoken navigator.
//
// `CSWGamepadMenuIos` is one of the three iOS-derived overlays Aspyr added for
// the gamepad. Its eight entries are custom textured quads with their own draw
// and hit handling, held in fixed arrays — they are NOT `CSWGuiControl`s, so
// the mod's generic navigation chain (which walks a panel's
// CExoArrayList<CSWGuiControl*>) cannot see a single one of them. The menu is
// therefore completely silent without a dedicated navigator, which is what
// this is: read the engine's own selection index, speak the entry.
//
// Same shape as the Abilities-screen handler — read the index, speak the
// label — rather than any attempt to re-implement the menu.
//
// Entry order is fixed by the engine and is the API:
//   0 Menus, 1 Party Leader, 2 Solo/Party, 3 Stealth,
//   4 Quick Save, 5 Free Look, 6 Switch Weapons, 7 Help
// Entry 1 expands into a three-item sub-list (the party slots). Entry 3 is
// skipped by the engine when its gate is clear — we read the live index, so
// a skipped entry needs no handling here.
//
// The labels come from OUR string tables, not from the engine's
// `override/gamepad.txt`: that file is read by line index in a fixed
// five-language block, so it would speak whatever language the GAME is in
// rather than the language the mod is set to, and it would need the file
// present and unmodified. The eight entries are fixed engine functionality, so
// naming them ourselves costs nothing and keeps every spoken string in one
// place.
//
// Engine reference: docs/llm-docs/k2-controller-support.md.

#pragma once

namespace acc::pad::quickmenu {

// The Y button arrived at the GUI manager while in the world. Arms the
// navigator; the next Tick speaks whatever entry the engine landed on.
void NoteOpened();

// The menu is gone — A activated an entry, or B cancelled. Idempotent.
void NoteClosed();

// True while armed. The pad's in-world D-Pad bindings check this and stand
// down: while this menu is up the D-Pad belongs to the engine, which does its
// own navigation, and we only speak the result.
bool IsArmed();

// Per-tick. Reads the engine's selection / sub-selection indices and speaks
// on change. Cheap and silent when disarmed; self-gates to KOTOR 2.
void Tick();

}  // namespace acc::pad::quickmenu
