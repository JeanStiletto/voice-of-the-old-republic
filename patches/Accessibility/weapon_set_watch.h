// Weapon-set swap announcement (KOTOR 2 only).
//
// KOTOR 2 creatures carry a second weapon pair (inventory slots 18/19,
// "Konfig 2" on the equip screen) and a SwitchWeaps action that swaps it
// with the active pair (slots 4/5) — the game key the installer moves from
// H to the physical Ö/semicolon key, the equip screen's BTN_SWAPWEAPONS,
// and scripted ActionSwitchWeapons. The engine stores NO "active set"
// flag: the swap physically exchanges the items between the two slot
// pairs (RE trail in docs/kotor2-port.md, "Weapon-set swap batch"), so
// the observable truth is the cross-exchange of the four slot handles.
//
// This module polls those four handles on the active leader each tick and
// speaks what the leader now holds ("Waffen gewechselt: <main hand>
// [und <off hand>]", FmtWeaponSwitched*) when a cross-exchange lands.
// A normal re-equip from inventory never produces the exchange pattern
// (the displaced item returns to the backpack, not to the other pair),
// so ordinary gear changes stay silent. Silent baseline on leader change.
//
// Inert on KOTOR 1: the game has no second weapon pair (the slot offsets
// are Kotor2Only). Reads only; no hooks — driven from the per-tick
// dispatcher.
#pragma once

namespace acc::weapon_set_watch {

void Tick();

}  // namespace acc::weapon_set_watch
