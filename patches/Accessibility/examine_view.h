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

#include <cstddef>

namespace acc::examine_view {

// Localized EFFECT_TYPES → display name. Nullptr for unmapped types.
// Shared with combat_query's Q/E brief.
const char* EffectName(int type);

// Look up a feat's localized name by feat_index (CSWFeat[] index, ushort).
// Drives CSWRules::GetFeat → CSWFeat::GetNameText with SEH guards. Returns
// true and writes a null-terminated string when the engine resolves a
// non-empty name. False on any miss / fault. Shared with combat::queue's
// row speech (which decodes action_type=11 entries via feat_id @+0x5c).
bool ResolveFeatName(unsigned short featIdx, char* outBuf, size_t outBufSize);

// Look up a spell / force power's localized name by spell_id (int).
// Drives Rules->spells → CSWSpellArray::GetSpell → CSWSpell::GetSpellNameText
// with SEH guards. Same contract as ResolveFeatName. Used to decode
// action_type=9 (Cast Force Power) queue entries via spell_id @+0x24.
bool ResolveSpellName(int spellId, char* outBuf, size_t outBufSize);

bool Open();
bool IsActive();

// Press-edge only (value != 0). Called after the queue / actionbar gates.
bool HandleInputEvent(int code, int value);

void ForceDisarm(const char* reason);
void Tick();
void PollWin32Hotkey();

}  // namespace acc::examine_view
