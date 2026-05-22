// Combat system, Phase 2C v2 — Shift+H examine list view.
//
// A synthetic, in-DLL arrow-navigable list. KOTOR 1's engine has no rich
// creature-examine panel (CSWGuiExamine is a generic TLK-message-box,
// verified 2026-05-22 from the SetMessage decomp), so we build our own
// list of pre-composed rows from direct field reads + engine accessors
// for feat/effect names.
//
// User contract while armed:
//   Open       — captures the cycle / LastTarget focus, builds rows,
//                speaks "Untersuchen: <name>. N Einträge." opener,
//                speaks row 0.
//   Up / Down  — steps through rows (Name, Faction, HP, Distance,
//                Weapon, Effects×N, Feats×M). Each step speaks the
//                row text + position.
//   Enter      — closes the view (no commit action — read-only).
//   Esc        — closes the view.
//
// Self-disarm: closes when the target becomes unresolvable. HP and
// distance refresh on each Up/Down step (cheap direct field reads).
//
// Input routing lives in interact_hotkey.cpp (same pattern as combat
// queue submenu).

#pragma once

namespace acc::examine_view {

// Localized EFFECT_TYPES enum → display name. Returns nullptr for
// unmapped types so callers can decide between "Effekt #N" fallback and
// silently skipping. Lives here so combat_query's Q/E brief can reuse
// the same table that drives the Shift+H examine row.
const char* EffectName(int type);

// Open the view over the user's current cycle / LastTarget focus.
// Returns true on success; false on no-target / target unresolvable.
// On false the caller should speak ExamineNoTarget / ExamineFailed.
bool Open();

// True when the view is armed.
bool IsActive();

// Manager-level input gate. Called from interact_hotkey's poll AFTER
// the queue / actionbar gates. Press-edge only (value != 0).
bool HandleInputEvent(int code, int value);

// Forced disarm. Called when target becomes unresolvable mid-session.
void ForceDisarm(const char* reason);

// Per-tick self-disarm probe. Called from core_tick.cpp.
void Tick();

// Win32 polling for the open hotkey (default Shift+H — shared with the
// older one-shot Examine hotkey; that path is now replaced by Open here).
void PollWin32Hotkey();

}  // namespace acc::examine_view
