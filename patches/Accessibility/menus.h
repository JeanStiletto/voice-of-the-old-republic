// Public surface of menus.cpp — entry points core_tick calls.

#pragma once

namespace acc::menus {

// First per tick — drop pointers the engine may have freed (currently
// the tabbed-panel cluster pointer).
void ValidatePanels();

// Focus-text / panel-contents / dialog-reply / container-loot /
// equip-picker selection deltas + container give-mode G-key poll.
void TickMonitors();

// Home/End rising edges. Engine keymap drops these (no kotor.ini
// [Keymapping] action), so we synth-dispatch through OnHandleInputEvent.
void PollHomeEndKeys();

// LAST per tick so no monitor sees a partially-applied state.
void TickPendingOps();

// OnSetActiveControl writes the slot per panel-focus event; this drains
// it once. Last-write-wins collapses intra-tick SetActive cascades to a
// single announcement (e.g. MessageBox init firing NULL → OK → Abbrechen
// back-to-back).
void DrainPendingAnnounce();

// Drops the slot without speaking — for subsystems that announced focus
// in their own format (e.g. editbox "Editbox. <value>") and want to
// suppress the plain re-announce.
void ClearPendingAnnounce();

// Channel-keyed dedup shared by panel-focus drain (ch 0) and listbox-row
// hook (ch 1). Non-static so the focus monitor's voluntary
// AnnounceControl can prime ch 0's cache via MarkSpoken — stops the
// engine's post-nav SetActive echo from re-announcing.
void SpeakIfChanged(int channel, const char* text);
void MarkSpoken(int channel, const char* text);

// g_drilledIntoSubScreen retargets arrow-key nav from the InGameMenu
// strip (engine's strip-stays-fg pattern) to the visible sub-screen.
// Originally armed only on strip-icon Enter; direct-open paths (Esc,
// M, I, J) bypass that arm and leave chain nav pointing at the strip
// instead of the sub-screen. The SubScreen monitor auto-arms drill on
// any newly-detected sub-screen.
bool IsDrilledIntoSubScreen();
void SetDrilledIntoSubScreen(bool drilled);

}  // namespace acc::menus
