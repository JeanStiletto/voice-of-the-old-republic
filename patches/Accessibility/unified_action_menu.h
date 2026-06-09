// Unified in-world action menu — one navigable menu over BOTH the engine's
// target-action rows (CSWGuiTargetActionMenu, 3 categories) and the player's
// personal action bar (CSWGuiMainInterface, up to 4 populated categories).
//
// Replaces the former three components (radial_menu / target_action_menu /
// actionbar_menu): blind users don't need the visual target-vs-personal
// split the engine renders, so we present one menu whose categories carry
// the distinction by name.
//
// Navigation (identical for every category):
//   Left / Right        — previous / next category (clamp at the ends)
//   Up / Down           — previous / next entry within the category (clamp)
//   Home / End          — first / last entry of the current category
//   Ctrl+Home / Ctrl+End— first / last category
//   Shift + any arrow   — speak the selected entry's full description, no move
//   Enter               — fire the selected entry; Esc — cancel
//
// Category order, left → right: the target block first (so Shift+Enter lands
// on Attacks for a hostile creature), then the personal block. Empty
// categories are skipped.
//
// Entry points:
//   ArmFromRadial  — Shift+Enter (the picker has already opened the radial).
//   OpenTarget     — Shift+1/2/3 (jump to a target category).
//   OpenPersonal   — Shift+4/5/6/7 (jump to a personal column).
// Bare 1..7 are NOT handled here — the engine fires those instantly; only
// the Shift gestures open this menu.
//
// Engine read/primitive layers live in engine_radial (target) and
// engine_actionbar (personal); this module only orchestrates + speaks.

#pragma once

#include <cstdint>

namespace acc::unified_menu {

// Open on a specific TARGET category (Shift+1/2/3, row 0..2). Requires a
// focused (narrated) target; speaks "kein Fokus" and returns false when
// none, or the empty-category phrase when that row has no actions. Does
// NOT silently fall through to the personal block — Shift+1/2/3 are the
// explicit target openers.
bool OpenTarget(int row);

// Open on a specific PERSONAL column (Shift+4/5/6/7, col 0..3). Always
// available; refreshes against the narrated target (if any) so Left can
// still cross into the target block.
bool OpenPersonal(int col);

// Arm from the Shift+Enter radial path. The picker (engine_picker::Drive
// with forceRadial) has already run PopulateMenus against `targetHandle`.
// `name` is the target name for the "Aktionsmenü, X" pre-roll. Opens on the
// first populated TARGET category. Returns false — and arms nothing — when
// no target category is populated, so the caller can speak the exact
// existing "keine Aktionen … Enter" message (no regression).
bool ArmFromRadial(const char* name, uint32_t targetHandle);

bool IsActive();

// True while armed but suspended under a blocking engine panel (a MessageBox
// or a hotkey-opened sub-screen). The menu keeps its state + world pause but
// stops owning input so the panel handles its own keys; it resumes at the
// same position when the panel closes. Callers route input only when active
// AND not suspended.
bool IsSuspended();

// Per-tick panel-stack notification, driven by interact_hotkey. `blocked` is
// the current IsForegroundUiBlocking() state. Suspends on the rising edge and
// resumes (re-asserting pause + re-speaking the current category) on the
// falling edge; a no-op when the state is unchanged or the menu is inactive.
void SetForegroundBlocked(bool blocked);

// Routed by interact_hotkey's Win32 poll. `code` is one of the engine
// logical nav codes (kInputNav*, kInputEnter*, kInputEsc*, kInputHome/End)
// or the internal kInputCatFirst / kInputCatLast. `value` 0 = release
// (ignored). Returns true when consumed.
bool HandleInputEvent(int code, int value);

void ForceDisarm(const char* reason);

// Bare-key announce support: the user's last-selected index for a personal
// column / target row, kept in lock-step with the engine's per-slot
// selection so a bare 1..7 press announces what the engine actually fires.
int PersonalSelection(int col);
int TargetSelection(int row);

}  // namespace acc::unified_menu
