// Shift+H examine list view.
//
// Synthetic in-DLL arrow-navigable list of pre-composed rows. KOTOR 1's
// CSWGuiExamine is a generic TLK message box, not a rich examine panel,
// so we build our own from direct field reads + engine name accessors.
//
// User contract while armed:
//   Open     — captures cycle/LastTarget focus, builds rows, speaks
//              opener + row 0.
//   Up/Down  — step through rows (Name, Faction, HP, Distance, Weapon,
//              Effects×N, Feats×M). HP and distance refresh on step.
//   Enter    — close (read-only, no commit).
//   Esc      — close.
//
// Self-disarms when the target becomes unresolvable.
//
// Input routing lives in interact_hotkey.cpp.

#pragma once

namespace acc::examine_view {

// Localized EFFECT_TYPES → display name. nullptr for unmapped types.
// Shared with combat_query's Q/E brief.
const char* EffectName(int type);

bool Open();
bool IsActive();

// Press-edge only (value != 0). Called after the queue / actionbar gates.
bool HandleInputEvent(int code, int value);

void ForceDisarm(const char* reason);
void Tick();
void PollWin32Hotkey();

}  // namespace acc::examine_view
